// Operations of the launcher. Each runs on the single worker (or the update/hint
// threads), writes only pending results, and posts one event to the controller.
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include "action_trace.h"
#include "app_controller.h"
#include "child_process.h"
#include "digest_batch.h"
#include "host_platform.h"
#include "import_source.h"
#include "preparation.h"
#include "session_notification.h"
#include "app_text.h"

namespace fs = std::filesystem;
namespace x360n::desktop {
// A snapshot is for presentation only. Navigation never hashes the content.
// Populate it on the worker; operations revalidate before using the files.
std::vector<App::CheckedTarget> CheckContents(const fs::path& root, const std::vector<Target>& targets, InspectionMode mode) {
  ActionTrace trace(mode == InspectionMode::Full ? "content.snapshot" : "content.list");
  std::vector<App::CheckedTarget> checked;
  for (const auto& target : targets) {
    App::CheckedTarget item;
    item.state = Inspect(ProjectPaths{root}, target, mode);
    if (mode == InspectionMode::Full && item.state.inputs_ok) item.library = InspectLibrary(ProjectPaths{root}, target, targets);
    if (target.kind == "dashboard" && !target.inputs.empty()) {
      const auto& first = target.inputs.front();
      const auto file = target.preparation_source.empty() ? first.destination :
          target.preparation_source / first.destination.lexically_relative(target.game_root);
      if (mode == InspectionMode::Preview) item.added = fs::is_regular_file(file);
      else { const auto anchor = DigestFile(file); item.added = anchor && anchor->sha256 == first.sha256; }
    }
    item.added = item.added || InGameLibrary(target);
    checked.push_back(std::move(item));
  }
  return checked;
}
void AdoptSnapshot(App& a) {
  if (!a.pending_snapshot) return;
  a.targets = std::move(a.pending_targets);
  a.checked = std::move(a.pending_checked);
  a.catalog_loaded = true;
  a.pending_snapshot = false;
  ++a.hint_generation;
}
// Second phase of a listing: built titles' receipts by file times, so a row can
// say ready or stale without delaying the list. Presentation only.
void StartHints(App& a) {
  if (a.hint_check.joinable()) a.hint_check.join();
  if (a.root.empty() || a.checked.size() != a.targets.size()) return;
  std::vector<std::pair<size_t, Target>> todo;
  for (size_t i = 0; i < a.targets.size(); ++i) {
    const auto& state = a.checked[i].state;
    if (state.verification_pending && state.launch_candidate && !state.receipt_current && !state.receipt_stale)
      todo.emplace_back(i, a.targets[i]);
  }
  if (todo.empty()) return;
  a.hint_check = std::thread([&a, root = a.root, todo = std::move(todo), generation = a.hint_generation] {
    App::Hints found{generation, {}};
    try {
      ActionTrace trace("ui.hints");
      ReceiptHints hints{ProjectPaths{root}};
      for (const auto& [index, target] : todo) {
        if (a.closing) break;
        found.items.emplace_back(index, hints.Check(target.id, target.manifest, target.exe));
      }
    } catch (const std::exception&) {}  // a hint never fails the listing
    if (!a.closing) Post(a, [found = std::move(found)](App& app) { OnHinted(app, found); });
  });
}
void CheckEnvironmentAsync(App& a, const fs::path& root, bool verify_contents) {
  if (a.busy) return;
  if (a.worker.joinable()) a.worker.join();
  a.pending_root = root; a.pending_full_check = verify_contents; a.busy = true; a.initializing = true; a.cancel = false;
  SetActivity(a, Activity::Check, Tr("Verificando ambiente", "Checking environment"), Tr("Conferindo a instalação em segundo plano.", "Verifying the installation in the background."));
  a.worker = std::thread([&a, root] {
    std::string error;
    try {
      DigestBatch batch([&a] { return a.cancel.load(); });
      VerifyDistribution(a.self.parent_path());
      a.pending_payload = InspectEnvironment(Payload(a), root);
      const auto installed = DigestFile(root / ".local/payload.sha256");
      const auto packaged = DigestFile(Payload(a) / "payload.sha256");
      a.pending_payload_current = a.pending_payload.current && installed && packaged && installed->sha256 == packaged->sha256;
    } catch (const std::exception& e) { error = e.what(); }
    Post(a, [error](App& app) { OnEnvironmentChecked(app, error); });
  });
}

// ---------- Self-update (installed packages only) ----------
namespace {
std::string Cancelled() { return Tr("Atualização cancelada. Nada foi alterado.", "Update cancelled. Nothing was changed."); }
// HTTPS only, redirects included; the system curl avoids linking a network stack.
void Download(const App& a, const std::string& url, const fs::path& file, int seconds, const std::atomic<bool>& stop) {
  ProcessSpec spec;
  spec.exe = SystemTool("curl");
  spec.args = {"-fsSL", "--proto", "=https", "--proto-redir", "=https", "--connect-timeout", "15", "--max-time", std::to_string(seconds),
               "-A", std::string("x360-native/") + X360N_VERSION, "-H", "Accept: application/vnd.github+json", "-o", Utf8(file), url};
  spec.working_dir = file.parent_path();
  spec.stdout_file = Updates(a) / "curl.log"; spec.stderr_file = Updates(a) / "curl.error.log";
  spec.no_console = true;
  spec.should_cancel = [&stop] { return stop.load(); };
  const auto result = RunProcess(spec);
  if (result.cancelled) throw std::runtime_error(Cancelled());
  if (!result.launched || result.exit_code != 0)
    throw std::runtime_error(Tr("Não foi possível baixar ", "Could not download ") + url + " (" + result.launch_error + " " + DescribeExitCode(result.exit_code) + ")");
}
int64_t Stamp() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
}  // namespace
// Packages ask GitHub Releases once per start; nothing else is sent.
void CheckForUpdate(App& a) {
  if (a.preferences.empty() || a.update_check.joinable()) return;
  a.update_check = std::thread([&a] {
    std::optional<AvailableUpdate> found;
    try {
      fs::create_directories(Updates(a));
      const auto list = Updates(a) / "releases.json";
      Download(a, kReleasesApi, list, 30, a.closing);
      std::ifstream in(list, std::ios::binary);
      found = SelectUpdate({std::istreambuf_iterator<char>(in), {}}, X360N_VERSION, a.update_channel);
    } catch (const std::exception&) { found.reset(); }  // offline or rate limited: try again next start
    if (!a.closing) Post(a, [found](App& app) { OnUpdateChecked(app, found); });
  });
}
void StartUpdate(App& a) {
  if (a.busy || !a.update) return;
  if (a.worker.joinable()) a.worker.join();
  const auto update = *a.update;
  a.cancel = false; a.failed = false; a.busy = true; a.result = 0; a.stage = 0; a.last_action = kUpdate;
  a.show_details = false; a.settings = false;
  SetActivity(a, Activity::Import, Tr("Atualizando o aplicativo", "Updating the application"),
    Tr("Baixando a versão ", "Downloading version ") + update.version + Tr(". Seus jogos e configurações serão mantidos.", ". Your games and settings will be kept."),
    {Tr("Download", "Download"), Tr("Verificação", "Verification"), Tr("Instalação", "Installation")});
  a.worker = std::thread([&a, update] {
    std::string error;
    try {
      const auto dir = Updates(a) / update.version;
      fs::remove_all(dir);
      fs::create_directories(dir / "extract");
      const auto archive = dir / (update.package + PackageArchiveExtension()), checksum = dir / (update.package + PackageArchiveExtension() + ".sha256");
      Download(a, update.checksum_url, checksum, 60, a.cancel);
      Download(a, update.archive_url, archive, 3600, a.cancel);
      a.stage = 1;
      Post(a, [](App& app) { OnProgress(app, Tr("Verificando a atualização...", "Verifying the update...")); });
      VerifyArchive(archive, checksum);
      ProcessSpec tar;
      tar.exe = SystemTool("tar"); tar.args = {"-xf", Utf8(archive), "-C", Utf8(dir / "extract")};
      tar.working_dir = dir; tar.stdout_file = dir / "tar.log"; tar.stderr_file = dir / "tar.error.log"; tar.no_console = true;
      tar.should_cancel = [&a] { return a.cancel.load(); };
      const auto extracted = RunProcess(tar);
      if (extracted.cancelled) throw std::runtime_error(Cancelled());
      if (!extracted.launched || extracted.exit_code != 0) throw std::runtime_error(Tr("Não foi possível extrair a atualização. Nada foi alterado.", "Could not extract the update. Nothing was changed."));
      if (a.cancel) throw std::runtime_error(Cancelled());
      a.stage = 2;
      Post(a, [](App& app) { OnProgress(app, Tr("Instalando a atualização...", "Installing the update...")); });
      const auto transaction = Updates(a) / ("txn-" + std::to_string(CurrentProcess()) + "-" + std::to_string(Stamp()));
      ApplyUpdate(a.self.parent_path(), dir / "extract" / update.package, update.version, transaction);
      std::error_code ec; fs::remove_all(dir, ec);  // only this download
    } catch (const std::exception& e) { error = e.what(); }
    Post(a, [error](App& app) { OnUpdateFinished(app, error); });
  });
}

// ---------- Preparation stages ----------
std::vector<std::string> PreparationStageNames() {
  return {Tr("Computador", "Computer"), Tr("Componentes", "Components"), Tr("Ajustes", "Setup"),
          Tr("Conversão", "Conversion"), Tr("Preparação", "Preparation"),
          Tr("Verificação", "Verification"), Tr("Conclusão", "Completion")};
}
// Stages the supervisor's `x360n prepare` reports (aot_pipeline.cpp). Reused
// steps count as done; no percentage is ever derived from them.
int PreparationStage(const std::string& line) {
  const auto space = line.find_last_of(' ');
  const auto name = space == std::string::npos ? line : line.substr(space + 1);
  if (name == "reused-shared-sdk" || name == "sdk-configure" || name == "sdk-build") return 1;
  if (name == "1-configure" || name == "3-reconfigure" || name == "2-tool" || name == "safe-rebuild") return 2;
  if (name == "2-codegen" || name == "reused-codegen") return 3;
  if (name == "4-build") return 4;
  if (name == "prepared" || name == "reused-preparation") return 5;
  return -1;
}

void Start(App& a, int action, fs::path source) {
  ActionTrace trace("ui.start");
  if (a.busy || (action != kImport && a.selected >= a.targets.size())) return;
  if (a.root.empty()) throw std::runtime_error("Choose an environment before importing content.");
  if (action == kLaunch && !a.targets[a.selected].session) return;
  if (a.worker.joinable()) a.worker.join();
  a.logs = a.root / ".local/evidence/logs/desktop" / std::to_string(Stamp());
  a.cancel = false; a.failed = false; a.busy = true; a.session = false; a.installing = action == kInstall; a.importing = action == kImport; a.import_recognized = false; a.imported_id.clear();
  a.result = 0; a.last_action = action; a.last_source = source;
  const auto target = a.selected < a.targets.size() ? a.targets[a.selected] : Target{};
  a.launch_verified = false;
  a.pending_snapshot = false;
  a.preparing_id = action == kPrepare ? target.id : std::string();
  a.stage = 0;
  if (action == kImport)
    SetActivity(a, Activity::Import, Tr("Importando conteúdo", "Importing content"), Tr("Verificando o conjunto selecionado antes de copiar...", "Verifying the selected set before copying..."),
      {Tr("Extração", "Extraction"), Tr("Verificação", "Verification"), Tr("Importação", "Import")});
  else if (action == kLaunch)
    SetActivity(a, Activity::Session, Tr("Preparando sessão", "Preparing session"), Tr("Conferindo a integridade antes de abrir a dashboard.", "Verifying integrity before opening the dashboard."));
  else if (action == kVerifyTarget)
    SetActivity(a, Activity::Check, Tr("Conferindo preparação", "Checking preparation"), Tr("Conferindo os arquivos do conteúdo selecionado. Você pode cancelar.", "Checking the selected content. You can cancel."));
  else if (action == kInstall)
    SetActivity(a, Activity::Check, Tr("Instalando componentes", "Installing components"), Tr("Conclua ou cancele pelo instalador da Microsoft e aguarde o retorno.", "Finish or cancel in the Microsoft installer and wait for it to return."));
  else if (action == kRemove)
    SetActivity(a, Activity::Check, Tr("Removendo da biblioteca", "Removing from library"), Tr("Retirando o jogo da My Games e apagando a cópia importada. Saves e perfis são mantidos.", "Withdrawing the game from My Games and deleting the imported copy. Saves and profiles are kept."));
  else
    SetActivity(a, Activity::Prepare, Tr("Preparando conteúdo", "Preparing content"), Tr("Preparando para este computador. Você pode cancelar e continuar depois.", "Preparing for this computer. You can cancel and continue later."),
      PreparationStageNames());
  a.operation_target = target.id;
  a.show_details = false; a.settings = false;
  const bool capture_crash = a.capture_crash;
  a.worker = std::thread([&a, target, action, source, capture_crash] {
    auto progress = [&a](std::string text) { Post(a, [text = std::move(text)](App& app) { OnProgress(app, text); }); };
    auto verify_target = [&] {
      if (!a.pending_snapshot) { a.pending_targets = a.targets; a.pending_checked = a.checked; }
      auto check = [&](const std::string& id) -> Inspection {
        auto it = std::find_if(a.pending_targets.begin(), a.pending_targets.end(), [&](const auto& t) { return t.id == id; });
        if (it == a.pending_targets.end()) {
          Inspection missing; missing.inputs_ok = false;
          missing.issues.push_back("Required preparation profile is missing: " + id); return missing;
        }
        progress(Tr("Conferindo ", "Verifying ") + (english ? it->label_en : it->label));
        auto& item = a.pending_checked.at(size_t(it - a.pending_targets.begin()));
        item.state = Inspect(ProjectPaths{a.root}, *it); item.library.clear();
        if (item.state.prepared) item.library = InspectLibrary(ProjectPaths{a.root}, *it, a.pending_targets);
        if (!item.library.empty()) item.state.prepared = false;
        return item.state;
      };
      auto state = check(target.id);
      for (const auto& dep : target.dependencies) {
        const auto dependency = check(dep);
        state.inputs_ok = state.inputs_ok && dependency.inputs_ok;
        state.prepared = state.prepared && dependency.prepared;
        state.issues.insert(state.issues.end(), dependency.issues.begin(), dependency.issues.end());
      }
      a.pending_snapshot = true;
      return state;
    };
    auto finish = [&](const std::string& message) {
      ActionTrace trace("operation.finish");
      // Session completion must never wait for an unrelated catalog/hash scan.
      try { if (action != kLaunch && action != kVerifyTarget && !a.cancel.load()) {
        DigestBatch batch([&a] { return a.cancel.load(); });
        a.pending_targets = action == kImport || action == kRemove ? ReadCatalog(ProjectPaths{a.root}) : a.targets;
        a.pending_checked = CheckContents(a.root, a.pending_targets, InspectionMode::Preview);
        a.pending_snapshot = true;
        if (action == kPrepare && !a.failed) verify_target();
      } }
      catch (const std::exception& error) { a.failed = true; Post(a, [text = std::string(error.what())](App& app) { OnFinished(app, text); }); return; }
      std::ofstream(a.logs / "result.txt", std::ios::binary) << message << '\n';
      Post(a, [message](App& app) { OnFinished(app, message); });
    };
    try {
      fs::create_directories(a.logs);
      if (!a.preferences.empty()) {
        ActionTrace trace("environment.preflight");
        VerifyDistribution(a.self.parent_path());
        if (!InspectEnvironment(Payload(a), a.root).current)
          throw std::runtime_error("Esta instalação precisa ser atualizada. Selecione novamente o ambiente em Configurações. Seus jogos e dados serão preservados.");
      }
      if (action == kRemove) {
        RemoveGame(ProjectPaths{a.root}, a.targets, target.id);
        finish((english ? target.label_en : target.label) + "\n" + Tr("Removido da biblioteca e da My Games. Saves, perfis e o arquivo original foram mantidos.", "Removed from the library and My Games. Saves, profiles and the original file were kept.")); return;
      }
      if (action == kPrepare && !target.aot) throw std::runtime_error("Import a valid XEX game before preparation.");
      if (action == kPrepare && !target.preparation_source.empty())
        Import(ProjectPaths{a.root}, target, target.preparation_source, [&] { return a.cancel.load(); });
      if (action == kPrepare || action == kVerifyTarget || action == kLaunch) {
        ActionTrace trace("launch.preflight");
        if (action != kVerifyTarget) {
          progress(Tr("Conferindo a preparação e concluindo o registro na My Games...", "Checking preparation and completing My Games registration..."));
          if (CompleteLibraryPreparation(ProjectPaths{a.root}, target, a.targets, [&] { return a.cancel.load(); })) {
            if (action == kPrepare) {
              a.stage = 7;
              finish(Tr("Preparação concluída. Conteúdo verificado e registrado na My Games.", "Preparation complete. Content verified and registered in My Games.")); return;
            }
          } else if (action == kLaunch) {
            const auto state = verify_target();
            std::string reason = Tr("A preparação mudou. Selecione Preparar para atualizar os componentes antes de iniciar.", "Preparation changed. Select Prepare to update components before starting.");
            for (const auto& issue : state.issues) reason += "\n" + issue;
            throw std::runtime_error(reason);
          }
        }
        if (action != kLaunch) {
          DigestBatch batch([&a] { return a.cancel.load(); });
          const auto state = verify_target();
          if (action == kVerifyTarget) {
            finish(Tr("Verificação concluída. Consulte o estado e o Diagnóstico do título.", "Verification finished. See the title's status and Diagnostics.")); return;
          }
          if (!state.inputs_ok)
            throw std::runtime_error(Tr("O conteúdo mudou desde a verificação. Confira os detalhes e prepare novamente.", "Content changed since verification. Check details and prepare again."));
        }
      }
      if (action == kImport) {
        ImportSource content(source, Extractor(a), a.logs,
          [&] { return a.cancel.load(); }, [&] {
            progress(Tr("Extraindo o conteúdo...\nIsso pode levar alguns minutos. Você pode cancelar; o arquivo original será preservado.", "Extracting content...\nThis may take a few minutes. You can cancel; the original file will be preserved."));
          }, [&](const std::string& stage) { progress(stage); });
        if (content.system_update()) {
          const auto it = std::find_if(a.targets.begin(), a.targets.end(), [](const auto& t) { return t.id == "dash_17559"; });
          if (it == a.targets.end()) throw std::runtime_error("Falta o perfil de preparação Retail 17559 nesta instalação.");
          a.stage = 2; a.imported_id = it->id;
          progress(Tr("Dashboard Xbox 360 — Retail 17559: adicionando componentes verificados...", "Xbox 360 Dashboard — Retail 17559: adding verified components..."));
          Import(ProjectPaths{a.root}, *it, content.path(), [&] { return a.cancel.load(); }, true);
          a.import_recognized = true;
          const auto state = Inspect(ProjectPaths{a.root}, *it);
          std::string message = Tr("Dashboard Xbox 360 — Retail 17559\n", "Xbox 360 Dashboard — Retail 17559\n");
          if (state.inputs_ok) message += Tr("Conteúdo adicionado. Selecione Preparar para continuar.", "Content added. Select Prepare to continue.");
          else {
            message += Tr("Importação parcial. Complete os arquivos abaixo usando Importar arquivo ou pasta; a dashboard ainda não está pronta.\n", "Partial import. Supply the files below using Import file or folder; the dashboard is not ready yet.\n");
            for (const auto& issue : state.issues) message += issue + "\n";
            for (std::istringstream report(content.report()); report;) {
              std::string line; std::getline(report, line);
              if (line.starts_with("missing\t")) message += line.substr(8) + "\n";
            }
          }
          finish(message); return;
        }
        a.stage = 1;
        progress(Tr("Identificando o conteúdo...\nConferindo os arquivos antes de adicionar.", "Identifying the content...\nChecking the files before adding."));
        if (fs::is_directory(content.path()) && fs::is_regular_file(content.path() / "default.xex")) {
          a.stage = 2;
          a.imported_id = ImportGame(ProjectPaths{a.root}, a.targets, content.path(), source, [&] { return a.cancel.load(); });
          a.import_recognized = true;
          finish(Tr("Jogo adicionado à Biblioteca.", "Game added to Library.")); return;
        }
        const auto found = IdentifyContent(a.targets, content.identity_path(), [&] { return a.cancel.load(); });
        std::vector<std::string> ids;
        for (const auto& item : found) if (item.recognized && std::find(ids.begin(), ids.end(), item.target_id) == ids.end()) ids.push_back(item.target_id);
        if (ids.size() > 1) throw std::runtime_error(Tr("Há mais de uma versão nesta pasta. Escolha a subpasta da versão que deseja adicionar. Nada foi copiado.", "This folder contains multiple versions. Choose the version's subfolder. Nothing was copied."));
        if (ids.empty()) {
          std::string message = Tr("Nenhum jogo compatível foi encontrado. O original foi preservado. Selecione outra ISO, pacote Xbox 360 ou pasta extraída.", "No compatible game was found. The original was preserved. Choose another ISO, Xbox 360 package, or extracted folder.");
          if (!found.empty()) message = (english ? "Content identified" : found.front().label) + (found.front().version.empty() ? "" : " — " + found.front().version) + "\n" + (english ? "No preparation is available for this file. Select supported, complete extracted content." : found.front().problem);
          finish(message); return;
        }
        auto selected = *std::find_if(a.targets.begin(), a.targets.end(), [&](const auto& t) { return t.id == ids.front(); });
        if (selected.kind == "game") {
          a.imported_id = ImportGame(ProjectPaths{a.root}, a.targets, content.path(), source, [&] { return a.cancel.load(); });
          a.import_recognized = true;
          finish(Tr("Jogo adicionado à Biblioteca.", "Game added to Library.")); return;
        }
        a.imported_id = selected.id;
        if (!selected.importable) { finish((english ? selected.label_en : selected.label) + "\n" + Tr("Esta versão foi reconhecida. Preparação ainda indisponível; consulte o Diagnóstico.", "This version was recognized. Preparation is not yet available; see Diagnostics.")); return; }
        a.stage = 2;
        progress(Tr("Conferindo e adicionando o conteúdo extraído...", "Checking and adding the extracted content..."));
        Import(ProjectPaths{a.root}, selected, content.path(), [&] { return a.cancel.load(); }, !content.extracted());
        a.import_recognized = true;
        finish((english ? selected.label_en : selected.label) + "\n" + Tr("Conteúdo verificado e adicionado. Seus originais foram preservados.", "Content verified and added. Your originals are preserved.")); return;
      }
      auto run = [&](fs::path exe, std::vector<std::string> args, const std::string& label, uintptr_t closed_event = 0) {
        ProcessSpec spec;
        spec.exe = exe; spec.args = args; spec.working_dir = a.root;
        spec.stdout_file = a.logs / (label + ".log"); spec.stderr_file = a.logs / (label + ".error.log");
        spec.no_console = true;
        spec.collect_metrics = action == kPrepare && (ActionTrace::Enabled() || std::getenv("X360N_PREPARATION_METRICS"));
        if (action != kInstall) spec.should_cancel = [&] { return a.cancel.load(); };
        if (action == kLaunch) spec.on_started = [&] { Post(a, [](App& app) { OnSessionStarted(app); }); };
        spec.notification_event = closed_event;
        spec.on_notification = [&] { Post(a, [](App& app) { OnSessionClosed(app); }); };
        auto next_sample = std::chrono::steady_clock::now();
        std::string previous_stage;
        if (action == kPrepare) spec.on_tick = [&] {
          const auto now = std::chrono::steady_clock::now();
          if (now < next_sample) return;
          next_sample = now + std::chrono::milliseconds(500);
          // Read only the current step's bounded log tail, on the process
          // worker. Never rescan whole logs or extraction trees on the UI thread.
          std::ifstream in(spec.stdout_file, std::ios::binary | std::ios::ate);
          if (!in) return;
          const auto size = in.tellg();
          in.seekg(std::max<std::streamoff>(0, static_cast<std::streamoff>(size) - 65536));
          std::string last;
          for (std::string line; std::getline(in, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.starts_with("X360N_STAGE ")) last = line.substr(12);
          }
          if (last.empty() || last == previous_stage) return;
          previous_stage = last;
          const int mapped = label == "tools" ? 0 : PreparationStage(last);
          if (mapped > a.stage.load()) a.stage = mapped;
          if (mapped >= 0)
            progress(Tr("Preparação: ", "Preparation: ") + PreparationStageNames().at(mapped));
        };
        auto result = RunProcess(spec);
        if (spec.collect_metrics) {
          std::ofstream metrics(a.logs / "preparation-metrics.tsv", std::ios::app);
          // Whole process-tree totals, including every compiler worker.
          metrics << label << '\t' << result.duration.count() << '\t' << result.exit_code
                  << '\t' << result.cancelled << '\t' << result.timed_out;
          if (result.metrics) {
            const auto& m = *result.metrics;
            metrics << '\t' << m.cpu_us << '\t' << m.read_bytes << '\t' << m.read_operations
                    << '\t' << m.write_bytes << '\t' << m.peak_job_commit_bytes;
          } else metrics << "\tunavailable";
          metrics << '\n';
        }
        std::string launch_detail;
        if (action == kLaunch) {
          // The supervisor performs the complete preflight. Consume its bounded
          // status record only after exit; never rehash the launch closure here.
          std::ifstream verified(spec.stdout_file, std::ios::binary);
          std::string prefix(65536, '\0');
          verified.read(prefix.data(), std::streamsize(prefix.size()));
          prefix.resize(size_t(verified.gcount()));
          a.launch_verified = prefix.find("X360N_LAUNCH_VERIFIED\n") != std::string::npos ||
                              prefix.find("X360N_LAUNCH_VERIFIED\r\n") != std::string::npos;
          if (result.exit_code != 0 || !result.launched) {
            std::ifstream error(spec.stderr_file, std::ios::binary);
            launch_detail.resize(4096); error.read(launch_detail.data(), std::streamsize(launch_detail.size()));
            launch_detail.resize(size_t(error.gcount()));
          }
        }
        if (result.cancelled) throw std::runtime_error("Preparação cancelada. Os originais e dados de usuário foram preservados; tente preparar novamente para retomar as etapas válidas.");
        if (!result.launched || result.exit_code != 0)
          throw std::runtime_error("A etapa " + label + " não concluiu. " + result.launch_error + " " + DescribeExitCode(result.exit_code) +
              (launch_detail.empty() ? "" : "\n" + launch_detail) + "\nDiagnóstico local: " + Utf8(a.logs));
      };
      if (action == kLaunch) {
        SessionNotification notification({}, true);
        progress(Tr("Conferindo ", "Verifying ") + (english ? target.label_en : target.label));
        std::vector<std::string> args{"--root", Utf8(a.root), "session", Utf8(target.manifest), "--catalog-target", target.id, "--timeout", "0", "--closed-event", notification.name()};
        if (capture_crash) args.push_back("--crash-stack");
        if (!a.session_user_data.empty()) { args.push_back("--user-data-root"); args.push_back(Utf8(a.session_user_data)); }
        run(Supervisor(a), args, "session", notification.handle());
        finish(Tr("Sessão encerrada.", "Session ended."));
        return;
      }
#ifdef _WIN32
      const auto powershell = SystemTool("WindowsPowerShell/v1.0/powershell");
      if (action == kInstall) {
        run(powershell, {"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", Utf8(a.root / "scripts/setup/install-build-tools.ps1"), "-AllowInstallation"}, "msvc");
        finish(Tr("Instalador encerrado. Verifique as dependências novamente antes de preparar.", "Installer closed. Recheck dependencies before preparing.")); return;
      }
      // Pinned Git/CMake/Ninja/LLVM; the shared preparation then runs natively.
      auto tool_args = std::vector<std::string>{"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", Utf8(a.root / "scripts/prepare-tools.ps1"), "-UpdateDependencies"};
      if (a.allow_downloads) tool_args.push_back("-AllowDownloads");
      progress(Tr("Verificando os componentes necessários...", "Checking required components..."));
      run(powershell, tool_args, "tools");
#else
      if (action == kInstall) throw std::runtime_error("System installers are only offered on Windows.");
      progress(Tr("Verificando os componentes necessários...", "Checking required components..."));
      std::string missing;
      for (const char* tool : {"clang", "clang++", "cmake", "ninja", "git"}) {
        try { (void)FindHostTool(tool); } catch (const std::exception&) { missing += std::string(missing.empty() ? "" : ", ") + tool; }
      }
      if (!missing.empty()) throw std::runtime_error(Tr("Ferramentas de compilação ausentes no PATH: ", "Build tools missing from PATH: ") + missing);
#endif
      a.stage = 1;
      if (target.generic_aot) GenerateAotProject(ProjectPaths{a.root}, target);
      auto prepare = [&](const std::string& id) {
        if (a.cancel) throw std::runtime_error("Preparação cancelada.");
        Target pending; pending.id = id;
        const auto marker = PreparationMarker(ProjectPaths{a.root}, pending);
        auto item = std::find_if(a.targets.begin(), a.targets.end(), [&](const auto& t) { return t.id == id; });
        auto label = item == a.targets.end() ? id : (english ? item->label_en : item->label);
        progress(Tr("Preparando ", "Preparing ") + label + Tr(".\nA primeira preparação pode levar vários minutos.", ".\nFirst-time preparation can take several minutes."));
        // The supervisor's shared AOT pipeline owns SDK, codegen, build, cache and receipts.
        const auto command = PreparationCommand(a.root, Supervisor(a), {"--root", Utf8(a.root), "prepare", id, "--keep-incomplete"});
        run(command.exe, command.args, id);
        if (id != target.id) fs::remove(marker);
      };
      for (const auto& dep : target.dependencies) prepare(dep);
      prepare(target.id);
      a.stage = 6;
      progress(Tr("Registrando conteúdo verificado na biblioteca original...", "Registering verified content in the original library..."));
      fs::remove(PreparationMarker(ProjectPaths{a.root}, target));
      if (!CompleteLibraryPreparation(ProjectPaths{a.root}, target, a.targets, [&] { return a.cancel.load(); }))
        throw std::runtime_error(Tr("A preparação não produziu componentes válidos. Consulte o Diagnóstico.", "Preparation did not produce valid components. See Diagnostics."));
      fs::remove(PreparationMarker(ProjectPaths{a.root}, target));
      a.stage = 7;
      finish(Tr("Preparação concluída. Os componentes serão conferidos antes de habilitar a sessão.", "Preparation finished. Components will be checked before enabling the session."));
    } catch (const std::exception& error) {
      // The driver records failures of builds it actually owns. Tools/import/
      // registration errors stay in this operation's logs and library inspection.
      a.failed = true; finish(error.what());
    }
  });
}

// Review states for layout checks and captures; never used by the package.
void ApplyPreview(App& a, int state) {
#ifndef X360N_DISTRIBUTION
  using namespace std::chrono_literals;
  switch (state) {
    case 1: a.busy = true; a.importing = true; a.session = false; a.initializing = false; a.stage = 0;
      SetActivity(a, Activity::Import, Tr("Importando conteúdo", "Importing content"), Tr("Extraindo o jogo...\nIsso pode levar alguns minutos. Você pode cancelar; o arquivo original será preservado.", "Extracting the game...\nThis may take a few minutes. You can cancel; the original file will be preserved."),
        {Tr("Extração", "Extraction"), Tr("Verificação", "Verification"), Tr("Importação", "Import")});
      a.started = App::Clock::now() - 75s; a.extracted_bytes = 1503238553ull; break;
    case 2: a.busy = true; a.importing = false; a.session = false; a.initializing = false; a.stage = 4;
      SetActivity(a, Activity::Prepare, Tr("Preparando conteúdo", "Preparing content"), Tr("Preparando para este computador…\nVocê pode cancelar e continuar depois.", "Preparing for this computer…\nYou can cancel and continue later."),
        PreparationStageNames());
      a.started = App::Clock::now() - 421s; break;
    case 3: a.busy = false; a.result = 3; a.importing = false; a.result_title = Tr("Não foi possível concluir", "Could not finish");
      a.activity_text = Tr("Consulte o Diagnóstico para ver o motivo e tente novamente.", "Check Diagnostics for the cause and try again.");
      a.operation_details = "codegen failed; exit_code=1"; break;
    case 4: a.busy = false; a.result = 1; a.importing = true; a.result_title = Tr("Importação concluída", "Import finished");
      a.activity_text = Tr("Forza Horizon 0.0.0.10\nConteúdo verificado e adicionado. Seus originais foram preservados.", "Forza Horizon 0.0.0.10\nContent verified and added. Your originals are preserved."); break;
    case 5: a.busy = false; a.result = 0; a.importing = false; a.preview_empty = false; a.preparing_id.clear(); break;
    case 6: {
      a.preview_empty = false;
      const char* names[] = {"Castle Crashers", "Forza Horizon", "Halo 3", "Gears of War 3", "Alan Wake", "Fable III", "Minecraft: Xbox 360 Edition", "Red Dead Redemption: Game of the Year Edition with a very long title", "Crimson Skies: High Road to Revenge"};
      const char* ids[] = {"584111F7", "4D5309C9", "4D5307E6", "4D5308AB", "4D53082D", "4D5308CC", "584111F7", "5454082B", "4D53004C"};
      std::vector<Target> targets; std::vector<App::CheckedTarget> checked;
      for (size_t i = 0; i < a.targets.size(); ++i) if (a.targets[i].id.rfind("preview_", 0) != 0) {
        targets.push_back(a.targets[i]); checked.push_back(i < a.checked.size() ? a.checked[i] : App::CheckedTarget{});
      }
      a.targets = std::move(targets); a.checked = std::move(checked);
      for (int i = 0; i < 9; ++i) {
        Target t; t.id = "preview_" + std::to_string(i); t.label = t.label_en = names[i]; t.title_id = ids[i];
        t.version = std::string("0.0.") + std::to_string(i + 1) + ".0"; t.catalogued = true; t.aot = i != 5 && i != 7; t.kind = "game";
        App::CheckedTarget c; c.added = true; c.state.inputs_ok = i != 3; c.state.prepared = i % 3 == 0 || i == 4; c.state.interrupted = i == 8;
        if (i == 2) c.library.push_back("missing");
        a.targets.push_back(t); a.checked.push_back(c);
      }
      a.home = false; a.import_view = false; a.games = true; a.settings = a.show_details = false;
      a.dashboard_indices.clear();
      for (size_t i = 0; i < a.targets.size(); ++i) if (a.targets[i].kind != "dashboard" && a.checked[i].added) a.dashboard_indices.push_back(i);
      RebuildRows(a); if (!a.rows.empty()) a.selected = a.rows.front(); break;
    }
    case 7: a.preview_empty = true; a.games = true; a.home = false; a.import_view = false; a.settings = a.show_details = false; a.selected = a.targets.size(); RebuildRows(a); break;
  }
  if (state == 7) a.selected = a.targets.size();
  InspectSelected(a);
#else
  (void)a; (void)state;
#endif
}
}  // namespace x360n::desktop
