// Exercise the shared launcher controller and the one frontend headless: no
// window, Computer Use, screenshots, input injection, title executables or
// renderer validation. Frames are laid out by the real pages and, on request,
// rasterized by the launcher's own CPU renderer into PNG files.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <cstring>
#include <iostream>
#include <SDL3/SDL.h>
#include <thread>
#include "app_controller.h"
#include "frontend.h"
#include "soft_raster.h"
#include "app_text.h"
#include "console_options.h"
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
  CHECK(PreparationStage("dash_17559 sdk-configure") == 1);
  CHECK(PreparationStage("dash_17559 1-configure") == 2);
  CHECK(PreparationStage("dash_17559 3-reconfigure") == 2);
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
TEST_CASE("a launch refused for a changed preparation offers to prepare again", "[launcher-ui]") {
  Frontend f; const auto target = f.TargetFixture();
  f.app.home = false;  // the Dashboard page, where the version is started
  f.Seed(target);
  f.app.checked[0].state = Inspect(f.paths, target, InspectionMode::Preview);
  InspectSelected(f.app);
  REQUIRE(f.app.next_action == kLaunch);
  // The preparation recipe changed after the receipt; the original files did not.
  std::ofstream(target.manifest, std::ios::app) << "# changed after preparation\n";
  Command(f.app, kLaunch);
  REQUIRE(f.Until([&] { return !f.app.busy; }));
  INFO(f.app.details);
  CHECK(f.app.failed);
  CHECK(f.app.result == 3);
  CHECK(f.app.checked[0].state.inputs_ok);
  CHECK_FALSE(f.app.checked[0].state.prepared);
  CHECK(f.app.next_action == kPrepare);  // not "add content": the files are intact
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
TEST_CASE("visible launcher buttons have independent hover identities", "[launcher-usability]") {
  Frontend f; f.Seed(f.TargetFixture());
  Headless headless; ui::Ui view(f.app); view.record = true;
  ui::ApplyStyle(1);
  auto& io = ImGui::GetIO();
  REQUIRE(io.ConfigDebugHighlightIdConflicts);
  for (int page : {kHome, kGames, kImportPage, kDashboards, kSettings, kDetails}) {
    Command(f.app, kHome); Command(f.app, page);
    for (int frame = 0; frame != 3; ++frame) headless.Frame(view, 1220, 800);
    const auto controls = view.placed;
    std::vector<ImGuiID> hovered;
    for (const auto& control : controls) {
      if (control.kind != "button" || !Inside(view.window, control.rect)) continue;
      if (control.id && !Enabled(f.app, control.id)) continue;
      INFO("page=" << page << " " << Describe(control));
      const auto center = control.rect.GetCenter();
      io.AddMousePosEvent(center.x, center.y);
      for (int frame = 0; frame != 3; ++frame) headless.Frame(view, 1220, 800);
      CHECK(GImGui->HoveredIdPreviousFrameItemCount <= 1);
      const auto id = GImGui->HoveredId;
      if (id) {
        CHECK(std::ranges::find(hovered, id) == hovered.end());
        hovered.push_back(id);
      }
    }
  }
}
TEST_CASE("country options translate and sort without changing console IDs", "[launcher-usability]") {
  const bool en = GENERATE(false, true);
  english = en;
  const auto& options = CountryOptions();
  REQUIRE(options.size() == ConsoleLocales().size());
  std::vector<uint32_t> ids;
  std::string previous;
  for (const auto& option : options) {
    INFO(option.name);
    const auto key = OptionSearchKey(option.name);
    CHECK(previous <= key); previous = key;
    CHECK(std::ranges::any_of(ConsoleLocales(), [&](const auto& value) { return value.country == option.value; }));
    CHECK(std::ranges::find(ids, option.value) == ids.end());
    ids.push_back(option.value);
  }
  const auto germany = std::ranges::find_if(options, [](const auto& v) { return v.value == 24; });
  REQUIRE(germany != options.end());
  CHECK(germany->name == (en ? "Germany (DE)" : "Alemanha (DE)"));
  CHECK(OptionSearchKey("ÍNDIA (IN)").find("india") == 0);
  CHECK(OptionSearchKey("Japão (JP)").find("japao") == 0);
  CHECK(OptionSearchKey("África do Sul (ZA)").find("za") != std::string::npos);
  english = false;
}
TEST_CASE("failed items expose the recorded cause without preparing again", "[launcher-usability]") {
  Frontend f; f.Seed(f.TargetFixture());
  f.app.home = false;
  auto& inspection = f.app.checked[0].state;
  inspection.verification_pending = inspection.interrupted = true;
  inspection.prepared = inspection.launch_candidate = false;
  inspection.issues = {"compile step failed: fixture-specific diagnostic", "logs/fixture.log"};
  InspectSelected(f.app);
  CHECK(f.app.status.find(inspection.issues.front()) == std::string::npos);
  CHECK(f.app.primary_label == "Preparar");
  CHECK(Label(f.app, kViewIssue) == "Ver erro");
  const auto details = f.app.details;
  Command(f.app, kViewIssue);
  CHECK(PageOf(f.app) == Page::Diagnostics);
  CHECK(f.app.details == details);
  CHECK(f.app.details.find("logs/fixture.log") != std::string::npos);
  CHECK(DiagnosticReport(f.app, DiagnosticSection::Content).find(inspection.issues.front()) != std::string::npos);
  CHECK_FALSE(f.app.busy);
  CHECK_FALSE(fs::exists(f.root / "child-started.txt"));
}
TEST_CASE("launcher fonts cover every console language and country label", "[launcher-fonts]") {
  Headless headless;
  auto& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(1000, 700);
  ImGui::NewFrame();
  REQUIRE(io.Fonts->Fonts.Size == 2);
  CHECK(std::string(ui::T().fonts.regular->GetDebugName()) == "Noto Sans CJK JP Regular");
  CHECK(std::string(ui::T().fonts.bold->GetDebugName()) == "Noto Sans CJK JP Medium");
  for (auto* font : io.Fonts->Fonts) CHECK(font->Sources.Size == 1);
  auto check = [&](const char* label) {
    INFO(label);
    for (auto* font : {ui::T().fonts.regular, ui::T().fonts.bold}) {
      const char* at = label;
      while (*at) {
        unsigned int codepoint = 0;
        const int bytes = ImTextCharFromUtf8(&codepoint, at, nullptr);
        REQUIRE(bytes > 0);
        INFO("codepoint " << codepoint);
        REQUIRE(codepoint != 0xFFFD);
        CHECK(font->IsGlyphInFont(ImWchar(codepoint)));
        // Check the actual baked glyph too, so fallback '?' cannot pass.
        for (float size : {14.0f, 28.0f}) {
          const auto* glyph = font->GetFontBaked(size)->FindGlyphNoFallback(ImWchar(codepoint));
          CHECK(glyph != nullptr);
          if (glyph && codepoint != ' ') CHECK(glyph->Visible);
        }
        at += bytes;
      }
    }
  };
  for (const auto& language : ConsoleLanguages()) check(language.name);
  // Country names are displayed by CountryOptions in the application language.
  for (const bool en : {false, true}) {
    english = en;
    for (const auto& option : CountryOptions()) check(option.name.c_str());
  }
  english = false;
  ImGui::EndFrame();
}
TEST_CASE("closing a console selector does not also navigate back", "[launcher-usability]") {
  const ImGuiKey key = GENERATE(ImGuiKey_Escape, ImGuiKey_GamepadFaceRight);
  Frontend f; f.Seed(f.TargetFixture());
  Command(f.app, kSettings);
  Headless headless;
  ui::ApplyStyle(1);
  ui::Ui view(f.app); view.record = true;
  auto& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  auto frame = [&] { headless.Frame(view, 1220, 800); ui::RunDeferred(view); };
  frame(); frame();
  const auto combo = std::ranges::find_if(view.placed, [](const auto& p) { return p.id == kConsoleLanguage; });
  REQUIRE(combo != view.placed.end());
  const auto center = combo->rect.GetCenter();
  io.AddMousePosEvent(center.x, center.y); frame();
  io.AddMouseButtonEvent(0, true); frame();
  io.AddMouseButtonEvent(0, false); frame(); frame();
  REQUIRE(view.popup_was_open);
  CHECK_FALSE(io.WantTextInput);  // selectors contain only the options, no search field
  io.AddKeyEvent(key, true); frame();
  CHECK(PageOf(f.app) == Page::Settings);
  CHECK_FALSE(view.popup_was_open);
  io.AddKeyEvent(key, false); frame(); frame();
  CHECK_FALSE(view.popup_was_open);
  io.AddKeyEvent(key, true); frame();
  CHECK(PageOf(f.app) != Page::Settings);
  io.AddKeyEvent(key, false); frame();
}
TEST_CASE("diagnostic reports distinguish selected content from the operation target", "[launcher-usability]") {
  english = false;
  Frontend f; auto target = f.TargetFixture(); target.label = target.label_en = "Selected content";
  target.limitations = target.limitations_en = "Unrelated development validation notes";
  f.Seed(target);
  InspectSelected(f.app);
  CHECK(f.app.details.find("Unrelated development validation notes") == std::string::npos);
  f.app.environment.cpu = "Test CPU"; f.app.environment.gpu = "Test GPU";
  f.app.environment.missing_tools = {"clang++"}; f.app.environment.toolchain = false;
  f.app.activity_kind = Activity::Prepare; f.app.result = 3;
  f.app.operation_target = "previous-operation-target";
  f.app.result_title = "Preparation failed"; f.app.activity_text = "Required input is missing";
  f.app.logs = f.root / "operation.log"; f.app.operation_details = "exit_code=7";
  f.app.details = "Details of a different selection";
  const auto environment = DiagnosticReport(f.app, DiagnosticSection::Environment);
  CHECK(environment.find("Test CPU") != std::string::npos);
  CHECK(environment.find("Test GPU") != std::string::npos);
  CHECK(environment.find("clang++") != std::string::npos);
  const auto content = DiagnosticReport(f.app, DiagnosticSection::Content);
  CHECK(content.find("Selected content") != std::string::npos);
  CHECK(content.find("previous-operation-target") == std::string::npos);
  const auto operation = DiagnosticReport(f.app, DiagnosticSection::Operation);
  CHECK(operation.find("previous-operation-target") != std::string::npos);
  CHECK(operation.find("Required input is missing") != std::string::npos);
  CHECK(operation.find("operation.log") != std::string::npos);
  CHECK(operation.find("exit_code=7") != std::string::npos);
  CHECK(operation.find("Selected content") == std::string::npos);
  CHECK(operation.find("Details of a different selection") == std::string::npos);
  const auto full = DiagnosticReport(f.app);
  CHECK(full.find(X360N_VERSION) != std::string::npos);
  CHECK(full.find("Unrelated development validation notes") == std::string::npos);
  CHECK_FALSE(f.app.busy);
  CHECK_FALSE(fs::exists(f.root / "child-started.txt"));
  Target dependency; dependency.id = "required-profile";
  f.app.targets[0].dependencies = {dependency.id, "missing-profile"};
  f.app.targets.push_back(dependency); f.app.checked.resize(2);
  f.app.checked[1].state.issues = {"Required component hash mismatch"};
  const auto dependencies = DiagnosticReport(f.app, DiagnosticSection::Content);
  CHECK(dependencies.find("Required component hash mismatch") != std::string::npos);
  CHECK(dependencies.find("missing-profile") != std::string::npos);
  CHECK(dependencies.find("Perfil necessário não encontrado") != std::string::npos);
  OnInspected(f.app, "Environment verification failed: fixture-specific cause");
  CHECK(DiagnosticReport(f.app, DiagnosticSection::Operation).find("fixture-specific cause") != std::string::npos);
  f.app.environment = {}; f.app.targets.clear(); f.app.selected = 0;
  CHECK(DiagnosticReport(f.app, DiagnosticSection::Environment).find("Ambiente ainda não verificado") != std::string::npos);
  CHECK(DiagnosticReport(f.app, DiagnosticSection::Content).find("Nenhum conteúdo selecionado") != std::string::npos);
}
TEST_CASE("small status icons are centered and symmetric in rendered pixels", "[launcher-icons]") {
  const float density = GENERATE(1.f, 1.5f, 2.f);
  Headless headless;
  ui::ApplyStyle(1);
  auto& io = ImGui::GetIO(); io.DisplaySize = ImVec2(128, 128);
  io.DisplayFramebufferScale = ImVec2(density, density);
  const int pixels = int(128 * density);
  for (const char* name : {"warning", "error", "info", "language", "dashboard", "chip"}) for (float size : {16.f, 20.f, 24.f, 32.f}) {
    INFO(name << " at " << size);
    ImGui::NewFrame();
    ui::Icon(ImGui::GetForegroundDrawList(), name, ImRect(44, 44, 84, 84), IM_COL32_WHITE, size);
    ImGui::Render();
    ui::Canvas canvas; canvas.Resize(pixels, pixels);
    std::fill(canvas.pixels.begin(), canvas.pixels.end(), 0);
    ui::Rasterize(canvas, ImGui::GetDrawData());
    int min_x = pixels, min_y = pixels, max_x = -1, max_y = -1;
    for (int y = 0; y < pixels; ++y) for (int x = 0; x < pixels; ++x) if ((canvas.pixels[y * pixels + x] & 255) > 8) {
      min_x = std::min(min_x, x); max_x = std::max(max_x, x);
      min_y = std::min(min_y, y); max_y = std::max(max_y, y);
    }
    REQUIRE(max_x >= min_x); REQUIRE(max_y >= min_y);
    CHECK(std::abs((min_x + max_x + 1) / 2.f - 64.f * density) <= 1.f);
    CHECK(std::abs((min_y + max_y + 1) / 2.f - 64.f * density) <= 1.f);
    uint64_t difference = 0, ink = 0;
    for (int y = min_y; y <= max_y; ++y) for (int x = min_x; x <= max_x; ++x) {
      const int a = canvas.pixels[y * pixels + x] & 255;
      const int b = canvas.pixels[y * pixels + min_x + max_x - x] & 255;
      difference += std::abs(a - b); ink += a + b;
    }
    REQUIRE(ink > 0);
    CHECK(double(difference) / double(ink) < 0.03);
    if (std::string_view(name) == "dashboard") {
      // Four separate tiles, not a connected cross or an eight-shaped outline.
      const int center_x = (min_x + max_x) / 2, center_y = (min_y + max_y) / 2;
      for (int y = min_y; y <= max_y; ++y) CHECK((canvas.pixels[y * pixels + center_x] & 255) <= 8);
      for (int x = min_x; x <= max_x; ++x) CHECK((canvas.pixels[center_y * pixels + x] & 255) <= 8);
    }
  }
  const char* file = std::getenv("X360N_ICON_CAPTURE");
  if (file && density == 1.f) {
    io.DisplaySize = ImVec2(1024, 360);
    const char* names[] = {"warning", "error", "info", "check", "settings", "language", "dashboard", "save"};
    ImGui::NewFrame();
    auto* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(0, 0), io.DisplaySize, ui::P.bg);
    for (int i = 0; i < 8; ++i) {
      const float x = i * 128.f;
      ui::Text(dl, ui::Font::Body, names[i], ImRect(x, 8, x + 128, 40), ui::P.text, ui::Center);
      for (int row = 0; row < 4; ++row) {
        const float sizes[] = {16, 20, 24, 32};
        const auto color = i == 0 ? ui::P.warn : i == 1 ? ui::P.err : ui::P.text;
        ui::Icon(dl, names[i], ImRect(x, 48 + row * 72.f, x + 128, 112 + row * 72.f), color, sizes[row]);
      }
    }
    ImGui::Render();
    ui::Canvas canvas; canvas.Resize(1024, 360); ui::Rasterize(canvas, ImGui::GetDrawData());
    CHECK(ui::WritePng(file, canvas));
  }
}
TEST_CASE("launcher text stays centered and rasterizes at the current display density", "[launcher-fonts]") {
  Headless headless;
  ui::ApplyStyle(1);
  auto& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(160, 80);
  // Reuse the same fonts across monitor changes; direct draw-list text must
  // update even when no ImGui widget has pushed that face in this frame.
  for (float density : {1.f, 2.f, 1.5f, 1.f}) {
    io.DisplayFramebufferScale = ImVec2(density, density);
    for (auto font : {ui::Font::Body, ui::Font::Bold, ui::Font::Caption, ui::Font::Headline}) {
      INFO("density=" << density << " font=" << int(font));
      ImGui::NewFrame();
      ui::Text(ImGui::GetForegroundDrawList(), font, "HHH", ImRect(0, 20, 160, 60), IM_COL32_WHITE, ui::Center);
      auto* face = font == ui::Font::Bold || font == ui::Font::Headline ? ui::T().fonts.bold : ui::T().fonts.regular;
      CHECK(face->GetFontBaked(ui::FontSize(font))->RasterizerDensity == density);
      ImGui::Render();
      ui::Canvas canvas; canvas.Resize(int(160 * density), int(80 * density));
      std::fill(canvas.pixels.begin(), canvas.pixels.end(), 0);
      ui::Rasterize(canvas, ImGui::GetDrawData());
      int lo = canvas.height, hi = -1;
      for (int y = 0; y < canvas.height; ++y) for (int x = 0; x < canvas.width; ++x)
        if ((canvas.pixels[y * canvas.width + x] & 255) > 32) { lo = std::min(lo, y); hi = std::max(hi, y); }
      REQUIRE(hi >= lo);
      CHECK(std::abs((lo + hi + 1) / 2.f - 40 * density) <= density);
    }
  }
}
TEST_CASE("control borders have crisp physical pixel edges", "[launcher-layout]") {
  const float density = GENERATE(1.f, 1.5f, 2.f);
  const float stroke = GENERATE(1.f, 2.f);
  INFO("density=" << density << " stroke=" << stroke);
  Headless headless;
  auto& io = ImGui::GetIO(); io.DisplaySize = ImVec2(100, 80);
  io.DisplayFramebufferScale = ImVec2(density, density);
  ImGui::NewFrame();
  ui::Round(ImGui::GetForegroundDrawList(), ImRect(20, 20, 80, 60), 8, 0, IM_COL32_WHITE, stroke);
  ImGui::Render();
  ui::Canvas canvas; canvas.Resize(int(100 * density), int(80 * density));
  std::fill(canvas.pixels.begin(), canvas.pixels.end(), 0);
  ui::Rasterize(canvas, ImGui::GetDrawData());
  const int left = int(20 * density), right = int(80 * density), width = int(std::round(stroke * density));
  for (int x = left - 2; x < right + 2; ++x) {
    const bool ink = (x >= left && x < left + width) || (x >= right - width && x < right);
    const int pixel = canvas.pixels[int(40 * density) * canvas.width + x] & 255;
    CHECK(pixel == (ink ? 255 : 0));
  }
}
// Geometry review of every page, banner state, size and scale without showing
// a window. Set X360N_LAYOUT_CAPTURE to a folder to also save PNG renders.
TEST_CASE("launcher layout keeps controls inside the window, their cards and their labels", "[launcher-layout]") {
  const bool expanded = GENERATE(false, true);
  INFO((expanded ? "expanded experiments" : "collapsed experiments"));
  Frontend f; f.Seed(f.TargetFixture());
  // The sidebar update action takes room only while a newer release is known.
  f.app.update = AvailableUpdate{"10.100.100", "x360-native-10.100.100-" + PackagePlatform(), "", "", ""};
  const char* capture = std::getenv("X360N_LAYOUT_CAPTURE");
  const fs::path captures = capture ? fs::path(capture) : fs::path();
  if (!captures.empty()) fs::create_directories(captures);
  const bool quick_capture = std::getenv("X360N_LAYOUT_CAPTURE_QUICK") != nullptr;
  Headless headless;
  ui::Ui view(f.app);
  view.native_expanded = expanded;
  view.upload = ui::UpdateSoftTexture;
  view.record = true;
  struct PageCase { const char* name; int command; int preview; int diagnostic_tab = 0; };  // preview 6: game list, 7: empty library
  const PageCase pages[] = {{"home", kHome, 0}, {"library", kGames, 7}, {"library-list", kGames, 6}, {"import", kImportPage, 0},
                            {"dashboard", kDashboards, 0}, {"settings", kSettings, 0}, {"diagnostics", kDetails, 0},
                            {"diagnostics-content", kDetails, 0, 1}, {"diagnostics-operation", kDetails, 0, 2}};
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
    view.diagnostic_tab = page.diagnostic_tab;
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
    const bool save_capture = !captures.empty() && (!quick_capture ||
      (!expanded && !en && scale == 1.f && size.w == 1220));
    if (save_capture) headless.Capture(captures / (state + ".png"));
    if (page.command == kSettings) {
      // Clipped content stays reachable: the fully scrolled page shows every card above the window's bottom edge.
      view.settings_scroll = 1e9f;
      for (int frame = 0; frame != 3; ++frame) headless.Frame(view, W, H);
      view.deferred.clear();
      for (const auto& card : view.cards) CHECK(card.Max.y <= H + 0.5f);
      if (save_capture && view.settings_scroll_max > 0) headless.Capture(captures / (state + "-scrolled.png"));
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
