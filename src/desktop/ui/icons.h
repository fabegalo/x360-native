#pragma once
// Vector icons drawn from primitives at any scale; no icon font is shipped.
// Names: home, library, download, dashboard, settings, info, language, play,
// folder, file, search, refresh, cancel, add, check, warning, error, gamepad,
// chip, save, tools, disc, remove, package.
#include "theme.h"

namespace x360n::ui {
// Draws the icon centered in r at `size` (96-DPI pixels, scaled).
void Icon(ImDrawList* dl, const char* name, const ImRect& r, Color color, float size = 16.0f);
}  // namespace x360n::ui
