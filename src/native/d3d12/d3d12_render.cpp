#include "d3d12_render.h"
#include <rex/graphics/pipeline/render_target/cache.h>
#include <rex/logging.h>
#include <rex/graphics/flags.h>
#include <d3dcompiler.h>
#include <stdexcept>
#include <bit>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <utility>
REXCVAR_DEFINE_BOOL(native_stats, false, "GPU/Native", "Log cumulative Native CPU submission costs for bounded diagnostics");
REXCVAR_DEFINE_BOOL(native_texture_cache, true, "GPU/Native", "Reuse textures only while every guest source byte matches; disable for diagnostics");
REXCVAR_DEFINE_BOOL(native_texture_cpu, false, "GPU/Native", "Diagnose texture loads with CPU conversion and synchronized guest bytes");
REXCVAR_DECLARE(std::string, native_pso_dump);
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
  // Suballocate immutable ranges for this batch. RecycleBatch waits for its
  // own fence before reusing uploads, including texture copies and root CBVs.
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
  cmd_.ResourceBarrier(1, &b); before = after;
}
void D3D12Render::Barrier() {
  D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; b.UAV.pResource = edram_.Get();
  cmd_.ResourceBarrier(1, &b);
  // Every earlier EDRAM access, ROV or not, is now ordered before later ones.
  edram_rov_layout_.reset();
  if (stats_enabled_) ++edram_barriers_;
}
D3D12_CPU_DESCRIPTOR_HANDLE D3D12Render::Cpu(uint32_t i, bool s) {
  auto h = (s ? samplers_ : views_)->GetCPUDescriptorHandleForHeapStart();
  h.ptr += i * provider_.GetDescriptorSize(s ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); return h;
}
D3D12_GPU_DESCRIPTOR_HANDLE D3D12Render::Gpu(uint32_t i, bool s) {
  auto h = (s ? samplers_ : views_)->GetGPUDescriptorHandleForHeapStart();
  h.ptr += i * provider_.GetDescriptorSize(s ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); return h;
}
uint32_t D3D12Render::Views(uint32_t n) { Require(view_count_ + n <= 65536, "view heap exhausted"); auto i = view_count_; view_count_ += n; return i; }
uint32_t D3D12Render::Samplers(uint32_t n) { Require(sampler_count_ + n <= 2048, "sampler heap exhausted"); auto i = sampler_count_; sampler_count_ += n; return i; }
D3D12_GPU_DESCRIPTOR_HANDLE D3D12Render::MemoryTable(bool memexport) {
  auto& base=arena_memory_tables_[unsigned(memexport)];
  if (base!=UINT32_MAX) return Gpu(base);
  base=Views(3);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format=DXGI_FORMAT_R32_TYPELESS;
  srv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER; srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.NumElements=GuestMemoryMirror::kSize/4; srv.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;
  device_->CreateShaderResourceView(memexport ? nullptr : memory_.Get(),&srv,Cpu(base));
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav{}; uav.Format=DXGI_FORMAT_R32_TYPELESS;
  uav.ViewDimension=D3D12_UAV_DIMENSION_BUFFER; uav.Buffer.NumElements=GuestMemoryMirror::kSize/4;
  uav.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;
  device_->CreateUnorderedAccessView(memexport ? memory_.Get() : nullptr,nullptr,&uav,Cpu(base+1));
  uav.Format=DXGI_FORMAT_R32_UINT; uav.Buffer.NumElements=UINT(edram_size_/4)+8; uav.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_NONE;
  device_->CreateUnorderedAccessView(edram_.Get(),nullptr,&uav,Cpu(base+2));
  return Gpu(base);
}
D3D12Render::D3D12Render(rex::ui::d3d12::D3D12Provider& provider) : provider_(provider), device_(provider.GetDevice()) {
  stats_enabled_ = REXCVAR_GET(native_stats);
  texture_cache_enabled_ = REXCVAR_GET(native_texture_cache);
  texture_cpu_ = REXCVAR_GET(native_texture_cpu);
  pipeline_workers_ = std::make_unique<PipelineWorkers>(NativePipelineThreads());
  device_->QueryInterface(IID_PPV_ARGS(&debug_));
  D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
  Check(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)), "feature query");
  Require(options.ROVsSupported, "D3D12 rasterizer ordered views required");
  Require(options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_2, "D3D12 resource binding tier 2 required");
  Check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator_)), "allocator");
  Check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(), nullptr, IID_PPV_ARGS(&list_)), "command list");
  cmd_.Attach(list_.Get());
  Check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "fence");
  event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr); Require(event_ != nullptr, "fence event");
  memory_ = Buffer(0x20000000, D3D12_HEAP_TYPE_DEFAULT, memory_state_, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  edram_ = Buffer(edram_size_ + sizeof(SampleCounts), D3D12_HEAP_TYPE_DEFAULT, edram_state_, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
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
  const auto pso_dump = REXCVAR_GET(native_pso_dump);
  if (!pso_dump.empty()) {
    std::filesystem::create_directories(pso_dump);
    std::ofstream out(std::filesystem::path(pso_dump) / "root.bin", std::ios::binary);
    out.write(static_cast<const char*>(blob->GetBufferPointer()), blob->GetBufferSize());
    Require(bool(out), "PSO root diagnostic write failed");
  }
  Check(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root_)), "draw root");
  pipeline_store_.SetRoot(blob->GetBufferPointer(), blob->GetBufferSize(), root_.Get());
  p[1].ShaderVisibility = p[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_DOMAIN;
  blob.Reset(); errors.Reset();
  Check(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors), "domain root serialization");
  Check(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root_tess_)), "domain root");
  pipeline_store_.SetRoot(blob->GetBufferPointer(), blob->GetBufferSize(), root_tess_.Get());
  BeginGpuTimestamp();
}
// Watches reference cache entries: retire them before the cache is destroyed.
D3D12Render::~D3D12Render() {
  // Submitted resources and pending guest publication outlive their fence,
  // even when the caller did not explicitly drain before teardown.
  try { Wait(serial_); PublishCompleted(); }
  catch (const std::exception& e) { REX_FATAL("Native D3D12: unsafe render teardown: {}", e.what()); }
  if (stats_enabled_) try { CollectGpuTimestamps(); LogSubmissionStats(); } catch (const std::exception& e) {
    REXLOG_WARN("Native final submission stats unavailable: {}", e.what());
  }
  scaled_.Clear();
  mirror_.reset(); if (event_) CloseHandle(event_);
}
void D3D12Render::BeginGpuTimestamp() {
  if (!stats_enabled_) return;
  if (!timestamps_) {
    D3D12_QUERY_HEAP_DESC qd{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, uint32_t(batches_.size() * 2), 0};
    Check(device_->CreateQueryHeap(&qd, IID_PPV_ARGS(&timestamps_)), "timestamp heap");
    timestamp_readback_ = Buffer(batches_.size() * 16, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    Check(provider_.GetDirectQueue()->GetTimestampFrequency(&timestamp_frequency_), "timestamp frequency");
  }
  cmd_.EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, batch_index_ * 2);
}
void D3D12Render::EndGpuTimestamp() {
  if (!stats_enabled_) return;
  cmd_.EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, batch_index_ * 2 + 1);
  cmd_.ResolveQueryData(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, batch_index_ * 2, 2,
      timestamp_readback_.Get(), batch_index_ * 16);
  timestamp_serials_[batch_index_] = serial_ + 1;
}
void D3D12Render::CollectGpuTimestamps() {
  // Completed submissions in serial order: busy time inside each, idle gaps
  // between consecutive ones on the same direct queue.
  const uint64_t completed = fence_->GetCompletedValue();
  if (!timestamps_ || completed == UINT64_MAX) return;
  for (;;) {
    unsigned slot = unsigned(batches_.size());
    for (unsigned i = 0; i < batches_.size(); ++i)
      if (timestamp_serials_[i] && timestamp_serials_[i] <= completed &&
          (slot == batches_.size() || timestamp_serials_[i] < timestamp_serials_[slot])) slot = i;
    if (slot == batches_.size()) return;
    timestamp_serials_[slot] = 0;
    void* mapped; D3D12_RANGE range{size_t(slot) * 16, size_t(slot) * 16 + 16};
    Check(timestamp_readback_->Map(0, &range, &mapped), "map timestamps");
    uint64_t ticks[2]; std::memcpy(ticks, static_cast<const uint8_t*>(mapped) + range.Begin, sizeof(ticks));
    D3D12_RANGE none{}; timestamp_readback_->Unmap(0, &none);
    if (ticks[1] >= ticks[0]) gpu_busy_ticks_ += ticks[1] - ticks[0];
    if (gpu_last_end_ && ticks[0] >= gpu_last_end_) gpu_idle_ticks_ += ticks[0] - gpu_last_end_;
    gpu_last_end_ = ticks[1]; ++gpu_timed_;
  }
}
void D3D12Render::LogSubmissionStats() {
  auto ms = [&](uint64_t ticks) { return timestamp_frequency_ ? double(ticks) * 1000.0 / double(timestamp_frequency_) : 0.0; };
  REXLOG_INFO("Native queue submissions={} asynchronous={} fence_waits={} fence_ms={} gpu_timed={} gpu_busy_ms={:.3f} gpu_idle_between_ms={:.3f} sampler_reuses={}",
      serial_, async_submissions_, fence_waits_, flush_wait_ns_ / 1000000, gpu_timed_, ms(gpu_busy_ticks_), ms(gpu_idle_ticks_), sampler_reuses_);
  REXLOG_INFO("Native costs presents={} draws={} resolves={} empty_resolves={} overlap_flushes={} submits={} texture_uploads={} texture_bytes={} texture_ms={} resolve_ms={} flush_ms={} cache_hits={} (flush overlaps resolve)",
      present_count_, draw_count_, resolve_count_, empty_resolve_count_, overlap_flushes_, serial_, texture_uploads_, texture_upload_bytes_, texture_ns_ / 1000000,
      resolve_ns_ / 1000000, flush_ns_ / 1000000, texture_cache_hits_);
  REXLOG_INFO("Native draw costs draw_ms={} guest_upload_ms={} guest_upload_bytes={} gpu_texture_loads={} cpu_texture_loads={} flush_wait_ms={} (draw includes textures; uploads are mirror pages the CPU wrote)",
      draw_ns_ / 1000000, guest_upload_ns_ / 1000000, guest_upload_bytes_, gpu_texture_loads_, cpu_texture_loads_, flush_wait_ns_ / 1000000);
  REXLOG_INFO("Native EDRAM barriers edram_barriers={} edram_layout_barriers={} draws={}",
      edram_barriers_, edram_layout_barriers_, draw_count_);
  std::string causes;
  for (unsigned i = 0; i < kFlushCauses; ++i) causes += fmt::format(" {}", flush_causes_[i]);
  REXLOG_INFO("Native flush causes (guest_sync present overlap capacity drain){}", causes);
}
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
  // pso_ms/pso_thread_cpu_ms sum the creations on pipeline workers (they
  // overlap each other and recording); pso_replay_ms is what submissions spent
  // binding pending pipelines (waiting for them) and replaying deferred commands.
  REXLOG_INFO("Native pipeline totals creations={} workers={} shader_ms={} pso_ms={} pso_thread_cpu_ms={} pso_replay_ms={} deferred_submissions={} draw_ms={} texture_ms={} upload_ms={} resolve_ms={} fence_ms={}",
      pipeline_creations_.load(), pipeline_workers_ ? pipeline_workers_->threads() : 0, shader_ns_ / 1000000,
      pipeline_ns_async_.load() / 1000000, pipeline_thread_ns_async_.load() / 1000000, pipeline_wait_ns_ / 1000000,
      pipeline_waits_, draw_ns_ / 1000000, texture_ns_ / 1000000,
      guest_upload_ns_ / 1000000, resolve_ns_ / 1000000, flush_wait_ns_ / 1000000);
  REXLOG_INFO("Native pipeline disk hits={} rejects={}", pipeline_disk_hits_.load(), pipeline_disk_rejects_.load());
  REXLOG_INFO("Native flush causes guest_sync={} (event={} packet={} wait={} buffer_end={} present={}) present={} overlap={} capacity={} drain={} wait_ms={}",
      flush_causes_[kFlushGuestSync], sync_sources_[kGuestSyncEvent], sync_sources_[kGuestSyncPacket], sync_sources_[kGuestSyncWait], sync_sources_[kGuestSyncBufferEnd], sync_sources_[kGuestSyncPresent], flush_causes_[kFlushPresent], flush_causes_[kFlushOverlap], flush_causes_[kFlushCapacity], flush_causes_[kFlushDrain], flush_wait_ns_ / 1000000);
  std::string opcodes;
  for (unsigned i = 0; i < 256; ++i) if (sync_opcodes_[i]) opcodes += fmt::format(" {:02X}:{}", i, sync_opcodes_[i]);
  REXLOG_INFO("Native guest sync opcodes{}", opcodes);
  REXLOG_INFO("Native overlap sites indices={} cpu_texture={} resolve_rect={} packet={} mirror_upload_bytes={}", overlap_sites_[kReadIndices], overlap_sites_[kReadCpuTexture], overlap_sites_[kReadResolveRect], overlap_sites_[kReadPacket], mirror_ ? mirror_->uploaded_bytes() : 0);
  frame_ms_.clear(); window_start_ = now;
}
void D3D12Render::ReplayCommands() {
  if (!cmd_.deferred()) return;
  // Replay binds each pending pipeline in recording order, waiting only for
  // pipelines these commands use; later compiles keep running.
  const auto start = std::chrono::steady_clock::now();
  cmd_.Replay();
  ++pipeline_waits_;
  pipeline_wait_ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
}
void D3D12Render::Flush(FlushCause cause) {
  Timer timer(stats_enabled_, flush_ns_);
  if (stats_enabled_) ++flush_causes_[cause];
  EndGpuTimestamp();
  ReplayCommands();
  Check(list_->Close(), "close"); ID3D12CommandList* lists[]{list_.Get()};
  provider_.GetDirectQueue()->ExecuteCommandLists(1, lists);
  Check(provider_.GetDirectQueue()->Signal(fence_.Get(), ++serial_), "signal");
  // Keep staging, epochs and scaled tickets with the submitted arena. CPU
  // publication waits only at an observable dependency or arena reuse; the
  // direct queue still orders GPU RAW/WAR/WAW on memory/EDRAM/textures.
  if (cause == kFlushDrain || cause == kFlushGuestSync || cause == kFlushOverlap)
    Wait(serial_);
  else if (stats_enabled_) ++async_submissions_;
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
  RecycleBatch();
  if (stats_enabled_ && serial_ % 30 == 0) LogSubmissionStats();
}
void D3D12Render::PublishCompleted() {
  if (!pending_readback_batches_) return;
  const auto completed = fence_->GetCompletedValue();
  Require(completed != UINT64_MAX, "render device removed");
  for (;;) {
    RetiredBatch* batch = nullptr;
    for (auto& candidate : batches_)
      if ((!candidate.writeback.empty() || !candidate.scaled_readbacks.empty()) &&
          (!batch || candidate.serial < batch->serial)) batch = &candidate;
    if (!batch || batch->serial > completed) return;
    if (!batch->writeback.empty()) {
      void* mapped; D3D12_RANGE range{0, size_t(batch->readback_used)};
      Check(batch->readback->Map(0, &range, &mapped), "map resolve readback");
      batch->writeback.Complete([&](const GuestWriteback::Write& w) {
        mirror_->CopyReadbackToGuest(w.address, static_cast<const uint8_t*>(mapped) + w.offset,
                                     w.length, w.cpu_generations);
      });
      D3D12_RANGE none{}; batch->readback->Unmap(0, &none);
    }
    for (const auto& scaled : batch->scaled_readbacks) {
      const size_t bytes = size_t(scaled.ticket.length)*scale_x_*scale_y_;
      void* mapped; D3D12_RANGE range{size_t(scaled.offset), size_t(scaled.offset)+bytes};
      Check(scaled.buffer->Map(0, &range, &mapped), "map scaled resolve readback");
      scaled_.Complete(scaled.ticket, {static_cast<const uint8_t*>(mapped)+scaled.offset, bytes},
                        scaled.pixel_log2, *guest_memory_);
      D3D12_RANGE none{}; scaled.buffer->Unmap(0, &none);
    }
    batch->scaled_readbacks.clear();
    batch->readback_used = 0;
    --pending_readback_batches_;
  }
}
bool D3D12Render::GuestWritesOverlap(uint32_t address, uint32_t length) const {
  if (!length) return false;
  if (writeback_.Overlaps(address, length)) return true;
  for (const auto& batch : batches_) if (batch.writeback.Overlaps(address, length)) return true;
  return false;
}
SampleCounts D3D12Render::ReadSampleCounts(bool counting_after) {
  if (!sample_counts_readback_) {
    sample_counts_readback_ = Buffer(sizeof(SampleCounts), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    const auto initial = Upload(sample_counts_.data(), sizeof(sample_counts_));
    Transition(edram_.Get(), edram_state_, D3D12_RESOURCE_STATE_COPY_DEST);
    cmd_.CopyBufferRegion(edram_.Get(), edram_size_, initial.resource.Get(), initial.offset, sizeof(SampleCounts));
    Transition(edram_.Get(), edram_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    edram_rov_layout_.reset();
  }
  if (sample_counting_) {
    // The transition orders all preceding ROV atomics, including draws in
    // earlier recording arenas. The fence retires the copy before publication.
    Transition(edram_.Get(), edram_state_, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmd_.CopyBufferRegion(sample_counts_readback_.Get(), 0, edram_.Get(), edram_size_, sizeof(SampleCounts));
    Transition(edram_.Get(), edram_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    edram_rov_layout_.reset();
    Flush(kFlushGuestSync);
    void* mapped = nullptr;
    const D3D12_RANGE range{0, sizeof(SampleCounts)}, none{};
    Check(sample_counts_readback_->Map(0, &range, &mapped), "map sample counts");
    std::memcpy(sample_counts_.data(), mapped, sizeof(sample_counts_));
    sample_counts_readback_->Unmap(0, &none);
  }
  sample_counting_ = counting_after;
  return sample_counts_;
}
void D3D12Render::SetSampleCounts(const SampleCounts& counts, bool visibility_only) {
  if (!sample_counts_readback_) ReadSampleCounts(false);
  const auto upload = Upload(counts.data(), sizeof(counts));
  Transition(edram_.Get(), edram_state_, D3D12_RESOURCE_STATE_COPY_DEST);
  cmd_.CopyBufferRegion(edram_.Get(), edram_size_, upload.resource.Get(), upload.offset, sizeof(counts));
  Transition(edram_.Get(), edram_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  edram_rov_layout_.reset();
  sample_counts_ = counts;
  visibility_only_ = visibility_only;
}
void D3D12Render::Wait(uint64_t serial) {
  const auto completed = fence_->GetCompletedValue();
  Require(completed != UINT64_MAX, "render device removed");
  if (completed >= serial) return;
  Timer wait(stats_enabled_, flush_wait_ns_);
  if (stats_enabled_) ++fence_waits_;
  Check(fence_->SetEventOnCompletion(serial, event_), "fence completion");
  Require(WaitForSingleObject(event_, 5000) == WAIT_OBJECT_0, "render GPU timeout");
}
void D3D12Render::RecycleBatch() {
  auto& retired = batches_[batch_index_];
  Require(retired.writeback.empty() && retired.scaled_readbacks.empty(), "overwriting pending readback batch");
  retired.serial = serial_;
  retired.readback = std::move(readback_);
  retired.readback_size = std::exchange(readback_size_, 0);
  retired.readback_used = std::exchange(readback_used_, 0);
  std::swap(retired.writeback, writeback_);
  retired.scaled_readbacks.swap(scaled_readbacks_);
  if (!retired.writeback.empty() || !retired.scaled_readbacks.empty()) ++pending_readback_batches_;
  retired.allocator = std::move(allocator_);
  retired.views = std::move(views_); retired.samplers = std::move(samplers_);
  retired.transient = std::move(transient_);
  retired.uploads = std::move(upload_pages_); retired.scratch = std::move(scratch_);
  batch_index_ = (batch_index_ + 1) % batches_.size();
  auto& next = batches_[batch_index_];
  Wait(next.serial);
  PublishCompleted();
  if (stats_enabled_) CollectGpuTimestamps();
  allocator_ = std::move(next.allocator);
  views_ = std::move(next.views); samplers_ = std::move(next.samplers);
  transient_ = std::move(next.transient);
  upload_pages_ = std::move(next.uploads); scratch_ = std::move(next.scratch);
  Require(next.writeback.empty() && next.scaled_readbacks.empty(), "reusing pending readback batch");
  readback_ = std::move(next.readback);
  readback_size_ = std::exchange(next.readback_size, 0);
  next.serial = 0;
  transient_.clear(); view_count_ = sampler_count_ = 0;
  arena_samplers_.clear();
  arena_memory_tables_.fill(UINT32_MAX);
  for (auto& s : scratch_) s.used = 0;
  uint64_t retained = 0;
  for (auto& page : upload_pages_) { page.used = 0; retained += page.size; }
  if (retained > 64 * 1024 * 1024) upload_pages_.clear();
  upload_page_index_ = 0;
  if (allocator_) Check(allocator_->Reset(), "reset allocator");
  else Check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator_)), "batch allocator");
  if (!views_) {
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 65536, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    Check(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&views_)), "batch views");
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER; hd.NumDescriptors = 2048;
    Check(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&samplers_)), "batch samplers");
  }
  Check(list_->Reset(allocator_.Get(), nullptr), "reset list");
  BeginGpuTimestamp();
}
void D3D12Render::SyncGuestRead(uint32_t address, uint64_t length, ReadSite site) {
  const auto extent = uint32_t(std::min<uint64_t>(length, GuestMemoryMirror::kSize));
  if (!GuestWritesOverlap(address, extent)) return;
  if (stats_enabled_) { ++overlap_flushes_; ++overlap_sites_[site]; }
  if (writeback_.Overlaps(address, extent)) Flush(kFlushCapacity);
  uint64_t serial = 0;
  for (const auto& batch : batches_)
    if (batch.writeback.Overlaps(address, extent)) serial = std::max(serial, batch.serial);
  if (serial) { Wait(serial); PublishCompleted(); }
}
void D3D12Render::ReserveReadback(uint64_t bytes) {
  if (!bytes) return;
  if (readback_used_ + bytes > readback_size_) {
    // Only an old, nonempty allocation can be referenced by recorded work.
    if (readback_used_) Flush(kFlushCapacity);
    if (bytes > readback_size_) {
      readback_size_ = std::max(bytes, 16ull * 1024 * 1024);
      readback_ = Buffer(readback_size_, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    }
  }
}
void D3D12Render::QueueGuestWrite(uint32_t address, uint32_t length, const std::vector<uint64_t>* snapshot_epochs) {
  if (!length) return;
  const uint64_t slot = (uint64_t(length) + 255) & ~uint64_t(255);
  Require(uint64_t(address) + length <= GuestMemoryMirror::kSize && readback_used_ + slot <= readback_size_,
          "GPU writeback not reserved or outside guest physical memory");
  // Make the GPU mirror authoritative and retire texture aliases immediately.
  // The COPY_SOURCE transition also orders UAV stores from the draw/resolve.
  std::vector<uint64_t> cpu_generations;
  mirror_->RangeWrittenByGpu(address, length, &cpu_generations, snapshot_epochs ? std::span<const uint64_t>(*snapshot_epochs) : std::span<const uint64_t>());
  Transition(memory_.Get(), memory_state_, D3D12_RESOURCE_STATE_COPY_SOURCE);
  cmd_.CopyBufferRegion(readback_.Get(), readback_used_, memory_.Get(), address, length);
  writeback_.Add(address, length, readback_used_, std::move(cpu_generations));
  readback_used_ += slot;
}
GuestMemoryMirror& D3D12Render::Mirror(rex::memory::Memory& memory) {
  guest_memory_ = &memory;
  if (!mirror_) mirror_ = std::make_unique<GuestMemoryMirror>(memory, [this](uint32_t a, uint32_t n) { UploadGuest(a, n); });
  scaled_.Attach(*mirror_);
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
  cmd_.CopyBufferRegion(memory_.Get(), address, data.resource.Get(), data.offset, bytes);
}
}
