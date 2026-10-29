#pragma once
#include <filesystem>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>

namespace x360n::content {
// XContent header offsets, shared by recognition and extraction. This is only
// format recognition: the extracted 17559 anchor and every asset need SHA-256.
inline bool IsSystemUpdatePackage(const std::filesystem::path& path) {
  unsigned char h[0x364]{};
  std::ifstream in(path, std::ios::binary);
  in.read(reinterpret_cast<char*>(h), sizeof(h));
  auto be = [&](size_t p) { return uint32_t(h[p]) << 24 | uint32_t(h[p+1]) << 16 |
                                  uint32_t(h[p+2]) << 8 | h[p+3]; };
  return in.gcount() == sizeof(h) && std::string_view(reinterpret_cast<char*>(h),4) == "PIRS" &&
      be(0x344) == 0xB0000 && be(0x360) == 0xFFFE07D1;
}
inline bool HasZipSignature(const std::filesystem::path& path) {
  char h[4]{}; std::ifstream in(path, std::ios::binary); in.read(h,4);
  return std::string_view(h,4) == std::string_view("PK\3\4",4) ||
         std::string_view(h,4) == std::string_view("PK\5\6",4);
}
// Selected directory only; never search other dashboard versions or ancestors.
std::filesystem::path FindSystemUpdatePackage(const std::filesystem::path& directory);
int ExtractSystemUpdate(const std::filesystem::path& source, const std::filesystem::path& destination);
}
