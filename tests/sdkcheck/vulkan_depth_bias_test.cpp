#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "gpu/xenos/vulkan/depth_bias.h"
#include "gpu/xenos/xenos_settings.h"

#include <bit>

using namespace rex::gpu;
using rex::xenos::RegisterFile;
namespace xenos = rex::xenos;
namespace reg = rex::xenos::reg;
namespace draw_util = rex::gpu::draw_state;
using namespace rex::gpu::vulkan;
namespace {
RegisterFile Registers(xenos::DepthRenderTargetFormat format, float offset, float slope = 0) {
  RegisterFile regs{};
  reg::PA_SU_SC_MODE_CNTL mode{};
  mode.poly_offset_front_enable = 1;
  regs[mode.kRegisterIndex] = mode.value;
  reg::RB_DEPTH_INFO depth{};
  depth.depth_format = format;
  regs[depth.kRegisterIndex] = depth.value;
  regs[std::to_underlying(xenos::Register::PA_SU_POLY_OFFSET_FRONT_OFFSET)] = std::bit_cast<uint32_t>(offset);
  regs[std::to_underlying(xenos::Register::PA_SU_POLY_OFFSET_FRONT_SCALE)] = std::bit_cast<uint32_t>(slope);
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

TEST_CASE("Vulkan constant depth bias requires every optional feature", "[sdk][gpu][shader-fetch][depth-bias]") {
  const auto regs = Registers(xenos::DepthRenderTargetFormat::kD24FS8, 1.0f / 4096);
  for (unsigned bits = 0; bits < 16; ++bits) {
    rex::gpu::vulkan::VulkanDevice::Properties properties{};
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

TEST_CASE("Vulkan float24 uses half host range and preserves offset sign", "[sdk][gpu][shader-fetch][depth-bias]") {
  rex::gpu::vulkan::VulkanDevice::Properties p{};
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

TEST_CASE("Vulkan depth bias preserves guest face and primitive selection", "[sdk][gpu][shader-fetch][depth-bias]") {
  rex::gpu::vulkan::VulkanDevice::Properties p{};
  p.depthBiasControl = p.leastRepresentableValueForceUnormRepresentation = p.depthBiasExact = true;
  auto regs = Registers(xenos::DepthRenderTargetFormat::kD24FS8, 0.125f);
  REQUIRE(GetHostDepthBias(regs, false, 1, 1, p, true).constant_factor == 0);
  auto mode = regs.Get<reg::PA_SU_SC_MODE_CNTL>();
  mode.cull_front = 1;
  mode.poly_offset_back_enable = 1;
  regs[mode.kRegisterIndex] = mode.value;
  regs[std::to_underlying(xenos::Register::PA_SU_POLY_OFFSET_BACK_OFFSET)] = std::bit_cast<uint32_t>(-0.25f);
  REQUIRE(GetHostDepthBias(regs, true, 1, 1, p, true).constant_factor == -2097152.0f);
  mode.poly_offset_para_enable = 1;
  regs[mode.kRegisterIndex] = mode.value;
  REQUIRE(GetHostDepthBias(regs, false, 1, 1, p, true).constant_factor == 1048576.0f);
}

TEST_CASE("Canary shader depth bias is limited to eligible decal redraws", "[sdk][gpu][shader-fetch][depth-bias]") {
  struct TestSettings : XenosSettings {} settings;
  auto regs = Registers(xenos::DepthRenderTargetFormat::kD24FS8, -0.25f, 32);
  auto mode = regs.Get<reg::PA_SU_SC_MODE_CNTL>();
  mode.poly_offset_back_enable = 1;
  regs[mode.kRegisterIndex] = mode.value;
  regs[std::to_underlying(xenos::Register::PA_SU_POLY_OFFSET_BACK_OFFSET)] = std::bit_cast<uint32_t>(0.5f);
  regs[std::to_underlying(xenos::Register::PA_SU_POLY_OFFSET_BACK_SCALE)] = std::bit_cast<uint32_t>(-16.0f);
  reg::RB_DEPTHCONTROL depth{};
  depth.z_enable = 1;
  depth.zfunc = xenos::CompareFunction::kLessEqual;
  draw_util::HostDepthPolygonOffset offset;
  REQUIRE(settings.depth_bias_shader_offset.Set(false));
  REQUIRE_FALSE(draw_util::GetHostDepthPolygonOffsetIfNeeded(settings, regs, true, depth, 15, offset));
  REQUIRE(offset.front_scale == 0);
  REQUIRE(settings.depth_bias_shader_offset.Set(true));
  for (unsigned function = 0; function < 8; ++function) {
    depth.zfunc = xenos::CompareFunction(function);
    const bool eligible = function == unsigned(xenos::CompareFunction::kLessEqual) ||
                          function == unsigned(xenos::CompareFunction::kGreaterEqual);
    REQUIRE(draw_util::GetHostDepthPolygonOffsetIfNeeded(settings, regs, true, depth, 15, offset) == eligible);
  }
  depth.zfunc = xenos::CompareFunction::kLessEqual;
  REQUIRE(draw_util::GetHostDepthPolygonOffsetIfNeeded(settings, regs, true, depth, 15, offset));
  REQUIRE(offset.front_scale == 2.0f);
  REQUIRE(offset.back_scale == -1.0f);
  REQUIRE(offset.front_offset == -0.125f);
  REQUIRE(offset.back_offset == 0.25f);
  REQUIRE_FALSE(draw_util::GetHostDepthPolygonOffsetIfNeeded(settings, regs, false, depth, 15, offset));
  REQUIRE_FALSE(draw_util::GetHostDepthPolygonOffsetIfNeeded(settings, regs, true, depth, 0, offset));
  depth.stencil_enable = 1;
  REQUIRE_FALSE(draw_util::GetHostDepthPolygonOffsetIfNeeded(settings, regs, true, depth, 15, offset));
  depth.stencil_enable = 0;
  depth.z_enable = 0;
  REQUIRE_FALSE(draw_util::GetHostDepthPolygonOffsetIfNeeded(settings, regs, true, depth, 15, offset));
  depth.z_enable = 1;
  reg::RB_COLORCONTROL color{};
  color.alpha_to_mask_enable = 1;
  regs[color.kRegisterIndex] = color.value;
  REQUIRE_FALSE(draw_util::GetHostDepthPolygonOffsetIfNeeded(settings, regs, true, depth, 15, offset));
  regs[color.kRegisterIndex] = 0;
  reg::RB_DEPTH_INFO info{};
  info.depth_format = xenos::DepthRenderTargetFormat::kD24S8;
  regs[info.kRegisterIndex] = info.value;
  REQUIRE(draw_util::GetHostDepthPolygonOffsetIfNeeded(settings, regs, true, depth, 15, offset));
  REQUIRE(offset.front_offset == -0.25f);
  REQUIRE(offset.back_offset == 0.5f);
  mode.poly_offset_front_enable = mode.poly_offset_back_enable = 0;
  regs[mode.kRegisterIndex] = mode.value;
  REQUIRE_FALSE(draw_util::GetHostDepthPolygonOffsetIfNeeded(settings, regs, true, depth, 15, offset));
  REQUIRE(offset.front_scale == 0);
  REQUIRE(offset.back_offset == 0);
  REQUIRE(settings.depth_bias_shader_offset.Set(false));
}
