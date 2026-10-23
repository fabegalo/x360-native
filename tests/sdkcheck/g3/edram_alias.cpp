// patch 0195: with ROV/FSI, a color target aliasing the depth buffer disables
// depth/stencil only if it writes bits those tests use (Xenia Canary
// 9da693480). CPU-only.
#include <rex/graphics/pipeline/render_target/cache.h>

#include <iostream>

using namespace rex::graphics;

namespace {
bool Overlaps(xenos::ColorRenderTargetFormat format, uint32_t write_mask, bool z, bool stencil) {
  float clamp[4];
  uint32_t keep_low, keep_high;
  RenderTargetCache::GetPSIColorFormatInfo(format, write_mask, clamp[0], clamp[1], clamp[2],
                                           clamp[3], keep_low, keep_high);
  reg::RB_DEPTHCONTROL depth_control{};
  depth_control.z_enable = z;
  depth_control.stencil_enable = stencil;
  return RenderTargetCache::ColorOverlapsDepthStencil(format, keep_low, keep_high, depth_control);
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
  constexpr auto k8888 = xenos::ColorRenderTargetFormat::k_8_8_8_8;
  // Red of 8_8_8_8 is bits 0:7, the stencil byte of a D24S8 sample.
  check(!Overlaps(k8888, 0b0001, true, false), "stencil-byte color write keeps depth testing");
  check(Overlaps(k8888, 0b0001, true, true), "stencil-byte color write conflicts with stencil");
  check(Overlaps(k8888, 0b0010, true, false), "green overlaps depth bits");
  check(Overlaps(k8888, 0b1111, false, true), "full write overlaps stencil");
  check(!Overlaps(k8888, 0b0000, true, true), "no color write keeps both tests");
  check(Overlaps(xenos::ColorRenderTargetFormat::k_16_16_16_16, 0b1000, true, false),
        "64bpp writes are conservatively overlapping");
  std::cout << (checks - failures) << '/' << checks << " checks passed\n";
  return failures ? 1 : 0;
}
