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
  return std::filesystem::temp_directory_path() /
         ("x360n_" + name + "_" + std::to_string(X360N_GETPID()));
}
