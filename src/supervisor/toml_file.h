// toml::parse_file through x360n::io: the same table and the same parse_error
// for a missing or unreadable file, from any installation folder (parse_file
// opens the ordinary name, which Windows limits to MAX_PATH).
#pragma once

#include <memory>
#include <string>

#include <toml++/toml.hpp>

#include "fs_io.h"

namespace x360n {
inline toml::table ParseTomlFile(const std::filesystem::path& file) {
  const auto name = file.u8string();
  const auto label = std::make_shared<const std::string>(name.begin(), name.end());
  io::ifstream in(file, std::ios::binary);
  if (!in) throw toml::parse_error("File could not be opened for reading", toml::source_position{}, label);
  return toml::parse(in, *label);
}
}  // namespace x360n
