#pragma once
#include <chrono>
#include <cstdint>
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
#include <psapi.h>
#endif

namespace x360n {
// Opt-in, process-local files avoid contention between launcher and supervisor.
// Set X360N_ACTION_TRACE to an existing local directory before starting them.
class ActionTrace {
  struct Resources {
    uint64_t cpu = 0, read_bytes = 0, reads = 0, write_bytes = 0, writes = 0, peak_commit = 0;
    bool valid = false;
  };
  static const std::filesystem::path& Directory() {
    static const std::filesystem::path directory = [] {
#ifdef _WIN32
      wchar_t* p = nullptr; size_t count = 0; _wdupenv_s(&p, &count, L"X360N_ACTION_TRACE");
      std::filesystem::path value = p ? p : L""; std::free(p); return value;
#else
      const auto* p = std::getenv("X360N_ACTION_TRACE"); return p ? std::filesystem::path(p) : std::filesystem::path();
#endif
    }();
    return directory;
  }
  static Resources Observe() noexcept {
    Resources r;
    try {
      if (Directory().empty()) return r;
#ifdef _WIN32
      FILETIME creation{}, exit{}, kernel{}, user{}; IO_COUNTERS io{};
      PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
      const auto process = GetCurrentProcess();
      if (!GetProcessTimes(process, &creation, &exit, &kernel, &user) || !GetProcessIoCounters(process, &io) ||
          !K32GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) return r;
      const auto ticks = [](FILETIME t) { return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
      r = {ticks(kernel) + ticks(user), io.ReadTransferCount, io.ReadOperationCount,
           io.WriteTransferCount, io.WriteOperationCount, memory.PeakPagefileUsage, true};
#endif
    } catch (...) {}
    return r;
  }
 public:
  explicit ActionTrace(std::string name) : name_(std::move(name)) {}
  ~ActionTrace() { Event(name_, start_, &resources_); }
  static bool Enabled() noexcept { try { return !Directory().empty(); } catch (...) { return false; } }
  static void Event(const std::string& name,
                    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now(),
                    const Resources* baseline = nullptr) noexcept {
    try {
    const auto& directory = Directory();
    if (directory.empty()) return;
    const auto now = std::chrono::steady_clock::now();
    const auto resources = baseline && baseline->valid ? Observe() : Resources{};
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
    // Separate file keeps the original four-column timing contract intact.
    // Process-local deltas include concurrent UI threads, not child processes.
    // Nested scopes overlap; peak commit is the process lifetime high-water mark.
    if (baseline && resources.valid) {
      std::ofstream usage(directory / ("resources-" + std::to_string(pid) + ".tsv"), std::ios::app);
      usage << std::fixed << std::setprecision(3)
            << std::chrono::duration<double, std::milli>(now.time_since_epoch()).count()
            << '\t' << tid << '\t' << name << '\t' << std::chrono::duration<double, std::milli>(now - start).count()
            << '\t' << (resources.cpu - baseline->cpu) / 10 << '\t' << resources.read_bytes - baseline->read_bytes
            << '\t' << resources.reads - baseline->reads << '\t' << resources.write_bytes - baseline->write_bytes
            << '\t' << resources.writes - baseline->writes << '\t' << resources.peak_commit << '\n';
    }
    } catch (...) { /* Optional diagnostics must not change operation outcomes. */ }
  }
 private:
  std::string name_;
  std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
  Resources resources_ = Observe();
};
}
