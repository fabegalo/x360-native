#pragma once

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/runtime.h>
#include <rex/system/xthread.h>
#include <rex/system/thread_state.h>
#include <rex/system/xtypes.h>
#include <rex/system/xio.h>
#include <chrono>
#include <cstring>
#include <functional>
#include <memory>
#include <utility>

extern "C" REX_FUNC(__imp__XamShowDeviceSelectorUI);
REXCVAR_DEFINE_UINT32(dash17559_selector_probe_seconds, 0, "Diagnostics",
                     "Request original storage selector once after 1..120 seconds; zero disables the diagnostic");

namespace dash17559 {
// Called only from the serialized system-frame callback. Ordinary sessions
// retain the existing poll directly: no added clock read or frame wrapper.
inline std::function<void()> WithStorageSelectorDiagnostic(
    rex::Runtime& runtime, std::function<void()> poll) {
  const auto seconds = REXCVAR_GET(dash17559_selector_probe_seconds);
  if (!seconds || seconds > 120) return poll;
  struct Request {
    rex::Runtime& runtime;
    uint32_t address = 0;
    explicit Request(rex::Runtime& value) : runtime(value) {}
    ~Request() { if (address) runtime.memory()->SystemHeapFree(address); }
  };
  return [&runtime, poll = std::move(poll), seconds,
          request = std::make_shared<Request>(runtime), submitted = false,
          start = std::chrono::steady_clock::now()]() mutable {
    poll();
    if (submitted) {
      if (!request->address) return;
      const auto* io = runtime.memory()->TranslateVirtual<rex::system::XAM_OVERLAPPED*>(request->address + 8);
      if (io->result == 997) return;
      const auto device = rex::memory::load_and_swap<uint32_t>(runtime.memory()->TranslateVirtual(request->address));
      REXLOG_INFO("17559 storage diagnostic: completed result={:08X} extended={:08X} device={:08X}",
                  uint32_t(io->result), uint32_t(io->extended_error), device);
      runtime.memory()->SystemHeapFree(request->address);
      request->address = 0;
      return;
    }
    if (std::chrono::steady_clock::now() - start < std::chrono::seconds(seconds)) return;
    submitted = true;
    request->address = runtime.memory()->SystemHeapAlloc(8 + sizeof(rex::system::XAM_OVERLAPPED));
    if (!request->address) {
      REXLOG_ERROR("17559 storage diagnostic: guest allocation failed");
      return;
    }
    auto* base = runtime.memory()->virtual_membase();
    std::memset(base + request->address, 0, 8 + sizeof(rex::system::XAM_OVERLAPPED));
    rex::memory::store_and_swap<uint32_t>(base + request->address, 0xDEADBEEF);
    auto* ctx = rex::system::XThread::GetCurrentThread()->thread_state()->context();
    // Exercise the PPC export, including its 64-bit requested-space GPR and
    // real OVERLAPPED. The original app alone selects a device or cancels.
    ctx->r3.u64 = 0xFF;
    ctx->r4.u64 = 1;
    ctx->r5.u64 = 0;
    ctx->r6.u64 = 1024 * 1024;
    ctx->r7.u64 = request->address;
    ctx->r8.u64 = request->address + 8;
    __imp__XamShowDeviceSelectorUI(*ctx, base);
    REXLOG_INFO("17559 storage diagnostic: requested original selector result={:08X}", ctx->r3.u32);
    if (ctx->r3.u32 != 997) {
      runtime.memory()->SystemHeapFree(request->address);
      request->address = 0;
    }
  };
}
}  // namespace dash17559
