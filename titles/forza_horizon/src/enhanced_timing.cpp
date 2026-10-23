// Timing algorithms adapted from Forza Horizon 1: ReImagined cb676f7 (MIT).
// Copyright (c) 2026 Dyotson and Forza Horizon 1: ReImagined contributors.
// See LICENSE-ReImagined. Addresses below were checked against this profile's
// 0.0.0.10 AOT (SHA-256 db40df605ade49a612b35a7a24c38f6004bcb17a88ed6b48288de16df9e3987c).
// No other title, revision or renderer installs these semantics.
#include "enhanced_timing.h"
#include <rex/chrono/clock.h>
#include <rex/chrono/frame_timing.h>
#include <rex/chrono/frame_layout.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <mutex>
#include <unordered_map>

namespace {
using Timing = rex::chrono::FrameTiming;
using Clock = rex::chrono::Clock;
bool enabled = false;  // Published before the guest threads start.
bool layout_enabled = false;
uint32_t frame_rate = 30;
std::atomic<uint64_t> waiting{0};
uint32_t Load32(const uint8_t* base, uint32_t a) {
  return uint32_t(base[a]) << 24 | uint32_t(base[a+1]) << 16 |
         uint32_t(base[a+2]) << 8 | base[a+3];
}
void Store32(uint8_t* base, uint32_t a, uint32_t v) {
  base[a] = uint8_t(v >> 24); base[a+1] = uint8_t(v >> 16);
  base[a+2] = uint8_t(v >> 8); base[a+3] = uint8_t(v);
}
struct Vblank { uint64_t count = 0, actual = 0, nominal = 0; };
std::mutex vblank_mutex;
Vblank vblank;
void OnVblank() {
  const uint64_t now = Clock::QueryGuestTickCount();
  const uint64_t interval = Clock::guest_tick_frequency() / (2 * frame_rate);
  std::lock_guard lock(vblank_mutex);
  uint64_t nominal = vblank.count ? vblank.nominal + interval : now;
  if (now < nominal) nominal = now;
  else if (now - nominal >= interval) nominal += (now - nominal) / interval * interval;
  vblank = {vblank.count + 1, now, nominal};
}
// Frame timestamps telescope to elapsed guest time; a late frame gets its real
// duration. Never speed up the system/PPC/audio clocks to reach a selected FPS.
bool Delta(double& dt) {
  static uint64_t previous = 0, seen = 0;
  const uint64_t now = Clock::QueryGuestTickCount(), freq = Clock::guest_tick_frequency();
  Vblank current;
  { std::lock_guard lock(vblank_mutex); current = vblank; }
  const uint64_t window = std::min(freq / 500, freq / (4 * frame_rate));
  uint64_t stamp = current.count != seen && now >= current.actual &&
      now - current.actual <= window ? current.nominal : now;
  seen = current.count;
  if (!previous) { previous = stamp; return false; }
  stamp = std::max(stamp, previous + std::max(uint64_t(1), freq / 10000));
  dt = double(stamp - previous) / double(freq);
  previous = stamp;
  return dt <= 4.0;  // Preserve the original load/pause rule beyond four seconds.
}
void WaitForRender() {
  static uint64_t seen = 0, wait_id = 0;
  const auto source = Timing::ReadSource();
  const uint64_t now = Clock::QueryGuestTickCount(), freq = Clock::guest_tick_frequency();
  if (source.interval > 1.15 * double(freq) / frame_rate &&
      now >= source.tick && now - source.tick < freq / 10 && source.sequence == seen) {
    waiting.store(++wait_id, std::memory_order_release);
    Timing::WaitSource(source, 50);
    waiting.store(0, std::memory_order_release);
  }
  seen = Timing::ReadSource().sequence;
}
double AnimatedDelta(uint32_t camera, double original) {
  static std::mutex mutex;
  static std::unordered_map<uint32_t, uint64_t> clocks;
  const uint64_t now = Clock::QueryGuestTickCount();
  std::lock_guard lock(mutex);
  if (clocks.size() > 64) clocks.clear();
  auto [it, inserted] = clocks.try_emplace(camera, now);
  const double dt = now >= it->second ? double(now - it->second) / Clock::guest_tick_frequency() : 1.0;
  it->second = now;
  return inserted || dt > 0.25 ? original : dt;
}
}
namespace x360n::forza {
void ConfigureEnhancedTiming() {
  layout_enabled = REXCVAR_GET(native_enhanced) && REXCVAR_GET(native_ultrawide) &&
      rex::cvar::GetFlagByName("gpu_plugin") == "native";
  enabled = REXCVAR_GET(native_enhanced) &&
      rex::cvar::GetFlagByName("gpu_plugin") == "native" && REXCVAR_GET(native_framerate) > 30;
  if (!enabled) return;
  frame_rate = REXCVAR_GET(native_framerate);
  Timing::SetGuestVblankRate(2 * frame_rate);
  REXLOG_INFO("Native Enhanced: Forza 4D5309C9/2DC7007B/0.0.0.10 target {} FPS; guest vblank {} Hz; elapsed-time simulation/camera/crowd hooks enabled; system/audio time unchanged", frame_rate, 2 * frame_rate);
}
void ResizeEnhancedLayout(uint32_t width, uint32_t height) {
  // Preserve the original 16:9 framing on narrower windows. The presenter
  // letterboxes it; ultrawide preserves vertical FOV and a centered 16:9 UI.
  if (layout_enabled && width && height && uint64_t(width) * 9 > uint64_t(height) * 16)
    rex::chrono::FrameLayout::SetDisplayAspect(width, height);
  else rex::chrono::FrameLayout::SetDisplayAspect(0, 0);
}
}

extern "C" REX_FUNC(__imp__sub_823E22C8);
extern "C" REX_FUNC(sub_823E22C8) {
  const uint32_t viewport = ctx.r3.u32;
  __imp__sub_823E22C8(ctx, base);
  if (!layout_enabled) return;
  const auto aspect = rex::chrono::FrameLayout::DisplayAspect();
  if (!aspect.x || viewport > 0xFFFFFFEFu ||
      Load32(base, viewport + 8) != 1280 || Load32(base, viewport + 12) != 720) return;
  // This guest getter feeds projection and frustum construction (including
  // 829A48F4 gameplay and 8263AE18 map). Smaller shadow/reflection viewports
  // retain their own original aspect. No renderer camera classification.
  ctx.f1.f64 *= (double(aspect.x) / aspect.y) / (16.0 / 9.0);
}

// midasm_hook at 825E6F08, immediately after lvx v56,[82028BA0].
// The guest creates its UI orthographic projection diag(2/1280,2/720,0.0001)
// and passes it to the UI renderer at 825E7020. VMX host lanes are reversed:
// guest X is f32[3]. Changing only that coefficient preserves vertical scale,
// translation/depth and the complete UI's original centered safe rectangle.
void ForzaEnhancedUiProjection(PPCVRegister& v56) {
  if (!layout_enabled) return;
  const auto aspect = rex::chrono::FrameLayout::DisplayAspect();
  if (aspect.x)
    v56.f32[3] *= float((16.0 / 9.0) / (double(aspect.x) / aspect.y));
}

extern "C" REX_FUNC(__imp__sub_8247C678);
extern "C" REX_FUNC(sub_8247C678) {
  if (enabled) OnVblank();
  __imp__sub_8247C678(ctx, base);  // Preserve the original two-vblank event/divider.
}
extern "C" REX_FUNC(__imp__sub_8247C168);
extern "C" REX_FUNC(sub_8247C168) {
  const uint32_t caller = uint32_t(ctx.lr);
  __imp__sub_8247C168(ctx, base);
  if (!enabled || caller != 0x823EDB08) return;
  double dt;
  const bool precise = Delta(dt);
  WaitForRender();
  if (precise) { ctx.f31.f64 = double(float(dt)); ctx.r3.u64 = 1; }
}
extern "C" REX_FUNC(__imp__sub_8259F3E8);
extern "C" REX_FUNC(sub_8259F3E8) {
  const uint64_t wait = enabled ? waiting.load(std::memory_order_acquire) : 0;
  const uint32_t address = ctx.r3.u32 + 0x948;
  const uint32_t before = wait ? Load32(base, address) : 0;
  __imp__sub_8259F3E8(ctx, base);
  // A render pass with no main frame cannot produce a swap. Release that exact
  // main-loop wait, avoiding the persistent 18-FPS feedback stall from #187.
  if (wait && waiting.load(std::memory_order_acquire) == wait && Load32(base, address) == before)
    Timing::WakeSourceWaiters();
}
extern "C" REX_FUNC(__imp__sub_82DF7BB0);
extern "C" REX_FUNC(sub_82DF7BB0) {
  if (!enabled) { __imp__sub_82DF7BB0(ctx, base); return; }
  const uint32_t object = ctx.r3.u32;
  const double dt = ctx.f1.f64;
  if (!(dt > 0.0)) { Store32(base, object + 0x3EAE4, 0); return; }
  // Baked crowd frames are 30 Hz, independently of render-thread cadence.
  int64_t units = int32_t(Load32(base, object + 0x3EAE0)) +
      std::llround(std::min(dt, 60.0) * 3000000.0);
  units = std::max(int64_t(0), units);
  Store32(base, object + 0x3EAE4, uint32_t(units / 100000));
  Store32(base, object + 0x3EAE0, uint32_t(units % 100000));
}
extern "C" REX_FUNC(sub_8246EBB8);
extern "C" REX_FUNC(__imp__sub_82647B78);
extern "C" REX_FUNC(sub_82647B78) {
  if (enabled && uint32_t(ctx.lr) == 0x82653588 && Load32(base, ctx.r3.u32 + 0x150) == 2) {
    const uint32_t camera = ctx.r3.u32;
    PPCContext timer = ctx;
    timer.r3.u64 = Load32(base, camera + 0x70);
    sub_8246EBB8(timer, base);
    const double dt = timer.f1.f64;
    if (std::isfinite(dt) && dt >= 0.0 && dt <= 0.25) {
      constexpr double step = 0.001745329238474369, turn = 6.283185307179586;
      double angle = std::fmod(std::bit_cast<float>(Load32(base, camera + 0x80)) - step + step * 30 * dt, turn);
      if (angle < 0) angle += turn;
      Store32(base, camera + 0x80, std::bit_cast<uint32_t>(float(angle)));
    }
  }
  __imp__sub_82647B78(ctx, base);
}
extern "C" REX_FUNC(__imp__sub_82862170);
extern "C" REX_FUNC(sub_82862170) {
  if (enabled && uint32_t(ctx.lr) == 0x826499D4) ctx.f1.f64 = AnimatedDelta(ctx.r3.u32, ctx.f1.f64);
  __imp__sub_82862170(ctx, base);
}
extern "C" REX_FUNC(__imp__sub_82862CD0);
extern "C" REX_FUNC(sub_82862CD0) {
  if (enabled && uint32_t(ctx.lr) == 0x826499D4) ctx.f1.f64 = AnimatedDelta(ctx.r3.u32, ctx.f1.f64);
  __imp__sub_82862CD0(ctx, base);
}
