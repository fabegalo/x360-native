#include <cstdio>
#include "../frontend.h"
#include "app_text.h"

namespace x360n::ui {
using namespace desktop;
namespace {
float s(float v) { return T().S(v); }
#ifdef _WIN32
constexpr bool kD3D12 = true;
#else
constexpr bool kD3D12 = false;  // only the APIs this system has are offered
#endif
// Numeric field on a painted frame; Enter applies the limits like the button beside it.
void Field(Ui& ui, int id, std::string& value, const ImRect& frame) {
  auto& a = ui.a;
  const bool on = Enabled(a, id);
  const ImGuiID gid = ImGui::GetID(id);
  const bool focused = ImGui::GetFocusID() == gid || ImGui::GetActiveID() == gid;
  Round(ui.dl, frame, s(8), P.surface2, focused ? P.accent_text : on ? P.border : Mix(P.surface, P.border, 50), focused ? 2.0f : 1.0f);
  const float eh = FontSize(Font::Body) + s(4);
  ImGui::SetCursorScreenPos(ImVec2(frame.Min.x + s(10), frame.Min.y + (frame.GetHeight() - eh) / 2));
  ImGui::SetNextItemWidth(frame.GetWidth() - s(20));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(on ? P.text : P.faint));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, s(2)));
  ImGui::BeginDisabled(!on);
  Push(Font::Body);
  char buffer[8]{};
  std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
  ImGui::PushID(id);
  if (ImGui::InputText("##field", buffer, 4, ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
    value = buffer;
    if (Enabled(a, kTimingApply)) ui.Run(kTimingApply);
  } else value = buffer;
  ImGui::PopID();
  Pop();
  ImGui::EndDisabled();
  ImGui::PopStyleVar(); ImGui::PopStyleColor(2);
  ui.Place(id, frame, "edit");
}
void Combo(Ui& ui, int id, const ImRect& r) {
  auto& a = ui.a;
  struct Item { uint32_t value; const char* name; };
  std::vector<Item> items;
  uint32_t selected = 0;
  if (id == kConsoleLanguage) { for (const auto& v : ConsoleLanguages()) items.push_back({v.id, v.name}); selected = a.console_settings.language; }
  else { for (const auto& v : ConsoleLocales()) items.push_back({v.country, v.name}); selected = a.console_settings.country; }
  const char* preview = "";
  for (const auto& item : items) if (item.value == selected) preview = item.name;
  ImGui::SetCursorScreenPos(r.Min);
  ImGui::SetNextItemWidth(r.GetWidth());
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(P.surface2));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImGui::ColorConvertU32ToFloat4(Mix(P.surface2, Rgb(255, 255, 255), 6)));
  ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(P.surface));
  ImGui::PushStyleColor(ImGuiCol_Header, ImGui::ColorConvertU32ToFloat4(Mix(P.surface, P.accent, 18)));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::ColorConvertU32ToFloat4(Mix(P.surface, P.accent, 28)));
  ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(P.surface2));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s(10), (r.GetHeight() - FontSize(Font::Body)) / 2));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, s(8));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
  ImGui::BeginDisabled(!Enabled(a, id));
  Push(Font::Body);
  ImGui::PushID(id);
  if (ImGui::BeginCombo("##combo", preview, ImGuiComboFlags_HeightLarge)) {
    for (const auto& item : items) {
      const bool is = item.value == selected;
      if (ImGui::Selectable(item.name, is)) ui.deferred.push_back([&a, id, value = item.value] { SetConsoleValue(a, id, value); });
      if (is) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::PopID();
  Pop();
  ImGui::EndDisabled();
  ImGui::PopStyleVar(3); ImGui::PopStyleColor(6);
  ui.Place(id, r, "combo");
}
std::string EnvironmentText(const App& a) {
  const auto& e = a.environment;
  if (e.description.empty()) return Tr("Escolha um ambiente para verificar este computador.", "Choose an environment to check this computer.");
  std::string text;
  if (!e.cpu.empty()) text += "CPU: " + e.cpu + "\n";
  if (!e.memory.empty()) text += Tr("Memória: ", "Memory: ") + e.memory + "\n";
  for (const char c : e.description) if (c != '\r') text += c;
  while (!text.empty() && text.back() == '\n') text.pop_back();
  return text;
}
}  // namespace
void SettingsPage(Ui& ui, const Frame& f) {
  auto& a = ui.a;
  const float cx = f.cx, cw = f.cw;
  // Cards fill two columns, or three on wide windows, each one placed under
  // the shortest column in reading order, so no column ends far below the other.
  const float gap = s(16);
  const int columns = cw >= s(1150) ? 3 : cw >= s(700) ? 2 : 1;
  const float colw = (cw - (columns - 1) * gap) / columns;
  const float ow = colw - s(48);
  const bool native = a.renderer_settings.experimental && a.renderer_settings.renderer == Renderer::Native;
  const auto renderer_hint = Tr("O renderer vale para toda a sessão e é aplicado no próximo início.", "The renderer applies to the whole session on the next launch.");
  const auto enhanced_hint = std::string(native ? "" : Tr("Disponível somente com o renderer Native, liberado pelo acesso de desenvolvimento. ", "Available only with the Native renderer, unlocked by the development access. ")) +
    Tr("Separa limite visual e FPS de simulação; o alvo de simulação exige um perfil identificado do jogo. Padrão preserva o tempo original.", "Separates display and simulation limits; the simulation target requires an identified game profile. Default preserves original timing.");
  const auto console_hint = Tr("Aplicado ao iniciar uma nova sessão.\nIdiomas disponíveis dependem dos recursos originais de cada versão e jogo.", "Applies when starting a new session.\nAvailable languages depend on each version's and game's original resources.");
  const auto env_text = EnvironmentText(a);
  const auto about = "x360-native " + VersionLabel(X360N_VERSION) + " · " + kPlatform;
  const float hint_h = WrappedHeight(Font::Body, renderer_hint, ow), enhanced_h = WrappedHeight(Font::Body, enhanced_hint, ow);
  const float console_h = WrappedHeight(Font::Body, console_hint, ow), env_h = WrappedHeight(Font::Body, env_text, ow);
  const float about_h = WrappedHeight(Font::Body, about, ow) + s(4);
  float need = 0;
  for (int id : {kFolder, kStorage, kData, kCheck}) need = std::max(need, ButtonWidth(ui, id, false));
  const bool pairs = 2 * need + s(12) <= ow;
  const int storage_rows = pairs ? 2 : 4;
  const float combo_h = s(34);
  // Section caption, option (two text lines), toggle/chip/field rows, label above a row.
  const float cap = s(26), opt = s(58), row = s(36), label = s(26);
  const int apis = kD3D12 ? 2 : 1;
  const float heights[5] = {
    s(52) + hint_h + s(16) + 3 * cap + (2 + apis) * opt + (apis) * s(8) + 2 * s(16) + 2 * row + s(4) + s(20),
    s(52) + enhanced_h + s(12) + row + s(16) + label + 3 * (row + s(12)) + row + s(16) + row + s(20) + 3 * (label + row + s(12)) + row + s(20),
    s(56) + s(24) + s(12) + storage_rows * s(46) - s(8) + s(16) + env_h + s(20),
    s(50) + 2 * (s(22) + s(4) + combo_h + s(14)) + console_h + s(20),
    s(48) + row + s(16) + about_h + s(20)};
  // The cards scroll inside the viewport below the header; focus scrolls a control into view.
  const ImRect viewport(f.sb + 1, f.yc, f.W, f.H);
  ImGui::SetCursorScreenPos(viewport.Min);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, s(10));
  ImGui::BeginChild("settings", viewport.GetSize(), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
  auto* page = ui.dl;
  ui.dl = ImGui::GetWindowDrawList();
  ui.scrolled = true;
  const float top = ImGui::GetCursorScreenPos().y;
  float column_bottom[3] = {top, top, top};
  ImRect cards[5];
  for (int i = 0; i < 5; ++i) {
    const int c = int(std::min_element(column_bottom, column_bottom + columns) - column_bottom);
    cards[i] = Rect(cx + c * (colw + gap), column_bottom[c], colw, heights[i]);
    column_bottom[c] += heights[i] + gap;
  }
  for (const auto& c : cards) Card(ui, c);
  Heading(ui, cards[0], "chip", Tr("Gráficos", "Graphics"));
  Heading(ui, cards[1], "settings", "Native Enhanced");
  const auto experimental = Tr("Experimental", "Experimental");
  Chip(ui, cards[1].Max.x - s(24) - ChipWidth(experimental), cards[1].Min.y + s(17), experimental, Tone::Warn);
  Heading(ui, cards[2], "save", Tr("Armazenamento e ambiente", "Storage and environment"));
  Heading(ui, cards[3], "language", Tr("Console Xbox 360", "Xbox 360 console"));
  Heading(ui, cards[4], "tools", Tr("Aplicativo desktop", "Desktop application"));
  auto caption = [&](float x, float y, const std::string& text) { Text(ui.dl, Font::Caption, text, Rect(x, y, ow, s(18)), P.muted); };
  auto option_label = [&](int id, float x, float y, float w) {
    TextBlock(ui, id, Label(a, id), Rect(x, y, w, s(22)), Font::Body, Enabled(a, kFps30) ? P.text : P.faint, false);
  };
  auto field = [&](int label_id, int edit_id, std::string& value, float x, float y) {
    const float field_w = s(88);
    option_label(label_id, x, y + (row - s(22)) / 2, ow - field_w - s(12));
    Field(ui, edit_id, value, Rect(x + ow - field_w, y, field_w, row));
  };
  // Equal-width chips across the row with a fixed gutter.
  auto chips = [&](int first, int count, float x, float y, float width) {
    const float w = (width - (count - 1) * s(6)) / count;
    for (int i = 0; i < count; ++i) Button(ui, first + i, Kind::Chip, Rect(x + i * (w + s(6)), y, w, row), nullptr, 2);
  };
  {
    const ImRect& c = cards[0]; ui.card = c;
    const float x = c.Min.x + s(24);
    float y = c.Min.y + s(52);
    TextBlock(ui, 0, renderer_hint, Rect(x, y, ow, hint_h), Font::Body, P.muted); y += hint_h + s(16);
    caption(x, y, "Renderer"); y += cap;
    Button(ui, kRenderer, Kind::Option, Rect(x, y, ow, opt), nullptr, 2); y += opt + s(8);
    Button(ui, kRendererNative, Kind::Option, Rect(x, y, ow, opt), nullptr, 2); y += opt + s(16);
    caption(x, y, Tr("API gráfica", "Graphics API")); y += cap;
    if (kD3D12) { Button(ui, kApiD3D12, Kind::Option, Rect(x, y, ow, opt), nullptr, 2); y += opt + s(8); }
    Button(ui, kApiVulkan, Kind::Option, Rect(x, y, ow, opt), nullptr, 2); y += opt + s(16);
    caption(x, y, Tr("Sessão", "Session")); y += cap;
    Button(ui, kNativeDevelopment, Kind::Toggle, Rect(x, y, ow, row), nullptr, 2); y += row + s(4);
    Button(ui, kFullscreen, Kind::Toggle, Rect(x, y, ow, row), nullptr, 2);
  }
  {
    const ImRect& c = cards[1]; ui.card = c;
    const float x = c.Min.x + s(24);
    float y = c.Min.y + s(52);
    TextBlock(ui, 0, enhanced_hint, Rect(x, y, ow, enhanced_h), Font::Body, P.muted); y += enhanced_h + s(12);
    Button(ui, kEnhanced, Kind::Toggle, Rect(x, y, ow, row), nullptr, 2); y += row + s(16);
    // Presets and the custom target share one row; the custom value needs Apply.
    option_label(kFpsLabel, x, y, ow); y += label;
    const float field_w = s(88);
    chips(kFps30, 4, x, y, ow - field_w - s(6));
    Field(ui, kFpsCustom, a.fps_text, Rect(x + ow - field_w, y, field_w, row));
    y += row + s(12);
    field(kPresentLabel, kPresentRate, a.present_text, x, y); y += row + s(12);
    chips(kEffectBilinear, 3, x, y, ow); y += row + s(12);
    field(kSharpnessLabel, kSharpness, a.sharpness_text, x, y); y += row + s(16);
    Button(ui, kTimingApply, Kind::Secondary, Rect(x, y, std::min(ow, std::max(s(220), ButtonWidth(ui, kTimingApply, false))), row), nullptr, 2); y += row + s(20);
    option_label(kScaleLabel, x, y, ow); y += label;
    chips(kScale1, 4, x, y, ow); y += row + s(12);
    option_label(kAnisoLabel, x, y, ow); y += label;
    chips(kAnisoGuest, 5, x, y, ow); y += row + s(12);
    option_label(kAaLabel, x, y, ow); y += label;
    chips(kAaNone, 3, x, y, ow); y += row + s(12);
    Button(ui, kUltrawide, Kind::Toggle, Rect(x, y, ow, row), nullptr, 2);
  }
  {
    const ImRect& c = cards[2]; ui.card = c;
    const float x = c.Min.x + s(24);
    // Display only: a shortcut's "scripts/.." root reads as the folder it names.
    auto shown = a.root.lexically_normal();
    if (shown.has_relative_path() && !shown.has_filename()) shown = shown.parent_path();
    TextBlock(ui, 0, a.root.empty() ? Tr("Nenhum ambiente selecionado", "No environment selected") : Utf8(shown), Rect(x, c.Min.y + s(56), ow, s(24)), Font::Body, P.muted, false);
    const float bw2 = pairs ? (ow - s(12)) / 2 : ow;
    int slot = 0;
    for (int id : {kFolder, kStorage, kData, kCheck}) {
      const int col = pairs ? slot % 2 : 0, line = pairs ? slot / 2 : slot;
      Button(ui, id, Kind::Secondary, Rect(x + col * (bw2 + s(12)), c.Min.y + s(92) + line * s(46), bw2, s(38)), nullptr, 2);
      ++slot;
    }
    TextBlock(ui, 0, env_text, Rect(x, c.Min.y + s(92) + storage_rows * s(46) - s(8) + s(16), ow, env_h), Font::Body, P.muted);
  }
  {
    const ImRect& c = cards[3]; ui.card = c;
    const float x = c.Min.x + s(24);
    float y = c.Min.y + s(50);
    TextBlock(ui, 0, Tr("Idioma da dashboard, Guide e jogos", "Dashboard, Guide and game language"), Rect(x, y, ow, s(22)), Font::Body, P.text, false); y += s(22) + s(4);
    Combo(ui, kConsoleLanguage, Rect(x, y, ow, combo_h)); y += combo_h + s(14);
    TextBlock(ui, 0, Tr("Região / localidade", "Region / locale"), Rect(x, y, ow, s(22)), Font::Body, P.text, false); y += s(22) + s(4);
    Combo(ui, kConsoleCountry, Rect(x, y, ow, combo_h)); y += combo_h + s(14);
    TextBlock(ui, 0, console_hint, Rect(x, y, ow, console_h), Font::Body, P.muted);
  }
  {
    const ImRect& c = cards[4]; ui.card = c;
    const float x = c.Min.x + s(24);
    const float chip_w = std::min((ow - s(8)) / 2, std::max({s(140), ButtonWidth(ui, kLangPt, false), ButtonWidth(ui, kLangEn, false)}));
    Button(ui, kLangPt, Kind::Chip, Rect(x, c.Min.y + s(48), chip_w, row), nullptr, 2);
    Button(ui, kLangEn, Kind::Chip, Rect(x + chip_w + s(8), c.Min.y + s(48), chip_w, row), nullptr, 2);
    // Selectable, so the exact build can be copied into a support report.
    ReadOnlyText(ui, "##about", about, Rect(x, c.Min.y + s(48) + row + s(16), ow, about_h), Font::Body, P.surface);
  }
  ui.card = {};
  float content = 0;
  for (const auto& c : cards) content = std::max(content, c.Max.y);
  ImGui::SetCursorScreenPos(ImVec2(viewport.Min.x, top));
  ImGui::Dummy(ImVec2(1, content - top + s(28)));
  if (ui.settings_scroll >= 0) ImGui::SetScrollY(std::min(ui.settings_scroll, ImGui::GetScrollMaxY()));
  ui.settings_scroll_max = ImGui::GetScrollMaxY();
  ui.scrolled = false;
  ui.dl = page;
  ImGui::EndChild();
  ImGui::PopStyleVar(2);
}
}  // namespace x360n::ui
