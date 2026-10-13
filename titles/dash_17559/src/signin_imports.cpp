#include <rex/logging/assert.h>
#include <rex/ppc/func.h>

// Original Sign-in 17559 import table: ordinal 054B is absent from the pinned
// SDK. Its XAM implementation (816DEF20) uses the profile service, outside C7.1.
// Keep it an explicit trap, just like the dashboard's unresolved imports; a
// guessed boolean or HRESULT here could falsely accept a profile operation.
extern "C" REX_FUNC(sub_9011DBEC) {
  REX_FATAL("17559 Sign-in: unsupported XAM ordinal 054B (profile service), LR={:08X}",
            uint32_t(ctx.lr));
}
