#include "renderer_settings.h"
#include "scratch.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <fstream>
using namespace x360n;
TEST_CASE("fullscreen preference persists independently and old settings remain windowed", "[renderer][fullscreen]") {
  const auto root = PathFromUtf8(X360N_SOURCE_ROOT) / ".local/tmp" / UniqueTempPath("fullscreen").filename();
  ProjectPaths paths{root};
  CHECK_FALSE(LoadRendererSettings(paths).fullscreen);
  SaveRendererSettings(paths, {Renderer::Xenos, false, true});
  const auto snapshot = LoadRendererSettings(paths);
  CHECK(snapshot.fullscreen);
  CHECK(snapshot.renderer == Renderer::Xenos);
  SaveRendererSettings(paths, {});
  CHECK_FALSE(LoadRendererSettings(paths).fullscreen);
  CHECK(snapshot.fullscreen);
  std::ofstream(paths.local() / "graphics.toml") << "renderer = 'xenos'\nexperimental = false\n";
  CHECK_FALSE(LoadRendererSettings(paths).fullscreen);
  std::ofstream(paths.local() / "graphics.toml") << "renderer = 'xenos'\nexperimental = false\nfullscreen = 'true'\n";
  CHECK_THROWS(LoadRendererSettings(paths));
  std::filesystem::remove_all(root);
}
TEST_CASE("global renderer overrides manifests and requires development access", "[renderer]") {
  const std::vector<std::string> args{"--gpu_plugin=xenos", "--mnk_mode=true", "--gpu_plugin", "other", "--gpu-plugin=native"};
  CHECK(SessionRendererArguments(args, {}) == std::vector<std::string>{"--mnk_mode=true", "--gpu_plugin=xenos", "--gpu_backend=d3d12"});
  CHECK(SessionRendererArguments(args, {Renderer::Native, true}) == std::vector<std::string>{"--mnk_mode=true", "--gpu_plugin=native", "--gpu_backend=d3d12"});
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
  CHECK(SessionRendererArguments({}, session).at(0) == "--gpu_plugin=native");
  CHECK_THROWS(CheckSessionRenderer(root / "game.exe", session));
  CHECK_NOTHROW(CheckSessionRenderer(root / "game.exe", {}));
  std::ofstream(paths.local() / "graphics.toml") << "renderer = 'typo'\nexperimental = true\n";
  CHECK_THROWS(LoadRendererSettings(paths));
  std::filesystem::remove_all(root);
}
TEST_CASE("graphics API migrates old files and strips every conflicting manifest override", "[renderer][gpu-backend]") {
  const auto root = PathFromUtf8(X360N_SOURCE_ROOT) / ".local/tmp" / UniqueTempPath("api").filename();
  ProjectPaths paths{root};
  RendererSettings selected{Renderer::Xenos, true, true, GraphicsApi::Vulkan};
  SaveRendererSettings(paths, selected);
  const auto session = LoadRendererSettings(paths);
  REQUIRE(session.api == GraphicsApi::Vulkan);
  SaveRendererSettings(paths, {});
  CHECK(session.api == GraphicsApi::Vulkan);
  CHECK(LoadRendererSettings(paths).api == GraphicsApi::D3D12);
  const std::vector<std::string> args{"--gpu_backend", "any", "--gpu-backend=d3d12", "--gpu_plugin=native", "--mnk_mode=true"};
  CHECK(SessionRendererArguments(args, session) == std::vector<std::string>{"--mnk_mode=true", "--gpu_plugin=xenos", "--gpu_backend=vulkan"});
  CHECK_THROWS(SessionRendererArguments({"--gpu-backend"}, session));
  CHECK_THROWS(SessionRendererArguments({"--gpu_backend", "--foo"}, session));
  CHECK_THROWS(CheckSessionRenderer(root / "game.exe", session)); // Missing preflight.
  selected.renderer = Renderer::Native;
  CHECK_THROWS_WITH(CheckSessionRenderer(root / "game.exe", selected),
      "Native/Vulkan unavailable: the independent draw/EDRAM executor is incomplete; no fallback to Xenos or D3D12.");
  selected.experimental = false;
  CHECK_THROWS(SaveRendererSettings(paths, selected));
  std::ofstream(paths.local() / "graphics.toml") << "renderer = 'xenos'\nexperimental = false\n";
  CHECK(LoadRendererSettings(paths).api == GraphicsApi::D3D12);
  for (const auto* value : {"'any'", "'dx12'", "3", "true"}) {
    std::ofstream(paths.local() / "graphics.toml") << "renderer = 'xenos'\nexperimental = true\napi = " << value << '\n';
    CHECK_THROWS(LoadRendererSettings(paths));
  }
  std::filesystem::remove_all(root);
}
