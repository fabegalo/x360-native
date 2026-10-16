// The avatar service of titles/motocross_madness is fed only from the Retail
// 17559 dashboard profile (titles/dash_17559/preparation.toml [avatar]). Checks
// the repository profiles against the user's own imported files; skipped when
// they are absent. No title or dashboard process is started.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <stdexcept>
#include <string>

#include "session_profile.h"

namespace fs = std::filesystem;
using namespace x360n;

namespace {
fs::path Root() { return fs::path(X360N_SOURCE_ROOT); }
SessionProfile Load(const std::string& title) {
  return LoadSessionProfile(ProjectPaths{Root()}, Root() / "titles" / title / (title + "_manifest.toml"),
                            false);
}
}  // namespace

TEST_CASE("Motocross Madness receives the 17559 dashboard avatar files", "[local][avatar]") {
  const auto dashboard = Load("dash_17559");
  const auto game = Load("motocross_madness");
  REQUIRE(dashboard.avatar_files);
  CHECK(dashboard.avatar_files->version == "2.0.17559.0");
  CHECK(game.avatar_service == "2.0.17559.0");
  // The dashboard's isolated XUI copy is the same original the title was built with.
  CHECK(dashboard.avatar_files->xam.sha256 == game.avatar_xam_sha256);
  CHECK(game.game_root == fs::weakly_canonical(Root() / ".local/inputs/titles/motocross_madness/extracted"));
  for (const auto* file : {&dashboard.avatar_files->xam, &dashboard.avatar_files->catalog})
    if (!fs::exists(file->path)) SKIP("not present: " + file->path.string());
  // A prepared build is launchable by the session with its receipt and content.
  for (const auto* profile : {&dashboard, &game})
    if (fs::exists(profile->exe))
      CHECK_NOTHROW(LoadSessionProfile(ProjectPaths{Root()}, profile->manifest, true));
  const auto args = AvatarServiceArguments(dashboard, game);
  REQUIRE(args.size() == 2);
  CHECK(args[0] == "--x360n_avatar_xam_17559=" + PathUtf8(dashboard.avatar_files->xam.path));
  CHECK(args[1] == "--x360n_avatar_catalog_17559=" + PathUtf8(dashboard.avatar_files->catalog.path));
}

TEST_CASE("Motocross Madness is refused by other dashboard versions", "[local][avatar]") {
  const auto game = Load("motocross_madness");
  // dash_6717 has no session profile yet; take its registered version only.
  auto dashboard = Load("dash_17559");
  dashboard.name = "dash_6717";
  dashboard.dashboard_version = "2.0.6717.0";
  for (const bool registered : {true, false}) {
    // Even with files at hand, another version never supplies this title.
    if (!registered) dashboard.avatar_files.reset();
    try {
      AvatarServiceArguments(dashboard, game);
      FAIL("accepted the avatar service of another dashboard version");
    } catch (const std::runtime_error& e) {
      CAPTURE(e.what());
      CHECK(std::string(e.what()).find("requires the session dashboard 2.0.17559.0; dash_6717 is 2.0.6717.0") !=
            std::string::npos);
    }
  }
  // A session started on the title itself has no dashboard to provide the files.
  CHECK_THROWS(AvatarServiceArguments(game, game));
}
