#include <algorithm>
#include <rex/logging.h>
#include <cstring>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>

// XuiGamerCard calls XAM services internally, without an import thunk. Use the
// same identity and settings service as the dashboard and system applications.
#define BRIDGE_XAM_SERVICE(address, name) \
  extern "C" REX_FUNC(__imp__##name); \
  extern "C" REX_FUNC(sub_##address) { __imp__##name(ctx, base); }
BRIDGE_XAM_SERVICE(816D8590, XamUserGetXUID)
BRIDGE_XAM_SERVICE(816D8698, XamUserGetIndexFromXUID)
BRIDGE_XAM_SERVICE(816D86A8, XamUserGetGamerTag)
BRIDGE_XAM_SERVICE(816DD040, XamUserGetName)
BRIDGE_XAM_SERVICE(816DD118, XamUserGetSigninState)
BRIDGE_XAM_SERVICE(816DDB20, XamUserGetSigninInfo)
BRIDGE_XAM_SERVICE(816B1F28, XamUserReadProfileSettings)
BRIDGE_XAM_SERVICE(816B16A8, XamUserReadProfileSettingsEx)
// Tile reads complete asynchronously in XAM; the gamercard keeps loading its
// statistics when a picture is unavailable.
BRIDGE_XAM_SERVICE(81670C00, XamReadTileEx)
BRIDGE_XAM_SERVICE(81670B68, XamReadTile)
#undef BRIDGE_XAM_SERVICE

// The original HUD polls the system input queue, rather than the suspended
// title's queue. Bridge the device boundary; original XuiProcessInput retains
// focus, navigation, button dispatch and dialog completion.
extern "C" REX_FUNC(__imp__XamInputGetKeystrokeHudEx);
extern "C" REX_FUNC(__imp__sub_816F4AB8);
extern "C" REX_FUNC(sub_816F4AB8) {
  if (ctx.r4.u32 == 0x400000FFu) {
    ctx.r3.u64 = 0xFF; // ANY_USER in the native flags, a value in the HUD Ex ABI.
    __imp__XamInputGetKeystrokeHudEx(ctx, base);
  } else
    __imp__sub_816F4AB8(ctx, base);
}

// The XUI library's private malloc/free/size/realloc boundary (tag 0x248E).
// Back allocations with the existing guest heap; never start XAM's system heap
// or replace account services. Strong symbols override only these AOT functions.
extern "C" REX_FUNC(sub_817AB898) {
  const auto size = ctx.r3.u32;
  ctx.r3.u64 = REX_KERNEL_MEMORY()->SystemHeapAlloc(size);
  if (!ctx.r3.u32) REXLOG_ERROR("17559 XUI allocation failed size={} lr={:08X}", size, uint32_t(ctx.lr));
}
extern "C" REX_FUNC(sub_817AB8A0) {
  uint32_t size = 0;
  if (ctx.r3.u32) REX_KERNEL_MEMORY()->LookupHeap(ctx.r3.u32)->QuerySize(ctx.r3.u32, &size);
  ctx.r3.u64 = size;
}
extern "C" REX_FUNC(sub_817AB8C0) {
  if (ctx.r3.u32) REX_KERNEL_MEMORY()->SystemHeapFree(ctx.r3.u32);
}
extern "C" REX_FUNC(sub_817AB8D8) {
  auto* memory = REX_KERNEL_MEMORY();
  const auto old = ctx.r3.u32, requested = ctx.r4.u32;
  if (!requested) { if (old) memory->SystemHeapFree(old); ctx.r3.u64 = 0; return; }
  const auto next = memory->SystemHeapAlloc(requested);
  if (next && old) {
    uint32_t size = 0;
    memory->LookupHeap(old)->QuerySize(old, &size);
    std::memcpy(base + next, base + old, std::min(size, requested));
    memory->SystemHeapFree(old);
  }
  ctx.r3.u64 = next;
}
// XAM's self-module getter, used by its original section:// resource builders.
#include <rex/cvar.h>
REXCVAR_DEFINE_UINT32(dash17559_ui_self, 0, "Dashboard 17559", "Verified original XUI module handle");
extern "C" REX_FUNC(sub_816E03A8) { ctx.r3.u64 = REXCVAR_GET(dash17559_ui_self); }

#include <rex/logging.h>
// Temporary stage diagnostics: execute the original body and preserve its result.
#define TRACE_XUI_STAGE(address) \
  extern "C" REX_FUNC(__imp__sub_##address); \
  extern "C" REX_FUNC(sub_##address) { \
    __imp__sub_##address(ctx, base); \
    REXLOG_INFO("17559 XUI stage " #address " -> {:08X}", ctx.r3.u32); \
  }
TRACE_XUI_STAGE(81782328)
TRACE_XUI_STAGE(8177DAC8)
TRACE_XUI_STAGE(817BC088)
TRACE_XUI_STAGE(8177F760)
TRACE_XUI_STAGE(817986A8)
TRACE_XUI_STAGE(81780EA0)
TRACE_XUI_STAGE(817DA4A0)
TRACE_XUI_STAGE(817DAF80)
TRACE_XUI_STAGE(817DB6D8)
TRACE_XUI_STAGE(817E0C10)
TRACE_XUI_STAGE(817C46A8)
TRACE_XUI_STAGE(817B8BD0)
TRACE_XUI_STAGE(816954D0)
TRACE_XUI_STAGE(81693568)
TRACE_XUI_STAGE(81694830)
#undef TRACE_XUI_STAGE

// The original D3D path calls these internal XMem entrypoints directly.
// Route only this allocator boundary to the same SDK service as external imports.
extern "C" REX_FUNC(__imp__XMemAlloc);
extern "C" REX_FUNC(__imp__XMemFree);
extern "C" REX_FUNC(__imp__sub_816E69E0);
extern "C" REX_FUNC(__imp__sub_816E6AB0);
extern "C" REX_FUNC(sub_816E69E0) {
  // Physical requests use the original alignment/protection decoder and
  // MmAllocatePhysicalMemoryEx. Only the unavailable virtual XAM heap is bridged.
  if (ctx.r4.u32 & 0x80000000u) __imp__sub_816E69E0(ctx, base);
  else __imp__XMemAlloc(ctx, base);
}
extern "C" REX_FUNC(sub_816E6AB0) {
  if (ctx.r3.u32 >= 0xA0000000u) __imp__sub_816E6AB0(ctx, base);
  else __imp__XMemFree(ctx, base);
}
// XAM realloc 816E6BB8(pointer, size). A null pointer reached XAM's own heap,
// which this host never starts, so XuiGamerCard could not grow its request
// list. Virtual blocks use the bridged heap above; physical blocks keep the
// original path.
extern "C" REX_FUNC(__imp__sub_816E6BB8);
extern "C" REX_FUNC(sub_816E6BB8) {
  const auto old = ctx.r3.u32, requested = ctx.r4.u32;
  if (old >= 0xA0000000u) { __imp__sub_816E6BB8(ctx, base); return; }
  auto* memory = REX_KERNEL_MEMORY();
  const auto next = requested ? memory->SystemHeapAlloc(requested, 0x20) : 0;
  if ((next || !requested) && old) {
    uint32_t size = 0;
    memory->LookupHeap(old)->QuerySize(old, &size);
    if (next) std::memcpy(base + next, base + old, std::min(size, requested));
    memory->SystemHeapFree(old);
  }
  ctx.r3.u64 = next;
}

extern "C" REX_FUNC(__imp__sub_817B8A58);
extern "C" REX_FUNC(sub_817B8A58) {
  const auto descriptor = ctx.r3.u32;
  auto read_name = [&](uint32_t pointer) {
    std::string result;
    if (pointer) for (unsigned i = 0; i < 128; ++i) {
      const auto ch = rex::memory::load_and_swap<uint16_t>(base + pointer + i * 2);
      if (!ch) break;
      result.push_back(ch < 128 ? char(ch) : '?');
    }
    return result;
  };
  const auto name = read_name(rex::memory::load_and_swap<uint32_t>(base + descriptor + 4));
  const auto parent = read_name(rex::memory::load_and_swap<uint32_t>(base + descriptor + 8));
  __imp__sub_817B8A58(ctx, base);
  if (static_cast<int32_t>(ctx.r3.u32) < 0)
    REXLOG_ERROR("17559 XUI class '{}' base '{}' registration -> {:08X}", name, parent, ctx.r3.u32);
}

// The section:// resolver asks for the title hosted below the HUD. In the
// original manager, 8169EDAC snapshots the title returned by 816E03B8. Our
// host is the SDK, so obtain that same identity from its actual executable,
// without constructing a second XAM title/worker manager.
extern "C" REX_FUNC(__imp__sub_8169CE70);
extern "C" REX_FUNC(sub_8169CE70) {
  const auto manager = rex::memory::load_and_swap<uint32_t>(base + 0x81A9F850);
  if (manager) __imp__sub_8169CE70(ctx, base);
  else ctx.r3.u64 = REX_KERNEL_STATE()->title_id();
}

extern "C" REX_FUNC(__imp__sub_817DA648);
extern "C" REX_FUNC(sub_817DA648) {
  std::string path;
  for (unsigned i=0; ctx.r3.u32 && i<512; ++i) {
    auto c=rex::memory::load_and_swap<uint16_t>(base+ctx.r3.u32+2*i);
    if (!c) break;
    path.push_back(c<128 ? char(c) : '?');
  }
  __imp__sub_817DA648(ctx, base);
  REXLOG_INFO("17559 XUI resolve '{}' -> {:08X}", path, ctx.r3.u32);
}

// HUD control constructors use XAM's private C++ allocator (tag 0x500).
// Pair it with its original delete boundary, backed by the SDK guest heap.
extern "C" REX_FUNC(sub_816E6B18) {
  ctx.r3.u64 = REX_KERNEL_MEMORY()->SystemHeapAlloc(std::max(1u, ctx.r3.u32));
}
// Its malloc 816E6B68 (flags 0x500) reached the same unstarted XAM heap and
// returned null, so XUI data sources could not copy their listener lists and
// property changes were never delivered. It pairs with the delete below.
extern "C" REX_FUNC(sub_816E6B68) {
  ctx.r3.u64 = REX_KERNEL_MEMORY()->SystemHeapAlloc(std::max(1u, ctx.r3.u32));
}
extern "C" REX_FUNC(sub_816E6B10) {
  if (ctx.r3.u32) REX_KERNEL_MEMORY()->SystemHeapFree(ctx.r3.u32);
}

// Internal XamAllocEx/XamFree calls share the SDK allocation contract.
extern "C" REX_FUNC(__imp__XamAllocEx);
extern "C" REX_FUNC(__imp__sub_816E67E8);
extern "C" REX_FUNC(__imp__XamFree);
extern "C" REX_FUNC(__imp__sub_816E6858);
extern "C" REX_FUNC(sub_816E6858) {
  if (ctx.r3.u32 >= 0xA0000000u) __imp__sub_816E6858(ctx, base);
  else __imp__XamFree(ctx, base);
}
extern "C" REX_FUNC(sub_816E67E8) {
  const auto tag=ctx.r3.u32, flags=ctx.r4.u32, size=ctx.r5.u32, output=ctx.r6.u32, caller=uint32_t(ctx.lr);
  // The original system compositor requests tagged physical command memory.
  // Preserve its physical allocator, as for XMemAlloc above; the SDK's XAM
  // virtual-pool adapter explicitly rejects this allocation class.
  if (flags & 0x40000000u) __imp__sub_816E67E8(ctx, base);
  else __imp__XamAllocEx(ctx, base);
  REXLOG_INFO("17559 XUI XamAllocEx tag={:08X} flags={:08X} size={} caller={:08X} -> {:08X} ptr={:08X}", tag, flags, size, caller, ctx.r3.u32, output ? rex::memory::load_and_swap<uint32_t>(base+output) : 0);
}

namespace {
std::string XuiDiagnosticName(uint8_t* base, uint32_t p) {
  std::string name;
  for (unsigned i=0; p && i<128; ++i) {
    auto c=rex::memory::load_and_swap<uint16_t>(base+p+2*i);
    if (!c) break;
    name.push_back(c<128 ? char(c) : '?');
  }
  return name;
}
}
extern "C" REX_FUNC(__imp__sub_817C3AD8);
extern "C" REX_FUNC(sub_817C3AD8) {
  const auto name=XuiDiagnosticName(base,rex::memory::load_and_swap<uint32_t>(base+ctx.r3.u32));
  __imp__sub_817C3AD8(ctx,base);
  if (int32_t(ctx.r3.u32)<0) REXLOG_ERROR("17559 XUI scene control {} -> {:08X}",name,ctx.r3.u32);
}
extern "C" REX_FUNC(__imp__sub_817B8B58);
extern "C" REX_FUNC(sub_817B8B58) {
  const auto name=XuiDiagnosticName(base,ctx.r3.u32);
  __imp__sub_817B8B58(ctx,base);
  if (int32_t(ctx.r3.u32)<0) REXLOG_ERROR("17559 XUI object create {} -> {:08X}",name,ctx.r3.u32);
}
extern "C" REX_FUNC(__imp__sub_817BEFD8);
extern "C" REX_FUNC(sub_817BEFD8) {
  const auto property=ctx.r4.u32;
  const auto flags=rex::memory::load_and_swap<uint32_t>(base+property);
  const auto descriptor=rex::memory::load_and_swap<uint32_t>(base+property+4);
  __imp__sub_817BEFD8(ctx,base);
  if (int32_t(ctx.r3.u32)<0) REXLOG_ERROR("17559 XUI property flags={:08X} descriptor={:08X} -> {:08X}",flags,descriptor,ctx.r3.u32);
}

// XuiGamerCard factory 81728A50 uses XamAlloc(0x20000000,20,out).
// Its virtual allocation and existing delete boundary must use the same heap.
extern "C" REX_FUNC(__imp__XamAlloc);
extern "C" REX_FUNC(__imp__sub_816E67A0);
extern "C" REX_FUNC(sub_816E67A0) {
  const auto flags=ctx.r3.u32, size=ctx.r4.u32;
  if (flags == 0x20000000u) __imp__XamAlloc(ctx,base);
  else __imp__sub_816E67A0(ctx,base);
  REXLOG_INFO("17559 XUI XamAlloc flags={:08X} size={} -> {:08X}",flags,size,ctx.r3.u32);
}
