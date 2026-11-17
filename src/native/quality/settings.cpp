#include "settings.h"
#include "../pipeline/pipeline_workers.h"
#include <algorithm>

#include <rex/chrono/frame_timing.h>
#include <stdexcept>
#include <bit>







namespace x360n::native {
NativeSettings& GetNativeSettings() { static NativeSettings settings; return settings; }
void AddNativeSettingAliases(rex::ISettingsRegistry& registry) {
  registry.AddAlias("native_anisotropy", "gpu.native.anisotropy");
  registry.AddAlias("native_capture", "gpu.native.capture");
  registry.AddAlias("native_capture_frames", "gpu.native.capture_frames");
  registry.AddAlias("native_capture_skip", "gpu.native.capture_skip");
  registry.AddAlias("native_capture_trigger", "gpu.native.capture_trigger");
  registry.AddAlias("native_d3d12_hybrid_color", "gpu.native.d3d12_hybrid_color");
  registry.AddAlias("native_d3d12_striped_sample_counts", "gpu.native.d3d12_striped_sample_counts");
  registry.AddAlias("native_draw_dump", "gpu.native.draw_dump");
  registry.AddAlias("native_draw_dump_ps", "gpu.native.draw_dump_ps");
  registry.AddAlias("native_eager_submit", "gpu.native.eager_submit");
  registry.AddAlias("native_eager_submit_depth", "gpu.native.eager_submit_depth");
  registry.AddAlias("native_frame_profile", "gpu.native.frame_profile");
  registry.AddAlias("native_gpu_timeout_ms", "gpu.native.gpu_timeout_ms");
  registry.AddAlias("native_pipeline_threads", "gpu.native.pipeline_threads");
  registry.AddAlias("native_pso_dump", "gpu.native.pso_dump");
  registry.AddAlias("native_redundant_state_filter", "gpu.native.redundant_state_filter");
  registry.AddAlias("native_scale_x", "gpu.native.scale_x");
  registry.AddAlias("native_scale_y", "gpu.native.scale_y");
  registry.AddAlias("native_stats", "gpu.native.stats");
  registry.AddAlias("native_texture_cache", "gpu.native.texture_cache");
  registry.AddAlias("native_texture_cpu", "gpu.native.texture_cpu");
  registry.AddAlias("native_trace", "gpu.native.trace");
  registry.AddAlias("native_vulkan_perf", "gpu.native.vulkan_perf");
  registry.AddAlias("native_enhanced", "gpu.native.enhanced");
  registry.AddAlias("native_framerate", "gpu.native.framerate");
  registry.AddAlias("native_present_rate", "gpu.native.present_rate");
  registry.AddAlias("native_ultrawide", "gpu.native.ultrawide");
}

bool NativeRedundantStateFilter() { return GetNativeSettings().redundant_state_filter.Get(); }
unsigned NativeEagerSubmitWork(unsigned automatic_work) {
  const auto work = GetNativeSettings().eager_submit.Get();
  return work < 0 ? automatic_work : unsigned(work);
}
unsigned NativeEagerSubmitDepth() { return std::min(GetNativeSettings().eager_submit_depth.Get(), 1u); }
unsigned NativePipelineThreads() {
  const auto threads = GetNativeSettings().pipeline_threads.Get();
  return threads < 0 ? PipelineWorkers::DefaultThreads() : unsigned(std::min(threads, 64));
}
NativeQualitySettings GetNativeQualitySettings() {
  NativeQualitySettings q;
  if (!rex::chrono::FrameTiming::Enhanced()) return q;
  q.scale_x = GetNativeSettings().scale_x.Get(); q.scale_y = GetNativeSettings().scale_y.Get();
  q.anisotropy = GetNativeSettings().anisotropy.Get();
  if (!q.scale_x || q.scale_x > 4 || !q.scale_y || q.scale_y > 4)
    throw std::invalid_argument("Native resolution scale must be in [1, 4]");
  if (q.anisotropy && (q.anisotropy < 2 || q.anisotropy > 16 || !std::has_single_bit(q.anisotropy)))
    throw std::invalid_argument("Native anisotropy must be 0, 2, 4, 8 or 16");
  return q;
}
rex::xenos::AnisoFilter NativeAnisotropy(rex::xenos::AnisoFilter guest) {
  const auto q = GetNativeQualitySettings();
  return q.anisotropy ? rex::xenos::AnisoFilter(std::countr_zero(q.anisotropy) + 1) : guest;
}
}
