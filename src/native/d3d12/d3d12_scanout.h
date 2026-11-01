#pragma once
#include "../commands/scanout_backend.h"
#include "d3d12_render.h"
#include "d3d12_fxaa.h"
#include <array>
#include <memory>
#include <rex/ui/d3d12/d3d12_provider.h>
#include <wrl/client.h>
namespace x360n::native {
class D3D12Scanout final : public ScanoutBackend {
 public:
  explicit D3D12Scanout(rex::ui::d3d12::D3D12Provider& provider);
  ~D3D12Scanout();
  void Present(rex::ui::Presenter& presenter, const ScanoutTexture& texture,
               const void* pixels, const uint32_t* lut, const ScanoutOverlay* overlay,
               const void* overlay_pixels, ScanoutPostEffect = ScanoutPostEffect::kNone) override;
  void Drain() override;
  void InitializeShaderStorage(const std::filesystem::path&, uint32_t title_id) override;
  void CompleteGuestWrites(GuestSync source, uint32_t opcode) override { if (render_) render_->CompleteGuestWrites(source, opcode); }
  void SyncGuestRead(uint32_t address, uint32_t length, uint32_t opcode) override { if (render_) render_->SyncPacketRead(address, length, opcode); }
  rex::graphics::Shader* LoadShader(rex::graphics::xenos::ShaderType type, const uint32_t* data, uint32_t count) override;
  void Draw(const rex::graphics::RegisterFile& regs, rex::memory::Memory& memory, rex::graphics::Shader* vs, rex::graphics::Shader* ps,
      rex::graphics::xenos::PrimitiveType primitive, uint32_t count, const DrawIndices* indices, DrawPurpose = DrawPurpose::kGuest) override;
  void Resolve(const rex::graphics::RegisterFile& regs, rex::memory::Memory& memory) override;
  SampleCounts ReadSampleCounts(bool counting_after) override;
  void SetSampleCounts(const SampleCounts&, bool visibility_only) override;
  void CompleteFrame(const ScanoutTexture&, const void* pixels) override;
  EdramSnapshot ReadEdram() override { return Render().ReadEdram(); }
  void WriteEdram(const EdramSnapshot& snapshot) override { Render().WriteEdram(snapshot); }
  BackendCounters Counters() override;
 private:
  // CompleteFrame plus the scaled pixels to present (empty when unscaled).
  void FinishGuestFrame(const ScanoutTexture& input, const void* input_pixels, ScanoutTexture& output,
                        const void*& pixels, std::vector<uint8_t>& scaled_pixels);
  template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
  struct Slot {
    Ptr<ID3D12CommandAllocator> allocator;
    Ptr<ID3D12GraphicsCommandList> list;
    Ptr<ID3D12Resource> upload;
    uint64_t capacity = 0, serial = 0;
  };
  void Wait(uint64_t serial);
  D3D12Render& Render();
  rex::ui::d3d12::D3D12Provider& provider_;
  Ptr<ID3D12RootSignature> root_;
  Ptr<ID3D12PipelineState> pipeline_;
  Ptr<ID3D12DescriptorHeap> descriptors_;
  Ptr<ID3D12Fence> fence_;
  HANDLE event_ = nullptr;
  uint64_t serial_ = 0;
  std::array<Slot, 3> slots_;
  std::unique_ptr<D3D12Render> render_;
  std::unique_ptr<D3D12Fxaa> fxaa_;
  std::filesystem::path pipeline_directory_;
};
}
