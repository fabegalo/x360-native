#include "renderer.h"
#include <imgui_impl_sdlgpu3.h>
#include "soft_raster.h"

namespace x360n::ui {
namespace {
class GpuPresenter final : public Presenter {
 public:
  GpuPresenter(SDL_Window* window, SDL_GPUDevice* device) : window_(window), device_(device) {}
  ~GpuPresenter() override {
    SDL_WaitForGPUIdle(device_);
    ImGui_ImplSDLGPU3_Shutdown();
    SDL_ReleaseWindowFromGPUDevice(device_, window_);
    SDL_DestroyGPUDevice(device_);
  }
  void NewFrame() override { ImGui_ImplSDLGPU3_NewFrame(); }
  void Upload(ImTextureData* texture) override { ImGui_ImplSDLGPU3_UpdateTexture(texture); }
  std::string Name() const override { return std::string("SDL_GPU ") + SDL_GetGPUDeviceDriver(device_); }
  void Render(ImDrawData* data) override {
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device_);
    if (!commands) return;
    SDL_GPUTexture* swapchain = nullptr;
    const bool minimized = data->DisplaySize.x <= 0 || data->DisplaySize.y <= 0;
    if (SDL_WaitAndAcquireGPUSwapchainTexture(commands, window_, &swapchain, nullptr, nullptr) && swapchain && !minimized) {
      ImGui_ImplSDLGPU3_PrepareDrawData(data, commands);
      SDL_GPUColorTargetInfo target{};
      target.texture = swapchain;
      target.clear_color = SDL_FColor{13 / 255.0f, 15 / 255.0f, 18 / 255.0f, 1.0f};
      target.load_op = SDL_GPU_LOADOP_CLEAR;
      target.store_op = SDL_GPU_STOREOP_STORE;
      SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
      ImGui_ImplSDLGPU3_RenderDrawData(data, commands, pass);
      SDL_EndGPURenderPass(pass);
    }
    SDL_SubmitGPUCommandBuffer(commands);
  }
 private:
  SDL_Window* window_;
  SDL_GPUDevice* device_;
};
class SoftwarePresenter final : public Presenter {
 public:
  explicit SoftwarePresenter(SDL_Window* window) : window_(window) {}
  void Upload(ImTextureData* texture) override { UpdateSoftTexture(texture); }
  std::string Name() const override { return "software"; }
  void Render(ImDrawData* data) override {
    SDL_Surface* surface = SDL_GetWindowSurface(window_);
    if (!surface) return;
    const int w = int(data->DisplaySize.x * data->FramebufferScale.x), h = int(data->DisplaySize.y * data->FramebufferScale.y);
    if (w <= 0 || h <= 0) return;
    if (canvas_.width != w || canvas_.height != h) canvas_.Resize(w, h);
    std::fill(canvas_.pixels.begin(), canvas_.pixels.end(), IM_COL32(13, 15, 18, 255));
    Rasterize(canvas_, data);
    SDL_Surface* frame = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ABGR8888, canvas_.pixels.data(), w * 4);
    if (!frame) return;
    SDL_BlitSurface(frame, nullptr, surface, nullptr);
    SDL_DestroySurface(frame);
    SDL_UpdateWindowSurface(window_);
  }
 private:
  SDL_Window* window_;
  Canvas canvas_;
};
}  // namespace
std::unique_ptr<Presenter> CreateGpuPresenter(SDL_Window* window, std::string& error) {
  SDL_GPUDevice* device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXBC | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_METALLIB, false, nullptr);
  if (!device) { error = SDL_GetError(); return nullptr; }
  if (!SDL_ClaimWindowForGPUDevice(device, window)) { error = SDL_GetError(); SDL_DestroyGPUDevice(device); return nullptr; }
  SDL_SetGPUSwapchainParameters(device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);
  ImGui_ImplSDLGPU3_InitInfo info{};
  info.Device = device;
  info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(device, window);
  info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
  if (!ImGui_ImplSDLGPU3_Init(&info)) {
    error = "ImGui SDL_GPU backend";
    SDL_ReleaseWindowFromGPUDevice(device, window); SDL_DestroyGPUDevice(device); return nullptr;
  }
  return std::make_unique<GpuPresenter>(window, device);
}
std::unique_ptr<Presenter> CreateSoftwarePresenter(SDL_Window* window, std::string& error) {
  if (!SDL_GetWindowSurface(window)) { error = SDL_GetError(); return nullptr; }
  auto& io = ImGui::GetIO();
  io.BackendRendererName = "x360n_software";
  io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
  return std::make_unique<SoftwarePresenter>(window);
}
}  // namespace x360n::ui
