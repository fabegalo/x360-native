#pragma once
#include <cstdint>
namespace rex { class Runtime; }
namespace dash1888 {
void InstallSystemUi(rex::Runtime& runtime);
void ReleaseSystemUi();
#ifdef X360N_1888_UI_DIAGNOSTIC
// Explicit CPU-only test entry, absent from the dashboard build. Creates and
// destroys real original scenes, with optional CPU drawing, scripted input,
// animation and timer checks. Never creates a window or presents frames.
uint32_t ProbeOriginalApplication(rex::Runtime& runtime, uint32_t message, uint32_t* scene,
                                  bool record_draw = false, bool advance_animation = false);
#endif
}
