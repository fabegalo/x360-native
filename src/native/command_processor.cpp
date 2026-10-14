#include "command_processor.h"
#include <rex/graphics/graphics_system.h>
#include <rex/logging.h>
#include <cstdio>
#include <cstdlib>
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
    REXLOG_INFO("Native: independent D3D12 compute scanout ready; guest draws/EDRAM unsupported");
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
    uint32_t address, const uint32_t*, uint32_t count) {
  REXLOG_ERROR("Native: IM_LOAD type={} address={:08X} dwords={}", uint32_t(type), address, count);
  Unsupported("Xenos guest shader translation is not implemented");
}
bool CommandProcessor::IssueDraw(rex::graphics::xenos::PrimitiveType, uint32_t, IndexBufferInfo*, bool) {
  Unsupported("guest draw/EDRAM operation is not implemented");
}
bool CommandProcessor::IssueCopy() { Unsupported("EDRAM resolve is not implemented"); }
bool CommandProcessor::ExecutePacketType3_EVENT_WRITE_ZPD(rex::memory::RingBuffer*, uint32_t, uint32_t) {
  Unsupported("occlusion query is not implemented");
}
}
