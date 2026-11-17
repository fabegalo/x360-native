#include "display_transition.h"
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/thread_registry.h>
#include <rex/kernel/object/module_object.h>
#include <rex/kernel/object/object_table.h>
#include <rex/xam/title_lifecycle.h>
#include <rex/xam/system_ui.h>
#include <rex/gpu/graphics_system.h>
#include <rex/xenos/fetch.h>
#include <rex/memory/endian.h>
#include <rex/logging/log_category.h>

namespace dash17559 {
namespace {
// XAM 17559 816C90F0 uses the same D3D library as this dashboard. Execute its
// already recompiled texture-header and GetGammaRamp routines; do not guess the
// physical layout, channel ordering or initial gamma. The loader loop is HLE,
// scoped to this profile; the original XAM module is not loaded as an SDK replacement.
constexpr uint32_t kTextureHeader = 0x92875B80; // XAM counterpart 817F7BC8
constexpr uint32_t kGetGammaRamp = 0x921A8E60; // XAM counterpart 8183D4B0
constexpr uint32_t kConvertGamma = 0x921B3978; // XAM counterpart 818498D0

rex::kernel::X_RESULT PresentTransition(rex::kernel::TitleRuntime& runtime) {
  using namespace rex;
  using namespace rex::kernel;
  const auto display = runtime.GetPersistedDisplay();
  auto* gpu = REX_INTERFACE(gpu::IGraphicsSystem);
  auto* thread = runtime.GetThreadRegistry().GetCallingThread();
  if (!gpu || !gpu->HasDriver() || !thread || !display.buffer ||
      !thread->CanCallGuestFunction(kTextureHeader) || !thread->CanCallGuestFunction(kGetGammaRamp) ||
      !thread->CanCallGuestFunction(kConvertGamma)) {
    REXLOG_ERROR("17559 transition: missing persisted image, presentation or original D3D code");
    return XResultFromWin32(50);
  }
  // Other formats require their own PWL gamma contract, not this table path.
  if (display.format != 0x28280106) {
    REXLOG_ERROR("17559 transition: unsupported persisted format {:08X}", display.format);
    return XResultFromWin32(50);
  }
  uint32_t device_address = 0;
  if (const auto kernel = runtime.GetModuleRegistry().FindKernelModule("xboxkrnl.exe")) {
    auto module = runtime.GetObjectTable().Get<ModuleObject>(*kernel);
    if (module) device_address = module->GetVariable(0x1BE);
  }
  if (!device_address) return XResultFromWin32(50);
  auto* memory = KernelThread::GetCallingThread()->GetAddressSpace();
  const auto device = memory::load_and_swap<uint32_t>(memory->TranslateVirtual(device_address));
  if (!device) return XResultFromWin32(50);
  const auto scratch = memory->SystemHeapAlloc(0x1300);
  if (!scratch) return XResultFromWin32(8);
  struct Free { GuestAddressSpace* memory; uint32_t address;
    ~Free() { memory->SystemHeapFree(address); } } free{memory, scratch};
  // Arguments and header layout follow original XAM 816C9280–816C92A4.
  uint64_t texture_args[] = {display.width, display.height, 1, 0, display.format,
                            0, display.buffer, 0, 0, scratch, 0, 0};
  if (!thread->CallGuestFunction(kTextureHeader, texture_args)) return XResultFromWin32(50);
  xenos::TextureFetch fetch{};
  for (unsigned i = 0; i < 6; ++i)
    reinterpret_cast<uint32_t*>(&fetch)[i] = memory::load_and_swap<uint32_t>(
        memory->TranslateVirtual(scratch + 28 + i * 4));
  if (fetch.type != xenos::FetchConstantType::kTexture ||
      (uint32_t(fetch.base_address) << 12) != display.buffer ||
      fetch.size_2d.width + 1 != display.width || fetch.size_2d.height + 1 != display.height) {
    REXLOG_ERROR("17559 transition: original texture header disagrees with persisted descriptor");
    return XResultFromWin32(13);
  }
  const auto physical = memory->GetPhysicalAddress(display.buffer);
  if (physical == UINT32_MAX) return XResultFromWin32(13);
  fetch.base_address = physical >> 12;
  uint64_t gamma_args[] = {device, scratch + 0x100};
  if (!thread->CallGuestFunction(kGetGammaRamp, gamma_args)) return XResultFromWin32(50);
  GammaRamp original{};
  auto* guest_gamma = memory->TranslateVirtual<be_u16*>(scratch + 0x100);
  for (size_t i = 0; i < original.size(); ++i) original[i] = guest_gamma[i];
  REXLOG_INFO("17559 transition: original image {:08X}, {}x{}, fetch={:08X}/{:08X}, LUT end={}/{}/{}",
              display.buffer, display.width, display.height, fetch.dword_0, fetch.dword_1,
              original[255], original[511], original[767]);
  // Original 816C90D8 signals a frame event; the worker waits on it, then
  // presents each LUT. Use the existing guest VBlank source, never a fade timer
  // or a forged guest completion counter. All frames precede title teardown.
  for (unsigned step = 0; step <= 32; ++step) {
    auto ramp = TransitionRamp(original, step);
    auto* input = memory->TranslateVirtual<be_u16*>(scratch + 0x700);
    for (size_t i = 0; i < ramp.size(); ++i) input[i] = ramp[i];
    // SetGammaRamp in the original XAM calls this conversion before writing
    // the hardware LUT. Preserve its display-gamma transfer function too.
    uint64_t convert_args[] = {scratch + 0xD00, scratch + 0x700, 0};
    if (!thread->CallGuestFunction(kConvertGamma, convert_args)) return XResultFromWin32(50);
    auto* output = memory->TranslateVirtual<be_u16*>(scratch + 0xD00);
    for (size_t i = 0; i < ramp.size(); ++i) ramp[i] = output[i];
    if (!gpu->WaitForGuestVblank() ||
        !gpu->PresentSystemFrame(reinterpret_cast<const uint32_t*>(&fetch), ramp.data())) {
      REXLOG_ERROR("17559 transition: presentation failed at original LUT step {}", step);
      return XResultFromWin32(1460);
    }
  }
  if (!gpu->WaitForGuestVblank()) return XResultFromWin32(1460);
  REXLOG_INFO("17559 transition: 33 system-image frames submitted; black endpoint reached before teardown");
  return X_ERROR_SUCCESS;
}
}
void InstallDisplayTransition(rex::kernel::TitleRuntime& runtime) {
  auto* xam = REX_INTERFACE(rex::xam::IXAMTitleServices);
  REX_CHECK(xam, "17559 transition: XAM title services unavailable");
  xam->GetSystemUI().launch_display_transition = [&runtime] { return PresentTransition(runtime); };
}
}
