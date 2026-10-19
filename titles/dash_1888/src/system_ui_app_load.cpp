#include "system_ui_request.h"
#include <rex/kernel/xam/module.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <string>
#include <cstring>

extern "C" REX_FUNC(__imp__XamAppRequestLoad) {
  auto xam = REX_KERNEL_STATE()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  ctx.r3.u64 = xam && xam->system_ui_request ? xam->system_ui_request(dash1888::kReserveApp, 0, 0) : 0;
}
extern "C" REX_FUNC(__imp__XamAppUnrequestLoad) {
  auto xam = REX_KERNEL_STATE()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  ctx.r3.u64 = xam && xam->system_ui_request ?
      xam->system_ui_request(dash1888::kReleaseApp, 0, 0) : 0x80004001u;
}
// 818DE018: name, user, initial data, byte length, flags, optional output.
// The caller has already reserved a transition via XamAppRequestLoad. Copy it
// to the host queue; never unload an app from inside its own original callback.
extern "C" REX_FUNC(__imp__XamAppLoad) {
  auto xam = REX_KERNEL_STATE()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  std::string file;
  for (unsigned i = 0; ctx.r3.u32 && i < 128; ++i) {
    const auto c = base[ctx.r3.u32 + i]; if (!c) break; file += char(c);
  }
  uint32_t message = 0, size = 0;
  if (file == "createprofile.xex") { message = 0x2100E; size = 16; }
  else if (file == "signin.xex") { message = 0x21001; size = 16; }
  else if (file == "hud.xex") { message = 0x21015; size = 12; }
  else if (file == "gamerprofile.xex") { message = 0x21008; size = 24; }
  if (!message || ctx.r7.u32 || ctx.r8.u32 || !xam || !xam->system_ui_request) {
    if (xam && xam->system_ui_request) xam->system_ui_request(dash1888::kReleaseApp, 0, 0);
    ctx.r3.u64 = 0x80004001u; return;
  }
  const auto request = REX_KERNEL_MEMORY()->SystemHeapAlloc(size);
  if (!request) {
    xam->system_ui_request(dash1888::kReleaseApp, 0, 0);
    ctx.r3.u64 = 0x8007000Eu; return;
  }
  std::memset(base + request, 0, size);
  if (message == 0x21015 && !ctx.r5.u32 && !ctx.r6.u32) {
    rex::memory::store_and_swap<uint32_t>(base + request + 4, ctx.r4.u32);
  } else if (ctx.r5.u32 && ctx.r6.u32 == size && message != 0x21015) std::memcpy(base + request, base + ctx.r5.u32, size);
  else if (ctx.r5.u32 || ctx.r6.u32) {
    REX_KERNEL_MEMORY()->SystemHeapFree(request);
    xam->system_ui_request(dash1888::kReleaseApp, 0, 0);
    ctx.r3.u64 = 0x80070057u; return;
  } else if (message != 0x21015) {
    rex::memory::store_and_swap<uint32_t>(base + request, ctx.r4.u32);
    if (message == 0x21001) rex::memory::store_and_swap<uint32_t>(base + request + 4, 1);
  }
  const auto result = xam->system_ui_request(message, request, size);
  if (result) xam->system_ui_request(dash1888::kReleaseApp, 0, 0);
  REX_KERNEL_MEMORY()->SystemHeapFree(request);
  ctx.r3.u64 = result;
}
