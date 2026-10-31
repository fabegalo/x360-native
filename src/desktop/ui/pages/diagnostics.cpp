#include "../frontend.h"
#include "app_text.h"

namespace x360n::ui {
using namespace desktop;
void DiagnosticsPage(Ui& ui, const Frame& f) {
  auto s = [](float v) { return T().S(v); };
  auto& a = ui.a;
  const bool checked = !a.environment.description.empty();
  struct Health { std::string label; const char* icon; bool ok; };
  const Health health[] = {{Tr("Gráficos", "Graphics"), "chip", a.environment.graphics},
    {Tr("Ferramentas", "Toolchain"), "tools", a.environment.toolchain},
    {Tr("Armazenamento", "Storage"), "save", a.environment.writable}};
  const float gap = s(12), tile_w = (f.cw - 2 * gap) / 3;
  for (int i = 0; i < 3; ++i) {
    const auto tile = Rect(f.cx + i * (tile_w + gap), f.yc, tile_w, s(80));
    Card(ui, tile); ui.card = tile;
    const auto tone = !checked ? Tone::Neutral : health[i].ok ? Tone::Ok : Tone::Warn;
    Icon(ui.dl, health[i].icon, Rect(tile.Min.x + s(16), tile.Min.y + s(16), s(24), s(24)), ToneColor(tone));
    TextBlock(ui, 0, health[i].label, Rect(tile.Min.x + s(48), tile.Min.y + s(14), tile_w - s(64), s(24)), Font::Caption, P.muted, false);
    TextBlock(ui, 0, !checked ? Tr("Não verificado", "Not checked") : health[i].ok ? Tr("Pronto", "Ready") : Tr("Requer atenção", "Needs attention"),
      Rect(tile.Min.x + s(16), tile.Min.y + s(44), tile_w - s(32), s(24)), Font::Bold, ToneColor(tone), false);
  }
  const auto card = Rect(f.cx, f.yc + s(96), f.cw, f.bottom - f.yc - s(96));
  Card(ui, card); ui.card = card;
  const float x = card.Min.x + s(20), width = card.GetWidth() - s(40), y = card.Min.y + s(16);
  const float logs_w = ButtonWidth(ui, kOpenLogs, true), check_w = ButtonWidth(ui, kCheck, true);
  Button(ui, kOpenLogs, Kind::Secondary, Rect(x, y, logs_w, s(40)), "folder", 2);
  Button(ui, kCheck, Kind::Secondary, Rect(x + logs_w + s(8), y, check_w, s(40)), "refresh", 2);
  const auto copy = ui.diagnostic_copied ? Tr("Copiado", "Copied") : Tr("Copiar relatório", "Copy report");
  const auto copy_rect = Rect(x + width - s(180), y, s(180), s(40));
  ImGui::SetCursorScreenPos(copy_rect.Min); Push(Font::Body);
  if (ImGui::Button(copy.c_str(), copy_rect.GetSize())) {
    ImGui::SetClipboardText(DiagnosticReport(a).c_str());
    ui.diagnostic_copied = true;
  }
  Pop(); ui.Place(0, copy_rect, "button", copy, TextWidth(Font::Body, copy) + s(16));
  Button(ui, kCaptureCrash, Kind::Toggle, Rect(x, y + s(52), width, s(36)), nullptr, 2);
  const char* tabs_pt[] = {"Ambiente", "Conteúdo", "Última operação"};
  const char* tabs_en[] = {"Environment", "Content", "Latest operation"};
  const float tab_w = (width - 2 * s(8)) / 3, tab_y = y + s(104);
  for (int i = 0; i < 3; ++i) {
    const auto r = Rect(x + i * (tab_w + s(8)), tab_y, tab_w, s(38));
    const char* label = english ? tabs_en[i] : tabs_pt[i];
    ImGui::SetCursorScreenPos(r.Min); Push(Font::Bold);
    const auto base = ui.diagnostic_tab == i ? Mix(P.surface, P.accent, 35) : P.surface2;
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(base));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(Mix(base, P.accent_text, 12)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4(Mix(base, P.accent_text, 20)));
    if (ImGui::Button(label, r.GetSize())) { ui.diagnostic_tab = i; ui.diagnostic_copied = false; }
    ImGui::PopStyleColor(3); Pop(); ui.Place(0, r, "button", label, TextWidth(Font::Bold, label) + s(16));
  }
  const auto text = DiagnosticReport(a, DiagnosticSection(ui.diagnostic_tab));
  const auto content = Rect(x, tab_y + s(52), width, card.Max.y - (tab_y + s(52)) - s(20));
  ReadOnlyText(ui, "##diagnostic-report", text, content, Font::Body, P.surface);
  ui.card = {};
}
}  // namespace x360n::ui
