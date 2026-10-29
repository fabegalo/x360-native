#include "../frontend.h"
#include "app_text.h"

namespace x360n::ui {
using namespace desktop;
void ImportPage(Ui& ui, const Frame& f) {
  auto s = [](float v) { return T().S(v); };
  if (ui.a.busy) return;
  const float cx = f.cx, cw = f.cw, yc = f.yc;
  const ImRect drop = Rect(cx, yc, cw, s(268));
  // Dropping anywhere on the window imports; this card is where it is explained.
  Round(ui.dl, drop, s(16), P.surface, P.faint, 2.0f, true);
  if (ui.record) ui.cards.push_back(drop);
  ui.card = drop;
  const float mid = drop.GetCenter().x;
  const ImRect disc(mid - s(32), drop.Min.y + s(24), mid + s(32), drop.Min.y + s(88));
  Circle(ui.dl, disc, Mix(P.surface, P.accent_text, 14));
  Icon(ui.dl, "download", disc, P.accent_text, 26);
  Text(ui.dl, Font::Big, Tr("Arraste um arquivo ou pasta para cá", "Drop a file or folder here"), ImRect(drop.Min.x + s(24), drop.Min.y + s(92), drop.Max.x - s(24), drop.Min.y + s(128)), P.text, Center);
  TextBlock(ui, 0, DropText(), Rect(drop.Min.x + s(40), drop.Min.y + s(132), cw - s(80), s(44)), Font::Body, P.text, true, Center);
  const float bw = s(220), gap = s(12), bx = cx + (cw - 2 * bw - gap) / 2, by = drop.Min.y + s(196);
  Button(ui, kPrimary, Kind::Primary, Rect(bx, by, bw, s(46)), nullptr, 2);
  Button(ui, kImportFolder, Kind::Secondary, Rect(bx + bw + gap, by, bw, s(46)), "folder", 2);
  ui.card = {};
  if (f.banner) return;
  struct Tile { const char* glyph; std::string title, text; };
  const Tile tiles[3] = {
    {"disc", "ISO / GDFX", Tr("Imagem de disco do Xbox 360", "Xbox 360 disc image")},
    {"package", "XBLA / STFS", Tr("Pacote de conteúdo (CON, LIVE, PIRS)", "Content package (CON, LIVE, PIRS)")},
    {"folder", Tr("Pasta extraída", "Extracted folder"), Tr("Jogo já extraído, com default.xex", "Already extracted game with default.xex")}};
  const float tw = (cw - 2 * gap) / 3, ty = drop.Max.y + s(16);
  for (int i = 0; i < 3; ++i) {
    const ImRect c = Rect(cx + i * (tw + gap), ty, tw, s(100));
    Card(ui, c);
    Icon(ui.dl, tiles[i].glyph, ImRect(c.Min.x + s(18), c.Min.y + s(18), c.Min.x + s(42), c.Min.y + s(44)), P.accent_text);
    Text(ui.dl, Font::Bold, tiles[i].title, ImRect(c.Min.x + s(48), c.Min.y + s(18), c.Max.x - s(12), c.Min.y + s(44)), P.text);
    Wrapped(ui.dl, Font::Caption, tiles[i].text, ImRect(c.Min.x + s(18), c.Min.y + s(52), c.Max.x - s(14), c.Max.y - s(8)), P.muted);
  }
}
}  // namespace x360n::ui
