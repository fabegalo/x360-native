#include "vulkan_backend.h"
#include "vulkan_render.h"
#include "vulkan_scanout.h"
#include <stdexcept>

namespace x360n::native {
class VulkanBackend final : public ScanoutBackend {
 public:
  explicit VulkanBackend(rex::ui::vulkan::VulkanProvider& provider)
      : provider_(provider), scanout_(provider) {
    const auto& properties = provider.vulkan_device()->properties();
    if (!properties.fragmentShaderSampleInterlock || !properties.fragmentStoresAndAtomics)
      throw std::runtime_error("Native Vulkan: EDRAM requires fragment sample interlock and fragment storage writes");
  }
  void Present(rex::ui::Presenter& presenter, const ScanoutTexture& texture, const void* pixels,
      const uint32_t* lut, const ScanoutOverlay* overlay, const void* overlay_pixels) override {
    if (render_) {
      render_->CompleteGuestWrites(kGuestSyncPresent, 0xFF);
      render_->Flush(false);
      render_->EndFrame();
    }
    scanout_.Present(presenter, texture, pixels, lut, overlay, overlay_pixels);
  }
  void Drain() override {
    if (render_) render_->Flush(true);
    scanout_.Drain();
  }
  void InitializeShaderStorage(const std::filesystem::path& root, uint32_t title) override {
    pipeline_directory_ = root / "native-vulkan-pso-v1" / std::to_string(title);
    if (render_) render_->SetPipelineDirectory(pipeline_directory_);
  }
  void CompleteGuestWrites(GuestSync sync, uint32_t opcode) override {
    if (render_) render_->CompleteGuestWrites(sync, opcode);
  }
  void SyncGuestRead(uint32_t address, uint32_t length, uint32_t opcode) override {
    if (render_) render_->SyncGuestRead(address, length, opcode);
  }
  rex::graphics::Shader* LoadShader(rex::graphics::xenos::ShaderType type,
      const uint32_t* data, uint32_t count) override {
    return Render().LoadShader(type, data, count);
  }
  void Draw(const rex::graphics::RegisterFile& regs, rex::memory::Memory& memory,
      rex::graphics::Shader* vs, rex::graphics::Shader* ps,
      rex::graphics::xenos::PrimitiveType primitive, uint32_t count, const DrawIndices* indices) override {
    Render().Draw(regs, memory, vs, ps, primitive, count, indices);
  }
  void Resolve(const rex::graphics::RegisterFile& regs, rex::memory::Memory& memory) override {
    Render().Resolve(regs, memory);
  }
  SampleCounts ReadSampleCounts(bool counting_after) override {
    return Render().ReadSampleCounts(counting_after);
  }
 private:
  VulkanRender& Render() {
    if (!render_) {
      render_ = std::make_unique<VulkanRender>(provider_);
      render_->SetPipelineDirectory(pipeline_directory_);
    }
    return *render_;
  }
  rex::ui::vulkan::VulkanProvider& provider_;
  VulkanScanout scanout_;
  std::unique_ptr<VulkanRender> render_;
  std::filesystem::path pipeline_directory_;
};
std::unique_ptr<ScanoutBackend> CreateVulkanScanout(rex::ui::GraphicsProvider& provider) {
  auto* vulkan = dynamic_cast<rex::ui::vulkan::VulkanProvider*>(&provider);
  if (!vulkan) throw std::invalid_argument("Native Vulkan requires a Vulkan provider");
  return std::make_unique<VulkanBackend>(*vulkan);
}
}
