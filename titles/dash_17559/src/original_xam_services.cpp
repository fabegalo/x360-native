#include "system_ui.h"
#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/user_module.h>
#include <mutex>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/kernel/xam/offline_achievement_helpers.h>
#include <rex/system/xio.h>
#include <rex/system/xam/app_manager.h>
#include <array>

REXCVAR_DECLARE(bool, dash17559_storage_io_diagnostics);
namespace rex::kernel::xam {
uint32_t XMsgCancelIORequest_entry(rex::MappedPtr<rex::system::XAM_OVERLAPPED>, uint32_t);
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
  auto& runtime = *rex::Runtime::instance();
  auto* state = runtime.kernel_state();
  {
    std::lock_guard lock(mapping);
    const auto& path = SystemXamPath();
    if (path.empty() || (!state->GetModule(path) && !state->LoadUserModule(path, false))) {
      REXLOG_ERROR("17559: original XAM image unavailable for {}", name);
      ctx.r3.u64 = unavailable;
      return;
    }
  }
  auto* fn = runtime.function_dispatcher()->GetFunction(address);
  if (!fn) {
    REXLOG_ERROR("17559: missing original {} {:08X}", name, address);
    ctx.r3.u64 = unavailable;
    return;
  }
  fn(ctx, base);
}
}  // namespace
}  // namespace dash17559
// Original export 0x1E6 (816E35A8): (str1, str2, count, flags) UTF-16 collation.
extern "C" REX_FUNC(__imp__XamUniSortCmpString) {
  dash17559::CallOriginalXam(ctx, base, 0x816E35A8, "XamUniSortCmpString");
}
// Original export 0x283 (816EE090): localized title/display strings of a
// content metadata block in the console language; 87 without metadata.
extern "C" REX_FUNC(__imp__XamContentGetLocalizedString) {
  dash17559::CallOriginalXam(ctx, base, 0x816EE090, "XamContentGetLocalizedString");
}

extern "C" REX_FUNC(__imp__XMsgInProcessCall) {
  auto& runtime = *rex::Runtime::instance();
  if (ctx.r3.u32 != 0xFC || ctx.r4.u32 != 0x58072) {
    ctx.r3.u64 = runtime.kernel_state()->app_manager()->DispatchMessageSync(
        ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32);
    return;
  }
  // Original FC dispatch: 818CCE08, case 58072 at 818CD4E0. Its size method
  // 818E1828 does not use `this`: it parses the supplied filters and computes
  // the actual URI/header/schema/task scratch size, including flag-dependent
  // buffers. Storage constructs this task even for a local device transfer.
  auto* memory = runtime.memory();
  const auto packet = ctx.r5.u32;
  if ((ctx.r6.u32 && ctx.r6.u32 != 20) ||
      !rex::kernel::xam::AchievementGuestSpan(memory, packet, 20, rex::memory::kMemoryProtectRead)) {
    ctx.r3.u64 = 0x80070057;
    return;
  }
  const auto output = rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(packet + 16));
  if (!rex::kernel::xam::AchievementGuestSpan(memory, output, 4, rex::memory::kMemoryProtectWrite)) {
    ctx.r3.u64 = 0x80070057;
    return;
  }
  ctx.r3.u64 = 0; // Unused original object parameter.
  ctx.r4.u64 = packet;
  dash17559::CallOriginalXam(ctx, base, 0x818E1828, "XOnlineGetWebServiceTaskBufferSize");
  if (REXCVAR_GET(dash17559_storage_io_diagnostics))
    REXLOG_INFO("17559 web task size: result={:08X} bytes={}", ctx.r3.u32,
                rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(output)));
}

// Export 020F returns a pointer into the original localized XUS table. The
// dashboard copies these labels when constructing its confirmation dialogs.
extern "C" REX_FUNC(__imp__XamLookupCommonStringByIndex) {
  const auto index = ctx.r3.u32;
  dash17559::CallOriginalXam(ctx, base, 0x816E0BF0, "XamLookupCommonStringByIndex", 0);
  if (REXCVAR_GET(dash17559_storage_io_diagnostics)) {
    auto* memory = rex::Runtime::instance()->memory();
    if (!rex::kernel::xam::AchievementGuestSpan(memory, 0x81AA3EC4, 4,
                                               rex::memory::kMemoryProtectRead)) return;
    const auto table = rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(0x81AA3EC4));
    if (rex::kernel::xam::AchievementGuestSpan(memory, table, 16, rex::memory::kMemoryProtectRead)) {
      REXLOG_INFO("17559 common string: index={} result={:08X} table={:08X} count={} flags={:08X}",
                  index, ctx.r3.u32, table,
                  rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(table)),
                  rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(table + 12)));
    }
  }
}

extern "C" REX_FUNC(__imp__XMsgCancelIORequest) {
  if (REXCVAR_GET(dash17559_storage_io_diagnostics)) {
    auto* memory = rex::Runtime::instance()->memory();
    if (rex::kernel::xam::AchievementGuestSpan(memory, ctx.r3.u32, sizeof(rex::system::XAM_OVERLAPPED),
                                               rex::memory::kMemoryProtectRead)) {
      const auto* io = memory->TranslateVirtual<rex::system::XAM_OVERLAPPED*>(ctx.r3.u32);
      REXLOG_INFO("17559 cancel I/O: lr={:08X} io={:08X} wait={} result={:08X} event={:08X}",
                  uint32_t(ctx.lr), ctx.r3.u32, ctx.r4.u32, uint32_t(io->result), uint32_t(io->event));
    }
  }
  rex::ppc::HostToGuestFunction<rex::kernel::xam::XMsgCancelIORequest_entry>(ctx, base);
}

void Trace17559StorageScene(PPCRegister& result, PPCRegister& name, PPCRegister& stack) {
  if (!REXCVAR_GET(dash17559_storage_io_diagnostics)) return;
  auto* memory = rex::Runtime::instance()->memory();
  uint32_t scene = 0;
  if (rex::kernel::xam::AchievementGuestSpan(memory, stack.u32 + 80, 4, rex::memory::kMemoryProtectRead))
    scene = rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(stack.u32 + 80));
  REXLOG_INFO("17559 storage scene: name={:08X} result={:08X} scene={:08X}", name.u32, result.u32, scene);
}

void Trace17559StorageProgressInit(PPCRegister& result, PPCRegister& object) {
  if (REXCVAR_GET(dash17559_storage_io_diagnostics))
    REXLOG_INFO("17559 storage progress init: object={:08X} result={:08X}", object.u32, result.u32);
}

void Trace17559StorageProgressTick(PPCRegister& object) {
  if (!REXCVAR_GET(dash17559_storage_io_diagnostics)) return;
  auto* memory = rex::Runtime::instance()->memory();
  if (!rex::kernel::xam::AchievementGuestSpan(memory, object.u32, 228, rex::memory::kMemoryProtectRead)) return;
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
