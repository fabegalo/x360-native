// patch 0183: guest texture addressing from Xenia Canary (344fca7, ace153cb8,
// 4a863a0e1). Synthetic fetch constants only; no GPU.
#include <iostream>
#include <rex/graphics/pipeline/texture/util.h>

using namespace rex::graphics;

namespace {

xenos::xe_gpu_texture_fetch_t Fetch2D(uint32_t width, uint32_t height, bool packed,
                                      uint32_t base_page, uint32_t mip_page,
                                      uint32_t mip_min, uint32_t mip_max) {
  xenos::xe_gpu_texture_fetch_t fetch = {};
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

Pages Resolve(const xenos::xe_gpu_texture_fetch_t& fetch) {
  Pages p;
  texture_util::GetSubresourcesFromFetchConstant(fetch, nullptr, nullptr, nullptr, &p.base,
                                                 &p.mip, &p.min, &p.max);
  return p;
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

  std::cout << (checks - failures) << '/' << checks << " checks passed\n";
  return failures ? 1 : 0;
}
