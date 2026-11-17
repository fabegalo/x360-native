// Shared host capability, independent of dashboard assets/version.
// No Vision camera backend exists. xboxkrnl ordinal 0x21D returns a state,
// not a success code: 0 = disconnected, as in the SDK's unbuilt usbcam shim.
// Verified callers: 17559 0x92368568 and 6717 0x92356FA0 only initialize
// capture when the returned state is 2. No device or completion is fabricated.
#include <rex/logging/log_category.h>
#include <rex/ppc/context.h>
#include <rex/ppc/hook.h>
#include <rex/kernel/types/status.h>

REX_HOOK_RAW(__imp__XUsbcamGetState) { ctx.r3.u64 = 0; }

// xboxkrnl 0x222 XUsbcamCreate(buffer, size, ...): report the unsupported operation,
// never the SDK's unconditional success stub. Do not allocate a fictional handle or
// touch the caller's buffer/output. Verified callers treat nonzero as failure:
// - 17559: checks nonzero at 0x92368440 and unwinds its initialization reference
//   at 0x92368448, keeping its handle uncreated;
// - 6717: 0x92356E78 calls it with the 0x4B000-byte buffer it just allocated; a
//   nonzero result skips to 0x92356E84, which decrements its initialization
//   reference (lwarx/stwcx. on 0x9288DD7C) and returns that status from 0x92356DE0.
REX_HOOK_RAW(__imp__XUsbcamCreate) {
  REXLOG_WARN("XUsbcamCreate: camera backend not implemented");
  ctx.r3.u64 = rex::kernel::X_STATUS_NOT_IMPLEMENTED;
}
