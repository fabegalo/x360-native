#pragma once
#include "shader_cache.h"
#include "draw_types.h"
#include "texture_snapshot.h"
#include "guest_writeback.h"
#include <rex/graphics/command_processor.h>
#include <rex/graphics/util/draw.h>
#include <rex/ui/d3d12/d3d12_provider.h>
#include <wrl/client.h>
#include <map>
#include <unordered_map>
#include <unordered_set>
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
  void CountPresent() { ++present_count_; }
  // Guest synchronization point: publish recorded resolve results and start a
  // new epoch; guest bytes compared in an earlier epoch are compared again.
  void CompleteGuestWrites() { ++guest_epoch_; if (!writeback_.empty()) Flush(); }
 private:
  template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
  Ptr<ID3D12Resource> Buffer(uint64_t, D3D12_HEAP_TYPE, D3D12_RESOURCE_STATES, D3D12_RESOURCE_FLAGS = D3D12_RESOURCE_FLAG_NONE);
  struct UploadSlice {
    Ptr<ID3D12Resource> resource;
    uint64_t offset;
    D3D12_GPU_VIRTUAL_ADDRESS Address() const { return resource->GetGPUVirtualAddress() + offset; }
  };
  UploadSlice Upload(const void*, size_t);
  // Writable mapped range with the same fence lifetime as Upload.
  UploadSlice Reserve(size_t, uint8_t*& mapped);
  struct UploadPage { Ptr<ID3D12Resource> resource; uint8_t* mapped; uint64_t size, used; };
  std::vector<UploadPage> upload_pages_;
  size_t upload_page_index_ = 0;
  D3D12_GPU_VIRTUAL_ADDRESS Constants(const void*, size_t);
  void Transition(ID3D12Resource*, D3D12_RESOURCE_STATES&, D3D12_RESOURCE_STATES);
  void Barrier();
  void UploadGuest(rex::memory::Memory&, uint32_t address, uint32_t length);
  // Before Native reads guest memory that a pending resolve will write.
  void SyncGuestRead(uint32_t address, uint64_t length);
  GuestWriteback writeback_;
  rex::memory::Memory* guest_memory_ = nullptr;
  uint64_t guest_epoch_ = 1;
  uint64_t readback_used_ = 0, overlap_flushes_ = 0;
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
  Ptr<ID3D12Resource> memory_, edram_, readback_;
  uint64_t readback_size_ = 0;
  D3D12_RESOURCE_STATES memory_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
  D3D12_RESOURCE_STATES edram_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  Ptr<ID3D12DescriptorHeap> views_, samplers_;
  uint32_t view_count_ = 0, sampler_count_ = 0;
  Ptr<ID3D12RootSignature> root_, copy_root_, clear_root_;
  std::vector<Ptr<ID3D12Resource>> transient_;
  std::map<std::string, Ptr<ID3D12PipelineState>> pipelines_;
  std::map<unsigned, std::vector<uint8_t>> rectangles_;
  std::map<unsigned, std::vector<uint32_t>> points_;
  struct CachedTexture {
    TextureSnapshot source;
    Ptr<ID3D12Resource> resource;
    uint64_t bytes = 0, last_use = 0, epoch = 0;
  };
  // Estimated snapshot + host bytes; least recently bound entries go first.
  static constexpr uint64_t kTextureCacheBudget = 1024ull << 20;
  static constexpr size_t kTextureCacheEntries = 4096;
  uint64_t texture_use_ = 0;
  std::unordered_map<std::string, CachedTexture> textures_;
  // Vertex ranges already copied to memory_ in the current guest epoch.
  std::unordered_set<uint64_t> vertex_uploads_;
  uint64_t vertex_epoch_ = 0;
  uint64_t texture_cache_bytes_ = 0, texture_cache_hits_ = 0;
  bool texture_cache_enabled_ = true;
  std::vector<uint8_t> depth_shader_;
  const std::vector<uint8_t>& RectangleShader(unsigned interpolators, unsigned clip_planes);
  std::array<Ptr<ID3D12PipelineState>, 9> copy_pipelines_;
  std::array<Ptr<ID3D12PipelineState>, 2> clear_pipelines_;
  uint64_t draw_count_ = 0, resolve_count_ = 0, empty_resolve_count_ = 0, present_count_ = 0;
  uint64_t invalid_vertex_fetches_ = 0, invalid_texture_fetches_ = 0;
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
