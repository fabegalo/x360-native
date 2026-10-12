// motocross_madness: logs the game's calls into the XAM avatar imports, at the
// call sites listed in analysis/motocross_madness_avatar_trace.toml (mid-asm
// hooks; the codegen declares each hook as void name(PPCRegister&...)). Diagnostic
// only: no register or memory is changed and the imports run unchanged.
#include <cstdint>

#include <rex/logging.h>
#include <rex/ppc/context.h>

namespace {

uint32_t Lo(const PPCRegister& r) { return static_cast<uint32_t>(r.u64); }

void Call(const char* import, const PPCRegister& r3, const PPCRegister& r4,
          const PPCRegister& r5, const PPCRegister& r6) {
  REXLOG_INFO("avatar-trace: {}(0x{:X}, 0x{:X}, 0x{:X}, 0x{:X})", import, Lo(r3), Lo(r4),
              Lo(r5), Lo(r6));
}

}  // namespace

namespace motocross {
// xam_aot_probe.cpp; no-op unless enabled. Registers are restored before it returns.
void MaybeRunXamAotProbe(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& r6,
                         PPCRegister& r7, PPCRegister& r8);
}

// 0x82957D30 (after "li r3,4"): the arguments of the only call, at 0x82957D34.
void x360n_avatar_trace_initialize_args(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5,
                                        PPCRegister& r6, PPCRegister& r7, PPCRegister& r8) {
  motocross::MaybeRunXamAotProbe(r3, r4, r5, r6, r7, r8);
  REXLOG_INFO("avatar-trace: XamAvatarInitialize(0x{:X}, 0x{:X}, 0x{:X}, callbacks=0x{:08X}, "
              "buffer=0x{:08X}, size=0x{:X}) from 0x82957D34",
              Lo(r3), Lo(r4), Lo(r5), Lo(r6), Lo(r7), Lo(r8));
}

// 0x82957D34, after the call returns.
void x360n_avatar_trace_initialize_result(PPCRegister& r3) {
  REXLOG_INFO("avatar-trace: XamAvatarInitialize -> 0x{:08X}", Lo(r3));
}

#define X360N_AVATAR_SITE(fn, import)                                                   \
  void fn(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& r6) {         \
    Call(import, r3, r4, r5, r6);                                                        \
  }
X360N_AVATAR_SITE(x360n_avatar_trace_shutdown, "XamAvatarShutdown")
X360N_AVATAR_SITE(x360n_avatar_trace_manifest_local_user, "XamAvatarGetManifestLocalUser")
X360N_AVATAR_SITE(x360n_avatar_trace_metadata_random, "XamAvatarGetMetadataRandom")
X360N_AVATAR_SITE(x360n_avatar_trace_body_type, "XamAvatarManifestGetBodyType")
X360N_AVATAR_SITE(x360n_avatar_trace_set_custom_asset, "XamAvatarSetCustomAsset")
X360N_AVATAR_SITE(x360n_avatar_trace_assets_result_size, "XamAvatarGetAssetsResultSize")
X360N_AVATAR_SITE(x360n_avatar_trace_get_assets, "XamAvatarGetAssets")
X360N_AVATAR_SITE(x360n_avatar_trace_generate_mipmaps, "XamAvatarGenerateMipMaps")
X360N_AVATAR_SITE(x360n_avatar_trace_load_animation, "XamAvatarLoadAnimation")
#undef X360N_AVATAR_SITE
