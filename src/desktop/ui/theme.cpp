#include "theme.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
// One unmodified font family embedded on every platform.
extern "C" const unsigned char x360n_font_noto_regular[];
extern "C" const unsigned int x360n_font_noto_regular_size;
extern "C" const unsigned char x360n_font_noto_medium[];
extern "C" const unsigned int x360n_font_noto_medium_size;

namespace x360n::ui {
Color Mix(Color base, Color over, int percent) {
  auto ch = [&](int shift) {
    const int a = int((base >> shift) & 0xFF), b = int((over >> shift) & 0xFF);
    return Color((a * (100 - percent) + b * percent) / 100) << shift;
  };
  return ch(IM_COL32_R_SHIFT) | ch(IM_COL32_G_SHIFT) | ch(IM_COL32_B_SHIFT) | (Color(0xFF) << IM_COL32_A_SHIFT);
}
Theme& T() { static Theme theme; return theme; }
void LoadFonts(ImGuiIO& io) {
  auto add = [&](const unsigned char* data, unsigned int bytes, const char* name) {
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    config.OversampleH = config.OversampleV = 1;
    config.PixelSnapH = true;
    ImFormatString(config.Name, IM_ARRAYSIZE(config.Name), "%s", name);
    return io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(data), int(bytes), 20.0f, &config);
  };
  auto& fonts = T().fonts;
  fonts.regular = add(x360n_font_noto_regular, x360n_font_noto_regular_size, "Noto Sans CJK JP Regular");
  fonts.bold = add(x360n_font_noto_medium, x360n_font_noto_medium_size, "Noto Sans CJK JP Medium");
  io.FontDefault = fonts.regular;
}
namespace {
ImFont* FontOf(Font font) {
  const auto& f = T().fonts;
  ImFont* result = f.regular;
  switch (font) {
    case Font::Bold: case Font::Headline: case Font::Big: result = f.bold; break;
    default: break;
  }
  // Direct AddText/CalcTextSizeA bypass PushFont, which normally propagates
  // framebuffer density. Without this, headings can reuse a 1x bitmap on Retina.
  result->CurrentRasterizerDensity = ImGui::GetFontRasterizerDensity();
  return result;
}
}  // namespace
float FontSize(Font font) {
  switch (font) {
    case Font::Caption: return T().S(18);
    case Font::Headline: return T().S(36);
    case Font::Big: return T().S(26);
    default: return T().S(20);
  }
}
void Push(Font font) { ImGui::PushFont(FontOf(font), FontSize(font)); }
ImVec2 TextSize(Font font, const std::string& text, float wrap) {
  return FontOf(font)->CalcTextSizeA(FontSize(font), FLT_MAX, wrap, text.data(), text.data() + text.size());
}
float TextWidth(Font font, const std::string& text) { return TextSize(font, text).x; }
void Text(ImDrawList* dl, Font font, const std::string& text, const ImRect& r, Color color, Align align, bool vcenter) {
  auto* f = FontOf(font);
  const float size = FontSize(font), avail = std::max(0.0f, r.GetWidth());
  std::string shown = text;
  // A single line never wraps: cut at whole code points and end with an ellipsis.
  if (const auto newline = shown.find('\n'); newline != std::string::npos) shown.resize(newline);
  float w = f->CalcTextSizeA(size, FLT_MAX, 0, shown.data(), shown.data() + shown.size()).x;
  if (w > avail + 0.5f) {
    const std::string full = shown;
    size_t keep = full.size();
    do {
      // Step back one whole UTF-8 code point.
      do --keep; while (keep > 0 && (static_cast<unsigned char>(full[keep]) & 0xC0) == 0x80);
      shown = full.substr(0, keep) + "…";
      w = f->CalcTextSizeA(size, FLT_MAX, 0, shown.data(), shown.data() + shown.size()).x;
    } while (keep > 0 && w > avail);
  }
  const float x = align == Center ? r.Min.x + (avail - w) / 2 : align == Right ? r.Max.x - w : r.Min.x;
  // Center visible capitals, not the font's ascent/descent box, so labels
  // share a stable optical baseline beside icons.
  const auto* cap = f->GetFontBaked(size)->FindGlyph('H');
  const float y = vcenter ? r.GetCenter().y - (cap->Y0 + cap->Y1) / 2 : r.Min.y;
  const ImVec4 clip(r.Min.x, r.Min.y - size, r.Max.x + 1, r.Max.y + size);
  dl->AddText(f, size, ImVec2(std::floor(x), std::floor(y)), color, shown.data(), shown.data() + shown.size(), 0.0f, &clip);
}
float WrappedHeight(Font font, const std::string& text, float width) {
  if (text.empty()) return 0;
  return TextSize(font, text, std::max(1.0f, width)).y;
}
float Wrapped(ImDrawList* dl, Font font, const std::string& text, const ImRect& r, Color color, Align align) {
  auto* f = FontOf(font);
  const float size = FontSize(font), width = std::max(1.0f, r.GetWidth());
  const char* at = text.data();
  const char* end = text.data() + text.size();
  float y = r.Min.y;
  const float line = f->CalcTextSizeA(size, FLT_MAX, 0, "A").y;
  if (text.empty()) return 0;
  // Each paragraph (between '\n') wraps on its own, like CalcTextSizeA measures it.
  while (at <= end) {
    const char* paragraph = std::find(at, end, '\n');
    do {
      const char* eol = at < paragraph ? f->CalcWordWrapPosition(size, at, paragraph, width) : paragraph;
      if (eol == at && at < paragraph) ++eol;
      const float w = f->CalcTextSizeA(size, FLT_MAX, 0, at, eol).x;
      const float x = align == Center ? r.Min.x + (width - w) / 2 : align == Right ? r.Max.x - w : r.Min.x;
      if (eol > at) dl->AddText(f, size, ImVec2(std::floor(x), std::floor(y)), color, at, eol);
      y += line;
      at = eol;
      while (at < paragraph && *at == ' ') ++at;
    } while (at < paragraph);
    at = paragraph + 1;
  }
  return y - r.Min.y;
}
void Round(ImDrawList* dl, const ImRect& r, float radius, Color fill, Color border, float bw, bool dashed) {
  const float density = std::max(1.0f, ImGui::GetIO().DisplayFramebufferScale.x);
  struct RestoreFringe { ImDrawList* list; float value; ~RestoreFringe() { list->_FringeScale = value; } } restore{dl, dl->_FringeScale};
  dl->_FringeScale = 1.0f / density;  // antialias only one physical pixel at any DPI
  auto snap = [density](ImVec2 p) { return ImVec2(std::round(p.x * density) / density, std::round(p.y * density) / density); };
  const ImRect box(snap(r.Min), snap(r.Max));
  if (fill) dl->AddRectFilled(box.Min, box.Max, fill, radius);
  if (!border) return;
  bw = std::max(1.0f, std::round(bw * density)) / density;
  const ImVec2 inset(bw / 2, bw / 2);
  // AddRect adds another half-pixel offset. Applying both offsets blurs every
  // straight card/control edge, especially the two-pixel selection outline.
  if (!dashed) { dl->PathRect(box.Min + inset, box.Max - inset, radius); dl->PathStroke(border, ImDrawFlags_Closed, bw); return; }
  // Dashed outline: the same rounded path, stroked in segments.
  dl->PathRect(box.Min + inset, box.Max - inset, radius);
  ImVector<ImVec2> points; points.swap(dl->_Path);
  const float dash = T().S(6), gap = T().S(4);
  float carry = 0; bool on = true;
  for (int i = 0; i < points.Size; ++i) {
    ImVec2 a = points[i], b = points[(i + 1) % points.Size];
    const ImVec2 d = b - a;
    float length = std::sqrt(d.x * d.x + d.y * d.y);
    ImVec2 dir = length > 0 ? (b - a) / length : ImVec2(0, 0);
    float t = 0;
    while (t < length) {
      const float span = std::min((on ? dash : gap) - carry, length - t);
      if (on) { const ImVec2 segment[] = {a + dir * t, a + dir * (t + span)}; dl->AddPolyline(segment, 2, border, 0, bw); }
      t += span; carry += span;
      if (carry >= (on ? dash : gap) - 0.01f) { carry = 0; on = !on; }
    }
  }
}
void Circle(ImDrawList* dl, const ImRect& r, Color fill) {
  dl->AddCircleFilled(r.GetCenter(), std::min(r.GetWidth(), r.GetHeight()) / 2, fill, 32);
}
}  // namespace x360n::ui
namespace x360n::ui {
void ApplyStyle(float scale) {
  T().scale = scale;
  ImGuiStyle style;
  ImGui::StyleColorsDark(&style);
  auto c = [](Color v) { return ImGui::ColorConvertU32ToFloat4(v); };
  style.Colors[ImGuiCol_Text] = c(P.text);
  style.Colors[ImGuiCol_TextDisabled] = c(P.faint);
  style.Colors[ImGuiCol_WindowBg] = c(P.bg);
  style.Colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  style.Colors[ImGuiCol_PopupBg] = c(P.surface);
  style.Colors[ImGuiCol_Border] = c(P.border);
  style.Colors[ImGuiCol_FrameBg] = c(P.surface2);
  style.Colors[ImGuiCol_FrameBgHovered] = c(Mix(P.surface2, Rgb(255, 255, 255), 6));
  style.Colors[ImGuiCol_FrameBgActive] = c(Mix(P.surface2, Rgb(255, 255, 255), 10));
  style.Colors[ImGuiCol_Button] = c(P.surface2);
  style.Colors[ImGuiCol_ButtonHovered] = c(Mix(P.surface2, Rgb(255, 255, 255), 9));
  style.Colors[ImGuiCol_ButtonActive] = c(Mix(P.surface2, Rgb(255, 255, 255), 14));
  style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
  style.Colors[ImGuiCol_ScrollbarGrab] = c(P.faint);
  style.Colors[ImGuiCol_ScrollbarGrabHovered] = c(P.muted);
  style.Colors[ImGuiCol_ScrollbarGrabActive] = c(P.accent_text);
  style.Colors[ImGuiCol_TextSelectedBg] = c(Mix(P.surface, P.accent, 55));
  style.Colors[ImGuiCol_Header] = c(Mix(P.surface, P.accent, 18));
  style.Colors[ImGuiCol_HeaderHovered] = c(Mix(P.surface, P.accent, 28));
  style.Colors[ImGuiCol_HeaderActive] = c(Mix(P.surface, P.accent, 38));
  style.Colors[ImGuiCol_NavCursor] = c(P.accent_text);
  style.Colors[ImGuiCol_InputTextCursor] = c(P.text);
  style.WindowBorderSize = 0; style.ChildBorderSize = 0; style.PopupBorderSize = 1;
  style.FrameRounding = 8; style.PopupRounding = 10; style.ScrollbarRounding = 4; style.GrabRounding = 4;
  style.ScrollbarSize = 10;
  style.FontSizeBase = 20;
  style.ScaleAllSizes(scale);
  ImGui::GetStyle() = style;
}
}  // namespace x360n::ui
