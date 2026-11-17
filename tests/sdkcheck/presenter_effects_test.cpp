#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include "gpu/xenos/host/presenter.h"
#include "gpu/xenos/null/null_xenos_graphics_driver.h"

namespace {
// Exercises the shared layout algorithm; no command list, draw or GPU image exists.
class LayoutSurface final : public rex::gpu::Surface {
 public:
  TypeIndex GetType() const override { return kTypeIndexWaylandSurface; }
 protected:
  bool ReadSize(rex::u32& width, rex::u32& height) const override {
    width = 7680; height = 4320; return true;
  }
};
class LayoutHost final : public rex::gpu::IPresentationHost {
 public:
  void RequestPaint() override {}
  void OnPresenterDetached() override {}
};
class LayoutPresenter final : public rex::gpu::Presenter {
 public:
  explicit LayoutPresenter(const rex::gpu::XenosGraphicsDriverBase& driver) : Presenter({}, driver) {}
  using Presenter::GetGuestOutputPaintFlow;
  using Presenter::GuestOutputPaintConfig;
  using Presenter::GuestOutputPaintEffect;
  using Presenter::GuestOutputPaintFlow;
  using Presenter::GuestOutputProperties;
  using Presenter::kMaxGuestOutputPaintEffects;
  using Presenter::kGuestOutputMailboxSize;
#if defined(REX_HAS_FIDELITYFX_SDK)
  using Presenter::CalculateCasPostSetupSharpness;
  using Presenter::FsrRcasConstants;
#endif
  rex::gpu::Surface::TypeFlags GetSupportedSurfaceTypes() const override {
    return rex::gpu::Surface::kTypeFlagWaylandSurface;
  }
  bool CaptureGuestOutput(rex::gpu::RawImage&) override { return false; }
 protected:
  SurfacePaintConnectResult ConnectOrReconnectPaintingToSurfaceFromUIThread(
      rex::gpu::Surface&, rex::u32, rex::u32, bool, bool& implicit_vsync) override {
    implicit_vsync = false; return SurfacePaintConnectResult::kSuccess;
  }
  void DriverDisconnectPaintingFromSurface() override {}
  bool DriverRefreshGuestOutput(rex::u32, rex::u32, rex::u32,
      rex::MoveOnlyFunction<bool(GuestOutputRefreshContext&)>, bool&) override { return false; }
  PaintResult DriverPaintAndPresent(bool) override { return PaintResult::kNotPresented; }
};
}

TEST_CASE("Presenter layout preserves inactive output and explicit effect availability",
          "[sdk][presenter-effects]") {
  rex::gpu::null::NullXenosGraphicsDriver driver;
  LayoutSurface surface;
  LayoutHost host;
  LayoutPresenter presenter(driver);
  REQUIRE(presenter.Attach(&host, &surface));
  rex::ScopeGuard detach([&] { (void)presenter.Attach(nullptr, nullptr); });
  LayoutPresenter::GuestOutputPaintConfig config;
  const auto flow = presenter.GetGuestOutputPaintFlow({}, 7680, 4320, 16384, 16384, config);
  CHECK(flow.effect_count == 0);
  REQUIRE(flow.letterbox_clear_rectangle_count == 1);
  CHECK(flow.letterbox_clear_rectangles[0].width == 7680);
  CHECK(flow.letterbox_clear_rectangles[0].height == 4320);
#if !defined(REX_HAS_FIDELITYFX_SDK)
  CHECK_FALSE(driver.present_effect.SetText(rex::SettingLayer::kRuntime, "cas"));
  CHECK(driver.present_effect.Get() == "bilinear");
#endif
}

#if defined(REX_HAS_FIDELITYFX_SDK)
TEST_CASE("Presenter effects retain bounded multipass FSR CAS and dithering",
          "[sdk][presenter-effects]") {
  rex::gpu::null::NullXenosGraphicsDriver driver;
  LayoutSurface surface;
  LayoutHost host;
  LayoutPresenter presenter(driver);
  REQUIRE(presenter.Attach(&host, &surface));
  rex::ScopeGuard detach([&] { (void)presenter.Attach(nullptr, nullptr); });
  using Effect = LayoutPresenter::GuestOutputPaintEffect;
  using Config = LayoutPresenter::GuestOutputPaintConfig;
  LayoutPresenter::GuestOutputProperties properties;
  properties.frontbuffer_width = 64; properties.frontbuffer_height = 36;
  properties.display_aspect_ratio_x = 16; properties.display_aspect_ratio_y = 9;
  properties.is_8bpc = false;
  Config config;
  config.SetEffect(Config::Effect::kFsr);
  config.SetFsrMaxUpsamplingPasses(4);
  auto flow = presenter.GetGuestOutputPaintFlow(properties, 7680, 4320, 16384, 16384, config);
  REQUIRE(flow.effect_count == LayoutPresenter::kMaxGuestOutputPaintEffects);
  REQUIRE(flow.effect_count > LayoutPresenter::kGuestOutputMailboxSize);
  for (size_t i = 0; i < 4; ++i) {
    CHECK(flow.effects[i] == Effect::kFsrEasu);
    rex::u32 width, height; flow.GetEffectInputSize(i, width, height);
    CHECK(flow.effect_output_sizes[i].first <= width * 2);
    CHECK(flow.effect_output_sizes[i].second <= height * 2);
  }
  CHECK(flow.effects[4] == Effect::kFsrRcas);
  CHECK(flow.effects[5] == Effect::kBilinear);
  CHECK((flow.effect_output_sizes[5] == std::pair<rex::u32, rex::u32>{7680, 4320}));
  config.SetDither(true);
  flow = presenter.GetGuestOutputPaintFlow(properties, 7680, 4320, 16384, 16384, config);
  CHECK(flow.effects[flow.effect_count - 1] == Effect::kBilinearDither);
  config.SetDither(false);
  config.SetEffect(Config::Effect::kCas);
  properties.frontbuffer_width = 7680; properties.frontbuffer_height = 4320;
  flow = presenter.GetGuestOutputPaintFlow(properties, 7680, 4320, 16384, 16384, config);
  REQUIRE(flow.effect_count == 1);
  CHECK(flow.effects[0] == Effect::kCasSharpen);
  config.SetCasAdditionalSharpness(std::numeric_limits<float>::quiet_NaN());
  CHECK(config.GetCasAdditionalSharpness() == 0.0f);
  config.SetCasAdditionalSharpness(2.0f);
  CHECK(config.GetCasAdditionalSharpness() == 1.0f);
  CHECK(LayoutPresenter::CalculateCasPostSetupSharpness(0.0f) == -0.125f);
  CHECK(LayoutPresenter::CalculateCasPostSetupSharpness(1.0f) == -0.2f);
  CHECK(LayoutPresenter::FsrRcasConstants::CalculatePostSetupSharpness(0.0f) == 1.0f);
  CHECK(LayoutPresenter::FsrRcasConstants::CalculatePostSetupSharpness(2.0f) == 0.25f);
}
#endif
