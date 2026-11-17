#pragma once
#include <rex/hid/input_system.h>
#include <rex/logging/log_category.h>

namespace x360n {
// Both original dashboard hosts support controllers whose firmware emits
// GUIDE with BACK+START. Explicit user configuration still takes precedence.
inline void ConfigureGuideButton() {
  auto* input = REX_INTERFACE(rex::hid::IInputSystem);
  REX_CHECK(input, "Original system UI requires the console input service");
  auto& settings = input->GetSettings();
  if (settings.guide_button.GetLayer() == rex::SettingLayer::kDefault)
    REX_CHECK(settings.guide_button.SetText(rex::SettingLayer::kTitle, "true"), "Cannot configure Guide input");
  if (settings.guide_chord_ms.GetLayer() == rex::SettingLayer::kDefault)
    REX_CHECK(settings.guide_chord_ms.SetText(rex::SettingLayer::kTitle, "150"), "Cannot configure Guide chord");
  if (settings.guide_chord_users.GetLayer() == rex::SettingLayer::kDefault)
    REX_CHECK(settings.guide_chord_users.SetText(rex::SettingLayer::kTitle, "15"), "Cannot configure Guide users");
}
}  // namespace x360n
