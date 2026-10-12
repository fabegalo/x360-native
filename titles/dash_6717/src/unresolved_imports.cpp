// The pinned SDK omits the camera implementation from its build. xam ordinal
// 0xAE6, absent from its table, is in curl_bind.cpp. Fail at the call site rather
// than return fictional success.
// Imports/thunk addresses are from this Retail 6717 XEX, not from 17559.
#include <rex/logging/assert.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

namespace {
[[noreturn]] void Missing(PPCContext& ctx, const char* name) {
  REX_FATAL("dash_6717 unsupported import {}; LR={:08X}", name, uint32_t(ctx.lr));
}
}
extern "C" REX_FUNC(__imp__XUsbcamDestroy) { Missing(ctx, "XUsbcamDestroy"); }
extern "C" REX_FUNC(__imp__XUsbcamGetConfig) { Missing(ctx, "XUsbcamGetConfig"); }
extern "C" REX_FUNC(__imp__XUsbcamSetConfig) { Missing(ctx, "XUsbcamSetConfig"); }
extern "C" REX_FUNC(__imp__XUsbcamSetView) { Missing(ctx, "XUsbcamSetView"); }
extern "C" REX_FUNC(__imp__XUsbcamSetCaptureMode) { Missing(ctx, "XUsbcamSetCaptureMode"); }
extern "C" REX_FUNC(__imp__XUsbcamReadFrame) { Missing(ctx, "XUsbcamReadFrame"); }
