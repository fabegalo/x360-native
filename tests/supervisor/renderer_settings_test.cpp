#include "renderer_settings.h"
#include "scratch.h"
#include <catch2/catch_test_macros.hpp>
#include <fstream>
using namespace x360n;
TEST_CASE("global renderer overrides manifests and requires development access", "[renderer]") {
  const std::vector<std::string> args{"--gpu_plugin=xenos", "--mnk_mode=true", "--gpu_plugin", "other", "--gpu-plugin=native"};
  CHECK(SessionRendererArguments(args, {}) == std::vector<std::string>{"--mnk_mode=true", "--gpu_plugin=xenos"});
  CHECK(SessionRendererArguments(args, {Renderer::Native, true}) == std::vector<std::string>{"--mnk_mode=true", "--gpu_plugin=native"});
  CHECK_THROWS(SessionRendererArguments(args, {Renderer::Native, false}));
  CHECK_THROWS(SessionRendererArguments({"--gpu_plugin"}, {}));
  CHECK_THROWS(SessionRendererArguments({"--"}, {}));
}
TEST_CASE("global renderer persists and session snapshot does not change", "[renderer]") {
  const auto root = PathFromUtf8(X360N_SOURCE_ROOT) / ".local/tmp" / UniqueTempPath("renderer").filename();
  ProjectPaths paths{root};
  CHECK(LoadRendererSettings(paths).renderer == Renderer::Xenos);
  SaveRendererSettings(paths, {Renderer::Native, true});
  const auto session = LoadRendererSettings(paths);
  CHECK(session.renderer == Renderer::Native); CHECK(session.experimental);
  SaveRendererSettings(paths, {});
  CHECK(LoadRendererSettings(paths).renderer == Renderer::Xenos);
  CHECK(SessionRendererArguments({}, session).back() == "--gpu_plugin=native");
  CHECK_THROWS(CheckSessionRenderer(root / "game.exe", session));
  CHECK_NOTHROW(CheckSessionRenderer(root / "game.exe", {}));
  std::ofstream(paths.local() / "graphics.toml") << "renderer = 'typo'\nexperimental = true\n";
  CHECK_THROWS(LoadRendererSettings(paths));
  std::filesystem::remove_all(root);
}
