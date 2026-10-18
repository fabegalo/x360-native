// Child process execution with captured output, timeout and reliable exit
// status. On Windows the child runs in a Job Object with KILL_ON_JOB_CLOSE, so
// it (and anything it spawns) dies with the supervisor or on timeout.
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace x360n {

struct ProcessSpec {
  std::filesystem::path exe;
  std::vector<std::string> args;  // UTF-8
  std::filesystem::path working_dir;
  std::filesystem::path stdout_file;
  std::filesystem::path stderr_file;
  std::chrono::milliseconds timeout{0};  // 0 = wait forever
  // Run the child under the Windows debug API and, on an exception it does not
  // handle (second chance), record its stack with DbgHelp before it dies.
  // First-chance exceptions are passed back untouched: the ReXGlue runtime uses
  // them itself (MMIO access handlers), so only an unhandled one is a crash.
  // Slower than a plain run; meant for diagnosing a crash.
  bool capture_crash = false;
  std::function<bool()> should_cancel;
  bool no_console = false;  // Desktop preparation; does not hide a title's GUI.
  // Borrowed manual-reset Windows event; signaled only by this session's close.
  uintptr_t cancel_event = 0;
  std::function<void()> on_started;
  std::function<void()> on_tick;  // worker-only, bounded progress/log sampling
  uintptr_t notification_event = 0;  // borrowed auto-reset lifecycle event
  std::function<void()> on_notification;
  bool collect_metrics = false;  // Opt-in Job Object accounting, including descendants.
};

struct CrashInfo {
  bool captured = false;
  uint32_t code = 0;          // NTSTATUS exception code
  uint64_t address = 0;       // where it was raised
  std::string access;         // access violations: "read 0x...", "write 0x...", "execute 0x..."
  uint32_t thread_id = 0;
  std::vector<std::string> frames;  // "module!symbol+0xoff (file:line)", innermost first
};

struct ProcessResult {
  bool launched = false;
  std::string launch_error;
  bool timed_out = false;
  bool cancelled = false;
  uint32_t exit_code = 0;  // raw; NTSTATUS values on crashes
  std::chrono::milliseconds duration{0};
  CrashInfo crash;  // only with ProcessSpec::capture_crash
  struct Metrics {
    uint64_t cpu_us = 0, read_bytes = 0, read_operations = 0, write_bytes = 0, peak_job_commit_bytes = 0;
  };
  std::optional<Metrics> metrics;
};

ProcessResult RunProcess(const ProcessSpec& spec);

// Quotes arguments following the CommandLineToArgvW rules.
std::wstring BuildCommandLine(const std::filesystem::path& exe, const std::vector<std::string>& args);

// Human-readable meaning of an exit code (NTSTATUS names for common crashes).
std::string DescribeExitCode(uint32_t code);

}  // namespace x360n
