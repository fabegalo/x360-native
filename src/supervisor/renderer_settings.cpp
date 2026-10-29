#include "renderer_settings.h"
#include "child_process.h"
#include "host_platform.h"
#include <toml++/toml.hpp>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#endif
namespace x360n {
namespace {
void Validate(RendererSettings s) {
  if (s.swap_post_effect != "none" && s.swap_post_effect != "fxaa" && s.swap_post_effect != "fxaa_extreme")
    throw std::runtime_error("Invalid Native anti-aliasing setting");
  if ((s.present_effect != "bilinear" && s.present_effect != "cas" && s.present_effect != "fsr") ||
      s.cas_sharpness > 100 || s.fsr_sharpness_reduction > 200)
    throw std::runtime_error("Invalid Native presentation effect/sharpness");
  if (s.framerate < 30 || s.framerate > 240 || s.present_rate > 480 ||
      s.scale_x < 1 || s.scale_x > 4 || s.scale_y < 1 || s.scale_y > 4 ||
      (s.anisotropy != 0 && s.anisotropy != 2 && s.anisotropy != 4 && s.anisotropy != 8 && s.anisotropy != 16))
    throw std::runtime_error("Invalid Native Enhanced timing/quality settings");
  if (s.renderer != Renderer::Xenos && s.renderer != Renderer::Native)
    throw std::runtime_error("Unknown global renderer");
  if (s.api != GraphicsApi::D3D12 && s.api != GraphicsApi::Vulkan)
    throw std::runtime_error("Unknown global graphics API");
#ifndef _WIN32
  if (s.api == GraphicsApi::D3D12)
    throw std::runtime_error("D3D12 is unavailable on this platform; select Vulkan");
#endif
  if (s.api == GraphicsApi::Vulkan && !s.experimental
#ifndef _WIN32
      && s.renderer != Renderer::Xenos
#endif
      )
    throw std::runtime_error("Vulkan is experimental; explicit development access is required");
  if (s.renderer == Renderer::Native && !s.experimental)
    throw std::runtime_error("Native is experimental; current title/gameplay validation has not been completed. Explicit development access is required.");
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
  if (table.contains("enhanced") && !table["enhanced"].is_boolean())
    throw std::runtime_error("Invalid Native Enhanced preference");
  result.enhanced = table["enhanced"].value_or(false);
  if (table.contains("ultrawide") && !table["ultrawide"].is_boolean())
    throw std::runtime_error("Invalid Native ultrawide preference");
  result.ultrawide = table["ultrawide"].value_or(false);
  const auto number = [&](const char* key, uint32_t& value) {
    if (!table.contains(key)) return;
    const auto parsed = table[key].value<int64_t>();
    if (!parsed || *parsed < 0 || *parsed > UINT32_MAX) throw std::runtime_error(std::string("Invalid graphics preference: ") + key);
    value = uint32_t(*parsed);
  };
  number("framerate", result.framerate); number("present_rate", result.present_rate);
  number("scale_x", result.scale_x); number("scale_y", result.scale_y); number("anisotropy", result.anisotropy);
  if (table.contains("present_effect")) {
    const auto effect = table["present_effect"].value<std::string>();
    if (!effect) throw std::runtime_error("Invalid Native presentation effect");
    result.present_effect = *effect;
  }
  number("cas_sharpness", result.cas_sharpness);
  number("fsr_sharpness_reduction", result.fsr_sharpness_reduction);
  if (table.contains("swap_post_effect")) {
    const auto effect = table["swap_post_effect"].value<std::string>();
    if (!effect) throw std::runtime_error("Invalid Native anti-aliasing setting");
    result.swap_post_effect = *effect;
  }
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
        << "\napi = '" << (settings.api == GraphicsApi::Vulkan ? "vulkan" : "d3d12") << "'\n"
        << "enhanced = " << (settings.enhanced ? "true" : "false")
        << "\nframerate = " << settings.framerate << "\npresent_rate = " << settings.present_rate
        << "\nscale_x = " << settings.scale_x << "\nscale_y = " << settings.scale_y
        << "\nanisotropy = " << settings.anisotropy
        << "\npresent_effect = '" << settings.present_effect << "'"
        << "\ncas_sharpness = " << settings.cas_sharpness
        << "\nfsr_sharpness_reduction = " << settings.fsr_sharpness_reduction
        << "\nswap_post_effect = '" << settings.swap_post_effect << "'"
        << "\nultrawide = " << (settings.ultrawide ? "true" : "false") << '\n';
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
    // Session controls are authoritative; manifests cannot activate enhancements
    // behind the user's selection or carry them into the Xenos baseline.
    bool timing_option = false;
    for (const char* name : {"native_enhanced", "native_framerate", "native_present_rate", "native_scale_x", "native_scale_y", "native_anisotropy", "native_ultrawide", "swap_post_effect", "present_effect", "present_cas_additional_sharpness", "present_fsr_sharpness_reduction"}) {
      if (settings.renderer != Renderer::Native && !std::string_view(name).starts_with("native_")) continue;
      std::string option = std::string("--") + name;
      std::string normalized = arg;
      const auto equal = normalized.find('=');
      if (normalized.starts_with("--"))
        std::replace(normalized.begin() + 2, equal == std::string::npos ? normalized.end() : normalized.begin() + equal, '-', '_');
      if (normalized == option) {
        if (++i == args.size() || args[i].starts_with("--")) throw std::runtime_error("Missing manifest Native setting value");
        timing_option = true; break;
      }
      if (normalized.starts_with(option + "=")) { timing_option = true; break; }
    }
    if (timing_option) continue;
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
  const bool enhanced = settings.renderer == Renderer::Native && settings.enhanced;
  if (settings.renderer == Renderer::Native) {
    result.push_back(enhanced ? "--native_enhanced=true" : "--native_enhanced=false");
    result.push_back("--native_framerate=" + std::to_string(enhanced ? settings.framerate : 30));
    result.push_back("--native_present_rate=" + std::to_string(enhanced ? settings.present_rate : 0));
    result.push_back("--native_scale_x=" + std::to_string(enhanced ? settings.scale_x : 1));
    result.push_back("--native_scale_y=" + std::to_string(enhanced ? settings.scale_y : 1));
    result.push_back("--native_anisotropy=" + std::to_string(enhanced ? settings.anisotropy : 0));
    result.push_back("--present_effect=" + (enhanced ? settings.present_effect : std::string("bilinear")));
    result.push_back("--swap_post_effect=" + (enhanced ? settings.swap_post_effect : std::string("none")));
    result.push_back(enhanced && settings.ultrawide ? "--native_ultrawide=true" : "--native_ultrawide=false");
    if (enhanced && settings.present_effect == "cas")
      result.push_back("--present_cas_additional_sharpness=" + std::to_string(settings.cas_sharpness / 100.0));
    if (enhanced && settings.present_effect == "fsr")
      result.push_back("--present_fsr_sharpness_reduction=" + std::to_string(settings.fsr_sharpness_reduction / 100.0));
  }
  return result;
}
void CheckSessionRenderer(const std::filesystem::path& exe, RendererSettings settings) {
  Validate(settings);
  if (settings.renderer == Renderer::Native && settings.api == GraphicsApi::Vulkan)
    throw std::runtime_error("Native/Vulkan is implemented experimentally; launcher access awaits validation with a real title. No fallback to Xenos or D3D12.");
  // The runtime loader is authoritative for the executable's build config and
  // ABI. This preflight only detects an absent plugin before creating a window.
  if (settings.renderer == Renderer::Native &&
      !std::filesystem::is_regular_file(exe.parent_path() / "rexgpu-native.dll") &&
      !std::filesystem::is_regular_file(exe.parent_path() / "rexgpu-natived.dll") &&
      !std::filesystem::is_regular_file(exe.parent_path() / "rexgpu-nativerd.dll"))
    throw std::runtime_error("Native (Experimental) unavailable: rexgpu-native.dll missing beside " +
                             PathUtf8(exe) + "; no fallback to Xenos.");
  if (settings.api == GraphicsApi::Vulkan) {
    const auto probe = exe.parent_path() / ("x360n_gpu_preflight" + ExecutableSuffix());
    if (!std::filesystem::is_regular_file(probe))
      throw std::runtime_error("Vulkan unavailable: x360n_gpu_preflight" + ExecutableSuffix() + " missing beside " + PathUtf8(exe) + "; rebuild/stage this title. No API fallback.");
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
