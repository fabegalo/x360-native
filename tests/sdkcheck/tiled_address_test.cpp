#include <catch2/catch_test_macros.hpp>
#include <rex/xenos/texture.h>
#include <algorithm>
#include <cstdint>

TEST_CASE("3D tiled addresses retain high bits until the guest address boundary",
          "[sdk][gpu][tiled-address]") {
  using namespace rex::xenos;
  CHECK(sizeof(GetTiledOffset3D(0, 0, 0, 32, 32, 0)) == 8);
  CHECK(sizeof(GetTiledAddressLowerBound3D(0, 0, 0, 32, 32, 0)) == 8);
  CHECK(sizeof(GetTiledAddressUpperBound3D(1, 1, 1, 32, 32, 0)) == 8);
  // At these aligned coordinates the bank and pipe bits are zero. The storage
  // stride is 8192 * 2048 * 16 bytes per slice, well beyond a 32-bit offset.
  CHECK(GetTiledOffset3D(0, 0, 256, 8192, 2048, 4) == INT64_C(68719476736));
  CHECK(GetTiledOffset3D(0, 0, 512, 8192, 2048, 4) == INT64_C(137438953472));
  CHECK(GetTiledAddressLowerBound3D(0, 0, 256, 8192, 2048, 4) == UINT64_C(68719476736));
  CHECK(GetTiledAddressUpperBound3D(0, 8, 4, 32, 32, 4) == 0);
  CHECK(GetTiledAddressUpperBound3D(8, 0, 4, 32, 32, 4) == 0);
  CHECK(GetTiledAddressUpperBound3D(8, 8, 0, 32, 32, 4) == 0);
}

TEST_CASE("3D tiled bounds contain every addressed block including bank swaps and row overlap",
          "[sdk][gpu][tiled-address]") {
  using namespace rex::xenos;
  for (uint32_t bpp : {0u, 1u, 2u, 3u, 4u}) {
    for (uint32_t pitch : {32u, 64u}) {
      for (uint32_t right : {1u, 7u, 9u, 31u, 33u, 65u, 97u}) {
        for (uint32_t bottom : {1u, 9u, 17u, 33u}) {
          for (uint32_t back : {1u, 4u, 5u, 8u, 9u}) {
            CAPTURE(bpp, pitch, right, bottom, back);
            uint64_t highest = 0;
            for (uint32_t z = 0; z < back; ++z)
              for (uint32_t y = 0; y < bottom; ++y)
                for (uint32_t x = 0; x < right; ++x)
                  highest = std::max(highest, uint64_t(GetTiledOffset3D(x, y, z, pitch, 64, bpp)));
            CHECK(GetTiledAddressUpperBound3D(right, bottom, back, pitch, 64, bpp) ==
                  highest + (UINT64_C(1) << bpp));
          }
        }
      }
    }
  }
}

TEST_CASE("Packed tails retain base storage and the console mip orientation", "[sdk][gpu][tiled-address]") {
  using namespace rex::xenos;
  struct Case {
    uint32_t width, height, packed, base, mip, min, max, base_out, mip_out, min_out, max_out;
  };
  const Case cases[] = {
      {16,16,1,0x100,0x200,0,4,0x100,0x100,0,4},
      {8,8,1,0x100,0,0,3,0x100,0x100,0,3},
      {64,64,1,0x100,0x200,0,6,0x100,0x200,0,6},
      {64,64,0,0x100,0x200,2,6,0x100,0x200,2,6},
      {64,64,0,0x200,0x200,2,6,0,0x200,2,6},
      {64,64,0,0,0x200,0,6,0,0x200,1,6}};
  for (const auto& c : cases) {
    CAPTURE(c.width,c.height,c.base,c.mip,c.min);
    TextureFetch fetch{};
    fetch.type = FetchConstantType::kTexture;
    fetch.dimension = DataDimension::k2DOrStacked;
    fetch.format = TextureFormat::k_8_8_8_8;
    fetch.tiled = 1;
    fetch.pitch = 1;
    fetch.size_2d.width = c.width - 1;
    fetch.size_2d.height = c.height - 1;
    fetch.packed_mips = c.packed;
    fetch.base_address = c.base;
    fetch.mip_address = c.mip;
    fetch.mip_min_level = c.min;
    fetch.mip_max_level = c.max;
    uint32_t base, mip, min, max;
    GetSubresourcesFromFetchConstant(fetch,nullptr,nullptr,nullptr,&base,&mip,&min,&max);
    CHECK(base == c.base_out); CHECK(mip == c.mip_out);
    CHECK(min == c.min_out); CHECK(max == c.max_out);
  }
  uint32_t x,y,z;
  REQUIRE(GetPackedMipOffset(2,1,1,TextureFormat::k_8_8_8_8,0,x,y,z));
  CHECK(x == 16); CHECK(y == 0); CHECK(z == 0);
  REQUIRE(GetPackedMipOffset(4,1,1,TextureFormat::k_8_8_8_8,0,x,y,z));
  CHECK(x == 0); CHECK(y == 16);
}

TEST_CASE("Texture layout preserves mip array alignment and volume depth", "[sdk][gpu][tiled-address]") {
  using namespace rex::xenos;
  const auto array = GetGuestTextureLayout(DataDimension::k2DOrStacked,2,64,64,3,
                                           false,TextureFormat::k_8_8_8_8,false,true,3);
  CHECK(array.array_size == 3);
  CHECK(array.mip_offsets_bytes[2] - array.mip_offsets_bytes[1] ==
        array.mips[1].array_layer_stride_bytes * 4);
  const auto volume = GetGuestTextureLayout(DataDimension::k3D,2,64,64,16,
                                            true,TextureFormat::k_8_8_8_8,false,true,3);
  CHECK(volume.mips[1].array_layer_stride_bytes == 32768);
  CHECK(volume.mips[2].array_layer_stride_bytes == 16384);
  const auto packed = GetGuestTextureLayout(DataDimension::k2DOrStacked,1,8,65,1,
                                            true,TextureFormat::k_8_8_8_8,true,true,6);
  CHECK(packed.base.z_layer_stride_block_rows == 128);
  uint32_t x,y,z;
  REQUIRE(GetPackedMipOffset(64,64,512,TextureFormat::k_8_8_8_8,7,x,y,z));
  CHECK(z == 8);
}
