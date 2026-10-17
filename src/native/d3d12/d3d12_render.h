#pragma once
#include "../pipeline/shader_cache.h"
#include "../pipeline/pipeline_store.h"
#include "../commands/draw_types.h"
#include "../memory/guest_memory_mirror.h"
#include "../textures/texture_load.h"
#include "../memory/guest_writeback.h"
#include "../commands/scanout_backend.h"
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
  void SetPipelineDirectory(const std::filesystem::path& path) { pipeline_store_.SetDirectory(path); }
  void Draw(const rex::graphics::RegisterFile&, rex::memory::Memory&,
      rex::graphics::Shader*, rex::graphics::Shader*, rex::graphics::xenos::PrimitiveType,
      uint32_t count, const DrawIndices*);
  void Resolve(const rex::graphics::RegisterFile&, rex::memory::Memory&);
  // Diagnostic accounting only (native_stats): why a submission waited.
  enum FlushCause : unsigned { kFlushGuestSync, kFlushPresent, kFlushOverlap, kFlushCapacity, kFlushDrain, kFlushCauses };
  void Flush(FlushCause);
  void CountPresent();
  void EndFrame();
  // Guest synchronization point: publish recorded resolve results.
  void CompleteGuestWrites(GuestSync source, uint32_t opcode) {
    if (writeback_.empty()) return;
    if (stats_enabled_) { ++sync_sources_[source]; ++sync_opcodes_[opcode & 0xFF]; }
    Flush(kFlushGuestSync);
  }
  void SyncPacketRead(uint32_t address, uint32_t length, uint32_t opcode) {
    if (!writeback_.Overlaps(address, length)) return;
    if (stats_enabled_) ++sync_opcodes_[opcode & 0xFF];
    Flush(kFlushOverlap);
  }
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
  // Resident guest memory mirror (memory_), created with the guest memory.
  GuestMemoryMirror& Mirror(rex::memory::Memory&);
  std::unique_ptr<GuestMemoryMirror> mirror_;
  // Records a copy of current guest bytes into memory_ (mirror upload).
  void UploadGuest(uint32_t address, uint32_t length);
  // Before Native reads guest memory that a pending resolve will write.
  enum ReadSite : unsigned { kReadIndices, kReadCpuTexture, kReadResolveRect, kReadSites };
  void SyncGuestRead(uint32_t address, uint64_t length, ReadSite);
  uint64_t overlap_sites_[kReadSites]{};
  GuestWriteback writeback_;
  rex::memory::Memory* guest_memory_ = nullptr;
  uint64_t readback_used_ = 0, overlap_flushes_ = 0;
  D3D12_CPU_DESCRIPTOR_HANDLE Cpu(uint32_t, bool sampler = false);
  D3D12_GPU_DESCRIPTOR_HANDLE Gpu(uint32_t, bool sampler = false);
  uint32_t Views(uint32_t);
  uint32_t Samplers(uint32_t);
  // Loads or reuses the textures of a shader and writes its view and sampler
  // tables before the draw state is recorded; Tables binds them afterwards.
  struct TextureTables { D3D12_GPU_VIRTUAL_ADDRESS indices = 0; };
  TextureTables BindTextures(const rex::graphics::RegisterFile&, rex::memory::Memory&, rex::graphics::DxbcShader&);
  void SetTextureTables(const TextureTables&, unsigned root);
  // Records a GPU load of guest bytes from memory_ into texture (COPY_DEST).
  void LoadTextureGpu(const TextureLoadPlan&, ID3D12Resource* texture);
  struct Scratch { Ptr<ID3D12Resource> buffer; uint64_t size, used; D3D12_RESOURCE_STATES state; };
  std::vector<Scratch> scratch_;
  Ptr<ID3D12RootSignature> load_root_;
  std::array<Ptr<ID3D12PipelineState>, size_t(LoadShader::kCount)> load_pipelines_;
  static void TextureWatch(void* context, void* data, uint64_t argument, bool by_gpu);
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
    Ptr<ID3D12Resource> resource;
    uint64_t bytes = 0, last_use = 0;
    // Guarded by GuestMemoryMirror::Lock(): set by a CPU or GPU write to the
    // base (0) or mip (1) range; a fired watch clears its own handle.
    bool outdated = true;
    GuestMemoryMirror::WatchHandle watches[2]{};
  };
  void Unwatch(CachedTexture&);
  // Estimated host bytes; least recently bound entries go first.
  static constexpr uint64_t kTextureCacheBudget = 1024ull << 20;
  static constexpr size_t kTextureCacheEntries = 4096;
  uint64_t texture_use_ = 0;
  std::unordered_map<std::string, CachedTexture> textures_;
  uint64_t texture_cache_bytes_ = 0, texture_cache_hits_ = 0, gpu_texture_loads_ = 0, cpu_texture_loads_ = 0;
  bool texture_cache_enabled_ = true;
  bool texture_cpu_ = false;
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
  uint64_t shader_ns_ = 0, pipeline_ns_ = 0, pipeline_thread_ns_ = 0, pipeline_process_ns_ = 0;
  uint64_t pipeline_creations_ = 0;
  uint64_t pipeline_disk_hits_ = 0, pipeline_disk_rejects_ = 0;
  PipelineStore pipeline_store_;
  uint64_t flush_causes_[kFlushCauses]{}, flush_wait_ns_ = 0;
  uint64_t sync_sources_[kGuestSyncSources]{};
  uint64_t sync_opcodes_[256]{};
  // Guest present intervals of the current stats window (native_stats).
  std::vector<float> frame_ms_;
  std::chrono::steady_clock::time_point last_present_{}, window_start_{};
  struct Timer {
    bool enabled; uint64_t& total; std::chrono::steady_clock::time_point start{};
    Timer(bool on, uint64_t& result) : enabled(on), total(result) { if (enabled) start = std::chrono::steady_clock::now(); }
    ~Timer() { if (enabled) total += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count(); }
  };
};
}
