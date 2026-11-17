#include <catch2/catch_test_macros.hpp>
#include <rex/xenos/display_overlay.h>

using namespace rex::gpu;
using rex::xenos::RegisterFile;

TEST_CASE("Display overlay decodes original RGB scanout coordinates and surface", "[sdk][display-overlay]") {
  RegisterFile regs{};
  regs[rex::xenos::Register::D1OVL_ENABLE] = 1;
  regs[rex::xenos::Register::D1OVL_CONTROL1] = 0;
  regs[rex::xenos::Register::D1OVL_SURFACE_ADDRESS] = 0x12345000;
  regs[rex::xenos::Register::D1OVL_PITCH] = 864;
  regs[rex::xenos::Register::D1OVL_SURFACE_OFFSET_X] = 256;
  regs[rex::xenos::Register::D1OVL_SURFACE_OFFSET_Y] = 20;
  regs[rex::xenos::Register::D1OVL_START] = (256 << 16) | 20;
  regs[rex::xenos::Register::D1OVL_END] = (800 << 16) | 100;
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
  CHECK(decoded.fetch.endianness == rex::xenos::Endian::kNone);
  CHECK(decoded.fetch.swizzle == REXGPU_SWIZZLE(B, G, R, A));
  CHECK(decoded.fetch.format == rex::xenos::TextureFormat::k_8_8_8_8);
  SECTION("Ten-bit RGB format") {
    regs[rex::xenos::Register::D1OVL_CONTROL1] = 0x100;
    auto ten = DecodeDisplayOverlay(regs);
    REQUIRE(ten.valid);
    CHECK(ten.fetch.format == rex::xenos::TextureFormat::k_2_10_10_10);
  }
  SECTION("Disable removes a previously valid overlay") {
    regs[rex::xenos::Register::D1OVL_ENABLE] = 0;
    CHECK_FALSE(DecodeDisplayOverlay(regs).valid);
  }
  SECTION("Malformed and unsupported surfaces are not sampled") {
    for (auto [reg, value] : {std::pair{rex::xenos::Register::D1OVL_PITCH, 0u},
          {rex::xenos::Register::D1OVL_PITCH, 32u}, {rex::xenos::Register::D1OVL_PITCH, 865u},
          {rex::xenos::Register::D1OVL_SURFACE_ADDRESS, 0u},
          {rex::xenos::Register::D1OVL_SURFACE_ADDRESS, 0x12345004u},
          {rex::xenos::Register::D1OVL_END, (200u << 16) | 100u},
          {rex::xenos::Register::D1OVL_END, (800u << 16) | 10u},
          {rex::xenos::Register::D1OVL_CONTROL1, 0x700u}}) {
      const auto saved = regs[reg];
      regs[reg] = value;
      CHECK_FALSE(DecodeDisplayOverlay(regs).valid);
      regs[reg] = saved;
    }
  }
}
