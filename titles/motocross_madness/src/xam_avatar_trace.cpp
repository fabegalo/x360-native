// motocross_madness: diagnostic hooks inside the recompiled original XAM 2.0.17559.0
// (analysis/xam17559_avatar_trace.toml), compiled into motocross_madness_xam. They only
// log r3; no register or memory is changed.
#include <cstdint>

#include <rex/logging.h>
#include <rex/ppc/context.h>

#define X360N_XAM_STEP(fn, what)                                                         \
  void fn(PPCRegister& r3) {                                                             \
    REXLOG_INFO("xam-avatar-trace: {} -> 0x{:08X}", what, static_cast<uint32_t>(r3.u64)); \
  }
X360N_XAM_STEP(x360n_xam_avatar_init_ready, "0x8196B9A8: 0x8196D5D0()")
X360N_XAM_STEP(x360n_xam_avatar_init_buffer, "0x8196B9A8: 0x81968640(buffer, 0x100000)")
X360N_XAM_STEP(x360n_xam_avatar_init_step3, "0x8196B9A8: 0x8196B3D8()")
X360N_XAM_STEP(x360n_xam_avatar_init_pack, "0x8196B9A8: result (after 0x8196B8C8)")
#undef X360N_XAM_STEP
