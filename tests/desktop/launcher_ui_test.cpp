// Exercise the actual Win32 procedure on hidden fixture windows; no Computer
// Use, screenshots, input injection, title executables or renderer validation.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include <iostream>
#ifdef X360N_FRONTEND_BASELINE
#include X360N_FRONTEND_BASELINE
#else
#include "../../src/desktop/win32/main_win32.cpp"
#endif
#include "../supervisor/preparation_fixture.h"
#include "../supervisor/scratch.h"
namespace {
using Clock = std::chrono::steady_clock;
int64_t Now() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(); }
struct Frontend {
  fs::path root = UniqueTempPath("launcher-ui") / std::to_string(GetTickCount64());
  ProjectPaths paths{root};
  App app;
  ULONG_PTR gdiplus = 0;
  Frontend() {
    fs::create_directories(root);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput input; Gdiplus::GdiplusStartup(&gdiplus, &input, nullptr);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS}; InitCommonControlsEx(&controls);
    app.root = root; app.self = root / "launcher.exe";
    app.environment.description = "synthetic environment";
    app.environment.graphics = app.environment.writable = app.environment.architecture = true;
#ifndef X360N_FRONTEND_BASELINE
    app.supervisor_available = true;
#endif
    fs::copy_file(X360N_LAUNCHER_CHILD, root / "x360n.exe");
    WNDCLASSW cls{}; cls.lpfnWndProc = WndProc; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"LauncherUiFixture";
    RegisterClassW(&cls);
    app.window = CreateWindowExW(0, cls.lpszClassName, L"fixture", WS_OVERLAPPEDWINDOW,
        0, 0, 900, 700, nullptr, nullptr, cls.hInstance, &app);
    if (!app.window) throw std::runtime_error("fixture window creation failed");
    MSG msg{}; // The fixture controls loading explicitly.
    while (PeekMessageW(&msg, app.window, kOpenEnvironment, kOpenEnvironment, PM_REMOVE)) {}
  }
  ~Frontend() {
    app.cancel = true;
    if (app.worker.joinable()) app.worker.join();
    Pump();
    if (IsWindow(app.window)) DestroyWindow(app.window);
    MSG msg{}; while (PeekMessageW(&msg, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
    Gdiplus::GdiplusShutdown(gdiplus); CoUninitialize();
    std::error_code ec; fs::remove_all(root, ec);
  }
  void Pump() {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message != WM_QUIT) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
  }
  bool Until(const std::function<bool()>& done, int milliseconds = 15000) {
    const auto end = Now() + milliseconds;
    do { Pump(); if (done()) return true; MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT); } while (Now() < end);
    return false;
  }
  Target TargetFixture() {
    const auto dir = root / "titles/dashboard";
    const auto game = paths.inputs() / "dashboard";
    const auto exe = paths.build() / "titles/dashboard/dashboard.exe";
    fs::create_directories(dir); fs::create_directories(game); fs::create_directories(exe.parent_path());
    std::ofstream(game / "dash.xex") << "synthetic input";
    std::ofstream(exe) << "synthetic artifact; not executed";
    const auto hash = DigestFile(game / "dash.xex")->sha256;
    const auto manifest = dir / "dashboard_manifest.toml";
    std::ofstream(manifest) << "[project]\nname='dashboard'\ngame_root='../../.local/inputs/dashboard'\n"
      << "[entrypoint]\nfile_path='../../.local/inputs/dashboard/dash.xex'\n";
    std::ofstream(dir / "preparation.toml") << "label='fixture'\nlimitations='synthetic'\nkind='dashboard'\nsession=true\n"
      << "[[files]]\npath='dash.xex'\nsource_name='dash.xex'\nsha256='" << hash << "'\n";
    WriteTestReceipt(paths, "dashboard", manifest, exe, {game / "dash.xex"});
    return ReadCatalog(paths).front();
  }
  void Seed(const Target& target, bool unrelated = false) {
    app.targets = {target}; app.checked.resize(1); app.checked[0].state = Inspect(paths, target);
    if (unrelated) {
      Target slow; slow.id = "unrelated"; slow.aot = false;
      for (unsigned i = 0; i != 6000; ++i) slow.inputs.push_back({paths.inputs() / ("missing-" + std::to_string(i)), "missing", std::string(64, 'a')});
      app.targets.push_back(slow); app.checked.resize(2);
    }
    app.selected = 0; InspectSelected(app);
  }
};
}
TEST_CASE("launcher catalog loading leaves the message thread responsive", "[launcher-ui]") {
  Frontend f; f.TargetFixture();
  // Resolving a large inventory used to run synchronously inside Load().
  const auto inventory = f.root / "titles/dashboard/inventory.tsv";
  std::ofstream list(inventory);
  for (int i = 0; i != 5000; ++i) list << std::string(64, 'a') << '\t' << i << ".bin\n";
  list.close();
  std::ofstream(f.root / "titles/dashboard/preparation.toml", std::ios::app) << "\n";
  // inventory belongs at table root, before [[files]].
  auto setup = f.root / "titles/dashboard/preparation.toml";
  std::ifstream in(setup); std::string text{std::istreambuf_iterator<char>(in), {}}; in.close();
  text.insert(0, "inventory='inventory.tsv'\n"); std::ofstream(setup) << text;
  const auto start = Now(); Load(f.app, f.root); const auto dispatch = Now() - start;
  std::cout << "catalog UI dispatch ms=" << dispatch << '\n';
  CHECK(dispatch < 200);
  REQUIRE(f.app.busy);
  const auto heartbeat = Now(); SendMessageW(f.app.window, WM_COMMAND, kDetails, 0);
  CHECK(Now() - heartbeat < 200);
  f.app.cancel = true;
  REQUIRE(f.Until([&] { return !f.app.busy; }));
  CHECK_FALSE(f.app.session);
}
TEST_CASE("launcher completes consecutive sessions without scanning unrelated content", "[launcher-ui]") {
  Frontend f; const auto target = f.TargetFixture(); f.Seed(target, true);
  const std::string modes[]{"ok", "fail", "ok"};
  for (unsigned step = 0; step != 3; ++step) {
    const auto& mode = modes[step];
    std::ofstream(f.root / "child-mode.txt") << mode;
    fs::remove(f.root / "child-ended.txt");
    const auto launch_start = Now();
    if (step == 2) { SendMessageW(f.app.window, WM_COMMAND, kRetry, 0); f.Pump(); }
    else Start(f.app, kLaunch);
    REQUIRE(f.app.busy);
    SendMessageW(f.app.window, WM_COMMAND, kDetails, 0);
    SendMessageW(f.app.window, WM_COMMAND, mode == "fail" ? kGames : kHome, 0);
    CHECK(f.app.busy);
    CHECK_FALSE(IsWindowEnabled(Item(f.app, kPrimary)));
    REQUIRE(f.Until([&] { return !f.app.busy; }));
    int64_t ended = 0; std::ifstream(f.root / "child-ended.txt") >> ended;
    REQUIRE(ended != 0);
    const auto latency = Now() - ended;
    int64_t started = 0; std::ifstream(f.root / "child-started.txt") >> started;
    REQUIRE(started != 0);
    std::cout << "session " << mode << " launch-to-child-start ms=" << started - launch_start << '\n';
    CHECK(started - launch_start < 1000);
    std::cout << "session " << mode << " exit-to-idle ms=" << latency << '\n';
    CHECK(latency < 500);
    CHECK_FALSE(f.app.session);
    CHECK(f.app.failed.load() == (mode == "fail"));
    CHECK(IsWindowEnabled(Item(f.app, kFolder)));
    CHECK(IsWindowEnabled(Item(f.app, kPrimary))); // Runtime failure needs no rebuild.
  }
}
TEST_CASE("launcher cancels verification before creating a session process", "[launcher-ui]") {
  Frontend f; f.Seed(f.TargetFixture());
  Start(f.app, kLaunch); f.app.cancel = true;
  REQUIRE(f.Until([&] { return !f.app.busy; }));
  CHECK_FALSE(fs::exists(f.root / "child-started.txt"));
  CHECK_FALSE(f.app.session);
}
#ifndef X360N_FRONTEND_BASELINE
TEST_CASE("launcher reports native closure before supervisor diagnostic cleanup completes", "[launcher-ui]") {
  Frontend f; f.Seed(f.TargetFixture());
  std::ofstream(f.root / "child-mode.txt") << "cleanup";
  Start(f.app, kLaunch);
  REQUIRE(f.Until([&] { return fs::exists(f.root / "child-closed.txt") && f.app.finishing_session; }));
  int64_t closed = 0; std::ifstream(f.root / "child-closed.txt") >> closed;
  CHECK(Now() - closed < 200);
  CHECK_FALSE(f.app.session);
  CHECK(f.app.busy); // Do not overlap two supervisors still holding the lease.
  CHECK_FALSE(IsWindowEnabled(Item(f.app, kPrimary)));
  CHECK_FALSE(IsWindowEnabled(Item(f.app, kCancel)));
  REQUIRE(f.Until([&] { return !f.app.busy; }));
  CHECK_FALSE(f.app.finishing_session);
  CHECK_FALSE(f.app.failed);
}
#endif
