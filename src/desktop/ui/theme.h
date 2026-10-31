#pragma once
// Visual language of the launcher: palette, fonts, metrics and drawing
// primitives over Dear ImGui. Same palette and proportions on every system.
#include <imgui.h>
#include <imgui_internal.h>
#include <string>

namespace x360n::ui {
using Color = ImU32;
constexpr Color Rgb(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }
struct Palette {
  Color bg, sidebar, surface, surface2, border, text, muted, faint;
  Color accent, accent_hover, accent_press, accent_text, on_accent;
  Color ok, warn, err, info;
};
// Xbox-inspired green on graphite. Contrast (text on surface) is above 11:1;
// muted text on surface is above 5:1; white on the accent fill is above 5:1.
inline constexpr Palette P{
  Rgb(13, 15, 18),   Rgb(10, 12, 14),   Rgb(22, 26, 31),   Rgb(31, 36, 43),   Rgb(46, 53, 62),
  Rgb(236, 240, 245), Rgb(174, 184, 196), Rgb(130, 142, 157),
  Rgb(16, 124, 16),  Rgb(21, 143, 21),  Rgb(12, 104, 12),  Rgb(96, 214, 96),  Rgb(255, 255, 255),
  Rgb(96, 214, 96),  Rgb(245, 183, 64), Rgb(240, 96, 96),  Rgb(104, 168, 255)};
Color Mix(Color base, Color over, int percent);

// Noto Sans CJK JP regular/medium, identical embedded files on every platform.
// Sizes are logical pixels scaled by Theme::scale.
struct Fonts { ImFont* regular = nullptr; ImFont* bold = nullptr; };
enum class Font { Body, Bold, Caption, Headline, Big };
struct Theme {
  float scale = 1.0f;
  Fonts fonts;
  float S(float v) const { return float(int(v * scale + 0.5f)); }
};
Theme& T();
void LoadFonts(ImGuiIO& io);
void Push(Font font);
inline void Pop() { ImGui::PopFont(); }
float FontSize(Font font);
ImVec2 TextSize(Font font, const std::string& text, float wrap = -1.0f);
float TextWidth(Font font, const std::string& text);
// Single-line text, vertically centered in r, clipped with an ellipsis.
enum Align { Left = 0, Center = 1, Right = 2 };
void Text(ImDrawList* dl, Font font, const std::string& text, const ImRect& r, Color color, Align align = Left, bool vcenter = true);
// Wrapped text from the top of r; returns the height it used.
float Wrapped(ImDrawList* dl, Font font, const std::string& text, const ImRect& r, Color color, Align align = Left);
float WrappedHeight(Font font, const std::string& text, float width);
void Round(ImDrawList* dl, const ImRect& r, float radius, Color fill, Color border = 0, float border_width = 1.0f, bool dashed = false);
void Circle(ImDrawList* dl, const ImRect& r, Color fill);
}  // namespace x360n::ui
namespace x360n::ui {
// ImGui's own widgets (text fields, lists, scroll bars, popups) in the palette.
void ApplyStyle(float scale);
}  // namespace x360n::ui
