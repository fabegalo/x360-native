#pragma once
#include "action_trace.h"
#include <atomic>
#include <stdexcept>
#include <string_view>
#include <utility>
#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace x360n {
// One launcher operation, one private event/channel. It reports real frame
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
    if (!create && name_.empty()) return;
    namespace fs = std::filesystem;
    fs::path directory;
    int dir_fd = -1, channel = -1;
    bool owned = false;
    try {
      if (create) {
        auto pattern = (fs::temp_directory_path() / "x360n-session-XXXXXX").string();
        if (!mkdtemp(pattern.data())) throw std::runtime_error("Could not create launcher session notification directory");
        directory = pattern;
        owned = true;
        name_ = (directory / "signal").string();
      } else {
        const fs::path path(name_);
        directory = path.parent_path();
        const auto component = directory.filename().string();
        constexpr std::string_view prefix = "x360n-session-";
        if (!path.is_absolute() || path.filename() != "signal" ||
            directory.parent_path() != fs::temp_directory_path() ||
            !component.starts_with(prefix) || component.size() != prefix.size() + 6 ||
            component.substr(prefix.size()).find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") != std::string::npos)
          throw std::runtime_error("Invalid launcher session notification name");
      }
      // Refuse links and public directories before opening an IPC name from
      // another process. A private directory prevents FIFO replacement by peers.
      dir_fd = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
      struct stat info{};
      if (dir_fd < 0 || fstat(dir_fd, &info) < 0 || info.st_uid != geteuid() || (info.st_mode & 0077))
        throw std::runtime_error("Unsafe launcher session notification directory");
      if (create && mkfifoat(dir_fd, "signal", 0600) < 0)
        throw std::runtime_error("Could not create launcher session notification channel");
      channel = openat(dir_fd, "signal", O_RDWR | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
      if (channel < 0 || fstat(channel, &info) < 0 || !S_ISFIFO(info.st_mode) ||
          info.st_uid != geteuid() || (info.st_mode & 0077) || info.st_nlink != 1)
        throw std::runtime_error("Unsafe launcher session notification channel");
      handle_ = uintptr_t(channel + 1);
      if (create) { directory_fd_ = dir_fd; directory_ = std::move(directory); }
      else close(dir_fd);
    } catch (...) {
      if (channel >= 0) close(channel);
      if (owned && dir_fd >= 0) unlinkat(dir_fd, "signal", 0);
      if (dir_fd >= 0) close(dir_fd);
      if (owned) rmdir(directory.c_str());
      throw;
    }
#endif
  }
  ~SessionNotification() {
#ifdef _WIN32
    if (handle_) CloseHandle(reinterpret_cast<HANDLE>(handle_));
#else
    if (handle_) close(int(handle_ - 1));
    if (directory_fd_ >= 0) {
      unlinkat(directory_fd_, "signal", 0);
      close(directory_fd_);
      rmdir(directory_.c_str());
    }
#endif
  }
  SessionNotification(const SessionNotification&) = delete;
  SessionNotification& operator=(const SessionNotification&) = delete;
  uintptr_t handle() const { return handle_; }
  const std::string& name() const { return name_; }
  bool Signal() const noexcept {
    if (!handle_) return false;
#ifdef _WIN32
    return SetEvent(reinterpret_cast<HANDLE>(handle_)) != 0;
#else
    ssize_t count;
    do { count = write(int(handle_ - 1), "x", 1); } while (count < 0 && errno == EINTR);
    // A full channel already has a pending notification.
    return count == 1 || (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
#endif
  }
 private:
  std::string name_;
  uintptr_t handle_ = 0;
#ifndef _WIN32
  int directory_fd_ = -1;
  std::filesystem::path directory_;
#endif
};
}
