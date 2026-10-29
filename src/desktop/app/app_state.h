#pragma once
// State of the shared launcher. No system handle, font or widget lives here:
// the same state drives the one frontend on Windows, Linux and macOS, and the
// tests drive it directly through the controller.
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include "../model/catalog.h"
#include "console_settings.h"
#include "environment.h"
#include "renderer_settings.h"
#include "update.h"

namespace x360n::desktop {
// Commands. Navigation, actions and every Settings option share one space so
// the pages, the keyboard/gamepad and the tests issue the same commands.
enum : int {
  kFolder = 101, kTargets, kCheck, kImport, kPrepare, kLaunch, kDetails, kData, kCancel, kLanguage, kInstall, kStorage, kPrimary, kSettings, kImportFolder, kDashboards, kGames, kHome, kImportPage, kAddContent, kLibrary, kGameList, kRenderer, kNativeDevelopment,
  kSearch, kFilter0, kFilter1, kFilter2, kFilter3, kLangPt, kLangEn, kRendererNative, kRetry, kDismiss, kBannerLibrary, kBannerDetails,
  kConsoleLanguage, kConsoleCountry, kVerifyTarget, kUpdate, kRemove, kFullscreen, kCaptureCrash, kApiD3D12, kApiVulkan,
  kEnhanced, kFps30, kFps60, kFps120, kFps144, kFpsCustom, kPresentRate, kTimingApply, kFpsLabel, kPresentLabel,
  kEffectBilinear, kEffectCas, kEffectFsr, kSharpnessLabel, kSharpness,
  kScaleLabel, kScale1, kScale2, kScale3, kScale4,
  kAnisoLabel, kAnisoGuest, kAniso2, kAniso4, kAniso8, kAniso16,
  kAaNone, kAaFxaa, kAaExtreme, kUltrawide, kAaLabel, kOpenLogs, kGoBack };
enum class Page { Home, Library, Import, Dashboard, Settings, Diagnostics };
enum class Row { Ready, Preparing, Imported, Unsupported, Error, Pending, Unchecked };
enum class Tone { Neutral, Ok, Warn, Err, Info };
enum class Activity { None = 0, Import = 1, Prepare = 2, Session = 3, Check = 4 };

// A question the user answers before an operation continues (consent for
// downloads, removal, a system installer, ...). The frontend shows it modally.
struct Prompt {
  std::string title, text;
  std::vector<std::string> buttons;  // first: the default/confirming answer
  bool warning = false;
  std::function<void(struct App&, int)> answer;  // button index, -1 dismissed
};
// A file or folder the frontend asks the system to pick for a command.
struct PickRequest { int command = 0; bool folder = true; std::string title; };

struct App {
  using Clock = std::chrono::steady_clock;
  std::filesystem::path self, root, logs, preferences;
  std::string startup_error;
  Environment environment;
  RendererSettings renderer_settings;
  bool capture_crash = false;
  ConsoleSettings console_settings;
  std::vector<Target> targets;
  struct CheckedTarget { Inspection state; std::vector<std::string> library; bool added = false; };
  std::vector<CheckedTarget> checked;
  // Worker writes only pending results; the controller adopts them after completion/join.
  std::vector<Target> pending_targets;
  std::vector<CheckedTarget> pending_checked;
  Environment pending_environment;
  RendererSettings pending_renderer;
  ConsoleSettings pending_console;
  bool pending_snapshot = false;
  PayloadStatus pending_payload;
  std::filesystem::path pending_root;
  bool pending_payload_current = false;
  bool supervisor_available = false, pending_supervisor_available = false;
  bool launch_verified = false;  // worker result, consumed only after completion
  bool catalog_loaded = false, pending_full_check = false;
  std::vector<size_t> dashboard_indices, rows;
  Inspection current;
  size_t selected = 0;
  std::thread worker;
  std::atomic<bool> cancel{false};
  std::atomic<bool> failed{false};
  std::atomic<int> stage{0};
  bool busy = false, session = false, finishing_session = false, show_details = false;
  bool allow_downloads = false;
  bool installing = false;
  bool settings = false, games = false, importing = false, initializing = false;
  bool home = true, import_view = false;
  bool import_recognized = false;
  std::string imported_id;
  int next_action = kFolder;
  int filter = 0;
  std::string query;  // lowercase search text
  // Presentation texts produced by the controller.
  std::string chip_text, activity_title, selection_title, status, primary_label, details, activity_text, result_title;
  Tone chip_tone = Tone::Neutral;
  std::vector<std::string> stage_names;
  Activity activity_kind = Activity::None;
  Clock::time_point started{};
  uintmax_t extracted_bytes = 0;
  int result = 0;  // 0 none, 1 success, 2 warning, 3 error
  int last_action = 0; std::filesystem::path last_source;
  std::string preparing_id, operation_target;
  // Native Enhanced custom limits as typed; applied by kTimingApply.
  std::string fps_text, present_text, sharpness_text;
  std::optional<Prompt> prompt;
  std::optional<PickRequest> pick;
  // Frontend services; tests leave them empty.
  std::function<void(const std::filesystem::path&)> open_path;
  std::function<void()> wake;     // a worker posted an event
  std::function<void()> quit;     // the application may close now
  bool preview_empty = false;
  std::filesystem::path auto_import;  // review aid: import this path once the first verification finishes
  // Self-update: the release check runs beside the main worker; installing uses the worker.
  std::thread update_check;
  std::atomic<bool> closing{false};
  std::optional<AvailableUpdate> update;
  UpdateChannel update_channel = UpdateChannel::Stable;
  // Listing hints run after the list is shown; results apply only to the
  // snapshot they were computed for, and never while an operation runs.
  std::thread hint_check;
  unsigned hint_generation = 0;
  struct Hints { unsigned generation = 0; std::vector<std::pair<size_t, ReceiptHint>> items; };
  // Worker → controller events, run by Pump on the frontend thread.
  std::mutex events_mutex;
  std::deque<std::function<void(App&)>> events;
  ~App() {
    closing = true;
    if (hint_check.joinable()) hint_check.join();
    if (update_check.joinable()) update_check.join();
    if (worker.joinable()) worker.join();
  }
};
}  // namespace x360n::desktop
