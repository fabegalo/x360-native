#pragma once
#include <cstdint>
#include <rex/graphics/xenos.h>
namespace x360n::native {
struct NativeQualitySettings {
  uint32_t scale_x = 1, scale_y = 1;
  uint32_t anisotropy = 0; // 0 preserves guest state; otherwise 2/4/8/16.
};
NativeQualitySettings GetNativeQualitySettings();
// Pipeline creation workers (native_pipeline_threads); 0 is synchronous.
unsigned NativePipelineThreads();
rex::graphics::xenos::AnisoFilter NativeAnisotropy(rex::graphics::xenos::AnisoFilter guest);
}
