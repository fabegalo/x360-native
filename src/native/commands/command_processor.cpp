#include "command_processor.h"
#include <rex/graphics/graphics_system.h>
#include <rex/graphics/pipeline/shader/shader.h>
#include <rex/chrono/frame_timing.h>
#include <rex/graphics/display_overlay.h>
#include <rex/kernel/xboxkrnl/video.h>
#include <rex/logging.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <rex/cvar.h>
#include <rex/memory/ring_buffer.h>
#include <rex/thread/mutex.h>
#include "../memory/physical_write.h"
#include <cstring>
REXCVAR_DEFINE_STRING(native_trace, "", "GPU/Native", "Optional Native diagnostic directory");
namespace x360n::native {
[[noreturn]] void Unsupported(const char* message) {
  // The interface has no asynchronous failure return. Terminate this experimental
  // child with a distinct error instead of retiring an unimplemented draw or
  // hanging the guest on a completion it can never receive. No dialog/fallback.
  REXLOG_ERROR("Native unsupported/failure: {} (exit 78, no fallback)", message);
  std::fprintf(stderr, "Native unsupported/failure: %s (exit 78, no fallback)\n", message);
  std::fflush(stderr);
  rex::FlushLogging();
  std::_Exit(78);
}
namespace { CommandProcessor* active_processor = nullptr; }
void CompleteGuestWritesBeforePacket(GuestSync source, uint32_t opcode) { if (active_processor) active_processor->CompleteGuestWrites(source, opcode); }
void SyncGuestReadBeforePacket(uint32_t address, uint32_t length, uint32_t opcode) {
  if (active_processor) active_processor->SyncGuestRead(address, length, opcode);
}
void WaitForIdleBeforePacket() { if (active_processor) active_processor->WaitForIdle(); }
void CommandProcessor::WaitForIdle() {
  try { if (scanout_) scanout_->Drain(); }
  catch (const std::exception& e) { Unsupported(e.what()); }
}
void CommandProcessor::SyncGuestRead(uint32_t address, uint32_t length, uint32_t opcode) {
  try { if (scanout_) scanout_->SyncGuestRead(address, length, opcode); }
  catch (const std::exception& e) { Unsupported(e.what()); }
}
void CommandProcessor::CompleteGuestWrites(GuestSync source, uint32_t opcode) {
  try { if (scanout_) scanout_->CompleteGuestWrites(source, opcode); }
  catch (const std::exception& e) { Unsupported(e.what()); }
}
bool CommandProcessor::SetupContext() {
  try {
    active_processor = this;
    scanout_ = factory_(*graphics_system_->provider());
    REXLOG_INFO("{}: independent scanout and guest renderer ready", graphics_system_->name());
    return true;
  } catch (const std::exception& e) { Unsupported(e.what()); }
}
void CommandProcessor::ShutdownContext() {
  try { if (scanout_) scanout_->Drain(); }
  catch (const std::exception& e) { Unsupported(e.what()); }
  active_processor = nullptr;
  scanout_.reset();
}
void CommandProcessor::InitializeShaderStorage(const std::filesystem::path& path, uint32_t title_id, bool) {
  if (scanout_) scanout_->InitializeShaderStorage(path, title_id);
}
void CommandProcessor::WriteRegister(uint32_t index, uint32_t value) {
  if (index >= rex::graphics::RegisterFile::kRegisterCount)
    Unsupported("GPU register outside Native state");
  using namespace rex::graphics;
  if (index >= XE_GPU_REG_SCRATCH_REG0 && index <= XE_GPU_REG_SCRATCH_REG7 &&
      (register_file_->values[XE_GPU_REG_SCRATCH_UMSK] & (1u << (index - XE_GPU_REG_SCRATCH_REG0)))) {
    const uint64_t address = uint64_t(register_file_->values[XE_GPU_REG_SCRATCH_ADDR]) + (index - XE_GPU_REG_SCRATCH_REG0) * 4;
    if (address + 4 > 0x20000000) Unsupported("scratch writeback outside guest physical memory");
    CompleteGuestWrites(kGuestSyncPacket);
  }
  rex::graphics::CommandProcessor::WriteRegister(index, value);
}
void CommandProcessor::OnGuestMemoryWrite(uint32_t address, uint32_t length) {
  if (!length) return;
  if (uint64_t(address) + length > 0x20000000) Unsupported("GPU packet write outside guest physical memory");
  // Shared packet handlers write through the unprotected physical view.
  // Notify every alias, including Native's mirror and texture watches.
  NotifyPhysicalWrite(*memory_, address, length);
}
void CommandProcessor::IssueSwap(uint32_t, uint32_t width, uint32_t height) {
  last_swap_succeeded_ = false;
  try {
    if (!scanout_ || !graphics_system_->presenter()) throw std::runtime_error("scanout/presenter unavailable");
    const auto post_effect = GetActualSwapPostEffect() == SwapPostEffect::kFxaaExtreme
        ? ScanoutPostEffect::kFxaaExtreme : GetActualSwapPostEffect() == SwapPostEffect::kFxaa
        ? ScanoutPostEffect::kFxaa : ScanoutPostEffect::kNone;
    const auto texture = DecodeScanout(register_file_->GetTextureFetch(0), width, height);
    uint32_t lut[128 * 3];
    // Original D3D presentation uses PWL for both RGB10A2 fetch formats.
    if (texture.rgb10) {
      for (unsigned i = 0; i < 128 * 3; ++i) {
        const auto& c = gamma_ramp_pwl_rgb()[i];
        lut[i] = c.base | (c.delta << 16);
      }
    } else for (unsigned i = 0; i < 256; ++i) {
      const auto& c = gamma_ramp_256_entry_table()[i];
      lut[i] = c.color_10_red | (c.color_10_green << 10) | (c.color_10_blue << 20);
    }
    // Same D1OVL decoding and display coordinates as the pinned Xenos swap.
    ScanoutOverlay overlay{};
    const auto plane = rex::graphics::DecodeDisplayOverlay(*register_file_);
    if (plane.valid) {
      rex::system::X_VIDEO_MODE mode;
      rex::kernel::xboxkrnl::VdQueryVideoMode(&mode);
      overlay = {DecodeScanout(plane.fetch, 0, 0), plane.x, plane.y, plane.right, plane.bottom,
                 plane.offset_x, plane.offset_y, std::max(1u, uint32_t(mode.display_width)),
                 std::max(1u, uint32_t(mode.display_height))};
    }
    rex::chrono::FrameTiming::PacePresentation();
    scanout_->Present(*graphics_system_->presenter(), texture,
                      memory_->TranslatePhysical(texture.address), lut,
                      plane.valid ? &overlay : nullptr,
                      plane.valid ? memory_->TranslatePhysical(overlay.surface.address) : nullptr, post_effect);
    last_swap_succeeded_ = true;
    rex::chrono::FrameTiming::NotifySourceFrame();
  } catch (const std::exception& e) { Unsupported(e.what()); }
}
rex::graphics::Shader* CommandProcessor::LoadShader(rex::graphics::xenos::ShaderType type,
    uint32_t address, const uint32_t* data, uint32_t count) {
  try { return scanout_->LoadShader(type, data, count); }
  catch (const std::exception& e) { Unsupported(e.what()); }
}
bool CommandProcessor::ShouldIssueDraw(uint32_t condition) const {
  using namespace rex::graphics;
  draw_visibility_failed_ = !VisibilityDrawEnabled(condition, register_file_->values[XE_GPU_REG_PA_SC_VIZ_QUERY_STATUS_0],
                                                   register_file_->values[XE_GPU_REG_PA_SC_VIZ_QUERY_STATUS_1]);
  // Visibility rejects rasterization after vertex processing. Keep guest VS
  // memory exports even for invisible geometry, as for ordinary face culling.
  return !draw_visibility_failed_ ||
      (register_file_->Get<reg::RB_MODECONTROL>().edram_mode != xenos::EdramMode::kCopy &&
       active_vertex_shader() && active_vertex_shader()->memexport_eM_written());
}
bool CommandProcessor::IssueDraw(rex::graphics::xenos::PrimitiveType primitive, uint32_t count, IndexBufferInfo* indices, bool) {
  try {
    if (!REXCVAR_GET(native_trace).empty()) {
      std::filesystem::create_directories(REXCVAR_GET(native_trace));
      std::ofstream out(std::filesystem::path(REXCVAR_GET(native_trace)) / "last-draw-registers.bin", std::ios::binary);
      out.write(reinterpret_cast<const char*>(register_file_->values), sizeof(register_file_->values));
      REXLOG_INFO("Native draw primitive={} count={} indexed={} mode={}", uint32_t(primitive), count, indices != nullptr,
          uint32_t(register_file_->Get<rex::graphics::reg::RB_MODECONTROL>().edram_mode));
    }
    if (register_file_->Get<rex::graphics::reg::RB_MODECONTROL>().edram_mode == rex::graphics::xenos::EdramMode::kCopy) return IssueCopy();
    DrawIndices native_indices{};
    if (indices) {
      if (indices->length > 0x20000000) Unsupported("index buffer exceeds guest physical memory");
      native_indices = {indices->format, indices->endianness, indices->count, indices->guest_base, uint32_t(indices->length)};
    }
    using namespace rex::graphics;
    if (draw_visibility_failed_) {
      auto vertex_regs = std::make_unique<RegisterFile>(*register_file_);
      auto mode = vertex_regs->Get<reg::RB_MODECONTROL>();
      mode.edram_mode = xenos::EdramMode::kNoOperation;
      vertex_regs->values[XE_GPU_REG_RB_MODECONTROL] = mode.value;
      scanout_->Draw(*vertex_regs, *memory_, active_vertex_shader(), nullptr,
          primitive, count, indices ? &native_indices : nullptr);
      draw_visibility_failed_ = false;
      return true;
    }
    const auto viz = register_file_->Get<reg::PA_SC_VIZ_QUERY>();
    if (viz.viz_query_ena) {
      // Visibility is decided before the guest pixel shader and cannot be
      // inferred from its color writes, kill or alpha-test result. Probe real
      // geometry against the current depth buffer without modifying EDRAM.
      // Ordinary rendering then executes once, with its original side effects.
      auto query_regs = std::make_unique<RegisterFile>(*register_file_);
      auto mode = query_regs->Get<reg::RB_MODECONTROL>();
      mode.edram_mode = xenos::EdramMode::kDepthOnly;
      query_regs->values[XE_GPU_REG_RB_MODECONTROL] = mode.value;
      auto depth = query_regs->Get<reg::RB_DEPTHCONTROL>();
      depth.z_write_enable = depth.stencil_enable = depth.backface_enable = 0;
      query_regs->values[XE_GPU_REG_RB_DEPTHCONTROL] = depth.value;
      query_regs->values[XE_GPU_REG_RB_COLOR_MASK] = 0;
      auto color = query_regs->Get<reg::RB_COLORCONTROL>();
      color.alpha_test_enable = color.alpha_to_mask_enable = 0;
      query_regs->values[XE_GPU_REG_RB_COLORCONTROL] = color.value;
      const bool query_only = viz.kill_pix_post_hi_z || viz.kill_pix_post_detail_mask;
      const auto saved = scanout_->ReadSampleCounts(true);
      scanout_->SetSampleCounts({}, true);
      scanout_->Draw(*query_regs, *memory_, active_vertex_shader(), nullptr, primitive, count,
          indices ? &native_indices : nullptr,
          query_only ? DrawPurpose::kVisibilityWithVertexExports : DrawPurpose::kVisibility);
      const auto visible = scanout_->ReadSampleCounts(occlusion_queries_.active() != 0);
      scanout_->SetSampleCounts(saved, false);
      visibility_queries_.Accumulate(viz.viz_query_id, (visible[4] | visible[5]) != 0);
      if (query_only) return true;
    }
    scanout_->Draw(*register_file_, *memory_, active_vertex_shader(), active_pixel_shader(), primitive, count, indices ? &native_indices : nullptr);
    return true;
  } catch (const std::exception& e) { Unsupported(e.what()); }
}
bool CommandProcessor::IssueCopy() {
  try { scanout_->Resolve(*register_file_, *memory_); return true; }
  catch (const std::exception& e) { Unsupported(e.what()); }
}
bool CommandProcessor::ExecutePacketType3_VIZ_QUERY(rex::memory::RingBuffer* reader, uint32_t, uint32_t count) {
  try {
    using namespace rex::graphics;
    if (count != 1) throw std::runtime_error("Native visibility query: invalid payload size");
    const auto control = reader->ReadAndSwap<uint32_t>();
    const unsigned id = control & 63;
    if (!(control & 0x100)) {
      visibility_queries_.Begin(id);
      WriteRegister(XE_GPU_REG_VGT_EVENT_INITIATOR, xenos::VIZQUERY_START);
    } else {
      const bool visible = visibility_queries_.End(id);
      const auto status = id < 32 ? XE_GPU_REG_PA_SC_VIZ_QUERY_STATUS_0 : XE_GPU_REG_PA_SC_VIZ_QUERY_STATUS_1;
      const uint32_t bit = uint32_t(1) << (id & 31);
      WriteRegister(status, (register_file_->values[status] & ~bit) | (visible ? bit : 0));
      WriteRegister(XE_GPU_REG_VGT_EVENT_INITIATOR, xenos::VIZQUERY_END);
    }
    return true;
  } catch (const std::exception& e) { Unsupported(e.what()); }
}
bool CommandProcessor::ExecutePacketType3_EVENT_WRITE_ZPD(rex::memory::RingBuffer* reader, uint32_t, uint32_t count) {
  try {
    using namespace rex::graphics;
    if (count != 1) throw std::runtime_error("Native ZPD: invalid payload size");
    const uint32_t initiator = reader->ReadAndSwap<uint32_t>();
    if ((initiator & 0x3F) != xenos::ZPASS_DONE)
      throw std::runtime_error("Native ZPD: unsupported event initiator");
    WriteRegister(XE_GPU_REG_VGT_EVENT_INITIATOR, initiator & 0x3F);
    const auto address = register_file_->values[XE_GPU_REG_RB_SAMPLE_COUNT_ADDR];
    if ((address & 3) || uint64_t(address) + sizeof(SampleCounts) > 0x20000000)
      throw std::runtime_error("Native ZPD: report outside aligned guest physical memory");
    // Earlier resolves may alias the report. Publish those first, then observe
    // the driver's pending token and retire real GPU counters before writing.
    CompleteGuestWrites(kGuestSyncEvent, xenos::PM4_EVENT_WRITE_ZPD);
    SampleCounts report;
    auto* destination = memory_->TranslatePhysical(address);
    std::memcpy(report.data(), destination, sizeof(report));
    const bool end = OcclusionQueries::IsEnd(report);
    const bool counting_after = occlusion_queries_.CountingAfter(end);
    report = scanout_->ReadSampleCounts(counting_after);
    // TranslatePhysical bypasses the watched guest aliases. Retire their
    // cached copies before publishing these new bytes, as a guest write does.
    OnGuestMemoryWrite(address, sizeof(report));
    std::memcpy(destination, report.data(), sizeof(report));
    occlusion_queries_.Commit(end);
    return true;
  } catch (const std::exception& e) { Unsupported(e.what()); }
}
}
