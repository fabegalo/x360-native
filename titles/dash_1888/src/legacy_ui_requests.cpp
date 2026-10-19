#include "system_ui_request.h"
#include <rex/kernel/xam/module.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <cstring>

namespace {
uint32_t Request(uint32_t message, uint32_t user, uint32_t size) {
  auto* memory = REX_KERNEL_MEMORY();
  auto xam = REX_KERNEL_STATE()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  if (!xam || !xam->system_ui_request) return 0x80070490u;
  const auto request = memory->SystemHeapAlloc(size);
  if (!request) return 0x8007000Eu;
  std::memset(memory->TranslateVirtual(request), 0, size);
  rex::memory::store_and_swap<uint32_t>(memory->TranslateVirtual(request), user);
  const auto result = xam->system_ui_request(message, request, size);
  memory->SystemHeapFree(request);
  return result;
}
}
// The original 1888 export takes only the controller (818E6A08); r4 is not
// a modern mode argument. Keep this adapter private to this dashboard build.
extern "C" REX_FUNC(__imp__XamShowCreateProfileUI) {
  ctx.r3.u64 = ctx.r3.u32 < 4 ? Request(0x2100E, ctx.r3.u32, 16) : 87;
}
extern "C" REX_FUNC(__imp__XamShowGamerCardUI) {
  ctx.r3.u64 = ctx.r3.u32 < 4 ? Request(0x21008, ctx.r3.u32, 24) : 87;
}
// The local identity/settings service does not expose a console profile volume
// (GPD/dictionary files). VK checks the Win32 result and uses its own empty
// dictionary path. Never treat a stub's unchanged XUID as a successful mount.
extern "C" REX_FUNC(__imp__XamProfileOpen) { ctx.r3.u64 = 50; /* ERROR_NOT_SUPPORTED */ }
