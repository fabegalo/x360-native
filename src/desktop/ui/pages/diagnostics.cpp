#include "../frontend.h"
#include "app_text.h"

namespace x360n::ui {
using namespace desktop;
void DiagnosticsPage(Ui& ui, const Frame& f) {
  auto s = [](float v) { return T().S(v); };
  auto& a = ui.a;
  const float cx = f.cx, cw = f.cw, yc = f.yc;
  const ImRect card = Rect(cx, yc, cw, std::max(s(160), f.bottom - yc));
  Card(ui, card); ui.card = card;
  const float logs_w = std::max(s(150), ButtonWidth(ui, kOpenLogs, true));
  Button(ui, kCaptureCrash, Kind::Toggle, Rect(cx + s(16), yc + s(14), cw - s(32) - logs_w - s(12), s(36)), nullptr, 2);
  Button(ui, kOpenLogs, Kind::Secondary, Rect(cx + cw - s(16) - logs_w, yc + s(14), logs_w, s(36)), "folder", 2);
  // The exact build identification heads every diagnostic report.
  const auto about = "x360-native " + VersionLabel(X360N_VERSION) + " · " + kPlatform;
  const float about_h = WrappedHeight(Font::Body, about, cw - s(32)) + s(4);
  ReadOnlyText(ui, "##build", about, Rect(cx + s(16), yc + s(58), cw - s(32), about_h), Font::Body, P.surface);
  const float dy = s(58) + about_h + s(10);
  std::string details;
  for (const char c : a.details) if (c != '\r') details += c;
  ReadOnlyText(ui, "##details", details, Rect(cx + s(16), yc + dy, cw - s(32), card.GetHeight() - dy - s(14)), Font::Mono, P.surface);
  ui.card = {};
}
}  // namespace x360n::ui
