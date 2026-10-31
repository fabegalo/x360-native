#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <rex/graphics/vulkan/depth_bias.h>
#include <bit>

using namespace rex::graphics;
using namespace rex::graphics::vulkan;
namespace {
RegisterFile Registers(xenos::DepthRenderTargetFormat format, float offset, float slope = 0) {
  RegisterFile regs{};
  reg::PA_SU_SC_MODE_CNTL mode{};
  mode.poly_offset_front_enable = 1;
  regs[mode.register_index] = mode.value;
  reg::RB_DEPTH_INFO depth{};
  depth.depth_format = format;
  regs[depth.register_index] = depth.value;
  regs[XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_OFFSET] = std::bit_cast<uint32_t>(offset);
  regs[XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_SCALE] = std::bit_cast<uint32_t>(slope);
  return regs;
}
struct RecordedBias {
  bool extended = false;
  float constant = 0, slope = 0;
  VkDepthBiasRepresentationEXT representation{};
  bool exact = false;
  void CmdVkSetDepthBias(float c, float clamp, float s) {
    REQUIRE(clamp == 0);
    extended = false; constant = c; slope = s;
  }
  void CmdVkSetDepthBias2EXT(float c, float clamp, float s,
                           VkDepthBiasRepresentationEXT r, bool e) {
    REQUIRE(clamp == 0);
    extended = true; constant = c; slope = s; representation = r; exact = e;
  }
};
}

TEST_CASE("Vulkan constant depth bias requires every optional feature", "[depth-bias]") {
  const auto regs = Registers(xenos::DepthRenderTargetFormat::kD24FS8, 1.0f / 4096);
  for (unsigned bits = 0; bits < 16; ++bits) {
    rex::ui::vulkan::VulkanDevice::Properties properties{};
    properties.depthBiasControl = (bits & 1) != 0;
    properties.leastRepresentableValueForceUnormRepresentation = (bits & 2) != 0;
    properties.depthBiasExact = (bits & 4) != 0;
    const auto bias = GetHostDepthBias(regs, true, 1, 1, properties, (bits & 8) != 0);
    RecordedBias recorded;
    RecordHostDepthBias(recorded, bias);
    REQUIRE(recorded.extended == (bits == 15));
    REQUIRE(recorded.constant == (bits == 15 ? 2048.0f : 4096.0f));
    if (bits == 15) {
      REQUIRE(recorded.exact);
      REQUIRE(recorded.representation ==
              VK_DEPTH_BIAS_REPRESENTATION_LEAST_REPRESENTABLE_VALUE_FORCE_UNORM_EXT);
    }
  }
}

TEST_CASE("Vulkan float24 uses half host range and preserves offset sign", "[depth-bias]") {
  rex::ui::vulkan::VulkanDevice::Properties p{};
  p.depthBiasControl = p.leastRepresentableValueForceUnormRepresentation = p.depthBiasExact = true;
  for (const auto format : {xenos::DepthRenderTargetFormat::kD24S8,
                            xenos::DepthRenderTargetFormat::kD24FS8}) {
    for (const float offset : {-0.125f, 0.0f, 0.125f}) {
      const auto bias = GetHostDepthBias(Registers(format, offset, 16), true, 2, 3, p, true);
      const float expected = offset * (format == xenos::DepthRenderTargetFormat::kD24FS8
          ? 8388608.0f : 16777215.0f);
      REQUIRE(bias.constant_factor == expected);
      REQUIRE(bias.slope_factor == Catch::Approx(16 * xenos::kPolygonOffsetScaleSubpixelUnit * 3));
    }
  }
}

TEST_CASE("Vulkan depth bias preserves guest face and primitive selection", "[depth-bias]") {
  rex::ui::vulkan::VulkanDevice::Properties p{};
  p.depthBiasControl = p.leastRepresentableValueForceUnormRepresentation = p.depthBiasExact = true;
  auto regs = Registers(xenos::DepthRenderTargetFormat::kD24FS8, 0.125f);
  REQUIRE(GetHostDepthBias(regs, false, 1, 1, p, true).constant_factor == 0);
  auto mode = regs.Get<reg::PA_SU_SC_MODE_CNTL>();
  mode.cull_front = 1;
  mode.poly_offset_back_enable = 1;
  regs[mode.register_index] = mode.value;
  regs[XE_GPU_REG_PA_SU_POLY_OFFSET_BACK_OFFSET] = std::bit_cast<uint32_t>(-0.25f);
  REQUIRE(GetHostDepthBias(regs, true, 1, 1, p, true).constant_factor == -2097152.0f);
  mode.poly_offset_para_enable = 1;
  regs[mode.register_index] = mode.value;
  REQUIRE(GetHostDepthBias(regs, false, 1, 1, p, true).constant_factor == 1048576.0f);
}
