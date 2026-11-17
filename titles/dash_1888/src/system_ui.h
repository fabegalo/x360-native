#pragma once
#include <cstdint>
#include <rex/setting.h>
namespace rex::kernel { class TitleRuntime; }
namespace dash1888 {
class SystemUiSettings final : public rex::SettingOwner {
 public:
  SystemUiSettings() : SettingOwner("dash1888") {}
  REX_SETTING(bool, boot_intro, false, "Run the original Retail 1888 boot animation");
  REX_SETTING(rex::u32, guide_probe_seconds, 0, "Request original Guide after 1..120 seconds");
  REX_SETTING(rex::u32, guide_probe_cycles, 1, "Original Guide diagnostic cycles, 1..3");
  REX_SETTING(bool, ui_lifecycle_trace, false, "Trace original scene ownership during destruction");
  REX_SETTING(rex::u32, input_probe, 0, "Three scripted Guide cycles: 1 closes by Guide, 2 by B");
  REX_SETTING(bool, ui_input_trace, false, "Trace original system UI input");
};
inline SystemUiSettings& GetSystemUiSettings() { static SystemUiSettings settings; return settings; }

void InstallSystemUi(rex::kernel::TitleRuntime& runtime);
void ReleaseSystemUi(rex::kernel::TitleRuntime& runtime);
#ifdef X360N_1888_UI_DIAGNOSTIC
// Explicit CPU-only test entry, absent from the dashboard build. Creates and
// destroys real original scenes, with optional CPU drawing, scripted input,
// animation and timer checks. Never creates a window or presents frames.
uint32_t ProbeOriginalApplication(rex::kernel::TitleRuntime& runtime, uint32_t message, uint32_t* scene,
                                  bool record_draw = false, bool advance_animation = false);
#endif
}
