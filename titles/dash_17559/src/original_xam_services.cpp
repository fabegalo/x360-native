#include "system_ui.h"
#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/user_module.h>
#include <mutex>

namespace dash17559 {
namespace {
// Pure original XAM 17559 routines imported by dash.xex itself (the Storage
// list): run them from the original image, mapped on first use without its
// entry point, exactly as the Guide host maps it. No reimplementation of
// their collation/localization tables.
void CallOriginalXam(PPCContext& ctx, uint8_t* base, uint32_t address, const char* name) {
  static std::mutex mapping;
  auto& runtime = *rex::Runtime::instance();
  auto* state = runtime.kernel_state();
  {
    std::lock_guard lock(mapping);
    const auto& path = SystemXamPath();
    if (path.empty() || (!state->GetModule(path) && !state->LoadUserModule(path, false))) {
      REXLOG_ERROR("17559: original XAM image unavailable for {}", name);
      ctx.r3.u64 = 0x80004001;
      return;
    }
  }
  auto* fn = runtime.function_dispatcher()->GetFunction(address);
  if (!fn) {
    REXLOG_ERROR("17559: missing original {} {:08X}", name, address);
    ctx.r3.u64 = 0x80004001;  // E_NOTIMPL, never a fabricated result
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
