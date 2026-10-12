#pragma once
#include <atomic>
#include <cstdint>
#include <thread>

namespace x360n {
// Owns only the persistent native frame. Each title renders its actual swapchain
// in a child window; this class draws no dashboard or game interface.
class SessionWindow {
 public:
  explicit SessionWindow(bool visible = true);
  ~SessionWindow();
  SessionWindow(const SessionWindow&) = delete;
  SessionWindow& operator=(const SessionWindow&) = delete;
  uintptr_t handle() const { return handle_; }
  bool closed() const { return closed_.load(); }
  unsigned attached_count() const { return attached_count_.load(); }
 private:
  uintptr_t handle_ = 0;
  std::atomic<bool> closed_{false};
  std::atomic<unsigned> attached_count_{0};
  std::thread thread_;
};
}
