// HLE of the in-dashboard library route, scoped to the 17559 profile.
// Reference: official xam.xex SHA256 78b3dbd8...f828467, exports 0x4AC,
// 0x8199FBC8 (arguments), 0x8199FEE8/0x819A0000 (scheme expansion),
// 0x819A0440 (launch data and notification). No original XAM code is executed.
#include <cstring>
#include <string>
#include <rex/xam/title_lifecycle.h>
#include <rex/xam/loader_state.h>
#include <rex/logging/log_category.h>
#include <rex/ppc/context.h>
#include <rex/ppc/recompiled_code.h>
#include <rex/ppc/hook.h>
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/guest_memory.h>
#include <rex/kernel/thread_registry.h>
#include <rex/kernel/object/notification_broadcaster.h>
#include "launch_context.h"

bool dash17559::PrepareReturnContext(rex::kernel::TitleRuntime& runtime) {
  if (!runtime.IsLoaded()) return false;
  auto* services = REX_INTERFACE(rex::xam::IXAMTitleServices);
  if (!services) return false;
  auto& loader = services->GetLoaderState();
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

REX_HOOK_RAW(__imp__XamLaunchURI) {
  constexpr uint32_t invalid = 0x80070057, unsupported = 0x80004001;
  const uint32_t user = ctx.r3.u32, address = ctx.r4.u32;
  const uint32_t flags = ctx.r5.u32, overlapped = ctx.r6.u32;
  // This increment covers the real menu's current-dashboard request only.
  // Other flags can require reboot/title switching and are not equivalent.
  if (flags != 5) { ctx.r3.u64 = unsupported; return; }
  if (!base || !address || user > 3) { ctx.r3.u64 = invalid; return; }
  auto* rt = REX_RUNTIME();
  if (!rt) { ctx.r3.u64 = invalid; return; }
  auto& memory = rt->GetGuestMemory();
  std::string uri;
  for (uint32_t i = 0; i < 1019; ++i) {
    if (uint64_t(address) + i > UINT32_MAX) {
      ctx.r3.u64 = invalid; return;
    }
    const auto character = memory.Load<char>(address + i);
    if (!character) { ctx.r3.u64 = invalid; return; }
    const char c = *character;
    if (!c) break;
    uri.push_back(c);
  }
  if (uri.empty() || uri.size() == 1019) { ctx.r3.u64 = invalid; return; }
  constexpr std::string_view scheme = "gameslibrary:";
  if (!uri.starts_with(scheme)) { ctx.r3.u64 = unsupported; return; }
  const std::string payload = "pam:BuiltIn.Hub.xzp:root:BuiltIn.ContentApp.xzp:contentlibrary:PackageType=1;" +
                              uri.substr(scheme.size());
  if (payload.size() >= 1008) { ctx.r3.u64 = 0x8007007A; return; }
  if (overlapped && !memory.GetWritableSpan(overlapped, 28)) {
    ctx.r3.u64 = invalid; return;
  }
  auto* xam = REX_INTERFACE(rex::xam::IXAMTitleServices);
  auto* dispatcher = REX_INTERFACE(rex::kernel::IOverlappedDispatcher);
  if (!xam || (overlapped && !dispatcher)) { ctx.r3.u64 = unsupported; return; }
  auto& loader = xam->GetLoaderState();
  loader.launch_data.assign(1020, 0);
  // XAM's packed dashboard launch data: reserved, reason=12, user, ASCII URI.
  loader.launch_data[7] = 12;
  loader.launch_data[11] = static_cast<uint8_t>(user);
  std::memcpy(loader.launch_data.data() + 12, payload.c_str(), payload.size() + 1);
  loader.launch_data_present = true;
  rt->GetNotificationBroadcaster().BroadcastNotification(0x80040021, (user << 16) | 12);
  REXLOG_INFO("dash17559 URI delivered: {} -> {}; notification=80040021 reason=12 user={}",
              uri, payload, user);
  // Completion acknowledges delivery to the dashboard, not UI rendering or
  // successful game launch. The original UI still processes the notification.
  if (overlapped) {
    dispatcher->Complete(overlapped, {.result = 0});
    ctx.r3.u64 = 0x8000000A;
  } else {
    ctx.r3.u64 = 0;
  }
}
