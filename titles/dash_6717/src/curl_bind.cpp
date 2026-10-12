// xam ordinal 0xAE6 for Retail 2.0.6717.0 (thunk 0x92739D94; the pinned SDK's
// export table has 0xAE5 and 0xAE7 but not 0xAE6, so the codegen names it
// sub_<thunk> and this profile defines it).
//
// Evidence, from this dash.xex (x360n-content disasm; docs/dashboard.md §0.15):
// - The only call, at 0x9210F1F0, passes r3 = 0x927FC914 (returned by 0x92588028):
//   seven functions of dash.xex (0x92587390 ... 0x92587C98) and then the word 0x18.
//   The caller does not read a result; it calls 0x925881D0 next.
// - All seven use the dashboard's own cache state at 0x928C6BE8 (initialized by
//   0x925881D0) under its critical section at 0x928E6C98: the dashboard supplies
//   the implementation and hands the table to XAM.
// - Xenia Canary 2372a54105d9 (xam_table.inc) names 0xAE6 CurlBindFunctionTable.
//   xam.xex 2.0.17559.0 no longer exports it (export offset 0) and dash.xex
//   2.0.17559.0 does not import it.
// The runtime's XAM has nothing that would call through such a table (its Curl*
// exports are stubs, and this dashboard imports none of them), so the binding is
// recorded, not used. No result is written: the only caller does not read one and
// its type is not known.
#include "curl_bind.h"

#include <cstdint>

#include <rex/logging.h>
#include <rex/logging/assert.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

namespace dash6717 {
namespace {
CurlBinding g_binding;
}  // namespace

const CurlBinding& LastCurlBinding() {
  return g_binding;
}

}  // namespace dash6717

extern "C" REX_FUNC(sub_92739D94) {
  const uint32_t table = ctx.r3.u32;
  if (table == 0) {
    REX_FATAL("dash_6717: xam 0xAE6 (CurlBindFunctionTable per Xenia) called with a null table; "
              "LR={:08X}",
              static_cast<uint32_t>(ctx.lr));
  }
  dash6717::CurlBinding binding;
  binding.table = table;
  for (uint32_t i = 0; i < dash6717::CurlBinding::kWords; ++i) {
    binding.words[i] = __builtin_bswap32(*reinterpret_cast<const uint32_t*>(base + table + 4 * i));
  }
  binding.calls = dash6717::g_binding.calls + 1;
  dash6717::g_binding = binding;
  REXLOG_INFO(
      "dash_6717: xam 0xAE6 bound table 0x{:08X} ({:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}, "
      "{:08X}); recorded only, no XAM consumer",
      table, binding.words[0], binding.words[1], binding.words[2], binding.words[3],
      binding.words[4], binding.words[5], binding.words[6], binding.words[7]);
}
