// Child process execution with captured output, timeout and reliable exit
// status. On Windows the child runs in a Job Object with KILL_ON_JOB_CLOSE, so
// it (and anything it spawns) dies with the supervisor or on timeout.
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
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
};

struct ProcessResult {
  bool launched = false;
  std::string launch_error;
  bool timed_out = false;
  uint32_t exit_code = 0;  // raw; NTSTATUS values on crashes
  std::chrono::milliseconds duration{0};
};

ProcessResult RunProcess(const ProcessSpec& spec);

// Quotes arguments following the CommandLineToArgvW rules.
std::wstring BuildCommandLine(const std::filesystem::path& exe, const std::vector<std::string>& args);

// Human-readable meaning of an exit code (NTSTATUS names for common crashes).
std::string DescribeExitCode(uint32_t code);

}  // namespace x360n
