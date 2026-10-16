#pragma once
// Visual language of the desktop manager: palette, metrics and GDI/GDI+ helpers.
// Everything here is plain Win32; no resources are allocated per paint except
// short-lived GDI+ graphics objects. DPI scaling is always applied by the caller.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <string>
#include <objidl.h>
using std::max;
using std::min;
#include <gdiplus.h>

namespace x360n::ui {
struct Palette {
  COLORREF bg, sidebar, surface, surface2, border, text, muted, faint;
  COLORREF accent, accent_hover, accent_press, accent_text, on_accent;
  COLORREF ok, warn, err, info;
};
// Xbox-inspired green on graphite. Contrast (text on surface) is above 11:1;
// muted text on surface is above 5:1; white on the accent fill is above 5:1.
inline constexpr Palette dark{
  RGB(13, 15, 18),   RGB(10, 12, 14),   RGB(22, 26, 31),   RGB(31, 36, 43),   RGB(46, 53, 62),
  RGB(236, 240, 245), RGB(151, 161, 174), RGB(98, 108, 121),
  RGB(16, 124, 16),  RGB(21, 143, 21),  RGB(12, 104, 12),  RGB(96, 214, 96),  RGB(255, 255, 255),
  RGB(96, 214, 96),  RGB(245, 183, 64), RGB(240, 96, 96),  RGB(104, 168, 255)};
inline const Palette& P() { return dark; }

inline int S(int v, UINT dpi) { return MulDiv(v, int(dpi), 96); }
inline COLORREF Mix(COLORREF base, COLORREF over, int percent) {
  auto ch = [&](int a, int b) { return (a * (100 - percent) + b * percent) / 100; };
  return RGB(ch(GetRValue(base), GetRValue(over)), ch(GetGValue(base), GetGValue(over)), ch(GetBValue(base), GetBValue(over)));
}
inline Gdiplus::Color Gc(COLORREF c, BYTE a = 255) { return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c)); }

// Paint offset: lets a buffer cover only the invalid rectangle while callers
// keep drawing in window coordinates (GDI and GDI+ are shifted identically).
inline POINT& Origin() { static thread_local POINT origin{0, 0}; return origin; }
inline RECT Off(RECT r) { OffsetRect(&r, Origin().x, Origin().y); return r; }
inline void Fill(HDC dc, const RECT& rect, COLORREF c) {
  const RECT r = Off(rect);
  SetDCBrushColor(dc, c);
  FillRect(dc, &r, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}
// Anti-aliased rounded rectangle. `border` < 0 means no outline.
inline void Round(HDC dc, RECT r, int radius, COLORREF fill, COLORREF border = CLR_INVALID, int border_width = 1, bool dashed = false) {
  r = Off(r);
  Gdiplus::Graphics g(dc);
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  const float bw = float(border_width);
  Gdiplus::RectF rf(float(r.left) + bw / 2, float(r.top) + bw / 2, float(r.right - r.left) - bw, float(r.bottom - r.top) - bw);
  const float d = float(radius) * 2;
  Gdiplus::GraphicsPath path;
  path.AddArc(rf.X, rf.Y, d, d, 180, 90);
  path.AddArc(rf.GetRight() - d, rf.Y, d, d, 270, 90);
  path.AddArc(rf.GetRight() - d, rf.GetBottom() - d, d, d, 0, 90);
  path.AddArc(rf.X, rf.GetBottom() - d, d, d, 90, 90);
  path.CloseFigure();
  if (fill != CLR_INVALID) { Gdiplus::SolidBrush brush(Gc(fill)); g.FillPath(&brush, &path); }
  if (border != CLR_INVALID) {
    Gdiplus::Pen pen(Gc(border), bw);
    if (dashed) pen.SetDashStyle(Gdiplus::DashStyleDash);
    g.DrawPath(&pen, &path);
  }
}
inline void Circle(HDC dc, RECT r, COLORREF fill) {
  r = Off(r);
  Gdiplus::Graphics g(dc);
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  Gdiplus::SolidBrush brush(Gc(fill));
  g.FillEllipse(&brush, Gdiplus::Rect(r.left, r.top, r.right - r.left, r.bottom - r.top));
}
inline void Text(HDC dc, HFONT font, const std::wstring& text, RECT r, COLORREF color, UINT flags = DT_SINGLELINE | DT_VCENTER | DT_LEFT) {
  r = Off(r);
  auto old = SelectObject(dc, font);
  SetBkMode(dc, TRANSPARENT); SetTextColor(dc, color);
  DrawTextW(dc, text.c_str(), int(text.size()), &r, flags | DT_NOPREFIX | DT_END_ELLIPSIS);
  SelectObject(dc, old);
}
inline int TextWidth(HDC dc, HFONT font, const std::wstring& text) {
  auto old = SelectObject(dc, font); SIZE size{};
  GetTextExtentPoint32W(dc, text.c_str(), int(text.size()), &size);
  SelectObject(dc, old); return size.cx;
}
// Off-screen buffer so paints never flicker.
class Buffer {
 public:
  Buffer(HDC target, int w, int h) : target_(target), w_(w), h_(h) {
    dc_ = CreateCompatibleDC(target); bitmap_ = CreateCompatibleBitmap(target, max(1, w), max(1, h));
    old_ = SelectObject(dc_, bitmap_); SetStretchBltMode(dc_, COLORONCOLOR);
  }
  ~Buffer() { SelectObject(dc_, old_); DeleteObject(bitmap_); DeleteDC(dc_); }
  Buffer(const Buffer&) = delete; Buffer& operator=(const Buffer&) = delete;
  HDC dc() const { return dc_; }
  void Flush(int x = 0, int y = 0) { BitBlt(target_, x, y, w_, h_, dc_, 0, 0, SRCCOPY); }
 private:
  HDC target_, dc_; HBITMAP bitmap_; HGDIOBJ old_; int w_, h_;
};
// Segoe MDL2 Assets code points used by the interface.
namespace icon {
inline constexpr const wchar_t* home = L"";
inline constexpr const wchar_t* library = L"";
inline constexpr const wchar_t* download = L"";
inline constexpr const wchar_t* dashboard = L"";
inline constexpr const wchar_t* settings = L"";
inline constexpr const wchar_t* info = L"";
inline constexpr const wchar_t* language = L"";
inline constexpr const wchar_t* play = L"";
inline constexpr const wchar_t* folder = L"";
inline constexpr const wchar_t* file = L"";
inline constexpr const wchar_t* search = L"";
inline constexpr const wchar_t* refresh = L"";
inline constexpr const wchar_t* cancel = L"";
inline constexpr const wchar_t* add = L"";
inline constexpr const wchar_t* check = L"";
inline constexpr const wchar_t* warning = L"";
inline constexpr const wchar_t* error = L"";
inline constexpr const wchar_t* gamepad = L"";
inline constexpr const wchar_t* chip = L"";
inline constexpr const wchar_t* save = L"";
inline constexpr const wchar_t* tools = L"";
inline constexpr const wchar_t* disc = L"";
inline constexpr const wchar_t* package = L"";
}
}
