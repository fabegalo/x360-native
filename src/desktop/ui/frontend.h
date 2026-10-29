#pragma once
// The one frontend of the launcher. DrawFrame lays out and draws the whole
// window from the shared App state for the current ImGui frame; the same code
// runs in the application, in headless captures and in the layout tests.
#include "components.h"

namespace x360n::ui {
// Content area of the current page, below the header and the activity banner.
struct Frame { float W = 0, H = 0, sb = 0, pad = 0, cx = 0, cw = 0, yc = 0, bottom = 0; bool banner = false; };
void DrawFrame(Ui& ui);
// Runs the commands the frame queued; call after ImGui::Render().
void RunDeferred(Ui& ui);

void HomePage(Ui& ui, const Frame& f);
void ListPage(Ui& ui, const Frame& f, bool dashboard);
void DashboardNeeds(Ui& ui, const ImRect& detail);
void ImportPage(Ui& ui, const Frame& f);
void SettingsPage(Ui& ui, const Frame& f);
void DiagnosticsPage(Ui& ui, const Frame& f);
// Package icon of a game as a texture (decoded once per SHA-256), or null.
ImTextureData* Thumbnail(Ui& ui, const desktop::Target& t);
// Read-only, selectable multi-line text (diagnostics and the build identification).
void ReadOnlyText(Ui& ui, const char* id, const std::string& text, const ImRect& r, Font font, Color back);
}  // namespace x360n::ui
