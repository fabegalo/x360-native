#include "system_ui_dependencies.h"
#include <rex/cvar.h>
#include <rex/kernel/xboxkrnl/module.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmodule.h>
#include <charconv>
#include <cstring>

// Only CreateProfile's original import is bridged. The game's loader and
// search paths remain untouched. Forward through the kernel's own thunk so
// reference counts, DllMain, flags, unload and failure contracts stay shared.
extern "C" REX_FUNC(__imp__XexLoadImage) {
  auto* state = REX_KERNEL_STATE();
  auto kernel = state->GetKernelModule<rex::kernel::xboxkrnl::XboxkrnlModule>("xboxkrnl.exe");
  auto* dispatcher = state->function_dispatcher();
  const auto address = kernel ? kernel->GetProcAddressByOrdinal(0x199, uint32_t(ctx.lr)) : 0;
  auto* load = address ? dispatcher->GetFunction(address) : nullptr;
  if (!load) { ctx.r3.u64 = 0xC000000F; return; }
  const auto* requested = ctx.r3.u32 ? reinterpret_cast<const char*>(base + ctx.r3.u32) : "";
  if (std::strcmp(requested, "vk.xex") != 0) { load(ctx, base); return; }
  uint32_t self = 0;
  const auto flag = rex::cvar::GetFlagByName("dash17559_ui_self");
  const auto parsed = std::from_chars(flag.data(), flag.data()+flag.size(), self);
  const auto original = parsed.ec == std::errc{} && self
      ? rex::system::XModule::GetFromHModule(state, base + self) : nullptr;
  const auto path = original ? dash17559::OriginalKeyboardDependency(original->path()) : std::string{};
  if (path.empty()) {
    REXLOG_ERROR("17559 CreateProfile: verified original XAM namespace unavailable for vk.xex");
    if (ctx.r6.u32) rex::memory::store_and_swap<uint32_t>(base + ctx.r6.u32, 0);
    ctx.r3.u64 = 0xC000000F; return;
  }
  const auto name = state->memory()->SystemHeapAlloc(uint32_t(path.size()+1));
  if (!name) { ctx.r3.u64 = 0xC0000017; return; }
  std::memcpy(base+name, path.c_str(), path.size()+1);
  ctx.r3.u64 = name;
  load(ctx, base);
  state->memory()->SystemHeapFree(name);
}
