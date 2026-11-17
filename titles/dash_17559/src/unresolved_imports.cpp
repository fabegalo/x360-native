// dash_17559: imports that dash.xex calls but the pinned SDK (f5337cdc) does not
// provide at link time. Without these definitions the title does not link
// (build log x360n-build-4-build.log, 13 x "lld-link: error: undefined symbol").
//
// They are traps, not implementations: a call stops the process with the import,
// the reason and the guest return address (LR). Nothing is emulated and no value
// is returned, so a call can never pass for support (CLAUDE.md, rule 3). The
// exceptions, the disconnected camera state and unsupported camera creation,
// have explicit implementations below and in ../common/camera_state.cpp.
// Title-specific: the placeholder names sub_<thunk> are this XEX's thunk
// addresses. docs/dashboard.md §0.2 and §0.5 have the evidence.
#include <cstdint>

#include <rex/logging/log_category.h>
#include <rex/ppc/context.h>
#include <rex/ppc/hook.h>
#include <rex/kernel/types/status.h>

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
REX_HOOK_RAW(sub_9293C8BC) { Trap(ctx, "xam 0x546 (XamSetKerbTimeouts)", kNotInTable); }
REX_HOOK_RAW(sub_9293C89C) { Trap(ctx, "xam 0x547 (XamGetKerbTimeouts)", kNotInTable); }
REX_HOOK_RAW(sub_9293C8AC) { Trap(ctx, "xam 0x548 (XamSetMacsTimeouts)", kNotInTable); }
REX_HOOK_RAW(sub_9293C88C) { Trap(ctx, "xam 0x549 (XamGetMacsTimeouts)", kNotInTable); }
REX_HOOK_RAW(sub_9293CE6C) {
  Trap(ctx, "xam 0x54B (XamProfileHasWindowsLiveCredentials)", kNotInTable);
}

// xboxkrnl exports whose shims exist only in a source the runtime does not build.
REX_HOOK_RAW(__imp__XUsbcamSetCaptureMode) { Trap(ctx, "xboxkrnl 0x21A XUsbcamSetCaptureMode", kNotBuilt); }
REX_HOOK_RAW(__imp__XUsbcamGetConfig) { Trap(ctx, "xboxkrnl 0x21B XUsbcamGetConfig", kNotBuilt); }
REX_HOOK_RAW(__imp__XUsbcamSetConfig) { Trap(ctx, "xboxkrnl 0x21C XUsbcamSetConfig", kNotBuilt); }

// xboxkrnl 0x21D XUsbcamGetState() -> camera state. Implemented, not a trap: no Xbox
// Live Vision camera is attached to this host and the runtime has no camera support,
// so the real answer is "not connected", 0. That 0 means no camera is shown by
// dash.xex itself: 0x92337AB8 computes present = (state != 0) and only when present
// loads the "ThemeVisionEffect..." resource (UTF-16 at 0x92032D98); 0x92368568 sends
// state 2 on to further camera setup. The SDK's unbuilt shim
// (src/kernel/xboxkrnl/xboxkrnl_usbcam.cpp) and Xenia say "0 = not connected" as well.
// First called during startup (run 20261012T002758Z-dash17559-run-4, LR=0x9236857C).
// Implementation shared with 6717 in titles/common/camera_state.cpp.
REX_HOOK_RAW(__imp__XUsbcamReadFrame) { Trap(ctx, "xboxkrnl 0x21E XUsbcamReadFrame", kNotBuilt); }
REX_HOOK_RAW(__imp__XUsbcamSetView) { Trap(ctx, "xboxkrnl 0x220 XUsbcamSetView", kNotBuilt); }
// XUsbcamCreate reports the unsupported operation; implementation shared with 6717
// in titles/common/camera_state.cpp.
REX_HOOK_RAW(__imp__XUsbcamDestroy) { Trap(ctx, "xboxkrnl 0x223 XUsbcamDestroy", kNotBuilt); }
