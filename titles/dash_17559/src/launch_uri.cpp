// HLE of the in-dashboard library route, scoped to the 17559 profile.
// Reference: official xam.xex SHA256 78b3dbd8...f828467, exports 0x4AC,
// 0x8199FBC8 (arguments), 0x8199FEE8/0x819A0000 (scheme expansion),
// 0x819A0440 (launch data and notification). No original XAM code is executed.
#include <cstring>
#include <string>
#include <rex/kernel/xam/module.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include "launch_context.h"

bool dash17559::PrepareReturnContext(rex::Runtime& runtime) {
  auto& loader = runtime.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex")->loader_data();
  if (loader.launch_flags == 0) return true;
  if (loader.launch_flags != 0x100) return false;
  // Original 17559 XAM 816A0798 -> 816A0800..816A0840: for bit 8,
  // a 1020-byte dashboard context, reserved=0, reason=65-42=23, user=FF.
  // The immutable host handoff retains the game's original bytes and flags.
  loader.launch_data.assign(1020, 0);
  loader.launch_data[7] = 23;
  loader.launch_data[11] = 0xFF;
  loader.launch_data_present = true;
  REXLOG_INFO("17559 return context: XLNI_DASH_ARCADE flags=00000100 reason=23 user=FF");
  return true;
}

extern "C" REX_FUNC(__imp__XamLaunchURI) {
  constexpr uint32_t invalid = 0x80070057, unsupported = 0x80004001;
  const uint32_t user = ctx.r3.u32, address = ctx.r4.u32;
  const uint32_t flags = ctx.r5.u32, overlapped = ctx.r6.u32;
  // This increment covers the real menu's current-dashboard request only.
  // Other flags can require reboot/title switching and are not equivalent.
  if (flags != 5) { ctx.r3.u64 = unsupported; return; }
  if (!base || !address || user > 3) { ctx.r3.u64 = invalid; return; }
  auto* rt = rex::Runtime::instance();
  auto* memory = rt->memory();
  const auto accessible = [&](uint32_t p, uint32_t size, uint32_t protection) {
    if (uint64_t(p) + size > 0x100000000ull) return false;
    for (uint32_t i = 0; i < size; ++i) {
      auto* heap = memory->LookupHeap(p + i);
      rex::memory::HeapAllocationInfo info{};
      if (!heap || !heap->QueryRegionInfo(p + i, &info) ||
          !(info.state & rex::memory::kMemoryAllocationCommit) ||
          (info.protect & protection) != protection) return false;
    }
    return true;
  };
  std::string uri;
  for (uint32_t i = 0; i < 1019; ++i) {
    if (uint64_t(address) + i > UINT32_MAX ||
        !accessible(address + i, 1, rex::memory::kMemoryProtectRead)) {
      ctx.r3.u64 = invalid; return;
    }
    const char c = *memory->TranslateVirtual<const char*>(address + i);
    if (!c) break;
    uri.push_back(c);
  }
  if (uri.empty() || uri.size() == 1019) { ctx.r3.u64 = invalid; return; }
  constexpr std::string_view scheme = "gameslibrary:";
  if (!uri.starts_with(scheme)) { ctx.r3.u64 = unsupported; return; }
  const std::string payload = "pam:BuiltIn.Hub.xzp:root:BuiltIn.ContentApp.xzp:contentlibrary:PackageType=1;" +
                              uri.substr(scheme.size());
  if (payload.size() >= 1008) { ctx.r3.u64 = 0x8007007A; return; }
  if (overlapped && !accessible(overlapped, 28,
      rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite)) {
    ctx.r3.u64 = invalid; return;
  }
  auto* kernel = rt->kernel_state();
  auto xam = kernel->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  auto& loader = xam->loader_data();
  loader.launch_data.assign(1020, 0);
  // XAM's packed dashboard launch data: reserved, reason=12, user, ASCII URI.
  loader.launch_data[7] = 12;
  loader.launch_data[11] = static_cast<uint8_t>(user);
  std::memcpy(loader.launch_data.data() + 12, payload.c_str(), payload.size() + 1);
  loader.launch_data_present = true;
  kernel->BroadcastNotification(static_cast<rex::system::XNotificationID>(0x80040021),
                                (user << 16) | 12);
  REXLOG_INFO("dash17559 URI delivered: {} -> {}; notification=80040021 reason=12 user={}",
              uri, payload, user);
  // Completion acknowledges delivery to the dashboard, not UI rendering or
  // successful game launch. The original UI still processes the notification.
  if (overlapped) {
    kernel->CompleteOverlappedImmediate(overlapped, 0);
    ctx.r3.u64 = 0x8000000A;
  } else {
    ctx.r3.u64 = 0;
  }
}
