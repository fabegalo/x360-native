// x360-native launcher: one SDL3 + Dear ImGui frontend for Windows, Linux and macOS.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include "frontend.h"
#include "renderer.h"
#include "app_text.h"
#include "handheld.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#endif

namespace fs = std::filesystem;
using namespace x360n;
using namespace x360n::desktop;
namespace {
Uint32 g_wake = 0, g_picked = 0;
struct PickResult { int command; std::string path; };
void FileDialogDone(void* userdata, const char* const* files, int) {
  auto* result = new PickResult{int(reinterpret_cast<intptr_t>(userdata)), files && files[0] ? files[0] : ""};
  SDL_Event event{}; event.type = g_picked; event.user.data1 = result;
  if (!SDL_PushEvent(&event)) delete result;
}
float UiScale(SDL_Window* window) {
  const float density = SDL_GetWindowPixelDensity(window), display = SDL_GetWindowDisplayScale(window);
  return density > 0 && display > 0 ? display / density : 1.0f;
}
// Review aid (development builds only): selects one catalog title and runs the
// launcher's own commands — prepare when needed, then start the session — while
// logging what the user would see. It issues commands to the controller; it
// never injects input into this or any other window.
struct ReviewFlow {
  std::string target;
  std::ofstream log;
  int stage = 0;  // 0 wait for listing, 1 preparing, 2 session, 3 ended, 4 done
  std::string last_text;
  bool was_session = false, was_finishing = false, prepared = false;
  Uint64 ended_at = 0;
  void Write(const App& a, const std::string& event) {
    if (!log) return;
    log << SDL_GetTicks() << "\t" << event << "\tbusy=" << a.busy << " stage=" << a.stage.load() << " result=" << a.result
        << " chip=" << a.chip_text << " next=" << a.next_action << "\n" << std::flush;
  }
  bool import_watch = false;  // --import with --auto-log: log the import, then close
  bool import_started = false;
  void Step(App& a, SDL_Window* window, bool& running) {
    if ((target.empty() && !import_watch) || stage == 4) return;
    if (a.busy && a.activity_text != last_text) { last_text = a.activity_text; Write(a, "activity: " + a.activity_title + " | " + last_text); }
    if (import_watch) {
      if (a.busy && a.importing) import_started = true;
      if (import_started && !a.busy) {
        Write(a, "import ended: " + a.result_title + " | " + a.activity_text + " | selected=" + a.selection_title + " status=" + a.status);
        stage = 4; running = false;
      }
      return;
    }
    if (a.prompt) {
      // Downloads stay refused; only files already present are used.
      Write(a, "prompt: " + a.prompt->title + " -> " + (a.prompt->buttons.size() > 1 ? a.prompt->buttons[1] : a.prompt->buttons[0]));
      Answer(a, a.prompt->buttons.size() > 1 ? 1 : 0);
    }
    switch (stage) {
      case 0: {
        if (a.busy || !a.catalog_loaded) return;
        const auto it = std::find_if(a.targets.begin(), a.targets.end(), [&](const auto& t) { return t.id == target; });
        if (it == a.targets.end()) { Write(a, "target missing: " + target); stage = 4; running = false; return; }
        Command(a, kDashboards);
        if (a.busy) return;
        a.selected = size_t(it - a.targets.begin()); InspectSelected(a);
        Write(a, "selected " + target + " primary=" + a.primary_label + " status=" + a.status);
        if (a.next_action == kLaunch) { Command(a, kLaunch); stage = 2; }
        else if (a.next_action == kPrepare) { Command(a, kPrepare); stage = 1; }
        else { Write(a, "no action available"); stage = 4; running = false; }
        Write(a, "command issued");
        return;
      }
      case 1:
        if (a.busy || a.prompt) return;
        Write(a, "preparation ended: " + a.result_title + " | " + a.activity_text);
        if (a.result != 1) { stage = 4; running = false; return; }
        InspectSelected(a);
        if (a.next_action != kLaunch) { Write(a, "not launchable after preparation: " + a.status); stage = 4; running = false; return; }
        Command(a, kLaunch); stage = 2; Write(a, "launch issued");
        return;
      case 2:
        if (a.session && !was_session) Write(a, "session running");
        if (a.finishing_session && !was_finishing) Write(a, "platform window closed");
        was_session = a.session; was_finishing = a.finishing_session;
        if (a.busy) return;
        Write(a, "session ended: " + a.result_title + " | " + a.activity_text);
        // A refused start that asks for preparation follows the launcher's own next action once.
        if (a.result == 3 && !prepared && a.next_action == kPrepare) {
          prepared = true; Write(a, "prepare issued: " + a.primary_label);
          Command(a, kPrepare); stage = 1; return;
        }
        stage = 3; ended_at = SDL_GetTicks();
        return;
      case 3:
        if (SDL_GetTicks() - ended_at < 3000) return;
        Write(a, std::string("launcher focus after session: ") + ((SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) ? "input-focus" : "no-focus"));
        stage = 4; running = false;
        return;
    }
  }
};
}  // namespace

int main(int argc, char** argv) {
  App a;
  a.self = SelfExecutable();
  std::string forced_renderer;
  fs::path smoke;  // review aid: report the frontend after real frames, then close
  ReviewFlow review;
  int frames = 0;
  for (int i = 1; i < argc; ++i) {
    const std::string option = argv[i];
    if (option == "--root" && i + 1 < argc) a.root = PathFromUtf8(argv[++i]);
#ifndef X360N_DISTRIBUTION
    // Local review aids; no effect on data.
    else if (option == "--lang" && i + 1 < argc) english = std::string(argv[++i]) == "en";
    else if (option == "--import" && i + 1 < argc) a.auto_import = PathFromUtf8(argv[++i]);
    else if (option == "--smoke" && i + 1 < argc) smoke = PathFromUtf8(argv[++i]);
    else if (option == "--auto-start" && i + 1 < argc) review.target = argv[++i];
    else if (option == "--auto-log" && i + 1 < argc) review.log.open(PathFromUtf8(argv[++i]), std::ios::app);
    else if (option == "--session-user-data" && i + 1 < argc) a.session_user_data = PathFromUtf8(argv[++i]);
#endif
    else if (option == "--renderer" && i + 1 < argc) forced_renderer = argv[++i];
  }
  if (const char* env = std::getenv("X360N_LAUNCHER_RENDERER"); env && forced_renderer.empty()) forced_renderer = env;
  ConfigureFromPreferences(a);
  review.import_watch = review.log.is_open() && !a.auto_import.empty();
#ifdef _WIN32
  SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");
#endif
  SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI, "1");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "x360-native", SDL_GetError(), nullptr);
    return 1;
  }
  g_wake = SDL_RegisterEvents(2); g_picked = g_wake + 1;
  const float content = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  const float start_scale = content > 0 ? content : 1.0f;
  // SteamOS Gaming Mode (Steam Deck LCD/OLED or any gamescope session) shows one
  // fullscreen app; Desktop Mode and other systems keep the normal window.
  const auto handheld = DetectHandheld();
  if (const auto described = DescribeHandheld(handheld); !described.empty())
    SDL_Log("x360-native handheld session: %s", described.c_str());
  SDL_Window* window = SDL_CreateWindow("x360-native", int(1220 * start_scale), int(800 * start_scale),
                                        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN |
                                            (LauncherStartsFullscreen(handheld) ? SDL_WINDOW_FULLSCREEN : 0));
  if (!window) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "x360-native", SDL_GetError(), nullptr);
    SDL_Quit(); return 1;
  }
#ifdef _WIN32
  if (auto* hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr))) {
    BOOL dark = TRUE; DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
  }
#endif
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  auto& io = ImGui::GetIO();
  io.IniFilename = nullptr; io.LogFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
  ui::LoadFonts(io);
  float scale = UiScale(window);
  ui::ApplyStyle(scale);
  auto apply_min_size = [&] {
    const float density = SDL_GetWindowPixelDensity(window);
    (void)density;
    SDL_SetWindowMinimumSize(window, int(1000 * scale), int(700 * scale));
  };
  apply_min_size();
  // GPU first; the CPU rasterizer keeps the launcher usable without a GPU device.
  std::string gpu_error, soft_error;
  std::unique_ptr<ui::Presenter> presenter;
  if (forced_renderer != "software") {
    ImGui_ImplSDL3_InitForSDLGPU(window);
    presenter = ui::CreateGpuPresenter(window, gpu_error);
    if (!presenter) ImGui_ImplSDL3_Shutdown();
  }
  if (!presenter) {
    ImGui_ImplSDL3_InitForOther(window);
    presenter = ui::CreateSoftwarePresenter(window, soft_error);
  }
  if (!presenter) {
    const auto message = "No usable renderer for the launcher window.\nGPU: " + gpu_error + "\nSoftware: " + soft_error;
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "x360-native", message.c_str(), window);
    ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext(); SDL_DestroyWindow(window); SDL_Quit(); return 1;
  }
  SDL_Log("x360-native launcher renderer: %s%s%s", presenter->Name().c_str(), gpu_error.empty() ? "" : " (GPU unavailable: ", gpu_error.empty() ? "" : (gpu_error + ")").c_str());
  ui::Ui ui(a);
  ui.upload = [&](ImTextureData* texture) { presenter->Upload(texture); };
  bool running = true;
  a.wake = [] { SDL_Event event{}; event.type = g_wake; SDL_PushEvent(&event); };
  a.quit = [&] { running = false; };
  a.open_path = [](const fs::path& path) { OpenPath(path); };
  SDL_ShowWindow(window);
  // An interrupted rename may have removed app/workspace, so this cannot
  // depend on successful payload discovery or initialized preferences.
  try { ConfirmUpdates(a.self.parent_path(), X360N_VERSION); }
  catch (const std::exception& error) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "x360-native", error.what(), window);
    presenter.reset(); ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext(); SDL_DestroyWindow(window); SDL_Quit(); return 1;
  }
  Open(a);
  bool pick_open = false, launch_running = false, gamepad = true;
  int drops = 0; fs::path dropped;
  int settle = 3;  // frames to draw after the last event so ImGui settles
  while (running) {
    SDL_Event event;
    const bool animate = a.busy || settle > 0 || !smoke.empty() || ((!review.target.empty() || review.import_watch) && review.stage != 4);
    const bool got = animate ? SDL_WaitEventTimeout(&event, a.busy ? 33 : 0) : SDL_WaitEvent(&event);
    for (bool more = got; more; more = SDL_PollEvent(&event)) {
      settle = 3;
      if (event.type == g_picked) {
        std::unique_ptr<PickResult> result(static_cast<PickResult*>(event.user.data1));
        pick_open = false;
        Picked(a, result->command, PathFromUtf8(result->path));
        continue;
      }
      if (event.type == g_wake) continue;
      ImGui_ImplSDL3_ProcessEvent(&event);
      switch (event.type) {
        case SDL_EVENT_QUIT: case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
          if (RequestClose(a)) running = false;
          break;
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
          if (const float now = UiScale(window); now != scale) { scale = now; ui::ApplyStyle(scale); apply_min_size(); }
          break;
        case SDL_EVENT_DROP_BEGIN: drops = 0; dropped.clear(); break;
        case SDL_EVENT_DROP_FILE: ++drops; if (event.drop.data) dropped = PathFromUtf8(event.drop.data); break;
        case SDL_EVENT_DROP_COMPLETE: if (drops == 1) Drop(a, dropped); drops = 0; break;
        default: break;
      }
    }
    if (settle > 0) --settle;
    Pump(a);
    review.Step(a, window, running);
    if (a.pick && !pick_open) {
      pick_open = true;
      const auto request = *a.pick;
      SDL_PropertiesID props = SDL_CreateProperties();
      SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, window);
      SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, request.title.c_str());
      SDL_ShowFileDialogWithProperties(request.folder ? SDL_FILEDIALOG_OPENFOLDER : SDL_FILEDIALOG_OPENFILE, FileDialogDone,
                                       reinterpret_cast<void*>(intptr_t(request.command)), props);
      SDL_DestroyProperties(props);
    }
    // The game owns the controller during a session; the launcher reads it only when focused and idle.
    const bool focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    if (const bool pad = focused && !a.busy; pad != gamepad) {
      gamepad = pad;
      ImGui_ImplSDL3_SetGamepadMode(pad ? ImGui_ImplSDL3_GamepadMode_AutoFirst : ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);
    }
    presenter->NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    ui::DrawFrame(ui);
    ImGui::Render();
    presenter->Render(ImGui::GetDrawData());
    ui::RunDeferred(ui);
    // A finished session returns the focus to the launcher.
    const bool launching = a.busy && a.last_action == kLaunch;
    if (launch_running && !launching) SDL_RaiseWindow(window);
    launch_running = launching;
    if (!smoke.empty() && a.busy) frames = 0;
    if (!smoke.empty() && ++frames == 60) {
      int w = 0, h = 0; SDL_GetWindowSizeInPixels(window, &w, &h);
      std::ofstream(smoke) << "renderer=" << presenter->Name() << "\nwindow=" << w << "x" << h << "\nscale=" << scale
                           << "\npage=" << int(PageOf(a)) << "\nroot=" << Utf8(a.root) << "\nchip=" << a.chip_text
                           << "\nprimary=" << a.primary_label << "\nresult=" << a.result << "\n";
      running = false;
    }
  }
  a.closing = true;
  presenter.reset();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
