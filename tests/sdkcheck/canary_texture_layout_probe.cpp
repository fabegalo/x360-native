// patch 0183: guest texture addressing from Xenia Canary (344fca7, ace153cb8,
// 4a863a0e1). Synthetic fetch constants only; no GPU.
#include <iostream>
#include <catch2/catch_test_macros.hpp>
#include <rex/xenos/texture.h>

namespace xenos = rex::xenos;
namespace texture_util = rex::xenos;

namespace {

xenos::TextureFetch Fetch2D(uint32_t width, uint32_t height, bool packed,
                                      uint32_t base_page, uint32_t mip_page,
                                      uint32_t mip_min, uint32_t mip_max) {
  xenos::TextureFetch fetch = {};
  fetch.type = xenos::FetchConstantType::kTexture;
  fetch.format = xenos::TextureFormat::k_8_8_8_8;
  fetch.tiled = 1;
  fetch.pitch = 1;
  fetch.dimension = xenos::DataDimension::k2DOrStacked;
  fetch.size_2d.width = width - 1;
  fetch.size_2d.height = height - 1;
  fetch.packed_mips = packed;
  fetch.base_address = base_page;
  fetch.mip_address = mip_page;
  fetch.mip_min_level = mip_min;
  fetch.mip_max_level = mip_max;
  return fetch;
}

struct Pages {
  uint32_t base, mip, min, max;
};

Pages Resolve(const xenos::TextureFetch& fetch) {
  Pages p;
  texture_util::GetSubresourcesFromFetchConstant(fetch, nullptr, nullptr, nullptr, &p.base,
                                                 &p.mip, &p.min, &p.max);
  return p;
}

}  // namespace

TEST_CASE("Guest texture layouts preserve tails, conversion metadata and wide 1D backing", "[sdk][gpu][shader-fetch][texture-layout]") {
  int failures = 0, checks = 0;
  auto check = [&](bool ok, const char* name) {
    ++checks;
    INFO(name);
    CHECK(ok);
    if (!ok) {
      ++failures;
      std::cerr << "FAIL " << name << '\n';
    }
  };

  // A 16x16 packed texture has level 0 in the tail: the whole tail is read
  // from base_address, even with a separate nonzero mip_address (344fca7).
  {
    Pages p = Resolve(Fetch2D(16, 16, true, 0x100, 0x200, 0, 4));
    check(p.base == 0x100, "packed level 0 keeps base page");
    check(p.mip == 0x100, "packed level 0 tail uses base page, not mip_address");
    check(p.min == 0 && p.max == 4, "packed level 0 keeps mip range");
  }
  // Same with no mip_address (ace153cb8).
  {
    Pages p = Resolve(Fetch2D(8, 8, true, 0x100, 0, 0, 3));
    check(p.mip == 0x100 && p.max == 3, "packed level 0 without mip_address keeps mips");
  }
  // Not packed at level 0 (64x64): mip_address is kept.
  {
    Pages p = Resolve(Fetch2D(64, 64, true, 0x100, 0x200, 0, 6));
    check(p.base == 0x100 && p.mip == 0x200, "unpacked level 0 keeps mip_address");
  }
  // 4a863a0e1: a nonzero minimum mip level drops the base only when it is
  // aliased by the mip address, not when the base exists separately.
  {
    Pages p = Resolve(Fetch2D(64, 64, false, 0x100, 0x200, 2, 6));
    check(p.base == 0x100, "separate base survives nonzero mip_min_level");
    check(p.min == 2, "separate base keeps guest mip_min_level");
  }
  {
    Pages p = Resolve(Fetch2D(64, 64, false, 0x200, 0x200, 2, 6));
    check(p.base == 0 && p.min == 2, "aliased base dropped with nonzero mip_min_level");
  }
  {
    Pages p = Resolve(Fetch2D(64, 64, false, 0, 0x200, 0, 6));
    check(p.base == 0 && p.min == 1, "missing base clamps mip_min_level to 1");
  }

  // 344fca7: a 2x1 tail is packed along X like a square texture.
  {
    uint32_t x = ~0u, y = ~0u, z = ~0u;
    check(texture_util::GetPackedMipOffset(2, 1, 1, xenos::TextureFormat::k_8_8_8_8, 0, x, y, z),
          "2x1 level 0 is packed");
    check(x == 16 && y == 0 && z == 0, "2x1 level 0 offset is (16, 0)");
    texture_util::GetPackedMipOffset(2, 1, 1, xenos::TextureFormat::k_8_8_8_8, 1, x, y, z);
    check(x == 8 && y == 0, "2x1 level 1 offset is (8, 0)");
    // 4x1 is still wider than tall: laid out along Y.
    texture_util::GetPackedMipOffset(4, 1, 1, xenos::TextureFormat::k_8_8_8_8, 0, x, y, z);
    check(x == 0 && y == 16, "4x1 level 0 offset is (0, 16)");
  }

  {
    auto fetch = Fetch2D(1, 1, false, 0x100, 0, 0, 0);
    fetch.mag_filter = fetch.min_filter = fetch.mip_filter = xenos::TextureFilter::kPoint;
    fetch.aniso_filter = xenos::AnisoFilter::kDisabled;
    fetch.swizzle = 0x688;
    const uint32_t normalized_point = (1u << 24) | (1u << 26);
    check(texture_util::GetTextureIntegerScaleBits(fetch, 0) == normalized_point,
          "UNORM8 metadata requests Q16 conversion without a width override");
    fetch.format = xenos::TextureFormat::k_4_4_4_4;
    check(texture_util::GetTextureIntegerScaleBits(fetch, 0) ==
        (normalized_point | 3u | (3u << 6) | (3u << 12) | (3u << 18)),
        "RGBA4 metadata carries all four component widths");
    fetch.format = xenos::TextureFormat::k_5_6_5;
    check(texture_util::GetTextureIntegerScaleBits(fetch, 0) ==
        (normalized_point | 4u | (5u << 6) | (4u << 12) | (4u << 18)),
        "RGB565 metadata replicates the last stored component");
    fetch.format = xenos::TextureFormat::k_8_8_8_8;
    fetch.num_format = 1;
    check(texture_util::GetTextureIntegerScaleBits(fetch, 0xE4) ==
        ((1u << 26) | 7u | (23u << 6) | (39u << 12)),
        "integer metadata carries mixed signs and excludes gamma components");
    fetch.format = xenos::TextureFormat::k_32_FLOAT;
    check(texture_util::GetTextureIntegerScaleBits(fetch, 0) == (1u << 26),
          "float textures retain only point-coordinate metadata");
    fetch.min_filter = xenos::TextureFilter::kLinear;
    check(texture_util::GetTextureIntegerScaleBits(fetch, 0) == 0,
          "linear floating textures request no conversion or coordinate snap");
  }
  for (uint32_t size : {8192u, 8193u, 24576u, 786432u, 16777216u}) {
    uint32_t width = size - 1, height = 0, pitch = 2, max_level = 4;
    texture_util::RemapWide1DTexture(width, height, pitch, max_level);
    const bool wide = size > 8192;
    check(width == 8191 && height == (wide ? std::min((size + 8191) / 8192, 128u) - 1 : 0),
          "wide 1D rows preserve boundaries and the 128-row materialization bound");
    check(pitch == (wide ? 256u : 2u) && max_level == (wide ? 0u : 4u),
          "wide 1D pitch and mip range match its host backing");
  }
  const auto wide = texture_util::GetGuestTextureLayout(xenos::DataDimension::k1D,
      256, 8192, 3, 1, false, xenos::TextureFormat::k_32_FLOAT, false, true, 0);
  check(wide.base.level_data_extent_bytes == 8192 * 3 * 4,
        "wide 1D guest layout includes every materialized row");
  std::cout << (checks - failures) << '/' << checks << " checks passed\n";
  CHECK(failures == 0);
}
