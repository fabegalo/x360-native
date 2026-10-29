#pragma once
#include <chrono>
#include <functional>
#include <map>
#include <vector>

#include "paths.h"
namespace x360n {
struct AotOptions {
  ProjectPaths paths;
  std::string title;
  std::filesystem::path build, sdk, cmake, ninja, cc, cxx;
  std::map<std::string, std::string> variables;
  std::chrono::seconds timeout{1800};
  bool keep_incomplete = false;
  bool shared_sdk = true;
  std::function<bool()> cancelled;
  std::function<void(const std::string&)> progress;
};
struct AotResult {
  bool reused = false;
  unsigned commands = 0;
  std::filesystem::path executable, receipt;
};
AotResult PrepareAot(AotOptions options);
int GuardNinja(const std::filesystem::path& configuration,
               const std::vector<std::string>& arguments,
               std::function<bool()> cancelled = {});
}  // namespace x360n
