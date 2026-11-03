#pragma once
#include <cstdint>
#include <filesystem>
namespace x360n::content {
int Extract(const std::filesystem::path& package, const std::filesystem::path& destination);
int ExtractDisc(const std::filesystem::path& image, const std::filesystem::path& destination);
// Whether one more container entry of `size` bytes fits one extraction, after
// `entries` entries and `declared` file bytes. A file may use the whole 32-bit
// size of XDVDFS and STFS entries: retail discs hold files beyond 2 GiB (Forza
// Horizon's colorado/bin.zip, 2.74 GiB). The entry count and the declared total
// bound what a package can claim.
inline bool WithinExtractionLimits(uint64_t entries, uint64_t size, uint64_t declared) {
  constexpr uint64_t kTotal = 8ull << 30;
  return entries <= 65536 && size <= 0xFFFFFFFFull && declared <= kTotal && size <= kTotal - declared;
}
}
