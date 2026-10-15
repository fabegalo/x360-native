#include "d3d12_render.h"
#include <rex/graphics/pipeline/render_target/cache.h>
#include <rex/logging.h>
#include <rex/graphics/flags.h>
#include <d3dcompiler.h>
#include <stdexcept>
#include <bit>
#include <cmath>
#include <algorithm>
REXCVAR_DEFINE_BOOL(native_stats, false, "GPU/Native", "Log cumulative Native CPU submission costs for bounded diagnostics");
REXCVAR_DEFINE_BOOL(native_texture_cache, true, "GPU/Native", "Reuse textures only while every guest source byte matches; disable for diagnostics");
REXCVAR_DEFINE_BOOL(native_texture_cpu, false, "GPU/Native", "Diagnose texture loads with CPU conversion and synchronized guest bytes");
namespace x360n::native {
using namespace rex::graphics;
using namespace rex::graphics::xenos;
namespace {
void Check(HRESULT hr, const char* what) {
  if (FAILED(hr)) throw std::runtime_error(std::string("Native render: ") + what + " HRESULT=" + std::to_string(uint32_t(hr)));
}
void Require(bool v, const char* what) { if (!v) throw std::runtime_error(std::string("Native: ") + what); }
}
D3D12Render::Ptr<ID3D12Resource> D3D12Render::Buffer(uint64_t bytes, D3D12_HEAP_TYPE type,
    D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags) {
  D3D12_HEAP_PROPERTIES heap{}; heap.Type = type;
  D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = bytes;
  d.Height = 1; d.DepthOrArraySize = 1; d.MipLevels = 1; d.SampleDesc.Count = 1;
  d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; d.Flags = flags;
  Ptr<ID3D12Resource> r;
  Check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(&r)), "buffer");
  return r;
}
D3D12Render::UploadSlice D3D12Render::Upload(const void* data, size_t bytes) {
  uint8_t* mapped;
  auto slice = Reserve(bytes, mapped);
  std::memcpy(mapped, data, bytes);
  return slice;
}
D3D12Render::UploadSlice D3D12Render::Reserve(size_t bytes, uint8_t*& mapped) {
  // Suballocate immutable ranges for this submission. Reuse only after Flush
  // has waited for the fence, including texture copies and root CBVs.
  const uint64_t aligned = (uint64_t(bytes) + 511) & ~uint64_t(511);
  while (upload_page_index_ < upload_pages_.size() &&
      upload_pages_[upload_page_index_].size - upload_pages_[upload_page_index_].used < aligned)
    ++upload_page_index_;
  if (upload_page_index_ == upload_pages_.size()) {
    uint64_t size = std::max(aligned, uint64_t(4 * 1024 * 1024));
    auto resource = Buffer(size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    void* mapped; D3D12_RANGE empty{};
    Check(resource->Map(0, &empty, &mapped), "map upload arena");
    upload_pages_.push_back({resource, static_cast<uint8_t*>(mapped), size, 0});
  }
  auto& page = upload_pages_[upload_page_index_];
  const uint64_t offset = page.used; page.used += aligned;
  mapped = page.mapped + offset;
  return {page.resource, offset};
}
D3D12_GPU_VIRTUAL_ADDRESS D3D12Render::Constants(const void* p, size_t n) { return Upload(p, n).Address(); }
void D3D12Render::Transition(ID3D12Resource* r, D3D12_RESOURCE_STATES& before, D3D12_RESOURCE_STATES after) {
  if (before == after) return;
  D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
  list_->ResourceBarrier(1, &b); before = after;
}
void D3D12Render::Barrier() {
  D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; b.UAV.pResource = edram_.Get();
  list_->ResourceBarrier(1, &b);
}
D3D12_CPU_DESCRIPTOR_HANDLE D3D12Render::Cpu(uint32_t i, bool s) {
  auto h = (s ? samplers_ : views_)->GetCPUDescriptorHandleForHeapStart();
  h.ptr += i * device_->GetDescriptorHandleIncrementSize(s ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); return h;
}
D3D12_GPU_DESCRIPTOR_HANDLE D3D12Render::Gpu(uint32_t i, bool s) {
  auto h = (s ? samplers_ : views_)->GetGPUDescriptorHandleForHeapStart();
  h.ptr += i * device_->GetDescriptorHandleIncrementSize(s ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); return h;
}
uint32_t D3D12Render::Views(uint32_t n) { Require(view_count_ + n <= 65536, "view heap exhausted"); auto i = view_count_; view_count_ += n; return i; }
uint32_t D3D12Render::Samplers(uint32_t n) { Require(sampler_count_ + n <= 2048, "sampler heap exhausted"); auto i = sampler_count_; sampler_count_ += n; return i; }
D3D12Render::D3D12Render(rex::ui::d3d12::D3D12Provider& provider) : provider_(provider), device_(provider.GetDevice()) {
  stats_enabled_ = REXCVAR_GET(native_stats);
  texture_cache_enabled_ = REXCVAR_GET(native_texture_cache);
  texture_cpu_ = REXCVAR_GET(native_texture_cpu);
  device_->QueryInterface(IID_PPV_ARGS(&debug_));
  D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
  Check(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)), "feature query");
  Require(options.ROVsSupported, "D3D12 rasterizer ordered views required");
  Require(options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_2, "D3D12 resource binding tier 2 required");
  Check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator_)), "allocator");
  Check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(), nullptr, IID_PPV_ARGS(&list_)), "command list");
  Check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "fence");
  event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr); Require(event_ != nullptr, "fence event");
  memory_ = Buffer(0x20000000, D3D12_HEAP_TYPE_DEFAULT, memory_state_, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  edram_ = Buffer(kEdramSizeBytes, D3D12_HEAP_TYPE_DEFAULT, edram_state_, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 65536, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
  Check(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&views_)), "views");
  hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER; hd.NumDescriptors = 2048;
  Check(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&samplers_)), "samplers");
  // Fixed binding ABI of the shared translator. Submission and resources are Native-owned.
  D3D12_ROOT_PARAMETER p[10]{};
  const unsigned cb[] = {0, 1, 1, 2, 3};
  for (unsigned i = 0; i < 5; ++i) { p[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; p[i].Descriptor = {cb[i], 0}; }
  p[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX; p[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  // Match the translator's bindless ABI: b4 contains per-stage descriptor
  // indices, and spaces 1/2/3 alias the same heap for 2D/3D/cube views.
  // Descriptors retain their existing lifetime (until the submission fence).
  D3D12_DESCRIPTOR_RANGE ranges[] = {
    {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0},
    {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, 1},
    {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, UINT_MAX, 0, 0, 0},
    {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, 1, 0},
    {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, 2, 0},
    {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, 3, 0}};
  p[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[5].DescriptorTable = {2, ranges};
  p[6].ParameterType = p[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  p[6].Descriptor = p[8].Descriptor = {4, 0};
  p[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  p[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
  p[7].ParameterType = p[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  p[7].DescriptorTable = {1, ranges + 2};
  p[9].DescriptorTable = {3, ranges + 3};
  D3D12_ROOT_SIGNATURE_DESC rd{10, p, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
  Ptr<ID3DBlob> blob, errors;
  Check(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors), "draw root serialization");
  Check(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root_)), "draw root");
}
// Watches reference cache entries: retire them before the cache is destroyed.
D3D12Render::~D3D12Render() { mirror_.reset(); if (event_) CloseHandle(event_); }
void D3D12Render::EndFrame() {
  // Pinned clear_memory_page_state contract: CPU-uploaded pages are reloaded
  // on their next request after a frame, GPU-written pages stay resident.
  if (mirror_ && REXCVAR_GET(clear_memory_page_state)) mirror_->InvalidateCpuPages();
}
void D3D12Render::CountPresent() {
  ++present_count_;
  if (!stats_enabled_) return;
  const auto now = std::chrono::steady_clock::now();
  if (last_present_.time_since_epoch().count()) frame_ms_.push_back(std::chrono::duration<float, std::milli>(now - last_present_).count());
  else window_start_ = now;
  last_present_ = now;
  const double window = std::chrono::duration<double>(now - window_start_).count();
  if (window < 5.0 || frame_ms_.empty()) return;
  // Interval statistics of guest presents in this window: average rate,
  // percentile frame times and 1% low (average rate of the slowest 1%).
  std::vector<float> sorted = frame_ms_;
  std::sort(sorted.begin(), sorted.end());
  auto pct = [&](double p) { return sorted[std::min(sorted.size() - 1, size_t(p * double(sorted.size())))]; };
  const size_t slow = std::max<size_t>(1, sorted.size() / 100);
  double slow_ms = 0; for (size_t i = sorted.size() - slow; i < sorted.size(); ++i) slow_ms += sorted[i];
  REXLOG_INFO("Native frame stats frames={} seconds={:.2f} fps={:.2f} p50_ms={:.2f} p95_ms={:.2f} p99_ms={:.2f} max_ms={:.2f} low1_fps={:.2f}",
      sorted.size(), window, double(sorted.size()) / window, pct(0.50), pct(0.95), pct(0.99), sorted.back(), 1000.0 * double(slow) / slow_ms);
  REXLOG_INFO("Native flush causes guest_sync={} (event={} packet={} wait={} buffer_end={} present={}) present={} overlap={} capacity={} drain={} wait_ms={}",
      flush_causes_[kFlushGuestSync], sync_sources_[kGuestSyncEvent], sync_sources_[kGuestSyncPacket], sync_sources_[kGuestSyncWait], sync_sources_[kGuestSyncBufferEnd], sync_sources_[kGuestSyncPresent], flush_causes_[kFlushPresent], flush_causes_[kFlushOverlap], flush_causes_[kFlushCapacity], flush_causes_[kFlushDrain], flush_wait_ns_ / 1000000);
  std::string opcodes;
  for (unsigned i = 0; i < 256; ++i) if (sync_opcodes_[i]) opcodes += fmt::format(" {:02X}:{}", i, sync_opcodes_[i]);
  REXLOG_INFO("Native guest sync opcodes{}", opcodes);
  REXLOG_INFO("Native overlap sites indices={} cpu_texture={} resolve_rect={} mirror_upload_bytes={}", overlap_sites_[kReadIndices], overlap_sites_[kReadCpuTexture], overlap_sites_[kReadResolveRect], mirror_ ? mirror_->uploaded_bytes() : 0);
  frame_ms_.clear(); window_start_ = now;
}
void D3D12Render::Flush(FlushCause cause) {
  Timer timer(stats_enabled_, flush_ns_);
  if (stats_enabled_) ++flush_causes_[cause];
  Check(list_->Close(), "close"); ID3D12CommandList* lists[]{list_.Get()};
  provider_.GetDirectQueue()->ExecuteCommandLists(1, lists);
  Check(provider_.GetDirectQueue()->Signal(fence_.Get(), ++serial_), "signal");
  {
    Timer wait(stats_enabled_, flush_wait_ns_);
    Check(fence_->SetEventOnCompletion(serial_, event_), "fence completion");
  Require(WaitForSingleObject(event_, 5000) == WAIT_OBJECT_0, "render GPU timeout");
  }
  Check(device_->GetDeviceRemovedReason(), "device status");
  if (debug_) {
    const auto messages = debug_->GetNumStoredMessagesAllowedByRetrievalFilter();
    for (uint64_t i = 0; i < messages; ++i) {
      SIZE_T bytes = 0; debug_->GetMessage(i, nullptr, &bytes);
      std::vector<uint8_t> storage(bytes);
      auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
      Check(debug_->GetMessage(i, message, &bytes), "debug message");
      if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
        throw std::runtime_error(std::string("Native D3D12 validation: ") + message->pDescription);
    }
    debug_->ClearStoredMessages();
  }
  if (!writeback_.empty()) {
    // The fence above retired every resolve copy of this submission.
    void* mapped; D3D12_RANGE range{0, size_t(readback_used_)};
    Check(readback_->Map(0, &range, &mapped), "map resolve readback");
    writeback_.Complete([&](const GuestWriteback::Write& w) {
      mirror_->CopyReadbackToGuest(w.address, static_cast<const uint8_t*>(mapped) + w.offset, w.length);
    });
    D3D12_RANGE none{}; readback_->Unmap(0, &none);
  }
  readback_used_ = 0;
  transient_.clear(); view_count_ = sampler_count_ = 0;
  for (auto& s : scratch_) s.used = 0;
  uint64_t retained = 0;
  for (auto& page : upload_pages_) { page.used = 0; retained += page.size; }
  if (retained > 64 * 1024 * 1024) upload_pages_.clear();
  upload_page_index_ = 0;
  Check(allocator_->Reset(), "reset allocator"); Check(list_->Reset(allocator_.Get(), nullptr), "reset list");
  if (stats_enabled_ && serial_ % 30 == 0)
    REXLOG_INFO("Native costs presents={} draws={} resolves={} empty_resolves={} overlap_flushes={} submits={} texture_uploads={} texture_bytes={} texture_ms={} resolve_ms={} flush_ms={} cache_hits={} (flush overlaps resolve)",
        present_count_, draw_count_, resolve_count_, empty_resolve_count_, overlap_flushes_, serial_, texture_uploads_, texture_upload_bytes_, texture_ns_ / 1000000,
        resolve_ns_ / 1000000, flush_ns_ / 1000000, texture_cache_hits_);
  if (stats_enabled_ && serial_ % 30 == 0)
    REXLOG_INFO("Native draw costs draw_ms={} guest_upload_ms={} guest_upload_bytes={} gpu_texture_loads={} cpu_texture_loads={} flush_wait_ms={} (draw includes textures; uploads are mirror pages the CPU wrote)",
        draw_ns_ / 1000000, guest_upload_ns_ / 1000000, guest_upload_bytes_, gpu_texture_loads_, cpu_texture_loads_, flush_wait_ns_ / 1000000);
}
void D3D12Render::SyncGuestRead(uint32_t address, uint64_t length, ReadSite site) {
  if (length && writeback_.Overlaps(address, uint32_t(std::min<uint64_t>(length, 0x20000000)))) {
    if (stats_enabled_) { ++overlap_flushes_; ++overlap_sites_[site]; }
    Flush(kFlushOverlap);
  }
}
GuestMemoryMirror& D3D12Render::Mirror(rex::memory::Memory& memory) {
  guest_memory_ = &memory;
  if (!mirror_) mirror_ = std::make_unique<GuestMemoryMirror>(memory, [this](uint32_t a, uint32_t n) { UploadGuest(a, n); });
  return *mirror_;
}
void D3D12Render::UploadGuest(uint32_t address, uint32_t bytes) {
  Timer timer(stats_enabled_, guest_upload_ns_);
  Require(uint64_t(address) + bytes <= 0x20000000, "guest upload bounds");
  if (!bytes) return;
  // Only pages the CPU wrote since they were valid: never the GPU-written
  // pages of a pending resolve, which stay valid in the mirror.
  if (stats_enabled_) guest_upload_bytes_ += bytes;
  auto data = Upload(guest_memory_->TranslatePhysical(address), bytes);
  Transition(memory_.Get(), memory_state_, D3D12_RESOURCE_STATE_COPY_DEST);
  list_->CopyBufferRegion(memory_.Get(), address, data.resource.Get(), data.offset, bytes);
}
}
