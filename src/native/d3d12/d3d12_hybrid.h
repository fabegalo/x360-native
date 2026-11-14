#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <rex/graphics/register_file.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace x360n::native {
class D3D12Render;
struct HybridColorKey {
  uint32_t base, pitch_tiles, width, height, msaa, left, top, right, bottom;
  bool operator==(const HybridColorKey&) const = default;
};
// GPU cache of one logical EDRAM's opaque HDR color surfaces. Depth/stencil
// stays in the ROV buffer. Every route change exports color before ROV use.
class D3D12HybridColor {
 public:
  explicit D3D12HybridColor(D3D12Render&);
  std::optional<HybridColorKey> Classify(const rex::graphics::RegisterFile&, uint32_t color_mask) const;
  void Begin(const HybridColorKey&);
  void End();
  D3D12_CPU_DESCRIPTOR_HANDLE target() const;
  void Drawn() { ++draws_; }
  void LogStats() const;
 private:
  template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
  struct Surface {
    HybridColorKey key;
    Ptr<ID3D12Resource> texture;
    Ptr<ID3D12DescriptorHeap> rtv;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_RENDER_TARGET;
    uint64_t bytes, last_use;
  };
  D3D12Render& render_;
  std::vector<std::unique_ptr<Surface>> surfaces_;
  Surface* active_ = nullptr;
  Ptr<ID3D12RootSignature> root_;
  Ptr<ID3D12PipelineState> load_[2], store_[2];
  bool msaa4_ = false;
  uint64_t bytes_ = 0, use_ = 0, draws_ = 0, imports_ = 0, exports_ = 0;
  void Initialize();
  void BindBridge(bool store);
};
bool NativeHybridColorEnabled();
}
