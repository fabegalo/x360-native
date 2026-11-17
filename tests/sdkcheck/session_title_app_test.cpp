#include "../../titles/common/session_title_app.h"
#include "../../titles/common/launch_request.h"
#include "../../titles/common/dashboard_paths.h"
#include "object_app.h"
#include "kernel/title_entries.h"
#include "kernel/title_fakes.h"
#include "kernel/title_image.h"
#include "kernel/kernel_fixture.h"
#include "temp_folder.h"
#include "test_memory.h"
#include <rex/apu/audio_system.h>
#include <rex/hid/input_system.h>
#include <rex/path_settings.h>
#include <rex/kernel/heap.h>
#include <rex/xam/title_lifecycle.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <atomic>
#include <fstream>
#include <stdexcept>

namespace {
class SessionFixtureApp final : public rex::test::ObjectApp<x360n::SessionTitleApp> {
 public:
  explicit SessionFixtureApp(const rex::ppc::PPCImage& image) : ObjectApp("session_fixture", image) {
    rex::test::ReleaseTestMemory();
    rex::test::g_counts.Reset();
    rex::test::g_entry.Reset();
    static_cast<rex::test_ui::FakeUIData&>(rex::test_ui::g_ui) = rex::test_ui::FakeUIData{};
    rex::test::WriteTitle(folder.path / "game");
  }
  rex::test::TempFolder folder{"x360n_session_host_"};
  bool fail_init = false;
  bool reject_settings = false, derive_cache = false, settings_loaded = false;
  std::filesystem::path settled_cache;
  bool dashboard = false;
  unsigned session_unloads = 0;
  rex::u32 resident_address = 0;

 protected:
  void ReportError(std::string_view message) override {
    // Negative fixtures report in the test log, without an interactive startup dialog.
    rex::app::App::ReportError(message);
  }
  void OnRegisterModules(std::vector<rex::IModule*>& modules) override {
    ObjectApp::OnRegisterModules(modules);
    modules.push_back(&provider_);
  }
  rex::Result<void> OnConfigure(rex::SettingsRegistry& settings) override {
    REX_TRY_VOID(SessionTitleApp::OnConfigure(settings));
    for (auto* setting : settings.Enumerate("x360n")) REX_TRY_VOID(setting->Reset());
    REX_TRY_VOID(settings.Reset("title.entry"));
    REX_TRY_VOID(settings.Reset("title.imports"));
    REX_TRY_VOID(settings.Reset("title.metadata_root"));
    REX_TRY_VOID(settings.Reset("title.update_root"));
    REX_TRY_VOID(settings.SetText(rex::SettingLayer::kTitle, "title.entry", "default.xex"));
    REX_TRY_VOID(settings.SetText(rex::SettingLayer::kTitle, "title.imports", ""));
    REX_TRY_VOID(root.SetText(rex::SettingLayer::kTitle, folder.path.string()));
    for (const auto& [name, path] : std::initializer_list<std::pair<const char*, std::filesystem::path>>{
             {"title.game_root", folder.path / "game"}, {"app.user_root", folder.path / "user"},
             {"app.cache_root", folder.path / "cache"}, {"app.log_root", folder.path / "logs"},
             {"app.config_file", folder.path / "config.toml"}, {"x360n.handoff_out", folder.path / "handoff.bin"}})
    {
      REX_TRY_VOID(settings.Reset(name));
      REX_TRY_VOID(settings.SetText(rex::SettingLayer::kTitle, name, path.string()));
    }
    for (const auto* name : {"ui.driver", "hid.drivers", "apu.driver", "gpu.driver"})
      REX_TRY_VOID(settings.SetText(rex::SettingLayer::kTitle, name, "count"));
    return rex::Ok();
  }
  void OnRelease() override {
    // Restore process-wide fixture settings before their owners unregister.
    for (const auto* name : {"title.entry", "title.imports", "title.game_root", "title.metadata_root", "title.update_root"}) {
      CAPTURE(name);
      // A configuration failure disconnects the kernel before the app releases its hooks.
      if (REX_SETTINGS().Find(name)) CHECK(REX_SETTINGS().Reset(name));
    }
    for (auto* setting : REX_SETTINGS().Enumerate("x360n")) {
      CAPTURE(setting->GetName());
      CHECK(setting->Reset());
    }
    SessionTitleApp::OnRelease();
  }
  rex::Result<void> OnInit() override {
    REX_TRY_VOID(SessionTitleApp::OnInit());
    if (fail_init) return rex::Err(rex::ErrorCategory::kRuntime, "synthetic host startup failure");
    return rex::Ok();
  }
  rex::Result<void> OnSettingsLoaded(rex::SettingsRegistry& settings) override {
    REX_TRY_VOID(SessionTitleApp::OnSettingsLoaded(settings));
    settings_loaded = true;
    settled_cache = REX_PATHS().cache_root.Get();
    if (dashboard) {
      const x360n::DashboardAsset originals[] = {{"required.dat",
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"}};
      return x360n::PrepareDashboardPaths(GetTitleRuntime().GetTitlePaths(), "17559", originals);
    }
    if (reject_settings) return rex::Err(rex::ErrorCategory::kValidation, "synthetic invalid originals");
    if (derive_cache) REX_TRY_VOID(REX_PATHS().cache_root.SetText(
        rex::SettingLayer::kRuntime, (settled_cache / "revision").string()));
    return rex::Ok();
  }
  rex::Result<void> OnSessionLaunch(rex::kernel::TitleRuntime& runtime) override {
    resident_address = runtime.GetGuestHeap().SystemHeapAlloc(32);
    if (!resident_address) return rex::Err(rex::ErrorCategory::kMemory, "fixture resident service allocation failed");
    return runtime.GetGuestMemory().Store(resident_address, rex::be_u32{0x12345678});
  }
  void OnSessionUnload(rex::kernel::TitleRuntime& runtime) override {
    ++session_unloads;
    CHECK(runtime.IsLoaded());
    CHECK(REX_INTERFACE(rex::xam::IXAMTitleServices));
    if (resident_address) {
      auto value = runtime.GetGuestMemory().Load<rex::be_u32>(resident_address);
      REQUIRE(value);
      CHECK(uint32_t(*value) == 0x12345678);
      runtime.GetGuestHeap().SystemHeapFree(resident_address);
      resident_address = 0;
    }
    SessionTitleApp::OnSessionUnload(runtime);
  }

 private:
  rex::test::CountingProvider provider_;
};
}

TEST_CASE("Settled host settings precede driver startup and startup freeze",
          "[sdk][service-contracts][session-host]") {
  SessionFixtureApp app(rex::test::MakeTitleImage(rex::test::kReturningEntry));
  const auto requested = app.folder.path / "cli-cache";
  app.derive_cache = true;
  SECTION("valid settled values derive the effective cache before freezing") {
    REQUIRE(app.Init(rex::CLIArgList::Parse(std::vector<std::string>{"--app.cache_root=" + requested.string()})));
    CHECK(app.settings_loaded);
    CHECK(app.settled_cache == requested);
    CHECK(REX_PATHS().cache_root.Get() == requested / "revision");
    CHECK_FALSE(REX_PATHS().cache_root.Set(requested / "late"));
    CHECK(app.Shutdown() == 0);
  }
  SECTION("invalid originals stop before driver startup and clean up") {
    app.reject_settings = true;
    CHECK_FALSE(app.Init(rex::CLIArgList::Parse(std::vector<std::string>{"--app.cache_root=" + requested.string()})));
    CHECK(app.settings_loaded);
    CHECK(app.settled_cache == requested);
    CHECK(app.Shutdown() == 1);
    CHECK(rex::test::g_counts.graphics_shutdown == 0);
    CHECK(rex::test::g_counts.input_shutdown == 0);
    CHECK_FALSE(REX_SETTINGS().Find("x360n.handoff_out"));
  }
}

TEST_CASE("Dashboard settled paths validate originals and isolate each revision",
          "[sdk][service-contracts][session-host][dashboard-paths]") {
  SessionFixtureApp app(rex::test::MakeTitleImage(rex::test::kReturningEntry));
  app.dashboard = true;
  const auto cache = app.folder.path / "selected-cache";
  const auto metadata = app.folder.path / "selected-metadata";
  std::vector<std::string> arguments{"--app.cache_root=" + cache.string()};
  bool accepted = true;
  SECTION("default metadata stays beside the verified originals") {}
  SECTION("explicit metadata is isolated by revision") {
    arguments.push_back("--title.metadata_root=" + metadata.string());
  }
  SECTION("update overlays fail before drivers start") {
    arguments.push_back("--title.update_root=" + app.folder.path.string());
    accepted = false;
  }
  SECTION("missing originals fail before drivers start") { accepted = false; }
  if (accepted || arguments.size() > 1) {
    std::ofstream(app.folder.path / "game" / "required.dat", std::ios::binary) << "abc";
  }
  auto initialized = app.Init(rex::CLIArgList::Parse(arguments));
  CHECK(bool(initialized) == accepted);
  CHECK_FALSE(app.GetTitleRuntime().IsLoaded());
  if (accepted) {
    CHECK(REX_PATHS().cache_root.Get() == cache / "dashboard-17559");
    CHECK(app.GetTitleRuntime().GetTitlePaths().metadata_root.Get() ==
          (arguments.size() > 1 ? metadata / "dashboard-17559" : app.folder.path / "game" / "metadata"));
  } else {
    CHECK(REX_PATHS().cache_root.Get() == cache);
    CHECK(rex::test::g_counts.graphics_shutdown == 0);
  }
  CHECK(app.Shutdown() == (accepted ? 0 : 1));
}

TEST_CASE("Failed shared host startup releases settings before another host initializes",
          "[sdk][service-contracts][session-host]") {
  {
    SessionFixtureApp failed(rex::test::MakeTitleImage(rex::test::kReturningEntry));
    failed.fail_init = true;
    CHECK_FALSE(failed.Init(rex::CLIArgList::Parse(std::vector<std::string>{})));
    CHECK(failed.Shutdown() == 1);
    CHECK_FALSE(REX_SETTINGS().Find("x360n.handoff_out"));
  }
  SessionFixtureApp next(rex::test::MakeTitleImage(rex::test::kReturningEntry));
  REQUIRE(next.Init(rex::CLIArgList::Parse(std::vector<std::string>{})));
  CHECK(REX_SETTINGS().Find("x360n.handoff_out"));
  CHECK(next.Shutdown() == 0);
  CHECK_FALSE(REX_SETTINGS().Find("x360n.handoff_out"));
}

TEST_CASE("Shared title host preserves guest exit and window-close handoff before unload",
          "[sdk][service-contracts][session-host]") {
  bool host_close = false;
  SECTION("guest returns") {}
  SECTION("window closes while the guest waits") { host_close = true; }
  SessionFixtureApp app(rex::test::MakeTitleImage(host_close ? rex::test::kDelayingEntry : rex::test::kReturningEntry));
  REQUIRE(app.Init(rex::CLIArgList::Parse(std::vector<std::string>{})));
  REQUIRE(app.LaunchTitle());
  REQUIRE(rex::test::WaitFor([] { return rex::test::g_entry.entered == 1; }));
  if (host_close) {
    rex::test_ui::Deliver([](rex::ui::IUIEventListener& listener) {
      listener.OnUIEvent(rex::ui::UIEvent(rex::ui::UIEvent::Type::kClose));
    });
  }
  app.Run();
  CHECK(app.GetTitleRuntime().IsLoaded());
  x360n::LaunchRequest request;
  REQUIRE(x360n::ReadLaunchRequest(app.folder.path / "handoff.bin", request));
  CHECK(request.kind == (host_close ? x360n::LaunchRequest::WindowClose : x360n::LaunchRequest::GuestExit));
  CHECK(app.Shutdown() == 0);
  CHECK(app.session_unloads == 1);
  CHECK(app.resident_address == 0);
  CHECK_FALSE(app.GetTitleRuntime().IsLoaded());
  CHECK(rex::test::g_counts.graphics_attach == 1);
  CHECK(rex::test::g_counts.graphics_detach == 1);
  CHECK_FALSE(rex::test::g_counts.ui_closed_with_runtime);
}

TEST_CASE("Failed driver attachment releases resident title services while memory is mapped",
          "[sdk][service-contracts][session-host]") {
  SessionFixtureApp app(rex::test::MakeTitleImage(rex::test::kReturningEntry));
  REQUIRE(app.Init(rex::CLIArgList::Parse(std::vector<std::string>{})));
  rex::test::g_counts.fail_graphics_attach = true;
  CHECK_FALSE(app.LaunchTitle());
  CHECK_FALSE(app.GetTitleRuntime().IsLoaded());
  CHECK(app.session_unloads == 1);
  CHECK(app.resident_address == 0);
  CHECK(app.Shutdown() == 1);
  CHECK(app.session_unloads == 1);
}

TEST_CASE("An older unload guard cannot remove the replacement cleanup",
          "[sdk][service-contracts][session-host]") {
  rex::test::KernelFixture fixture;
  unsigned stale = 0, current = 0;
  auto old = fixture.GetRuntime().OnTitleUnload([&] { ++stale; });
  auto active = fixture.GetRuntime().OnTitleUnload([&] {
    CHECK(fixture.GetRuntime().IsLoaded());
    ++current;
  });
  old.Reset();
  fixture.GetRuntime().Unload();
  CHECK(stale == 0);
  CHECK(current == 1);
  fixture.GetRuntime().Unload();
  CHECK(current == 1);
}

TEST_CASE("Resident host cleanup follows guest worker stop in a loaded tool runtime",
          "[sdk][service-contracts][session-host]") {
  rex::test::KernelFixture fixture;
  std::atomic<bool> entered{false}, stopped{false};
  fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
    const auto output = caller.Alloc(4);
    REQUIRE(caller.Kernel("NtCreateEvent", {output, 0, 0, 0}) == 0);
    const auto event = caller.Load32(output);
    REQUIRE(caller.Spawn([&, event](rex::test::Caller& worker) {
      rex::ScopeGuard completed([&] { stopped = true; });
      entered = true;
      worker.Kernel("NtWaitForSingleObjectEx", {event, 0, 0, 0});
    }));
  });
  REQUIRE(rex::test::WaitFor([&] { return entered.load(); }));
  bool released = false;
  auto release = fixture.GetRuntime().OnTitleUnload([&] {
    CHECK(stopped.load());
    CHECK(fixture.GetRuntime().IsLoaded());
    released = true;
  });
  fixture.GetRuntime().Unload();
  CHECK(released);
  CHECK(stopped.load());
}

TEST_CASE("A host cleanup exception still releases the title runtime",
          "[sdk][service-contracts][session-host]") {
  {
    rex::test::KernelFixture fixture;
    auto release = fixture.GetRuntime().OnTitleUnload([] {
      throw std::runtime_error("synthetic resident service cleanup failure");
    });
    CHECK_THROWS_WITH(fixture.GetRuntime().Unload(), "synthetic resident service cleanup failure");
    CHECK_FALSE(fixture.GetRuntime().IsLoaded());
    CHECK(REX_RUNTIME() == nullptr);
  }
  rex::test::KernelFixture next;
  CHECK(next.GetRuntime().IsLoaded());
}

TEST_CASE("Missing session UI modules fail configuration before title loading",
          "[sdk][service-contracts][session-host]") {
  SessionFixtureApp app(rex::test::MakeTitleImage(rex::test::kReturningEntry));
  const auto missing = app.folder.path / "missing-system-ui.dll";
  CHECK_FALSE(app.Init(rex::CLIArgList::Parse(std::vector<std::string>{
      "--x360n.system_ui_host=" + missing.string(), "--x360n.system_ui_root=" + app.folder.path.string()})));
  CHECK_FALSE(app.GetTitleRuntime().IsLoaded());
  CHECK(rex::test::g_counts.graphics_shutdown == 0);
  CHECK(app.Shutdown() == 1);
}

#if defined(X360N_TEST_SYSTEM_UI_HOST)
TEST_CASE("The real session UI library registers legacy settings before driver startup",
          "[sdk][service-contracts][session-host][session-ui-module]") {
  SessionFixtureApp app(rex::test::MakeTitleImage(rex::test::kReturningEntry));
  REQUIRE(app.Init(rex::CLIArgList::Parse(std::vector<std::string>{
      "--x360n.system_ui_host=" X360N_TEST_SYSTEM_UI_HOST,
      "--x360n.system_ui_root=" + app.folder.path.string(),
      "--dash17559_guide_probe_seconds=7"})));
  CHECK_FALSE(app.GetTitleRuntime().IsLoaded());
  const auto setting = REX_SETTINGS().Find("dash17559.guide_probe_seconds");
  REQUIRE(setting);
  CHECK((*setting)->GetText() == "7");
  CHECK(app.Shutdown() == 0);
  CHECK_FALSE(REX_SETTINGS().Find("dash17559.guide_probe_seconds"));
}
#endif
