// A failed title load leaves host graphics alive until its App owner shuts down.
#include <catch2/catch_test_macros.hpp>
#include <rex/app/title_app.h>
#include <rex/gpu/graphics_system.h>
#include <rex/path_settings.h>
#include "object_app.h"
#include "kernel/title_fakes.h"
#include "kernel/title_image.h"
#include "test_memory.h"
#include "temp_folder.h"

namespace {
class SetupFailureApp final : public rex::test::ObjectApp<rex::app::TitleApp> {
 public:
  explicit SetupFailureApp(const rex::ppc::PPCImage& image) : ObjectApp("setup_failure", image, false) {
    rex::test::ReleaseTestMemory();
    rex::test::g_counts.Reset();
    static_cast<rex::test_ui::FakeUIData&>(rex::test_ui::g_ui) = {};
    rex::test::WriteTitle(folder.path / "game");
  }
  rex::test::TempFolder folder{"x360n_setup_failure_"};
 protected:
  void ReportError(std::string_view message) override { rex::app::App::ReportError(message); }
  void OnRegisterModules(std::vector<rex::IModule*>& modules) override {
    ObjectApp::OnRegisterModules(modules);
    modules.push_back(&provider_);
  }
  rex::Result<void> OnConfigure(rex::SettingsRegistry& settings) override {
    REX_TRY_VOID(TitleApp::OnConfigure(settings));
    REX_TRY_VOID(root.SetText(rex::SettingLayer::kTitle, folder.path.string()));
    for (const auto& [name, value] : std::initializer_list<std::pair<const char*, std::string>>{
        {"title.entry", "default.xex"}, {"title.imports", ""},
        {"title.game_root", (folder.path / "game").string()},
        {"app.user_root", (folder.path / "user").string()},
        {"app.cache_root", (folder.path / "cache").string()},
        {"app.log_root", (folder.path / "logs").string()},
        {"app.config_file", (folder.path / "config.toml").string()},
        {"ui.driver", "count"}, {"hid.drivers", "count"}, {"apu.driver", "count"}, {"gpu.driver", "count"}}) {
      REX_TRY_VOID(settings.Reset(name));
      REX_TRY_VOID(settings.SetText(rex::SettingLayer::kTitle, name, value));
    }
    return rex::Ok();
  }
  void OnRelease() override {
    for (const auto* name : {"title.entry", "title.imports", "title.game_root"})
      if (REX_SETTINGS().Find(name)) CHECK(REX_SETTINGS().Reset(name));
    TitleApp::OnRelease();
  }
 private:
  rex::test::CountingProvider provider_;
};
}

TEST_CASE("a failed title load keeps host graphics until App shutdown", "[sdk][runtime]") {
  // The legacy function table crosses the end of the 0x90000000 XEX heap.
  rex::ppc::PPCImage image{};
  image.code_base = image.image_base = 0x9FFF0000;
  image.code_size = 0x1000;
  image.image_size = 0x8000;
  {
    SetupFailureApp app(image);
    REQUIRE(app.Init({}));
    REQUIRE_FALSE(app.LaunchTitle());
    CHECK(app.GetGraphics().HasDriver());
    CHECK(app.GetGraphics().GetDriverName() == "count");
    CHECK(rex::test::g_counts.graphics_shutdown == 0);
    app.Shutdown();
    CHECK(rex::test::g_counts.graphics_shutdown == 1);
    CHECK_FALSE(app.GetGraphics().HasDriver());
  }
  SetupFailureApp again(image);
  REQUIRE(again.Init({}));
  CHECK(again.Shutdown() == 0);
  CHECK(rex::test::g_counts.graphics_shutdown == 1);
}
