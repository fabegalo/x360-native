#include <cstdio>
#include "app_controller.h"
#include <algorithm>
#include <fstream>
#include "action_trace.h"
#include "digest_batch.h"
#include "host_platform.h"
#include "app_text.h"
#include "workspace_lease.h"

namespace fs = std::filesystem;
namespace x360n::desktop {
fs::path Payload(const App& a) { return a.self.parent_path() / "app/workspace"; }
fs::path Supervisor(const App& a) {
  const auto name = PathFromUtf8("x360n" + ExecutableSuffix());
  const auto packaged = a.self.parent_path() / "app" / name;
  return fs::is_regular_file(packaged) ? packaged : a.self.parent_path() / name;
}
fs::path Extractor(const App& a) { return Supervisor(a).parent_path() / PathFromUtf8("x360n-extract" + ExecutableSuffix()); }
fs::path Updates(const App& a) { return a.preferences.parent_path() / "updates"; }
void VerifyDistribution(const fs::path& directory) {
#ifdef X360N_DISTRIBUTION
  VerifyPackage(directory, X360N_PACKAGE_PAYLOAD);
#else
  VerifyPackage(directory);
#endif
}
void Post(App& a, std::function<void(App&)> event) {
  { std::lock_guard lock(a.events_mutex); a.events.push_back(std::move(event)); }
  if (a.wake) a.wake();
}
namespace {
void Fail(App& a, const std::string& what) {
  if (!a.worker.joinable()) { a.busy = false; a.session = false; }
  a.result = 3; a.result_title = Tr("Não foi possível concluir", "Could not finish");
  a.activity_text = Tr("Verifique o Diagnóstico e tente novamente. Seus arquivos foram preservados.", "Check Diagnostics and try again. Your files were preserved.");
  a.details = what;
}
template <class F> void Guard(App& a, F&& f) {
  try { f(); } catch (const std::exception& error) { Fail(a, error.what()); }
}
size_t IndexOf(const App& a, const std::string& id) {
  const auto it = std::find_if(a.targets.begin(), a.targets.end(), [&](const auto& t) { return t.id == id; });
  return size_t(it - a.targets.begin());
}
}  // namespace
void Pump(App& a) {
  for (;;) {
    std::function<void(App&)> event;
    {
      std::lock_guard lock(a.events_mutex);
      if (a.events.empty()) return;
      event = std::move(a.events.front()); a.events.pop_front();
    }
    Guard(a, [&] { event(a); });
  }
}
Page PageOf(const App& a) {
  if (a.settings) return Page::Settings;
  if (a.show_details) return Page::Diagnostics;
  if (a.home) return Page::Home;
  if (a.import_view) return Page::Import;
  return a.games ? Page::Library : Page::Dashboard;
}
PageText PageTexts(const App& a) {
  switch (PageOf(a)) {
    case Page::Home: return {Tr("Seu Xbox 360, no PC.", "Your Xbox 360, on PC."),
      Tr("Configure sua dashboard original e adicione seus jogos.\nA experiência de console continua nos menus originais do Xbox 360.", "Set up your original dashboard and add your games.\nThe console experience stays in the original Xbox 360 menus.")};
    case Page::Library: return {Tr("Biblioteca", "Library"),
      Tr("Jogos importados e o estado de preparação de cada um.\nPara jogar, use a My Games dentro da dashboard original.", "Imported games and the preparation state of each one.\nTo play, use My Games inside the original dashboard.")};
    case Page::Import: return {Tr("Importar", "Import"),
      Tr("Adicione uma ISO, pacote Xbox 360, System Update 17559 (ZIP/PIRS) ou pasta extraída.\nO conteúdo é extraído e verificado; seus originais ficam preservados.", "Add an ISO, Xbox 360 package, System Update 17559 (ZIP/PIRS) or extracted folder.\nContent is extracted and verified; your originals stay untouched.")};
    case Page::Dashboard: return {"Dashboard",
      Tr("Versões da dashboard original e o estado de cada preparação.\nCada versão usa somente seus próprios componentes.", "Original dashboard versions and the preparation state of each.\nEach version uses only its own components.")};
    case Page::Settings: return {Tr("Configurações", "Settings"),
      Tr("Renderer gráfico, armazenamento e idioma.\nTrocar uma pasta nunca apaga dados existentes.", "Graphics renderer, storage and language.\nChanging a folder never deletes existing data.")};
    case Page::Diagnostics: return {Tr("Diagnóstico", "Diagnostics"),
      Tr("O diagnóstico fica neste computador. Nada é enviado automaticamente.\nLogs de execução ficam na pasta local do ambiente.", "Diagnostics stay on this computer. Nothing is uploaded automatically.\nExecution logs live in the environment's local folder.")};
  }
  return {};
}
std::string DropText() {
  return Tr("Tudo acontece neste computador. Apenas conteúdos com uma preparação disponível poderão ser executados; nenhum jogo ou dashboard está incluído.", "Everything stays on this computer. Only content with an available preparation can run; no games or dashboards are included.");
}
void SetActivity(App& a, Activity kind, const std::string& title, const std::string& text, std::vector<std::string> stages) {
  a.activity_kind = kind; a.activity_title = title; a.stage_names = std::move(stages);
  a.started = App::Clock::now(); a.extracted_bytes = 0;
  a.activity_text = text;
}
std::string Elapsed(const App& a) {
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(App::Clock::now() - a.started).count();
  char text[32]; std::snprintf(text, sizeof(text), "%02lld:%02lld", (long long)(seconds / 60), (long long)(seconds % 60)); return text;
}
std::string FormatBytes(uintmax_t bytes) {
  char text[64];
  if (bytes >= (1ull << 30)) std::snprintf(text, sizeof(text), "%.2f GB", double(bytes) / (1024.0 * 1024.0 * 1024.0));
  else if (bytes >= (1ull << 20)) std::snprintf(text, sizeof(text), "%.0f MB", double(bytes) / (1024.0 * 1024.0));
  else std::snprintf(text, sizeof(text), "%.0f KB", double(bytes) / 1024.0);
  return text;
}
// Lowercase for search: ASCII and the Latin-1 letters used by Portuguese.
std::string Lower(const std::string& s) {
  std::string out; out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c >= 'A' && c <= 'Z') out += char(c + 32);
    else if (c == 0xC3 && i + 1 < s.size()) {
      auto n = static_cast<unsigned char>(s[i + 1]);
      if (n >= 0x80 && n <= 0x9E && n != 0x97) n = static_cast<unsigned char>(n + 0x20);
      out += char(c); out += char(n); ++i;
    } else out += char(c);
  }
  return out;
}

// ---------- State of listed items ----------
Row StatusOf(const App& a, size_t i) {
  const auto& t = a.targets[i];
  const auto& c = a.checked.at(i);
  if (a.busy && !a.session && !a.preparing_id.empty() && a.preparing_id == t.id) return Row::Preparing;
  if (c.state.verification_pending) {
    // Listing hints only; the full check still runs before preparing or starting.
    if (!t.aot) return Row::Unsupported;
    if (c.state.interrupted) return Row::Error;
    if (!c.state.launch_candidate) return Row::Imported;
    if (!c.state.receipt_current) return c.state.receipt_stale ? Row::Imported : Row::Unchecked;
    for (const auto& dep : t.dependencies) {
      const auto it = std::find_if(a.targets.begin(), a.targets.end(), [&](const auto& d) { return d.id == dep; });
      if (it == a.targets.end()) return Row::Error;
      const auto& state = a.checked.at(size_t(it - a.targets.begin())).state;
      if (!state.receipt_current && !state.prepared) return state.receipt_stale ? Row::Imported : Row::Unchecked;
    }
    // Reuse the preparation receipt for presentation. Starting still completes
    // any pending registration and performs the supervisor's independent check.
    // Do not hash game inventories just to display an already prepared title.
    return c.library.empty() ? Row::Ready : Row::Imported;
  }
  if (!c.state.inputs_ok || c.state.interrupted) return t.kind == "dashboard" && !c.state.interrupted ? Row::Pending : Row::Error;
  if (!t.aot) return Row::Unsupported;
  if (c.state.prepared && c.library.empty()) return Row::Ready;
  return Row::Imported;
}
std::string RowLabel(Row row, bool dashboard) {
  switch (row) {
    case Row::Ready: return Tr(dashboard ? "Pronta" : "Pronto", "Ready");
    case Row::Preparing: return Tr("Preparando", "Preparing");
    case Row::Imported: return Tr("Preparar", "Prepare");
    case Row::Unsupported: return Tr("Não suportado", "Not supported");
    case Row::Pending: return Tr("Pendente", "Pending");
    case Row::Unchecked: return Tr("Conferir", "Check");
    default: return Tr("Erro", "Error");
  }
}
Tone RowTone(Row row) {
  switch (row) {
    case Row::Ready: return Tone::Ok;
    case Row::Preparing: case Row::Unchecked: return Tone::Info;
    case Row::Imported: return Tone::Neutral;
    case Row::Unsupported: case Row::Pending: return Tone::Warn;
    default: return Tone::Err;
  }
}
bool Matches(const App& a, size_t i) {
  if (!a.games) return true;
  if (a.filter != 0) {
    const auto row = StatusOf(a, i);
    const bool ok = a.filter == 1 ? row == Row::Ready :
      a.filter == 2 ? (row == Row::Imported || row == Row::Preparing || row == Row::Unchecked) :
      (row == Row::Error || row == Row::Unsupported || row == Row::Pending);
    if (!ok) return false;
  }
  if (a.query.empty()) return true;
  const auto& t = a.targets[i];
  return Lower(t.label + " " + t.label_en + " " + t.title_id + " " + t.version).find(a.query) != std::string::npos;
}
void RebuildRows(App& a) {
  a.rows.clear();
  const bool pool = a.checked.size() == a.targets.size() && !a.preview_empty;
  if (pool) for (auto i : a.dashboard_indices) if (Matches(a, i)) a.rows.push_back(i);
}
void KeepSelectionVisible(App& a) {
  if (std::find(a.rows.begin(), a.rows.end(), a.selected) == a.rows.end()) a.selected = a.rows.empty() ? a.targets.size() : a.rows.front();
}
// All games use the same persistent import/removal lifecycle.
bool Removable(const App& a) {
  if (a.selected >= a.targets.size() || a.checked.size() != a.targets.size()) return false;
  return InGameLibrary(a.targets[a.selected]);
}

void UpdateRendererControls(App& a) {
  a.fps_text = std::to_string(a.renderer_settings.framerate);
  a.present_text = std::to_string(a.renderer_settings.present_rate);
  a.sharpness_text = std::to_string(a.renderer_settings.present_effect == "cas" ? a.renderer_settings.cas_sharpness : 100 - a.renderer_settings.fsr_sharpness_reduction / 2);
}

void InspectSelected(App& a) {
  a.current = {};
  a.current.inputs_ok = false;
  const Page page = PageOf(a);
  a.selection_title.clear();
  auto chip = [&](const std::string& text, Tone tone) { a.chip_text = text; a.chip_tone = tone; };
  auto primary = [&](const std::string& text) { a.primary_label = text; };
  if (a.selected >= a.targets.size() || a.checked.size() != a.targets.size()) {
    if (!a.root.empty() && (a.games || page == Page::Home || page == Page::Import)) {
      a.next_action = a.games || page == Page::Import ? kImportPage : kDashboards;
      chip(Tr("Biblioteca vazia", "Library is empty"), Tone::Neutral);
      a.status = Tr("Os jogos que você adicionar aparecerão aqui com seu estado de preparação.\nTítulos cadastrados como compatíveis não contam como jogos instalados.", "Games you add appear here with their preparation status.\nCompatibility entries are not installed games.");
      primary(Tr("Adicionar conteúdo", "Add content"));
    } else {
      a.next_action = kFolder;
      chip(Tr("Ambiente não configurado", "Environment not set up"), Tone::Warn);
      a.status = Tr("Escolha onde guardar seus arquivos. Depois, adicione suas dashboards e jogos.\nA preparação acontece neste computador; seus arquivos não são enviados.", "Choose where to keep your files, then add dashboards and games.\nPreparation happens on this computer; your files are never uploaded.");
      primary(Tr("Escolher pasta", "Choose folder"));
    }
    if (page == Page::Import) { a.next_action = kImport; primary(Tr("Escolher arquivo", "Choose file")); }
    return;
  }
  const auto& t = a.targets[a.selected];
  a.selection_title = english ? t.label_en : t.label;
  a.current = a.checked.at(a.selected).state;
  if (a.games && !a.import_view && !a.home && !t.aot) {
    a.next_action = kImportPage;
    const bool identified = a.current.inputs_ok || a.current.verification_pending;
    chip(identified ? Tr("Não suportado", "Not supported") : Tr("Erro", "Error"), identified ? Tone::Warn : Tone::Err);
    a.status = Tr("Este jogo foi cadastrado, mas não tem preparação AOT disponível.\nEle não pode ser executado nem aparece na My Games.", "This game is catalogued but has no available AOT preparation.\nIt cannot run and does not appear in My Games.");
    primary(Tr("Adicionar conteúdo", "Add content"));
    std::string details = english ? t.limitations_en : t.limitations;
    details += "\nTitle ID: " + (t.title_id.empty() ? "—" : t.title_id) + "\nVersion: " + (t.version.empty() ? "—" : t.version);
    details += "\nMedia ID: " + t.media_id + "\nOriginal PE: " + t.original_name + "\n" + Utf8(t.game_root);
    for (const auto& issue : a.current.issues) details += "\n" + issue;
    a.details = details; return;
  }
  if (a.current.verification_pending) {
    std::string detail = a.environment.description + "\n" + (english ? t.limitations_en : t.limitations);
    a.current.launch_candidate &= a.supervisor_available;
    for (const auto& dep : t.dependencies) {
      const auto found = std::find_if(a.targets.begin(), a.targets.end(), [&](const auto& candidate) { return candidate.id == dep; });
      if (found == a.targets.end()) { a.current.launch_candidate = false; detail += "\nPerfil necessário ausente: " + dep; }
      else {
        const auto& state = a.checked.at(size_t(found - a.targets.begin())).state;
        a.current.launch_candidate &= state.prepared || state.launch_candidate;
      }
    }
    a.next_action = a.current.launch_candidate && t.session ? kLaunch : kPrepare;
    primary(a.next_action == kLaunch ? Tr("Conferir e iniciar", "Verify and start") : Tr("Conferir e preparar", "Verify and prepare"));
    // The row's listing hint, explained; the full check runs before any action.
    const auto row = StatusOf(a, a.selected);
    if (row == Row::Error) {
      chip(Tr("Falha na última tentativa", "Last attempt failed"), Tone::Err);
      a.status = Tr("A última preparação ou execução falhou. O motivo está no Diagnóstico.\nPreparar de novo retoma as etapas válidas.",
                    "The last preparation or run failed. The reason is in Diagnostics.\nPreparing again resumes the valid steps.");
    } else if (row == Row::Imported) {
      chip(a.current.receipt_stale ? Tr("Precisa preparar de novo", "Needs preparing again") : Tr("Não preparado", "Not prepared"), Tone::Warn);
      a.status = a.current.receipt_stale ?
          Tr("Arquivos mudaram desde a última preparação, por exemplo numa atualização do aplicativo.\nPrepare novamente para usar esta versão.",
             "Files changed since the last preparation, for example in an application update.\nPrepare again to use this version.") :
          Tr("Ainda não preparado neste computador.\nA preparação compila o título; seus arquivos são conferidos antes.",
             "Not prepared on this computer yet.\nPreparation compiles the title; your files are verified first.");
    } else if (row == Row::Ready) {
      chip(t.session ? Tr("Pronta para iniciar", "Ready to start") : Tr("Preparado", "Prepared"), Tone::Ok);
      primary(t.session ? Tr("Iniciar Xbox 360", "Start Xbox 360") : Tr("Preparado para My Games", "Prepared for My Games"));
      a.status = Tr("Preparação em dia. Inicie seus jogos pela My Games da dashboard.",
                    "Preparation up to date. Start your games through the dashboard's My Games.");
    } else {
      chip(Tr("Verificação pendente", "Verification pending"), Tone::Info);
      a.status = Tr("Conteúdo listado. Arquivos e recibos serão conferidos antes de preparar ou iniciar.\nEsta listagem ainda não comprova integridade ou compatibilidade.",
                    "Content listed. Files and receipts will be verified before preparation or launch.\nThis listing does not yet establish integrity or compatibility.");
    }
    for (const auto& issue : a.current.issues) detail += "\n" + issue;
    if (page == Page::Home && !a.current.launch_candidate) { a.next_action = kDashboards; primary(Tr("Configurar Xbox 360", "Set up Xbox 360")); }
    if (page == Page::Import) { a.next_action = kImport; primary(Tr("Escolher arquivo", "Choose file")); }
    if (!t.importable && !t.generic_aot && !a.current.launch_candidate) { a.next_action = kDashboards; primary(Tr("Escolher dashboard", "Choose dashboard")); }
    a.details = detail; return;
  }
  std::string missing = a.current.inputs_ok ? "" : (english ? t.label_en : t.label);
  std::string detail;
  detail += a.environment.description + "\n";
  for (const auto& issue : a.current.issues) detail += issue + "\n";
  for (const auto& dep : t.dependencies) {
    auto it = std::find_if(a.targets.begin(), a.targets.end(), [&](const auto& candidate) { return candidate.id == dep; });
    if (it == a.targets.end()) {
      a.current.prepared = false; a.current.inputs_ok = false;
      detail += "Perfil necessário não encontrado: " + dep + "\n";
      continue;
    }
    auto r = a.checked.at(size_t(it - a.targets.begin())).state;
    if (missing.empty() && !r.inputs_ok) missing = english ? it->label_en : it->label;
    a.current.inputs_ok = a.current.inputs_ok && r.inputs_ok;
    a.current.prepared = a.current.prepared && r.prepared;
    for (const auto& issue : r.issues) detail += dep + ": " + issue + "\n";
  }
  if (a.current.inputs_ok) {
    for (const auto& issue : a.checked.at(a.selected).library) { a.current.prepared = false; detail += issue + "\n"; }
  }
  if (!a.supervisor_available) {
    a.current.prepared = false;
    detail += "Falta o supervisor ao lado do aplicativo.\n";
  }
  a.next_action = !a.current.inputs_ok ? kImport : !a.current.prepared ? kPrepare : t.session ? kLaunch : kPrepare;
  primary(a.next_action == kImport ? Tr("Selecionar arquivos", "Choose files") :
          a.next_action == kPrepare ? Tr("Verificar e preparar", "Verify and prepare") : Tr("Abrir dashboard", "Open dashboard"));
  std::string state = !missing.empty() ? Tr("Próximo conjunto: ", "Next set: ") + missing :
    a.current.prepared ? Tr("Conteúdo verificado. Inicie seus jogos pela My Games da dashboard.\nO menu do próprio jogo permite retornar à dashboard.", "Content verified. Launch games through the dashboard's My Games.\nReturn to the dashboard from the game's own menu.") :
    Tr("Conteúdo verificado. A preparação será feita nesta pasta.\nDownloads e instalações serão informados antes de começar.", "Content verified. Preparation will use this folder.\nDownloads and installations will be explained before they start.");
  if (!a.current.inputs_ok) {
    const bool added = a.checked.at(a.selected).added;
    state = (added ? Tr("Conteúdo incompleto", "Content incomplete") : Tr("Aguardando seus arquivos", "Waiting for your files")) + "\n" +
      Tr("Adicione seus arquivos originais para continuar. Os componentes necessários estão listados no Diagnóstico.", "Add your original files to continue. Required components are listed in Diagnostics.");
    if (!missing.empty()) state += "\n" + Tr("Próximo conjunto: ", "Next set: ") + missing;
    chip(added ? Tr("Conteúdo incompleto", "Content incomplete") : Tr("Aguardando arquivos", "Waiting for files"), added ? Tone::Err : Tone::Warn);
  } else if (!a.current.prepared) chip(Tr("Pronto para preparar", "Ready to prepare"), Tone::Info);
  else chip(t.session ? Tr("Pronta para iniciar", "Ready to start") : Tr("Preparado para a My Games", "Prepared for My Games"), Tone::Ok);
  if (!t.importable && !t.generic_aot) {
    state = Tr("Preparação ainda indisponível para esta versão.\nConsulte a pendência no Diagnóstico; seus arquivos serão preservados.", "Preparation is not yet available for this version.\nSee Diagnostics for its requirements; your files are preserved.");
    chip(Tr("Indisponível", "Unavailable"), Tone::Warn);
  }
  if (t.session && !t.launch_library && a.current.inputs_ok && a.current.prepared)
    state = Tr("Dashboard original preparada para iniciar.\nConsulte as limitações desta versão no Diagnóstico.",
               "Original dashboard prepared to start.\nSee this version's limitations in Diagnostics.");
  if (t.generic_aot) state = a.current.prepared ? Tr("Build AOT verificado. Compatibilidade de execução ainda não comprovada.\nTente iniciar pela My Games original.", "AOT build verified. Runtime compatibility is not yet proven.\nTry launching through original My Games.") :
      Tr("Preparação AOT automática disponível. Compilar não garante execução.\nFalhas e estágios ficam registrados no Diagnóstico para nova tentativa.", "Automatic AOT preparation available. Compilation does not guarantee execution.\nErrors and stages are saved in Diagnostics for retry.");
  if (a.games && a.current.prepared) primary(Tr("Preparado para My Games", "Prepared for My Games"));
  detail += "\n" + (english ? t.limitations_en : t.limitations);
  if (t.catalogued) {
    detail += "\nTitle ID: " + (t.title_id.empty() ? "—" : t.title_id) +
      "\nVersion: " + (t.version.empty() ? "—" : t.version) +
      "\nMedia ID: " + (t.media_id.empty() ? "—" : t.media_id) + "\nOriginal PE: " + t.original_name;
  }
  if (!t.user_root.empty()) detail += "\nData: " + Utf8(t.user_root);
  a.status = state;
  a.details = detail.empty() ? "Verificação concluída. Nenhum dado é enviado automaticamente.\nLogs de execução: " + Utf8(a.root / ".local/evidence/runs") : detail;
  if (page == Page::Home) {
    a.next_action = a.current.prepared ? kLaunch : kDashboards;
    primary(a.current.prepared ? Tr("Iniciar Xbox 360", "Start Xbox 360") : Tr("Configurar Xbox 360", "Set up Xbox 360"));
    if (a.current.prepared) { chip(Tr("Pronta para iniciar", "Ready to start"), Tone::Ok); a.status = (english ? t.label_en : t.label) + "\n" + Tr("Abra o menu original do Xbox 360. Seus jogos continuam na My Games, dentro da dashboard.", "Open the original Xbox 360 menu. Your games stay in My Games, inside the dashboard."); }
    else { chip(Tr("Configuração pendente", "Setup pending"), Tone::Warn); a.status = Tr("Configure uma dashboard original para começar.\nJogos são adicionados separadamente, em Importar.", "Set up an original dashboard to get started.\nGames are added separately, in Import."); }
  } else if (page == Page::Import) {
    a.next_action = kImport; primary(Tr("Escolher arquivo", "Choose file"));
  } else if (a.next_action == kImport) {
    a.next_action = kImportPage; primary(Tr("Adicionar conteúdo", "Add content"));
  }
}
void Load(App& a, const fs::path& root, InspectionMode mode) {
  ActionTrace trace("ui.load");
  if (a.busy && a.initializing) return;
  if (a.root != root) { a.targets.clear(); a.checked.clear(); a.catalog_loaded = false; }
  a.dashboard_indices.clear(); a.rows.clear(); a.selected = 0;
  // Hardware probing is independent of navigation between pages.
  const bool probe = a.environment.description.empty() || a.root != root;
  a.root = root;
  if (!a.busy && (mode == InspectionMode::Full || probe || !a.catalog_loaded || a.checked.size() != a.targets.size())) {
    if (a.worker.joinable()) a.worker.join();
    a.busy = true; a.initializing = true; a.cancel = false;
    SetActivity(a, Activity::Check, mode == InspectionMode::Full ? Tr("Conferindo integridade", "Checking integrity") : Tr("Carregando biblioteca", "Loading library"),
      mode == InspectionMode::Full ? Tr("Conferindo arquivos e recibos. Você pode cancelar.", "Verifying files and receipts. You can cancel.") :
      Tr("Listando seu conteúdo. A integridade completa é conferida ao preparar ou iniciar.", "Listing your content. Full integrity is checked when preparing or launching."));
    a.worker = std::thread([&a, root, mode] {
      std::string error;
      try {
        DigestBatch batch([&a] { return a.cancel.load(); });
        a.pending_environment = CheckEnvironment(root);
        a.pending_renderer = root.empty() ? RendererSettings{} : LoadRendererSettings(ProjectPaths{root});
        a.pending_console = root.empty() ? ConsoleSettings{} : LoadConsoleSettings(ProjectPaths{root});
        a.pending_supervisor_available = fs::is_regular_file(Supervisor(a));
        a.pending_targets = root.empty() ? std::vector<Target>{} : ReadCatalog(ProjectPaths{root});
        a.pending_checked = CheckContents(root, a.pending_targets, mode);
        a.pending_snapshot = true;
      } catch (const std::exception& e) { error = e.what(); }
      Post(a, [error](App& app) { OnInspected(app, error); });
    });
    return;
  }
  UpdateRendererControls(a);
  a.selected = a.targets.size();
  for (size_t i = 0; i < a.targets.size(); ++i) {
    if ((a.targets[i].kind != "dashboard") != a.games) continue;
    if (a.games && !a.checked.at(i).added) continue;
    a.dashboard_indices.push_back(i);
    if (!a.games && (a.dashboard_indices.size() == 1 || a.targets[i].session)) a.selected = i;
  }
  if (a.games && !a.dashboard_indices.empty()) a.selected = a.dashboard_indices.front();
  RebuildRows(a);
  if (a.games && !a.rows.empty() && std::find(a.rows.begin(), a.rows.end(), a.selected) == a.rows.end()) a.selected = a.rows.front();
  InspectSelected(a);
}
void SavePreferences(const App& a) {
  if (a.preferences.empty()) return;
  fs::create_directories(a.preferences.parent_path());
  std::ofstream out(a.preferences, std::ios::binary);
  const bool portable = a.root == a.preferences.parent_path() / "environment";
  out << (english ? "en" : "pt") << '\n' << (portable ? "." : Utf8(a.root)) << '\n';
}
void Navigate(App& a, int id) {
  a.result = 0;
  a.home = id == kHome; a.games = id == kGames || id == kLibrary;
  a.import_view = id == kImportPage || id == kAddContent;
  a.settings = a.show_details = false;
}
void GoBack(App& a) { if (a.settings || a.show_details) { a.settings = a.show_details = false; InspectSelected(a); } }

// ---------- Worker results ----------
void OnInspected(App& a, const std::string& error) {
  if (a.worker.joinable()) a.worker.join();
  a.busy = false; a.initializing = false;
  if (error.empty()) {
    AdoptSnapshot(a);
    a.environment = std::move(a.pending_environment);
    a.renderer_settings = a.pending_renderer;
    a.console_settings = a.pending_console;
    a.supervisor_available = a.pending_supervisor_available;
    Load(a, a.root);
    StartHints(a);
    if (!a.auto_import.empty() && !a.busy) {
      const auto source = std::move(a.auto_import); a.auto_import.clear();
      Navigate(a, kImportPage); Load(a, a.root); Start(a, kImport, source);
    }
  } else {
    a.current = {}; a.current.inputs_ok = false; a.next_action = kFolder;
    a.result = 3; a.result_title = Tr("Não foi possível conferir o conteúdo", "Could not check content");
    a.activity_text = Tr("Consulte o Diagnóstico para o motivo. Seus arquivos não foram alterados.", "See Diagnostics for the reason. Your files were not changed.");
    a.details = error;
  }
}
void OnInitialized(App& a, const std::string& error) {
  if (a.worker.joinable()) a.worker.join();
  a.busy = false; a.initializing = false;
  if (!error.empty()) { a.root.clear(); a.startup_error = error; }
  Load(a, a.root, a.pending_full_check ? InspectionMode::Full : InspectionMode::Preview);
  if (error.empty()) SavePreferences(a);
  else {
    a.result = 3; a.result_title = Tr("Não foi possível organizar esta pasta", "Could not prepare this folder");
    a.activity_text = Tr("Escolha outra pasta em Configurações. Seus arquivos não foram alterados.", "Choose another folder in Settings. Your files were not changed.");
    a.details = error;
  }
}
void InitializeRoot(App& a, const fs::path& root) {
  a.root = root; a.targets.clear(); a.checked.clear(); a.catalog_loaded = false;
  a.busy = true; a.initializing = true;
  SetActivity(a, Activity::Check, Tr("Organizando ambiente", "Preparing environment"), Tr("Aguarde a atualização dos componentes do aplicativo.", "Updating application components."));
  a.worker = std::thread([&a, root] {
    std::string error;
    try { InitializeEnvironment(Payload(a), root, true, [&a] { if (a.cancel.load()) throw std::runtime_error("Environment update cancelled; reopen to resume."); }); }
    catch (const std::exception& e) { error = e.what(); }
    Post(a, [error](App& app) { OnInitialized(app, error); });
  });
}
void OnEnvironmentChecked(App& a, const std::string& error) {
  if (a.worker.joinable()) a.worker.join();
  a.busy = false; a.initializing = false;
  if (!error.empty()) throw std::runtime_error(error);
  const auto root = a.pending_root;
  if (a.pending_payload_current) {
    a.targets.clear(); a.checked.clear(); a.catalog_loaded = false;
    Load(a, root, a.pending_full_check ? InspectionMode::Full : InspectionMode::Preview); SavePreferences(a); return;
  }
  if (fs::exists(root / "deps.lock.json")) {
    std::string message = Tr("Esta instalação precisa ser atualizada. Jogos, perfis e saves serão preservados.\n\nComponentes:", "This installation needs an update. Games, profiles and saves will be preserved.\n\nComponents:");
    for (const auto& item : a.pending_payload.components) message += "\n" + item;
    a.prompt = Prompt{"x360-native", message + Tr("\n\nAtualizar agora?", "\n\nUpdate now?"), {Tr("Atualizar", "Update"), Tr("Agora não", "Not now")}, false,
                      [root](App& app, int answer) { if (answer == 0) InitializeRoot(app, root); }};
    return;
  }
  InitializeRoot(a, root);
}
void OnFinished(App& a, const std::string& text) {
  ActionTrace trace("ui.finished");
  if (a.worker.joinable()) a.worker.join();
  a.busy = false; a.session = false; a.finishing_session = false; a.installing = false; a.preparing_id.clear();
  AdoptSnapshot(a);
  if ((a.last_action == kLaunch && a.failed && !a.launch_verified) || a.cancel.load()) {
    const auto index = IndexOf(a, a.operation_target);
    if (index < a.targets.size()) {
      auto& state = a.checked.at(index).state;
      state.verification_pending = false;
      state.inputs_ok = false;
      state.prepared = false; state.issues.push_back(text);
    }
  }
  if (a.last_action == kRemove) Load(a, a.root);
  if (a.importing) {
    Load(a, a.root);
    const auto index = IndexOf(a, a.imported_id);
    if (index < a.targets.size()) a.selected = index;
  }
  InspectSelected(a);
  a.details = text + "\n\nLogs: " + Utf8(a.logs) + "\n\n" + a.details;
  const bool cancelled = a.cancel.load();
  if (a.failed) {
    a.result = cancelled ? 2 : 3;
    a.result_title = cancelled ? Tr("Operação cancelada", "Operation cancelled") : Tr("Não foi possível concluir", "Could not finish");
  } else if (a.importing && !a.import_recognized) {
    a.result = 2; a.result_title = Tr("Nenhum conteúdo importado", "Nothing was imported");
  } else {
    a.result = 1;
    a.result_title = a.importing ? Tr("Importação concluída", "Import finished") :
      a.last_action == kLaunch ? Tr("Sessão encerrada", "Session ended") :
      a.last_action == kVerifyTarget ? Tr("Verificação concluída", "Verification finished") :
      a.last_action == kRemove ? Tr("Jogo removido", "Game removed") :
      a.last_action == kInstall ? Tr("Instalador encerrado", "Installer closed") : Tr("Preparação concluída", "Preparation finished");
  }
  a.activity_text = text;
  StartHints(a);
}
void OnProgress(App& a, const std::string& text) {
  a.details = text;
  if (!a.cancel) a.activity_text = text;
}
void OnHinted(App& a, const App::Hints& found) {
  if (a.busy || found.generation != a.hint_generation || a.checked.size() != a.targets.size()) return;
  for (const auto& [index, hint] : found.items)
    if (index < a.checked.size()) ApplyReceiptHint(a.checked[index].state, hint);
  // Rows draw their state; only a status filter changes which rows exist.
  if (a.filter != 0) { RebuildRows(a); KeepSelectionVisible(a); }
  if (!a.show_details && !a.settings) InspectSelected(a);
}
void OnSessionStarted(App& a) {
  if (a.busy && a.last_action == kLaunch) {
    a.session = true;
    a.activity_title = Tr("Sessão em andamento", "Session running");
    a.activity_text = Tr("A dashboard está sob supervisão. Feche a janela da plataforma para encerrar.", "The dashboard is supervised. Close the platform window to end the session.");
  }
}
void OnSessionClosed(App& a) {
  if (a.busy && a.last_action == kLaunch) {
    a.session = false; a.finishing_session = true;
    a.activity_title = Tr("Sessão encerrada", "Session ended");
    a.activity_text = Tr("A janela foi fechada. Finalizando os registros locais.", "The window closed. Finalizing local diagnostics.");
    ActionTrace::Event("ui.session.closed");
  }
}
void OnUpdateChecked(App& a, std::optional<AvailableUpdate> found) {
  if (a.update_check.joinable()) a.update_check.join();
  a.update = std::move(found);
}
void OnUpdateFinished(App& a, std::string error) {
  if (a.worker.joinable()) a.worker.join();
  a.busy = false;
  if (error.empty()) {
    // The new files are in place; the old launcher image was only renamed.
    if (StartDetached(a.self)) { if (a.quit) a.quit(); return; }
    error = Tr("A nova versão não iniciou. Feche o aplicativo e execute recover-update para restaurar a anterior.",
               "The new version could not start. Close the application and run recover-update to restore the previous version.");
  }
  a.result = a.cancel ? 2 : 3;
  a.result_title = Tr("Atualização não instalada", "Update not installed");
  a.activity_text = error;
  a.details = error + "\n" + Utf8(Updates(a));
}

// ---------- Startup ----------
void ConfigureFromPreferences(App& a) {
  if (!fs::is_directory(Payload(a))) {
    if (a.root.empty()) if (auto root = FindProjectRoot(a.self)) a.root = root->root;
    return;
  }
  a.preferences = a.self.parent_path() / "data/preferences.txt";
  {
    std::ifstream in(a.preferences); std::string language, root;
    std::getline(in, language); std::getline(in, root);
    if (!language.empty()) english = language == "en";
    auto saved = root == "." ? a.preferences.parent_path() / "environment" : PathFromUtf8(root);
    if (a.root.empty() && !root.empty() && fs::is_regular_file(saved / "deps.lock.json")) a.root = saved;
  }
  std::ifstream channel(a.preferences.parent_path() / "update-channel.txt");
  std::string selected; std::getline(channel, selected);
  if (selected == "preview") a.update_channel = UpdateChannel::Preview;
  // A first-time user shouldn't need to choose a source repository. The package
  // prepares its own verified source payload in the user's normal data folder.
  // An existing installation is preserved; its location can be changed in Settings.
  if (a.root.empty()) a.root = a.preferences.parent_path() / "environment";
  a.initializing = true;
}
void Open(App& a) {
  a.initializing = false;
  if (!a.preferences.empty()) CheckEnvironmentAsync(a, a.root);
  else Load(a, a.root);
  CheckForUpdate(a);
}
bool RequestClose(App& a) {
  if (!a.busy) return true;
  a.prompt = Prompt{Tr("Operação em andamento", "Operation running"),
    a.installing ? Tr("Conclua ou cancele pelo instalador Microsoft e aguarde o retorno.", "Finish or cancel in the Microsoft installer and wait for it to return.") :
    a.session ? Tr("Encerre pela janela da plataforma. Voltar pelo menu do jogo retorna à dashboard.", "End it from the platform window. Returning through the game's menu goes back to the dashboard.") :
    a.finishing_session ? Tr("A sessão já encerrou. Aguarde a conclusão dos registros locais.", "The session has ended. Wait for local diagnostics to finish.") :
    Tr("Cancele a operação e aguarde sua conclusão antes de fechar.", "Cancel the operation and wait for it to finish before closing."),
    {"OK"}, false, {}};
  return false;
}

// ---------- Availability, selection and labels ----------
bool Enabled(const App& a, int id) {
  const bool has = a.selected < a.targets.size();
  const bool native = a.renderer_settings.experimental;
  const bool configured = !a.busy && !a.root.empty();
#ifdef _WIN32
  constexpr bool windows = true;
#else
  constexpr bool windows = false;
#endif
  switch (id) {
    case kHome: case kDashboards: case kGames: case kImportPage: case kLibrary: case kSearch:
    case kFilter0: case kFilter1: case kFilter2: case kFilter3: return !a.initializing;
    case kGameList: return !a.initializing && a.checked.size() == a.targets.size();
    case kDetails: case kDismiss: case kGoBack: return true;
    case kData: case kOpenLogs: return !a.root.empty();
    case kCancel: return a.busy && !a.session && !a.finishing_session && !a.installing;
    case kStorage: case kRenderer: case kNativeDevelopment: case kFullscreen: case kConsoleLanguage: case kConsoleCountry: return configured;
    case kRendererNative: return configured && native && windows;
    case kApiD3D12: return configured && windows;
    // Windows: Vulkan is an experimental Xenos API; Linux/macOS: the default.
    case kApiVulkan: return configured && (!windows || (native && a.renderer_settings.renderer == Renderer::Xenos));
    case kRemove: return !a.busy && !a.initializing && Removable(a);
    case kPrimary: {
      const bool preparable = has && (a.targets[a.selected].importable || a.targets[a.selected].generic_aot);
      const bool ok = a.next_action == kFolder || a.next_action == kDashboards || a.next_action == kImportPage ||
        (a.next_action == kImport && !a.root.empty()) ||
        (a.next_action == kPrepare && preparable && !a.current.prepared && (a.current.inputs_ok || a.current.verification_pending) && a.environment.writable && a.environment.architecture) ||
        (a.next_action == kVerifyTarget && a.current.verification_pending && a.current.launch_candidate) ||
        (a.next_action == kLaunch && (a.current.prepared || a.current.launch_candidate) && a.environment.graphics);
      return !a.busy && ok;
    }
    case kEnhanced: return configured && native && a.renderer_settings.renderer == Renderer::Native;
    case kSharpness:
      return Enabled(a, kEnhanced) && a.renderer_settings.enhanced && a.renderer_settings.present_effect != "bilinear";
    case kFps30: case kFps60: case kFps120: case kFps144: case kFpsCustom: case kPresentRate: case kTimingApply:
    case kEffectBilinear: case kEffectCas: case kEffectFsr: case kScale1: case kScale2: case kScale3: case kScale4:
    case kAnisoGuest: case kAniso2: case kAniso4: case kAniso8: case kAniso16: case kAaNone: case kAaFxaa: case kAaExtreme: case kUltrawide:
    case kFpsLabel: case kPresentLabel: case kSharpnessLabel: case kScaleLabel: case kAnisoLabel: case kAaLabel:
      return Enabled(a, kEnhanced) && a.renderer_settings.enhanced;
    default: return !a.busy;
  }
}
bool Selected(const App& a, int id) {
  const Page page = PageOf(a);
  const auto& r = a.renderer_settings;
  switch (id) {
    case kHome: return page == Page::Home;
    case kGames: return page == Page::Library;
    case kImportPage: return page == Page::Import;
    case kDashboards: return page == Page::Dashboard;
    case kSettings: return page == Page::Settings;
    case kDetails: return page == Page::Diagnostics;
    case kFilter0: case kFilter1: case kFilter2: case kFilter3: return a.filter == id - kFilter0;
    case kLangPt: return !english;
    case kLangEn: return english;
    case kRenderer: return r.renderer == Renderer::Xenos;
    case kRendererNative: return r.renderer == Renderer::Native;
    case kApiD3D12: return r.api == GraphicsApi::D3D12;
    case kApiVulkan: return r.api == GraphicsApi::Vulkan;
    case kNativeDevelopment: return r.experimental;
    case kEnhanced: return r.enhanced;
    case kFps30: return r.framerate == 30;
    case kFps60: return r.framerate == 60;
    case kFps120: return r.framerate == 120;
    case kFps144: return r.framerate == 144;
    case kEffectBilinear: return r.present_effect == "bilinear";
    case kEffectCas: return r.present_effect == "cas";
    case kEffectFsr: return r.present_effect == "fsr";
    case kScale1: case kScale2: case kScale3: case kScale4: return r.scale_x == uint32_t(id - kScale1 + 1) && r.scale_y == r.scale_x;
    case kAnisoGuest: return r.anisotropy == 0;
    case kAniso2: return r.anisotropy == 2;
    case kAniso4: return r.anisotropy == 4;
    case kAniso8: return r.anisotropy == 8;
    case kAniso16: return r.anisotropy == 16;
    case kAaNone: return r.swap_post_effect == "none";
    case kAaFxaa: return r.swap_post_effect == "fxaa";
    case kAaExtreme: return r.swap_post_effect == "fxaa_extreme";
    case kUltrawide: return r.ultrawide;
    case kFullscreen: return r.fullscreen;
    case kCaptureCrash: return a.capture_crash;
    default: return false;
  }
}
std::string Label(const App& a, int id) {
  struct Text { int id; const char* pt; const char* en; };
  static const Text texts[] = {{kFolder, "Selecionar ambiente", "Choose environment"},
       {kCheck, "Verificar ambiente", "Check environment"},
       {kDetails, "Diagnóstico", "Diagnostics"}, {kData, "Abrir dados locais", "Open local data"},
       {kCancel, "Cancelar", "Cancel"}, {kStorage, "Pasta de saves", "Save data folder"},
       {kSettings, "Configurações", "Settings"}, {kImportFolder, "Escolher pasta", "Choose folder"},
       {kDashboards, "Dashboard", "Dashboard"}, {kGames, "Biblioteca", "Library"}, {kHome, "Início", "Home"},
       {kImportPage, "Importar", "Import"}, {kAddContent, "Adicionar conteúdo", "Add content"}, {kLibrary, "Ver biblioteca", "View library"},
       {kFilter0, "Todos", "All"}, {kFilter1, "Prontos", "Ready"}, {kFilter2, "Preparar", "To prepare"}, {kFilter3, "Atenção", "Attention"},
       {kLangPt, "Português (BR)", "Portuguese (BR)"}, {kLangEn, "English", "English"},
       {kRenderer, "Xenos (ReXGlue)", "Xenos (ReXGlue)"}, {kRendererNative, "Native (Experimental)", "Native (Experimental)"},
       {kApiD3D12, "Direct3D 12", "Direct3D 12"}, {kApiVulkan, "Vulkan", "Vulkan"},
       {kNativeDevelopment, "Acesso de desenvolvimento ao Native", "Native development access"},
       {kEnhanced, "Ativar no próximo início", "Enable on next launch"},
       {kAaLabel, "Antisserrilhamento", "Anti-aliasing"},
       {kFps30, "30", "30"}, {kFps60, "60", "60"}, {kFps120, "120", "120"}, {kFps144, "144", "144"},
       {kFpsLabel, "FPS · 30–240", "FPS · 30–240"}, {kPresentLabel, "Limite visual · 0 livre", "Display cap · 0 uncapped"},
       {kTimingApply, "Aplicar limites personalizados", "Apply custom limits"},
       {kEffectBilinear, "Bilinear", "Bilinear"}, {kEffectCas, "CAS", "CAS"}, {kEffectFsr, "FSR 1", "FSR 1"},
       {kSharpnessLabel, "Nitidez · 0–100%", "Sharpness · 0–100%"},
       {kScaleLabel, "Resolução interna · escala X/Y", "Internal resolution · X/Y scale"},
       {kScale1, "1×", "1×"}, {kScale2, "2×", "2×"}, {kScale3, "3×", "3×"}, {kScale4, "4×", "4×"},
       {kAnisoLabel, "Filtragem anisotrópica", "Anisotropic filtering"},
       {kAnisoGuest, "Jogo", "Game"}, {kAniso2, "2×", "2×"}, {kAniso4, "4×", "4×"}, {kAniso8, "8×", "8×"}, {kAniso16, "16×", "16×"},
       {kAaNone, "AA: jogo", "AA: game"}, {kAaFxaa, "FXAA", "FXAA"}, {kAaExtreme, "FXAA+", "FXAA+"},
       {kUltrawide, "Ultrawide · câmera e HUD do perfil", "Ultrawide · profile camera and HUD"},
       {kFullscreen, "Iniciar sessão em tela cheia", "Start session in fullscreen"},
       {kCaptureCrash, "Capturar pilha de falhas nas próximas sessões", "Capture crash stacks in upcoming sessions"},
       {kRetry, "Tentar novamente", "Try again"}, {kBannerDetails, "Diagnóstico", "Diagnostics"}, {kBannerLibrary, "Ver biblioteca", "View library"},
       {kDismiss, "Fechar", "Dismiss"}, {kRemove, "Remover da biblioteca", "Remove from library"},
       {kOpenLogs, "Abrir logs", "Open logs"}};
  if (id == kPrimary) return a.primary_label;
  if (id == kLanguage) return english ? "Language: English" : "Idioma: Português";
  if (id == kUpdate) return a.update ? Tr("Instalar ", "Install ") + a.update->version : std::string();
  for (const auto& t : texts) if (t.id == id) return Tr(t.pt, t.en);
  return {};
}
std::string OptionCaption(const App& a, int id) {
#ifdef _WIN32
  if (id == kApiVulkan) return a.renderer_settings.renderer == Renderer::Native ?
      Tr("Experimental · sem validação em jogo", "Experimental · not validated in game") :
      Tr("Experimental · driver conferido no início", "Experimental · driver checked at launch");
  if (id == kApiD3D12) return Tr("Padrão · disponível para Xenos e Native", "Default · available for Xenos and Native");
  if (id == kRendererNative) return Enabled(a, kRendererNative) || a.busy ? Tr("Experimental · executor próprio", "Experimental · independent executor")
                                                                          : Tr("Requer acesso de desenvolvimento", "Requires development access");
#else
  if (id == kApiVulkan) {
#ifdef __APPLE__
    return Tr("Padrão · Vulkan via MoltenVK, driver conferido no início", "Default · Vulkan through MoltenVK, driver checked at launch");
#else
    return Tr("Padrão · driver conferido no início", "Default · driver checked at launch");
#endif
  }
  if (id == kRendererNative) return Tr("Indisponível neste sistema · requer Direct3D 12", "Unavailable on this system · requires Direct3D 12");
#endif
  (void)a;
  return Tr("Padrão · compatível com o ReXGlue", "Default · ReXGlue compatible");
}
const char* PrimaryGlyph(const App& a) {
  return a.next_action == kLaunch ? "play" : a.next_action == kPrepare ? "tools" : a.next_action == kImport ? "file" :
         a.next_action == kImportPage ? "add" : a.next_action == kFolder ? "folder" : "settings";
}

// ---------- Commands ----------
namespace {
void ChangeRenderer(App& a, const RendererSettings& settings) {
  SaveRendererSettings(ProjectPaths{a.root}, settings);
  a.renderer_settings = settings; UpdateRendererControls(a);
}
uint32_t Number(const std::string& text) {
  if (text.empty() || text.size() > 3 || text.find_first_not_of("0123456789") != std::string::npos) return UINT32_MAX;
  return uint32_t(std::stoul(text));
}
void Prepare(App& a) {
  if (!a.environment.toolchain) {
#ifdef _WIN32
    a.prompt = Prompt{"Microsoft Build Tools 2022", Tr("Falta o componente C++ do Microsoft Build Tools 2022. Abrir o instalador oficial com os componentes necessários selecionados?\n\nEsta é uma instalação no sistema: o Windows pode pedir permissão de administrador. O instalador mostra espaço, downloads e termos antes da instalação. Use o próprio instalador para cancelar. Jogos e dados serão preservados.", "Microsoft Build Tools 2022 C++ is missing. Open the official installer with the required components preselected?\n\nThis changes the system: Windows may request administrator permission. The installer shows space, downloads and terms before installation. Cancel using the installer itself. Games and data are preserved."),
                      {Tr("Abrir instalador", "Open installer"), Tr("Cancelar", "Cancel")}, false, [](App& app, int answer) { if (answer == 0) Start(app, kInstall); }};
#else
    std::string missing;
    for (const auto& tool : a.environment.missing_tools) missing += "\n• " + tool;
    a.prompt = Prompt{Tr("Ferramentas de compilação ausentes", "Build tools missing"),
      Tr("A preparação AOT compila o título neste computador e precisa destas ferramentas no PATH:", "AOT preparation compiles the title on this computer and needs these tools on PATH:") + missing +
      Tr("\n\nInstale-as pelo gerenciador de pacotes do sistema e selecione Verificar ambiente em Configurações. O aplicativo não instala nada no sistema.",
         "\n\nInstall them through the system package manager, then select Check environment in Settings. The application installs nothing system-wide."),
      {"OK"}, true, {}};
#endif
    return;
  }
#ifdef _WIN32
  a.prompt = Prompt{"x360-native", Tr("Preparar verifica e obtém Git, CMake, Ninja, LLVM e o SDK fixados. Downloads de ferramentas: até 905 MiB; o SDK e suas dependências são adicionais. Arquivos verificados em cache são reutilizados. Ferramentas incompatíveis serão atualizadas com uma cópia anterior preservada. Reserve espaço para fontes e compilação (recomendação inicial: 30 GiB, não uma medição do mínimo).\n\nPermitir os downloads necessários? Não há instalação global nem elevação. Não = usar somente os arquivos já presentes.", "Preparation verifies and obtains pinned Git, CMake, Ninja, LLVM and SDK. Tool downloads: up to 905 MiB; SDK and its dependencies are additional. Verified cached files are reused. Incompatible tools will be updated with their previous copies preserved. Reserve space for sources and compilation (initial recommendation: 30 GiB, not a measured minimum).\n\nAllow required downloads? No global installation or elevation. No = use only existing files."),
                    {Tr("Sim", "Yes"), Tr("Não", "No"), Tr("Cancelar", "Cancel")}, false,
                    [](App& app, int answer) {
                      if (answer != 0 && answer != 1) return;
                      app.allow_downloads = answer == 0;
                      InspectSelected(app);
                      if (app.current.inputs_ok || app.current.verification_pending) Start(app, kPrepare);
                    }};
#else
  a.prompt = Prompt{"x360-native", Tr("Preparar compila o título com as ferramentas nativas deste sistema (clang, CMake, Ninja) e o SDK fixado. Arquivos verificados em cache são reutilizados. Reserve espaço para fontes e compilação (recomendação inicial: 30 GiB, não uma medição do mínimo).\n\nContinuar? Não há instalação global nem elevação.", "Preparation compiles the title with this system's native tools (clang, CMake, Ninja) and the pinned SDK. Verified cached files are reused. Reserve space for sources and compilation (initial recommendation: 30 GiB, not a measured minimum).\n\nContinue? No global installation or elevation."),
                    {Tr("Preparar", "Prepare"), Tr("Cancelar", "Cancel")}, false,
                    [](App& app, int answer) {
                      if (answer != 0) return;
                      app.allow_downloads = false;
                      InspectSelected(app);
                      if (app.current.inputs_ok || app.current.verification_pending) Start(app, kPrepare);
                    }};
#endif
}
}  // namespace
void Command(App& a, int id) {
  Guard(a, [&] {
    ActionTrace trace("ui.command." + std::to_string(id));
    if (id == kPrimary) id = a.next_action;
    if (id == kGoBack) { GoBack(a); return; }
    if (id == kDismiss) { a.result = 0; return; }
    if (id == kBannerDetails) id = kDetails;
    if (id == kBannerLibrary) id = kLibrary;
    if (a.busy && id != kDetails && id != kData && id != kOpenLogs && id != kCancel && id != kDismiss &&
        id != kHome && id != kDashboards && id != kGames && id != kImportPage && id != kLibrary &&
        id != kSearch && id != kGameList && !(id >= kFilter0 && id <= kFilter3)) return;
    if (id == kDetails || id == kSettings) {
      if (id == kDetails) { a.show_details = !a.show_details; a.settings = false; }
      else { if (a.busy) return; a.settings = !a.settings; a.show_details = false; }
      if (!a.show_details && !a.settings && !a.busy) InspectSelected(a);
      return;
    }
    if ((id == kData || id == kOpenLogs) && !a.root.empty()) {
      auto p = id == kOpenLogs && !a.logs.empty() && fs::is_directory(a.logs) ? a.logs : a.root / ".local";
      fs::create_directories(p);
      if (a.open_path) a.open_path(p);
      return;
    }
    if (id == kCancel) { a.cancel = true; a.activity_text = Tr("Cancelando... Aguarde a etapa atual terminar com segurança.", "Cancelling... Wait for the current step to finish safely."); return; }
    if (id == kCaptureCrash) { a.capture_crash = !a.capture_crash; return; }
    if (id == kEnhanced || (id >= kFps30 && id <= kFps144) || id == kTimingApply || (id >= kEffectBilinear && id <= kEffectFsr) ||
        (id >= kScale1 && id <= kScale4) || (id >= kAnisoGuest && id <= kAniso16) || (id >= kAaNone && id <= kAaExtreme) || id == kUltrawide) {
      if (a.root.empty() || !a.renderer_settings.experimental || a.renderer_settings.renderer != Renderer::Native) return;
      auto settings = a.renderer_settings;
      if (id == kEnhanced) settings.enhanced = !settings.enhanced;
      else if (id == kUltrawide) settings.ultrawide = !settings.ultrawide;
      else if (id >= kScale1 && id <= kScale4) settings.scale_x = settings.scale_y = uint32_t(id - kScale1 + 1);
      else if (id >= kAnisoGuest && id <= kAniso16) { const uint32_t values[] = {0, 2, 4, 8, 16}; settings.anisotropy = values[id - kAnisoGuest]; }
      else if (id >= kAaNone && id <= kAaExtreme) { const char* effects[] = {"none", "fxaa", "fxaa_extreme"}; settings.swap_post_effect = effects[id - kAaNone]; }
      else if (id >= kEffectBilinear && id <= kEffectFsr) { const char* effects[] = {"bilinear", "cas", "fsr"}; settings.present_effect = effects[id - kEffectBilinear]; }
      else if (id == kTimingApply) {
        const auto fps = Number(a.fps_text), present = Number(a.present_text);
        if (fps == UINT32_MAX || present == UINT32_MAX) throw std::runtime_error(Tr("Informe FPS e limite visual (0 = livre).", "Enter FPS and display cap (0 = uncapped)."));
        settings.framerate = fps; settings.present_rate = present;
        if (settings.present_effect != "bilinear") {
          const auto strength = Number(a.sharpness_text);
          if (strength > 100) throw std::runtime_error(Tr("Nitidez deve estar entre 0 e 100%.", "Sharpness must be between 0 and 100%."));
          if (settings.present_effect == "cas") settings.cas_sharpness = strength;
          else settings.fsr_sharpness_reduction = (100 - strength) * 2;
        }
      } else { const uint32_t presets[] = {30, 60, 120, 144}; settings.framerate = presets[id - kFps30]; }
      ChangeRenderer(a, settings); return;
    }
    if (id == kRenderer || id == kRendererNative || id == kNativeDevelopment || id == kFullscreen || id == kApiD3D12 || id == kApiVulkan) {
      if (a.root.empty() || (id != kFullscreen && id != kNativeDevelopment && !Enabled(a, id))) return;
      auto settings = a.renderer_settings;
      if (id == kFullscreen) settings.fullscreen = !settings.fullscreen;
      else if (id == kNativeDevelopment) {
        settings.experimental = !settings.experimental;
        // Without development access only the default renderer/API of this host remains.
        if (!settings.experimental) { settings.renderer = Renderer::Xenos; settings.api = RendererSettings{}.api; }
      } else if (id == kRendererNative) settings.renderer = Renderer::Native;
      else if (id == kApiD3D12) settings.api = GraphicsApi::D3D12;
      else if (id == kApiVulkan) settings.api = GraphicsApi::Vulkan;
      else settings.renderer = Renderer::Xenos;
      ChangeRenderer(a, settings); return;
    }
    if (id == kHome || id == kDashboards || id == kGames || id == kImportPage || id == kAddContent || id == kLibrary) {
      Navigate(a, id); Load(a, a.root); return;
    }
    if (id >= kFilter0 && id <= kFilter3) { a.filter = id - kFilter0; RebuildRows(a); KeepSelectionVisible(a); InspectSelected(a); return; }
    if (id == kStorage) { a.pick = PickRequest{kStorage, true, Tr("Selecione a pasta de saves e perfis", "Select the saves and profiles folder")}; return; }
    if (id == kFolder) { a.pick = PickRequest{kFolder, true, Tr("Escolha uma pasta vazia para o ambiente ou uma existente do x360-native", "Choose an empty environment folder or an existing x360-native environment")}; return; }
    if (id == kImport || id == kImportFolder) {
      a.pick = PickRequest{kImport, id == kImportFolder, id == kImport ? Tr("Selecione um arquivo original", "Select an original file") : Tr("Selecione a pasta com seus arquivos originais", "Select the folder with your original files")};
      return;
    }
    if (id == kLanguage || id == kLangPt || id == kLangEn) {
      english = id == kLanguage ? !english : id == kLangEn;
      auto selected = a.selected; Load(a, a.root); a.selected = selected; InspectSelected(a); SavePreferences(a); return;
    }
    if (id == kUpdate && a.update) {
      const auto question = Tr("Instalar a versão ", "Install version ") + a.update->version + Tr(" (atual: ", " (current: ") + X360N_VERSION + ")?\n\n" +
        Tr("O pacote é baixado do GitHub Releases do x360-native e conferido pelo SHA-256 publicado e pelo índice do próprio pacote. "
           "Ambiente, jogos, perfis e saves são preservados; a versão atual fica em data/updates; recover-update permite restaurá-la. "
           "O aplicativo reinicia ao final; o ambiente pode pedir para ser atualizado em seguida.",
           "The package is downloaded from x360-native's GitHub Releases and checked against the published SHA-256 and its own index. "
           "Environment, games, profiles and saves are preserved; the current version stays in data/updates; recover-update can restore it. "
           "The application restarts at the end; the environment may then ask to be updated.");
      a.prompt = Prompt{"x360-native", question, {Tr("Instalar", "Install"), Tr("Cancelar", "Cancel")}, false, [](App& app, int answer) { if (answer == 0) StartUpdate(app); }};
      return;
    }
    if (id == kRemove) {
      if (!Removable(a)) return;
      const auto& t = a.targets[a.selected];
      const auto question = Tr("Remover \"", "Remove \"") + (english ? t.label_en : t.label) + Tr("\" da biblioteca?\n\n", "\" from the library?\n\n") +
        Tr("Serão apagados deste ambiente: a cópia importada do jogo, sua cópia na My Games das dashboards e a preparação AOT gerada para ele. "
           "Saves, perfis e o arquivo original que você importou são mantidos. Uma pasta importada no próprio local não é apagada.\n\n"
           "Para jogar de novo, importe e prepare o jogo outra vez.",
           "This deletes from the environment: the game's imported copy, its copy in the dashboards' My Games and the AOT preparation generated for it. "
           "Saves, profiles and the original file you imported are kept. A folder imported in place is not deleted.\n\n"
           "To play again, import and prepare the game again.");
      // The safe answer is the default one.
      a.prompt = Prompt{"x360-native", question, {Tr("Cancelar", "Cancel"), Tr("Remover", "Remove")}, true, [](App& app, int answer) { if (answer == 1) Start(app, kRemove); }};
      return;
    }
    if (id == kRetry) {
      a.result = 0;
      if (a.last_action == kUpdate) StartUpdate(a);
      else if (a.last_action == kImport && !a.last_source.empty()) Start(a, kImport, a.last_source);
      else if (a.last_action == kPrepare || a.last_action == kLaunch || a.last_action == kVerifyTarget) {
        const auto index = IndexOf(a, a.operation_target);
        if (index >= a.targets.size()) throw std::runtime_error("O alvo da operação não está mais neste catálogo. Verifique o ambiente.");
        Navigate(a, a.targets[index].kind == "dashboard" ? kDashboards : kGames);
        Load(a, a.root); a.selected = index; InspectSelected(a);
        Command(a, a.last_action);
      }
      return;
    }
    if (id == kCheck) {
      if (!a.preferences.empty()) { CheckEnvironmentAsync(a, a.root, true); return; }
      Load(a, a.root, InspectionMode::Full); return;
    }
    if (id == kPrepare) { Prepare(a); return; }
    if (id == kLaunch || id == kVerifyTarget) {
      InspectSelected(a);
      if ((id == kLaunch && (a.current.prepared || a.current.launch_candidate)) ||
          (id == kVerifyTarget && a.current.verification_pending)) Start(a, id);
    }
  });
}
void SelectRow(App& a, size_t row) {
  if (row < a.rows.size()) { a.selected = a.rows[row]; Guard(a, [&] { InspectSelected(a); }); }
}
void SetQuery(App& a, const std::string& text) {
  a.query = Lower(text);
  RebuildRows(a); KeepSelectionVisible(a);
  Guard(a, [&] { InspectSelected(a); });
}
void SetConsoleValue(App& a, int id, uint32_t value) {
  Guard(a, [&] {
    if (a.root.empty() || a.busy) return;
    auto settings = a.console_settings;
    if (id == kConsoleLanguage) settings.language = value; else settings.country = value;
    WorkspaceLease lease(ProjectPaths{a.root});
    SaveConsoleSettings(ProjectPaths{a.root}, settings);
    a.console_settings = settings;
  });
}
void Picked(App& a, int command, const fs::path& path) {
  a.pick.reset();
  if (path.empty()) return;
  Guard(a, [&] {
    if (command == kImport) { Start(a, kImport, path); return; }
    if (command == kStorage) {
      a.prompt = Prompt{"x360-native", Tr("Usar esta pasta nas próximas sessões? Os dados anteriores permanecem na pasta original e não serão movidos ou apagados. Para reutilizar saves existentes, escolha a pasta que contém os diretórios dos títulos.", "Use this folder in future sessions? Previous data stays in its original folder and will not be moved or deleted. To reuse existing saves, choose the folder containing the title directories."),
                        {Tr("Usar pasta", "Use folder"), Tr("Cancelar", "Cancel")}, false,
                        [path](App& app, int answer) {
                          if (answer != 0) return;
                          WorkspaceLease lease(ProjectPaths{app.root}); SetUserDataRoot(ProjectPaths{app.root}, path);
                          app.targets.clear(); app.catalog_loaded = false; Load(app, app.root);
                        }};
      return;
    }
    if (command == kFolder) {
      if (!a.preferences.empty()) { CheckEnvironmentAsync(a, path); return; }
      a.targets.clear(); a.checked.clear(); a.catalog_loaded = false;
      Load(a, path); SavePreferences(a);
    }
  });
}
void Drop(App& a, const fs::path& path) {
  if (a.busy || path.empty()) return;
  Guard(a, [&] { Navigate(a, kImportPage); Load(a, a.root); Start(a, kImport, path); });
}
void Answer(App& a, int button) {
  if (!a.prompt) return;
  auto prompt = std::move(*a.prompt); a.prompt.reset();
  if (prompt.answer) Guard(a, [&] { prompt.answer(a, button); });
}
}  // namespace x360n::desktop
