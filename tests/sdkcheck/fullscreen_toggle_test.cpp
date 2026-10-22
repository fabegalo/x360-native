#include <catch2/catch_test_macros.hpp>
#include <windows.h>
#include <chrono>
#include <rex/ui/keybinds.h>
#include <rex/ui/ui_event.h>
#include "session_window.h"

namespace {
bool Key(rex::ui::VirtualKey key, bool alt, bool repeat = false) {
  rex::ui::KeyEvent e(nullptr, key, 1, repeat, false, false, alt, false);
  return rex::ui::IsFullscreenToggleKey(e);
}
bool Popup(HWND window) { return (GetWindowLongPtrW(window, GWL_STYLE) & WS_POPUP) != 0; }
bool WaitPopup(HWND window, bool expected) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (Popup(window) != expected && std::chrono::steady_clock::now() < end) Sleep(5);
  return Popup(window) == expected;
}
}

TEST_CASE("Alt+Enter and F11 are the fullscreen toggle keys", "[sdk][fullscreen-toggle]") {
  using rex::ui::VirtualKey;
  CHECK(Key(VirtualKey::kReturn, true));
  CHECK(Key(VirtualKey::kF11, false));
  CHECK(Key(VirtualKey::kReturn, true, true));  // repeats are consumed too
  CHECK_FALSE(Key(VirtualKey::kReturn, false)); // plain Enter stays a game key
  CHECK_FALSE(Key(VirtualKey::kF10, false));
}

TEST_CASE("session frame toggles fullscreen from the title message and its own keys",
          "[sdk][fullscreen-toggle]") {
  x360n::SessionWindow host(false);
  const auto window = reinterpret_cast<HWND>(host.handle());
  REQUIRE_FALSE(Popup(window));
  RECT windowed{}; GetWindowRect(window, &windowed);
  // The embedded title posts WM_APP + 0x362 on Alt+Enter/F11.
  PostMessageW(window, WM_APP + 0x362, 0, 0);
  REQUIRE(WaitPopup(window, true));
  MONITORINFO monitor{sizeof(monitor)};
  REQUIRE(GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor));
  RECT full{}; GetWindowRect(window, &full);
  CHECK(full.left == monitor.rcMonitor.left); CHECK(full.top == monitor.rcMonitor.top);
  CHECK(full.right == monitor.rcMonitor.right); CHECK(full.bottom == monitor.rcMonitor.bottom);
  // Between titles the frame has focus: F11 toggles back, a repeat does not.
  PostMessageW(window, WM_KEYDOWN, VK_F11, 1 | (1 << 30));
  CHECK(WaitPopup(window, true));
  PostMessageW(window, WM_KEYDOWN, VK_F11, 1);
  REQUIRE(WaitPopup(window, false));
  RECT restored{}; GetWindowRect(window, &restored);
  CHECK(restored.right - restored.left == windowed.right - windowed.left);
  CHECK(restored.bottom - restored.top == windowed.bottom - windowed.top);
  PostMessageW(window, WM_SYSKEYDOWN, VK_RETURN, 1 | (1 << 29));
  CHECK(WaitPopup(window, true));
  CHECK_FALSE(host.closed());
}
