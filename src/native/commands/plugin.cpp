#include "command_processor.h"
#include "../quality/settings.h"
#include <rex/chrono/frame_timing.h>
#include <rex/driver_context.h>
#include <rex/driver_module.h>
#include <rex/driver_registration.h>
#include <rex/gpu/graphics_system.h>
#include <gpu/xenos/xenos_graphics_driver_base.h>
#if X360N_NATIVE_D3D12
#include <gpu/xenos/host/d3d12/d3d12_provider.h>
#include <gpu/xenos/host/d3d12/d3d12_settings.h>
#endif
#if X360N_NATIVE_VULKAN
#include "../vulkan/vulkan_backend.h"
#include <gpu/xenos/host/vulkan/vulkan_provider.h>
#include <gpu/xenos/host/vulkan/vulkan_settings.h>
#endif

namespace x360n::native {
class GraphicsDriver final : public rex::gpu::XenosGraphicsDriverBase {
 public:
  explicit GraphicsDriver(bool vulkan) : vulkan_(vulkan) {}
  ~GraphicsDriver() override { Shutdown(); }
  std::string_view GetName() const override { return vulkan_ ? "native-vulkan" : "native-d3d12"; }
  rex::Result<void> Initialize(rex::IDriverContext& context) override {
    if (registry_) return rex::Ok();
    REX_TRY_VOID(XenosGraphicsDriverBase::Initialize(context));
#if X360N_NATIVE_VULKAN
    if (vulkan_) api_settings_ = &vulkan_settings_;
#endif
#if X360N_NATIVE_D3D12
    if (!vulkan_) api_settings_ = &d3d12_settings_;
#endif
    if (!api_settings_) {
      XenosGraphicsDriverBase::Shutdown();
      return rex::Err(rex::ErrorCategory::kConfig, "requested Native backend is not compiled");
    }
    auto& registry = context.GetSettingsRegistry();
    if (auto registered = registry.Register(*api_settings_); !registered) {
      XenosGraphicsDriverBase::Shutdown(); return registered;
    }
    registry_ = &registry;
    // Native's scaled texture coordinates remain guest texels, including GPU encoders.
    if (auto locked = draw_resolution_scaled_texture_offsets.Lock(false); !locked) {
      Shutdown(); return locked;
    }
    return rex::Ok();
  }
  void Shutdown() override {
    XenosGraphicsDriverBase::Shutdown();
    if (registry_) registry_->Unregister(*api_settings_);
    registry_ = nullptr; api_settings_ = nullptr;
  }
  rex::Result<void> AttachTitle(const rex::TitleInfo& info, rex::IPhysicalMemory& memory,
      rex::IPhysicalWriteWatch& watch, rex::IGuestInterrupts& interrupts) override {
    // Unload resets the shared timeline; every title reactivates its selected Native path.
    rex::chrono::FrameTiming::ActivateNative();
    auto attached = XenosGraphicsDriverBase::AttachTitle(info, memory, watch, interrupts);
    if (!attached) rex::chrono::FrameTiming::Reset();
    return attached;
  }
 protected:
  void CreateProvider(bool with_presentation) override {
#if X360N_NATIVE_VULKAN
    if (vulkan_) {
      provider_ = rex::gpu::vulkan::VulkanProvider::Create(true, with_presentation,
          vulkan_settings_.device.Get(), vulkan_settings_);
      return;
    }
#endif
#if X360N_NATIVE_D3D12
    provider_ = rex::gpu::d3d12::D3D12Provider::Create(d3d12_settings_.adapter.Get(), d3d12_settings_);
#endif
  }
  std::unique_ptr<rex::gpu::CommandProcessor> CreateCommandProcessor() override {
#if X360N_NATIVE_VULKAN
    if (vulkan_) return std::make_unique<CommandProcessor>(this, title_info_, *physical_memory_,
        *write_watch_, *guest_interrupts_, &CreateVulkanScanout);
#endif
#if X360N_NATIVE_D3D12
    return std::make_unique<CommandProcessor>(this, title_info_, *physical_memory_,
        *write_watch_, *guest_interrupts_, &CreateD3D12Scanout);
#else
    return nullptr;
#endif
  }
 private:
  bool vulkan_;
  rex::ISettingsRegistry* registry_ = nullptr;
  rex::SettingOwner* api_settings_ = nullptr;
#if X360N_NATIVE_D3D12
  rex::gpu::d3d12::D3D12Settings d3d12_settings_;
#endif
#if X360N_NATIVE_VULKAN
  rex::gpu::vulkan::VulkanSettings vulkan_settings_;
#endif
};
namespace {
#if X360N_NATIVE_D3D12
std::unique_ptr<rex::IDriver> CreateD3D12() { return std::make_unique<GraphicsDriver>(false); }
#endif
#if X360N_NATIVE_VULKAN
std::unique_ptr<rex::IDriver> CreateVulkan() { return std::make_unique<GraphicsDriver>(true); }
#endif
constexpr rex::DriverRegistration kDrivers[] = {
#if X360N_NATIVE_D3D12
  {rex::DriverType::kGraphics, "native-d3d12", rex::GraphicsFamily::kXenos, &CreateD3D12},
#endif
#if X360N_NATIVE_VULKAN
  {rex::DriverType::kGraphics, "native-vulkan", rex::GraphicsFamily::kXenos, &CreateVulkan},
#endif
};
}
class NativeProvider final : public rex::IModule, public rex::IDriverModule {
 public:
  REX_MODULE_SERVES(::rex::IDriverModule)
  std::string_view GetName() const override { return "rexgpu-native"; }
  std::span<const rex::InterfaceType> GetRequiredInterfaces() const override {
    static constexpr rex::InterfaceType required[] = {
        rex::gpu::IGraphicsSystem::kInterface, rex::ISettingsRegistry::kInterface};
    return required;
  }
  rex::Result<void> Connect(rex::IInterfaceFactory& factory) override {
    auto& graphics = *factory.Get<rex::gpu::IGraphicsSystem>();
    registry_ = factory.Get<rex::ISettingsRegistry>();
    REX_TRY_VOID(registry_->Register(GetNativeSettings()));
    AddNativeSettingAliases(*registry_);
    (void)rex::chrono::GetNativeFrameSettings();
    rex::gpu::XenosGraphicsDriverBase::SetGraphicsSettings(&graphics.GetSettings());
    return rex::Ok();
  }
  void Disconnect() override {
    if (registry_) registry_->Unregister(GetNativeSettings());
    registry_ = nullptr;
    rex::gpu::XenosGraphicsDriverBase::SetGraphicsSettings(nullptr);
  }
  rex::Result<void> Init() override { return rex::Ok(); }
  void Shutdown() override {}
  rex::IInterface* QueryInterface(std::string_view name, rex::u32 version) override {
    return rex::FindModuleInterface<rex::IDriverModule>(*this, name, version);
  }
  std::span<const rex::DriverRegistration> GetDrivers() const override { return kDrivers; }
 private:
  rex::ISettingsRegistry* registry_ = nullptr;
};
}
REX_DECLARE_MODULE(x360n::native::NativeProvider)
