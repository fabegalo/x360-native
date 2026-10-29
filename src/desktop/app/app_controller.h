#pragma once
// Rules of the launcher, shared by every system: what each page shows, which
// action is next, which commands are allowed, and how worker results land.
#include "app_state.h"

namespace x360n::desktop {
// Package layout: the supervisor/extractor and payload beside the launcher.
std::filesystem::path Payload(const App& a);
std::filesystem::path Supervisor(const App& a);
std::filesystem::path Extractor(const App& a);
std::filesystem::path Updates(const App& a);
void VerifyDistribution(const std::filesystem::path& directory);

// Startup: package preferences, command line, environment check.
void ConfigureFromPreferences(App& a);
void Open(App& a);
// Runs the events posted by workers. Call on the frontend thread.
void Pump(App& a);
void Post(App& a, std::function<void(App&)> event);

Page PageOf(const App& a);
struct PageText { std::string headline, subtitle; };
PageText PageTexts(const App& a);
std::string DropText();
Row StatusOf(const App& a, size_t i);
std::string RowLabel(Row row, bool dashboard);
Tone RowTone(Row row);
bool Matches(const App& a, size_t i);
void RebuildRows(App& a);
bool Removable(const App& a);
void InspectSelected(App& a);
void Load(App& a, const std::filesystem::path& root, InspectionMode mode = InspectionMode::Preview);
void Navigate(App& a, int id);
void GoBack(App& a);
void SavePreferences(const App& a);
void UpdateRendererControls(App& a);
void SetActivity(App& a, Activity kind, const std::string& title, const std::string& text, std::vector<std::string> stages = {});

// One command from a page, the keyboard or a gamepad. Errors become the
// error banner, exactly like a failed operation.
void Command(App& a, int id);
void SelectRow(App& a, size_t row);
void SetQuery(App& a, const std::string& text);
void SetConsoleValue(App& a, int id, uint32_t value);
void Picked(App& a, int command, const std::filesystem::path& path);
void Drop(App& a, const std::filesystem::path& path);
void Answer(App& a, int button);
// Frontend close request: false while an operation must finish first.
bool RequestClose(App& a);

// Whether a command is available now, and whether an option is selected.
bool Enabled(const App& a, int id);
bool Selected(const App& a, int id);
std::string Label(const App& a, int id);
std::string OptionCaption(const App& a, int id);
// Glyph name of the primary action (play, tools, file, add, folder, settings).
const char* PrimaryGlyph(const App& a);
std::string Elapsed(const App& a);
std::string FormatBytes(uintmax_t bytes);
std::string Lower(const std::string& utf8);

// Worker results, posted by the operations and run by Pump.
void OnInspected(App& a, const std::string& error);
void OnInitialized(App& a, const std::string& error);
void OnEnvironmentChecked(App& a, const std::string& error);
void OnFinished(App& a, const std::string& text);
void OnProgress(App& a, const std::string& text);
void OnHinted(App& a, const App::Hints& found);
void OnSessionStarted(App& a);
void OnSessionClosed(App& a);
void OnUpdateChecked(App& a, std::optional<AvailableUpdate> found);
void OnUpdateFinished(App& a, std::string error);
void KeepSelectionVisible(App& a);

// Operations (operations.cpp): each runs on the worker and posts its result.
std::vector<App::CheckedTarget> CheckContents(const std::filesystem::path& root, const std::vector<Target>& targets,
                                             InspectionMode mode = InspectionMode::Full);
void AdoptSnapshot(App& a);
void StartHints(App& a);
void CheckEnvironmentAsync(App& a, const std::filesystem::path& root, bool verify_contents = false);
void CheckForUpdate(App& a);
void StartUpdate(App& a);
void Start(App& a, int action, std::filesystem::path source = {});
void ApplyPreview(App& a, int state);
// Stage index of a supervisor "X360N_STAGE <title> <name>" line, -1 unknown.
int PreparationStage(const std::string& stage);
std::vector<std::string> PreparationStageNames();
}  // namespace x360n::desktop
