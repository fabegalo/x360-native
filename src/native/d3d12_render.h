#pragma once
#include "shader_cache.h"
#include "draw_types.h"
#include "texture_snapshot.h"
#include <rex/graphics/command_processor.h>
#include <rex/graphics/util/draw.h>
#include <rex/ui/d3d12/d3d12_provider.h>
#include <wrl/client.h>
#include <map>
#include <d3d12sdklayers.h>
#include <chrono>
namespace x360n::native {
class D3D12Render {
 public:
  explicit D3D12Render(rex::ui::d3d12::D3D12Provider&);
  ~D3D12Render();
  ShaderCache shaders;
  void Draw(const rex::graphics::RegisterFile&, rex::memory::Memory&,
      rex::graphics::Shader*, rex::graphics::Shader*, rex::graphics::xenos::PrimitiveType,
      uint32_t count, const DrawIndices*);
  void Resolve(const rex::graphics::RegisterFile&, rex::memory::Memory&);
  void Flush();
 private:
  template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
  Ptr<ID3D12Resource> Buffer(uint64_t, D3D12_HEAP_TYPE, D3D12_RESOURCE_STATES, D3D12_RESOURCE_FLAGS = D3D12_RESOURCE_FLAG_NONE);
  struct UploadSlice {
    Ptr<ID3D12Resource> resource;
    uint64_t offset;
    D3D12_GPU_VIRTUAL_ADDRESS Address() const { return resource->GetGPUVirtualAddress() + offset; }
  };
  UploadSlice Upload(const void*, size_t);
  struct UploadPage { Ptr<ID3D12Resource> resource; uint8_t* mapped; uint64_t size, used; };
  std::vector<UploadPage> upload_pages_;
  size_t upload_page_index_ = 0;
  D3D12_GPU_VIRTUAL_ADDRESS Constants(const void*, size_t);
  void Transition(ID3D12Resource*, D3D12_RESOURCE_STATES&, D3D12_RESOURCE_STATES);
  void Barrier();
  void UploadGuest(rex::memory::Memory&, uint32_t address, uint32_t length);
  D3D12_CPU_DESCRIPTOR_HANDLE Cpu(uint32_t, bool sampler = false);
  D3D12_GPU_DESCRIPTOR_HANDLE Gpu(uint32_t, bool sampler = false);
  uint32_t Views(uint32_t);
  uint32_t Samplers(uint32_t);
  void BindTextures(const rex::graphics::RegisterFile&, rex::memory::Memory&, rex::graphics::DxbcShader&, unsigned root, unsigned sampler_root);
  Ptr<ID3D12RootSignature> ComputeRoot(unsigned constants, bool source);
  Ptr<ID3D12PipelineState> ComputePSO(ID3D12RootSignature*, const void*, size_t);
  rex::ui::d3d12::D3D12Provider& provider_;
  ID3D12Device* device_;
  Ptr<ID3D12InfoQueue> debug_;
  Ptr<ID3D12CommandAllocator> allocator_;
  Ptr<ID3D12GraphicsCommandList> list_;
  Ptr<ID3D12Fence> fence_;
  HANDLE event_{};
  uint64_t serial_ = 0;
  Ptr<ID3D12Resource> memory_, edram_;
  D3D12_RESOURCE_STATES memory_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
  D3D12_RESOURCE_STATES edram_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  Ptr<ID3D12DescriptorHeap> views_, samplers_;
  uint32_t view_count_ = 0, sampler_count_ = 0;
  Ptr<ID3D12RootSignature> root_, copy_root_, clear_root_;
  std::vector<Ptr<ID3D12Resource>> transient_;
  std::map<std::string, Ptr<ID3D12PipelineState>> pipelines_;
  std::map<unsigned, std::vector<uint8_t>> rectangles_;
  struct CachedTexture {
    TextureSnapshot source;
    Ptr<ID3D12Resource> resource;
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    uint64_t bytes = 0;
  };
  std::map<std::string, CachedTexture> textures_;
  uint64_t texture_cache_bytes_ = 0, texture_cache_hits_ = 0;
  bool texture_cache_enabled_ = true;
  std::vector<uint8_t> depth_shader_;
  const std::vector<uint8_t>& RectangleShader(unsigned interpolators, unsigned clip_planes);
  std::array<Ptr<ID3D12PipelineState>, 9> copy_pipelines_;
  std::array<Ptr<ID3D12PipelineState>, 2> clear_pipelines_;
  uint64_t draw_count_ = 0, resolve_count_ = 0;
  bool stats_enabled_ = false;
  uint64_t texture_ns_ = 0, resolve_ns_ = 0, flush_ns_ = 0;
  uint64_t texture_uploads_ = 0, texture_upload_bytes_ = 0;
  uint64_t draw_ns_ = 0, guest_upload_ns_ = 0, guest_upload_bytes_ = 0;
  struct Timer {
    bool enabled; uint64_t& total; std::chrono::steady_clock::time_point start{};
    Timer(bool on, uint64_t& result) : enabled(on), total(result) { if (enabled) start = std::chrono::steady_clock::now(); }
    ~Timer() { if (enabled) total += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count(); }
  };
};
}
