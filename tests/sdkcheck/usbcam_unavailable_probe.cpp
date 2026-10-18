// The real SDK exports, without any dashboard or title-specific definitions.
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/system/xtypes.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <crtdbg.h>
#endif

extern "C" {
REX_FUNC(__imp__XUsbcamCreate);
REX_FUNC(__imp__XUsbcamGetState);
REX_FUNC(__imp__XUsbcamSetCaptureMode);
REX_FUNC(__imp__XUsbcamGetConfig);
REX_FUNC(__imp__XUsbcamSetConfig);
REX_FUNC(__imp__XUsbcamReadFrame);
REX_FUNC(__imp__XUsbcamSnapshot);
REX_FUNC(__imp__XUsbcamSetView);
REX_FUNC(__imp__XUsbcamGetView);
REX_FUNC(__imp__XUsbcamDestroy);
REX_FUNC(__imp__XUsbcamReset);
}

int main(int argc, char** argv) {
#ifdef _WIN32
  // Fatal-operation subprocesses must fail without interactive CRT dialogs.
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  _set_error_mode(_OUT_TO_STDERR);
#endif
  alignas(32) std::array<uint8_t, 128> memory;
  memory.fill(0xA5);
  const auto before = memory;
  PPCContext ctx{};
  ctx.r3.u64 = 16; ctx.r4.u64 = 64; ctx.r5.u64 = 96;
  ctx.lr = 0x12345678; ctx.r31.u64 = 0x1122334455667788;
  const struct { const char* name; PPCFunc* function; } unsupported[] = {
      {"SetCaptureMode", __imp__XUsbcamSetCaptureMode}, {"GetConfig", __imp__XUsbcamGetConfig},
      {"SetConfig", __imp__XUsbcamSetConfig}, {"ReadFrame", __imp__XUsbcamReadFrame},
      {"Snapshot", __imp__XUsbcamSnapshot}, {"SetView", __imp__XUsbcamSetView},
      {"GetView", __imp__XUsbcamGetView}, {"Destroy", __imp__XUsbcamDestroy},
      {"Reset", __imp__XUsbcamReset}};
  if (argc == 2) {
    for (auto operation : unsupported) if (std::strcmp(argv[1], operation.name) == 0) {
      std::fprintf(stderr, "calling unsupported XUsbcam%s LR=12345678\n", operation.name);
      std::fflush(stderr);
      operation.function(ctx, memory.data());
      std::fprintf(stderr, "FAIL: unsupported operation returned\n");
      return 100;
    }
    return 101;
  }
  __imp__XUsbcamCreate(ctx, memory.data());
  using rex::X_STATUS;
  if (ctx.r3.u32 != X_STATUS_NOT_IMPLEMENTED || ctx.r4.u32 != 64 || ctx.r5.u32 != 96 ||
      ctx.lr != 0x12345678 || ctx.r31.u64 != 0x1122334455667788 || memory != before) return 1;
  __imp__XUsbcamGetState(ctx, memory.data());
  if (ctx.r3.u64 != 0 || memory != before) return 2;
  std::puts("PASS: camera disconnected; creation unsupported; registers and buffers preserved; 11 exports linked");
}
