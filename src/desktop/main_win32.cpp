#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <Xinput.h>
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
enum { kFolder = 101, kTargets, kCheck, kImport, kPrepare, kLaunch, kDetails, kData, kCancel, kLanguage, kInstall, kStorage };
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
  HWND window{}, title{}, subtitle{}, root_text{}, combo{}, status{}, details{};
  HFONT font{}, title_font{};
  HICON icon{};
  std::vector<HWND> buttons;
  fs::path self, root, logs, preferences;
  Environment environment;
  std::vector<Target> targets;
  Inspection current;
  size_t selected = 0;
  std::thread worker;
  std::atomic<bool> cancel{false};
  bool busy = false, session = false, show_details = false;
  UINT dpi = 96;
  WORD buttons_down = 0;
  unsigned timer_ticks = 0;
  bool allow_downloads = false;
  bool installing = false;
  ~App() { if (worker.joinable()) worker.join(); DeleteObject(font); DeleteObject(title_font); DestroyIcon(icon); }
};
void Set(HWND h, const std::string& text) { SetWindowTextW(h, Wide(text).c_str()); }
void Post(HWND window, UINT type, const std::string& text) {
  auto* value = new std::string(text);
  if (!PostMessageW(window, type, 0, reinterpret_cast<LPARAM>(value))) delete value;
}
HWND Control(App& a, const wchar_t* cls, const wchar_t* text, DWORD style, int id = 0) {
  auto h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
                         a.window, reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
  SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(a.font), TRUE);
  return h;
}
void Layout(App& a) {
  RECT client{}; GetClientRect(a.window, &client);
  auto px = [&](int x) { return MulDiv(x, int(a.dpi), 96); };
  int width = client.right, margin = px(24);
  auto move = [&](HWND h, int x, int y, int w, int height) { MoveWindow(h, px(x), px(y), w, px(height), TRUE); };
  move(a.title, 24, 20, width - px(200), 34);
  move(GetDlgItem(a.window, kLanguage), MulDiv(width, 96, a.dpi) - 182, 20, px(158), 32);
  move(a.subtitle, 24, 60, width - 2 * margin, 46);
  move(a.root_text, 24, 116, width - px(214), 28);
  move(GetDlgItem(a.window, kFolder), MulDiv(width, 96, a.dpi) - 182, 110, px(158), 32);
  move(a.combo, 24, 156, width - 2 * margin, 200);
  move(a.status, 24, 202, width - 2 * margin, 172);
  int x = 24;
  for (int id : {kCheck, kImport, kPrepare, kLaunch}) {
    move(GetDlgItem(a.window, id), x, 382, px(168), 36); x += 180;
  }
  move(GetDlgItem(a.window, kDetails), 24, 430, px(168), 32);
  move(GetDlgItem(a.window, kData), 204, 430, px(168), 32);
  move(GetDlgItem(a.window, kCancel), 384, 430, px(168), 32);
  move(GetDlgItem(a.window, kStorage), 564, 430, px(168), 32);
  MoveWindow(a.details, margin, px(476), width - 2 * margin,
             std::max(px(64), int(client.bottom) - px(496)), TRUE);
  ShowWindow(a.details, a.show_details ? SW_SHOW : SW_HIDE);
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
}
void InspectSelected(App& a) {
  a.current = {};
  a.current.inputs_ok = false;
  if (a.selected >= a.targets.size()) {
    Set(a.status, Tr("Escolha uma pasta para o ambiente e seus dados. O aplicativo prepara os arquivos do projeto sem incluir jogos ou dashboards.\r\n\r\nUse uma pasta fora do aplicativo para preservar o conteúdo ao remover ou trocar o pacote.", "Choose a folder for your environment and data. The app prepares the project files; games and dashboards are not included.\r\n\r\nChoose a folder outside the app so removing or replacing the package preserves your data."));
    Enable(a); return;
  }
  const auto& t = a.targets[a.selected];
  a.current = Inspect(ProjectPaths{a.root}, t);
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
  std::string state = !a.current.inputs_ok ? Tr("Faltam componentes. Importe a pasta extraída da versão indicada. Veja detalhes para os arquivos necessários.", "Missing components. Import the extracted folder for this version. See details for required files.") :
                      !a.current.prepared ? Tr("Precisa preparar. Os componentes verificados estão presentes.", "Needs preparation. Verified components are present.") :
                      t.session ? Tr("Preparado para abrir a dashboard original.", "Prepared to open the original dashboard.") : Tr("Preparado para diagnóstico; sessão guiada indisponível neste alvo.", "Prepared for diagnostics; guided session unavailable for this target.");
  state += "\r\n\r\n" + (english ? t.limitations_en : t.limitations);
  if (!t.user_root.empty()) state += Tr("\r\n\r\nDados: ", "\r\n\r\nData: ") + Utf8(t.user_root);
  std::error_code ec; auto space = fs::space(a.root, ec);
  if (!ec) state += Tr("\r\nEspaço livre: ", "\r\nFree space: ") + std::to_string(space.available / (1024 * 1024 * 1024)) + " GiB.";
  Set(a.status, state);
  Set(a.details, detail.empty() ? "Verificação concluída. Nenhum dado é enviado automaticamente.\r\nLogs de execução: " + Utf8(a.root / ".local/runs") : detail);
  Enable(a);
}
void Load(App& a, const fs::path& root) {
  a.targets.clear(); a.selected = 0;
  SendMessageW(a.combo, CB_RESETCONTENT, 0, 0);
  a.root = root;
  a.environment = CheckEnvironment(root);
  SetWindowTextW(a.root_text, root.empty() ? L"Selecione um ambiente existente" : root.c_str());
  if (!root.empty()) a.targets = ReadCatalog(ProjectPaths{root});
  for (size_t i = 0; i < a.targets.size(); ++i) {
    auto label = Wide(english ? a.targets[i].label_en : a.targets[i].label);
    SendMessageW(a.combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    if (a.targets[i].session) a.selected = i;
  }
  SendMessageW(a.combo, CB_SETCURSEL, a.selected, 0);
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
       {kDetails, "Ver detalhes", "Show details"}, {kData, "Abrir dados locais", "Open local data"},
       {kCancel, "Cancelar preparação", "Cancel preparation"}, {kStorage, "Pasta de saves", "Save data folder"}}) Set(GetDlgItem(a.window, id), Tr(pt, en));
  Set(a.subtitle, Tr("Preparar o ambiente e abrir a dashboard original\r\nPrévia Windows x64 · D3D12 · perfis e save/load ainda não aprovados", "Prepare your environment and open the original dashboard\r\nWindows x64 preview · D3D12 · profiles and save/load not yet approved"));
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
  a.cancel = false; a.busy = true; a.session = action == kLaunch; a.installing = action == kInstall;
  const auto target = a.targets[a.selected];
  Enable(a);
  Set(a.status, action == kImport ? Tr("Verificando o conjunto selecionado antes de copiar...", "Verifying the selected set before copying...") :
                action == kLaunch ? Tr("Sessão em andamento. Use My Games para iniciar o jogo e o menu do jogo para voltar. Feche a janela da plataforma para encerrar.", "Session running. Use My Games to launch and the game's menu to return. Close the platform window to exit.") :
                Tr("Preparação em andamento. As etapas reais serão registradas abaixo.", "Preparation running. Actual stages are recorded below."));
  a.show_details = true; Layout(a);
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
          Post(a.window, kProgress, "Preparando " + id + ": configure → codegen → configure → build.\r\nLogs das etapas: " + Utf8(a.root / ".local/build/titles" / id));
          run(powershell, {"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", Utf8(a.root / "scripts/build-title.ps1"), "-Title", id}, id);
          fs::remove(marker);
        };
        for (const auto& dep : target.dependencies) prepare(dep);
        prepare(target.id);
        Post(a.window, kProgress, Tr("Registrando conteúdo verificado na biblioteca original...", "Registering verified content in the original library..."));
        PrepareLibrary(ProjectPaths{a.root}, target, a.targets, [&] { return a.cancel.load(); });
        finish("Preparação concluída. A identidade dos componentes será conferida antes de habilitar a sessão.");
      }
    } catch (const std::exception& error) { finish(error.what()); }
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
LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
  auto* a = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    a = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
    a->window = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(a));
  }
  if (!a) return DefWindowProcW(window, message, wp, lp);
  try {
    switch (message) {
      case WM_CTLCOLORSTATIC:
        SetBkColor(reinterpret_cast<HDC>(wp), RGB(255, 255, 255));
        return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
      case WM_CREATE: {
        a->dpi = GetDpiForWindow(window);
        a->font = CreateFontW(-MulDiv(10, a->dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        a->title_font = CreateFontW(-MulDiv(20, a->dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        a->icon = MakeIcon(); SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(a->icon));
        a->title = Control(*a, L"STATIC", L"x360-native", 0);
        SendMessageW(a->title, WM_SETFONT, reinterpret_cast<WPARAM>(a->title_font), TRUE);
        a->subtitle = Control(*a, L"STATIC", L"", 0);
        a->root_text = Control(*a, L"STATIC", L"", SS_PATHELLIPSIS);
        a->combo = Control(*a, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, kTargets);
        a->status = Control(*a, L"STATIC", L"", 0);
        a->details = Control(*a, L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER | WS_TABSTOP);
        for (const auto& pair : {std::pair{kFolder, L"Selecionar ambiente"}, {kCheck, L"Verificar novamente"}, {kImport, L"Importar conjunto"}, {kPrepare, L"Preparar"}, {kLaunch, L"Abrir dashboard"}, {kDetails, L"Ver detalhes"}, {kData, L"Abrir dados locais"}, {kCancel, L"Cancelar preparação"}})
          a->buttons.push_back(Control(*a, L"BUTTON", pair.second, WS_TABSTOP, pair.first));
        a->buttons.push_back(Control(*a, L"BUTTON", L"English / Português", WS_TABSTOP, kLanguage));
        a->buttons.push_back(Control(*a, L"BUTTON", L"", WS_TABSTOP, kStorage));
        DragAcceptFiles(window, TRUE); SetTimer(window, 1, 100, nullptr);
        Labels(*a); Layout(*a); Load(*a, a->root); return 0;
      }
      case WM_SIZE: if (a->title) Layout(*a); return 0;
      case WM_DPICHANGED: {
        a->dpi = HIWORD(wp);
        auto old = a->font, old_title = a->title_font;
        a->font = CreateFontW(-MulDiv(10, a->dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        a->title_font = CreateFontW(-MulDiv(20, a->dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        EnumChildWindows(window, [](HWND child, LPARAM font) -> BOOL { SendMessageW(child, WM_SETFONT, WPARAM(font), TRUE); return TRUE; }, reinterpret_cast<LPARAM>(a->font));
        SendMessageW(a->title, WM_SETFONT, reinterpret_cast<WPARAM>(a->title_font), TRUE);
        DeleteObject(old); DeleteObject(old_title);
        auto rect = reinterpret_cast<RECT*>(lp);
        SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        Layout(*a); return 0;
      }
      case WM_GETMINMAXINFO: {
        auto* size = reinterpret_cast<MINMAXINFO*>(lp); size->ptMinTrackSize = {MulDiv(800, a->dpi, 96), MulDiv(640, a->dpi, 96)}; return 0;
      }
      case WM_COMMAND: {
        auto id = LOWORD(wp);
        if (id == IDOK) { auto focus = GetFocus(); if (focus && focus != a->combo && IsWindowEnabled(focus)) SendMessageW(focus, BM_CLICK, 0, 0); return 0; }
        if (id == kDetails) { a->show_details = !a->show_details; Layout(*a); return 0; }
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
        if (id == kLanguage) { english = !english; Labels(*a); auto selected = a->selected; Load(*a, a->root); a->selected = selected; SendMessageW(a->combo, CB_SETCURSEL, selected, 0); InspectSelected(*a); SavePreferences(*a); return 0; }
        if (id == kTargets && HIWORD(wp) == CBN_SELCHANGE) { a->selected = size_t(SendMessageW(a->combo, CB_GETCURSEL, 0, 0)); InspectSelected(*a); }
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
          if (!last.empty()) Set(a->status, Tr("Preparação em andamento: ", "Preparation running: ") + last + "\r\n" + Tr("Pode cancelar com segurança. Os logs completos ficam em: ", "You can cancel safely. Full logs are in: ") + Utf8(a->logs));
        }
        if (GetForegroundWindow() != window || a->busy) { a->buttons_down = 0; return 0; }
        XINPUT_STATE input{};
        if (XInputGetState(0, &input) != ERROR_SUCCESS) { a->buttons_down = 0; return 0; }
        auto pressed = input.Gamepad.wButtons & ~a->buttons_down; a->buttons_down = input.Gamepad.wButtons;
        if (GetFocus() == a->combo && SendMessageW(a->combo, CB_GETDROPPEDSTATE, 0, 0)) {
          if (pressed & (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN)) {
            int selected = int(a->selected) + ((pressed & XINPUT_GAMEPAD_DPAD_DOWN) ? 1 : -1);
            if (selected >= 0 && selected < int(a->targets.size())) { a->selected = size_t(selected); SendMessageW(a->combo, CB_SETCURSEL, selected, 0); InspectSelected(*a); }
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
        std::unique_ptr<std::string> text(reinterpret_cast<std::string*>(lp)); Set(a->details, *text); return 0;
      }
      case kDetected: {
        std::unique_ptr<std::string> id(reinterpret_cast<std::string*>(lp));
        for (size_t i = 0; i < a->targets.size(); ++i) if (a->targets[i].id == *id) {
          a->selected = i; SendMessageW(a->combo, CB_SETCURSEL, i, 0); break;
        }
        return 0;
      }
      case kFinished: {
        std::unique_ptr<std::string> text(reinterpret_cast<std::string*>(lp));
        if (a->worker.joinable()) a->worker.join(); a->busy = false; a->session = false; a->installing = false;
        a->environment = CheckEnvironment(a->root);
        InspectSelected(*a); Set(a->details, *text + "\r\nLogs: " + Utf8(a->logs)); Enable(*a); return 0;
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
  WNDCLASSW cls{}; cls.lpfnWndProc = WndProc; cls.hInstance = instance;
  cls.lpszClassName = L"X360NativePreparation"; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1); RegisterClassW(&cls);
  auto window = CreateWindowExW(WS_EX_CONTROLPARENT, cls.lpszClassName, L"x360-native — preparação", WS_OVERLAPPEDWINDOW,
                               CW_USEDEFAULT, CW_USEDEFAULT, 960, 760, nullptr, nullptr, instance, &app);
  if (!window) { CoUninitialize(); return 1; }
  ShowWindow(window, show); UpdateWindow(window);
  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(window, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  }
  CoUninitialize(); return int(msg.wParam);
}
