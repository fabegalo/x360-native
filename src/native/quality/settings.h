#pragma once
#include <cstdint>
#include <rex/setting.h>
#include <rex/settings_registry.h>
#include <rex/xenos/state.h>
#include <rex/xenos/fetch.h>
#include <rex/xenos/ucode.h>
#include <rex/xenos/packet.h>
namespace x360n::native {
class NativeSettings final : public rex::SettingOwner {
 public:
  NativeSettings() : SettingOwner("gpu.native") {}
  REX_SETTING(rex::u32, anisotropy, 0, "Enhanced anisotropic filtering: 0 preserves guest, or 2/4/8/16");
  REX_SETTING(std::string, capture, "", "Record renderer frames for x360n_native_replay to this file (contains title data: keep it under .local); empty disables it");
  REX_SETTING(rex::u32, capture_frames, 1, "Frames native_capture records");
  REX_SETTING(rex::u32, capture_skip, 300, "Presents to skip before native_capture starts recording");
  REX_SETTING(std::string, capture_trigger, "", "start native_capture at the first present after this file exists (instead of native_capture_skip)");
  REX_SETTING(bool, d3d12_hybrid_color, false, "Experimental D3D12 opaque HDR host targets with explicit ROV depth/stencil and other-case routing");
  REX_SETTING(bool, d3d12_striped_sample_counts, true, "Distributed GPU sample counters with exact reduction at guest read/snapshot boundaries (false selects scalar counters)");
  REX_SETTING(std::string, draw_dump, "", "diagnostic draw snapshot directory");
  REX_SETTING(std::string, draw_dump_ps, "", "diagnostic pixel shader hash to snapshot");
  REX_SETTING(rex::i64, eager_submit, -1, "Early submission threshold in draws (a resolve counts as many): -1 selects the backend default, 0 waits for the next guest sync, positive values override the threshold", rex::Range{-1, UINT32_MAX});
  REX_SETTING(rex::u32, eager_submit_depth, 0, "Early submission (native_eager_submit) also while this many submissions are still executing: 0 only when the GPU queue is idle, 1 keeps the next batch queued (higher values are clamped to 1: three recording arenas)");
  REX_SETTING(std::string, frame_profile, "", "Per-frame renderer profile CSV (draws, resolves, syncs, time inside/outside the renderer, submissions, fences, GPU); empty disables it");
  REX_SETTING(rex::u32, gpu_timeout_ms, 5000, "Limit of one Native GPU fence wait before reporting a timeout (minimum 1000; software adapters may need more)");
  REX_SETTING(rex::i32, pipeline_threads, -1, "Graphics pipeline creation threads: -1 automatic, 0 creates each pipeline at its first draw (previous behavior)");
  REX_SETTING(std::string, pso_dump, "", "diagnostic directory for the latest graphics PSO inputs");
  REX_SETTING(bool, redundant_state_filter, true, "skip recording graphics state equal to what the command list already holds (false records every call: A/b and diagnosis)");
  REX_SETTING(rex::u32, scale_x, 1, "Enhanced internal horizontal resolution scale (1 to 4)");
  REX_SETTING(rex::u32, scale_y, 1, "Enhanced internal vertical resolution scale (1 to 4)");
  REX_SETTING(bool, stats, false, "Log cumulative Native CPU submission costs for bounded diagnostics");
  REX_SETTING(bool, texture_cache, true, "Reuse textures only while every guest source byte matches; disable for diagnostics");
  REX_SETTING(bool, texture_cpu, false, "Diagnose texture loads with CPU conversion and synchronized guest bytes");
  REX_SETTING(std::string, trace, "", "optional Native diagnostic directory");
  REX_SETTING(bool, vulkan_perf, false, "Measure Native Vulkan submission, fence, buffer and publication costs");
};
NativeSettings& GetNativeSettings();
void AddNativeSettingAliases(rex::ISettingsRegistry&);

struct NativeQualitySettings {
  uint32_t scale_x = 1, scale_y = 1;
  uint32_t anisotropy = 0; // 0 preserves guest state; otherwise 2/4/8/16.
};
NativeQualitySettings GetNativeQualitySettings();
// Pipeline creation workers (native_pipeline_threads); 0 is synchronous.
unsigned NativePipelineThreads();
// Recorded draws (a resolve counts as this many) after which the open command
// list is submitted early when the GPU queue is idle. -1 selects the backend's
// measured automatic threshold; explicit 0 always disables early submission.
unsigned NativeEagerSubmitWork(unsigned automatic_work = 0);
// Submissions that may still be executing for an early submission to happen:
// 0 waits for an idle queue, 1 keeps one batch queued behind the running one.
// Never more: with three recording arenas a deeper queue would block the CPU.
unsigned NativeEagerSubmitDepth();
// Redundant graphics state is not recorded again (native_redundant_state_filter).
bool NativeRedundantStateFilter();
rex::xenos::AnisoFilter NativeAnisotropy(rex::xenos::AnisoFilter guest);
}
