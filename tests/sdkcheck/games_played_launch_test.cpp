#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <chrono>
#include <rex/cvar.h>
#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/system/user_module.h>
#include <toml++/toml.hpp>
#include "../../titles/common/games_played.h"
#include "../../titles/common/launch_handoff.h"
#include "../../titles/common/launch_request.h"

REXCVAR_DECLARE(std::string, x360n_handoff_in);
REXCVAR_DECLARE(std::string, x360n_profile_root);
REXCVAR_DECLARE(std::string, x360n_dashboard_version);
REXCVAR_DECLARE(uint32_t, license_mask);

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
  REQUIRE(REXCVAR_GET(license_mask) == 0); // never force Full for this validation
  struct Reset {
    std::string input = REXCVAR_GET(x360n_handoff_in);
    std::string profile = REXCVAR_GET(x360n_profile_root);
    std::string version = REXCVAR_GET(x360n_dashboard_version);
    ~Reset() {
      REXCVAR_SET(x360n_handoff_in, input);
      REXCVAR_SET(x360n_profile_root, profile);
      REXCVAR_SET(x360n_dashboard_version, version);
    }
  } reset;
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
      rex::Runtime rt(image.parent_path(), work.dir);
      rex::RuntimeConfig config{};
      config.tool_mode = true;
      config.kernel_init = rex::kernel::InitializeKernel;
      REQUIRE(rt.Setup(std::move(config)) == 0);
      // Load original image metadata only; no guest execution, window or GPU.
      REQUIRE(rt.LoadXexImage("game:\\" + image.filename().string()) == 0);
      title = rt.kernel_state()->title_id();
      const auto content = x360n::ReadLaunchedContent(image, title);
      REQUIRE(content);
      REQUIRE(content->content_type == x360n::kArcadeContentType);
      full = content->license_mask != 0;
      INFO("title=" << title << " package license=" << content->license_mask);
      auto* user = rt.kernel_state()->user_profile();
      REQUIRE(user->CreateLocal("Counter", &account) == 0);
      REQUIRE(user->CreateLocal("Other", &other) == 0);
      REQUIRE(user->SetLocalSession(account) == 0);
      REQUIRE(user->RecordTitlePlayed(0x12345678)); // preserve an existing record
      x360n::LaunchRequest request;
      request.kind = x360n::LaunchRequest::Image;
      request.path = image.string();
      const auto handoff = work.dir / "launch.bin";
      REQUIRE(x360n::WriteLaunchRequest(handoff, request));
      REXCVAR_SET(x360n_handoff_in, handoff.string());
      REXCVAR_SET(x360n_profile_root, work.dir.string());
      REXCVAR_SET(x360n_dashboard_version, version);
      x360n::ImportLaunchData(rt);
      x360n::ImportLaunchData(rt); // replay must not increment twice
      CHECK(user->TitlesPlayed(account).size() == (full ? 2 : 1));
      CHECK(user->TitlesPlayed(other).empty());
      // Earlier dashboards keep the original behavior with this same license.
      REXCVAR_SET(x360n_dashboard_version, std::string("2.0.6682.0"));
      x360n::ImportLaunchData(rt);
      CHECK(user->TitlesPlayed(account).size() == 2);
      // Returning to a newer version must never remove an existing Trial record.
      REXCVAR_SET(x360n_dashboard_version, version);
      x360n::ImportLaunchData(rt);
      CHECK(user->TitlesPlayed(account).size() == 2);
    }
    rex::system::xam::UserProfile reopened;
    REQUIRE(reopened.InitializeLocal(work.dir));
    CHECK(reopened.TitlesPlayed(account) == std::vector<uint32_t>{0x12345678, title});
    CHECK(reopened.TitlesPlayed(other).empty());
  }
}
