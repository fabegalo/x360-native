#include "renderer_settings.h"
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
        << "'\nexperimental = " << (settings.experimental ? "true" : "false") << '\n';
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
    if (arg == "--gpu_plugin" || arg == "--gpu-plugin") {
      if (++i == args.size() || args[i].starts_with("--"))
        throw std::runtime_error("Missing manifest gpu_plugin value");
      continue;
    }
    if (arg.starts_with("--gpu_plugin=") || arg.starts_with("--gpu-plugin=")) continue;
    result.push_back(arg);
  }
  result.push_back(settings.renderer == Renderer::Native ? "--gpu_plugin=native" : "--gpu_plugin=xenos");
  return result;
}
void CheckSessionRenderer(const std::filesystem::path& exe, RendererSettings settings) {
  Validate(settings);
  // The runtime loader is authoritative for the executable's build config and
  // ABI. This preflight only detects an absent plugin before creating a window.
  if (settings.renderer == Renderer::Native &&
      !std::filesystem::is_regular_file(exe.parent_path() / "rexgpu-native.dll") &&
      !std::filesystem::is_regular_file(exe.parent_path() / "rexgpu-natived.dll") &&
      !std::filesystem::is_regular_file(exe.parent_path() / "rexgpu-nativerd.dll"))
    throw std::runtime_error("Native (Experimental) unavailable: rexgpu-native.dll missing beside " +
                             PathUtf8(exe) + "; no fallback to Xenos.");
}
}
