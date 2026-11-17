#include "command_processor.h"
#include <gpu/xenos/xenos_graphics_driver_base.h>
#include <gpu/xenos/xenos_settings.h>
#include <gpu/xenos/pipeline/shader/shader.h>
#include <rex/chrono/frame_timing.h>
#include <rex/chrono.h>
#include <rex/xenos/display_overlay.h>
#include <gpu/xenos/xenos_graphics_driver_base.h>
#include <gpu/xenos/physical_memory.h>
#include <rex/logging/log_category.h>
#include <rex/logging/logger.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>

#include <rex/memory/stream.h>
#include <rex/platform/sync.h>
#include "../memory/physical_write.h"
#include "../capture/capture_backend.h"
#include "../quality/settings.h"
#include <cstring>
#include "../../supervisor/fs_io.h"






namespace x360n::native {
[[noreturn]] void Unsupported(const char* message) {
  // The interface has no asynchronous failure return. Terminate this experimental
  // child with a distinct error instead of retiring an unimplemented draw or
  // hanging the guest on a completion it can never receive. No dialog/fallback.
  REXLOG_ERROR("Native unsupported/failure: {} (exit 78, no fallback)", message);
  std::fprintf(stderr, "Native unsupported/failure: %s (exit 78, no fallback)\n", message);
  std::fflush(stderr);
  REX_LOGGER().Flush();
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
    const auto quality = GetNativeQualitySettings();
    // Fetch offsets of resolution-scaled textures stay in guest texels, as on
    // the console. Host-texel offsets (the SDK default) give GPU-side texture
    // encoders a fraction of their footprint: Motocross Madness's GPU DXT1
    // terrain became 4x4 flat blocks. Read by both translators (patch 0267).
    scanout_ = CaptureBackend::Wrap(factory_(*GetGraphicsDriver().GetProvider(), GetXenosSettings(), write_watch_), physical_memory_, GetXenosSettings(), {
        std::filesystem::path(GetNativeSettings().capture.Get()), std::filesystem::path(GetNativeSettings().frame_profile.Get()),
        std::filesystem::path(GetNativeSettings().capture_trigger.Get()), GetNativeSettings().capture_skip.Get(), GetNativeSettings().capture_frames.Get(), quality.scale_x, quality.scale_y,
        GetGraphicsDriver().GetName().find("vulkan") != std::string::npos ? "vulkan" : "d3d12", GetXenosSettings().GetEDRAMTileCount()});
    REXLOG_INFO("{}: independent scanout and guest renderer ready", GetGraphicsDriver().GetName());
    // Without a title timing profile (which selects its own cadence before
    // the GPU starts), Native Enhanced above 30 FPS measures how many vblanks
    // the title waits per present and runs guest vblank at target * that.
    using Timing = rex::chrono::FrameTiming;
    generic_cadence_ = Timing::Enhanced() && rex::chrono::GetNativeFrameSettings().framerate.Get() > 30 && !Timing::GuestVblankRate();
    if (generic_cadence_)
      REXLOG_INFO("Native Enhanced: generic target {} FPS; guest vblank follows the title's measured present interval; system/audio time unchanged",
                  rex::chrono::GetNativeFrameSettings().framerate.Get());
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
  if (index >= rex::xenos::RegisterFile::kRegisterCount)
    Unsupported("GPU register outside Native state");
  using namespace rex::gpu;
using namespace rex::xenos;
using rex::xenos::RegisterFile;
namespace xenos = rex::xenos;
  if (index >= static_cast<uint32_t>(rex::xenos::Register::SCRATCH_REG0) && index <= static_cast<uint32_t>(rex::xenos::Register::SCRATCH_REG7) &&
      (register_file_->values[static_cast<uint32_t>(rex::xenos::Register::SCRATCH_UMSK)] & (1u << (index - static_cast<uint32_t>(rex::xenos::Register::SCRATCH_REG0))))) {
    const uint64_t address = uint64_t(register_file_->values[static_cast<uint32_t>(rex::xenos::Register::SCRATCH_ADDR)]) + (index - static_cast<uint32_t>(rex::xenos::Register::SCRATCH_REG0)) * 4;
    if (address + 4 > 0x20000000) Unsupported("scratch writeback outside guest physical memory");
    CompleteGuestWrites(kGuestSyncPacket);
  }
  rex::gpu::CommandProcessor::WriteRegister(index, value);
}
void CommandProcessor::IssueSwap(uint32_t, uint32_t width, uint32_t height) {
  last_swap_succeeded_ = false;
  try {
    if (!scanout_ || !GetGraphicsDriver().GetHostPresenter()) throw std::runtime_error("scanout/presenter unavailable");
    const auto post_effect = GetActualSwapPostEffect() == SwapPostEffect::kFXAAExtreme
        ? ScanoutPostEffect::kFxaaExtreme : GetActualSwapPostEffect() == SwapPostEffect::kFXAA
        ? ScanoutPostEffect::kFxaa : ScanoutPostEffect::kNone;
    const auto texture = DecodeScanout(register_file_->GetTextureFetch(0), width, height);
    uint32_t lut[128 * 3];
    // Original D3D presentation uses PWL for both RGB10A2 fetch formats.
    if (texture.rgb10) {
      for (unsigned i = 0; i < 128 * 3; ++i) {
        const auto& c = GetGammaRampPWLRGB()[i];
        lut[i] = c.base | (c.delta << 16);
      }
    } else for (unsigned i = 0; i < 256; ++i) {
      const auto& c = GetGammaRamp256EntryTable()[i];
      lut[i] = c.color_10_red | (c.color_10_green << 10) | (c.color_10_blue << 20);
    }
    // Same D1OVL decoding and display coordinates as the pinned Xenos swap.
    ScanoutOverlay overlay{};
    const auto plane = rex::gpu::DecodeDisplayOverlay(*register_file_);
    if (plane.valid) {
      const auto& mode = title_info_.video_mode;
      overlay = {DecodeScanout(plane.fetch, 0, 0), plane.x, plane.y, plane.right, plane.bottom,
                 plane.offset_x, plane.offset_y, std::max(1u, uint32_t(mode.width)),
                 std::max(1u, uint32_t(mode.height))};
    }
    rex::chrono::FrameTiming::PacePresentation();
    auto pixels = physical_memory_.GetReadOnlyPhysicalSpan({texture.address, texture.bytes});
    if (!pixels) throw std::runtime_error(pixels.error().GetDescription());
    std::span<const std::byte> overlay_pixels;
    if (plane.valid) {
      auto bytes = physical_memory_.GetReadOnlyPhysicalSpan({overlay.surface.address, overlay.surface.bytes});
      if (!bytes) throw std::runtime_error(bytes.error().GetDescription());
      overlay_pixels = *bytes;
    }
    scanout_->Present(*GetGraphicsDriver().GetHostPresenter(), texture, pixels->data(), lut,
                      plane.valid ? &overlay : nullptr, overlay_pixels.data(), post_effect);
    last_swap_succeeded_ = true;
    rex::chrono::FrameTiming::NotifySourceFrame();
    UpdateGenericCadence();
  } catch (const std::exception& e) { Unsupported(e.what()); }
}
void CommandProcessor::UpdateGenericCadence() {
  using Timing = rex::chrono::FrameTiming;
  using Clock = rex::Clock;
  if (!generic_cadence_) return;
  const uint32_t current = Timing::GuestVblankRate();
  if (current != cadence_rate_) {  // A title profile selected its own cadence.
    generic_cadence_ = false;
    REXLOG_INFO("Native Enhanced: guest vblank {} Hz selected by the title profile; generic cadence off", current);
    return;
  }
  uint32_t hz = current;
  if (!hz) {
    const auto& mode = title_info_.video_mode;
    hz = std::max(1u, uint32_t(std::lround(float(mode.refresh_rate))));
  }
  const uint64_t now = Clock::QueryGuestTickCount();
  const uint64_t interval = last_present_tick_ && now > last_present_tick_ ? now - last_present_tick_ : 0;
  last_present_tick_ = now;
  const uint32_t fps = rex::chrono::GetNativeFrameSettings().framerate.Get();
  const uint32_t next = cadence_.OnPresent(interval, Clock::GetGuestTickFrequency(), hz, fps);
  if (next == cadence_rate_) return;
  Timing::SetGuestVblankRate(next);
  cadence_rate_ = next;
  REXLOG_INFO("Native Enhanced: title presents every {} vblank(s); guest vblank {} Hz for {} FPS",
              cadence_.interval(), next, fps);
}
rex::gpu::Shader* CommandProcessor::LoadShader(rex::xenos::ShaderType type,
    uint32_t address, const uint32_t* data, uint32_t count) {
  try { return scanout_->LoadShader(type, data, count); }
  catch (const std::exception& e) { Unsupported(e.what()); }
}
bool CommandProcessor::ShouldIssueDraw(uint32_t condition) const {
  using namespace rex::gpu;
using namespace rex::xenos;
using rex::xenos::RegisterFile;
namespace xenos = rex::xenos;
  draw_visibility_failed_ = !VisibilityDrawEnabled(condition, register_file_->values[static_cast<uint32_t>(rex::xenos::Register::PA_SC_VIZ_QUERY_STATUS_0)],
                                                   register_file_->values[static_cast<uint32_t>(rex::xenos::Register::PA_SC_VIZ_QUERY_STATUS_1)]);
  // Visibility rejects rasterization after vertex processing. Keep guest VS
  // memory exports even for invisible geometry, as for ordinary face culling.
  return !draw_visibility_failed_ ||
      (register_file_->Get<reg::RB_MODECONTROL>().edram_mode != xenos::EDRAMMode::kCopy &&
       GetActiveVertexShader() && GetActiveVertexShader()->GetMemExportEMWritten());
}
bool CommandProcessor::IssueDraw(rex::xenos::PrimitiveType primitive, uint32_t count, IndexBuffer* indices, bool) {
  try {
    if (!GetNativeSettings().trace.Get().empty()) {
      x360n::io::create_directories(GetNativeSettings().trace.Get());
      x360n::io::ofstream out(std::filesystem::path(GetNativeSettings().trace.Get()) / "last-draw-registers.bin", std::ios::binary);
      out.write(reinterpret_cast<const char*>(register_file_->values), sizeof(register_file_->values));
      REXLOG_INFO("Native draw primitive={} count={} indexed={} mode={}", uint32_t(primitive), count, indices != nullptr,
          uint32_t(register_file_->Get<rex::xenos::reg::RB_MODECONTROL>().edram_mode));
    }
    if (register_file_->Get<rex::xenos::reg::RB_MODECONTROL>().edram_mode == rex::xenos::EDRAMMode::kCopy) return IssueCopy();
    DrawIndices native_indices{};
    if (indices) {
      if (indices->length > 0x20000000) Unsupported("index buffer exceeds guest physical memory");
      native_indices = {indices->format, indices->endianness, indices->count, indices->guest_base, uint32_t(indices->length)};
    }
    using namespace rex::gpu;
using namespace rex::xenos;
using rex::xenos::RegisterFile;
namespace xenos = rex::xenos;
    if (draw_visibility_failed_) {
      auto vertex_regs = std::make_unique<RegisterFile>(*register_file_);
      auto mode = vertex_regs->Get<reg::RB_MODECONTROL>();
      mode.edram_mode = xenos::EDRAMMode::kNoOperation;
      vertex_regs->values[static_cast<uint32_t>(rex::xenos::Register::RB_MODECONTROL)] = mode.value;
      scanout_->Draw(*vertex_regs, physical_memory_, GetActiveVertexShader(), nullptr,
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
      mode.edram_mode = xenos::EDRAMMode::kDepthOnly;
      query_regs->values[static_cast<uint32_t>(rex::xenos::Register::RB_MODECONTROL)] = mode.value;
      auto depth = query_regs->Get<reg::RB_DEPTHCONTROL>();
      depth.z_write_enable = depth.stencil_enable = depth.backface_enable = 0;
      query_regs->values[static_cast<uint32_t>(rex::xenos::Register::RB_DEPTHCONTROL)] = depth.value;
      query_regs->values[static_cast<uint32_t>(rex::xenos::Register::RB_COLOR_MASK)] = 0;
      auto color = query_regs->Get<reg::RB_COLORCONTROL>();
      color.alpha_test_enable = color.alpha_to_mask_enable = 0;
      query_regs->values[static_cast<uint32_t>(rex::xenos::Register::RB_COLORCONTROL)] = color.value;
      const bool query_only = viz.kill_pix_post_hi_z || viz.kill_pix_post_detail_mask;
      const auto saved = scanout_->ReadSampleCounts(true);
      scanout_->SetSampleCounts({}, true);
      scanout_->Draw(*query_regs, physical_memory_, GetActiveVertexShader(), nullptr, primitive, count,
          indices ? &native_indices : nullptr,
          query_only ? DrawPurpose::kVisibilityWithVertexExports : DrawPurpose::kVisibility);
      const auto visible = scanout_->ReadSampleCounts(occlusion_queries_.active() != 0);
      scanout_->SetSampleCounts(saved, false);
      visibility_queries_.Accumulate(viz.viz_query_id, (visible[4] | visible[5]) != 0);
      if (query_only) return true;
    }
    scanout_->Draw(*register_file_, physical_memory_, GetActiveVertexShader(), GetActivePixelShader(), primitive, count, indices ? &native_indices : nullptr);
    return true;
  } catch (const std::exception& e) { Unsupported(e.what()); }
}
bool CommandProcessor::IssueCopy() {
  try { scanout_->Resolve(*register_file_, physical_memory_); return true; }
  catch (const std::exception& e) { Unsupported(e.what()); }
}
bool CommandProcessor::ExecutePacketType3_VIZ_QUERY(rex::memory::RingBuffer* reader, uint32_t, uint32_t count) {
  try {
    using namespace rex::gpu;
using namespace rex::xenos;
using rex::xenos::RegisterFile;
namespace xenos = rex::xenos;
    if (count != 1) throw std::runtime_error("Native visibility query: invalid payload size");
    const auto control = reader->ReadAndSwap<uint32_t>();
    const unsigned id = control & 63;
    if (!(control & 0x100)) {
      visibility_queries_.Begin(id);
      WriteRegister(static_cast<uint32_t>(rex::xenos::Register::VGT_EVENT_INITIATOR), xenos::VIZQUERY_START);
    } else {
      const bool visible = visibility_queries_.End(id);
      const auto status = id < 32 ? static_cast<uint32_t>(rex::xenos::Register::PA_SC_VIZ_QUERY_STATUS_0) : static_cast<uint32_t>(rex::xenos::Register::PA_SC_VIZ_QUERY_STATUS_1);
      const uint32_t bit = uint32_t(1) << (id & 31);
      WriteRegister(status, (register_file_->values[status] & ~bit) | (visible ? bit : 0));
      WriteRegister(static_cast<uint32_t>(rex::xenos::Register::VGT_EVENT_INITIATOR), xenos::VIZQUERY_END);
    }
    return true;
  } catch (const std::exception& e) { Unsupported(e.what()); }
}
bool CommandProcessor::ExecutePacketType3_EVENT_WRITE_ZPD(rex::memory::RingBuffer* reader, uint32_t, uint32_t count) {
  try {
    using namespace rex::gpu;
using namespace rex::xenos;
using rex::xenos::RegisterFile;
namespace xenos = rex::xenos;
    if (count != 1) throw std::runtime_error("Native ZPD: invalid payload size");
    const uint32_t initiator = reader->ReadAndSwap<uint32_t>();
    if ((initiator & 0x3F) != xenos::ZPASS_DONE)
      throw std::runtime_error("Native ZPD: unsupported event initiator");
    WriteRegister(static_cast<uint32_t>(rex::xenos::Register::VGT_EVENT_INITIATOR), initiator & 0x3F);
    const auto address = register_file_->values[static_cast<uint32_t>(rex::xenos::Register::RB_SAMPLE_COUNT_ADDR)];
    if ((address & 3) || uint64_t(address) + sizeof(SampleCounts) > 0x20000000)
      throw std::runtime_error("Native ZPD: report outside aligned guest physical memory");
    // Earlier resolves may alias the report. Publish those first, then observe
    // the driver's pending token and retire real GPU counters before writing.
    CompleteGuestWrites(kGuestSyncEvent, xenos::PM4_EVENT_WRITE_ZPD);
    SampleCounts report;
    auto read = physical_memory_.CopyFromPhysical(address, std::as_writable_bytes(std::span(report)));
    if (!read) throw std::runtime_error(read.error().GetDescription());
    const bool end = OcclusionQueries::IsEnd(report);
    const bool counting_after = occlusion_queries_.CountingAfter(end);
    report = scanout_->ReadSampleCounts(counting_after);
    // The canonical physical writer publishes the report to every watched alias.
    auto written = physical_memory_.CopyToPhysical(address, std::as_bytes(std::span(report)));
    if (!written) throw std::runtime_error(written.error().GetDescription());
    occlusion_queries_.Commit(end);
    return true;
  } catch (const std::exception& e) { Unsupported(e.what()); }
}
}
