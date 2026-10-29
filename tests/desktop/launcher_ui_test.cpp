// Exercise the shared launcher controller and the one frontend headless: no
// window, Computer Use, screenshots, input injection, title executables or
// renderer validation. Frames are laid out by the real pages and, on request,
// rasterized by the launcher's own CPU renderer into PNG files.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include <cstdlib>
#include <fstream>
#include <cstring>
#include <iostream>
#include <SDL3/SDL.h>
#include <thread>
#include "app_controller.h"
#include "frontend.h"
#include "soft_raster.h"
#include "app_text.h"
#include "digest_batch.h"
#include "preparation.h"
#include "host_platform.h"
#include "../supervisor/preparation_fixture.h"
#include "../supervisor/scratch.h"
#include "catalog_paths.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
namespace fs = std::filesystem;
using namespace x360n;
using namespace x360n::desktop;
namespace {
using Clock = std::chrono::steady_clock;
int64_t Now() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(); }
struct Frontend {
  fs::path root = UniqueTempPath("launcher-ui") / std::to_string(Now());
  ProjectPaths paths{root};
  App app;
  Frontend() {
    fs::create_directories(root);
    app.root = root; app.self = root / ("launcher" + ExecutableSuffix());
    app.environment.description = "synthetic environment";
    app.environment.graphics = app.environment.writable = app.environment.architecture = true;
    app.supervisor_available = true;
    fs::copy_file(X360N_LAUNCHER_CHILD, root / ("x360n" + ExecutableSuffix()));
  }
  ~Frontend() {
    app.cancel = true;
    if (app.worker.joinable()) app.worker.join();
    Pump(app);
    std::error_code ec; fs::remove_all(root, ec);
  }
  bool Until(const std::function<bool()>& done, int milliseconds = 15000) {
    const auto end = Now() + milliseconds;
    do { Pump(app); if (done()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(5)); } while (Now() < end);
    return false;
  }
  Target TargetFixture() {
    const auto dir = root / "titles/dashboard";
    const auto game = paths.inputs() / "dashboard";
    const auto exe = NativeTitleExecutable(paths, "dashboard");  // build/titles/... (.exe) on Windows x64, build/<host>/titles/... elsewhere
    fs::create_directories(dir); fs::create_directories(game); fs::create_directories(exe.parent_path());
    std::ofstream(game / "dash.xex") << "synthetic input";
    // A real native image of this host (the fixture child), never executed by these tests.
    fs::copy_file(X360N_LAUNCHER_CHILD, exe, fs::copy_options::overwrite_existing);
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
    app.catalog_loaded = true;
    if (unrelated) {
      Target slow; slow.id = "unrelated"; slow.aot = false;
      for (unsigned i = 0; i != 6000; ++i) slow.inputs.push_back({paths.inputs() / ("missing-" + std::to_string(i)), "missing", std::string(64, 'a')});
      app.targets.push_back(slow); app.checked.resize(2);
    }
    app.selected = 0; InspectSelected(app);
  }
};
}  // namespace
TEST_CASE("launcher fullscreen and crash capture controls do not start a session", "[launcher-options]") {
  Frontend f;
  CHECK_FALSE(f.app.renderer_settings.fullscreen);
  CHECK_FALSE(f.app.capture_crash);
  Command(f.app, kFullscreen);
  CHECK(f.app.renderer_settings.fullscreen);
  CHECK(LoadRendererSettings(f.paths).fullscreen);
  Command(f.app, kCaptureCrash);
  CHECK(f.app.capture_crash);
  CHECK_FALSE(f.app.busy);
  CHECK_FALSE(fs::exists(f.root / "child-started.txt"));
  Command(f.app, kFullscreen);
  CHECK_FALSE(LoadRendererSettings(f.paths).fullscreen);
}
TEST_CASE("launcher offers only the graphics APIs of this system and never swaps a choice silently", "[launcher-ui]") {
  Frontend f;
  const auto initial = f.app.renderer_settings;
#ifdef _WIN32
  CHECK(initial.api == GraphicsApi::D3D12);
  CHECK(Enabled(f.app, kApiD3D12));
  CHECK_FALSE(Enabled(f.app, kApiVulkan));  // Vulkan stays behind the development access on Windows
  Command(f.app, kNativeDevelopment);
  CHECK(Enabled(f.app, kApiVulkan));
  CHECK(Enabled(f.app, kRendererNative));
  Command(f.app, kApiVulkan);
  CHECK(LoadRendererSettings(f.paths).api == GraphicsApi::Vulkan);
#else
  CHECK(initial.api == GraphicsApi::Vulkan);
  CHECK_FALSE(Enabled(f.app, kApiD3D12));
  CHECK(Enabled(f.app, kApiVulkan));
  Command(f.app, kApiD3D12);  // unavailable here: refused, nothing saved
  CHECK(f.app.renderer_settings.api == GraphicsApi::Vulkan);
  Command(f.app, kNativeDevelopment);
  CHECK_FALSE(Enabled(f.app, kRendererNative));  // Native requires Direct3D 12
#endif
  // Leaving development access returns to this host's default renderer and API.
  Command(f.app, kNativeDevelopment);
  CHECK(f.app.renderer_settings.renderer == Renderer::Xenos);
  CHECK(f.app.renderer_settings.api == initial.api);
  CHECK(LoadRendererSettings(f.paths).api == initial.api);
}
TEST_CASE("preparation stages come from the shared supervisor pipeline in order", "[launcher-ui]") {
  const auto names = PreparationStageNames();
  REQUIRE(names.size() == 7);
  CHECK(PreparationStage("dash_17559 sdk-build") == 1);
  CHECK(PreparationStage("dash_17559 reused-shared-sdk") == 1);
  CHECK(PreparationStage("dash_17559 2-tool") == 2);
  CHECK(PreparationStage("dash_17559 2-codegen") == 3);
  CHECK(PreparationStage("dash_17559 reused-codegen") == 3);
  CHECK(PreparationStage("dash_17559 4-build") == 4);
  CHECK(PreparationStage("dash_17559 prepared") == 5);
  CHECK(PreparationStage("dash_17559 reused-preparation") == 5);
  CHECK(PreparationStage("dash_17559 something-new") == -1);  // unknown never advances progress
  const auto command = PreparationCommand("C:/x360 env", "C:/x360 env/x360n" + ExecutableSuffix(), {"--root", "C:/x360 env", "prepare", "dash_17559", "--keep-incomplete"});
#ifdef _WIN32
  REQUIRE(command.args.size() == 10);
  CHECK(command.args[2] == "call");
  CHECK(command.args[3].find("devshell.cmd") != std::string::npos);
  CHECK(command.args[7] == "prepare");
  CHECK_THROWS(PreparationCommand("C:/100%", "C:/100%/x360n.exe", {"--root", "C:/100%"}));
#else
  CHECK(command.exe == fs::path("C:/x360 env/x360n"));
  CHECK(command.args.size() == 5);
#endif
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
TEST_CASE("launcher catalog loading leaves the frontend thread responsive", "[launcher-ui]") {
  Frontend f; f.TargetFixture();
  // Resolving a large inventory used to run synchronously inside Load().
  const auto inventory = f.root / "titles/dashboard/inventory.tsv";
  std::ofstream list(inventory);
  for (int i = 0; i != 5000; ++i) list << std::string(64, 'a') << '\t' << i << ".bin\n";
  list.close();
  // inventory belongs at table root, before [[files]].
  auto setup = f.root / "titles/dashboard/preparation.toml";
  std::ifstream in(setup); std::string text{std::istreambuf_iterator<char>(in), {}}; in.close();
  text.insert(0, "inventory='inventory.tsv'\n"); std::ofstream(setup) << text;
  const auto start = Now(); Load(f.app, f.root); const auto dispatch = Now() - start;
  std::cout << "catalog UI dispatch ms=" << dispatch << '\n';
  CHECK(dispatch < 200);
  REQUIRE(f.app.busy);
  const auto heartbeat = Now(); Command(f.app, kDetails);
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
  f.app.checked[0].state = Inspect(f.paths, target, InspectionMode::Preview);
  InspectSelected(f.app);
  const std::string modes[]{"ok", "fail", "ok"};
  for (unsigned step = 0; step != 3; ++step) {
    const auto& mode = modes[step];
    std::ofstream(f.root / "child-mode.txt") << mode;
    fs::remove(f.root / "child-ended.txt");
    const auto launch_start = Now();
    if (step == 2) Command(f.app, kRetry);
    else if (step == 0) Command(f.app, kPrimary);
    else Start(f.app, kLaunch);
    REQUIRE(f.app.busy);
    Command(f.app, kDetails);
    Command(f.app, mode == "fail" ? kGames : kHome);
    CHECK(f.app.busy);
    CHECK_FALSE(Enabled(f.app, kPrimary));
    REQUIRE(f.Until([&] { return !f.app.busy; }));
    INFO(f.app.details);
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
    CHECK(Enabled(f.app, kFolder));
    CHECK(Enabled(f.app, kPrimary));  // Runtime failure needs no rebuild.
  }
}
TEST_CASE("launcher cancels verification before creating a session process", "[launcher-ui]") {
  Frontend f; f.Seed(f.TargetFixture());
  Start(f.app, kLaunch); f.app.cancel = true;
  REQUIRE(f.Until([&] { return !f.app.busy; }));
  CHECK_FALSE(fs::exists(f.root / "child-started.txt"));
  CHECK_FALSE(f.app.session);
}
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
  Command(f.app, kGames);
  Command(f.app, kHome);
  CHECK_FALSE(f.app.busy);
  CHECK_FALSE(fs::exists(f.root / "child-started.txt"));
}
TEST_CASE("preview launch must reject changed content with preserved timestamps", "[launcher-ui]") {
  Frontend f; const auto target = f.TargetFixture(); f.Seed(target);
  f.app.checked[0].state = Inspect(f.paths, target, InspectionMode::Preview);
  const auto file = target.inputs.front().destination;
  const auto timestamp = fs::last_write_time(file);
  std::ofstream(file) << "corrupted input"; fs::last_write_time(file, timestamp);
  Command(f.app, kLaunch);
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
  Command(f.app, kGames);
  CHECK_FALSE(f.app.busy);
}
TEST_CASE("questions wait for an answer and a declined removal changes nothing", "[launcher-ui]") {
  Frontend f; auto target = f.TargetFixture();
  target.kind = "game"; target.catalogued = true; target.catalog_id = "import-record";
  f.Seed(target);
  f.app.games = true; f.app.home = false;
  REQUIRE(Removable(f.app));
  Command(f.app, kRemove);
  REQUIRE(f.app.prompt);
  CHECK_FALSE(f.app.busy);
  CHECK(f.app.prompt->warning);
  Answer(f.app, 0);  // the default answer keeps the game
  CHECK_FALSE(f.app.prompt);
  CHECK_FALSE(f.app.busy);
  CHECK(fs::exists(target.exe));
  // Pickers are requested from the frontend; a cancelled pick does nothing.
  Command(f.app, kImportFolder);
  REQUIRE(f.app.pick);
  CHECK(f.app.pick->folder);
  Picked(f.app, f.app.pick->command, {});
  CHECK_FALSE(f.app.pick);
  CHECK_FALSE(f.app.busy);
}
#ifdef _WIN32
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
#endif
TEST_CASE("launcher reports native closure before supervisor diagnostic cleanup completes", "[launcher-ui]") {
  Frontend f; f.Seed(f.TargetFixture());
  std::ofstream(f.root / "child-mode.txt") << "cleanup";
  Start(f.app, kLaunch);

  const bool closed_seen = f.Until([&] { return fs::exists(f.root / "child-closed.txt") && f.app.finishing_session; });
  INFO(f.app.details);
  REQUIRE(closed_seen);
  int64_t closed = 0; std::ifstream(f.root / "child-closed.txt") >> closed;
  CHECK(Now() - closed < 200);
  CHECK_FALSE(f.app.session);
  CHECK(f.app.busy);  // Do not overlap two supervisors still holding the lease.
  CHECK_FALSE(Enabled(f.app, kPrimary));
  CHECK_FALSE(Enabled(f.app, kCancel));
  REQUIRE(f.Until([&] { return !f.app.busy; }));
  CHECK_FALSE(f.app.finishing_session);
  CHECK_FALSE(f.app.failed);
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

namespace {
// One headless ImGui context with the launcher's fonts and style.
struct Headless {
  ImGuiContext* context = ImGui::CreateContext();
  Headless() {
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ui::LoadFonts(io);
  }
  ~Headless() { ImGui::DestroyContext(context); }
  void Frame(ui::Ui& view, float w, float h) {
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(w, h); io.DeltaTime = 1.0f / 60;
    ImGui::NewFrame();
    ui::DrawFrame(view);
    ImGui::Render();
    if (auto* textures = ImGui::GetDrawData()->Textures) for (auto* t : *textures) ui::UpdateSoftTexture(t);
  }
  void Capture(const fs::path& file) {
    auto* data = ImGui::GetDrawData();
    ui::Canvas canvas; canvas.Resize(int(data->DisplaySize.x), int(data->DisplaySize.y));
    std::fill(canvas.pixels.begin(), canvas.pixels.end(), IM_COL32(13, 15, 18, 255));
    ui::Rasterize(canvas, data);
    CHECK(ui::WritePng(file, canvas));
  }
};
bool Inside(const ImRect& outer, const ImRect& inner) {
  constexpr float e = 0.51f;
  return inner.Min.x >= outer.Min.x - e && inner.Min.y >= outer.Min.y - e && inner.Max.x <= outer.Max.x + e && inner.Max.y <= outer.Max.y + e;
}
bool Overlap(const ImRect& a, const ImRect& b) {
  constexpr float e = 0.51f;
  return a.Min.x < b.Max.x - e && b.Min.x < a.Max.x - e && a.Min.y < b.Max.y - e && b.Min.y < a.Max.y - e;
}
std::string Describe(const ui::Placed& p) {
  return p.kind + "#" + std::to_string(p.id) + " '" + p.text.substr(0, 60) + "' [" + std::to_string(int(p.rect.Min.x)) + "," + std::to_string(int(p.rect.Min.y)) +
         "," + std::to_string(int(p.rect.Max.x)) + "," + std::to_string(int(p.rect.Max.y)) + "]";
}
}  // namespace
// Geometry review of every page, banner state, size and scale without showing
// a window. Set X360N_LAYOUT_CAPTURE to a folder to also save PNG renders.
TEST_CASE("launcher layout keeps controls inside the window, their cards and their labels", "[launcher-layout]") {
  Frontend f; f.Seed(f.TargetFixture());
  // The sidebar update action takes room only while a newer release is known.
  f.app.update = AvailableUpdate{"10.100.100", "x360-native-10.100.100-" + PackagePlatform(), "", "", ""};
  const char* capture = std::getenv("X360N_LAYOUT_CAPTURE");
  const fs::path captures = capture ? fs::path(capture) : fs::path();
  if (!captures.empty()) fs::create_directories(captures);
  Headless headless;
  ui::Ui view(f.app);
  view.upload = ui::UpdateSoftTexture;
  view.record = true;
  struct PageCase { const char* name; int command; int preview; };  // preview 6: game list, 7: empty library
  const PageCase pages[] = {{"home", kHome, 0}, {"library", kGames, 7}, {"library-list", kGames, 6}, {"import", kImportPage, 0},
                            {"dashboard", kDashboards, 0}, {"settings", kSettings, 0}, {"diagnostics", kDetails, 0}};
  struct BannerCase { const char* name; int preview; };
  const BannerCase banners[] = {{"idle", 5}, {"busy", 2}, {"error", 3}, {"imported", 4}};
  struct SizeCase { const char* name; int w, h; };
  // Every page and banner at the minimum and default sizes; idle Settings, whose
  // card grid changes with the width, also at common window sizes and scale steps.
  const SizeCase sizes[] = {{"min", 1000, 700}, {"default", 1220, 800}, {"720p", 1280, 720}, {"900p", 1600, 900}, {"1080p", 1920, 1080}};
  for (const bool en : {false, true}) for (const float scale : {1.0f, 1.25f, 1.5f, 2.0f}) for (const auto& size : sizes) for (const auto& page : pages) for (const auto& banner : banners) {
    const bool base = (scale == 1.0f || scale == 1.5f) && size.w <= 1220;
    if (!base && (page.command != kSettings || banner.preview != 5)) continue;
    english = en;
    ui::ApplyStyle(scale);
    // Window pixels equal the outer size the window would have at this scale.
    const float W = float(int(size.w * scale)), H = float(int(size.h * scale));
    ApplyPreview(f.app, 5);
    f.app.settings = f.app.show_details = false;
    Command(f.app, page.command == kSettings || page.command == kDetails ? kHome : page.command);
    if (page.preview) ApplyPreview(f.app, page.preview);
    if (page.command == kSettings || page.command == kDetails) Command(f.app, page.command);
    if (banner.preview != 5) ApplyPreview(f.app, banner.preview);
    view.settings_scroll = 0;
    for (int frame = 0; frame != 3; ++frame) headless.Frame(view, W, H);
    view.deferred.clear();
    const std::string state = std::string(page.name) + "-" + banner.name + "-" + size.name + "-" + std::to_string(int(scale * 100)) + (en ? "-en" : "-pt");
    INFO(state);
    const ImRect window(0, 0, W, H);
    const auto& placed = view.placed;
    REQUIRE_FALSE(placed.empty());
    for (size_t i = 0; i < placed.size(); ++i) {
      const auto& c = placed[i];
      INFO(Describe(c));
      CHECK(c.rect.Min.x >= -0.5f); CHECK(c.rect.Max.x <= W + 0.5f);
      if (!c.scrolled) { CHECK(c.rect.Min.y >= -0.5f); CHECK(c.rect.Max.y <= H + 0.5f); }
      if (c.card.GetWidth() > 0) CHECK(Inside(c.card, c.rect));
      for (size_t j = i + 1; j < placed.size(); ++j) {
        INFO("overlaps " << Describe(placed[j]));
        CHECK_FALSE(Overlap(c.rect, placed[j].rect));
      }
      if (c.kind == "button" && c.need > 0) CHECK(c.need <= c.rect.GetWidth() + 0.5f);
      if (c.kind == "text") CHECK(c.need <= c.rect.GetHeight() + 0.5f);
    }
    if (!captures.empty()) headless.Capture(captures / (state + ".png"));
    if (page.command == kSettings) {
      // Clipped content stays reachable: the fully scrolled page shows every card above the window's bottom edge.
      view.settings_scroll = 1e9f;
      for (int frame = 0; frame != 3; ++frame) headless.Frame(view, W, H);
      view.deferred.clear();
      for (const auto& card : view.cards) CHECK(card.Max.y <= H + 0.5f);
      if (!captures.empty() && view.settings_scroll_max > 0) headless.Capture(captures / (state + "-scrolled.png"));
      view.settings_scroll = 0;
    }
  }
  english = false;
  ApplyPreview(f.app, 5);
}
TEST_CASE("captures are valid PNG files that SDL decodes back pixel for pixel", "[launcher-layout]") {
  const auto file = UniqueTempPath("launcher-png") / "capture.png";
  fs::create_directories(file.parent_path());
  ui::Canvas canvas; canvas.Resize(3, 2);
  for (size_t i = 0; i < canvas.pixels.size(); ++i) canvas.pixels[i] = IM_COL32(16 * i, 255 - 16 * i, 7, 255);
  REQUIRE(ui::WritePng(file, canvas));
  std::ifstream in(file, std::ios::binary);
  const std::string png{std::istreambuf_iterator<char>(in), {}};
  // The same decoder the launcher uses for package icons.
  SDL_Surface* decoded = SDL_LoadPNG_IO(SDL_IOFromConstMem(png.data(), png.size()), true);
  REQUIRE(decoded);
  SDL_Surface* rgba = SDL_ConvertSurface(decoded, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(decoded);
  REQUIRE(rgba);
  CHECK(rgba->w == 3); CHECK(rgba->h == 2);
  for (int y = 0; y < 2; ++y) for (int x = 0; x < 3; ++x) {
    uint32_t pixel = 0; std::memcpy(&pixel, static_cast<const unsigned char*>(rgba->pixels) + y * rgba->pitch + x * 4, 4);
    CHECK(pixel == canvas.pixels[size_t(y) * 3 + x]);
  }
  SDL_DestroySurface(rgba);
  std::error_code ec; fs::remove_all(file.parent_path(), ec);
}
TEST_CASE("every page opens from an empty environment and navigates by command", "[launcher-layout]") {
  Frontend f;
  Headless headless;
  ui::Ui view(f.app);
  view.upload = ui::UpdateSoftTexture;
  ui::ApplyStyle(1.0f);
  Load(f.app, f.root);
  REQUIRE(f.Until([&] { return !f.app.busy; }));
  for (int id : {kHome, kGames, kImportPage, kDashboards, kSettings, kDetails, kGoBack, kLanguage, kLanguage}) {
    Command(f.app, id);
    for (int frame = 0; frame != 2; ++frame) headless.Frame(view, 1220, 800);
    ui::RunDeferred(view);
    CHECK_FALSE(f.app.busy);
    CHECK(f.app.result != 3);
  }
  CHECK(PageOf(f.app) == Page::Dashboard);
  CHECK_FALSE(english);
}
