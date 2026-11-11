#pragma once
#include "guest_memory_mirror.h"
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>
namespace x360n::native {
// Exact storage mapping of the pinned resolve_*_scaled and
// texture_load_*_scaled kernels: 8-byte X blocks for 8bpp, 16-byte blocks for
// 16/32/64/128bpp. Sub-block X precedes subpixel Y in memory. Within each
// sub-block, pixels are contiguous (they are NOT individually replicated).
// address is relative to a block-aligned guest base, before endian decoding.
constexpr uint64_t ScaledResolveByteOffset(uint32_t address, unsigned pixel_log2,
    uint32_t scale_x, uint32_t scale_y, unsigned sample_x, unsigned sample_y) {
  const unsigned block = pixel_log2 ? 16u : 8u;
  const unsigned pixel = 1u << pixel_log2, pixels_per_block = block >> pixel_log2;
  const uint32_t host_x = ((address & (block-1)) >> pixel_log2) * scale_x + sample_x;
  return uint64_t(address & ~(block-1)) * scale_x * scale_y +
      uint64_t((host_x / pixels_per_block) * scale_y + sample_y) * block +
      (host_x % pixels_per_block) * pixel + (address & (pixel-1));
}
static_assert(ScaledResolveByteOffset(4, 2, 2, 2, 0, 0) == 8);
static_assert(ScaledResolveByteOffset(4, 2, 2, 2, 1, 1) == 28);
static_assert(ScaledResolveByteOffset(8, 2, 2, 2, 0, 0) == 32);
static_assert(ScaledResolveByteOffset(1, 0, 3, 2, 2, 1) == 13);
static_assert(ScaledResolveByteOffset(16, 4, 2, 3, 1, 2) == 176);
// Inverse of ScaledResolveByteOffset: the guest byte (relative to the same
// block-aligned base) whose subpixel is stored at scaled byte `offset`. The
// GPU replication shaders (shaders/scaled_memory.h) implement exactly this.
constexpr uint32_t ScaledResolveGuestByte(uint64_t offset, unsigned pixel_log2, uint32_t scale_x, uint32_t scale_y) {
  const unsigned block = pixel_log2 ? 16u : 8u, pixels_per_block = block >> pixel_log2;
  const uint64_t group = offset / (uint64_t(block) * scale_x * scale_y);
  const uint32_t within = uint32_t(offset % (uint64_t(block) * scale_x * scale_y));
  const uint32_t row = within / block, byte = within % block;
  const uint32_t host_x = (row / scale_y) * pixels_per_block + (byte >> pixel_log2);
  return uint32_t(group * block + ((host_x / scale_x) << pixel_log2) + (byte & ((1u << pixel_log2) - 1)));
}
static_assert(ScaledResolveGuestByte(28, 2, 2, 2) == 4);
static_assert(ScaledResolveGuestByte(13, 0, 3, 2) == 1);
static_assert(ScaledResolveGuestByte(176, 4, 2, 3) == 16);

// Which 4 KB guest pages have current subpixels in the backend's GPU-resident
// scaled memory (guest address * scale area). A CPU write or a non-scaled GPU
// write (memexport) retires a page; before scaled memory of a retired or
// never-written page is used, the backend replicates the canonical 1x bytes
// into it, so scaled memory is never read stale. Pages are never evicted.
// Only pages a scaled resolve wrote make data resolution scaled (Overlaps):
// replicated pages hold 1x texels repeated, and must not turn neighbouring
// CPU textures into scaled ones.
class ScaledResolveCache {
 public:
  static constexpr uint32_t kPage = 4096;
  ScaledResolveCache(uint32_t x, uint32_t y) : x_(x), y_(y) {}
  ~ScaledResolveCache();
  void Clear();
  bool enabled() const { return x_ * y_ != 1; }
  uint32_t scale_x() const { return x_; }
  uint32_t scale_y() const { return y_; }
  void Attach(GuestMemoryMirror& mirror) { mirror_ = &mirror; }
  // Any page of the range holds subpixels written by a scaled resolve.
  bool Overlaps(uint32_t start, uint32_t length) const;
  // Page-aligned runs covering [start, start + length) whose scaled memory is
  // not current and must be replicated from 1x memory before use.
  std::vector<std::pair<uint32_t, uint32_t>> Stale(uint32_t start, uint32_t length) const;
  // The GPU commands recorded so far make the pages covering the range
  // current (resolved: by a scaled resolve, else by replication). Call after
  // RangeWrittenByGpu, which retires watches of the range.
  void MarkCurrent(uint32_t start, uint32_t length, bool resolved);
 private:
  struct Page { bool valid = false, resolved = false; GuestMemoryMirror::WatchHandle watch{}; };
  static void Invalidate(void*, void*, uint64_t, bool);
  uint32_t x_, y_;
  GuestMemoryMirror* mirror_ = nullptr;
  std::unordered_map<uint32_t, std::unique_ptr<Page>> pages_;
};
}
