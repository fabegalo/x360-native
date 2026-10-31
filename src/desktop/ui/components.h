#pragma once
// Interactive pieces of the launcher drawn over ImGui's item/navigation system:
// mouse, keyboard (Tab/arrows/Enter) and gamepad reach the same commands.
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include "app_controller.h"
#include "icons.h"

namespace x360n::ui {
using desktop::App;
enum class Kind { Secondary, Primary, Nav, Chip, Ghost, Option, Toggle };
// Every interactive or text element placed this frame; layout tests read it.
struct Placed { int id; ImRect rect, card; std::string kind, text; float need = 0; bool scrolled = false; };
struct Ui {
  App& a;
  ImDrawList* dl = nullptr;
  ImRect window{}, card{};  // current container (card) for placement checks
  bool record = false, scrolled = false;  // scrolled: inside a scrolling viewport
  std::vector<Placed> placed;
  std::vector<ImRect> cards;
  std::vector<std::function<void()>> deferred;  // commands run after the frame
  // Decoded package icons by SHA-256; an empty entry is a failed decode.
  std::map<std::string, std::unique_ptr<ImTextureData>> thumbnails;
  std::function<void(ImTextureData*)> upload;   // renderer texture upload
  std::string search;                            // search box text
  bool focus_search = false;
  bool popup_was_open = false, native_expanded = false;
  int diagnostic_tab = 0;
  bool diagnostic_copied = false;
  float settings_scroll = -1, settings_scroll_max = 0;  // review aid: force the Settings scroll position
  int last_page = -1;
  explicit Ui(App& app) : a(app) {}
  void Run(int id) { deferred.push_back([this, id] {
    if (id == desktop::kViewIssue) diagnostic_tab = 1;
    if (id == desktop::kBannerDetails) diagnostic_tab = 2;
    diagnostic_copied = false;
    desktop::Command(a, id);
  }); }
  void Place(int id, const ImRect& r, const char* kind, const std::string& text = {}, float need = 0) {
    if (record) placed.push_back({id, r, card, kind, text, need, scrolled});
  }
};
inline ImRect Rect(float x, float y, float w, float h) { return ImRect(x, y, x + w, y + h); }
// Card surface. Placement checks expect every control that touches it to stay inside.
void Card(Ui& ui, const ImRect& r);
void Chip(Ui& ui, float x, float y, const std::string& text, desktop::Tone tone);
float ChipWidth(const std::string& text);
Color ToneColor(desktop::Tone tone);
// A command button. back: 0 page, 1 sidebar, 2 card surface.
bool Button(Ui& ui, int id, Kind kind, const ImRect& r, const char* glyph = nullptr, int back = 0);
float ButtonWidth(const Ui& ui, int id, bool glyph);
// Static text placed for checks: wrapped (multi-line) or single line.
void TextBlock(Ui& ui, int id, const std::string& text, const ImRect& r, Font font = Font::Body, Color color = P.text, bool wrap = true, Align align = Left);
// Card heading: icon and title.
void Heading(Ui& ui, const ImRect& card, const char* glyph, const std::string& text);
}  // namespace x360n::ui
