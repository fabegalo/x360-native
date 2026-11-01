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
      const uint32_t* lut, const ScanoutOverlay* overlay, const void* overlay_pixels, ScanoutPostEffect post_effect) override {
    ScanoutTexture output = texture;
    std::vector<uint8_t> scaled_pixels;
    FinishGuestFrame(texture, pixels, output, scaled_pixels);
    scanout_.Present(presenter, output, pixels, lut, overlay, overlay_pixels, post_effect,
                     texture.width, texture.height);
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
      rex::graphics::xenos::PrimitiveType primitive, uint32_t count, const DrawIndices* indices, DrawPurpose purpose) override {
    Render().Draw(regs, memory, vs, ps, primitive, count, indices, purpose);
  }
  void Resolve(const rex::graphics::RegisterFile& regs, rex::memory::Memory& memory) override {
    Render().Resolve(regs, memory);
  }
  SampleCounts ReadSampleCounts(bool counting_after) override {
    return Render().ReadSampleCounts(counting_after);
  }
  void SetSampleCounts(const SampleCounts& counts, bool visibility_only) override {
    Render().SetSampleCounts(counts, visibility_only);
  }
  void CompleteFrame(const ScanoutTexture& texture, const void* pixels) override {
    ScanoutTexture output = texture;
    std::vector<uint8_t> scaled_pixels;
    FinishGuestFrame(texture, pixels, output, scaled_pixels);
  }
  EdramSnapshot ReadEdram() override { return Render().ReadEdram(); }
  void WriteEdram(const EdramSnapshot& snapshot) override { Render().WriteEdram(snapshot); }
  BackendCounters Counters() override {
    // Counted on this (command) thread only while native_vulkan_perf is on.
    const auto& perf = NativeVulkanPerformance();
    BackendCounters c;
    c.measured = perf.enabled;
    c.submissions = perf.submissions;
    c.fence_waits = perf.blocked_waits;
    c.fence_wait_ns = perf.wait_ns;
    c.gpu_timed = perf.enabled && perf.gpu_samples;
    c.gpu_busy_ns = perf.gpu_ns;
    return c;
  }
 private:
  void FinishGuestFrame(const ScanoutTexture& texture, const void*& pixels, ScanoutTexture& output,
                        std::vector<uint8_t>& scaled_pixels) {
    if (!render_) return;
    render_->CompleteGuestWrites(kGuestSyncPresent, 0xFF);
    render_->Flush(false);
    if (render_->PrepareScanout(texture, pixels, output, scaled_pixels)) pixels = scaled_pixels.data();
    render_->EndFrame();
  }
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
