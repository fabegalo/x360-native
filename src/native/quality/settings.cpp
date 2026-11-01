#include "settings.h"
#include "../pipeline/pipeline_workers.h"
#include <algorithm>
#include <rex/cvar.h>
#include <rex/chrono/frame_timing.h>
#include <stdexcept>
#include <bit>
REXCVAR_DEFINE_UINT32(native_scale_x, 1, "GPU/Native", "Enhanced internal horizontal resolution scale (1 to 4)");
REXCVAR_DEFINE_UINT32(native_scale_y, 1, "GPU/Native", "Enhanced internal vertical resolution scale (1 to 4)");
REXCVAR_DEFINE_UINT32(native_anisotropy, 0, "GPU/Native", "Enhanced anisotropic filtering: 0 preserves guest, or 2/4/8/16");
REXCVAR_DEFINE_INT32(native_pipeline_threads, -1, "GPU/Native",
    "Graphics pipeline creation threads: -1 automatic, 0 creates each pipeline at its first draw (previous behavior)");
REXCVAR_DEFINE_UINT32(native_eager_submit, 64, "GPU/Native",
    "Submit recorded work early when the GPU queue is idle, after this many draws (a resolve counts as many); 0 waits for the next guest sync");
namespace x360n::native {
unsigned NativeEagerSubmitWork() { return REXCVAR_GET(native_eager_submit); }
unsigned NativePipelineThreads() {
  const auto threads = REXCVAR_GET(native_pipeline_threads);
  return threads < 0 ? PipelineWorkers::DefaultThreads() : unsigned(std::min(threads, 64));
}
NativeQualitySettings GetNativeQualitySettings() {
  NativeQualitySettings q;
  if (!rex::chrono::FrameTiming::Enhanced()) return q;
  q.scale_x = REXCVAR_GET(native_scale_x); q.scale_y = REXCVAR_GET(native_scale_y);
  q.anisotropy = REXCVAR_GET(native_anisotropy);
  if (!q.scale_x || q.scale_x > 4 || !q.scale_y || q.scale_y > 4)
    throw std::invalid_argument("Native resolution scale must be in [1, 4]");
  if (q.anisotropy && (q.anisotropy < 2 || q.anisotropy > 16 || !std::has_single_bit(q.anisotropy)))
    throw std::invalid_argument("Native anisotropy must be 0, 2, 4, 8 or 16");
  return q;
}
rex::graphics::xenos::AnisoFilter NativeAnisotropy(rex::graphics::xenos::AnisoFilter guest) {
  const auto q = GetNativeQualitySettings();
  return q.anisotropy ? rex::graphics::xenos::AnisoFilter(std::countr_zero(q.anisotropy) + 1) : guest;
}
}
