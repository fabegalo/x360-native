#include "session_window.h"
#include <future>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace x360n {
namespace {
constexpr UINT kAttachTitle = WM_APP + 0x360;
struct FrameState {
  std::atomic<bool>* closed;
  std::atomic<unsigned>* attached_count;
  HWND child = nullptr;
};
void SizeChild(HWND window, HWND child) {
  if (!IsWindow(child) || GetParent(child) != window) return;
  RECT rect{}; GetClientRect(window, &rect);
  SetWindowPos(child, nullptr, 0, 0, rect.right, rect.bottom,
               SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
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
        state->child = child; SizeChild(window, child); SetFocus(child);
        state->attached_count->fetch_add(1);
      }
      return 0;
    }
    if (message == WM_SIZE) { SizeChild(window, state->child); return 0; }
    if (message == WM_SETFOCUS && IsWindow(state->child)) { SetFocus(state->child); return 0; }
    if (message == WM_CLOSE) {
      state->closed->store(true);
      if (IsWindow(state->child)) PostMessageW(state->child, WM_CLOSE, 0, 0);
      DestroyWindow(window); return 0;
    }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
  }
  return DefWindowProcW(window, message, wp, lp);
}
}
SessionWindow::SessionWindow(bool visible) {
  std::promise<uintptr_t> ready; auto result = ready.get_future();
  thread_ = std::thread([this, visible, ready = std::move(ready)]() mutable {
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.lpfnWndProc = FrameProc; wc.hInstance = instance;
    wc.lpszClassName = L"X360NativeSession"; wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassW(&wc);
    FrameState state{&closed_, &attached_count_};
    RECT rect{0, 0, 1280, 720}; AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    auto window = CreateWindowExW(0, wc.lpszClassName, L"x360-native",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, &state);
    ready.set_value(reinterpret_cast<uintptr_t>(window));
    if (!window) return;
    if (visible) ShowWindow(window, SW_SHOW);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
      TranslateMessage(&message); DispatchMessageW(&message);
    }
  });
  handle_ = result.get();
  if (!handle_) { thread_.join(); throw std::runtime_error("could not create session window"); }
}
SessionWindow::~SessionWindow() {
  if (!closed()) PostMessageW(reinterpret_cast<HWND>(handle_), WM_CLOSE, 0, 0);
  if (thread_.joinable()) thread_.join();
}
}
#else
namespace x360n {
SessionWindow::SessionWindow(bool) { throw std::runtime_error("session window requires Windows"); }
SessionWindow::~SessionWindow() = default;
}
#endif
