#include "command_processor.h"
#include <rex/graphics/graphics_system.h>
#include <rex/logging.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <rex/cvar.h>
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
bool CommandProcessor::SetupContext() {
  try {
    scanout_ = factory_(*graphics_system_->provider());
    REXLOG_INFO("Native: independent D3D12 scanout and guest renderer ready");
    return true;
  } catch (const std::exception& e) { Unsupported(e.what()); }
}
void CommandProcessor::ShutdownContext() {
  try { if (scanout_) scanout_->Drain(); }
  catch (const std::exception& e) { Unsupported(e.what()); }
  scanout_.reset();
}
void CommandProcessor::WriteRegister(uint32_t index, uint32_t value) {
  if (index >= rex::graphics::RegisterFile::kRegisterCount)
    Unsupported("GPU register outside Native state");
  // The shared PM4 decoder publishes event completions to guest memory after
  // this register write. Retire Native work before that publication.
  if (index == rex::graphics::XE_GPU_REG_VGT_EVENT_INITIATOR && scanout_) {
    try { scanout_->Drain(); }
    catch (const std::exception& e) { Unsupported(e.what()); }
  }
  rex::graphics::CommandProcessor::WriteRegister(index, value);
}
void CommandProcessor::IssueSwap(uint32_t, uint32_t width, uint32_t height) {
  last_swap_succeeded_ = false;
  try {
    if (!scanout_ || !graphics_system_->presenter()) throw std::runtime_error("scanout/presenter unavailable");
    if (GetActualSwapPostEffect() != SwapPostEffect::kNone)
      throw std::runtime_error("Native post-processing is not implemented");
    const auto texture = DecodeScanout(register_file_->GetTextureFetch(0), width, height);
    uint32_t lut[256];
    for (unsigned i = 0; i < 256; ++i) {
      const auto& c = gamma_ramp_256_entry_table()[i];
      lut[i] = c.color_10_red | (c.color_10_green << 10) | (c.color_10_blue << 20);
    }
    scanout_->Present(*graphics_system_->presenter(), texture,
                      memory_->TranslatePhysical(texture.address), lut);
    last_swap_succeeded_ = true;
  } catch (const std::exception& e) { Unsupported(e.what()); }
}
rex::graphics::Shader* CommandProcessor::LoadShader(rex::graphics::xenos::ShaderType type,
    uint32_t address, const uint32_t* data, uint32_t count) {
  try { return scanout_->LoadShader(type, data, count); }
  catch (const std::exception& e) { Unsupported(e.what()); }
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
    scanout_->Draw(*register_file_, *memory_, active_vertex_shader(), active_pixel_shader(), primitive, count, indices ? &native_indices : nullptr);
    return true;
  } catch (const std::exception& e) { Unsupported(e.what()); }
}
bool CommandProcessor::IssueCopy() {
  try { scanout_->Resolve(*register_file_, *memory_); return true; }
  catch (const std::exception& e) { Unsupported(e.what()); }
}
bool CommandProcessor::ExecutePacketType3_EVENT_WRITE_ZPD(rex::memory::RingBuffer*, uint32_t, uint32_t) {
  Unsupported("occlusion query is not implemented");
}
}
