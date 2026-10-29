#include "theme.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

// Font files embedded at build time (cmake/desktop/embed_file.cmake).
extern "C" const unsigned char x360n_font_droid_sans[];
extern "C" const unsigned int x360n_font_droid_sans_size;
extern "C" const unsigned char x360n_font_roboto_medium[];
extern "C" const unsigned int x360n_font_roboto_medium_size;

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
  auto add = [&](const unsigned char* data, unsigned int size) {
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;  // static data
    config.OversampleH = 2;
    return io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(data), int(size), 14.0f, &config);
  };
  auto& fonts = T().fonts;
  fonts.regular = add(x360n_font_droid_sans, x360n_font_droid_sans_size);
  fonts.bold = add(x360n_font_roboto_medium, x360n_font_roboto_medium_size);
  fonts.mono = fonts.regular;
  io.FontDefault = fonts.regular;
}
namespace {
ImFont* FontOf(Font font) {
  const auto& f = T().fonts;
  switch (font) {
    case Font::Bold: case Font::Headline: case Font::Big: return f.bold;
    case Font::Mono: return f.mono;
    default: return f.regular;
  }
}
}  // namespace
float FontSize(Font font) {
  switch (font) {
    case Font::Caption: return T().S(12);
    case Font::Headline: return T().S(30);
    case Font::Big: return T().S(22);
    case Font::Mono: return T().S(13);
    default: return T().S(14);
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
  const float y = vcenter ? r.Min.y + (r.GetHeight() - size) / 2 : r.Min.y;
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
  if (fill) dl->AddRectFilled(r.Min, r.Max, fill, radius);
  if (!border) return;
  const ImVec2 inset(bw / 2, bw / 2);
  if (!dashed) { dl->AddRect(r.Min + inset, r.Max - inset, border, radius, 0, bw); return; }
  // Dashed outline: the same rounded path, stroked in segments.
  dl->PathRect(r.Min + inset, r.Max - inset, radius);
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
      if (on) dl->AddLine(a + dir * t, a + dir * (t + span), border, bw);
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
  style.FontSizeBase = 14;
  style.ScaleAllSizes(scale);
  ImGui::GetStyle() = style;
}
}  // namespace x360n::ui
