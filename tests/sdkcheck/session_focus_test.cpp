#include <catch2/catch_test_macros.hpp>
#include <SDL3/SDL.h>
#include <windows.h>
#include <chrono>
#include <iostream>
#include "session_window.h"

namespace {
void Pump() {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
  do {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {}
    Sleep(1);
  } while (std::chrono::steady_clock::now() < end);
}
struct Windows {
  x360n::SessionWindow host{false};
  x360n::SessionWindow other{false};
  SDL_Window* title = nullptr;
  SDL_Joystick* pad = nullptr;
  SDL_JoystickID pad_id = 0;
  ~Windows() {
    if (pad) SDL_CloseJoystick(pad);
    if (pad_id) SDL_DetachVirtualJoystick(pad_id);
    if (title) SDL_DestroyWindow(title);
    SDL_Quit();
  }
};
}

TEST_CASE("session activation recovers SDL focus after background title attachment",
          "[.session-focus]") {
  const auto previous_dpi = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  struct RestoreDpi { DPI_AWARENESS_CONTEXT value; ~RestoreDpi() { SetThreadDpiAwarenessContext(value); } } restore{previous_dpi};
  REQUIRE(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK));
  Windows windows;
  SDL_VirtualJoystickDesc desc{};
  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  desc.nbuttons = 1;
  desc.button_mask = 1;
  desc.name = "session focus fixture";
  windows.pad_id = SDL_AttachVirtualJoystick(&desc);
  REQUIRE(windows.pad_id);
  windows.pad = SDL_OpenJoystick(windows.pad_id);
  REQUIRE(windows.pad);
  auto button = [&](bool down) {
    REQUIRE(SDL_SetJoystickVirtualButton(windows.pad, 0, down));
    Pump();
  };
  const auto host = reinterpret_cast<HWND>(windows.host.handle());
  // Another input queue, as when an unrelated desktop application is active.
  const auto other = reinterpret_cast<HWND>(windows.other.handle());
  SetWindowTextW(other, L"x360-native focus test");
  ShowWindow(other, SW_SHOW);
  ShowWindow(host, SW_SHOWNOACTIVATE);
  REQUIRE(SetForegroundWindow(other));
  Pump();

  for (unsigned replacement = 0; replacement < 2; ++replacement) {
    CAPTURE(replacement);
    windows.title = SDL_CreateWindow("synthetic embedded title", 320, 160, SDL_WINDOW_HIDDEN);
    REQUIRE(windows.title);
    const auto child = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(windows.title),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    REQUIRE(child);
    auto style = GetWindowLongPtrW(child, GWL_STYLE);
    SetWindowLongPtrW(child, GWL_STYLE, (style & ~(WS_POPUP | WS_OVERLAPPEDWINDOW)) | WS_CHILD);
    SetParent(child, host);
    REQUIRE(GetParent(child) == host);
    ShowWindow(child, SW_SHOWNOACTIVATE);
    REQUIRE(SetForegroundWindow(other));
    // A background queue can retain native child focus without SDL accepting
    // it, because the root is not foreground. This is the startup race.
    SetFocus(child);
    PostMessageW(host, WM_APP + 0x360, reinterpret_cast<WPARAM>(child), 0);
    Pump();
    CHECK(GetForegroundWindow() == other);
    CHECK(SDL_GetKeyboardFocus() == nullptr);

    for (unsigned cycle = 0; cycle < 3; ++cycle) {
      CAPTURE(cycle);
      REQUIRE(SetForegroundWindow(host));
      Pump();
      std::cout << "replacement=" << replacement << " cycle=" << cycle
                << " native=" << (GetFocus() == child)
                << " sdl=" << (SDL_GetKeyboardFocus() == windows.title) << '\n';
      CHECK(GetFocus() == child);
      CHECK(SDL_GetKeyboardFocus() == windows.title);
      button(false);
      button(true);
      CHECK(SDL_GetJoystickButton(windows.pad, 0));
      button(false);
      REQUIRE(SetForegroundWindow(other));
      Pump();
      CHECK(SDL_GetKeyboardFocus() == nullptr);
      button(true);
      CHECK_FALSE(SDL_GetJoystickButton(windows.pad, 0));
      button(false);
    }
    SDL_DestroyWindow(windows.title);
    windows.title = nullptr;
    Pump();
  }
  CHECK(windows.host.attached_count() == 2);
}
