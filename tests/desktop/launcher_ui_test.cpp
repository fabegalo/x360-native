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
#include "catalog_paths.h"
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
#ifndef X360N_FRONTEND_BASELINE
    app.catalog_loaded = true;
#endif
    if (unrelated) {
      Target slow; slow.id = "unrelated"; slow.aot = false;
      for (unsigned i = 0; i != 6000; ++i) slow.inputs.push_back({paths.inputs() / ("missing-" + std::to_string(i)), "missing", std::string(64, 'a')});
      app.targets.push_back(slow); app.checked.resize(2);
    }
    app.selected = 0; InspectSelected(app);
  }
};
}
TEST_CASE("launcher fullscreen and crash capture controls do not start a session", "[launcher-options]") {
  Frontend f;
  CHECK_FALSE(f.app.renderer_settings.fullscreen);
  CHECK_FALSE(f.app.capture_crash);
  SendMessageW(f.app.window, WM_COMMAND, kFullscreen, 0);
  CHECK(f.app.renderer_settings.fullscreen);
  CHECK(LoadRendererSettings(f.paths).fullscreen);
  SendMessageW(f.app.window, WM_COMMAND, kCaptureCrash, 0);
  CHECK(f.app.capture_crash);
  CHECK_FALSE(f.app.busy);
  CHECK_FALSE(fs::exists(f.root / "child-started.txt"));
  SendMessageW(f.app.window, WM_COMMAND, kFullscreen, 0);
  CHECK_FALSE(LoadRendererSettings(f.paths).fullscreen);
}
TEST_CASE("reopening a prepared launcher restores ready without hashing content", "[launcher-ready]") {
  Frontend f; const auto target = f.TargetFixture();
  for (int reopen = 0; reopen != 2; ++reopen) {
    f.app.targets = {target};
    f.app.checked = CheckContents(f.root, f.app.targets, InspectionMode::Preview);
    f.app.selected = 0;
    DigestBatch batch;
    StartHints(f.app);
    REQUIRE(f.Until([&] { return f.app.checked[0].state.receipt_current; }));
    CHECK(StatusOf(f.app, 0) == Row::Ready);
    InspectSelected(f.app);
    CHECK(f.app.next_action == kLaunch);
    CHECK(f.app.chip_tone == Tone::Ok);
    CHECK(batch.files == 0);
    CHECK_FALSE(fs::exists(f.root / "child-started.txt"));
  }
  f.app.checked[0].library = {"registration incomplete"};
  CHECK(StatusOf(f.app, 0) == Row::Imported);
  f.app.checked[0].library.clear();
  ApplyReceiptHint(f.app.checked[0].state, ReceiptHint::Stale);
  CHECK(StatusOf(f.app, 0) == Row::Imported);
  fs::remove(target.exe);
  f.app.checked = CheckContents(f.root, f.app.targets, InspectionMode::Preview);
  CHECK(StatusOf(f.app, 0) != Row::Ready);
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
TEST_CASE("launcher lists and removes every game through its import record", "[library-membership]") {
  const auto root = UniqueTempPath("launcher-membership");
  fs::create_directories(root);
  Target game; game.id = "recipe"; game.game_root = root;
  game.exe = root / "compiled.exe";
  std::ofstream(root / "default.xex") << "original";
  std::ofstream(game.exe) << "compiled fixture, never executed";
  game.inputs = {{root / "default.xex", "default.xex", DigestFile(root / "default.xex")->sha256, {}}};
  App app; app.targets = {game}; app.selected = 0;
  app.checked = CheckContents(root, app.targets, InspectionMode::Preview);
  CHECK_FALSE(app.checked[0].added); CHECK_FALSE(Removable(app));
  app.targets[0].catalogued = true; app.targets[0].catalog_id = "import-record";
  for (const bool generic : {false, true}) {
    app.targets[0].generic_aot = generic;
    app.checked = CheckContents(root, app.targets, InspectionMode::Preview);
    CHECK(app.checked[0].added); CHECK(Removable(app));
  }
  app.targets[0].catalogued = false;
  app.checked = CheckContents(root, app.targets, InspectionMode::Preview);
  CHECK_FALSE(app.checked[0].added); CHECK_FALSE(Removable(app));
  fs::remove_all(root);
}
TEST_CASE("launcher completes consecutive sessions without scanning unrelated content", "[launcher-ui]") {
  Frontend f; const auto target = f.TargetFixture(); f.Seed(target, true);
#ifndef X360N_FRONTEND_BASELINE
  f.app.checked[0].state = Inspect(f.paths, target, InspectionMode::Preview);
  InspectSelected(f.app);
#endif
  const std::string modes[]{"ok", "fail", "ok"};
  for (unsigned step = 0; step != 3; ++step) {
    const auto& mode = modes[step];
    std::ofstream(f.root / "child-mode.txt") << mode;
    fs::remove(f.root / "child-ended.txt");
    const auto launch_start = Now();
    if (step == 2) { SendMessageW(f.app.window, WM_COMMAND, kRetry, 0); f.Pump(); }
    else {
#ifndef X360N_FRONTEND_BASELINE
      if (step == 0) SendMessageW(f.app.window, WM_COMMAND, kPrimary, 0);
      else
#endif
        Start(f.app, kLaunch);
    }
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
TEST_CASE("launcher lists a complete inventory without authenticating or executing it", "[launcher-ui]") {
  Frontend f; const auto target = f.TargetFixture();
  const auto setup = f.root / "titles/dashboard/preparation.toml";
  std::ifstream in(setup); std::string text{std::istreambuf_iterator<char>(in), {}}; in.close();
  std::ofstream(setup) << "inventory='inventory.tsv'\n" << text;
  std::ofstream list(f.root / "titles/dashboard/inventory.tsv");
  for (int i = 0; i != 5000; ++i) list << std::string(64, 'a') << '\t' << i << ".bin\n";
  list.close();
  {
    DigestBatch batch;
    const auto catalog = ReadCatalog(f.paths);
    REQUIRE(catalog.front().inputs.size() == 5001);
    const auto checked = CheckContents(f.root, catalog, InspectionMode::Preview);
    REQUIRE(checked.size() == 1);
    CHECK(checked[0].state.verification_pending);
    CHECK(checked[0].state.launch_candidate);
    CHECK_FALSE(checked[0].state.inputs_ok);
    CHECK_FALSE(checked[0].state.prepared);
    CHECK(batch.files == 0); CHECK(batch.bytes == 0);
  }
  const auto started = Now(); Load(f.app, f.root);
  REQUIRE(f.Until([&] { return !f.app.busy; }));
  std::cout << "complete initial listing ms=" << Now() - started << '\n';
  CHECK(Now() - started < 3000);
  REQUIRE(f.app.catalog_loaded);
  CHECK(f.app.next_action == kLaunch);
  CHECK_FALSE(f.app.current.prepared);
  SendMessageW(f.app.window, WM_COMMAND, kGames, 0);
  SendMessageW(f.app.window, WM_COMMAND, kHome, 0);
  CHECK_FALSE(f.app.busy);
  CHECK_FALSE(fs::exists(f.root / "child-started.txt"));
}
TEST_CASE("preview launch must reject changed content with preserved timestamps", "[launcher-ui]") {
  Frontend f; const auto target = f.TargetFixture(); f.Seed(target);
  f.app.checked[0].state = Inspect(f.paths, target, InspectionMode::Preview);
  const auto file = target.inputs.front().destination;
  const auto timestamp = fs::last_write_time(file);
  std::ofstream(file) << "corrupted input"; fs::last_write_time(file, timestamp);
  SendMessageW(f.app.window, WM_COMMAND, kLaunch, 0);
  REQUIRE(f.app.busy);
  REQUIRE(f.Until([&] { return !f.app.busy; }));
  CHECK(f.app.failed);
  CHECK_FALSE(f.app.checked[0].state.verification_pending);
  CHECK_FALSE(f.app.checked[0].state.inputs_ok);
  CHECK_FALSE(f.app.checked[0].state.prepared);
  REQUIRE_FALSE(f.app.checked[0].state.issues.empty());
  CHECK(f.app.checked[0].state.issues.back().find("dash.xex") != std::string::npos);
  CHECK_FALSE(fs::exists(f.root / "child-started.txt"));
}
TEST_CASE("selected verification authenticates only its preparation without launching tools", "[launcher-ui]") {
  Frontend f; auto target = f.TargetFixture(); target.session = false;
  f.Seed(target, true);
  f.app.checked[0].state = Inspect(f.paths, target, InspectionMode::Preview);
  Start(f.app, kVerifyTarget);
  REQUIRE(f.Until([&] { return !f.app.busy; }));
  CHECK_FALSE(f.app.failed);
  CHECK(f.app.checked[0].state.prepared);
  CHECK_FALSE(f.app.checked[0].state.verification_pending);
  CHECK(f.app.checked[1].state.issues.empty());
  CHECK_FALSE(fs::exists(f.root / "child-started.txt"));
  CHECK_FALSE(fs::exists(f.app.logs / "tools.log"));
}
TEST_CASE("empty catalogs finish loading once and navigation does not restart workers", "[launcher-ui]") {
  Frontend f; Load(f.app, f.root);
  REQUIRE(f.Until([&] { return !f.app.busy; }));
  CHECK(f.app.catalog_loaded); CHECK(f.app.targets.empty());
  SendMessageW(f.app.window, WM_COMMAND, kGames, 0);
  CHECK_FALSE(f.app.busy);
}
TEST_CASE("catalog path reuse pins directory identities for only one catalog read", "[launcher-ui]") {
  Frontend f;
  const auto parent = f.root / "content";
  const auto dir = parent / "nested";
  fs::create_directories(dir);
  std::ofstream(dir / "asset") << "asset";
  {
    CatalogPaths paths(parent);
    REQUIRE(paths.Resolve(dir / "asset"));
    CHECK(paths.Resolve(dir / "missing"));
    CHECK_FALSE(paths.Resolve(dir / "missing-parent/asset"));
    CHECK_FALSE(paths.Resolve(f.root / "outside"));
    CHECK_FALSE(MoveFileExW(dir.c_str(), (parent / "moved").c_str(), 0));
    CHECK_FALSE(MoveFileExW(parent.c_str(), (f.root / "moved").c_str(), 0));
    CHECK(paths.Resolve(dir / "asset"));
  }
  REQUIRE(MoveFileExW(dir.c_str(), (parent / "moved").c_str(), 0));
  CatalogPaths fresh(parent);
  CHECK_FALSE(fresh.Resolve(dir / "asset"));
  CHECK(fresh.Resolve(parent / "moved/asset"));
}
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
namespace {
struct Placed { HWND h; int id; RECT r; };
// Child rectangles in client coordinates; WS_VISIBLE because the fixture parent stays hidden.
std::vector<Placed> VisibleControls(const App& a) {
  std::vector<Placed> out;
  for (auto h : a.all) {
    if (!(GetWindowLongPtrW(h, GWL_STYLE) & WS_VISIBLE)) continue;
    RECT r{}; GetWindowRect(h, &r);
    MapWindowPoints(nullptr, a.window, reinterpret_cast<POINT*>(&r), 2);
    // A clipped (scrolled) control only shows its window region.
    RECT box{};
    const int kind = GetWindowRgnBox(h, &box);
    if (kind == NULLREGION) continue;
    if (kind != ERROR) { OffsetRect(&box, r.left, r.top); IntersectRect(&r, &r, &box); }
    out.push_back({h, GetDlgCtrlID(h), r});
  }
  return out;
}
std::wstring Describe(const Placed& p) {
  wchar_t cls[32]{}, text[96]{}; GetClassNameW(p.h, cls, 32); GetWindowTextW(p.h, text, 96);
  return std::wstring(cls) + L"#" + std::to_wstring(p.id) + L" '" + text + L"' [" + std::to_wstring(p.r.left) + L"," +
         std::to_wstring(p.r.top) + L"," + std::to_wstring(p.r.right) + L"," + std::to_wstring(p.r.bottom) + L"]";
}
std::string Narrow(const std::wstring& s) { std::string out; for (auto c : s) out += c < 128 ? char(c) : '?'; return out; }
// Space the owner-drawn label needs, mirroring DrawButton.
int NeededWidth(App& a, HDC dc, const Placed& p) {
  auto s = [&](int v) { return ui::S(v, a.dpi); };
  const auto style = a.styles[p.id];
  wchar_t text[256]{}; GetWindowTextW(p.h, text, 256);
  const std::wstring label = text;
  switch (style.kind) {
    case Kind::Nav: return s(50) + ui::TextWidth(dc, a.fonts.bold, label) + s(8);
    case Kind::Option: return s(48) + ui::TextWidth(dc, a.fonts.bold, label) + s(16);
    case Kind::Toggle: return s(52) + ui::TextWidth(dc, a.fonts.body, label);
    case Kind::Chip: return ui::TextWidth(dc, a.fonts.bold, label) + s(16);
    default: break;
  }
  const bool glyph = style.glyph || p.id == kPrimary;
  return glyph ? s(22) + s(8) + ui::TextWidth(dc, a.fonts.bold, label) + s(16) : ui::TextWidth(dc, a.fonts.body, label) + s(16);
}
void SaveCapture(App& a, const fs::path& output) {
  RECT client{}; GetClientRect(a.window, &client);
  HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
  HBITMAP bitmap = CreateCompatibleBitmap(screen, client.right, client.bottom);
  const auto old = SelectObject(dc, bitmap);
  Paint(a, dc);
  for (auto child : a.all) {
    if (!(GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE)) continue;
    RECT rect{}; GetWindowRect(child, &rect);
    MapWindowPoints(nullptr, a.window, reinterpret_cast<POINT*>(&rect), 2);
    const auto saved = SaveDC(dc);
    SetViewportOrgEx(dc, rect.left, rect.top, nullptr);
    HRGN region = CreateRectRgn(0, 0, 0, 0);
    if (GetWindowRgn(child, region) != ERROR) { OffsetRgn(region, rect.left, rect.top); ExtSelectClipRgn(dc, region, RGN_AND); }
    else IntersectClipRect(dc, 0, 0, rect.right - rect.left, rect.bottom - rect.top);
    DeleteObject(region);
    SendMessageW(child, WM_PRINT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT | PRF_ERASEBKGND | PRF_CHILDREN);
    RestoreDC(dc, saved);
  }
  SelectObject(dc, old); DeleteDC(dc); ReleaseDC(nullptr, screen);
  UINT count = 0, size = 0; Gdiplus::GetImageEncodersSize(&count, &size);
  std::vector<uint8_t> storage(size);
  auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());
  Gdiplus::GetImageEncoders(count, size, codecs);
  {
    Gdiplus::Bitmap image(bitmap, nullptr);
    for (UINT i = 0; i < count; ++i) if (std::wstring_view(codecs[i].MimeType) == L"image/png") image.Save(output.c_str(), &codecs[i].Clsid);
  }
  DeleteObject(bitmap);
}
}
TEST_CASE("version label names the build channel and never calls a development build stable", "[launcher-ui]") {
  struct Case { const char* version; const char* pt; const char* en; };
  const Case cases[] = {
    {"1.0.0-dev", "v1.0.0-dev · Desenvolvimento", "v1.0.0-dev · Development"},
    {"1.0.0-rc.1", "v1.0.0-rc.1 · Release Candidate", "v1.0.0-rc.1 · Release Candidate"},
    {"1.0.0", "v1.0.0 · Estável", "v1.0.0 · Stable"},
    {"0.69.27-preview.25", "v0.69.27-preview.25 · Prévia", "v0.69.27-preview.25 · Preview"},
    {"1.0.0-rc.1-dev", "v1.0.0-rc.1-dev · Desenvolvimento", "v1.0.0-rc.1-dev · Development"},
    {"2.1.0-nightly.3", "v2.1.0-nightly.3 · Pré-lançamento", "v2.1.0-nightly.3 · Pre-release"},
    {"1.0.0-device", "v1.0.0-device · Pré-lançamento", "v1.0.0-device · Pre-release"}};
  for (const auto& c : cases) {
    INFO(c.version);
    english = false; CHECK(VersionLabel(c.version) == c.pt);
    english = true; CHECK(VersionLabel(c.version) == c.en);
  }
  english = false;
  // The running build is labelled from the version CMake configured, never a fixed prefix.
  CHECK(VersionLabel(X360N_VERSION).rfind(std::string("v") + X360N_VERSION + " · ", 0) == 0);
}
// Geometry review of every page, banner state, size and scale without showing
// the window. Set X360N_LAYOUT_CAPTURE to a folder to also save PNG renders.
TEST_CASE("launcher layout keeps controls inside the window, their cards and their labels", "[launcher-layout]") {
  Frontend f; f.Seed(f.TargetFixture());
  // The sidebar update action takes room only while a newer release is known.
  f.app.update = AvailableUpdate{"10.100.100", "x360-native-10.100.100-windows-x64", "", "", ""};
  wchar_t capture[MAX_PATH]{};
  const fs::path captures = GetEnvironmentVariableW(L"X360N_LAYOUT_CAPTURE", capture, MAX_PATH) ? fs::path(capture) : fs::path();
  if (!captures.empty()) fs::create_directories(captures);
  struct PageCase { const char* name; int command; int preview; };  // preview 6: game list, 7: empty library
  const PageCase pages[] = {{"home", kHome, 0}, {"library", kGames, 7}, {"library-list", kGames, 6}, {"import", kImportPage, 0},
                            {"dashboard", kDashboards, 0}, {"settings", kSettings, 0}, {"diagnostics", kDetails, 0}};
  struct BannerCase { const char* name; int preview; };
  const BannerCase banners[] = {{"idle", 5}, {"busy", 2}, {"error", 3}, {"imported", 4}};
  struct SizeCase { const char* name; int w, h; };
  // Every page and banner at the minimum and default sizes; idle Settings, whose
  // card grid changes with the width, also at common window sizes and scale steps.
  const SizeCase sizes[] = {{"min", 1000, 700}, {"default", 1220, 800}, {"720p", 1280, 720}, {"900p", 1600, 900}, {"1080p", 1920, 1080}};
  for (const bool en : {false, true}) for (const UINT dpi : {96u, 120u, 144u, 192u}) for (const auto& size : sizes) for (const auto& page : pages) for (const auto& banner : banners) {
    const bool base = (dpi == 96 || dpi == 144) && size.w <= 1220;
    if (!base && (page.command != kSettings || banner.preview != 5)) continue;
    english = en;
    if (f.app.dpi != dpi) { f.app.dpi = dpi; ApplyFonts(f.app); }
    Labels(f.app);
    // Client size equals the outer size the window would have at this scale.
    RECT outer{0, 0, MulDiv(size.w, int(dpi), 96), MulDiv(size.h, int(dpi), 96)};
    SetWindowPos(f.app.window, nullptr, 0, 0, outer.right, outer.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
    ApplyPreview(f.app, 5);
    f.app.settings = f.app.show_details = false;
    SendMessageW(f.app.window, WM_COMMAND, page.command == kSettings || page.command == kDetails ? kHome : page.command, 0);
    if (page.preview) ApplyPreview(f.app, page.preview);
    if (page.command == kSettings || page.command == kDetails) SendMessageW(f.app.window, WM_COMMAND, page.command, 0);
    if (banner.preview != 5) ApplyPreview(f.app, banner.preview);
    Layout(f.app);
    const std::string state = std::string(page.name) + "-" + banner.name + "-" + size.name + "-" + std::to_string(dpi) + (en ? "-en" : "-pt");
    INFO(state);
    RECT client{}; GetClientRect(f.app.window, &client);
    const auto controls = VisibleControls(f.app);
    std::vector<RECT> cards;
    const auto& g = f.app.geo;
    for (const auto& r : {g.banner, g.hero, g.drop, g.list_card, g.detail, g.search, g.details_card, g.empty}) cards.push_back(r);
    for (const auto& r : g.stats) cards.push_back(r);
    for (const auto& r : g.tiles) cards.push_back(r);
    for (const auto& r : g.cards) cards.push_back(r);
    HDC dc = GetDC(f.app.window);
    for (size_t i = 0; i < controls.size(); ++i) {
      const auto& c = controls[i];
      INFO(Narrow(Describe(c)));
      CHECK(c.r.left >= 0); CHECK(c.r.top >= 0);
      CHECK(c.r.right <= client.right); CHECK(c.r.bottom <= client.bottom);
      for (const auto& card : cards) {
        RECT hit{};
        if (IsRectEmpty(&card) || !IntersectRect(&hit, &card, &c.r)) continue;
        RECT inside{}; UnionRect(&inside, &card, &c.r);
        CHECK(EqualRect(&inside, &card));
      }
      for (size_t j = i + 1; j < controls.size(); ++j) {
        RECT hit{};
        INFO("overlaps " << Narrow(Describe(controls[j])));
        CHECK_FALSE(IntersectRect(&hit, &c.r, &controls[j].r));
      }
      wchar_t cls[16]{}; GetClassNameW(c.h, cls, 16);
      const int w = c.r.right - c.r.left, h = c.r.bottom - c.r.top;
      RECT region{};
      const bool whole = GetWindowRgnBox(c.h, &region) == ERROR;
      if (std::wstring_view(cls) == L"Button" && w > h && whole) CHECK(NeededWidth(f.app, dc, c) <= w);
      if (std::wstring_view(cls) == L"Static" && !(GetWindowLongPtrW(c.h, GWL_STYLE) & (SS_ENDELLIPSIS | SS_PATHELLIPSIS)) && whole) {
        wchar_t text[2048]{}; GetWindowTextW(c.h, text, 2048);
        RECT need{0, 0, w, 0};
        auto font = reinterpret_cast<HFONT>(SendMessageW(c.h, WM_GETFONT, 0, 0));
        auto previous = SelectObject(dc, font);
        DrawTextW(dc, text, -1, &need, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
        SelectObject(dc, previous);
        CHECK(need.bottom <= h);
      }
    }
    ReleaseDC(f.app.window, dc);
    if (!captures.empty()) SaveCapture(f.app, captures / (state + ".png"));
    if (page.command == kSettings) {
      // Clipped content stays reachable: focus reveals the last control, and the
      // fully scrolled page shows every card above the window's bottom edge.
      const auto last = Item(f.app, kLangEn);
      Reveal(f.app, last);
      RECT box{};
      CHECK(GetWindowRgnBox(last, &box) == ERROR);
      f.app.scroll = 1 << 20; Layout(f.app);
      CHECK(f.app.scroll == f.app.geo.scroll_max);
      for (const auto& card : f.app.geo.cards) CHECK(card.bottom <= client.bottom);
      if (!captures.empty() && f.app.geo.scroll_max > 0) SaveCapture(f.app, captures / (state + "-scrolled.png"));
      f.app.scroll = 0; Layout(f.app);
    }
  }
  english = false;
  ApplyPreview(f.app, 5);
}
TEST_CASE("owner-drawn buttons never erase to the default background before drawing", "[launcher-layout]") {
  Frontend f;
  HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
  HBITMAP bitmap = CreateCompatibleBitmap(screen, 8, 8);
  const auto old = SelectObject(dc, bitmap);
  ReleaseDC(nullptr, screen);
  for (auto h : f.app.all) {
    wchar_t cls[16]{}; GetClassNameW(h, cls, 16);
    if (std::wstring_view(cls) != L"Button") continue;
    INFO(GetDlgCtrlID(h));
    RECT all{0, 0, 8, 8}; ui::Fill(dc, all, RGB(1, 2, 3));
    CHECK(SendMessageW(h, WM_ERASEBKGND, reinterpret_cast<WPARAM>(dc), 0) != 0);
    CHECK(GetPixel(dc, 4, 4) == RGB(1, 2, 3));
  }
  SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc);
}
#endif
