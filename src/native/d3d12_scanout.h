#pragma once
#include "scanout_backend.h"
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
               const void* pixels, const uint32_t* lut) override;
  void Drain() override;
 private:
  template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
  struct Slot {
    Ptr<ID3D12CommandAllocator> allocator;
    Ptr<ID3D12GraphicsCommandList> list;
    Ptr<ID3D12Resource> upload;
    uint64_t capacity = 0, serial = 0;
  };
  void Wait(uint64_t serial);
  rex::ui::d3d12::D3D12Provider& provider_;
  Ptr<ID3D12RootSignature> root_;
  Ptr<ID3D12PipelineState> pipeline_;
  Ptr<ID3D12DescriptorHeap> descriptors_;
  Ptr<ID3D12Fence> fence_;
  HANDLE event_ = nullptr;
  uint64_t serial_ = 0;
  std::array<Slot, 3> slots_;
};
}
