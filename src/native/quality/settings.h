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
// Recorded draws (a resolve counts as this many) after which the open command
// list is submitted early when the GPU queue is idle (native_eager_submit, 0 off).
unsigned NativeEagerSubmitWork();
// Submissions that may still be executing for an early submission to happen:
// 0 waits for an idle queue, 1 keeps one batch queued behind the running one.
// Never more: with three recording arenas a deeper queue would block the CPU.
unsigned NativeEagerSubmitDepth();
rex::graphics::xenos::AnisoFilter NativeAnisotropy(rex::graphics::xenos::AnisoFilter guest);
}
