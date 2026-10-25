// CPU regressions use the renderer's draw, extent, ownership and transfer code.
// Only GPU resource allocation / barrier emission are replaced by recording
// hooks.
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <rex/graphics/pipeline/render_target/cache.h>
#include <rex/string.h>

using namespace rex::graphics;
template <class T> void Set(RegisterFile &r, T v) {
  r[T::register_index] = v.value;
}
void F(RegisterFile &r, uint32_t i, float v) { std::memcpy(&r[i], &v, 4); }
struct Cache final : RenderTargetCache {
  using Key = RenderTargetKey;
  using Rect = Transfer::Rectangle;
  using RenderTargetCache::PixelShaderInterlockFullEdramBarrierPlaced;
  Path path;
  unsigned creates = 0, barriers = 0;
  Cache(RegisterFile &r, rex::memory::Memory &m, Path p)
      : RenderTargetCache(r, m, 1, 1), path(p) {
    InitializeCommon();
  }
  Path GetPath() const override { return path; }
  bool IsGammaFormatHostStorageSeparate() const override { return false; }
  uint32_t GetMaxRenderTargetWidth() const override { return 16384; }
  uint32_t GetMaxRenderTargetHeight() const override { return 16384; }
  bool
  IsHostDepthEncodingDifferent(xenos::DepthRenderTargetFormat) const override {
    return true;
  }
  struct Resource : RenderTarget {
    Resource(Key k) : RenderTarget(k) {}
  };
  RenderTarget *CreateRenderTarget(Key k) override {
    ++creates;
    return new Resource(k);
  }
  void RequestPixelShaderInterlockBarrier() override {
    ++barriers;
    PixelShaderInterlockFullEdramBarrierPlaced();
  }
  unsigned Transfers() const {
    unsigned n = 0;
    for (unsigned i = 0; i < 5; ++i)
      n += last_update_transfers()[i].size();
    return n;
  }
  Key Bound(unsigned i) const {
    auto *r = last_update_accumulated_render_targets()[i];
    return r ? r->key() : Key();
  }
#ifndef X360N_EDRAM_WINDOW_LEGACY
  static uint32_t Rectangles(unsigned a, unsigned z, Key k, Rect *r) {
    return Transfer::GetRangeRectangles(a, z, k, r);
  }
#endif
};
void Setup(RegisterFile &r, xenos::MsaaSamples msaa, int x, int y,
           bool b64 = false) {
  std::memset(r.values, 0, sizeof(r.values));
  reg::RB_SURFACE_INFO s{};
  s.surface_pitch = 1280;
  s.msaa_samples = msaa;
  Set(r, s);
  reg::PA_SU_SC_MODE_CNTL mode{};
  mode.vtx_window_offset_enable = 1;
  Set(r, mode);
  reg::PA_SC_WINDOW_OFFSET w{};
  w.window_x_offset = x;
  w.window_y_offset = y;
  Set(r, w);
  reg::PA_SC_WINDOW_SCISSOR_TL tl{};
  Set(r, tl);
  reg::PA_SC_WINDOW_SCISSOR_BR br{};
  br.br_x = 1280;
  br.br_y = 128;
  Set(r, br);
  reg::PA_SC_SCREEN_SCISSOR_BR screen{};
  screen.br_x = 8192;
  screen.br_y = 8192;
  Set(r, screen);
  reg::PA_CL_VTE_CNTL v{};
  v.vport_x_scale_ena = v.vport_x_offset_ena = 1;
  v.vport_y_scale_ena = v.vport_y_offset_ena = 1;
  Set(r, v);
  F(r, XE_GPU_REG_PA_CL_VPORT_XSCALE, 640.f);
  F(r, XE_GPU_REG_PA_CL_VPORT_XOFFSET, 640.f);
  F(r, XE_GPU_REG_PA_CL_VPORT_YSCALE, -64.f);
  F(r, XE_GPU_REG_PA_CL_VPORT_YOFFSET, 64.f);
  reg::RB_COLOR_INFO c{};
  c.color_base = 1024;
  c.color_format = b64 ? xenos::ColorRenderTargetFormat::k_16_16_16_16
                       : xenos::ColorRenderTargetFormat::k_8_8_8_8;
  Set(r, c);
  reg::RB_DEPTH_INFO d{};
  d.depth_base = 0;
  Set(r, d);
}
int main(int argc, char **) {
  int checks = 0, failures = 0;
  auto check = [&](bool ok, const char *n) {
    ++checks;
    if (!ok) {
      ++failures;
      std::cerr << "FAIL " << n << '\n';
    }
  };
  rex::memory::Memory memory;
  Shader shader(xenos::ShaderType::kVertex, 0, nullptr, 0, std::endian::native);
  rex::string::StringBuffer dis;
  shader.AnalyzeUcode(dis);
  reg::RB_DEPTHCONTROL depth{};
  depth.z_enable = 1;
  depth.z_write_enable = 1;
#ifndef X360N_EDRAM_WINDOW_LEGACY
  for (auto msaa : {xenos::MsaaSamples::k1X, xenos::MsaaSamples::k2X,
                    xenos::MsaaSamples::k4X}) {
    const int tw = msaa == xenos::MsaaSamples::k4X ? 40 : 80;
    const int th = msaa == xenos::MsaaSamples::k1X ? 16 : 8;
    const int pitch = 1280 / tw;
    for (bool host : {false, true})
      for (bool b64 : {false, true}) {
        depth.stencil_enable = b64;
        RegisterFile r;
        Setup(r, msaa, 0, -th, b64);
        bool kept = false;
        check(draw_util::GetWindowOffsetTiles(r, depth, 15, host, kept) ==
                      -pitch &&
                  !kept,
              "vertical whole tile");
        auto original_color = r[reg::RB_COLOR_INFO::register_index];
        Cache cache(r, memory,
                    host ? Cache::Path::kHostRenderTargets
                         : Cache::Path::kPixelShaderInterlock);
        check(cache.GetWindowOffsetTiles(r, depth, 15, 1) == -pitch,
              "cache offset decision");
        check(cache.Update(true, depth, 15, shader, -pitch),
              "real cache update");
        check(r[reg::RB_COLOR_INFO::register_index] == original_color,
              "guest base preserved");
        if (host) {
          check(cache.Bound(0).base_tiles == unsigned(2048 - pitch),
                "depth base wraps");
          check(cache.Bound(1).base_tiles ==
                    unsigned(1024 - pitch * (b64 ? 2 : 1)),
                "32/64bpp host base");
          check(cache.Transfers() == 0, "first ownership has no copies");
          const unsigned creates = cache.creates;
          check(cache.Update(true, depth, 15, shader, -pitch) &&
                    cache.Transfers() == 0 && cache.creates == creates,
                "stable pass reuses targets without transfer");
          cache.BeginFrame();
          check(cache.Update(true, depth, 15, shader, 0) &&
                    cache.Transfers() > 0,
                "switch to no offset transfers ownership");
        } else {
          unsigned barriers = cache.barriers;
          check(cache.Update(true, depth, 15, shader, -pitch) &&
                    cache.barriers == barriers,
                "stable interlock mapping needs no extra barrier");
          check(cache.Update(true, depth, 15, shader, 0) &&
                    cache.barriers > barriers,
                "interlock mapping transition emits barrier");
        }
        draw_util::Scissor a{}, b{};
        draw_util::GetScissor(r, a, true, false);
        draw_util::GetScissor(r, b, true, true);
        check(a.extent[0] == b.extent[0] && a.extent[1] == b.extent[1] &&
                  b.offset[1] == a.offset[1] + th,
              "scissor offset applied once");
        DrawExtentEstimator extent(r, memory);
        check(extent.EstimateMaxY(false, shader, true) ==
                  extent.EstimateMaxY(false, shader, false) + th,
              "extent follows transformed scissor/viewport");
        draw_util::ViewportInfo va{}, vb{};
        draw_util::GetHostViewportInfo(r, 1, 1, false, 16384, 16384, false,
                                       depth, false, false, false, va, true,
                                       false);
        draw_util::GetHostViewportInfo(r, 1, 1, false, 16384, 16384, false,
                                       depth, false, false, false, vb, true,
                                       true);
        auto center = [](const auto &v) {
          return v.xy_offset[1] +
                 v.xy_extent[1] * (0.5 + v.ndc_offset[1] * 0.5);
        };
        check(std::abs(center(vb) - center(va) - th) < .001,
              "viewport offset removed once");
        Setup(r, msaa, -tw, 0, b64);
        check(draw_util::GetWindowOffsetTiles(r, depth, 15, host, kept) ==
                  (host ? 0 : -1),
              "horizontal tiles only on ROV/FSI");
        for (int y : {0, th, -th + 1}) {
          Setup(r, msaa, 0, y, b64);
          check(draw_util::GetWindowOffsetTiles(r, depth, 15, host, kept) == 0,
                "zero/positive/nonintegral offset stays in viewport");
        }
      }
    RegisterFile r;
    Setup(r, msaa, 0, -th);
    Cache cache(r, memory, Cache::Path::kHostRenderTargets);
    auto br = r.Get<reg::PA_SC_WINDOW_SCISSOR_BR>();
    br.br_y = 4096;
    Set(r, br);
    check(cache.GetWindowOffsetTiles(r, depth, 15, 1) == 0,
          "full-screen depth prepass refuses oversized host period");
    F(r, XE_GPU_REG_PA_CL_VPORT_YOFFSET, 2048.f);
    F(r, XE_GPU_REG_PA_CL_VPORT_YSCALE, -2048.f);
    check(cache.Update(true, depth, 0, shader, 0),
          "full depth/stencil prepass updates ownership");
    br.br_y = 128;
    Set(r, br);
    F(r, XE_GPU_REG_PA_CL_VPORT_YOFFSET, 64.f);
    F(r, XE_GPU_REG_PA_CL_VPORT_YSCALE, -64.f);
    check(cache.GetWindowOffsetTiles(r, depth, 15, 1) == 0 &&
              cache.GetWindowOffsetTiles(r, depth, 15, 2) == 0,
          "tile keeps prepass decision across passes and frames");
    check(cache.Update(true, depth, 15, shader, 0) && cache.Transfers() > 0,
          "subsequent color pass transfers actual prepass ownership");
    check(cache.GetWindowOffsetTiles(r, depth, 15, 4) == -pitch,
          "decision expires after unused frame");
    auto tl = r.Get<reg::PA_SC_WINDOW_SCISSOR_TL>();
    tl.window_offset_disable = 1;
    Set(r, tl);
    bool kept = false;
    check(draw_util::GetWindowOffsetTiles(r, depth, 15, false, kept) == 0 &&
              kept,
          "disabled window scissor refuses transform");
  }
  // Transfer coordinates include the wrapped tail and the second 64bpp period.
  Cache::Key key;
  key.base_tiles = 2040;
  key.pitch_tiles_at_32bpp = 16;
  key.msaa_samples = xenos::MsaaSamples::k1X;
  Cache::Rect rects[12]{};
  check(Cache::Rectangles(0, 8, key, rects) == 1 && rects[0].x_pixels == 640 &&
            rects[0].y_pixels == 0,
        "wrapped transfer rectangles");
  key.base_tiles = 0;
  key.resource_format = uint32_t(xenos::ColorRenderTargetFormat::k_16_16_16_16);
  key.next_period = 1;
  check(Cache::Rectangles(0, 32, key, rects) == 1 &&
            rects[0].y_pixels == 1024 && rects[0].width_pixels == 1280,
        "64bpp next-period transfer");
  check(draw_util::AddWindowOffsetToEdramBase(0, -1, false) == 2047 &&
            draw_util::AddWindowOffsetToEdramBase(0, -1, true) == 2046,
        "signed wrapping and 64bpp multiplier");
  {
    RegisterFile r;
    Setup(r, xenos::MsaaSamples::k4X, 0, -8, true);
    auto d = r.Get<reg::RB_DEPTH_INFO>();
    d.depth_base = 100;
    Set(r, d);
    auto c = r.Get<reg::RB_COLOR_INFO>();
    c.color_base = 132;
    Set(r, c);
    Cache cache(r, memory, Cache::Path::kHostRenderTargets);
    check(cache.Update(true, depth, 15, shader, -32) &&
              cache.GetLastUpdateBoundRenderTargets() == 3,
          "different guest bases survive an adjusted 32/64bpp base collision");
    c.color_base = 100;
    Set(r, c);
    cache.BeginFrame();
    check(cache.Update(true, depth, 15, shader, -32) &&
              cache.GetLastUpdateBoundRenderTargets() == 2,
          "original guest color/depth alias keeps existing precedence");
  }
  {
    RegisterFile r;
    Setup(r, xenos::MsaaSamples::k4X, 0, -8, true);
    auto tl = r.Get<reg::PA_SC_WINDOW_SCISSOR_TL>();
    tl.tl_y = 256;
    Set(r, tl);
    auto br = r.Get<reg::PA_SC_WINDOW_SCISSOR_BR>();
    br.br_y = 264;
    Set(r, br);
    F(r, XE_GPU_REG_PA_CL_VPORT_YOFFSET, 260);
    F(r, XE_GPU_REG_PA_CL_VPORT_YSCALE, -4);
    bool kept = false;
    check(draw_util::GetWindowOffsetTiles(r, depth, 15, true, kept) == -32,
          "64bpp scissor wholly in next period is eligible");
    Cache cache(r, memory, Cache::Path::kHostRenderTargets);
    check(cache.Update(true, depth, 15, shader, -32) &&
              cache.Bound(1).next_period == 1,
          "real host ownership key selects the second 64bpp period");
    tl.tl_y = 248;
    Set(r, tl);
    check(draw_util::GetWindowOffsetTiles(r, depth, 15, true, kept) == 0 &&
              kept,
          "64bpp host period crossing stays in viewport");
    check(draw_util::GetWindowOffsetTiles(r, depth, 0, true, kept) == -32,
          "depth-only prepass supports the same 32bpp span");
  }
#endif
  if (argc > 1) {
    // Identical input for the legacy control and the current benchmark,
    // independent of the depth/stencil combinations exercised above.
    depth.stencil_enable = 0;
    RegisterFile r;
#ifdef X360N_EDRAM_WINDOW_LEGACY
    for (int scenario : {0})
      for (bool host : {false, true}) {
#else
    for (int scenario : {0, 1, 2})
      for (bool host : {false, true}) {
#endif
        Setup(r, xenos::MsaaSamples::k4X, 0, scenario ? -8 : 0);
        Cache c(r, memory,
                host ? Cache::Path::kHostRenderTargets
                     : Cache::Path::kPixelShaderInterlock);
        std::vector<double> samples;
        unsigned transfers = 0;
        uint64_t checksum = 0;
        for (int batch = 0; batch < 210; ++batch) {
          auto begin = std::chrono::steady_clock::now();
          for (int i = 0; i < 1000; ++i) {
            if (scenario == 2) {
              reg::PA_SC_WINDOW_OFFSET window{};
              window.window_y_offset = i & 1 ? -8 : 0;
              Set(r, window);
            }
#ifdef X360N_EDRAM_WINDOW_LEGACY
            auto offset = 0;
#else
            auto offset = c.GetWindowOffsetTiles(r, depth, 15, 1);
#endif
            checksum += unsigned(offset);
#ifdef X360N_EDRAM_WINDOW_LEGACY
            c.Update(true, depth, 15, shader);
#else
            c.Update(true, depth, 15, shader, offset);
#endif
            if (host)
              transfers += c.Transfers();
          }
          auto ns = std::chrono::duration<double, std::nano>(
                        std::chrono::steady_clock::now() - begin)
                        .count() /
                    1000;
          if (batch >= 10)
            samples.push_back(ns);
        }
        std::sort(samples.begin(), samples.end());
        std::cout << (host ? "RTV/FBO" : "ROV/FSI") << " scenario="
                  << (scenario == 0   ? "zero"
                      : scenario == 1 ? "stable-tile"
                                      : "alternating")
                  << " CPU offset+cache ns/draw p50=" << samples[100]
                  << " p95=" << samples[190] << " p99=" << samples[198]
                  << " resources=" << c.creates << " transfers=" << transfers
                  << " barriers=" << c.barriers << " checksum=" << checksum
                  << '\n';
      }
  }
  std::cout << checks - failures << '/' << checks << " checks passed" << std::endl;
  return failures ? 1 : 0;
}
