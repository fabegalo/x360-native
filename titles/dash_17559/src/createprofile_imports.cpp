#include "system_ui_dependencies.h"
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/module.h>
#include <rex/kernel/object/module_object.h>
#include <rex/kernel/object/object_table.h>
#include <rex/kernel/heap.h>
#include <rex/logging/log_category.h>
#include <rex/ppc/hook.h>
#include <cstring>

// Forward through the kernel's own thunk so references, DllMain, flags, unload
// and failure contracts stay shared. Release object pins before guest callbacks.
REX_HOOK_RAW(__imp__XexLoadImage) {
  auto& runtime = *REX_RUNTIME();
  uint32_t address = 0;
  if (const auto handle = runtime.GetModuleRegistry().FindKernelModule("xboxkrnl.exe")) {
    auto kernel = runtime.GetObjectTable().Get<rex::kernel::ModuleObject>(*handle);
    if (kernel) address = kernel->GetExportAddress(runtime, 0x199, uint32_t(ctx.lr));
  }
  const auto load = runtime.FindFunction(address);
  if (!address || !load) { ctx.r3.u64 = 0xC000000F; return; }
  const auto requested = ctx.r3.u32 ? runtime.GetGuestMemory().ReadString(ctx.r3.u32)
                                  : rex::Result<std::string>(std::string{});
  if (!requested) { ctx.r3.u64 = 0xC000000D; return; }
  if (*requested != "vk.xex") { (*load)(ctx, base); return; }
  std::string path;
  const auto self = dash17559::OriginalUiModuleHandle();
  if (self) {
    const auto bytes = runtime.GetGuestMemory().GetReadOnlySpan(self, sizeof(rex::kernel::X_LDR_DATA_TABLE_ENTRY));
    if (bytes) {
      const auto handle = rex::kernel::ModuleObject::FromHModule(runtime, const_cast<std::byte*>(bytes->data()));
      auto original = runtime.GetObjectTable().Get<rex::kernel::ModuleObject>(handle);
      if (original) path = dash17559::OriginalKeyboardDependency(original->path);
    }
  }
  if (path.empty()) {
    REXLOG_ERROR("17559 CreateProfile: verified original XAM namespace unavailable for vk.xex");
    if (ctx.r6.u32) (void)runtime.GetGuestMemory().Store(ctx.r6.u32, rex::be_u32(0));
    ctx.r3.u64 = 0xC000000F; return;
  }
  auto& heap = runtime.GetGuestHeap();
  const auto name = heap.SystemHeapAlloc(uint32_t(path.size()+1));
  if (!name) { ctx.r3.u64 = 0xC0000017; return; }
  rex::ScopeGuard free_name([&] { heap.SystemHeapFree(name); });
  std::memcpy(base+name, path.c_str(), path.size()+1);
  ctx.r3.u64 = name;
  (*load)(ctx, base);
}
