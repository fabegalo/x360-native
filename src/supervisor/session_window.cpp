#include "session_window.h"
#include <future>
#include <stdexcept>
#include "action_trace.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace x360n {
namespace {
constexpr UINT kAttachTitle = WM_APP + 0x360;
constexpr UINT kSyncTitleFocus = WM_APP + 0x361;
struct FrameState {
  std::atomic<bool>* closed;
  std::atomic<unsigned>* attached_count;
  HANDLE close_event;
  HANDLE closed_notification;
  HWND child = nullptr;
};
void SizeChild(HWND window, HWND child) {
  if (!IsWindow(child) || GetParent(child) != window) return;
  RECT rect{}; GetClientRect(window, &rect);
  SetWindowPos(child, nullptr, 0, 0, rect.right, rect.bottom,
               SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
}
void SyncTitleFocus(HWND window, HWND child) {
  if (!IsWindow(child) || GetParent(child) != window) return;
  const bool active = GetForegroundWindow() == window && !IsIconic(window);
  if (active && GetFocus() != child) SetFocus(child);
  // Activation belongs to the top-level frame, while SDL owns the child.
  // The child may already have native queue focus from background startup,
  // so SetFocus alone produces no new WM_SETFOCUS. Let SDL reconcile against
  // actual native focus and the foreground root using its existing handler.
  PostMessageW(child, WM_ACTIVATE, active ? WA_ACTIVE : WA_INACTIVE, 0);
}
LRESULT CALLBACK FrameProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
  auto* state = reinterpret_cast<FrameState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    state = static_cast<FrameState*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
  }
  if (state) {
    if (message == kAttachTitle) {
      const auto child = reinterpret_cast<HWND>(wp);
      if (IsWindow(child) && GetParent(child) == window) {
        state->child = child; SizeChild(window, child);
        PostMessageW(window, kSyncTitleFocus, 0, 0);
        state->attached_count->fetch_add(1);
        ActionTrace::Event("session.title_window.attached");
      }
      return 0;
    }
    if (message == kSyncTitleFocus) { SyncTitleFocus(window, state->child); return 0; }
    if (message == WM_SIZE) { SizeChild(window, state->child); return 0; }
    if (message == WM_ACTIVATE || message == WM_NCACTIVATE || message == WM_SETFOCUS) {
      // Native activation can precede the foreground change. Reconcile after
      // its messages, including non-client activation, have been dispatched.
      const auto result = DefWindowProcW(window, message, wp, lp);
      PostMessageW(window, kSyncTitleFocus, 0, 0);
      return result;
    }
    if (message == WM_CLOSE) {
      state->closed->store(true);
      SetEvent(state->close_event);
      if (state->closed_notification) SetEvent(state->closed_notification);
      ActionTrace::Event("session.window.closed");
      if (IsWindow(state->child)) PostMessageW(state->child, WM_CLOSE, 0, 0);
      DestroyWindow(window); return 0;
    }
    if (message == WM_DESTROY) { state->closed->store(true); SetEvent(state->close_event); if (state->closed_notification) SetEvent(state->closed_notification); PostQuitMessage(0); return 0; }
  }
  return DefWindowProcW(window, message, wp, lp);
}
}
SessionWindow::SessionWindow(bool visible, uintptr_t closed_notification, bool fullscreen) {
  ActionTrace trace("session.window.create");
  close_event_ = reinterpret_cast<uintptr_t>(CreateEventW(nullptr, TRUE, FALSE, nullptr));
  if (!close_event_) throw std::runtime_error("could not create session close event");
  std::promise<uintptr_t> ready; auto result = ready.get_future();
  thread_ = std::thread([this, visible, closed_notification, fullscreen, ready = std::move(ready)]() mutable {
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.lpfnWndProc = FrameProc; wc.hInstance = instance;
    wc.lpszClassName = L"X360NativeSession"; wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassW(&wc);
    FrameState state{&closed_, &attached_count_, reinterpret_cast<HANDLE>(close_event_), reinterpret_cast<HANDLE>(closed_notification)};
    RECT rect{0, 0, 1280, 720}; AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    auto window = CreateWindowExW(0, wc.lpszClassName, L"x360-native",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, &state);
    if (window && fullscreen) {
      // SDL renders inside this frame. Fullscreen belongs to the frame so it
      // survives every dashboard/title transition without resizing the desktop.
      MONITORINFO monitor{sizeof(monitor)};
      if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) {
        SetWindowLongPtrW(window, GWL_STYLE, WS_POPUP | WS_CLIPCHILDREN);
        const auto& bounds = monitor.rcMonitor;
        SetWindowPos(window, nullptr, bounds.left, bounds.top,
                     bounds.right - bounds.left, bounds.bottom - bounds.top,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
      } else { DestroyWindow(window); window = nullptr; }
    }
    ready.set_value(reinterpret_cast<uintptr_t>(window));
    if (!window) return;
    if (visible) ShowWindow(window, SW_SHOW);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
      TranslateMessage(&message); DispatchMessageW(&message);
    }
  });
  handle_ = result.get();
  if (!handle_) { thread_.join(); CloseHandle(reinterpret_cast<HANDLE>(close_event_)); throw std::runtime_error("could not create session window"); }
}
SessionWindow::~SessionWindow() {
  if (!closed()) PostMessageW(reinterpret_cast<HWND>(handle_), WM_CLOSE, 0, 0);
  if (thread_.joinable()) thread_.join();
  CloseHandle(reinterpret_cast<HANDLE>(close_event_));
}
}
#else
namespace x360n {
SessionWindow::SessionWindow(bool, uintptr_t, bool) { throw std::runtime_error("session window requires Windows"); }
SessionWindow::~SessionWindow() = default;
}
#endif
