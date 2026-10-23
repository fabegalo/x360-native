#pragma once
#include "../quality/settings.h"
#include "../memory/scaled_resolve.h"
#include "../pipeline/shader_cache.h"
#include "../pipeline/pipeline_store.h"
#include "../commands/draw_types.h"
#include "../commands/memexport.h"
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
#include <optional>
#include <d3d12sdklayers.h>
#include <chrono>
namespace x360n::native {
class D3D12Render {
  const NativeQualitySettings quality_ = GetNativeQualitySettings();
  const uint32_t scale_x_ = quality_.scale_x, scale_y_ = quality_.scale_y;
  const uint64_t edram_size_ = uint64_t(rex::graphics::xenos::kEdramSizeBytes) * scale_x_ * scale_y_;
 public:
  explicit D3D12Render(rex::ui::d3d12::D3D12Provider&);
  ~D3D12Render();
  ShaderCache shaders{scale_x_, scale_y_};
  void SetPipelineDirectory(const std::filesystem::path& path) { pipeline_store_.SetDirectory(path); }
  void Draw(const rex::graphics::RegisterFile&, rex::memory::Memory&,
      rex::graphics::Shader*, rex::graphics::Shader*, rex::graphics::xenos::PrimitiveType,
      uint32_t count, const DrawIndices*, DrawPurpose = DrawPurpose::kGuest);
  bool PrepareScanout(const ScanoutTexture& input, const void* pixels, ScanoutTexture& output,
                       std::vector<uint8_t>& storage) const { return scaled_.Scanout(input, pixels, output, storage); }
  void Resolve(const rex::graphics::RegisterFile&, rex::memory::Memory&);
  SampleCounts ReadSampleCounts(bool counting_after);
  void SetSampleCounts(const SampleCounts&, bool visibility_only);
  // Diagnostic accounting only (native_stats): why a submission waited.
  enum FlushCause : unsigned { kFlushGuestSync, kFlushPresent, kFlushOverlap, kFlushCapacity, kFlushDrain, kFlushCauses };
  void Flush(FlushCause);
  void CountPresent();
  void EndFrame();
  // Guest synchronization point: publish recorded resolve/MEMEXPORT results.
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
  void Wait(uint64_t serial);
  void RecycleBatch();
  // Resident guest memory mirror (memory_), created with the guest memory.
  GuestMemoryMirror& Mirror(rex::memory::Memory&);
  std::unique_ptr<GuestMemoryMirror> mirror_;
  ScaledResolveCache scaled_{scale_x_, scale_y_};
  uint32_t textures_resolution_scaled_ = 0;
  // Records a copy of current guest bytes into memory_ (mirror upload).
  void UploadGuest(uint32_t address, uint32_t length);
  // Reserve before recording draw/resolve state: this may submit the batch.
  void ReserveReadback(uint64_t bytes);
  // Record a GPU-written range in already reserved readback storage.
  void QueueGuestWrite(uint32_t address, uint32_t length, const std::vector<uint64_t>* snapshot_epochs = nullptr);
  std::unordered_map<const rex::graphics::Shader*, bool> memexport_addresses_known_;
  // Before Native reads guest memory that a pending GPU write will change.
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
  // Pixel-to-EDRAM mapping of the ROV draws recorded since the last EDRAM UAV
  // barrier or state transition; empty right after one (see Draw).
  using EdramRovLayout = std::array<uint32_t, 22>;
  std::optional<EdramRovLayout> edram_rov_layout_;
  uint64_t edram_barriers_ = 0, edram_layout_barriers_ = 0;
  Ptr<ID3D12DescriptorHeap> views_, samplers_;
  uint32_t view_count_ = 0, sampler_count_ = 0;
  // Samplers already written into the current arena's heap, by exact
  // D3D12_SAMPLER_DESC bytes. Cleared with the arena (RecycleBatch).
  using SamplerKey = std::array<uint32_t, sizeof(D3D12_SAMPLER_DESC) / 4>;
  struct SamplerKeyHash { size_t operator()(const SamplerKey&) const; };
  std::unordered_map<SamplerKey, uint32_t, SamplerKeyHash> arena_samplers_;
  uint64_t sampler_reuses_ = 0;
  uint32_t Sampler(const D3D12_SAMPLER_DESC&);
  Ptr<ID3D12RootSignature> root_, root_tess_, copy_root_, clear_root_;
  std::vector<Ptr<ID3D12Resource>> transient_;
  // Three independent recording arenas. Queue ordering protects persistent
  // memory/EDRAM/textures; a fence protects each arena's mutable descriptors,
  // uploads, scratch regions and command allocator from premature reuse.
  struct RetiredBatch {
    uint64_t serial = 0;
    Ptr<ID3D12CommandAllocator> allocator;
    Ptr<ID3D12DescriptorHeap> views, samplers;
    std::vector<Ptr<ID3D12Resource>> transient;
    std::vector<UploadPage> uploads;
    std::vector<Scratch> scratch;
  };
  std::array<RetiredBatch, 3> batches_;
  unsigned batch_index_ = 0;
  uint64_t fence_waits_ = 0, async_submissions_ = 0;
  // native_stats only: two timestamps bracket each submission of an arena and
  // are read once its fence has completed, never with an added wait.
  Ptr<ID3D12QueryHeap> timestamps_;
  Ptr<ID3D12Resource> timestamp_readback_;
  std::array<uint64_t, 3> timestamp_serials_{};
  uint64_t timestamp_frequency_ = 0, gpu_busy_ticks_ = 0, gpu_idle_ticks_ = 0, gpu_last_end_ = 0, gpu_timed_ = 0;
  void BeginGpuTimestamp();
  void EndGpuTimestamp();
  void CollectGpuTimestamps();
  void LogSubmissionStats();
  std::map<std::string, Ptr<ID3D12PipelineState>> pipelines_;
  std::map<unsigned, std::vector<uint8_t>> rectangles_, polygon_points_;
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
  std::array<std::vector<uint8_t>, 3> depth_shaders_;
  // Tail of the Native-owned EDRAM allocation; never part of guest EDRAM
  // addressing or resolves. Only the query variant of a shader accesses it.
  Ptr<ID3D12Resource> sample_counts_readback_;
  SampleCounts sample_counts_{};
  bool sample_counting_ = false;
  bool visibility_only_ = false;
  const std::vector<uint8_t>& RectangleShader(unsigned interpolators, unsigned clip_planes);
  struct ScaledReadback { ScaledResolveCache::Ticket ticket; Ptr<ID3D12Resource> buffer;
                          unsigned pixel_log2; uint64_t offset; };
  std::vector<ScaledReadback> scaled_readbacks_;
  Ptr<ID3D12RootSignature> downscale_root_;
  Ptr<ID3D12PipelineState> downscale_pipeline_;
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
