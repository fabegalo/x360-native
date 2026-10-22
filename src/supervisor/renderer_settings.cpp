#include "renderer_settings.h"
#include "child_process.h"
#include <toml++/toml.hpp>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif
namespace x360n {
namespace {
void Validate(RendererSettings s) {
  if (s.renderer != Renderer::Xenos && s.renderer != Renderer::Native)
    throw std::runtime_error("Unknown global renderer");
  if (s.api != GraphicsApi::D3D12 && s.api != GraphicsApi::Vulkan)
    throw std::runtime_error("Unknown global graphics API");
  if (s.api == GraphicsApi::Vulkan && !s.experimental)
    throw std::runtime_error("Vulkan is experimental; explicit development access is required");
  if (s.renderer == Renderer::Native && !s.experimental)
    throw std::runtime_error("Native unavailable for normal use: dashboard and title transitions are not supported. Explicit development access is required.");
}
}
RendererSettings LoadRendererSettings(const ProjectPaths& paths) {
  const auto file = paths.local() / "graphics.toml";
  if (!std::filesystem::exists(file)) return {};
  const auto table = toml::parse_file(PathUtf8(file));
  const auto renderer = table["renderer"].value<std::string>();
  const auto experimental = table["experimental"].value<bool>();
  if (!renderer || !experimental || (*renderer != "xenos" && *renderer != "native"))
    throw std::runtime_error("Invalid global graphics settings: " + PathUtf8(file));
  RendererSettings result{*renderer == "native" ? Renderer::Native : Renderer::Xenos, *experimental};
  if (table.contains("fullscreen") && !table["fullscreen"].is_boolean())
    throw std::runtime_error("Invalid fullscreen preference: " + PathUtf8(file));
  result.fullscreen = table["fullscreen"].value_or(false);
  if (table.contains("api")) {
    const auto api = table["api"].value<std::string>();
    if (!api || (*api != "d3d12" && *api != "vulkan"))
      throw std::runtime_error("Invalid global graphics API: " + PathUtf8(file));
    result.api = *api == "vulkan" ? GraphicsApi::Vulkan : GraphicsApi::D3D12;
  }
  Validate(result); return result;
}
void SaveRendererSettings(const ProjectPaths& paths, RendererSettings settings) {
  Validate(settings);
  const auto file = paths.local() / "graphics.toml";
  const auto temp = paths.local() / "graphics.toml.tmp";
  std::filesystem::create_directories(file.parent_path());
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out << "renderer = '" << (settings.renderer == Renderer::Native ? "native" : "xenos")
        << "'\nexperimental = " << (settings.experimental ? "true" : "false")
        << "\nfullscreen = " << (settings.fullscreen ? "true" : "false")
        << "\napi = '" << (settings.api == GraphicsApi::Vulkan ? "vulkan" : "d3d12") << "'\n";
    out.close();
    if (!out) throw std::runtime_error("Cannot persist global graphics settings");
  }
#ifdef _WIN32
  if (!MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    throw std::runtime_error("Cannot replace global graphics settings: Windows error " + std::to_string(GetLastError()));
#else
  std::filesystem::rename(temp, file);
#endif
}
std::vector<std::string> SessionRendererArguments(const std::vector<std::string>& args,
                                                RendererSettings settings) {
  Validate(settings);
  std::vector<std::string> result;
  for (size_t i = 0; i < args.size(); ++i) {
    const auto& arg = args[i];
    if (arg == "--") throw std::runtime_error("Session manifests cannot terminate option parsing");
    if (arg == "--gpu_plugin" || arg == "--gpu-plugin" ||
        arg == "--gpu_backend" || arg == "--gpu-backend") {
      if (++i == args.size() || args[i].starts_with("--"))
        throw std::runtime_error("Missing manifest GPU selection value");
      continue;
    }
    if (arg.starts_with("--gpu_plugin=") || arg.starts_with("--gpu-plugin=")) continue;
    if (arg.starts_with("--gpu_backend=") || arg.starts_with("--gpu-backend=")) continue;
    result.push_back(arg);
  }
  result.push_back(settings.renderer == Renderer::Native ? "--gpu_plugin=native" : "--gpu_plugin=xenos");
  result.push_back(settings.api == GraphicsApi::Vulkan ? "--gpu_backend=vulkan" : "--gpu_backend=d3d12");
  return result;
}
void CheckSessionRenderer(const std::filesystem::path& exe, RendererSettings settings) {
  Validate(settings);
  if (settings.renderer == Renderer::Native && settings.api == GraphicsApi::Vulkan)
    throw std::runtime_error("Native/Vulkan unavailable: the independent draw/EDRAM executor is incomplete; no fallback to Xenos or D3D12.");
  // The runtime loader is authoritative for the executable's build config and
  // ABI. This preflight only detects an absent plugin before creating a window.
  if (settings.renderer == Renderer::Native &&
      !std::filesystem::is_regular_file(exe.parent_path() / "rexgpu-native.dll") &&
      !std::filesystem::is_regular_file(exe.parent_path() / "rexgpu-natived.dll") &&
      !std::filesystem::is_regular_file(exe.parent_path() / "rexgpu-nativerd.dll"))
    throw std::runtime_error("Native (Experimental) unavailable: rexgpu-native.dll missing beside " +
                             PathUtf8(exe) + "; no fallback to Xenos.");
  if (settings.api == GraphicsApi::Vulkan) {
    const auto probe = exe.parent_path() / "x360n_gpu_preflight.exe";
    if (!std::filesystem::is_regular_file(probe))
      throw std::runtime_error("Vulkan unavailable: x360n_gpu_preflight.exe missing beside " + PathUtf8(exe) + "; rebuild/stage this title. No API fallback.");
    const auto log = std::filesystem::temp_directory_path() /
        ("x360n-gpu-preflight-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".log");
    ProcessSpec spec;
    spec.exe = probe; spec.args = {"xenos", "vulkan"}; spec.working_dir = exe.parent_path();
    spec.stdout_file = log; spec.stderr_file = log.string() + ".err";
    spec.timeout = std::chrono::seconds(20); spec.no_console = true;
    const auto result = RunProcess(spec);
    std::ifstream in(spec.stderr_file);
    std::string reason{std::istreambuf_iterator<char>(in), {}};
    std::error_code ignored;
    std::filesystem::remove(spec.stdout_file, ignored);
    std::filesystem::remove(spec.stderr_file, ignored);
    if (!result.launched || result.timed_out || result.exit_code != 0)
      throw std::runtime_error("Vulkan preflight failed (no fallback): " +
          (result.timed_out ? "driver probe exceeded 20 seconds" :
           !result.launched ? result.launch_error : reason.empty() ? DescribeExitCode(result.exit_code) : reason));
  }
}
}
