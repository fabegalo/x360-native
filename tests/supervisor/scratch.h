// Per-process scratch directory names. ctest -j runs each test case in its own
// process, so a fixed temp path makes parallel cases delete each other's files.
#pragma once

#include <filesystem>
#include <string>

#ifdef _WIN32
#include <process.h>
#define X360N_GETPID _getpid
#else
#include <unistd.h>
#define X360N_GETPID getpid
#endif

inline std::filesystem::path UniqueTempPath(const std::string& name) {
#ifdef __APPLE__
  // macOS TMPDIR is reached through the /var -> /private/var system link, which
  // the link-refusing code under test rejects; use its canonical spelling.
  const auto base = std::filesystem::canonical(std::filesystem::temp_directory_path());
#else
  const auto base = std::filesystem::temp_directory_path();
#endif
  return base / ("x360n_" + name + "_" + std::to_string(X360N_GETPID()));
}
