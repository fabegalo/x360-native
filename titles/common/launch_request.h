#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace x360n {
// Host-only handoff. Paths identify data, never commands to execute.
struct LaunchRequest {
  enum Kind : uint32_t {
    Image = 1, WindowClose = 2, GuestExit = 3,
    Dashboard = 4, TitleTerminate = 5, FirmwareExit = 6
  };
  Kind kind = Image;
  uint32_t flags = 0;
  bool data_present = false;
  std::string path;
  std::vector<uint8_t> data;
};
inline void WriteU32(std::ostream& out, uint32_t n) {
  for (unsigned i = 0; i < 4; ++i) out.put(char(n >> (8 * i)));
}
inline uint32_t ReadU32(std::istream& in) {
  uint32_t n = 0;
  for (unsigned i = 0; i < 4; ++i) n |= uint32_t(uint8_t(in.get())) << (8 * i);
  return n;
}
inline bool ReadLaunchRequest(const std::filesystem::path& file, LaunchRequest& out) {
  std::ifstream in(file, std::ios::binary);
  char magic[8]{};
  in.read(magic, 8);
  if (std::string(magic, 8) != "X360NH01") return false;
  LaunchRequest r;
  const auto kind = ReadU32(in);
  r.flags = ReadU32(in);
  const auto present = ReadU32(in), path_size = ReadU32(in), data_size = ReadU32(in);
  if (!in || kind < 1 || kind > 6 || present > 1 || path_size > 32768 || data_size > 65536)
    return false;
  r.kind = LaunchRequest::Kind(kind);
  r.data_present = present != 0;
  r.path.resize(path_size); r.data.resize(data_size);
  in.read(r.path.data(), path_size);
  in.read(reinterpret_cast<char*>(r.data.data()), data_size);
  if (!in || in.peek() != EOF || r.path.find('\0') != std::string::npos ||
      (!r.data_present && data_size) || (r.kind == LaunchRequest::Image && r.path.empty()) ||
      (r.kind != LaunchRequest::Image && !r.path.empty()) ||
      // Any HAL routine closes the platform; flags carries it for the log.
      (r.kind == LaunchRequest::FirmwareExit && (r.data_present || !r.data.empty())))
    return false;
  out = std::move(r);
  return true;
}
inline bool WriteLaunchRequest(const std::filesystem::path& file, const LaunchRequest& r) {
  if (r.path.size() > 32768 || r.data.size() > 65536) return false;
  auto temp = file; temp += ".tmp";
  std::ofstream out(temp, std::ios::binary | std::ios::trunc);
  out.write("X360NH01", 8);
  WriteU32(out, r.kind); WriteU32(out, r.flags); WriteU32(out, r.data_present);
  WriteU32(out, uint32_t(r.path.size())); WriteU32(out, uint32_t(r.data.size()));
  out.write(r.path.data(), r.path.size());
  out.write(reinterpret_cast<const char*>(r.data.data()), r.data.size());
  out.close();
  if (!out) return false;
  std::error_code ec;
  std::filesystem::rename(temp, file, ec);
  return !ec;
}
}  // namespace x360n
