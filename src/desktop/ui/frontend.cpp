#include "frontend.h"
#include <algorithm>
#include "app_text.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstring>

namespace x360n::ui {
using namespace desktop;
namespace {
float s(float v) { return T().S(v); }
const char* BannerGlyph(const App& a) {
  if (a.busy) return a.activity_kind == Activity::Import ? "download" : a.activity_kind == Activity::Prepare ? "tools" :
                     a.activity_kind == Activity::Session ? "play" : "refresh";
  return a.result == 3 ? "error" : a.result == 2 ? "warning" : "check";
}
// Activity/result banner: title, wrapped message, real stages and actions.
float Banner(Ui& ui, const Frame& f, float yc) {
  auto& a = ui.a;
  const float cx = f.cx, cw = f.cw;
  const bool busy = a.busy;
  const float tw = cw - s(76) - s(24), line = FontSize(Font::Body) * 1.2f;
  const float text_h = std::clamp(WrappedHeight(Font::Body, a.activity_text, tw), line, 4 * line);
  const float text_top = s(58), bar_top = text_top + text_h + s(12);
  const bool stages = busy && !a.stage_names.empty();
  const float right = cx + cw - s(24);
  std::vector<int> actions;
  if (!busy && a.result == 3) actions = {kBannerDetails, kRetry};
  else if (!busy && a.result == 1 && a.importing) actions = {kBannerLibrary};
  float actions_w = 0;
  for (int id : actions) actions_w += ButtonWidth(ui, id, true) + s(8);
  const float title_w = TextWidth(Font::Big, a.result_title);
  const bool action_row = !actions.empty() && cx + s(76) + title_w + s(16) > right - s(36) - actions_w;
  const float bh = busy ? bar_top + s(6) + (stages ? s(10) + s(26) : 0) + s(14) :
                   text_top + text_h + (action_row ? s(12) + s(36) : 0) + s(18);
  const ImRect b = Rect(cx, yc, cw, bh);
  const Tone tone = busy ? Tone::Info : a.result == 3 ? Tone::Err : a.result == 2 ? Tone::Warn : Tone::Ok;
  const Color color = busy ? P.accent_text : ToneColor(tone);
  Round(ui.dl, b, s(14), P.surface, busy ? P.border : Mix(P.surface, color, 45));
  if (ui.record) ui.cards.push_back(b);
  ui.card = b;
  const ImRect disc(b.Min.x + s(24), b.Min.y + s(16), b.Min.x + s(64), b.Min.y + s(56));
  Circle(ui.dl, disc, Mix(P.surface, color, 20));
  Icon(ui.dl, BannerGlyph(a), disc, color);
  float title_right = right;
  if (busy) {
    const float cancel_w = std::max(s(130), ButtonWidth(ui, kCancel, true));
    if (!a.session) Button(ui, kCancel, Kind::Secondary, Rect(right - cancel_w, yc + s(16), cancel_w, s(36)), "cancel", 2);
    const ImRect elapsed = Rect(right - cancel_w - s(12) - s(150), yc + s(18), s(150), s(28));
    title_right = elapsed.Min.x - s(8);
    std::string time = Elapsed(a);
    if (a.activity_kind == Activity::Import && a.stage == 0 && a.extracted_bytes) time += "   ·   " + FormatBytes(a.extracted_bytes);
    Text(ui.dl, Font::Caption, time, elapsed, P.muted, Right);
    // Progress: completed stages are solid; the current stage animates because no percentage is known.
    const ImRect bar = Rect(cx + s(24), yc + bar_top, cw - s(48), s(6));
    Round(ui.dl, bar, bar.GetHeight() / 2, P.surface2);
    const int count = int(a.stage_names.size());
    const float width = bar.GetWidth();
    const int done = count ? std::min(a.stage.load(), count) : 0;
    if (count && done > 0) Round(ui.dl, ImRect(bar.Min.x, bar.Min.y, bar.Min.x + width * done / count, bar.Max.y), bar.GetHeight() / 2, P.accent);
    const float seg_left = count ? bar.Min.x + width * done / count : bar.Min.x;
    const float seg_right = count ? bar.Min.x + width * std::min(done + 1, count) / count : bar.Max.x;
    if (seg_right > seg_left) {
      const float span = seg_right - seg_left, sweep = std::max(s(40), span / 3);
      const float t = float(std::fmod(ImGui::GetTime() * 1000.0, 1400.0) / 1400.0);
      const float pos = t * (span + sweep) - sweep;
      const float l = std::max(seg_left, seg_left + pos), r = std::min(seg_right, seg_left + pos + sweep);
      if (r > l) Round(ui.dl, ImRect(l, bar.Min.y, r, bar.Max.y), bar.GetHeight() / 2, P.accent_text);
    }
    if (stages) {
      const ImRect st = Rect(cx + s(24), bar.Max.y + s(10), cw - s(48), s(26));
      const float sw = st.GetWidth() / count;
      for (int i = 0; i < count; ++i) {
        const bool d = i < done, cur = i == done;
        const float dx = st.Min.x + i * sw, cy = st.GetCenter().y;
        Circle(ui.dl, ImRect(dx, cy - s(5), dx + s(10), cy + s(5)), d ? P.accent_text : cur ? P.info : P.faint);
        Text(ui.dl, cur ? Font::Bold : Font::Caption, a.stage_names[i], ImRect(dx + s(16), st.Min.y, dx + sw - s(4), st.Max.y), d ? P.muted : cur ? P.text : P.faint);
      }
    }
  } else {
    Button(ui, kDismiss, Kind::Ghost, Rect(right - s(36), yc + s(16), s(36), s(36)), "cancel", 2);
    title_right = right - s(36) - s(16);
    if (action_row) {
      float bx = cx + s(76);
      for (auto it = actions.rbegin(); it != actions.rend(); ++it) {
        const float w = ButtonWidth(ui, *it, true);
        Button(ui, *it, Kind::Secondary, Rect(bx, yc + text_top + text_h + s(12), w, s(36)), *it == kRetry ? "refresh" : *it == kBannerDetails ? "info" : "library", 2);
        bx += w + s(8);
      }
    } else {
      float bx = right - s(36);
      for (int id : actions) {
        const float w = ButtonWidth(ui, id, true); bx -= s(8) + w;
        Button(ui, id, Kind::Secondary, Rect(bx, yc + s(16), w, s(36)), id == kRetry ? "refresh" : id == kBannerDetails ? "info" : "library", 2);
      }
      title_right = bx - s(16);
    }
  }
  Text(ui.dl, Font::Big, busy ? a.activity_title : a.result_title, ImRect(b.Min.x + s(76), b.Min.y + s(14), title_right, b.Min.y + s(48)), P.text);
  ImGui::PushClipRect(ImVec2(cx + s(76), yc + text_top), ImVec2(cx + s(76) + tw, yc + text_top + text_h), true);
  TextBlock(ui, 0, a.activity_text, Rect(cx + s(76), yc + text_top, tw, text_h), Font::Body, P.text);
  ImGui::PopClipRect();
  ui.card = {};
  return bh;
}
void Sidebar(Ui& ui, const Frame& f) {
  auto* dl = ui.dl;
  const float sb = f.sb, H = f.H;
  dl->AddRectFilled(ImVec2(0, 0), ImVec2(sb, H), P.sidebar);
  dl->AddRectFilled(ImVec2(sb - 1, 0), ImVec2(sb, H), P.border);
  // Brand
  Round(dl, ImRect(s(20), s(28), s(56), s(64)), s(9), P.accent);
  Text(dl, Font::Big, "X", ImRect(s(20), s(28), s(56), s(64)), P.on_accent, Center);
  Text(dl, Font::Bold, "x360-native", ImRect(s(66), s(26), sb - s(12), s(46)), P.text);
  Text(dl, Font::Caption, "Xbox 360", ImRect(s(66), s(44), sb - s(12), s(64)), P.muted);
  const float nx = s(12), nw = sb - s(24), nh = s(42);
  float ny = s(92);
  const std::pair<int, const char*> nav[] = {{kHome, "home"}, {kGames, "library"}, {kImportPage, "download"}, {kDashboards, "dashboard"}};
  for (auto [id, glyph] : nav) { Button(ui, id, Kind::Nav, Rect(nx, ny, nw, nh), glyph, 1); ny += nh + s(4); }
  // Measure the footer so long version identifiers wrap instead of being cut.
  const float fx = s(20), fw = sb - s(36);
  const auto version = "v" + std::string(X360N_VERSION);
  const auto footer = version + "\n" + kPlatform;
  const float footer_h = WrappedHeight(Font::Caption, footer, fw);
  const float footer_y = H - s(20) - footer_h;
  ui.card = Rect(0, 0, sb, H);
  TextBlock(ui, 0, footer, Rect(fx, footer_y, fw, footer_h), Font::Caption, P.muted);
  dl->AddRectFilled(ImVec2(fx, footer_y - s(12)), ImVec2(fx + fw, footer_y - s(11)), P.border);
  float by = footer_y - s(24);
  by -= nh; Button(ui, kLanguage, Kind::Nav, Rect(nx, by, nw, nh), "language", 1);
  by -= nh + s(4); Button(ui, kDetails, Kind::Nav, Rect(nx, by, nw, nh), "info", 1);
  by -= nh + s(4); Button(ui, kSettings, Kind::Nav, Rect(nx, by, nw, nh), "settings", 1);
  if (ui.a.update) { by -= nh + s(12); Button(ui, kUpdate, Kind::Primary, Rect(nx, by, nw, nh), "download", 1); }
  ui.card = {};
}
void PromptModal(Ui& ui) {
  auto& a = ui.a;
  if (!a.prompt) return;
  if (!ImGui::IsPopupOpen("##prompt")) ImGui::OpenPopup("##prompt");
  const auto& view = *ImGui::GetMainViewport();
  const float w = std::min(view.Size.x - s(64), s(560));
  ImGui::SetNextWindowPos(view.GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(w, 0));
  ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(P.surface));
  ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(a.prompt->warning ? Mix(P.surface, P.warn, 45) : P.border));
  ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0, 0, 0, 0.55f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(s(24), s(20)));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, s(14));
  if (ImGui::BeginPopupModal("##prompt", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
    auto* dl = ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float inner = w - s(48);
    Icon(dl, a.prompt->warning ? "warning" : "info", Rect(at.x, at.y, s(28), s(28)), a.prompt->warning ? P.warn : P.accent_text, 20);
    Text(dl, Font::Big, a.prompt->title, Rect(at.x + s(40), at.y, inner - s(40), s(28)), P.text);
    const float text_h = WrappedHeight(Font::Body, a.prompt->text, inner);
    Wrapped(dl, Font::Body, a.prompt->text, Rect(at.x, at.y + s(44), inner, text_h), P.muted);
    ImGui::Dummy(ImVec2(inner, s(44) + text_h + s(20)));
    // Buttons right-aligned; the first answer is the default and takes focus.
    float bx = at.x + inner;
    const float by = ImGui::GetCursorScreenPos().y;
    int answered = -2;
    for (int i = int(a.prompt->buttons.size()) - 1; i >= 0; --i) {
      const auto& text = a.prompt->buttons[size_t(i)];
      const float bw = std::max(s(110), TextWidth(Font::Bold, text) + s(32));
      bx -= bw;
      const ImRect r = Rect(bx, by, bw, s(40));
      ImGui::SetCursorScreenPos(r.Min);
      ImGui::PushID(i);
      const bool first = i == 0;
      if (first && ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
      ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(first ? P.accent : P.surface2));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(first ? P.accent_hover : Mix(P.surface2, Rgb(255, 255, 255), 9)));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4(first ? P.accent_press : Mix(P.surface2, Rgb(255, 255, 255), 14)));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, s(10));
      Push(Font::Bold);
      if (ImGui::Button(text.c_str(), r.GetSize())) answered = i;
      Pop();
      ImGui::PopStyleVar(); ImGui::PopStyleColor(3); ImGui::PopID();
      bx -= s(8);
      if (ui.record) ui.placed.push_back({0, r, {}, "button", text, TextWidth(Font::Bold, text) + s(16)});
    }
    if (a.prompt->buttons.size() > 1 && ImGui::IsKeyPressed(ImGuiKey_Escape)) answered = -1;
    if (answered != -2) {
      ImGui::CloseCurrentPopup();
      ui.deferred.push_back([&a, answered] { Answer(a, answered); });
    }
    ImGui::EndPopup();
  }
  ImGui::PopStyleVar(2); ImGui::PopStyleColor(3);
}
}  // namespace

ImTextureData* Thumbnail(Ui& ui, const Target& t) {
  if (t.thumbnail_sha256.empty()) return nullptr;
  auto [it, inserted] = ui.thumbnails.try_emplace(t.thumbnail_sha256);
  if (!inserted) return it->second.get();
  const auto png = ReadThumbnail(t);
  if (png.empty() || !ui.upload) return nullptr;
  // SDL's own PNG loader; the decoded surface is converted to RGBA bytes.
  SDL_Surface* decoded = SDL_LoadPNG_IO(SDL_IOFromConstMem(png.data(), png.size()), true);
  if (!decoded) return nullptr;
  SDL_Surface* rgba = SDL_ConvertSurface(decoded, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(decoded);
  if (!rgba) return nullptr;
  const int w = rgba->w, h = rgba->h;
  if (w > 0 && h > 0 && w <= 1024 && h <= 1024) {
    auto texture = std::make_unique<ImTextureData>();
    texture->Create(ImTextureFormat_RGBA32, w, h);
    for (int y = 0; y < h; ++y)
      std::memcpy(static_cast<unsigned char*>(texture->GetPixels()) + size_t(y) * size_t(w) * 4, static_cast<const unsigned char*>(rgba->pixels) + size_t(y) * size_t(rgba->pitch), size_t(w) * 4);
    texture->UseColors = true;
    ui.upload(texture.get());
    if (texture->Status == ImTextureStatus_OK) it->second = std::move(texture);
  }
  SDL_DestroySurface(rgba);
  return it->second.get();
}
void ReadOnlyText(Ui& ui, const char* id, const std::string& text, const ImRect& r, Font font, Color back) {
  ImGui::SetCursorScreenPos(r.Min);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(back));
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(P.text));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
  Push(font);
  ImGui::InputTextMultiline(id, const_cast<char*>(text.c_str()), text.size() + 1, r.GetSize(), ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_WordWrap);
  Pop();
  ImGui::PopStyleVar(); ImGui::PopStyleColor(2);
  ui.Place(0, r, "edit", text);
}
void DrawFrame(Ui& ui) {
  auto& a = ui.a;
  const auto& io = ImGui::GetIO();
  ui.placed.clear(); ui.cards.clear(); ui.card = {};
  // Escape and the gamepad's B go back from Settings/Diagnostics; Ctrl+F searches.
  // NewFrame may already have consumed Escape to close a combo. Remember the
  // preceding frame so that the same key never also leaves the whole page.
  if (!ui.popup_was_open && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) && !io.WantTextInput &&
      (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))) ui.Run(kGoBack);
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F) && PageOf(a) == Page::Library) ui.focus_search = true;
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  ImGui::Begin("x360-native", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
               ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
  ImGui::PopStyleVar(2);
  ui.dl = ImGui::GetWindowDrawList();
  Frame f;
  f.W = io.DisplaySize.x; f.H = io.DisplaySize.y;
  ui.window = ImRect(0, 0, f.W, f.H);
  ui.dl->AddRectFilled(ImVec2(0, 0), io.DisplaySize, P.bg);
  f.sb = s(240); f.pad = s(28);
  // Navigation and page actions may share a command, but not a widget identity.
  ImGui::PushID("sidebar");
  Sidebar(ui, f);
  ImGui::PopID();
  const Page page = PageOf(a);
  if (int(page) != ui.last_page) { ui.last_page = int(page); ImGui::SetScrollY(0); }
  f.cx = f.sb + f.pad; f.cw = std::max(s(300), f.W - f.sb - 2 * f.pad);
  const float y = s(28);
  const bool diag = page == Page::Diagnostics;
  const auto texts = PageTexts(a);
  Text(ui.dl, Font::Headline, texts.headline, Rect(f.cx, y, f.cw - (diag ? s(216) : 0), s(44)), P.text);
  ui.Place(0, Rect(f.cx, y, f.cw - (diag ? s(216) : 0), s(44)), "line", texts.headline);
  TextBlock(ui, 0, texts.subtitle, Rect(f.cx, y + s(46), f.cw, s(28)), Font::Body, P.muted);
  if (diag) Button(ui, kData, Kind::Secondary, Rect(f.cx + f.cw - s(204), y + s(2), s(204), s(40)));
  f.yc = s(116); f.bottom = f.H - s(28);
  f.banner = (a.busy || a.result) && !diag;
  if (f.banner) {
    ImGui::PushID("operation");
    f.yc += Banner(ui, f, f.yc) + s(16);
    ImGui::PopID();
  }
  ImGui::PushID(int(page));
  switch (page) {
    case Page::Home: HomePage(ui, f); break;
    case Page::Library: ListPage(ui, f, false); break;
    case Page::Dashboard: ListPage(ui, f, true); break;
    case Page::Import: ImportPage(ui, f); break;
    case Page::Settings: SettingsPage(ui, f); break;
    case Page::Diagnostics: DiagnosticsPage(ui, f); break;
  }
  ImGui::PopID();
  PromptModal(ui);
  ui.popup_was_open = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
  ImGui::End();
}
void RunDeferred(Ui& ui) {
  auto deferred = std::move(ui.deferred); ui.deferred.clear();
  for (auto& run : deferred) run();
}
}  // namespace x360n::ui
