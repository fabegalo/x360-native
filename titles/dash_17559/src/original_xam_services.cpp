#include "system_ui.h"
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/module.h>
#include <rex/kernel/object/kernel_thread.h>
#include <rex/kernel/heap.h>
#include <rex/kernel/types/io.h>
#include <rex/logging/log_category.h>
#include <rex/ppc/hook.h>
#include <rex/ppc/call.h>
#include <rex/xam/app_manager.h>
#include <rex/xam/title_lifecycle.h>
#include <rex/xam/system_ui.h>
#include <mutex>
#include <array>

namespace {
auto* GuestMemory() {
  auto* thread = rex::kernel::KernelThread::GetCallingThread();
  REX_CHECK(thread, "17559 original XAM import requires a guest calling thread");
  return thread->GetAddressSpace();
}
bool GuestSpan(rex::kernel::GuestAddressSpace* memory, uint32_t address, uint32_t size,
               uint32_t protection) {
  return address && memory->IsGuestRangeAccessible(address, size, protection);
}
}

namespace dash17559 {
namespace {
// Pure original XAM 17559 routines imported by dash.xex itself (the Storage
// list): run them from the original image, mapped on first use without its
// entry point, exactly as the Guide host maps it. No reimplementation of
// their collation/localization tables.
void CallOriginalXam(PPCContext& ctx, uint8_t* base, uint32_t address, const char* name,
                     uint32_t unavailable = 0x80004001) {
  static std::mutex mapping;
  auto& runtime = *REX_RUNTIME();
  auto& modules = runtime.GetModuleRegistry();
  {
    std::lock_guard lock(mapping);
    const auto& path = SystemXamPath();
    if (path.empty() || (!modules.GetModule(path) && !modules.LoadUserModule(path, false))) {
      REXLOG_ERROR("17559: original XAM image unavailable for {}", name);
      ctx.r3.u64 = unavailable;
      return;
    }
  }
  auto fn = runtime.FindFunction(address);
  if (!fn) {
    REXLOG_ERROR("17559: missing original {} {:08X}", name, address);
    ctx.r3.u64 = unavailable;
    return;
  }
  (*fn)(ctx, base);
}
}  // namespace
}  // namespace dash17559
// Original export 0x1E6 (816E35A8): (str1, str2, count, flags) UTF-16 collation.
REX_HOOK_RAW(__imp__XamUniSortCmpString) {
  dash17559::CallOriginalXam(ctx, base, 0x816E35A8, "XamUniSortCmpString");
}
// Original export 0x283 (816EE090): localized title/display strings of a
// content metadata block in the console language; 87 without metadata.
REX_HOOK_RAW(__imp__XamContentGetLocalizedString) {
  dash17559::CallOriginalXam(ctx, base, 0x816EE090, "XamContentGetLocalizedString");
}

REX_HOOK_RAW(__imp__XMsgInProcessCall) {
  if (ctx.r3.u32 != 0xFC || ctx.r4.u32 != 0x58072) {
    ctx.r3.u64 = rex::xam::XMsgInProcessCall_entry(
        ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32);
    return;
  }
  // Original FC dispatch: 818CCE08, case 58072 at 818CD4E0. Its size method
  // 818E1828 does not use `this`: it parses the supplied filters and computes
  // the actual URI/header/schema/task scratch size, including flag-dependent
  // buffers. Storage constructs this task even for a local device transfer.
  auto* memory = GuestMemory();
  const auto packet = ctx.r5.u32;
  if ((ctx.r6.u32 && ctx.r6.u32 != 20) ||
      !GuestSpan(memory, packet, 20, rex::kernel::kMemoryProtectRead)) {
    ctx.r3.u64 = 0x80070057;
    return;
  }
  const auto output = rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(packet + 16));
  if (!GuestSpan(memory, output, 4, rex::kernel::kMemoryProtectWrite)) {
    ctx.r3.u64 = 0x80070057;
    return;
  }
  ctx.r3.u64 = 0; // Unused original object parameter.
  ctx.r4.u64 = packet;
  dash17559::CallOriginalXam(ctx, base, 0x818E1828, "XOnlineGetWebServiceTaskBufferSize");
  if (dash17559::GetSystemUiSettings().storage_io_diagnostics.Get())
    REXLOG_INFO("17559 web task size: result={:08X} bytes={}", ctx.r3.u32,
                rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(output)));
}

// Export 020F returns a pointer into the original localized XUS table. The
// dashboard copies these labels when constructing its confirmation dialogs.
REX_HOOK_RAW(__imp__XamLookupCommonStringByIndex) {
  const auto index = ctx.r3.u32;
  dash17559::CallOriginalXam(ctx, base, 0x816E0BF0, "XamLookupCommonStringByIndex", 0);
  if (dash17559::GetSystemUiSettings().storage_io_diagnostics.Get()) {
    auto* memory = GuestMemory();
    if (!GuestSpan(memory, 0x81AA3EC4, 4,
                                               rex::kernel::kMemoryProtectRead)) return;
    const auto table = rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(0x81AA3EC4));
    if (GuestSpan(memory, table, 16, rex::kernel::kMemoryProtectRead)) {
      REXLOG_INFO("17559 common string: index={} result={:08X} table={:08X} count={} flags={:08X}",
                  index, ctx.r3.u32, table,
                  rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(table)),
                  rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(table + 12)));
    }
  }
}

REX_HOOK_RAW(__imp__XMsgCancelIORequest) {
  if (dash17559::GetSystemUiSettings().storage_io_diagnostics.Get()) {
    auto* memory = GuestMemory();
    if (GuestSpan(memory, ctx.r3.u32, sizeof(rex::kernel::XOVERLAPPED),
                                               rex::kernel::kMemoryProtectRead)) {
      const auto* io = memory->TranslateVirtual<rex::kernel::XOVERLAPPED*>(ctx.r3.u32);
      REXLOG_INFO("17559 cancel I/O: lr={:08X} io={:08X} wait={} result={:08X} event={:08X}",
                  uint32_t(ctx.lr), ctx.r3.u32, ctx.r4.u32, rex::kernel::XOverlappedGetResult(const_cast<rex::kernel::XOVERLAPPED*>(io)), uint32_t(io->event));
    }
  }
  rex::ppc::GuestToHostThunk<rex::xam::XMsgCancelIORequest_entry>(ctx, base);
}

REX_HOOK_RAW(__imp__XamContentRegisterChangeCallback) {
  const auto callback = ctx.r3.u32;
  rex::ppc::GuestToHostThunk<rex::xam::XamContentRegisterChangeCallback_entry>(ctx, base);
  if (dash17559::GetSystemUiSettings().storage_io_diagnostics.Get()) {
    auto* services = REX_INTERFACE(rex::xam::IXAMTitleServices);
    REX_CHECK(services, "17559 XAM services unavailable");
    REXLOG_INFO("17559 content callback register: lr={:08X} requested={:08X} stored={:08X}",
                uint32_t(ctx.lr), callback, services->GetSystemUI().content_change_callback.load());
  }
}

void Trace17559StorageContentEvent(PPCRegister& reason, PPCRegister& item) {
  if (!dash17559::GetSystemUiSettings().storage_io_diagnostics.Get()) return;
  auto* memory = GuestMemory();
  if (!GuestSpan(memory, item.u32, 512, rex::kernel::kMemoryProtectRead)) return;
  const auto* bytes = memory->TranslateVirtual<uint8_t*>(item.u32);
  const auto manager = rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(0x9296F5F8));
  REXLOG_INFO("17559 content event: reason={} item={:08X} manager={:08X} device={} title={:08X} size={} flags={:08X}",
              reason.u32, item.u32, manager, rex::memory::load_and_swap<uint32_t>(bytes),
              rex::memory::load_and_swap<uint32_t>(bytes + 0x140),
              rex::memory::load_and_swap<uint64_t>(bytes + 0x148),
              rex::memory::load_and_swap<uint32_t>(bytes + 508));
}

void Trace17559StorageContentDispatch(PPCRegister& reason, PPCRegister& item, PPCRegister& manager) {
  if (dash17559::GetSystemUiSettings().storage_io_diagnostics.Get())
    REXLOG_INFO("17559 content dispatch: reason={} item={:08X} manager={:08X}",
                reason.u32, item.u32, manager.u32);
}

void Trace17559StorageScene(PPCRegister& result, PPCRegister& name, PPCContext& context) {
  if (!dash17559::GetSystemUiSettings().storage_io_diagnostics.Get()) return;
  auto* memory = GuestMemory();
  uint32_t scene = 0;
  // The official mid-asm call moves r1 below the guest red zone while the hook runs.
  const auto address = uint32_t(context.r1.u64 + rex::ppc::kRedZone + 80);
  if (GuestSpan(memory, address, 4, rex::kernel::kMemoryProtectRead))
    scene = rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(address));
  REXLOG_INFO("17559 storage scene: name={:08X} result={:08X} scene={:08X}", name.u32, result.u32, scene);
}

void Trace17559StorageProgressInit(PPCRegister& result, PPCRegister& object) {
  if (dash17559::GetSystemUiSettings().storage_io_diagnostics.Get())
    REXLOG_INFO("17559 storage progress init: object={:08X} result={:08X}", object.u32, result.u32);
}

void Trace17559StorageProgressTick(PPCRegister& object) {
  if (!dash17559::GetSystemUiSettings().storage_io_diagnostics.Get()) return;
  auto* memory = GuestMemory();
  if (!GuestSpan(memory, object.u32, 228, rex::kernel::kMemoryProtectRead)) return;
  auto word = [&](uint32_t offset) {
    return rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(object.u32 + offset));
  };
  const std::array<uint32_t, 4> fields = {object.u32, word(176), word(192), word(224)};
  static thread_local std::array<uint32_t, 4> last{};
  if (fields == last) return;
  last = fields;
  REXLOG_INFO("17559 storage progress tick: object={:08X} state={} field192={:08X} io={:08X}",
              fields[0], fields[1], fields[2], fields[3]);
}
