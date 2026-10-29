#include "components.h"
#include <algorithm>

namespace x360n::ui {
using namespace desktop;
Color ToneColor(Tone tone) {
  switch (tone) {
    case Tone::Ok: return P.ok;
    case Tone::Warn: return P.warn;
    case Tone::Err: return P.err;
    case Tone::Info: return P.info;
    default: return P.muted;
  }
}
void Card(Ui& ui, const ImRect& r) {
  Round(ui.dl, r, T().S(14), P.surface, P.border);
  if (ui.record) ui.cards.push_back(r);
}
float ChipWidth(const std::string& text) { return TextWidth(Font::Caption, text) + T().S(34); }
void Chip(Ui& ui, float x, float y, const std::string& text, Tone tone) {
  const float h = T().S(26), w = ChipWidth(text);
  const auto color = ToneColor(tone);
  Round(ui.dl, Rect(x, y, w, h), h / 2, Mix(P.surface, color, 16), Mix(P.surface, color, 38));
  Circle(ui.dl, Rect(x + T().S(11), y + h / 2 - T().S(3), T().S(6), T().S(6)), color);
  Text(ui.dl, Font::Caption, text, ImRect(x + T().S(24), y, x + w, y + h), color);
}
float ButtonWidth(const Ui& ui, int id, bool glyph) {
  const auto label = desktop::Label(ui.a, id);
  return TextWidth(glyph ? Font::Bold : Font::Body, label) + T().S(glyph ? 22 + 8 + 24 : 24);
}
void TextBlock(Ui& ui, int id, const std::string& text, const ImRect& r, Font font, Color color, bool wrap, Align align) {
  // The height actually drawn, so a label that would spill out of its place fails the layout checks.
  const float used = wrap ? Wrapped(ui.dl, font, text, r, color, align) : 0;
  if (!wrap) Text(ui.dl, font, text, r, color, align);
  ui.Place(id, r, wrap ? "text" : "line", text, wrap ? std::max(used, WrappedHeight(font, text, r.GetWidth())) : 0);
}
void Heading(Ui& ui, const ImRect& card, const char* glyph, const std::string& text) {
  auto s = [](float v) { return T().S(v); };
  Icon(ui.dl, glyph, ImRect(card.Min.x + s(24), card.Min.y + s(16), card.Min.x + s(48), card.Min.y + s(44)), P.accent_text);
  Text(ui.dl, Font::Bold, text, ImRect(card.Min.x + s(52), card.Min.y + s(16), card.Max.x - s(24), card.Min.y + s(44)), P.text);
}
bool Button(Ui& ui, int id, Kind kind, const ImRect& r, const char* glyph, int back) {
  auto s = [](float v) { return T().S(v); };
  auto& a = ui.a;
  const ImGuiID gid = ImGui::GetID(id);
  const bool disabled = !Enabled(a, id);
  ImGui::SetCursorScreenPos(r.Min);
  ImGui::ItemSize(r);
  ImGui::PushItemFlag(ImGuiItemFlags_Disabled, disabled);
  const bool visible = ImGui::ItemAdd(r, gid);
  ImGui::PopItemFlag();
  const std::string label = desktop::Label(a, id);
  if (id == kPrimary) glyph = PrimaryGlyph(a);
  // Space the label needs, so layout checks can tell when it would be cut.
  float need = 0;
  switch (kind) {
    case Kind::Nav: need = s(50) + TextWidth(Font::Bold, label) + s(8); break;
    case Kind::Option: need = s(48) + TextWidth(Font::Bold, label) + s(16); break;
    case Kind::Toggle: need = s(52) + TextWidth(Font::Body, label); break;
    case Kind::Chip: need = TextWidth(Font::Bold, label) + s(16); break;
    default: need = glyph ? s(22) + s(8) + TextWidth(Font::Bold, label) + s(16) : TextWidth(Font::Body, label) + s(16);
  }
  ui.Place(id, r, "button", label, r.GetWidth() > r.GetHeight() ? need : 0);
  if (!visible) return false;
  bool hot = false, pressed = false;
  const bool clicked = !disabled && ImGui::ButtonBehavior(r, gid, &hot, &pressed);
  if (clicked) ui.Run(id);
  // Keyboard/gamepad focus ring; the mouse alone never draws it.
  const bool focus = ImGui::GetFocusID() == gid && GImGui->NavCursorVisible && GImGui->NavHighlightItemUnderNav;
  auto* dl = ui.dl;
  const float w = r.GetWidth(), h = r.GetHeight();
  const bool selected = Selected(a, id);
  Color ink = P.text, glyph_ink = P.text;
  auto white = [&](int pct) { return Mix(P.surface2, Rgb(255, 255, 255), pct); };
  ImRect box = r;
  (void)back;
  switch (kind) {
    case Kind::Primary:
      Round(dl, r, s(10), disabled ? P.surface2 : pressed ? P.accent_press : hot ? P.accent_hover : P.accent);
      ink = glyph_ink = disabled ? P.faint : P.on_accent; break;
    case Kind::Secondary:
      Round(dl, r, s(10), pressed ? white(14) : hot && !disabled ? white(9) : P.surface2, P.border);
      ink = glyph_ink = disabled ? P.faint : P.text; break;
    case Kind::Nav:
      if (selected) {
        Round(dl, r, s(10), Mix(P.sidebar, P.accent_text, 14));
        Round(dl, ImRect(r.Min.x, r.Min.y + h / 2 - s(10), r.Min.x + s(4), r.Min.y + h / 2 + s(10)), s(2), P.accent_text);
      } else if (hot && !disabled) Round(dl, r, s(10), Mix(P.sidebar, Rgb(255, 255, 255), 6));
      ink = selected ? P.text : disabled ? P.faint : P.muted;
      glyph_ink = selected ? P.accent_text : ink; break;
    case Kind::Chip: {
      const ImRect pill(r.Min + ImVec2(1, 1), r.Max - ImVec2(1, 1));
      const Color base = P.surface;
      if (selected && disabled) Round(dl, pill, (h - 2) / 2, Mix(base, P.accent, 22), Mix(base, P.accent_text, 35));
      else if (selected) Round(dl, pill, (h - 2) / 2, Mix(P.bg, P.accent, 55), P.accent_text);
      else Round(dl, pill, (h - 2) / 2, hot && !disabled ? white(6) : base, disabled ? Mix(base, P.border, 55) : P.border);
      ink = disabled ? P.faint : selected ? P.on_accent : P.muted; break;
    }
    case Kind::Ghost:
      if (hot && !disabled) Round(dl, r, s(10), white(8));
      ink = glyph_ink = disabled ? P.faint : hot ? P.text : P.muted; break;
    case Kind::Option:
      Round(dl, r, s(12), selected ? Mix(P.surface, P.accent, 22) : hot && !disabled ? white(6) : P.surface2, selected ? P.accent_text : P.border, selected ? 2.0f : 1.0f);
      ink = disabled ? P.faint : P.text; break;
    case Kind::Toggle: {
      ink = disabled ? P.faint : P.text;
      const float tw = s(40), th = s(22), ty = r.Min.y + (h - th) / 2;
      Round(dl, ImRect(r.Min.x, ty, r.Min.x + tw, ty + th), th / 2, selected ? P.accent : P.surface2, selected ? P.accent : P.faint);
      const float knob = th - s(8), kx = selected ? r.Min.x + tw - s(4) - knob : r.Min.x + s(4);
      Circle(dl, Rect(kx, ty + s(4), knob, knob), selected ? P.on_accent : P.muted);
      box.Min.x += tw + s(12); break;
    }
  }
  const float x = r.Min.x, y = r.Min.y;
  if (kind == Kind::Option) {
    const float cy = y + h / 2;
    Circle(dl, ImRect(x + s(16), cy - s(9), x + s(34), cy + s(9)), selected ? P.accent_text : disabled ? P.faint : P.muted);
    Circle(dl, ImRect(x + s(18), cy - s(7), x + s(32), cy + s(7)), selected ? Mix(P.surface, P.accent, 22) : P.surface2);
    if (selected) Circle(dl, ImRect(x + s(21), cy - s(4), x + s(29), cy + s(4)), P.accent_text);
    Text(dl, Font::Bold, label, ImRect(x + s(48), y + s(8), r.Max.x - s(16), y + s(30)), ink);
    Text(dl, Font::Caption, OptionCaption(a, id), ImRect(x + s(48), y + s(30), r.Max.x - s(16), y + s(52)), disabled ? P.faint : P.muted);
  } else if (kind == Kind::Toggle) {
    Text(dl, Font::Body, label, ImRect(box.Min.x, y, r.Max.x, r.Max.y), ink);
  } else if (kind == Kind::Nav) {
    Icon(dl, glyph, ImRect(x + s(14), y, x + s(42), r.Max.y), glyph_ink);
    Text(dl, selected ? Font::Bold : Font::Body, label, ImRect(x + s(50), y, r.Max.x - s(8), r.Max.y), ink);
  } else if (glyph && kind != Kind::Chip && w <= h) {
    Icon(dl, glyph, r, glyph_ink);  // square buttons are icon-only; the label stays the accessible name
  } else if (glyph && kind != Kind::Chip) {
    const float iw = s(22), gap = s(8), tw = TextWidth(Font::Bold, label);
    const float total = iw + gap + std::min(tw, w - iw - gap - s(16));
    const float x0 = x + std::max(s(8), (w - total) / 2);
    Icon(dl, glyph, ImRect(x0, y, x0 + iw, r.Max.y), glyph_ink);
    Text(dl, Font::Bold, label, ImRect(x0 + iw + gap, y, r.Max.x - s(8), r.Max.y), ink);
  } else {
    Text(dl, kind == Kind::Chip && selected ? Font::Bold : Font::Body, label, ImRect(x + s(8), y, r.Max.x - s(8), r.Max.y), ink, Center);
  }
  if (focus) Round(dl, r, kind == Kind::Chip ? h / 2 : s(10), 0, P.accent_text, 2.0f);
  return clicked;
}
}  // namespace x360n::ui
