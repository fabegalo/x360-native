#pragma once
#include "guest_memory_mirror.h"
#include "../textures/texture.h"
#include <memory>
#include <unordered_map>
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

// Authoritative subpixels of resolved guest bytes. Pages are sparse and never
// evicted while valid: discarding them would silently reduce image quality.
// The ordinary guest mirror always contains the canonical 1x sample.
class ScaledResolveCache {
 public:
  ScaledResolveCache(uint32_t x, uint32_t y) : x_(x), y_(y) {}
  ~ScaledResolveCache();
  void Clear();
  bool enabled() const { return x_ * y_ != 1; }
  uint32_t scale_x() const { return x_; }
  uint32_t scale_y() const { return y_; }
  void Attach(GuestMemoryMirror& mirror) { mirror_ = &mirror; }
  bool Overlaps(uint32_t start, uint32_t length) const;
  // Caller holds the guest invalidation lock. Missing bytes use current guest
  // storage, including CPU-created data mixed with a partially resolved tile.
  uint8_t Read(uint32_t address, unsigned sample, uint8_t fallback) const;
  std::vector<uint8_t> Hydrate(uint32_t base, uint32_t length, unsigned pixel_log2,
                              rex::memory::Memory&) const;
  struct PendingPage { uint32_t index; uint64_t version; std::shared_ptr<std::vector<uint8_t>> previous; };
  struct Ticket { uint32_t start, length; std::vector<PendingPage> pages; std::vector<uint64_t> cpu_generations; };
  Ticket Capture(uint32_t start, uint32_t length) const;
  // Arm after RangeWrittenByGpu. A CPU or later GPU write retires the page,
  // so a delayed fence can never resurrect obsolete subpixels.
  void Begin(Ticket&);
  void Complete(const Ticket&, std::span<const uint8_t> pixels, unsigned pixel_log2,
                rex::memory::Memory&);
  // Produces a linear surface, with original endian/swizzle, before gamma.
  bool Scanout(const ScanoutTexture&, const void*, ScanoutTexture&, std::vector<uint8_t>&) const;
 private:
  static constexpr uint32_t kPage = 4096;
  struct Page { bool valid = false; uint64_t version = 0; GuestMemoryMirror::WatchHandle watch{};
                std::shared_ptr<std::vector<uint8_t>> data; };
  static void Invalidate(void*, void*, uint64_t, bool);
  uint32_t x_, y_;
  GuestMemoryMirror* mirror_ = nullptr;
  std::unordered_map<uint32_t, std::unique_ptr<Page>> pages_;
};
}
