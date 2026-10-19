#pragma once
#include <rex/cvar.h>

namespace x360n {
// Both original dashboard hosts support controllers whose firmware emits
// GUIDE with BACK+START. Explicit user configuration still takes precedence.
inline void ConfigureGuideButton() {
  rex::cvar::SetFlagByName("guide_button", "true");
  if (rex::cvar::GetFlagSource("guide_chord_ms") == rex::cvar::Source::kDefault)
    rex::cvar::SetFlagByName("guide_chord_ms", "150");
  if (rex::cvar::GetFlagSource("guide_chord_users") == rex::cvar::Source::kDefault)
    rex::cvar::SetFlagByName("guide_chord_users", "15");
}
}  // namespace x360n
