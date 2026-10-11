// SHA-256 (FIPS 180-4). Used to identify input files exactly; portable so
// identities computed on any host agree.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace x360n {

class Sha256 {
 public:
  Sha256();
  void Update(std::span<const uint8_t> data);
  std::array<uint8_t, 32> Finish();

 private:
  void Block(const uint8_t* p);
  std::array<uint32_t, 8> h_;
  std::array<uint8_t, 64> buf_{};
  size_t buf_len_ = 0;
  uint64_t total_len_ = 0;
};

std::string ToHex(std::span<const uint8_t> bytes);
std::string Sha256Hex(std::span<const uint8_t> data);

struct FileDigest {
  uint64_t size = 0;
  std::string sha256;
};
// Streams the file; returns nullopt if it cannot be opened or read.
std::optional<FileDigest> DigestFile(const std::filesystem::path& path);

}  // namespace x360n
