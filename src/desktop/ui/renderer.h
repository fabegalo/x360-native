#pragma once
// Presents the launcher's ImGui frames. SDL_GPU (Direct3D 12, Vulkan or Metal,
// whatever the system offers) is preferred; the CPU rasterizer with the
// window's own framebuffer is the fallback, so the launcher opens even when no
// GPU device can be created for it, independently of the game renderers.
#include <SDL3/SDL.h>
#include <imgui.h>
#include <memory>
#include <string>

namespace x360n::ui {
class Presenter {
 public:
  virtual ~Presenter() = default;
  virtual void NewFrame() {}
  virtual void Render(ImDrawData* data) = 0;
  virtual void Upload(ImTextureData* texture) = 0;
  virtual std::string Name() const = 0;
};
std::unique_ptr<Presenter> CreateGpuPresenter(SDL_Window* window, std::string& error);
std::unique_ptr<Presenter> CreateSoftwarePresenter(SDL_Window* window, std::string& error);
}  // namespace x360n::ui
