#include <catch2/catch_test_macros.hpp>
#include <rex/graphics/display_overlay.h>

using namespace rex::graphics;

TEST_CASE("Display overlay decodes original RGB scanout coordinates and surface", "[sdk][display-overlay]") {
  RegisterFile regs{};
  regs[XE_GPU_REG_D1OVL_ENABLE] = 1;
  regs[XE_GPU_REG_D1OVL_CONTROL1] = 0;
  regs[XE_GPU_REG_D1OVL_SURFACE_ADDRESS] = 0x12345000;
  regs[XE_GPU_REG_D1OVL_PITCH] = 864;
  regs[XE_GPU_REG_D1OVL_SURFACE_OFFSET_X] = 256;
  regs[XE_GPU_REG_D1OVL_SURFACE_OFFSET_Y] = 20;
  regs[XE_GPU_REG_D1OVL_START] = (256 << 16) | 20;
  regs[XE_GPU_REG_D1OVL_END] = (800 << 16) | 100;
  const auto decoded = DecodeDisplayOverlay(regs);
  REQUIRE(decoded.valid);
  CHECK(decoded.x == 256);
  CHECK(decoded.y == 20);
  CHECK(decoded.right == 800);
  CHECK(decoded.bottom == 100);
  CHECK(decoded.fetch.base_address == 0x12345);
  CHECK(decoded.fetch.pitch == 27);
  CHECK(decoded.fetch.size_2d.width == 543);
  CHECK(decoded.fetch.size_2d.height == 79);
  CHECK(decoded.fetch.tiled == 1);
  CHECK(decoded.fetch.endianness == rex::graphics::xenos::Endian::kNone);
  CHECK(decoded.fetch.swizzle == XE_GPU_MAKE_TEXTURE_SWIZZLE(B, G, R, A));
  CHECK(decoded.fetch.format == rex::graphics::xenos::TextureFormat::k_8_8_8_8);
  SECTION("Ten-bit RGB format") {
    regs[XE_GPU_REG_D1OVL_CONTROL1] = 0x100;
    auto ten = DecodeDisplayOverlay(regs);
    REQUIRE(ten.valid);
    CHECK(ten.fetch.format == rex::graphics::xenos::TextureFormat::k_2_10_10_10);
  }
  SECTION("Disable removes a previously valid overlay") {
    regs[XE_GPU_REG_D1OVL_ENABLE] = 0;
    CHECK_FALSE(DecodeDisplayOverlay(regs).valid);
  }
  SECTION("Malformed and unsupported surfaces are not sampled") {
    for (auto [reg, value] : {std::pair{XE_GPU_REG_D1OVL_PITCH, 0u},
          {XE_GPU_REG_D1OVL_PITCH, 32u}, {XE_GPU_REG_D1OVL_PITCH, 865u},
          {XE_GPU_REG_D1OVL_SURFACE_ADDRESS, 0u},
          {XE_GPU_REG_D1OVL_SURFACE_ADDRESS, 0x12345004u},
          {XE_GPU_REG_D1OVL_END, (200u << 16) | 100u},
          {XE_GPU_REG_D1OVL_END, (800u << 16) | 10u},
          {XE_GPU_REG_D1OVL_CONTROL1, 0x700u}}) {
      const auto saved = regs[reg];
      regs[reg] = value;
      CHECK_FALSE(DecodeDisplayOverlay(regs).valid);
      regs[reg] = saved;
    }
  }
}
