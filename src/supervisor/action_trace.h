#pragma once
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <iomanip>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace x360n {
// Opt-in, process-local files avoid contention between launcher and supervisor.
// Set X360N_ACTION_TRACE to an existing local directory before starting them.
class ActionTrace {
 public:
  explicit ActionTrace(std::string name) : name_(std::move(name)) {}
  ~ActionTrace() { Event(name_, start_); }
  static void Event(const std::string& name,
                    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now()) noexcept {
    try {
    static const std::filesystem::path directory = [] {
#ifdef _WIN32
      wchar_t* p = nullptr; size_t count = 0; _wdupenv_s(&p, &count, L"X360N_ACTION_TRACE");
      std::filesystem::path value = p ? p : L""; std::free(p); return value;
#else
      const auto* p = std::getenv("X360N_ACTION_TRACE"); return p ? std::filesystem::path(p) : std::filesystem::path();
#endif
    }();
    if (directory.empty()) return;
    const auto now = std::chrono::steady_clock::now();
    static std::mutex mutex;
    std::lock_guard lock(mutex);
#ifdef _WIN32
    const auto pid = GetCurrentProcessId(), tid = GetCurrentThreadId();
#else
    const unsigned pid = 0, tid = 0;
#endif
    std::ofstream out(directory / ("actions-" + std::to_string(pid) + ".tsv"), std::ios::app);
    out << std::fixed << std::setprecision(3) << std::chrono::duration<double, std::milli>(now.time_since_epoch()).count()
        << '\t' << tid << '\t' << name << '\t'
        << std::chrono::duration<double, std::milli>(now - start).count() << '\n';
    } catch (...) { /* Optional diagnostics must not change operation outcomes. */ }
  }
 private:
  std::string name_;
  std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};
}
