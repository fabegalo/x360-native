#include "display_transition.h"
#include <rex/runtime.h>
#include <rex/kernel/xam/module.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xthread.h>
#include <rex/graphics/xenos.h>
#include <rex/logging.h>

namespace dash17559 {
namespace {
// XAM 17559 816C90F0 uses the same D3D library as this dashboard. Execute its
// already recompiled texture-header and GetGammaRamp routines; do not guess the
// physical layout, channel ordering or initial gamma. The loader loop is HLE,
// scoped to this profile; the original XAM module is not loaded as an SDK replacement.
constexpr uint32_t kTextureHeader = 0x92875B80; // XAM counterpart 817F7BC8
constexpr uint32_t kGetGammaRamp = 0x921A8E60; // XAM counterpart 8183D4B0
constexpr uint32_t kConvertGamma = 0x921B3978; // XAM counterpart 818498D0

rex::X_RESULT PresentTransition(rex::Runtime& runtime) {
  using namespace rex;
  auto* state = runtime.kernel_state();
  const auto display = state->GetPersistedDisplay();
  auto* gpu = runtime.graphics_system();
  auto* thread = system::XThread::GetCurrentThread();
  auto* dispatcher = runtime.function_dispatcher();
  if (!gpu || !gpu->has_presentation() || !thread || !display.buffer ||
      !dispatcher->GetFunction(kTextureHeader) || !dispatcher->GetFunction(kGetGammaRamp) ||
      !dispatcher->GetFunction(kConvertGamma)) {
    REXLOG_ERROR("17559 transition: missing persisted image, presentation or original D3D code");
    return X_RESULT_FROM_WIN32(50);
  }
  // Other formats require their own PWL gamma contract, not this table path.
  if (display.format != 0x28280106) {
    REXLOG_ERROR("17559 transition: unsupported persisted format {:08X}", display.format);
    return X_RESULT_FROM_WIN32(50);
  }
  const auto* device_export = runtime.export_resolver()->GetExportByOrdinal("xboxkrnl.exe", 0x1BE);
  if (!device_export || !device_export->variable_ptr) return X_RESULT_FROM_WIN32(50);
  auto* memory = runtime.memory();
  const auto device = memory::load_and_swap<uint32_t>(memory->TranslateVirtual(device_export->variable_ptr));
  if (!device) return X_RESULT_FROM_WIN32(50);
  const auto scratch = memory->SystemHeapAlloc(0x1300);
  if (!scratch) return X_RESULT_FROM_WIN32(8);
  struct Free { memory::Memory* memory; uint32_t address;
    ~Free() { memory->SystemHeapFree(address); } } free{memory, scratch};
  // Arguments and header layout follow original XAM 816C9280–816C92A4.
  uint64_t texture_args[] = {display.width, display.height, 1, 0, display.format,
                            0, display.buffer, 0, 0, scratch, 0, 0};
  dispatcher->ExecuteTrap(thread->thread_state(), kTextureHeader, texture_args, 12);
  graphics::xenos::xe_gpu_texture_fetch_t fetch{};
  memory::copy_and_swap_32_unaligned(&fetch,
      memory->TranslateVirtual<uint32_t*>(scratch + 28), 6);
  if (fetch.type != graphics::xenos::FetchConstantType::kTexture ||
      (fetch.base_address << 12) != display.buffer ||
      fetch.size_2d.width + 1 != display.width || fetch.size_2d.height + 1 != display.height) {
    REXLOG_ERROR("17559 transition: original texture header disagrees with persisted descriptor");
    return X_RESULT_FROM_WIN32(13);
  }
  const auto physical = memory->GetPhysicalAddress(display.buffer);
  if (physical == UINT32_MAX) return X_RESULT_FROM_WIN32(13);
  fetch.base_address = physical >> 12;
  uint64_t gamma_args[] = {device, scratch + 0x100};
  dispatcher->ExecuteTrap(thread->thread_state(), kGetGammaRamp, gamma_args, 2);
  GammaRamp original{};
  auto* guest_gamma = memory->TranslateVirtual<be<uint16_t>*>(scratch + 0x100);
  for (size_t i = 0; i < original.size(); ++i) original[i] = guest_gamma[i];
  REXLOG_INFO("17559 transition: original image {:08X}, {}x{}, fetch={:08X}/{:08X}, LUT end={}/{}/{}",
              display.buffer, display.width, display.height, fetch.dword_0, fetch.dword_1,
              original[255], original[511], original[767]);
  // Original 816C90D8 signals a frame event; the worker waits on it, then
  // presents each LUT. Use the existing guest VBlank source, never a fade timer
  // or a forged guest completion counter. All frames precede title teardown.
  for (unsigned step = 0; step <= 32; ++step) {
    auto ramp = TransitionRamp(original, step);
    auto* input = memory->TranslateVirtual<be<uint16_t>*>(scratch + 0x700);
    for (size_t i = 0; i < ramp.size(); ++i) input[i] = ramp[i];
    // SetGammaRamp in the original XAM calls this conversion before writing
    // the hardware LUT. Preserve its display-gamma transfer function too.
    uint64_t convert_args[] = {scratch + 0xD00, scratch + 0x700, 0};
    dispatcher->ExecuteTrap(thread->thread_state(), kConvertGamma, convert_args, 3);
    auto* output = memory->TranslateVirtual<be<uint16_t>*>(scratch + 0xD00);
    for (size_t i = 0; i < ramp.size(); ++i) ramp[i] = output[i];
    if (!gpu->WaitForGuestVblank() ||
        !gpu->PresentSystemFrame(reinterpret_cast<const uint32_t*>(&fetch), ramp.data())) {
      REXLOG_ERROR("17559 transition: presentation failed at original LUT step {}", step);
      return X_RESULT_FROM_WIN32(1460);
    }
  }
  if (!gpu->WaitForGuestVblank()) return X_RESULT_FROM_WIN32(1460);
  REXLOG_INFO("17559 transition: 33 system-image frames submitted; black endpoint reached before teardown");
  return X_ERROR_SUCCESS;
}
}
void InstallDisplayTransition(rex::Runtime& runtime) {
  runtime.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex")
      ->launch_display_transition = [&runtime] { return PresentTransition(runtime); };
}
}
