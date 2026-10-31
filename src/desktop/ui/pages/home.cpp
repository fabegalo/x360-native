#include "../frontend.h"
#include "app_text.h"

namespace x360n::ui {
using namespace desktop;
void HomePage(Ui& ui, const Frame& f) {
  auto s = [](float v) { return T().S(v); };
  auto& a = ui.a;
  const float cx = f.cx, cw = f.cw, yc = f.yc;
  const ImRect hero = Rect(cx, yc, cw, s(160));
  Card(ui, hero); ui.card = hero;
  Chip(ui, hero.Min.x + s(28), hero.Min.y + s(26), a.chip_text, a.chip_tone);
  TextBlock(ui, 0, a.status, Rect(cx + s(28), yc + s(66), cw - s(56) - s(268), s(72)));
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
    {"Dashboard", ready ? Tr("Pronta", "Ready") : Tr("Pendente", "Pending"), std::to_string(ready) + "/" + std::to_string(dashboards) + Tr(" versões", " versions"), ready ? Tone::Ok : Tone::Warn, "dashboard"},
    {Tr("Biblioteca", "Library"), std::to_string(games), Tr("jogos importados", "imported games"), Tone::Neutral, "library"},
    {Tr("Preparados", "Prepared"), std::to_string(prepared), Tr("na My Games", "in My Games"), prepared ? Tone::Ok : Tone::Neutral, "check"},
    {Tr("Gráficos", "Graphics"), native ? "Native" : "Xenos", Tr("usado na sessão", "used for this session"), native ? Tone::Warn : Tone::Neutral, "chip"}};
  const float gap = s(12), sw = (cw - 3 * gap) / 4, sy = hero.Max.y + s(16);
  for (int i = 0; i < 4; ++i) {
    const ImRect c = Rect(cx + i * (sw + gap), sy, sw, s(96));
    Card(ui, c);
    Icon(ui.dl, stats[i].glyph, ImRect(c.Min.x + s(18), c.Min.y + s(14), c.Min.x + s(40), c.Min.y + s(36)), ToneColor(stats[i].tone == Tone::Neutral ? Tone::Info : stats[i].tone));
    Text(ui.dl, Font::Caption, stats[i].label, ImRect(c.Min.x + s(46), c.Min.y + s(14), c.Max.x - s(12), c.Min.y + s(36)), P.muted);
    Text(ui.dl, Font::Big, stats[i].value, ImRect(c.Min.x + s(18), c.Min.y + s(36), c.Max.x - s(12), c.Min.y + s(66)), P.text);
    Text(ui.dl, Font::Caption, stats[i].hint, ImRect(c.Min.x + s(18), c.Min.y + s(64), c.Max.x - s(12), c.Min.y + s(88)), P.muted);
  }
  const float qy = sy + s(96) + s(20);
  struct Step { std::string title, text; int action; };
  const Step steps[] = {
    {Tr("1. Console", "1. Console"), Tr("Escolha e prepare sua dashboard original.", "Choose and prepare your original dashboard."), kDashboards},
    {Tr("2. Jogos", "2. Games"), Tr("Adicione e prepare seus jogos na Biblioteca.", "Add and prepare your games in the Library."), kAddContent},
    {Tr("3. Jogar", "3. Play"), Tr("Inicie o Xbox 360 e abra os jogos pela My Games.", "Start Xbox 360 and open games through My Games."), kLibrary}};
  const float step_w = (cw - 2 * gap) / 3, step_h = std::min(s(208), f.bottom - qy);
  for (int i = 0; i < 3; ++i) {
    const auto c = Rect(cx + i * (step_w + gap), qy, step_w, step_h);
    Card(ui, c); ui.card = c;
    TextBlock(ui, 0, steps[i].title, Rect(c.Min.x + s(18), c.Min.y + s(16), step_w - s(36), s(28)), Font::Bold, P.text, false);
    TextBlock(ui, 0, steps[i].text, Rect(c.Min.x + s(18), c.Min.y + s(52), step_w - s(36), step_h - s(114)), Font::Body, P.muted);
    Button(ui, steps[i].action, Kind::Secondary, Rect(c.Min.x + s(18), c.Max.y - s(56), step_w - s(36), s(40)), nullptr, 2);
    ui.card = {};
  }
}
}  // namespace x360n::ui
