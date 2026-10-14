#include "texture.h"
#include <catch2/catch_test_macros.hpp>
using namespace rex::graphics::xenos;
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
  SECTION("reject unsupported format") { f.format = TextureFormat::k_DXT1; CHECK_THROWS(x360n::native::DecodeScanout(f, 0, 0)); }
  SECTION("reject reserved swizzle") { f.swizzle = 7; CHECK_THROWS(x360n::native::DecodeScanout(f, 0, 0)); }
  SECTION("reject mip-only") { f.mip_min_level = 1; CHECK_THROWS(x360n::native::DecodeScanout(f, 0, 0)); }
  SECTION("reject packed mip tail") { f.packed_mips = 1; CHECK_THROWS(x360n::native::DecodeScanout(f, 0, 0)); }
}
