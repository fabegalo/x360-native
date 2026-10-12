// dash_17559: imports that dash.xex calls but the pinned SDK (f5337cdc) does not
// provide at link time. Without these definitions the title does not link
// (build log x360n-build-4-build.log, 13 x "lld-link: error: undefined symbol").
//
// They are traps, not implementations: a call stops the process with the import,
// the reason and the guest return address (LR). Nothing is emulated and no value
// is returned, so a call can never pass for support (CLAUDE.md, rule 3). The
// exceptions, the disconnected camera state and unsupported camera creation,
// have explicit implementations below.
// Title-specific: the placeholder names sub_<thunk> are this XEX's thunk
// addresses. docs/dashboard.md §0.2 and §0.5 have the evidence.
#include <cstdint>

#include <rex/logging/assert.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/system/xtypes.h>

namespace {

[[noreturn]] void Trap(PPCContext& ctx, const char* import, const char* reason) {
  REX_FATAL("unresolved import {} ({}); called with LR=0x{:08X}", import, reason,
            static_cast<uint32_t>(ctx.lr));
}

constexpr const char* kNotInTable =
    "ordinal not in the SDK export table; codegen: Cannot resolve ordinal; name per Xenia "
    "Canary 04f85416e0f8 xam_table.inc";
constexpr const char* kNotBuilt =
    "src/kernel/xboxkrnl/xboxkrnl_usbcam.cpp is not compiled at the pinned SDK "
    "(src/kernel/CMakeLists.txt:53)";

}  // namespace

// xam ordinals missing from the SDK table. rexglue codegen registers their thunks
// under sub_<thunk address> and calls them but emits no definition. Thunk
// addresses from `x360n-content imports` (the SDK loader's records).
extern "C" REX_FUNC(sub_9293C8BC) { Trap(ctx, "xam 0x546 (XamSetKerbTimeouts)", kNotInTable); }
extern "C" REX_FUNC(sub_9293C89C) { Trap(ctx, "xam 0x547 (XamGetKerbTimeouts)", kNotInTable); }
extern "C" REX_FUNC(sub_9293C8AC) { Trap(ctx, "xam 0x548 (XamSetMacsTimeouts)", kNotInTable); }
extern "C" REX_FUNC(sub_9293C88C) { Trap(ctx, "xam 0x549 (XamGetMacsTimeouts)", kNotInTable); }
extern "C" REX_FUNC(sub_9293CE6C) {
  Trap(ctx, "xam 0x54B (XamProfileHasWindowsLiveCredentials)", kNotInTable);
}

// xboxkrnl exports whose shims exist only in a source the runtime does not build.
extern "C" REX_FUNC(__imp__XUsbcamSetCaptureMode) { Trap(ctx, "xboxkrnl 0x21A XUsbcamSetCaptureMode", kNotBuilt); }
extern "C" REX_FUNC(__imp__XUsbcamGetConfig) { Trap(ctx, "xboxkrnl 0x21B XUsbcamGetConfig", kNotBuilt); }
extern "C" REX_FUNC(__imp__XUsbcamSetConfig) { Trap(ctx, "xboxkrnl 0x21C XUsbcamSetConfig", kNotBuilt); }

// xboxkrnl 0x21D XUsbcamGetState() -> camera state. Implemented, not a trap: no Xbox
// Live Vision camera is attached to this host and the runtime has no camera support,
// so the real answer is "not connected", 0. That 0 means no camera is shown by
// dash.xex itself: 0x92337AB8 computes present = (state != 0) and only when present
// loads the "ThemeVisionEffect..." resource (UTF-16 at 0x92032D98); 0x92368568 sends
// state 2 on to further camera setup. The SDK's unbuilt shim
// (src/kernel/xboxkrnl/xboxkrnl_usbcam.cpp) and Xenia say "0 = not connected" as well.
// First called during startup (run 20261012T002758Z-dash17559-run-4, LR=0x9236857C).
extern "C" REX_FUNC(__imp__XUsbcamGetState) {
  ctx.r3.u64 = 0;  // not connected
}
extern "C" REX_FUNC(__imp__XUsbcamReadFrame) { Trap(ctx, "xboxkrnl 0x21E XUsbcamReadFrame", kNotBuilt); }
extern "C" REX_FUNC(__imp__XUsbcamSetView) { Trap(ctx, "xboxkrnl 0x220 XUsbcamSetView", kNotBuilt); }
// No camera backend exists. Report the unsupported operation, never the SDK's
// unconditional success stub. The guest checks nonzero at 0x92368440, unwinds
// its initialization reference at 0x92368448, and keeps its handle uncreated.
// Do not allocate a fictional handle or touch the caller's buffer/output.
extern "C" REX_FUNC(__imp__XUsbcamCreate) {
  using rex::X_STATUS;
  REXLOG_WARN("XUsbcamCreate: camera backend not implemented");
  ctx.r3.u64 = X_STATUS_NOT_IMPLEMENTED;
}
extern "C" REX_FUNC(__imp__XUsbcamDestroy) { Trap(ctx, "xboxkrnl 0x223 XUsbcamDestroy", kNotBuilt); }

// The title reaches this export for gameslibrary:_RootTitle=My Games; at
// LR=0x92190354. The SDK has no URI router: its stub leaves user index 0 in
// r3, which the caller mistakes for S_OK. Report the missing operation without
// completing the OVERLAPPED or initiating a different title behind the menu.
extern "C" REX_FUNC(__imp__XamLaunchURI) {
  REXLOG_WARN("XamLaunchURI: URI dispatch not implemented; LR={:08X} user={} uri={:08X} flags={:08X}",
              static_cast<uint32_t>(ctx.lr), ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
  ctx.r3.u64 = 0x80004001;  // E_NOTIMPL, checked as an HRESULT by the guest.
}
