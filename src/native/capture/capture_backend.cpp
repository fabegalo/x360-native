#include "capture_backend.h"
#include "../commands/memexport.h"
#include <rex/graphics/pipeline/shader/shader.h>
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/graphics/register_file.h>
#include <rex/graphics/util/draw.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>
#include <rex/chrono/frame_layout.h>
#include <rex/ui/presenter.h>
#include <fmt/format.h>
#include <xxhash.h>
#include <fmt/format.h>
#include <algorithm>
namespace x360n::native {
using namespace rex::graphics;
using namespace rex::graphics::xenos;
using capture::Record;
namespace {
double Ms(std::chrono::steady_clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }
// Only allocated guest physical pages are read: garbage fetch constants of
// unused bindings must not make the capture touch memory the title lacks.
bool Accessible(rex::memory::Memory& memory, uint32_t address, uint32_t length) {
  if (!length || uint64_t(address) + length > capture::kPhysicalSize) return false;
  for (uint64_t page = address & ~uint64_t(4095); page < uint64_t(address) + length; page += 4096) {
    bool allocated = false;
    for (uint32_t base : {0xE0000000u, 0xA0000000u, 0xC0000000u}) {
      auto* heap = memory.LookupHeap(base);
      const uint32_t origin = memory.GetPhysicalAddress(base);
      if (page < origin || page >= uint64_t(origin) + heap->heap_size()) continue;
      uint32_t protect = 0;
      if (heap->QueryProtect(base + uint32_t(page - origin), &protect) && protect) { allocated = true; break; }
    }
    if (!allocated) return false;
  }
  return true;
}
void TextureRanges(const RegisterFile& regs, const Shader* shader, std::vector<std::pair<uint32_t, uint32_t>>& out) {
  if (!shader) return;
  for (const auto& binding : shader->texture_bindings()) {
    const auto fetch = regs.GetTextureFetch(binding.fetch_constant);
    if (fetch.type != FetchConstantType::kTexture) continue;
    uint32_t width, height, depth, base, mip, low, high;
    texture_util::GetSubresourcesFromFetchConstant(fetch, &width, &height, &depth, &base, &mip, &low, &high);
    const auto layout = texture_util::GetGuestTextureLayout(fetch.dimension, fetch.pitch, width + 1, height + 1,
        depth + 1, fetch.tiled, fetch.format, fetch.packed_mips, true, high);
    if (base) out.emplace_back(base << 12, layout.base.level_data_extent_bytes);
    if (mip && high) out.emplace_back(mip << 12, layout.mips_total_extent_bytes);
  }
}
}
BackendCounters operator-(const BackendCounters& a, const BackendCounters& b) {
  BackendCounters c = a;
  c.submissions -= b.submissions; c.fence_waits -= b.fence_waits;
  c.fence_wait_ns -= b.fence_wait_ns; c.gpu_busy_ns -= b.gpu_busy_ns;
  return c;
}
std::string FrameStatsCsvHeader() {
  return "frame,frame_ms,backend_ms,outside_ms,capture_ms,profile_ms,draws,draw_ms,resolves,resolve_ms,guest_syncs,packet_reads,sync_ms,"
         "sample_reads,sample_ms,shaders,shader_ms,present_ms,submissions,fence_waits,fence_wait_ms,gpu_busy_ms,counters";
}
std::string FrameStatsCsvRow(const FrameStats& f) {
  const auto& c = f.counters;
  // Unmeasured counters are empty fields, never zero readings.
  auto measured = [&](double value, bool valid) { return valid ? fmt::format("{:.3f}", value) : std::string(); };
  // Outside the renderer and this decorator: PM4 decoding and waiting for the title.
  const double outside = std::max(0.0, f.frame_ms - f.backend_ms - f.capture_ms - f.profile_ms);
  return fmt::format("{},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{},{:.3f},{},{:.3f},{},{},{:.3f},{},{:.3f},{},{:.3f},{:.3f},{},{},{},{},{}",
      f.frame, f.frame_ms, f.backend_ms, outside, f.capture_ms, f.profile_ms, f.draws, f.draw_ms, f.resolves,
      f.resolve_ms, f.guest_syncs, f.packet_reads, f.sync_ms, f.sample_reads, f.sample_ms, f.shaders, f.shader_ms,
      f.present_ms, c.submissions, measured(double(c.fence_waits), c.measured),
      measured(double(c.fence_wait_ns) / 1e6, c.measured), measured(double(c.gpu_busy_ns) / 1e6, c.gpu_timed),
      c.measured ? (c.gpu_timed ? "gpu" : "cpu") : "off");
}

std::unique_ptr<ScanoutBackend> CaptureBackend::Wrap(std::unique_ptr<ScanoutBackend> inner,
    rex::memory::Memory& memory, Options options) {
  if (options.capture.empty() && options.profile.empty()) return inner;
  return std::make_unique<CaptureBackend>(std::move(inner), memory, std::move(options));
}
CaptureBackend::CaptureBackend(std::unique_ptr<ScanoutBackend> inner, rex::memory::Memory& memory, Options options)
    : inner_(std::move(inner)), memory_(memory), options_(std::move(options)) {
  if (!options_.profile.empty()) {
    if (options_.profile.has_parent_path()) std::filesystem::create_directories(options_.profile.parent_path());
    profile_ = std::fopen(options_.profile.string().c_str(), "w");
    if (!profile_) throw std::runtime_error("Native frame profile: cannot create " + options_.profile.string());
    std::fprintf(profile_, "%s\n", FrameStatsCsvHeader().c_str());
  }
  if (!options_.capture.empty()) {
    if (options_.scale_x != 1 || options_.scale_y != 1)
      throw std::runtime_error("Native frame capture requires resolution scale 1 (scaled resolve caches are not captured)");
    if (!options_.frames) throw std::runtime_error("Native frame capture: native_capture_frames must be positive");
    if (!options_.trigger.empty())
      REXLOG_INFO("Native frame capture: {} frame(s) to {} once {} exists", options_.frames,
                  options_.capture.string(), options_.trigger.string());
    else
      REXLOG_INFO("Native frame capture: {} frame(s) after {} present(s) to {}", options_.frames,
                  options_.skip_frames, options_.capture.string());
    if (!options_.skip_frames && options_.trigger.empty()) StartRecording();
  }
}
CaptureBackend::~CaptureBackend() {
  if (writer_) {
    try { FinishRecording(); } catch (const std::exception& e) { REXLOG_ERROR("Native frame capture: {}", e.what()); }
  }
  if (profile_) std::fclose(profile_);
}
bool HashImage(const rex::ui::RawImage& image, capture::Hash& out) {
  return capture::HashRgbxImage(image.data.data(), image.data.size(), image.width, image.height, image.stride, out);
}
CaptureBackend::CaptureWork::~CaptureWork() {
  if (--self.capture_depth_ == 0) self.frame_.capture_ms += Ms(Clock::now() - start);
}
CaptureBackend::Scope::~Scope() {
  const double ms = Ms(Clock::now() - start);
  total += ms;
  self.frame_.backend_ms += ms;
}

void CaptureBackend::StartRecording() {
  CaptureWork work(*this);
  capture::Header header{};
  std::memcpy(header.magic, capture::kMagic, sizeof(header.magic));
  header.version = capture::kVersion;
  header.register_count = RegisterFile::kRegisterCount;
  header.scale_x = options_.scale_x;
  header.scale_y = options_.scale_y;
  std::snprintf(header.backend, sizeof(header.backend), "%s", options_.backend.c_str());
  writer_ = std::make_unique<capture::Writer>(options_.capture, header);
  // Persistent GPU state that guest memory does not hold. Waits for the GPU.
  const auto edram = inner_->ReadEdram();
  const uint64_t blob = writer_->Blob(edram.bytes.data(), edram.bytes.size());
  writer_->Begin(Record::kEdram, 12);
  writer_->Put(blob);
  writer_->Put(uint32_t(edram.sample_counting));
  registers_ = std::make_unique<uint32_t[]>(RegisterFile::kRegisterCount);  // Replay starts from zeros.
  emitted_ranges_.clear();
  gpu_pages_.clear();
  for (auto& [shader, info] : shaders_) info.emitted = false;
  written_ranges_.clear();
  recorded_frames_ = 0;
  REXLOG_INFO("Native frame capture: recording after present {}", presents_);
}
void CaptureBackend::AbortRecording(const std::string& reason) {
  writer_.reset();
  registers_.reset();
  emitted_ranges_.clear();
  capture_done_ = true;
  std::error_code error;
  std::filesystem::remove(options_.capture, error);
  REXLOG_ERROR("Native frame capture rejected after {} frame(s): {}; {} was not kept", recorded_frames_, reason,
               options_.capture.string());
}
void CaptureBackend::FinishRecording() {
  CaptureWork work(*this);
  writer_->Finish(recorded_frames_);
  REXLOG_INFO("Native frame capture: {} frame(s), {} MiB ({} MiB guest memory) written to {}", recorded_frames_,
              writer_->bytes() >> 20, writer_->blob_bytes() >> 20, options_.capture.string());
  writer_.reset();
  registers_.reset();
  emitted_ranges_.clear();
  capture_done_ = true;
}
void CaptureBackend::EmitRegisters(const RegisterFile& regs) {
  std::vector<uint32_t> changes;
  for (uint32_t i = 0; i < RegisterFile::kRegisterCount; ++i)
    if (regs.values[i] != registers_[i]) { changes.push_back(i); changes.push_back(regs.values[i]); registers_[i] = regs.values[i]; }
  if (changes.empty()) return;
  writer_->Begin(Record::kRegisters, 4 + changes.size() * 4);
  writer_->Put(uint32_t(changes.size() / 2));
  writer_->Put(changes.data(), changes.size() * 4);
}
uint32_t CaptureBackend::EmitShader(Shader* shader) {
  if (!shader) return capture::kNoShader;
  auto found = shaders_.find(shader);
  if (found == shaders_.end()) throw std::logic_error("Native frame capture: draw with an unrecorded shader");
  auto& info = found->second;
  if (!info.emitted) {
    writer_->Begin(Record::kShader, 12 + info.words.size() * 4);
    writer_->Put(info.id); writer_->Put(info.type); writer_->Put(uint32_t(info.words.size()));
    writer_->Put(info.words.data(), info.words.size() * 4);
    info.emitted = true;
  }
  return info.id;
}
void CaptureBackend::EmitRange(uint32_t address, uint32_t length) {
  if (!length || uint64_t(address) + length > capture::kPhysicalSize) return;
  // Pages the GPU produced during the recording are results, not inputs: the
  // replay must produce them itself, or every comparison would be satisfied
  // by the recorded bytes. A page whose bytes changed after its GPU write was
  // published (hashed at the frame end) was rewritten by the CPU: an input again.
  const uint64_t end = uint64_t(address) + length;
  uint64_t run = address;
  for (uint64_t page = address & ~uint64_t(capture::kPageSize - 1); page < end; page += capture::kPageSize) {
    auto owned = gpu_pages_.find(uint32_t(page / capture::kPageSize));
    if (owned == gpu_pages_.end()) continue;
    if (owned->second.hashed) {
      const auto bytes = memory_.TranslatePhysical(uint32_t(page));
      if (capture::HashBytes(bytes, capture::kPageSize) != owned->second.hash) {
        gpu_pages_.erase(owned);
        continue;
      }
    }
    if (run < page) EmitRun(uint32_t(run), uint32_t(page - run));
    run = std::max<uint64_t>(run, page + capture::kPageSize);
  }
  if (run < end) EmitRun(uint32_t(run), uint32_t(end - run));
}
void CaptureBackend::MarkGpuWritten(uint32_t address, uint32_t length) {
  if (!length || uint64_t(address) + length > capture::kPhysicalSize) return;
  for (uint64_t page = address / capture::kPageSize; page <= (uint64_t(address) + length - 1) / capture::kPageSize; ++page)
    gpu_pages_[uint32_t(page)] = {};
}
void CaptureBackend::EmitRun(uint32_t address, uint32_t length) {
  const auto key = std::make_pair(address, length);
  auto found = emitted_ranges_.find(key);
  if (found == emitted_ranges_.end() && !Accessible(memory_, address, length)) return;
  const uint8_t* bytes = memory_.TranslatePhysical(address);
  const auto hash = capture::HashBytes(bytes, length);
  // Unchanged since it was last recorded: the replay already holds these bytes.
  if (found != emitted_ranges_.end() && found->second == hash) return;
  const uint64_t blob = writer_->Blob(bytes, length);
  writer_->Begin(Record::kMemory, 16);
  writer_->Put(address); writer_->Put(length); writer_->Put(blob);
  emitted_ranges_[key] = hash;
}
void CaptureBackend::EmitDrawInputs(const RegisterFile& regs, Shader* vs, Shader* ps, const DrawIndices* indices,
                                    DrawPurpose purpose) {
  std::vector<std::pair<uint32_t, uint32_t>> ranges;
  if (indices) ranges.emplace_back(indices->guest_base, indices->length);
  if (vs) for (const auto& binding : vs->vertex_bindings()) {
    const auto fetch = regs.GetVertexFetch(binding.fetch_constant);
    if (fetch.type == FetchConstantType::kVertex) ranges.emplace_back(fetch.address << 2, fetch.size << 2);
  }
  TextureRanges(regs, vs, ranges);
  TextureRanges(regs, ps, ranges);
  for (const auto& [address, length] : ranges) EmitRange(address, length);
  // MEMEXPORT destinations are GPU results, checked at the end of the frame.
  const bool vertex_exports = purpose == DrawPurpose::kGuest || purpose == DrawPurpose::kVisibilityWithVertexExports;
  std::vector<MemExportRange> exports;
  for (Shader* shader : {vs, ps}) {
    if (!shader || !shader->memexport_eM_written() || (shader == vs && !vertex_exports)) continue;
    auto [known, inserted] = memexport_known_.try_emplace(shader, false);
    if (inserted) known->second = MemExportAddressesKnown(*shader);
    AddMemExportRanges(regs, *shader, known->second, exports);
  }
  for (const auto& range : exports)
    if (range.length && range.length < capture::kPhysicalSize) {
      written_ranges_.emplace_back(range.address, range.length);
      MarkGpuWritten(range.address, range.length);
    }
}

void CaptureBackend::Present(rex::ui::Presenter& presenter, const ScanoutTexture& texture, const void* pixels,
    const uint32_t* lut, const ScanoutOverlay* overlay, const void* overlay_pixels, ScanoutPostEffect effect) {
  if (recording()) {
    CaptureWork work(*this);
    EmitRange(texture.address, texture.bytes);
    if (overlay) EmitRange(overlay->surface.address, overlay->surface.bytes);
  }
  {
    Scope scope{*this, frame_.present_ms};
    inner_->Present(presenter, texture, pixels, lut, overlay, overlay_pixels, effect);
  }
  if (recording()) {
    CaptureWork work(*this);
    capture::PresentRecord record{};
    const ScanoutTexture& t = texture;
    const uint32_t fields[9]{t.address, t.width, t.height, t.pitch, t.tiled, t.endian, t.swizzle, t.bytes, uint32_t(t.rgb10)};
    std::copy(std::begin(fields), std::end(fields), record.texture);
    record.post_effect = uint32_t(effect);
    record.has_overlay = overlay != nullptr;
    if (overlay) {
      const auto& s = overlay->surface;
      const uint32_t o[17]{s.address, s.width, s.height, s.pitch, s.tiled, s.endian, s.swizzle, s.bytes, uint32_t(s.rgb10),
          overlay->x, overlay->y, overlay->right, overlay->bottom, overlay->offset_x, overlay->offset_y,
          overlay->display_width, overlay->display_height};
      std::copy(std::begin(o), std::end(o), record.overlay);
    }
    record.lut.assign(lut, lut + (t.rgb10 ? 128 * 3 : 256));
    // Results after the backend published them for presentation.
    if (Accessible(memory_, t.address, t.bytes))
      record.frontbuffer = capture::HashBytes(memory_.TranslatePhysical(t.address), t.bytes);
    std::sort(written_ranges_.begin(), written_ranges_.end());
    written_ranges_.erase(std::unique(written_ranges_.begin(), written_ranges_.end()), written_ranges_.end());
    for (const auto& [address, length] : written_ranges_)
      if (Accessible(memory_, address, length))
        record.written.push_back({address, length, capture::HashBytes(memory_.TranslatePhysical(address), length)});
    written_ranges_.clear();
    // GPU results of this and earlier frames as published now: a later
    // difference means the CPU wrote the page.
    for (auto& [page, state] : gpu_pages_)
      if (Accessible(memory_, page * capture::kPageSize, capture::kPageSize))
        state = {true, capture::HashBytes(memory_.TranslatePhysical(page * capture::kPageSize), capture::kPageSize)};
    // The composed output (gamma, overlay, post effect) of this present.
    // A frame without it cannot be checked visually: the capture is rejected.
    rex::ui::RawImage image;
    if (!presenter.CaptureGuestOutput(image)) {
      AbortRecording("the presenter returned no composed image for the frame");
    } else if (!HashImage(image, record.output)) {
      AbortRecording(fmt::format("the presenter returned an invalid {}x{} image (stride {}, {} bytes)", image.width,
                                 image.height, image.stride, image.data.size()));
    } else {
      record.output_width = image.width;
      record.output_height = image.height;
      const auto aspect = rex::chrono::FrameLayout::DisplayAspect();
      record.aspect_x = aspect.x;
      record.aspect_y = aspect.y;
      writer_->Present(record);
      if (++recorded_frames_ == options_.frames) FinishRecording();
    }
  }
  ++presents_;
  EndProfileFrame();
  if (!capture_done_ && !recording() && !options_.capture.empty()) {
    std::error_code error;
    if (options_.trigger.empty() ? presents_ == options_.skip_frames : std::filesystem::exists(options_.trigger, error))
      StartRecording();
  }
}
void CaptureBackend::EndProfileFrame() {
  if (!profile_) return;
  const auto now = Clock::now();
  const auto counters = inner_->Counters();
  frame_.frame = presents_;
  frame_.frame_ms = Ms(now - frame_start_);
  frame_.counters = counters - frame_counters_;
  std::fprintf(profile_, "%s\n", FrameStatsCsvRow(frame_).c_str());
  std::fflush(profile_);
  frame_ = {};
  frame_counters_ = counters;
  frame_start_ = now;
  // This profiler's own row costs fall inside the next frame's interval.
  frame_.profile_ms = Ms(Clock::now() - now);
}
void CaptureBackend::Drain() {
  if (recording()) { CaptureWork work(*this); writer_->Simple(Record::kDrain, {}); }
  Scope scope{*this, frame_.sync_ms};
  inner_->Drain();
}
void CaptureBackend::CompleteGuestWrites(GuestSync source, uint32_t opcode) {
  if (recording()) { CaptureWork work(*this); writer_->Simple(Record::kCompleteGuestWrites, {uint32_t(source), opcode}); }
  ++frame_.guest_syncs;
  Scope scope{*this, frame_.sync_ms};
  inner_->CompleteGuestWrites(source, opcode);
}
void CaptureBackend::SyncGuestRead(uint32_t address, uint32_t length, uint32_t opcode) {
  if (recording()) { CaptureWork work(*this); writer_->Simple(Record::kSyncGuestRead, {address, length, opcode}); }
  ++frame_.packet_reads;
  Scope scope{*this, frame_.sync_ms};
  inner_->SyncGuestRead(address, length, opcode);
}
Shader* CaptureBackend::LoadShader(ShaderType type, const uint32_t* data, uint32_t count) {
  Shader* shader;
  {
    ++frame_.shaders;
    Scope scope{*this, frame_.shader_ms};
    shader = inner_->LoadShader(type, data, count);
  }
  // Every shader is kept from startup: draws may use shaders loaded before recording.
  if (!options_.capture.empty() && !capture_done_ && shader && !shaders_.contains(shader))
    shaders_.emplace(shader, ShaderInfo{next_shader_++, uint32_t(type), std::vector<uint32_t>(data, data + count)});
  return shader;
}
void CaptureBackend::Draw(const RegisterFile& regs, rex::memory::Memory& memory, Shader* vs, Shader* ps,
    PrimitiveType primitive, uint32_t count, const DrawIndices* indices, DrawPurpose purpose) {
  if (recording()) {
    CaptureWork work(*this);
    EmitRegisters(regs);
    const uint32_t vs_id = EmitShader(vs), ps_id = EmitShader(ps);
    EmitDrawInputs(regs, vs, ps, indices, purpose);
    const DrawIndices i = indices ? *indices : DrawIndices{};
    writer_->Simple(Record::kDraw, {vs_id, ps_id, uint32_t(primitive), count, uint32_t(purpose), uint32_t(indices != nullptr),
        uint32_t(i.format), uint32_t(i.endianness), i.count, i.guest_base, i.length});
  }
  ++frame_.draws;
  Scope scope{*this, frame_.draw_ms};
  inner_->Draw(regs, memory, vs, ps, primitive, count, indices, purpose);
}
void CaptureBackend::Resolve(const RegisterFile& regs, rex::memory::Memory& memory) {
  if (recording()) {
    CaptureWork work(*this);
    EmitRegisters(regs);
    const auto rect = regs.GetVertexFetch(0);
    if (rect.type == FetchConstantType::kVertex) EmitRange(rect.address << 2, std::max<uint32_t>(rect.size << 2, 24u));
    draw_util::ResolveInfo info;
    if (draw_util::GetResolveInfo(regs, memory, 1, 1, false, false, info) && info.copy_dest_extent_length) {
      const uint32_t begin = info.copy_dest_base & ~4095u;
      const uint64_t end = (uint64_t(info.copy_dest_extent_start) + info.copy_dest_extent_length + 4095) & ~uint64_t(4095);
      // Bytes of the destination pages the resolve does not write still reach the mirror.
      EmitRange(begin, uint32_t(std::min<uint64_t>(end, capture::kPhysicalSize) - begin));
      written_ranges_.emplace_back(info.copy_dest_extent_start, info.copy_dest_extent_length);
      MarkGpuWritten(info.copy_dest_extent_start, info.copy_dest_extent_length);
    }
    writer_->Simple(Record::kResolve, {});
  }
  ++frame_.resolves;
  Scope scope{*this, frame_.resolve_ms};
  inner_->Resolve(regs, memory);
}
SampleCounts CaptureBackend::ReadSampleCounts(bool counting_after) {
  SampleCounts result;
  {
    ++frame_.sample_reads;
    Scope scope{*this, frame_.sample_ms};
    result = inner_->ReadSampleCounts(counting_after);
  }
  if (recording()) {
    CaptureWork work(*this);
    writer_->Begin(Record::kReadSampleCounts, 4 + sizeof(result));
    writer_->Put(uint32_t(counting_after));
    writer_->Put(result);
  }
  return result;
}
void CaptureBackend::SetSampleCounts(const SampleCounts& counts, bool visibility_only) {
  if (recording()) {
    CaptureWork work(*this);
    writer_->Begin(Record::kSetSampleCounts, sizeof(counts) + 4);
    writer_->Put(counts);
    writer_->Put(uint32_t(visibility_only));
  }
  Scope scope{*this, frame_.sample_ms};
  inner_->SetSampleCounts(counts, visibility_only);
}
}
