// The pinned SDK omits the camera implementation from its build. xam ordinal
// 0xAE6, absent from its table, is in curl_bind.cpp. Fail at the call site rather
// than return fictional success.
// Imports/thunk addresses are from this Retail 6717 XEX, not from 17559.
#include <rex/logging/log_category.h>
#include <rex/ppc/context.h>
#include <rex/ppc/hook.h>

namespace {
[[noreturn]] void Missing(PPCContext& ctx, const char* name) {
  REX_FATAL("dash_6717 unsupported import {}; LR={:08X}", name, uint32_t(ctx.lr));
}
}
REX_HOOK_RAW(__imp__XUsbcamDestroy) { Missing(ctx, "XUsbcamDestroy"); }
REX_HOOK_RAW(__imp__XUsbcamGetConfig) { Missing(ctx, "XUsbcamGetConfig"); }
REX_HOOK_RAW(__imp__XUsbcamSetConfig) { Missing(ctx, "XUsbcamSetConfig"); }
REX_HOOK_RAW(__imp__XUsbcamSetView) { Missing(ctx, "XUsbcamSetView"); }
REX_HOOK_RAW(__imp__XUsbcamSetCaptureMode) { Missing(ctx, "XUsbcamSetCaptureMode"); }
REX_HOOK_RAW(__imp__XUsbcamReadFrame) { Missing(ctx, "XUsbcamReadFrame"); }
