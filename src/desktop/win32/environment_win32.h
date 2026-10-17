#pragma once
#include <filesystem>
#include <string>
namespace x360n::desktop {
struct Environment {
  bool graphics = false, writable = false, architecture = false, msvc = false;
  uintmax_t available = 0;
  std::string description;
};
Environment CheckEnvironment(const std::filesystem::path& root);
}
