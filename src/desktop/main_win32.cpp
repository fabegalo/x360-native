#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <Xinput.h>
#include <dwmapi.h>
#include <atomic>
#include <algorithm>
#include <fstream>
#include <memory>
#include <thread>
#include "catalog.h"
#include "child_process.h"
#include "environment_win32.h"
#include "workspace_lease.h"

namespace fs = std::filesystem;
using namespace x360n;
using namespace x360n::desktop;
namespace {
constexpr UINT kFinished = WM_APP + 1, kProgress = WM_APP + 2, kDetected = WM_APP + 3;
enum { kFolder = 101, kTargets, kCheck, kImport, kPrepare, kLaunch, kDetails, kData, kCancel, kLanguage, kInstall, kStorage, kPrimary, kSettings };
constexpr COLORREF background = RGB(15, 20, 27), surface = RGB(24, 32, 42), foreground = RGB(236, 241, 247), muted = RGB(157, 171, 189), accent = RGB(169, 231, 154);
HBRUSH background_brush = CreateSolidBrush(background), surface_brush = CreateSolidBrush(surface);
bool english = false;
std::string Tr(const char* pt, const char* en) { return english ? en : pt; }
std::wstring Wide(const std::string& text) {
  if (text.empty()) return {};
  auto n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
  std::wstring out(n, L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), out.data(), n);
  return out;
}
struct App {
  HWND window{}, title{}, subtitle{}, root_text{}, combo{}, status{}, details{}, headline{}, footer{};
  HFONT font{}, title_font{}, headline_font{};
  HICON icon{};
  std::vector<HWND> buttons;
  fs::path self, root, logs, preferences;
  std::string startup_error;
  Environment environment;
  std::vector<Target> targets;
  std::vector<size_t> dashboard_indices;
  Inspection current;
  size_t selected = 0;
  std::thread worker;
  std::atomic<bool> cancel{false};
  std::atomic<bool> failed{false};
  bool busy = false, session = false, show_details = false;
  UINT dpi = 96;
  WORD buttons_down = 0;
  unsigned timer_ticks = 0;
  bool allow_downloads = false;
  bool installing = false;
  bool settings = false;
  int next_action = kFolder;
  ~App() { if (worker.joinable()) worker.join(); DeleteObject(font); DeleteObject(title_font); DeleteObject(headline_font); DestroyIcon(icon); }
};
void Set(HWND h, const std::string& text) { SetWindowTextW(h, Wide(text).c_str()); }
void SyncSelection(App& a) {
  auto it = std::find(a.dashboard_indices.begin(), a.dashboard_indices.end(), a.selected);
  SendMessageW(a.combo, CB_SETCURSEL, it == a.dashboard_indices.end() ? -1 : it - a.dashboard_indices.begin(), 0);
}
void Post(HWND window, UINT type, const std::string& text) {
  auto* value = new std::string(text);
  if (!PostMessageW(window, type, 0, reinterpret_cast<LPARAM>(value))) delete value;
}
HWND Control(App& a, const wchar_t* cls, const wchar_t* text, DWORD style, int id = 0) {
  if (std::wstring_view(cls) == L"BUTTON") style |= BS_OWNERDRAW;
  auto h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
                         a.window, reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
  SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(a.font), TRUE);
  return h;
}
void Layout(App& a) {
  RECT client{}; GetClientRect(a.window, &client);
  auto px = [&](int x) { return MulDiv(x, int(a.dpi), 96); };
  int width = MulDiv(client.right, 96, a.dpi), height = MulDiv(client.bottom, 96, a.dpi);
  auto move = [&](HWND h, int x, int y, int w, int hgt) { MoveWindow(h, px(x), px(y), px(w), px(hgt), TRUE); };
  auto button = [&](int id, int x, int y, int w, int hgt, bool visible) {
    auto h = GetDlgItem(a.window, id); move(h, x, y, w, hgt); ShowWindow(h, visible ? SW_SHOW : SW_HIDE);
  };
  move(a.title, 40, 28, 260, 32);
  button(kLanguage, width - 378, 22, 100, 40, true);
  button(kSettings, width - 270, 22, 140, 40, true);
  button(kDetails, width - 122, 22, 90, 40, true);
  move(a.headline, 60, 112, width - 120, 62);
  move(a.subtitle, 60, 186, width - 120, 52);
  button(kPrimary, 60, 264, 270, 54, !a.settings && !a.show_details);
  button(kImport, 346, 264, 210, 54, !a.settings && !a.show_details && a.current.prepared && !a.busy);
  button(kCancel, 346, 264, 190, 54, !a.settings && !a.show_details && a.busy && !a.session);
  button(kPrepare, 0, 0, 0, 0, false); button(kLaunch, 0, 0, 0, 0, false);
  button(kFolder, 84, 448, 208, 44, a.settings);
  button(kStorage, 308, 448, 208, 44, a.settings);
  button(kData, 532, 448, 208, 44, a.settings);
  button(kCheck, 84, 508, 208, 40, a.settings);
  move(a.root_text, 84, 406, width - 168, 32); ShowWindow(a.root_text, a.settings ? SW_SHOW : SW_HIDE);
  move(a.combo, 84, 410, width - 168, 210);
  ShowWindow(a.combo, !a.settings && !a.show_details ? SW_SHOW : SW_HIDE);
  move(a.status, 84, 460, width - 168, 86);
  ShowWindow(a.status, !a.settings && !a.show_details ? SW_SHOW : SW_HIDE);
  move(a.details, 60, 266, width - 120, std::max(200, height - 348));
  ShowWindow(a.details, a.show_details ? SW_SHOW : SW_HIDE);
  move(a.footer, 40, height - 42, width - 80, 28);
  InvalidateRect(a.window, nullptr, TRUE);
}
void Enable(App& a) {
  bool has = a.selected < a.targets.size();
  for (auto b : a.buttons) EnableWindow(b, !a.busy);
  EnableWindow(a.combo, !a.busy);
  EnableWindow(GetDlgItem(a.window, kImport), !a.busy && has && a.targets[a.selected].importable);
  EnableWindow(GetDlgItem(a.window, kPrepare), !a.busy && has && a.current.inputs_ok && a.environment.writable && a.environment.architecture);
  EnableWindow(GetDlgItem(a.window, kLaunch), !a.busy && has && a.current.prepared && a.targets[a.selected].session && a.environment.graphics);
  EnableWindow(GetDlgItem(a.window, kData), !a.root.empty());
  EnableWindow(GetDlgItem(a.window, kStorage), !a.busy && !a.root.empty());
  EnableWindow(GetDlgItem(a.window, kDetails), TRUE);
  EnableWindow(GetDlgItem(a.window, kCancel), a.busy && !a.session && !a.installing);
  const bool action_ok = a.next_action == kFolder ||
    (a.next_action == kImport && has && a.targets[a.selected].importable) ||
    (a.next_action == kPrepare && a.current.inputs_ok && a.environment.writable && a.environment.architecture) ||
    (a.next_action == kLaunch && a.current.prepared && a.environment.graphics);
  EnableWindow(GetDlgItem(a.window, kPrimary), !a.busy && action_ok);
  EnableWindow(GetDlgItem(a.window, kSettings), !a.busy);
  Layout(a);
}
void InspectSelected(App& a) {
  a.current = {};
  a.current.inputs_ok = false;
  if (a.selected >= a.targets.size()) {
    a.next_action = kFolder;
    Set(a.headline, Tr("Seu Xbox 360 começa aqui.", "Your Xbox 360 starts here."));
    Set(a.subtitle, Tr("Escolha onde guardar seus arquivos. Depois, importe a dashboard e o jogo.\r\nA preparação acontece neste computador; seus arquivos não são enviados.", "Choose where to keep your files, then import the dashboard and game.\r\nPreparation happens on this computer; your files are never uploaded."));
    Set(GetDlgItem(a.window, kPrimary), Tr("Escolher pasta", "Choose folder"));
    Set(a.status, Tr("Você pode mudar a pasta e o idioma em Configurações.\r\nDashboard e jogos não estão incluídos no aplicativo.", "You can change the folder and language in Settings.\r\nDashboard and games are not included in the app."));
    Enable(a); return;
  }
  const auto& t = a.targets[a.selected];
  a.current = Inspect(ProjectPaths{a.root}, t);
  std::string missing = a.current.inputs_ok ? "" : (english ? t.label_en : t.label);
  std::string detail;
  detail += a.environment.description + "\r\n";
  for (const auto& issue : a.current.issues) detail += issue + "\r\n";
  for (const auto& dep : t.dependencies) {
    auto it = std::find_if(a.targets.begin(), a.targets.end(), [&](const auto& candidate) { return candidate.id == dep; });
    if (it == a.targets.end()) {
      a.current.prepared = false; a.current.inputs_ok = false;
      detail += "Perfil necessário não encontrado: " + dep + "\r\n";
      continue;
    }
    auto r = Inspect(ProjectPaths{a.root}, *it);
    if (missing.empty() && !r.inputs_ok) missing = english ? it->label_en : it->label;
    a.current.inputs_ok = a.current.inputs_ok && r.inputs_ok;
    a.current.prepared = a.current.prepared && r.prepared;
    for (const auto& issue : r.issues) detail += dep + ": " + issue + "\r\n";
  }
  if (a.current.inputs_ok) {
    for (const auto& issue : InspectLibrary(t, a.targets)) {
      a.current.prepared = false; detail += issue + "\r\n";
    }
  }
  auto supervisor = a.self.parent_path() / "x360n.exe";
  if (!fs::is_regular_file(supervisor)) {
    a.current.prepared = false;
    detail += "Falta o supervisor ao lado do aplicativo: " + Utf8(supervisor) + "\r\n";
  }
  a.next_action = !a.current.inputs_ok ? kImport : !a.current.prepared ? kPrepare : t.session ? kLaunch : kPrepare;
  Set(a.headline, !a.current.inputs_ok ? Tr("Traga seus arquivos.", "Bring your files.") :
      !a.current.prepared ? Tr("Tudo pronto para preparar.", "Ready to prepare.") : Tr("Sua dashboard está pronta.", "Your dashboard is ready."));
  Set(a.subtitle, !a.current.inputs_ok ? Tr("Selecione ou arraste a pasta extraída do conjunto abaixo.\r\nVamos identificar a versão e verificar os arquivos antes de copiar.", "Choose or drop the extracted folder for the set below.\r\nWe identify the version and verify every file before copying.") :
      !a.current.prepared ? Tr("O aplicativo obtém as ferramentas e prepara a execução local.\r\nEssa etapa pode demorar na primeira vez; depois, é só abrir.", "The app obtains the tools and prepares local execution.\r\nThe first preparation can take a while; after that, just open it.") :
      Tr("Abra o menu original do Xbox 360.\r\nSeus jogos continuam na My Games, dentro da dashboard.", "Open the original Xbox 360 menu.\r\nYour games stay in My Games, inside the dashboard."));
  Set(GetDlgItem(a.window, kPrimary), a.next_action == kImport ? Tr("Selecionar arquivos", "Choose files") :
      a.next_action == kPrepare ? Tr("Verificar e preparar", "Verify and prepare") : Tr("Abrir dashboard   →", "Open dashboard   →"));
  std::string state = !missing.empty() ? Tr("Próximo conjunto: ", "Next set: ") + missing :
    a.current.prepared ? Tr("Conteúdo verificado. Lance Castle Crashers pelo menu original.\r\nUse Return to Arcade no jogo para voltar à dashboard.", "Content verified. Launch Castle Crashers from the original menu.\r\nUse Return to Arcade in the game to return to the dashboard.") :
    Tr("Conteúdo verificado. A preparação será feita nesta pasta.\r\nDownloads e instalações serão informados antes de começar.", "Content verified. Preparation will use this folder.\r\nDownloads and installations will be explained before they start.");
  if (!t.importable || !t.session) state = english ? t.limitations_en : t.limitations;
  detail += "\r\n" + (english ? t.limitations_en : t.limitations);
  if (!t.user_root.empty()) detail += "\r\nData: " + Utf8(t.user_root);
  Set(a.status, state);
  Set(a.details, detail.empty() ? "Verificação concluída. Nenhum dado é enviado automaticamente.\r\nLogs de execução: " + Utf8(a.root / ".local/runs") : detail);
  Enable(a);
}
void Load(App& a, const fs::path& root) {
  a.targets.clear(); a.dashboard_indices.clear(); a.selected = 0;
  SendMessageW(a.combo, CB_RESETCONTENT, 0, 0);
  a.root = root;
  a.environment = CheckEnvironment(root);
  SetWindowTextW(a.root_text, root.empty() ? L"Selecione um ambiente existente" : root.c_str());
  if (!root.empty()) a.targets = ReadCatalog(ProjectPaths{root});
  for (size_t i = 0; i < a.targets.size(); ++i) {
    if (!a.targets[i].session && !a.targets[i].id.starts_with("dash_")) continue;
    a.dashboard_indices.push_back(i);
    auto label = Wide(english ? a.targets[i].label_en : a.targets[i].label);
    SendMessageW(a.combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    if (a.targets[i].session) a.selected = i;
  }
  SyncSelection(a);
  InspectSelected(a);
}
void SavePreferences(const App& a) {
  if (a.preferences.empty()) return;
  fs::create_directories(a.preferences.parent_path());
  std::ofstream out(a.preferences, std::ios::binary);
  out << (english ? "en" : "pt") << '\n' << Utf8(a.root) << '\n';
}
void Labels(App& a) {
  for (auto [id, pt, en] : {std::tuple{kFolder, "Selecionar ambiente", "Choose environment"},
       {kCheck, "Verificar novamente", "Check again"}, {kImport, "Importar conjunto", "Import content"},
       {kPrepare, "Preparar", "Prepare"}, {kLaunch, "Abrir dashboard", "Open dashboard"},
       {kDetails, "Detalhes", "Details"}, {kData, "Abrir dados locais", "Open local data"},
       {kCancel, "Cancelar", "Cancel"}, {kStorage, "Pasta de saves", "Save data folder"},
       {kSettings, "Configurações", "Settings"}}) Set(GetDlgItem(a.window, id), Tr(pt, en));
  Set(GetDlgItem(a.window, kImport), Tr("Importar outro conjunto", "Import another set"));
  Set(GetDlgItem(a.window, kLanguage), english ? "EN / PT" : "PT / EN");
  Set(a.footer, Tr("PRÉVIA WINDOWS x64    ·    Perfis e save/load ainda não aprovados    ·    Seus arquivos ficam neste computador", "WINDOWS x64 PREVIEW    ·    Profiles and save/load not yet approved    ·    Your files stay on this computer"));
}
fs::path PickFolder(HWND owner, const wchar_t* title) {
  IFileOpenDialog* dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return {};
  DWORD options = 0; dialog->GetOptions(&options);
  dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
  dialog->SetTitle(title);
  fs::path result;
  if (SUCCEEDED(dialog->Show(owner))) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dialog->GetResult(&item))) {
      PWSTR path = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) { result = path; CoTaskMemFree(path); }
      item->Release();
    }
  }
  dialog->Release(); return result;
}
void Start(App& a, int action, fs::path source = {}) {
  if (a.busy || a.selected >= a.targets.size()) return;
  if (action == kLaunch && !a.targets[a.selected].session) return;
  if (a.worker.joinable()) a.worker.join();
  a.logs = a.root / ".local/logs/desktop" / std::to_string(GetTickCount64());
  fs::create_directories(a.logs);
  a.cancel = false; a.failed = false; a.busy = true; a.session = action == kLaunch; a.installing = action == kInstall;
  const auto target = a.targets[a.selected];
  Enable(a);
  Set(a.headline, action == kImport ? Tr("Conferindo seus arquivos.", "Checking your files.") :
      action == kLaunch ? Tr("A sessão está aberta.", "Your session is open.") : Tr("Preparando para você.", "Preparing for you."));
  Set(a.subtitle, action == kLaunch ? Tr("Continue na janela da dashboard original.\r\nO aplicativo continua aqui quando você encerrar a sessão.", "Continue in the original dashboard window.\r\nThis app stays here when you end your session.") :
      Tr("Acompanhe a etapa atual abaixo. Você pode cancelar e continuar depois.\r\nSeus arquivos originais e saves são preservados.", "Follow the current step below. You can cancel and continue later.\r\nYour original files and saves are preserved."));
  Set(GetDlgItem(a.window, kPrimary), action == kLaunch ? Tr("Em execução", "Running") : Tr("Em andamento…", "Working…"));
  Set(a.status, action == kImport ? Tr("Verificando o conjunto selecionado antes de copiar...", "Verifying the selected set before copying...") :
                action == kLaunch ? Tr("Sessão em andamento. Use My Games para iniciar o jogo e o menu do jogo para voltar. Feche a janela da plataforma para encerrar.", "Session running. Use My Games to launch and the game's menu to return. Close the platform window to exit.") :
                Tr("Preparação em andamento. As etapas reais serão registradas abaixo.", "Preparation running. Actual stages are recorded below."));
  a.show_details = false; a.settings = false; Layout(a);
  a.worker = std::thread([&a, target, action, source] {
    auto finish = [&](const std::string& message) {
      std::ofstream(a.logs / "result.txt", std::ios::binary) << message << '\n';
      Post(a.window, kFinished, message);
    };
    try {
      if (action == kImport) {
        const auto detected = DetectImportTargets(a.targets, source, [&] { return a.cancel.load(); });
        if (detected.size() > 1) throw std::runtime_error(Tr("A pasta contém mais de um conjunto reconhecido. Selecione a subpasta de uma única versão; nenhum arquivo foi copiado.", "The folder contains multiple recognized sets. Select one version's subfolder; no files were copied."));
        auto selected = target;
        if (detected.size() == 1) {
          selected = *std::find_if(a.targets.begin(), a.targets.end(), [&](const auto& t) { return t.id == detected.front(); });
          Post(a.window, kDetected, selected.id);
        }
        auto count = Import(ProjectPaths{a.root}, selected, source, [&] { return a.cancel.load(); });
        finish("Importação concluída: " + std::to_string(count) + " componentes copiados. Originais preservados."); return;
      }
      auto run = [&](fs::path exe, std::vector<std::string> args, const std::string& label) {
        ProcessSpec spec;
        spec.exe = exe; spec.args = args; spec.working_dir = a.root;
        spec.stdout_file = a.logs / (label + ".log"); spec.stderr_file = a.logs / (label + ".error.log");
        spec.no_console = true;
        if (action != kLaunch && action != kInstall) spec.should_cancel = [&] { return a.cancel.load(); };
        auto result = RunProcess(spec);
        if (result.cancelled) throw std::runtime_error("Preparação cancelada. Os originais e dados de usuário foram preservados; tente preparar novamente para retomar as etapas válidas.");
        if (!result.launched || result.exit_code != 0)
          throw std::runtime_error("A etapa " + label + " não concluiu. " + result.launch_error + " " + DescribeExitCode(result.exit_code) + "\r\nDiagnóstico local: " + Utf8(a.logs));
      };
      if (action == kLaunch) {
        run(a.self.parent_path() / "x360n.exe", {"--root", Utf8(a.root), "session", Utf8(target.manifest), "--timeout", "0"}, "session");
        finish("Sessão encerrada. Consulte os logs para os resultados da execução.");
      } else {
        wchar_t sys[MAX_PATH]{}; GetSystemDirectoryW(sys, MAX_PATH);
        fs::path powershell = fs::path(sys) / "WindowsPowerShell/v1.0/powershell.exe";
        if (action == kInstall) {
          run(powershell, {"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", Utf8(a.root / "scripts/install-build-tools.ps1"), "-AllowInstallation"}, "msvc");
          finish(Tr("Instalador encerrado. Verifique as dependências novamente antes de preparar.", "Installer closed. Recheck dependencies before preparing.")); return;
        }
        auto tool_args = std::vector<std::string>{"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", Utf8(a.root / "scripts/prepare-tools.ps1")};
        if (a.allow_downloads) tool_args.push_back("-AllowDownloads");
        Post(a.window, kProgress, Tr("Verificando ferramentas e dependências...", "Checking tools and dependencies..."));
        run(powershell, tool_args, "tools");
        auto prepare = [&](const std::string& id) {
          if (a.cancel) throw std::runtime_error("Preparação cancelada.");
          Target pending; pending.id = id;
          const auto marker = PreparationMarker(ProjectPaths{a.root}, pending);
          fs::create_directories(marker.parent_path());
          { std::ofstream out(marker); if (!out) throw std::runtime_error("Não foi possível registrar o início da preparação."); out << "incomplete\n"; }
          auto item = std::find_if(a.targets.begin(), a.targets.end(), [&](const auto& t) { return t.id == id; });
          auto label = item == a.targets.end() ? id : (english ? item->label_en : item->label);
          Post(a.window, kProgress, Tr("Preparando ", "Preparing ") + label + Tr(".\r\nA primeira preparação pode levar vários minutos.", ".\r\nFirst-time preparation can take several minutes."));
          run(powershell, {"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", Utf8(a.root / "scripts/build-title.ps1"), "-Title", id}, id);
          fs::remove(marker);
        };
        for (const auto& dep : target.dependencies) prepare(dep);
        prepare(target.id);
        Post(a.window, kProgress, Tr("Registrando conteúdo verificado na biblioteca original...", "Registering verified content in the original library..."));
        PrepareLibrary(ProjectPaths{a.root}, target, a.targets, [&] { return a.cancel.load(); });
        finish("Preparação concluída. A identidade dos componentes será conferida antes de habilitar a sessão.");
      }
    } catch (const std::exception& error) { a.failed = true; finish(error.what()); }
  });
}
HICON MakeIcon() {
  auto dc = GetDC(nullptr); auto mem = CreateCompatibleDC(dc);
  auto color = CreateCompatibleBitmap(dc, 32, 32); auto old = SelectObject(mem, color);
  RECT r{0, 0, 32, 32}; auto brush = CreateSolidBrush(RGB(30, 65, 94)); FillRect(mem, &r, brush); DeleteObject(brush);
  SetTextColor(mem, RGB(255, 255, 255)); SetBkMode(mem, TRANSPARENT);
  DrawTextW(mem, L"XN", 2, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  auto mask = CreateBitmap(32, 32, 1, 1, nullptr);
  auto maskdc = CreateCompatibleDC(dc); auto oldmask = SelectObject(maskdc, mask); PatBlt(maskdc, 0, 0, 32, 32, BLACKNESS);
  SelectObject(maskdc, oldmask); DeleteDC(maskdc);
  ICONINFO info{TRUE, 0, 0, mask, color}; auto icon = CreateIconIndirect(&info);
  SelectObject(mem, old); DeleteObject(mask); DeleteObject(color); DeleteDC(mem); ReleaseDC(nullptr, dc); return icon;
}
void Paint(App& a) {
  PAINTSTRUCT ps{}; auto dc = BeginPaint(a.window, &ps);
  RECT r{}; GetClientRect(a.window, &r); FillRect(dc, &r, background_brush);
  auto px = [&](int n) { return MulDiv(n, a.dpi, 96); };
  if (!a.show_details) {
    RECT card{px(60), px(392), r.right - px(60), px(568)};
    auto old_brush = SelectObject(dc, surface_brush); auto old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, card.left, card.top, card.right, card.bottom, px(18), px(18));
    SelectObject(dc, old_brush); SelectObject(dc, old_pen);
    if (!a.settings) {
      auto old_font = SelectObject(dc, a.font); SetBkMode(dc, TRANSPARENT);
      int span = (r.right - px(120)) / 3;
      const std::string steps[] = {Tr("01  Conteúdo", "01  Content"), Tr("02  Preparação", "02  Preparation"), Tr("03  Dashboard", "03  Dashboard")};
      for (int i = 0; i < 3; ++i) {
        bool done = i == 0 ? a.current.inputs_ok : a.current.prepared;
        auto brush = CreateSolidBrush(done ? accent : RGB(55, 70, 87));
        RECT bar{px(60) + i * span, px(348), px(60) + (i + 1) * span - px(16), px(351)}; FillRect(dc, &bar, brush); DeleteObject(brush);
        RECT label{bar.left, px(358), bar.right, px(383)}; SetTextColor(dc, done ? foreground : muted);
        DrawTextW(dc, Wide(steps[i]).c_str(), -1, &label, DT_SINGLELINE | DT_LEFT);
      }
      SelectObject(dc, old_font);
    }
  }
  EndPaint(a.window, &ps);
}
void DrawButton(App& a, const DRAWITEMSTRUCT& item) {
  const bool primary = item.CtlID == kPrimary, disabled = item.itemState & ODS_DISABLED;
  COLORREF fill = primary && !disabled ? accent : surface;
  if ((item.itemState & ODS_SELECTED) && !disabled) fill = primary ? RGB(143, 204, 131) : RGB(40, 54, 69);
  auto brush = CreateSolidBrush(fill); auto old_brush = SelectObject(item.hDC, brush);
  auto old_pen = SelectObject(item.hDC, GetStockObject(NULL_PEN));
  auto r = item.rcItem; RoundRect(item.hDC, r.left, r.top, r.right, r.bottom, 10, 10);
  SelectObject(item.hDC, old_brush); SelectObject(item.hDC, old_pen); DeleteObject(brush);
  SetBkMode(item.hDC, TRANSPARENT); SetTextColor(item.hDC, disabled ? RGB(92, 107, 124) : primary ? background : foreground);
  auto old_font = SelectObject(item.hDC, a.font); wchar_t text[512]{}; GetWindowTextW(item.hwndItem, text, 512);
  DrawTextW(item.hDC, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  SelectObject(item.hDC, old_font);
  if (item.itemState & ODS_FOCUS) { InflateRect(&r, -5, -5); DrawFocusRect(item.hDC, &r); }
}
LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
  auto* a = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    a = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
    a->window = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(a));
  }
  if (!a) return DefWindowProcW(window, message, wp, lp);
  try {
    switch (message) {
      case WM_ERASEBKGND: return 1;
      case WM_PAINT: Paint(*a); return 0;
      case WM_DRAWITEM: DrawButton(*a, *reinterpret_cast<DRAWITEMSTRUCT*>(lp)); return TRUE;
      case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: {
        auto h = reinterpret_cast<HWND>(lp); bool panel = h == a->status || h == a->root_text || h == a->details || message == WM_CTLCOLORLISTBOX;
        SetTextColor(reinterpret_cast<HDC>(wp), h == a->headline || h == a->title ? foreground : muted);
        SetBkColor(reinterpret_cast<HDC>(wp), panel ? surface : background);
        return reinterpret_cast<LRESULT>(panel ? surface_brush : background_brush);
      }
      case WM_CREATE: {
        a->dpi = GetDpiForWindow(window);
        BOOL dark = TRUE; DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
        a->font = CreateFontW(-MulDiv(10, a->dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        a->title_font = CreateFontW(-MulDiv(20, a->dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        a->headline_font = CreateFontW(-MulDiv(32, a->dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        a->icon = MakeIcon(); SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(a->icon));
        a->title = Control(*a, L"STATIC", L"x360-native", 0);
        SendMessageW(a->title, WM_SETFONT, reinterpret_cast<WPARAM>(a->title_font), TRUE);
        a->subtitle = Control(*a, L"STATIC", L"", 0);
        a->headline = Control(*a, L"STATIC", L"", 0);
        SendMessageW(a->headline, WM_SETFONT, reinterpret_cast<WPARAM>(a->headline_font), TRUE);
        a->footer = Control(*a, L"STATIC", L"", SS_ENDELLIPSIS);
        a->root_text = Control(*a, L"STATIC", L"", SS_PATHELLIPSIS);
        a->combo = Control(*a, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, kTargets);
        a->status = Control(*a, L"STATIC", L"", 0);
        a->details = Control(*a, L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER | WS_TABSTOP);
        for (const auto& pair : {std::pair{kFolder, L"Selecionar ambiente"}, {kCheck, L"Verificar novamente"}, {kImport, L"Importar conjunto"}, {kPrepare, L"Preparar"}, {kLaunch, L"Abrir dashboard"}, {kDetails, L"Ver detalhes"}, {kData, L"Abrir dados locais"}, {kCancel, L"Cancelar preparação"}})
          a->buttons.push_back(Control(*a, L"BUTTON", pair.second, WS_TABSTOP, pair.first));
        a->buttons.push_back(Control(*a, L"BUTTON", L"English / Português", WS_TABSTOP, kLanguage));
        a->buttons.push_back(Control(*a, L"BUTTON", L"", WS_TABSTOP, kStorage));
        a->buttons.push_back(Control(*a, L"BUTTON", L"", WS_TABSTOP, kPrimary));
        a->buttons.push_back(Control(*a, L"BUTTON", L"", WS_TABSTOP, kSettings));
        DragAcceptFiles(window, TRUE); SetTimer(window, 1, 100, nullptr);
        Labels(*a); Layout(*a); Load(*a, a->root);
        if (!a->startup_error.empty()) {
          Set(a->status, Tr("Não foi possível usar a pasta padrão. Escolha outra pasta para continuar.", "The default folder could not be used. Choose another folder to continue."));
          Set(a->details, a->startup_error);
        }
        return 0;
      }
      case WM_SIZE: if (a->title) Layout(*a); return 0;
      case WM_DPICHANGED: {
        a->dpi = HIWORD(wp);
        auto old = a->font, old_title = a->title_font, old_headline = a->headline_font;
        a->font = CreateFontW(-MulDiv(10, a->dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        a->title_font = CreateFontW(-MulDiv(20, a->dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        a->headline_font = CreateFontW(-MulDiv(32, a->dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        EnumChildWindows(window, [](HWND child, LPARAM font) -> BOOL { SendMessageW(child, WM_SETFONT, WPARAM(font), TRUE); return TRUE; }, reinterpret_cast<LPARAM>(a->font));
        SendMessageW(a->title, WM_SETFONT, reinterpret_cast<WPARAM>(a->title_font), TRUE);
        SendMessageW(a->headline, WM_SETFONT, reinterpret_cast<WPARAM>(a->headline_font), TRUE);
        DeleteObject(old); DeleteObject(old_title); DeleteObject(old_headline);
        auto rect = reinterpret_cast<RECT*>(lp);
        SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        Layout(*a); return 0;
      }
      case WM_GETMINMAXINFO: {
        auto* size = reinterpret_cast<MINMAXINFO*>(lp); size->ptMinTrackSize = {MulDiv(960, a->dpi, 96), MulDiv(700, a->dpi, 96)}; return 0;
      }
      case WM_COMMAND: {
        auto id = LOWORD(wp);
        if (id == kPrimary) id = a->next_action;
        if (id == IDOK) { auto focus = GetFocus(); if (focus && focus != a->combo && IsWindowEnabled(focus)) SendMessageW(focus, BM_CLICK, 0, 0); return 0; }
        if (id == kDetails || id == kSettings) {
          if (id == kDetails) { a->show_details = !a->show_details; a->settings = false; }
          else { a->settings = !a->settings; a->show_details = false; }
          if (a->show_details || a->settings) {
            Set(a->headline, a->settings ? Tr("Do seu jeito.", "Make it yours.") : Tr("Detalhes da preparação.", "Preparation details."));
            Set(a->subtitle, a->settings ? Tr("Escolha a dashboard e onde seus arquivos ficam guardados.\r\nTrocar a pasta não apaga os dados existentes.", "Choose your dashboard and where your files are stored.\r\nChanging the folder never deletes existing data.") : Tr("O diagnóstico fica neste computador. Nada é enviado automaticamente.", "Diagnostics stay on this computer. Nothing is uploaded automatically."));
          } else if (!a->busy) InspectSelected(*a);
          Layout(*a); return 0;
        }
        if (id == kData && !a->root.empty()) { auto p = a->root / ".local"; fs::create_directories(p); ShellExecuteW(window, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL); return 0; }
        if (id == kCancel) { a->cancel = true; Set(a->status, "Cancelando a preparação..."); return 0; }
        if (a->busy) return 0;
        if (id == kStorage) {
          auto path = PickFolder(window, Wide(Tr("Selecione a pasta de saves e perfis", "Select the saves and profiles folder")).c_str());
          if (!path.empty() && MessageBoxW(window, Wide(Tr("Usar esta pasta nas próximas sessões? Os dados anteriores permanecem na pasta original e não serão movidos ou apagados. Para reutilizar saves existentes, escolha a pasta que contém os diretórios dos títulos.", "Use this folder in future sessions? Previous data stays in its original folder and will not be moved or deleted. To reuse existing saves, choose the folder containing the title directories.")).c_str(), L"x360-native", MB_YESNO | MB_ICONINFORMATION) == IDYES) {
            WorkspaceLease lease(ProjectPaths{a->root}); SetUserDataRoot(ProjectPaths{a->root}, path); Load(*a, a->root);
          }
          return 0;
        }
        if (id == kLanguage) { english = !english; Labels(*a); auto selected = a->selected; Load(*a, a->root); a->selected = selected; SyncSelection(*a); InspectSelected(*a); SavePreferences(*a); return 0; }
        if (id == kTargets && HIWORD(wp) == CBN_SELCHANGE) {
          auto item = size_t(SendMessageW(a->combo, CB_GETCURSEL, 0, 0));
          if (item < a->dashboard_indices.size()) { a->selected = a->dashboard_indices[item]; InspectSelected(*a); }
        }
        if (id == kFolder) {
          auto root = PickFolder(window, Wide(Tr("Escolha uma pasta vazia para o ambiente ou uma existente do x360-native", "Choose an empty environment folder or an existing x360-native environment")).c_str());
          if (!root.empty()) {
            if (!fs::is_regular_file(root / "deps.lock.json")) InitializeEnvironment(a->self.parent_path() / "workspace", root);
            Load(*a, root);
            SavePreferences(*a);
          }
        }
        if (id == kCheck) { a->environment = CheckEnvironment(a->root); InspectSelected(*a); }
        if (id == kImport) { auto source = PickFolder(window, L"Selecione a pasta com o conjunto original extraído"); if (!source.empty()) Start(*a, id, source); }
        if (id == kPrepare) {
          if (!a->environment.msvc) {
            if (MessageBoxW(window, Wide(Tr("Falta o componente C++ do Microsoft Build Tools 2022. Abrir o instalador oficial com os componentes necessários selecionados?\n\nEsta é uma instalação no sistema: o Windows pode pedir permissão de administrador. O instalador mostra espaço, downloads e termos antes da instalação. Use o próprio instalador para cancelar. Jogos e dados serão preservados.", "Microsoft Build Tools 2022 C++ is missing. Open the official installer with the required components preselected?\n\nThis changes the system: Windows may request administrator permission. The installer shows space, downloads and terms before installation. Cancel using the installer itself. Games and data are preserved.")).c_str(), L"Microsoft Build Tools 2022", MB_YESNO | MB_ICONINFORMATION) == IDYES) Start(*a, kInstall);
            return 0;
          }
          auto answer = MessageBoxW(window, Wide(Tr("Preparar verifica e obtém Git, CMake, Ninja, LLVM e o SDK fixados. Downloads de ferramentas: até 905 MiB; o SDK e suas dependências são adicionais. Arquivos verificados em cache são reutilizados. Reserve espaço para fontes e compilação (recomendação inicial: 30 GiB, não uma medição do mínimo).\n\nPermitir os downloads necessários? Não há instalação global nem elevação. Não = usar somente os arquivos já presentes.", "Preparation verifies and obtains pinned Git, CMake, Ninja, LLVM and SDK. Tool downloads: up to 905 MiB; SDK and its dependencies are additional. Verified cached files are reused. Reserve space for sources and compilation (initial recommendation: 30 GiB, not a measured minimum).\n\nAllow required downloads? No global installation or elevation. No = use only existing files.")).c_str(), L"x360-native", MB_YESNOCANCEL | MB_ICONINFORMATION);
          if (answer == IDCANCEL) return 0;
          a->allow_downloads = answer == IDYES;
        }
        if (id == kPrepare || id == kLaunch) { InspectSelected(*a); if ((id == kPrepare && a->current.inputs_ok) || (id == kLaunch && a->current.prepared)) Start(*a, id); }
        return 0;
      }
      case WM_DROPFILES: {
        auto drop = reinterpret_cast<HDROP>(wp); wchar_t path[32768]{};
        auto count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        if (count == 1) DragQueryFileW(drop, 0, path, 32768);
        DragFinish(drop);
        if (!a->busy && count == 1 && fs::is_directory(path)) Start(*a, kImport, path);
        return 0;
      }
      case WM_TIMER: {
        if (a->busy && !a->session && (++a->timer_ticks % 10 == 0)) {
          std::string last; fs::file_time_type latest{};
          for (auto& file : fs::directory_iterator(a->logs)) {
            if (file.path().extension() != ".log" || file.last_write_time() < latest) continue;
            std::ifstream in(file.path()); std::string stage;
            for (std::string line; std::getline(in, line);) if (line.starts_with("X360N_STAGE ")) stage = line.substr(12);
            if (!stage.empty()) { last = stage; latest = file.last_write_time(); }
          }
          if (!last.empty()) {
            auto stage = last.find("codegen") != std::string::npos ? Tr("Traduzindo o código original do título…", "Translating the title's original code…") :
                last.find("4-build") != std::string::npos ? Tr("Compilando a execução para este computador…", "Compiling native execution for this computer…") :
                last.find("configure") != std::string::npos ? Tr("Verificando e configurando a preparação…", "Checking and configuring preparation…") :
                Tr("Preparando e verificando as ferramentas necessárias…", "Preparing and verifying the required tools…");
            Set(a->status, stage + "\r\n" + Tr("Você pode cancelar e continuar depois. O diagnóstico está em Detalhes.", "You can cancel and continue later. Diagnostics are available in Details."));
          }
        }
        if (GetForegroundWindow() != window || a->busy) { a->buttons_down = 0; return 0; }
        XINPUT_STATE input{};
        if (XInputGetState(0, &input) != ERROR_SUCCESS) { a->buttons_down = 0; return 0; }
        auto pressed = input.Gamepad.wButtons & ~a->buttons_down; a->buttons_down = input.Gamepad.wButtons;
        if (GetFocus() == a->combo && SendMessageW(a->combo, CB_GETDROPPEDSTATE, 0, 0)) {
          if (pressed & (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN)) {
            int selected = int(SendMessageW(a->combo, CB_GETCURSEL, 0, 0)) + ((pressed & XINPUT_GAMEPAD_DPAD_DOWN) ? 1 : -1);
            if (selected >= 0 && selected < int(a->dashboard_indices.size())) { a->selected = a->dashboard_indices[selected]; SyncSelection(*a); InspectSelected(*a); }
          }
          if (pressed & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B)) SendMessageW(a->combo, CB_SHOWDROPDOWN, FALSE, 0);
          return 0;
        }
        if (pressed & (XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_RIGHT | XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_LEFT)) {
          bool previous = pressed & (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_LEFT);
          SetFocus(GetNextDlgTabItem(window, GetFocus(), previous));
        }
        if ((pressed & XINPUT_GAMEPAD_A) && GetFocus() != a->combo) SendMessageW(GetFocus(), BM_CLICK, 0, 0);
        else if (pressed & XINPUT_GAMEPAD_A) SendMessageW(a->combo, CB_SHOWDROPDOWN, TRUE, 0);
        if (pressed & XINPUT_GAMEPAD_B) { a->show_details = false; Layout(*a); }
        return 0;
      }
      case kProgress: {
        std::unique_ptr<std::string> text(reinterpret_cast<std::string*>(lp)); Set(a->details, *text); Set(a->status, *text); return 0;
      }
      case kDetected: {
        std::unique_ptr<std::string> id(reinterpret_cast<std::string*>(lp));
        for (size_t i = 0; i < a->targets.size(); ++i) if (a->targets[i].session &&
            std::find(a->targets[i].dependencies.begin(), a->targets[i].dependencies.end(), *id) != a->targets[i].dependencies.end()) {
          a->selected = i; SyncSelection(*a); return 0;
        }
        for (size_t i = 0; i < a->targets.size(); ++i) if (a->targets[i].id == *id) {
          if (std::find(a->dashboard_indices.begin(), a->dashboard_indices.end(), i) != a->dashboard_indices.end()) { a->selected = i; SyncSelection(*a); }
          break;
        }
        return 0;
      }
      case kFinished: {
        std::unique_ptr<std::string> text(reinterpret_cast<std::string*>(lp));
        if (a->worker.joinable()) a->worker.join(); a->busy = false; a->session = false; a->installing = false;
        a->environment = CheckEnvironment(a->root);
        InspectSelected(*a); Set(a->details, *text + "\r\nLogs: " + Utf8(a->logs));
        if (a->failed) {
          Set(a->headline, Tr("Vamos resolver esta etapa.", "Let's fix this step."));
          Set(a->subtitle, Tr("A preparação não terminou. Seus arquivos foram preservados.\r\nVeja o motivo abaixo e tente novamente após corrigir o problema.", "Preparation did not finish. Your files were preserved.\r\nRead the reason below, then retry after fixing the issue."));
          a->show_details = true;
        }
        Enable(*a); return 0;
      }
      case WM_CLOSE:
        if (a->busy) {
          MessageBoxW(window, a->installing ? L"Conclua ou cancele pelo instalador Microsoft e aguarde o retorno." : a->session ? L"Encerre pela janela da plataforma. Voltar pelo menu do jogo retorna à dashboard." : L"Cancele a preparação e aguarde sua conclusão antes de fechar.", L"Operação em andamento", MB_OK | MB_ICONINFORMATION); return 0;
        }
        DestroyWindow(window); return 0;
      case WM_DESTROY: PostQuitMessage(0); return 0;
    }
  } catch (const std::exception& error) {
    if (!a->worker.joinable()) { a->busy = false; a->session = false; }
    Set(a->status, "Não foi possível concluir. Verifique os detalhes e tente novamente.");
    Set(a->details, error.what()); a->show_details = true; Layout(*a); Enable(*a);
  }
  return DefWindowProcW(window, message, wp, lp);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&controls);
  App app;
  wchar_t self[32768]{}; GetModuleFileNameW(nullptr, self, 32768); app.self = self;
  if (!fs::is_directory(app.self.parent_path() / "workspace")) {
    if (auto root = FindProjectRoot(app.self)) app.root = root->root;
  } else {
    PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
      app.preferences = fs::path(local) / "x360-native/preparation.txt"; CoTaskMemFree(local);
      std::ifstream in(app.preferences); std::string language, root;
      std::getline(in, language); std::getline(in, root); english = language == "en";
      auto saved = fs::path(std::u8string(root.begin(), root.end()));
      if (!root.empty() && fs::is_regular_file(saved / "deps.lock.json")) app.root = saved;
    }
  }
  int count = 0; auto** args = CommandLineToArgvW(GetCommandLineW(), &count);
  if (count == 3 && std::wstring(args[1]) == L"--root") app.root = args[2];
  LocalFree(args);
  // A first-time user shouldn't need to choose a source repository. The package
  // prepares its own verified source payload in the user's normal data folder.
  // An existing installation is preserved; its location can be changed in Settings.
  if (app.root.empty() && !app.preferences.empty()) {
    const auto initial = app.preferences.parent_path() / "environment";
    try {
      InitializeEnvironment(app.self.parent_path() / "workspace", initial);
      app.root = initial; SavePreferences(app);
    } catch (const std::exception& error) {
      // Leave the first-run folder action available. Never overwrite a different
      // existing source version or hide it by deleting the user's environment.
      app.startup_error = error.what();
    }
  }
  WNDCLASSW cls{}; cls.lpfnWndProc = WndProc; cls.hInstance = instance;
  cls.lpszClassName = L"X360NativePreparation"; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  cls.hbrBackground = background_brush; RegisterClassW(&cls);
  auto window = CreateWindowExW(WS_EX_CONTROLPARENT, cls.lpszClassName, L"x360-native", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                               CW_USEDEFAULT, CW_USEDEFAULT, 1120, 760, nullptr, nullptr, instance, &app);
  if (!window) { CoUninitialize(); return 1; }
  ShowWindow(window, show); UpdateWindow(window);
  SetFocus(GetDlgItem(window, kPrimary));
  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(window, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  }
  CoUninitialize(); return int(msg.wParam);
}
