#include "../frontend.h"
#include "app_text.h"

namespace x360n::ui {
using namespace desktop;
void HomePage(Ui& ui, const Frame& f) {
  auto s = [](float v) { return T().S(v); };
  auto& a = ui.a;
  const float cx = f.cx, cw = f.cw, yc = f.yc;
  const ImRect hero = Rect(cx, yc, cw, s(188));
  Card(ui, hero); ui.card = hero;
  Chip(ui, hero.Min.x + s(28), hero.Min.y + s(26), a.chip_text, a.chip_tone);
  TextBlock(ui, 0, a.status, Rect(cx + s(28), yc + s(66), cw - s(56) - s(268), s(96)));
  Button(ui, kPrimary, Kind::Primary, Rect(cx + cw - s(28) - s(240), yc + s(66), s(240), s(56)), nullptr, 2);
  ui.card = {};
  if (f.banner) return;
  size_t dashboards = 0, ready = 0, games = 0, prepared = 0;
  if (a.checked.size() == a.targets.size()) for (size_t i = 0; i < a.targets.size(); ++i) {
    const auto& t = a.targets[i]; const auto& c = a.checked[i];
    if (t.kind == "dashboard") { ++dashboards; if (c.state.inputs_ok && c.state.prepared) ++ready; }
    else if (c.added) { ++games; if (c.state.inputs_ok && c.state.prepared && c.library.empty()) ++prepared; }
  }
  const bool native = a.renderer_settings.renderer == Renderer::Native;
  struct Stat { std::string label, value, hint; Tone tone; const char* glyph; };
  const Stat stats[4] = {
    {"Dashboard", ready ? Tr("Pronta", "Ready") : Tr("Pendente", "Pending"), std::to_string(ready) + "/" + std::to_string(dashboards) + Tr(" versões preparadas", " versions prepared"), ready ? Tone::Ok : Tone::Warn, "dashboard"},
    {Tr("Biblioteca", "Library"), std::to_string(games), Tr("jogos importados", "imported games"), Tone::Neutral, "library"},
    {Tr("Preparados", "Prepared"), std::to_string(prepared), Tr("prontos para a My Games", "ready for My Games"), prepared ? Tone::Ok : Tone::Neutral, "check"},
    {Tr("Gráficos", "Graphics"), native ? "Native" : "Xenos", Tr("renderer global da sessão", "session-wide renderer"), native ? Tone::Warn : Tone::Neutral, "chip"}};
  const float gap = s(12), sw = (cw - 3 * gap) / 4, sy = yc + s(188) + s(16);
  for (int i = 0; i < 4; ++i) {
    const ImRect c = Rect(cx + i * (sw + gap), sy, sw, s(96));
    Card(ui, c);
    Icon(ui.dl, stats[i].glyph, ImRect(c.Min.x + s(18), c.Min.y + s(14), c.Min.x + s(40), c.Min.y + s(36)), ToneColor(stats[i].tone == Tone::Neutral ? Tone::Info : stats[i].tone));
    Text(ui.dl, Font::Caption, stats[i].label, ImRect(c.Min.x + s(46), c.Min.y + s(14), c.Max.x - s(12), c.Min.y + s(36)), P.muted);
    Text(ui.dl, Font::Big, stats[i].value, ImRect(c.Min.x + s(18), c.Min.y + s(36), c.Max.x - s(12), c.Min.y + s(66)), P.text);
    Text(ui.dl, Font::Caption, stats[i].hint, ImRect(c.Min.x + s(18), c.Min.y + s(64), c.Max.x - s(12), c.Min.y + s(88)), P.muted);
  }
  const float qy = sy + s(96) + s(20);
  Button(ui, kAddContent, Kind::Secondary, Rect(cx, qy, s(220), s(44)), "add");
  Button(ui, kLibrary, Kind::Secondary, Rect(cx + s(232), qy, s(220), s(44)), "library");
}
}  // namespace x360n::ui
