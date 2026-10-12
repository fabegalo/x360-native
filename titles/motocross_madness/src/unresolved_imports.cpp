// motocross_madness: imports that default.xex calls but the pinned SDK (f5337cdc)
// does not provide at link time. Without these definitions the title does not link
// (x360n-build-4-build.log, 2026-10-12: 7 x "lld-link: error: undefined symbol"
// __imp__XUsbcam*): src/kernel/xboxkrnl/xboxkrnl_usbcam.cpp is not compiled
// (src/kernel/CMakeLists.txt:53).
//
// XUsbcamGetState (0 = disconnected) and XUsbcamCreate (X_STATUS_NOT_IMPLEMENTED)
// come from ../../common/camera_state.cpp. Checked against this XEX's callers
// (x360n-content disasm/callers on the loaded image):
// - GetState wrapper 0x82C928E8 returns the state unchanged unless it is 2. Its
//   callers treat anything but 2 as no camera: 0x82C944A0 maps it to 1167
//   (0x48F); 0x82C980AC and 0x82C98204 only start a frame read when the stored
//   state (216(r31)) is 2; 0x82C5AB14 takes its not-connected branch.
// - Create wrapper 0x82C92720 creates only when the handle at 0x83253E30 is 0,
//   counts the attempt at 0x83253E34, and on a nonzero result decrements that
//   count (0x82C927C8) and returns the status, leaving the handle untouched.
// - Destroy wrapper 0x82C92848 calls XUsbcamDestroy only when that count drops to
//   exactly 0; after a failed create it goes negative and is restored (0x82C92884).
//
// The other camera imports are traps, not implementations: a call stops the
// process with the import, the reason and the guest return address (LR). Nothing
// is emulated and no value is returned (CLAUDE.md, rule 3). With no camera they
// should not be reached; if one is, the run says so.
#include <cstdint>

#include <rex/logging/assert.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

namespace {

[[noreturn]] void Trap(PPCContext& ctx, const char* import) {
  REX_FATAL("unresolved import {} (src/kernel/xboxkrnl/xboxkrnl_usbcam.cpp is not compiled at "
            "the pinned SDK, src/kernel/CMakeLists.txt:53); called with LR=0x{:08X}",
            import, static_cast<uint32_t>(ctx.lr));
}

}  // namespace

extern "C" REX_FUNC(__imp__XUsbcamDestroy) { Trap(ctx, "xboxkrnl 0x223 XUsbcamDestroy"); }
extern "C" REX_FUNC(__imp__XUsbcamSetConfig) { Trap(ctx, "xboxkrnl 0x21C XUsbcamSetConfig"); }
extern "C" REX_FUNC(__imp__XUsbcamSetView) { Trap(ctx, "xboxkrnl 0x220 XUsbcamSetView"); }
extern "C" REX_FUNC(__imp__XUsbcamSetCaptureMode) {
  Trap(ctx, "xboxkrnl 0x21A XUsbcamSetCaptureMode");
}
extern "C" REX_FUNC(__imp__XUsbcamReadFrame) { Trap(ctx, "xboxkrnl 0x21E XUsbcamReadFrame"); }
