#include <catch2/catch_test_macros.hpp>
#include "../../src/native/quality/settings.h"
#include <rex/chrono/frame_timing.h>
#include "base/app_harness.h"

TEST_CASE("Native settings preserve old options and share the canonical scope", "[sdk][native-memory][native-settings]") {
  rex::test::RunInInit([] {
  auto& registry = REX_SETTINGS();
  auto& settings = x360n::native::GetNativeSettings();
  REQUIRE(registry.Register(settings));
  rex::ScopeGuard cleanup([&] {
    for (auto* setting : settings.GetOwnedSettings()) (void)setting->Reset();
    registry.Unregister(settings);
  });
  auto& frames = rex::chrono::GetNativeFrameSettings();
  const auto previous_rate = frames.framerate.Get();
  const auto previous_enhanced = frames.enhanced.Get();
  rex::ScopeGuard frame_cleanup([&] {
    (void)frames.framerate.Set(previous_rate); (void)frames.enhanced.Set(previous_enhanced);
  });
  x360n::native::AddNativeSettingAliases(registry);
  CHECK(registry.Enumerate("gpu.native").size() == 27);
  CHECK(settings.scale_x.Get() == 1); CHECK(settings.scale_y.Get() == 1);
  CHECK(settings.gpu_timeout_ms.Get() == 5000);
  CHECK(settings.texture_cache.Get()); CHECK_FALSE(settings.texture_cpu.Get());
  CHECK(settings.d3d12_striped_sample_counts.Get());
  CHECK(settings.capture.Get().empty()); CHECK(settings.frame_profile.Get().empty());
  REQUIRE(registry.SetText("native_scale_x", "3")); CHECK(settings.scale_x.Get() == 3);
  REQUIRE(registry.SetText("native_pipeline_threads", "0"));
  CHECK(x360n::native::NativePipelineThreads() == 0);
  REQUIRE(registry.SetText("native_eager_submit", "0"));
  CHECK(x360n::native::NativeEagerSubmitWork(271) == 0);
  REQUIRE(registry.SetText("native_eager_submit_depth", "2"));
  CHECK(x360n::native::NativeEagerSubmitDepth() == 1);
  CHECK_FALSE(registry.SetText("native_eager_submit", "-2"));
  CHECK(settings.eager_submit.Get() == 0);
  REQUIRE(registry.SetText("native_framerate", "120")); CHECK(frames.framerate.Get() == 120);
  REQUIRE(registry.SetText("native_enhanced", "true")); CHECK(frames.enhanced.Get());
  CHECK_FALSE(registry.SetText("native_gpu_timeout_ms", "invalid"));
  CHECK(settings.gpu_timeout_ms.Get() == 5000);
  CHECK_FALSE(registry.SetText("native_missing_option", "true"));
  });
}
