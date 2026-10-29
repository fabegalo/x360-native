#include <cstdio>
#include "../frontend.h"
#include "app_text.h"

namespace x360n::ui {
using namespace desktop;
namespace {
float s(float v) { return T().S(v); }
void DrawRow(Ui& ui, size_t row, size_t index, const ImRect& item) {
  auto& a = ui.a;
  const auto& t = a.targets[index];
  const bool dashboard = t.kind == "dashboard", sel = index == a.selected;
  const ImGuiID gid = ImGui::GetID(int(index) + 100000);
  ImGui::SetCursorScreenPos(item.Min);
  ImGui::ItemSize(item);
  ImGui::PushItemFlag(ImGuiItemFlags_Disabled, !Enabled(a, kGameList));
  const bool visible = ImGui::ItemAdd(item, gid);
  ImGui::PopItemFlag();
  if (!visible) return;
  bool hot = false, held = false;
  if (ImGui::ButtonBehavior(item, gid, &hot, &held, ImGuiButtonFlags_PressedOnClick | ImGuiButtonFlags_PressedOnDoubleClick)) {
    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown)) {
      ui.deferred.push_back([&a, row] { SelectRow(a, row); if (Enabled(a, kPrimary)) Command(a, kPrimary); });
    } else ui.deferred.push_back([&a, row] { SelectRow(a, row); });
  }
  // Keyboard and gamepad selection follows the focused row.
  if (GImGui->NavJustMovedToId == gid && !sel) ui.deferred.push_back([&a, row] { SelectRow(a, row); });
  const bool focus = ImGui::GetFocusID() == gid && GImGui->NavCursorVisible;
  const Row state = StatusOf(a, index); const Tone tone = RowTone(state);
  const ImRect card(item.Min.x + s(8), item.Min.y + s(4), item.Max.x - s(8), item.Max.y - s(4));
  auto* dl = ImGui::GetWindowDrawList();
  Round(dl, card, s(12), sel ? Mix(P.surface, P.accent, 18) : hot ? Mix(P.surface2, Rgb(255, 255, 255), 4) : P.surface2, sel ? P.accent_text : P.border, sel && focus ? 2.0f : 1.0f);
  const ImRect badge = Rect(card.Min.x + s(14), card.Min.y + s(10), s(44), s(44));
  if (auto* image = dashboard ? nullptr : Thumbnail(ui, t)) dl->AddImageRounded(image->GetTexRef(), badge.Min, badge.Max, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, s(10));
  else {
    const auto color = ToneColor(tone == Tone::Neutral ? Tone::Info : tone);
    Round(dl, badge, s(10), Mix(P.surface2, color, 18));
    Icon(dl, dashboard ? "dashboard" : "gamepad", badge, color);
  }
  const auto status = RowLabel(state, dashboard);
  const float bw = ChipWidth(status);
  const float chip_x = card.Max.x - s(16) - bw, chip_y = card.GetCenter().y - s(13);
  auto* page = ui.dl; ui.dl = dl;
  Chip(ui, chip_x, chip_y, status, tone);
  ui.dl = page;
  const float tx = badge.Max.x + s(16);
  Text(dl, Font::Bold, english ? t.label_en : t.label, ImRect(tx, card.Min.y + s(10), chip_x - s(12), card.Min.y + s(32)), P.text);
  std::string line = dashboard ? "Perfil: " + t.id + (t.version.empty() ? "" : "   ·   " + t.version) :
    "Title ID: " + (t.title_id.empty() ? std::string("—") : t.title_id) + "   ·   " + Tr("Versão: ", "Version: ") + (t.version.empty() ? std::string("—") : t.version);
  Text(dl, Font::Caption, line, ImRect(tx, card.Min.y + s(34), chip_x - s(12), card.Min.y + s(56)), P.muted);
}
}  // namespace
void ListPage(Ui& ui, const Frame& f, bool dashboard) {
  auto& a = ui.a;
  const float cx = f.cx, cw = f.cw;
  float top = f.yc;
  if (!dashboard) {
    const float sw = std::min(s(300), cw / 2);
    const ImRect search = Rect(cx, top, sw, s(40));
    Round(ui.dl, search, s(10), P.surface2, P.border);
    Icon(ui.dl, "search", ImRect(search.Min.x + s(12), search.Min.y, search.Min.x + s(34), search.Max.y), P.muted);
    const float eh = FontSize(Font::Body) + s(4);
    ImGui::SetCursorScreenPos(ImVec2(cx + s(38), top + (s(40) - eh) / 2));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, s(2)));
    ImGui::SetNextItemWidth(sw - s(38) - s(12));
    if (ui.focus_search) { ImGui::SetKeyboardFocusHere(); ui.focus_search = false; }
    Push(Font::Body);
    ImGui::BeginDisabled(!Enabled(a, kSearch));
    char buffer[256]{};
    std::snprintf(buffer, sizeof(buffer), "%s", ui.search.c_str());
    if (ImGui::InputTextWithHint("##search", Tr("Buscar jogo, Title ID ou versão", "Search game, Title ID or version").c_str(), buffer, sizeof(buffer))) {
      ui.search = buffer;
      ui.deferred.push_back([&ui] { SetQuery(ui.a, ui.search); });
    }
    ImGui::EndDisabled();
    Pop();
    ImGui::PopStyleVar(); ImGui::PopStyleColor();
    ui.Place(kSearch, Rect(cx + s(38), top + (s(40) - eh) / 2, sw - s(38) - s(12), eh), "edit");
    float fx = cx + sw + s(16);
    for (int i = 0; i < 4; ++i) {
      const float w = TextWidth(Font::Body, Label(a, kFilter0 + i)) + s(32);
      Button(ui, kFilter0 + i, Kind::Chip, Rect(fx, top, w, s(40)));
      fx += w + s(8);
    }
    top += s(52);
  }
  if (a.rows.empty()) {
    const ImRect empty = Rect(cx, top, cw, std::max(s(160), f.bottom - top));
    Card(ui, empty); ui.card = empty;
    const bool filtered = a.filter != 0 || !a.query.empty();
    const float mid = empty.GetCenter().x;
    const ImRect disc(mid - s(32), empty.Min.y + s(28), mid + s(32), empty.Min.y + s(92));
    Circle(ui.dl, disc, Mix(P.surface, P.accent_text, 14));
    Icon(ui.dl, filtered ? "search" : !dashboard ? "gamepad" : "dashboard", disc, P.accent_text, 26);
    Text(ui.dl, Font::Big, filtered ? Tr("Nada encontrado", "Nothing found") : !dashboard ? Tr("Sua biblioteca começa aqui", "Your library starts here") : Tr("Nenhuma dashboard disponível", "No dashboard available"),
         ImRect(empty.Min.x + s(24), empty.Min.y + s(92), empty.Max.x - s(24), empty.Min.y + s(124)), P.text, Center);
    const auto text = filtered ? Tr("Nenhum jogo corresponde à busca ou ao filtro atual.", "No game matches the current search or filter.") : a.status;
    TextBlock(ui, 0, text, Rect(cx + s(40), empty.Min.y + s(126), cw - s(80), s(56)), Font::Body, P.text, true, Center);
    if (!filtered) Button(ui, kPrimary, Kind::Primary, Rect(cx + (cw - s(240)) / 2, empty.Min.y + s(196), s(240), s(46)), nullptr, 2);
    ui.card = {};
    return;
  }
  const bool removable = !dashboard && Removable(a);
  const float dh = dashboard || removable ? s(196) : s(150);
  const ImRect detail = Rect(cx, f.bottom - dh, cw, dh);
  const ImRect list = Rect(cx, top, cw, std::max(s(96), detail.Min.y - s(12) - top));
  // Rows scroll inside the list; wheel, arrows and the gamepad move through them.
  ImGui::SetCursorScreenPos(list.Min);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
  ImGui::BeginChild("rows", list.GetSize(), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
  const float row_h = s(76);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const float row_w = ImGui::GetContentRegionAvail().x;
  for (size_t r = 0; r < a.rows.size(); ++r) {
    ImGui::PushID(int(r));
    DrawRow(ui, r, a.rows[r], Rect(origin.x, origin.y + r * row_h, row_w, row_h));
    ImGui::PopID();
  }
  ImGui::EndChild();
  ImGui::PopStyleVar(2);
  ui.Place(kGameList, list, "list");
  Card(ui, detail); ui.card = detail;
  Chip(ui, detail.Min.x + s(24), detail.Min.y + s(20), a.chip_text, a.chip_tone);
  const float aw = std::min(cw / 2, std::max({s(220), ButtonWidth(ui, kPrimary, true), ButtonWidth(ui, kAddContent, true)}));
  const float tx = detail.Min.x + s(24) + ChipWidth(a.chip_text) + s(12);
  Text(ui.dl, Font::Bold, a.selection_title, ImRect(tx, detail.Min.y + s(18), detail.Max.x - s(24) - aw - s(24), detail.Min.y + s(46)), P.text);
  TextBlock(ui, 0, a.status, Rect(cx + s(24), detail.Min.y + s(58), cw - s(48) - aw - s(24), s(80)));
  Button(ui, kPrimary, Kind::Primary, Rect(cx + cw - s(24) - aw, detail.Min.y + s(22), aw, s(46)), nullptr, 2);
  Button(ui, kAddContent, Kind::Secondary, Rect(cx + cw - s(24) - aw, detail.Min.y + s(22) + s(46) + s(12), aw, s(40)), "add", 2);
  if (removable) Button(ui, kRemove, Kind::Secondary, Rect(cx + cw - s(24) - aw, detail.Min.y + s(22) + s(46) + s(12) + s(40) + s(8), aw, s(36)), "remove", 2);
  if (dashboard) DashboardNeeds(ui, detail);
  ui.card = {};
}
}  // namespace x360n::ui
