#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <array>
#include <chrono>
#include "kernel/title_fixture.h"
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/kernel_settings.h>
#include <rex/xam/title_lifecycle.h>
#include <rex/xam/user_profile.h>
#include <toml++/toml.hpp>
#include "../../titles/common/games_played.h"
#include "../../titles/common/launch_handoff.h"
#include "../../titles/common/launch_request.h"

namespace {
struct Store {
  // Keep SDK profile paths below MAX_PATH even with the sandbox's long TEMP.
  std::filesystem::path dir = std::filesystem::current_path() /
      ("games-played-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  Store() { std::filesystem::create_directories(dir); }
  ~Store() { std::error_code ec; std::filesystem::remove_all(dir, ec); }
};
}

TEST_CASE("real installed Arcade license controls Games Played in both dashboard profiles",
          "[sdk][games-played][local-license]") {
  const char* path = std::getenv("X360N_TEST_ARCADE_IMAGE");
  if (!path) SKIP("Set X360N_TEST_ARCADE_IMAGE to an installed Arcade entrypoint");
  const std::filesystem::path image = path;
  REQUIRE(std::filesystem::is_regular_file(image));
  for (const auto* dashboard : {"dash_6717", "dash_17559"}) {
    INFO(dashboard);
    const auto prep = std::filesystem::path(X360N_SOURCE_ROOT) / "titles" / dashboard / "preparation.toml";
    const auto table = toml::parse_file(prep.string());
    const auto version = table["dashboard_version"].value_or(std::string{});
    REQUIRE(x360n::ParseDashboardVersion(version));
    Store work;
    uint64_t account = 0, other = 0;
    uint32_t title = 0;
    bool full = false;
    {
      rex::test::ReleaseTestMemory();
      rex::test::KernelHostApp app("games_played_contract", work.dir);
      const std::vector<std::string> args{"--app.config_file=", "--app.user_root=" + work.dir.string()};
      REQUIRE(app.Init(rex::CLIArgList::Parse(args)));
      REQUIRE(x360n::RegisterLaunchSettings(REX_SETTINGS()));
      rex::ScopeGuard unregister([] { REX_SETTINGS().Unregister(x360n::GetLaunchSettings()); });
      auto& settings = x360n::GetLaunchSettings();
      struct Saved { rex::SettingBase* setting; std::string text; rex::SettingLayer layer; };
      const std::array saved{Saved{&settings.handoff_in, settings.handoff_in.GetText(), settings.handoff_in.GetLayer()},
          Saved{&settings.profile_root, settings.profile_root.GetText(), settings.profile_root.GetLayer()},
          Saved{&settings.dashboard_version, settings.dashboard_version.GetText(), settings.dashboard_version.GetLayer()}};
      rex::ScopeGuard restore([&] {
        for (const auto& entry : saved) {
          CHECK(entry.setting->Reset());
          if (entry.layer != rex::SettingLayer::kDefault) CHECK(entry.setting->SetText(entry.layer, entry.text));
        }
      });
      rex::kernel::TitleRuntime rt(rex::ppc::PPCImage{});
      rex::ScopeGuard unload([&] {
        rt.Unload(); CHECK(rt.GetTitlePaths().game_root.Reset());
        CHECK(rt.GetTitlePaths().entry.Reset()); CHECK(rt.GetTitlePaths().imports.Reset());
      });
      REQUIRE(rt.GetTitlePaths().game_root.Set(image.parent_path()));
      REQUIRE(rt.GetTitlePaths().entry.Set(image.filename().string()));
      REQUIRE(rt.GetTitlePaths().imports.Set(""));
      // Load original metadata only; no guest execution or renderer.
      REQUIRE(rt.Load());
      REQUIRE(rt.GetKernelSettings().license_mask.Get() == 0);
      title = rt.GetTitleID();
      const auto content = x360n::ReadLaunchedContent(image, title);
      REQUIRE(content);
      REQUIRE(content->content_type == x360n::kArcadeContentType);
      full = content->license_mask != 0;
      INFO("title=" << title << " package license=" << content->license_mask);
      auto* user = &REX_INTERFACE(rex::xam::IXAMTitleServices)->GetUserProfile();
      REQUIRE(user->CreateLocal("Counter", &account) == 0);
      REQUIRE(user->CreateLocal("Other", &other) == 0);
      REQUIRE(user->SetLocalSession(account) == 0);
      REQUIRE(user->RecordTitlePlayed(0x12345678)); // preserve an existing record
      x360n::LaunchRequest request;
      request.kind = x360n::LaunchRequest::Image;
      request.path = image.string();
      const auto handoff = work.dir / "launch.bin";
      REQUIRE(x360n::WriteLaunchRequest(handoff, request));
      REQUIRE(settings.handoff_in.Set(handoff.string()));
      REQUIRE(settings.profile_root.Set(work.dir.string()));
      REQUIRE(settings.dashboard_version.Set(version));
      x360n::ImportLaunchData(rt);
      x360n::ImportLaunchData(rt); // replay must not increment twice
      CHECK(user->TitlesPlayed(account).size() == (full ? 2 : 1));
      CHECK(user->TitlesPlayed(other).empty());
      // Earlier dashboards keep the original behavior with this same license.
      REQUIRE(settings.dashboard_version.Set("2.0.6682.0"));
      x360n::ImportLaunchData(rt);
      CHECK(user->TitlesPlayed(account).size() == 2);
      // Returning to a newer version must never remove an existing Trial record.
      REQUIRE(settings.dashboard_version.Set(version));
      x360n::ImportLaunchData(rt);
      CHECK(user->TitlesPlayed(account).size() == 2);
    }
    rex::xam::UserProfile reopened;
    REQUIRE(reopened.InitializeLocal(work.dir));
    CHECK(reopened.TitlesPlayed(account) == std::vector<uint32_t>{0x12345678, title});
    CHECK(reopened.TitlesPlayed(other).empty());
  }
}
