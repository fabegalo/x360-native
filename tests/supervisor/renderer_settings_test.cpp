#include "renderer_settings.h"
#include "scratch.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <fstream>
#include <algorithm>
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
// The default session API of this host: D3D12 on Windows, Vulkan elsewhere.
const std::string kDefaultBackend = RendererSettings{}.api == GraphicsApi::Vulkan ? "--gpu_backend=vulkan" : "--gpu_backend=d3d12";
TEST_CASE("global renderer overrides manifests and requires development access", "[renderer]") {
  const std::vector<std::string> args{"--gpu_plugin=xenos", "--mnk_mode=true", "--gpu_plugin", "other", "--gpu-plugin=native"};
  CHECK(SessionRendererArguments(args, {}) == std::vector<std::string>{"--mnk_mode=true", "--gpu_plugin=xenos", kDefaultBackend});
  CHECK(SessionRendererArguments(args, {Renderer::Native, true}) == std::vector<std::string>{
      "--mnk_mode=true", "--gpu_plugin=native", kDefaultBackend, "--native_enhanced=false",
      "--native_framerate=30", "--native_present_rate=0", "--native_scale_x=1", "--native_scale_y=1",
      "--native_anisotropy=0", "--present_effect=bilinear", "--swap_post_effect=none", "--native_ultrawide=false"});
  CHECK_THROWS(SessionRendererArguments(args, {Renderer::Native, false}));
  CHECK_THROWS(SessionRendererArguments({"--gpu_plugin"}, {}));
  CHECK_THROWS(SessionRendererArguments({"--"}, {}));
  CHECK(SessionRendererArguments({"=value"}, {}) == std::vector<std::string>{"=value", "--gpu_plugin=xenos", kDefaultBackend});
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
#ifdef _WIN32
  CHECK_NOTHROW(CheckSessionRenderer(root / "game.exe", {}));
#else
  CHECK_THROWS(CheckSessionRenderer(root / "game.exe", {}));  // Vulkan by default: needs the staged preflight
#endif
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
  CHECK(LoadRendererSettings(paths).api == RendererSettings{}.api);
  const std::vector<std::string> args{"--gpu_backend", "any", "--gpu-backend=d3d12", "--gpu_plugin=native", "--mnk_mode=true"};
  CHECK(SessionRendererArguments(args, session) == std::vector<std::string>{"--mnk_mode=true", "--gpu_plugin=xenos", "--gpu_backend=vulkan"});
  CHECK_THROWS(SessionRendererArguments({"--gpu-backend"}, session));
  CHECK_THROWS(SessionRendererArguments({"--gpu_backend", "--foo"}, session));
  CHECK_THROWS(CheckSessionRenderer(root / "game.exe", session)); // Missing preflight.
  selected.renderer = Renderer::Native;
  CHECK_THROWS_WITH(CheckSessionRenderer(root / "game.exe", selected),
      "Native/Vulkan is implemented experimentally; launcher access awaits validation with a real title. No fallback to Xenos or D3D12.");
  selected.experimental = false;
  CHECK_THROWS(SaveRendererSettings(paths, selected));
  std::ofstream(paths.local() / "graphics.toml") << "renderer = 'xenos'\nexperimental = false\n";
  CHECK(LoadRendererSettings(paths).api == RendererSettings{}.api);
  for (const auto* value : {"'any'", "'dx12'", "3", "true"}) {
    std::ofstream(paths.local() / "graphics.toml") << "renderer = 'xenos'\nexperimental = true\napi = " << value << '\n';
    CHECK_THROWS(LoadRendererSettings(paths));
  }
  std::filesystem::remove_all(root);
}

TEST_CASE("Native enhancements persist and cannot leak from manifest into original mode", "[renderer][enhanced]") {
  const auto root = PathFromUtf8(X360N_SOURCE_ROOT) / ".local/tmp" / UniqueTempPath("enhanced").filename();
  ProjectPaths paths{root};
  RendererSettings s{Renderer::Native, true};
  s.enhanced = true; s.framerate = 144; s.present_rate = 120;
  s.scale_x = 3; s.scale_y = 2; s.anisotropy = 16;
  s.present_effect = "fsr"; s.fsr_sharpness_reduction = 38;
  s.swap_post_effect = "fxaa_extreme"; s.ultrawide = true;
  SaveRendererSettings(paths, s);
  const auto loaded = LoadRendererSettings(paths);
  CHECK(loaded.enhanced); CHECK(loaded.framerate == 144); CHECK(loaded.present_rate == 120);
  CHECK(loaded.scale_x == 3); CHECK(loaded.scale_y == 2); CHECK(loaded.anisotropy == 16);
  CHECK(loaded.present_effect == "fsr"); CHECK(loaded.fsr_sharpness_reduction == 38);
  CHECK(loaded.swap_post_effect == "fxaa_extreme"); CHECK(loaded.ultrawide);
  const std::vector<std::string> manifest{"--native-enhanced=true", "--native-framerate", "240", "--native_ultrawide=true", "--present-effect=cas", "--swap-post-effect=fxaa"};
  auto args = SessionRendererArguments(manifest, loaded);
  CHECK(std::count(args.begin(), args.end(), "--native_framerate=144") == 1);
  CHECK(std::count(args.begin(), args.end(), "--present_fsr_sharpness_reduction=0.380000") == 1);
  CHECK(std::count(args.begin(), args.end(), "--swap_post_effect=fxaa_extreme") == 1);
  CHECK(SessionRendererArguments(manifest, {Renderer::Native, true}) == SessionRendererArguments({}, {Renderer::Native, true}));
  CHECK(SessionRendererArguments({"--native-framerate=240", "--native-ultrawide=true"}, {}) == SessionRendererArguments({}, {}));
  s.framerate = 241; CHECK_THROWS(SaveRendererSettings(paths, s));
  s.framerate = 144; s.anisotropy = 3; CHECK_THROWS(SaveRendererSettings(paths, s));
  std::filesystem::remove_all(root);
}
