#include "../../src/native/textures/texture.h"
#include "../../src/native/textures/texture_snapshot.h"
#include <catch2/catch_test_macros.hpp>
using namespace rex::graphics::xenos;
TEST_CASE("Native packed signed RGB10A2 preserves endpoints and negative steps") {
  for (unsigned bits : {2u, 10u}) {
    const unsigned sign = 1u << (bits - 1);
    CHECK(x360n::native::PackedSnorm(0, bits) == 0.0f);
    CHECK(x360n::native::PackedSnorm(sign - 1, bits) == 1.0f);
    CHECK(x360n::native::PackedSnorm(sign, bits) == -1.0f);
    CHECK(x360n::native::PackedSnorm(sign + 1, bits) == -1.0f);
    CHECK(x360n::native::PackedSnorm(sign * 2 - 1, bits) == -1.0f / (sign - 1));
  }
}
TEST_CASE("Native texture reuse detects CPU and resolve writes including mip tails") {
  uint8_t base[]{1,2,3,4}, mips[]{5,6,7,8};
  x360n::native::TextureSnapshot snapshot;
  snapshot.Assign(base, mips);
  REQUIRE(snapshot.Matches(base, mips));
  base[3] = 9; CHECK_FALSE(snapshot.Matches(base, mips));
  base[3] = 4; mips[3] = 10; CHECK_FALSE(snapshot.Matches(base, mips));
  snapshot.Assign(base, mips); CHECK(snapshot.Matches(base, mips));
  CHECK_FALSE(snapshot.Matches(std::span(base, 3), mips));
  CHECK_FALSE(snapshot.Matches(base, {}));
}
TEST_CASE("Native RGBA4 preserves every guest channel and UNORM endpoint") {
  CHECK(x360n::native::Rgba4Snorm(0) == 0.0f);
  CHECK(x360n::native::Rgba4Snorm(7) == 1.0f);
  CHECK(x360n::native::Rgba4Snorm(8) == -1.0f);
  CHECK(x360n::native::Rgba4Snorm(9) == -1.0f);
  CHECK(x360n::native::Rgba4Snorm(15) == -1.0f / 7.0f);
  CHECK(x360n::native::ExpandRgba4(0xF321) == 0xFF332211u);
  for (unsigned channel = 0; channel < 4; ++channel) {
    for (unsigned value = 0; value < 16; ++value) {
      auto rgba = x360n::native::ExpandRgba4(uint16_t(value << (channel * 4)));
      CHECK(((rgba >> (channel * 8)) & 255) / 255.0 == value / 15.0);
      CHECK((rgba & ~(255u << (channel * 8))) == 0);
    }
  }
}
TEST_CASE("Native scanout validates guest texture contracts") {
  xe_gpu_texture_fetch_t f{};
  f.type = FetchConstantType::kTexture; f.dimension = DataDimension::k2DOrStacked;
  f.format = TextureFormat::k_8_8_8_8; f.pitch = 8; f.base_address = 0x100;
  f.size_2d.width = 255; f.size_2d.height = 127; f.swizzle = 0x688;
  auto t = x360n::native::DecodeScanout(f, 200, 90);
  CHECK(t.bytes == 256 * 128 * 4); CHECK(t.width == 200); CHECK(t.height == 90);
  CHECK(t.address == 0x100000);
  SECTION("tiled extent") {
    f.tiled = 1; f.size_2d.height = 128;
    CHECK(x360n::native::DecodeScanout(f, 0, 0).bytes == 256 * 160 * 4);
  }
  SECTION("reject truncated pitch") { f.pitch = 1; CHECK_THROWS(x360n::native::DecodeScanout(f, 0, 0)); }
  SECTION("reject physical overflow") { f.base_address = 0x1ffff; CHECK_THROWS(x360n::native::DecodeScanout(f, 0, 0)); }
  SECTION("reject signed") { f.sign_x = TextureSign::kSigned; CHECK_THROWS(x360n::native::DecodeScanout(f, 0, 0)); }
  SECTION("RGB10 scanout and PWL selection") {
    CHECK_FALSE(t.rgb10);
    for (auto format : {TextureFormat::k_2_10_10_10, TextureFormat::k_2_10_10_10_AS_16_16_16_16}) {
      f.format = format;
      auto ten = x360n::native::DecodeScanout(f, 200, 90);
      CHECK(ten.rgb10);
      CHECK(ten.bytes == t.bytes);
      CHECK(ten.width == t.width);
    }
  }
  SECTION("reject unsupported format") { f.format = TextureFormat::k_DXT1; CHECK_THROWS(x360n::native::DecodeScanout(f, 0, 0)); }
  SECTION("reject reserved swizzle") { f.swizzle = 7; CHECK_THROWS(x360n::native::DecodeScanout(f, 0, 0)); }
  SECTION("reject mip-only") { f.mip_min_level = 1; CHECK_THROWS(x360n::native::DecodeScanout(f, 0, 0)); }
  SECTION("reject packed mip tail") { f.packed_mips = 1; CHECK_THROWS(x360n::native::DecodeScanout(f, 0, 0)); }
}
