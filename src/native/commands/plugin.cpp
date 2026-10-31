#include "command_processor.h"
#include <rex/graphics/graphics_system.h>
#include <rex/chrono/frame_timing.h>
#include <rex/system/gpu_plugin.h>
#if X360N_NATIVE_D3D12
#include <rex/ui/d3d12/d3d12_provider.h>
#endif
#if X360N_NATIVE_VULKAN
#include "../vulkan/vulkan_backend.h"
#include <rex/ui/vulkan/provider.h>
#endif
#include <rex/logging.h>
#include <string_view>
namespace x360n::native {
class GraphicsSystem final : public rex::graphics::GraphicsSystem {
 public:
  explicit GraphicsSystem(bool vulkan) : vulkan_(vulkan) { rex::chrono::FrameTiming::ActivateNative(); }
  std::string name() const override { return vulkan_ ? "Native (Experimental) Vulkan" : "Native (Experimental) D3D12"; }
 protected:
  void CreateProvider(bool with_presentation) override {
#if X360N_NATIVE_VULKAN
    if (vulkan_) {
      provider_ = rex::ui::vulkan::VulkanProvider::Create(true, with_presentation);
      return;
    }
#endif
#if X360N_NATIVE_D3D12
    provider_ = rex::ui::d3d12::D3D12Provider::Create();
#endif
  }
  std::unique_ptr<rex::graphics::CommandProcessor> CreateCommandProcessor() override {
#if X360N_NATIVE_VULKAN
    if (vulkan_) return std::make_unique<CommandProcessor>(this, kernel_state_, &CreateVulkanScanout);
#endif
#if X360N_NATIVE_D3D12
    return std::make_unique<CommandProcessor>(this, kernel_state_, &CreateD3D12Scanout);
#else
    return nullptr;
#endif
  }
 private:
  bool vulkan_;
};
}
extern "C" REX_GPU_PLUGIN_EXPORT uint32_t rex_gpu_abi_version() {
  return rex::system::kGpuPluginAbiVersion;
}
extern "C" REX_GPU_PLUGIN_EXPORT rex::system::IGraphicsSystem* rex_gpu_create(
    uint32_t abi, const rex::system::GpuCreateInfo* info) {
  if (abi != rex::system::kGpuPluginAbiVersion || !info || info->struct_size < sizeof(*info)) {
    REXLOG_ERROR("Native: invalid graphics plugin ABI/create info"); return nullptr;
  }
  const std::string_view backend = info->backend ? info->backend : "any";
#if X360N_NATIVE_VULKAN
  if (backend == "vulkan") return new x360n::native::GraphicsSystem(true);
#endif
#if !X360N_NATIVE_D3D12
  // Hosts without D3D12 (Linux, macOS) have only the Vulkan backend.
  if (backend == "any") return new x360n::native::GraphicsSystem(true);
  REXLOG_ERROR("Native: backend '{}' is unsupported; Vulkan required", backend); return nullptr;
#else
  if (backend != "any" && backend != "d3d12") {
    REXLOG_ERROR("Native: backend '{}' is unsupported; D3D12 required", backend); return nullptr;
  }
  return new x360n::native::GraphicsSystem(false);
#endif
}
