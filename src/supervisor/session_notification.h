#pragma once
#include "action_trace.h"
#include <atomic>
#include <stdexcept>

namespace x360n {
// One launcher operation, one private auto-reset event. It reports real frame
// closure while the supervisor still preserves logs; it never cancels a job.
class SessionNotification {
 public:
  explicit SessionNotification(std::string name = {}, bool create = false) : name_(std::move(name)) {
#ifdef _WIN32
    if (create) {
      static std::atomic<unsigned> serial{0};
      name_ = "Local\\x360n-session-" + std::to_string(GetCurrentProcessId()) + "-" +
          std::to_string(GetTickCount64()) + "-" + std::to_string(serial++);
    }
    if (name_.empty()) return;
    constexpr std::string_view prefix = "Local\\x360n-session-";
    if (!name_.starts_with(prefix) || name_.size() == prefix.size() || name_.substr(prefix.size()).find_first_not_of("0123456789-") != std::string::npos)
      throw std::runtime_error("Invalid launcher session notification name");
    const std::wstring wide(name_.begin(), name_.end());
    handle_ = reinterpret_cast<uintptr_t>(create ? CreateEventW(nullptr, FALSE, FALSE, wide.c_str()) :
        OpenEventW(EVENT_MODIFY_STATE, FALSE, wide.c_str()));
    if (!handle_) throw std::runtime_error("Could not open launcher session notification");
    if (create && GetLastError() == ERROR_ALREADY_EXISTS) {
      CloseHandle(reinterpret_cast<HANDLE>(handle_)); handle_ = 0;
      throw std::runtime_error("Launcher session notification collision");
    }
#else
    if (create || !name_.empty()) throw std::runtime_error("Session notifications require Windows");
#endif
  }
  ~SessionNotification() {
#ifdef _WIN32
    if (handle_) CloseHandle(reinterpret_cast<HANDLE>(handle_));
#endif
  }
  SessionNotification(const SessionNotification&) = delete;
  SessionNotification& operator=(const SessionNotification&) = delete;
  uintptr_t handle() const { return handle_; }
  const std::string& name() const { return name_; }
 private:
  std::string name_;
  uintptr_t handle_ = 0;
};
}
