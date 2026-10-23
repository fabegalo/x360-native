// patch 0186: guest texture layout and tiled 3D bounds from Xenia Canary
// (bc6b7a020, 19da17403, c9c8e483b, 81e3deaee, c332733af). No GPU.
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <rex/graphics/pipeline/texture/util.h>

using namespace rex::graphics;

namespace {

// Xenia Canary texture_address::Tiled3D (canary_experimental), independent of
// the SDK formula.
int64_t CanaryTiled3D(int32_t x, int32_t y, int32_t z, uint32_t pitch_aligned,
                      uint32_t height_aligned, unsigned bytes_per_block_log2) {
  const int32_t outer_blocks =
      ((((z >> 2) * int32_t(height_aligned >> 4) + (y >> 4)) * int32_t(pitch_aligned >> 5)) +
       (x >> 5))
      << 7;
  const int32_t inner_blocks = ((z & 0b11) << 5) | (((y >> 1) & 0b11) << 3) | (x & 0b111);
  const int64_t bytes = int64_t(outer_blocks | inner_blocks) << bytes_per_block_log2;
  const uint32_t bank = ((y >> 3) ^ (z >> 2)) & 0b1;
  const uint32_t pipe = ((x >> 3) & 0b11) ^ (bank << 1);
  return int64_t(int32_t(((y & 1) << 4) | (pipe << 6) | (bank << 11))) | (bytes & 0b1111) |
         (((bytes >> 4) & 0b1) << 5) | (((bytes >> 5) & 0b111) << 8) | (bytes >> 8 << 12);
}

}  // namespace

int main() {
  int failures = 0, checks = 0;
  auto check = [&](bool ok, const char* name) {
    ++checks;
    if (!ok) {
      ++failures;
      std::cerr << "FAIL " << name << '\n';
    }
  };

  // The SDK 3D tiling formula is the Canary one.
  {
    bool same = true;
    for (uint32_t bpb = 0; bpb <= 4; ++bpb)
      for (uint32_t pitch : {32u, 64u, 96u})
        for (uint32_t height : {32u, 64u})
          for (int32_t z = 0; z < 12; ++z)
            for (int32_t y = 0; y < int32_t(height); ++y)
              for (int32_t x = 0; x < int32_t(pitch); ++x)
                same &= texture_util::GetTiledOffset3D(x, y, z, pitch, height, bpb) ==
                        CanaryTiled3D(x, y, z, pitch, height, bpb);
    check(same, "SDK GetTiledOffset3D equals Canary Tiled3D");
  }

  // c332733af / c9c8e483b: tiled 3D bounds are exact.
  {
    bool upper_exact = true, lower_exact = true;
    for (uint32_t bpb = 0; bpb <= 4; ++bpb)
      for (uint32_t pitch : {32u, 64u, 96u})
        for (uint32_t right : {1u, 8u, 33u, pitch})
          for (uint32_t bottom : {1u, 9u, 32u, 40u})
            for (uint32_t back : {1u, 3u, 4u, 6u, 8u}) {
              const uint32_t height = (bottom + 31) & ~31u;
              int64_t max_address = -1;
              for (uint32_t z = 0; z < back; ++z)
                for (uint32_t y = 0; y < bottom; ++y)
                  for (uint32_t x = 0; x < right; ++x)
                    max_address = std::max(max_address,
                                           CanaryTiled3D(x, y, z, pitch, height, bpb));
              upper_exact &= texture_util::GetTiledAddressUpperBound3D(right, bottom, back, pitch,
                                                                       height, bpb) ==
                             uint64_t(max_address + (int64_t(1) << bpb));
              // The first 32x32x4 tile of a region starting at (0, 0, front).
              const uint32_t front = back - 1;
              const uint32_t front_aligned = front & ~3u;
              int64_t min_address = INT64_MAX;
              for (uint32_t z = front_aligned; z < front_aligned + 4; ++z)
                for (uint32_t y = 0; y < 32; ++y)
                  for (uint32_t x = 0; x < 32; ++x)
                    min_address = std::min(min_address,
                                           CanaryTiled3D(x, y, z, pitch, height, bpb));
              lower_exact &= texture_util::GetTiledAddressLowerBound3D(0, 0, front, pitch,
                                                                       height, bpb) ==
                             uint64_t(min_address);
            }
    check(upper_exact, "tiled 3D upper bound is one block past the highest address");
    check(lower_exact, "tiled 3D lower bound is the lowest address of the first tile");
  }

  // bc6b7a020: non-base 2D array levels are aligned to four slices.
  {
    auto layout = texture_util::GetGuestTextureLayout(
        xenos::DataDimension::k2DOrStacked, 2, 64, 64, 3, true, xenos::TextureFormat::k_8_8_8_8,
        false, true, 2);
    check(layout.mip_offsets_bytes[2] == layout.mips[1].array_slice_stride_bytes * 4,
          "3-slice array mip 2 follows 4 slices of mip 1");
  }
  // bc6b7a020: volume depth is reduced at each mip level.
  {
    auto layout = texture_util::GetGuestTextureLayout(
        xenos::DataDimension::k3D, 1, 32, 32, 16, true, xenos::TextureFormat::k_8_8_8_8, false,
        true, 1);
    const auto& mip = layout.mips[1];
    check(mip.array_slice_stride_bytes == mip.row_pitch_bytes * mip.z_slice_stride_block_rows * 8,
          "32x32x16 volume mip 1 has 8 slices");
  }
  // 81e3deaee: a packed level-0 volume base uses power-of-two depth.
  {
    auto layout = texture_util::GetGuestTextureLayout(
        xenos::DataDimension::k3D, 1, 16, 16, 9, true, xenos::TextureFormat::k_8_8_8_8, true,
        true, 0);
    const auto& base = layout.base;
    check(base.array_slice_stride_bytes ==
              base.row_pitch_bytes * base.z_slice_stride_block_rows * 16,
          "16x16x9 packed volume base has 16 slices");
  }
  // 19da17403: linear 96bpp mip rows are 32 blocks, not 256-byte aligned.
  {
    auto layout = texture_util::GetGuestTextureLayout(
        xenos::DataDimension::k2DOrStacked, 2, 64, 64, 1, false,
        xenos::TextureFormat::k_32_32_32_FLOAT, false, true, 1);
    check(layout.mips[1].row_pitch_bytes == 32 * 12, "linear 96bpp mip row pitch is 384");
    auto layout_32bpp = texture_util::GetGuestTextureLayout(
        xenos::DataDimension::k2DOrStacked, 2, 64, 64, 1, false,
        xenos::TextureFormat::k_8_8_8_8, false, true, 1);
    check(layout_32bpp.mips[1].row_pitch_bytes == 256, "linear 32bpp mip row pitch is 256");
  }
  // bc6b7a020: packed 1x1 Z mips count the tail base.
  {
    uint32_t x, y, z;
    texture_util::GetPackedMipOffset(32, 32, 1024, xenos::TextureFormat::k_8_8_8_8, 9, x, y, z);
    check(z == 4, "32x32x1024 mip 9 Z offset is 4");
  }

  std::cout << (checks - failures) << '/' << checks << " checks passed\n";
  return failures ? 1 : 0;
}
