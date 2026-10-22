#pragma once
#include "paths.h"
#include <string>
#include <vector>
namespace x360n {
enum class Renderer { Xenos, Native };
enum class GraphicsApi { D3D12, Vulkan };
struct RendererSettings {
  Renderer renderer = Renderer::Xenos;
  bool experimental = false;
  bool fullscreen = false;
  GraphicsApi api = GraphicsApi::D3D12;
};
RendererSettings LoadRendererSettings(const ProjectPaths& paths);
void SaveRendererSettings(const ProjectPaths& paths, RendererSettings settings);
std::vector<std::string> SessionRendererArguments(const std::vector<std::string>& manifest_args,
                                                RendererSettings settings);
void CheckSessionRenderer(const std::filesystem::path& executable, RendererSettings settings);
}
