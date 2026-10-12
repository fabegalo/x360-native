#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <atomic>
#include <algorithm>
#include <fstream>
#include <memory>
#include <thread>
#include "catalog.h"
#include "child_process.h"

namespace fs = std::filesystem;
using namespace x360n;
using namespace x360n::desktop;
namespace {
constexpr UINT kFinished = WM_APP + 1, kProgress = WM_APP + 2;
enum { kFolder = 101, kTargets, kCheck, kImport, kPrepare, kLaunch, kDetails, kData, kCancel };
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
  fs::path self, root, logs;
  std::vector<Target> targets;
  Inspection current;
  size_t selected = 0;
  std::thread worker;
  std::atomic<bool> cancel{false};
  bool busy = false, session = false, show_details = false;
  UINT dpi = 96;
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
  move(a.title, 24, 20, width - 2 * margin, 34);
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
  MoveWindow(a.details, margin, px(476), width - 2 * margin,
             std::max(px(64), int(client.bottom) - px(496)), TRUE);
  ShowWindow(a.details, a.show_details ? SW_SHOW : SW_HIDE);
}
void Enable(App& a) {
  bool has = a.selected < a.targets.size();
  for (auto b : a.buttons) EnableWindow(b, !a.busy);
  EnableWindow(a.combo, !a.busy);
  EnableWindow(GetDlgItem(a.window, kImport), !a.busy && has && a.targets[a.selected].importable);
  EnableWindow(GetDlgItem(a.window, kPrepare), !a.busy && has && a.current.inputs_ok);
  EnableWindow(GetDlgItem(a.window, kLaunch), !a.busy && has && a.current.prepared && a.targets[a.selected].session);
  EnableWindow(GetDlgItem(a.window, kData), !a.root.empty());
  EnableWindow(GetDlgItem(a.window, kDetails), TRUE);
  EnableWindow(GetDlgItem(a.window, kCancel), a.busy && !a.session);
}
void InspectSelected(App& a) {
  a.current = {};
  a.current.inputs_ok = false;
  if (a.selected >= a.targets.size()) {
    Set(a.status, "Nenhum ambiente configurado. Selecione a pasta do projeto para verificar os conjuntos disponíveis.\r\n\r\nEsta versão do assistente ainda exige o projeto e as ferramentas de desenvolvimento. A instalação autônoma está pendente.");
    Enable(a); return;
  }
  const auto& t = a.targets[a.selected];
  a.current = Inspect(ProjectPaths{a.root}, t);
  std::string detail;
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
  auto supervisor = a.self.parent_path() / "x360n.exe";
  if (!fs::is_regular_file(supervisor)) {
    a.current.prepared = false;
    detail += "Falta o supervisor ao lado do aplicativo: " + Utf8(supervisor) + "\r\n";
  }
  std::string state = !a.current.inputs_ok ? "Falta componente ou o arquivo pertence a outro conjunto." :
                      !a.current.prepared ? "Precisa preparar. Os componentes verificados estão presentes." :
                      t.session ? "Preparado para abrir a dashboard original." : "Preparado para diagnóstico; sessão guiada indisponível neste alvo.";
  state += "\r\n\r\n" + t.limitations;
  if (!t.user_root.empty()) state += "\r\n\r\nDados: " + Utf8(t.user_root);
  std::error_code ec; auto space = fs::space(a.root, ec);
  if (!ec) state += "\r\nEspaço livre: " + std::to_string(space.available / (1024 * 1024 * 1024)) + " GiB.";
  Set(a.status, state);
  Set(a.details, detail.empty() ? "Verificação concluída. Nenhum dado é enviado automaticamente.\r\nLogs de execução: " + Utf8(a.root / ".local/runs") : detail);
  Enable(a);
}
void Load(App& a, const fs::path& root) {
  a.targets.clear(); a.selected = 0;
  SendMessageW(a.combo, CB_RESETCONTENT, 0, 0);
  a.root = root;
  SetWindowTextW(a.root_text, root.empty() ? L"Selecione um ambiente existente" : root.c_str());
  if (!root.empty()) a.targets = ReadCatalog(ProjectPaths{root});
  for (size_t i = 0; i < a.targets.size(); ++i) {
    auto label = Wide(a.targets[i].label);
    SendMessageW(a.combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    if (a.targets[i].session) a.selected = i;
  }
  SendMessageW(a.combo, CB_SETCURSEL, a.selected, 0);
  InspectSelected(a);
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
  a.cancel = false; a.busy = true; a.session = action == kLaunch;
  const auto target = a.targets[a.selected];
  Enable(a);
  Set(a.status, action == kImport ? "Verificando o conjunto selecionado antes de copiar..." :
                action == kLaunch ? "Sessão em andamento. Use My Games para iniciar o jogo e o menu do jogo para voltar. Feche a janela da plataforma para encerrar." :
                "Preparação em andamento. As etapas reais serão registradas abaixo. Não há download nem instalação automática nesta versão.");
  a.show_details = true; Layout(a);
  a.worker = std::thread([&a, target, action, source] {
    auto finish = [&](const std::string& message) {
      std::ofstream(a.logs / "result.txt", std::ios::binary) << message << '\n';
      Post(a.window, kFinished, message);
    };
    try {
      if (action == kImport) {
        auto count = Import(ProjectPaths{a.root}, target, source, [&] { return a.cancel.load(); });
        finish("Importação concluída: " + std::to_string(count) + " componentes copiados. Originais preservados."); return;
      }
      auto run = [&](fs::path exe, std::vector<std::string> args, const std::string& label) {
        ProcessSpec spec;
        spec.exe = exe; spec.args = args; spec.working_dir = a.root;
        spec.stdout_file = a.logs / (label + ".log"); spec.stderr_file = a.logs / (label + ".error.log");
        spec.no_console = true;
        if (action != kLaunch) spec.should_cancel = [&] { return a.cancel.load(); };
        auto result = RunProcess(spec);
        if (result.cancelled) throw std::runtime_error("Preparação cancelada. Os originais e dados de usuário foram preservados; tente preparar novamente para retomar as etapas válidas.");
        if (!result.launched || result.exit_code != 0)
          throw std::runtime_error("A etapa " + label + " não concluiu. " + result.launch_error + " " + DescribeExitCode(result.exit_code) + "\r\nDiagnóstico local: " + Utf8(a.logs));
      };
      if (action == kLaunch) {
        run(a.self.parent_path() / "x360n.exe", {"--root", Utf8(a.root), "session", Utf8(target.manifest), "--timeout", "0"}, "session");
        finish("Sessão encerrada. Consulte os logs para os resultados da execução.");
      } else {
        if (!fs::is_regular_file(a.root / ".local/toolchains" / ("llvm-" + std::string(X360N_LLVM_VERSION_LOCKED)) / "bin/clang++.exe"))
          throw std::runtime_error("Falta o LLVM fixado. O gerenciamento automático da toolchain ainda está pendente; consulte Preparar o ambiente no README.");
        wchar_t sys[MAX_PATH]{}; GetSystemDirectoryW(sys, MAX_PATH);
        fs::path powershell = fs::path(sys) / "WindowsPowerShell/v1.0/powershell.exe";
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
        a->subtitle = Control(*a, L"STATIC", L"Preparar o ambiente e abrir a dashboard original\r\nPrévia para Windows x64 · perfis, saves e instalação autônoma ainda pendentes", 0);
        a->root_text = Control(*a, L"STATIC", L"", SS_PATHELLIPSIS);
        a->combo = Control(*a, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, kTargets);
        a->status = Control(*a, L"STATIC", L"", 0);
        a->details = Control(*a, L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER | WS_TABSTOP);
        for (const auto& pair : {std::pair{kFolder, L"Selecionar ambiente"}, {kCheck, L"Verificar novamente"}, {kImport, L"Importar conjunto"}, {kPrepare, L"Preparar"}, {kLaunch, L"Abrir dashboard"}, {kDetails, L"Ver detalhes"}, {kData, L"Abrir dados locais"}, {kCancel, L"Cancelar preparação"}})
          a->buttons.push_back(Control(*a, L"BUTTON", pair.second, WS_TABSTOP, pair.first));
        Layout(*a); Load(*a, a->root); return 0;
      }
      case WM_SIZE: if (a->title) Layout(*a); return 0;
      case WM_GETMINMAXINFO: {
        auto* size = reinterpret_cast<MINMAXINFO*>(lp); size->ptMinTrackSize = {MulDiv(800, a->dpi, 96), MulDiv(640, a->dpi, 96)}; return 0;
      }
      case WM_COMMAND: {
        auto id = LOWORD(wp);
        if (id == kDetails) { a->show_details = !a->show_details; Layout(*a); return 0; }
        if (id == kData && !a->root.empty()) { auto p = a->root / ".local"; fs::create_directories(p); ShellExecuteW(window, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL); return 0; }
        if (id == kCancel) { a->cancel = true; Set(a->status, "Cancelando a preparação..."); return 0; }
        if (a->busy) return 0;
        if (id == kTargets && HIWORD(wp) == CBN_SELCHANGE) { a->selected = size_t(SendMessageW(a->combo, CB_GETCURSEL, 0, 0)); InspectSelected(*a); }
        if (id == kFolder) {
          auto root = PickFolder(window, L"Selecione a pasta do projeto x360-native");
          if (!root.empty()) {
            if (!fs::is_regular_file(root / "deps.lock.json")) throw std::runtime_error("A pasta selecionada não contém deps.lock.json. Selecione o projeto x360-native.");
            Load(*a, root);
          }
        }
        if (id == kCheck) InspectSelected(*a);
        if (id == kImport) { auto source = PickFolder(window, L"Selecione a pasta com o conjunto original extraído"); if (!source.empty()) Start(*a, id, source); }
        if (id == kPrepare || id == kLaunch) { InspectSelected(*a); if ((id == kPrepare && a->current.inputs_ok) || (id == kLaunch && a->current.prepared)) Start(*a, id); }
        return 0;
      }
      case kProgress: {
        std::unique_ptr<std::string> text(reinterpret_cast<std::string*>(lp)); Set(a->details, *text); return 0;
      }
      case kFinished: {
        std::unique_ptr<std::string> text(reinterpret_cast<std::string*>(lp));
        if (a->worker.joinable()) a->worker.join(); a->busy = false; a->session = false;
        InspectSelected(*a); Set(a->details, *text + "\r\nLogs: " + Utf8(a->logs)); Enable(*a); return 0;
      }
      case WM_CLOSE:
        if (a->busy) {
          MessageBoxW(window, a->session ? L"Encerre pela janela da plataforma. Voltar pelo menu do jogo retorna à dashboard." : L"Cancele a preparação e aguarde sua conclusão antes de fechar.", L"Operação em andamento", MB_OK | MB_ICONINFORMATION); return 0;
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
  SetProcessDPIAware();
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&controls);
  App app;
  wchar_t self[32768]{}; GetModuleFileNameW(nullptr, self, 32768); app.self = self;
  if (auto root = FindProjectRoot(app.self)) app.root = root->root;
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
