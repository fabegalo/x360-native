#pragma once
#include "paths.h"
#include <string>
#include <cstdint>
#include <vector>
namespace x360n {
enum class Renderer { Xenos, Native };
enum class GraphicsApi { D3D12, Vulkan };
struct RendererSettings {
  Renderer renderer = Renderer::Xenos;
  bool experimental = false;
  bool fullscreen = false;
  GraphicsApi api = GraphicsApi::D3D12;
  bool enhanced = false;
  uint32_t framerate = 30;
  uint32_t present_rate = 0;
  uint32_t scale_x = 1, scale_y = 1;
  uint32_t anisotropy = 0;
  std::string present_effect = "bilinear";
  uint32_t cas_sharpness = 0;  // percent, SDK range [0,1]
  uint32_t fsr_sharpness_reduction = 20;  // hundredths of a stop, SDK [0,2]
  std::string swap_post_effect = "none";
  bool ultrawide = false;
};
RendererSettings LoadRendererSettings(const ProjectPaths& paths);
void SaveRendererSettings(const ProjectPaths& paths, RendererSettings settings);
std::vector<std::string> SessionRendererArguments(const std::vector<std::string>& manifest_args,
                                                RendererSettings settings);
void CheckSessionRenderer(const std::filesystem::path& executable, RendererSettings settings);
}
