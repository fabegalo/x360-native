#pragma once
#include <rex/xenos/state.h>
#include <rex/xenos/fetch.h>
#include <rex/xenos/ucode.h>
#include <rex/xenos/packet.h>
namespace x360n::native {
void GetNativeColorClampsAndKeepMasks(rex::xenos::ColorRenderTargetFormat format,
    uint32_t write_mask, float& clamp_rgb_low, float& clamp_alpha_low,
    float& clamp_rgb_high, float& clamp_alpha_high,
    uint32_t& keep_mask_low, uint32_t& keep_mask_high);

// Visibility probing runs before the guest pixel shader. A separate probe
// suppresses vertex stores; a query-only guest draw keeps its real VS stores.
enum class DrawPurpose { kGuest, kVisibility, kVisibilityWithVertexExports, kGuestWithoutVertexExports };
struct DrawIndices {
  rex::xenos::IndexFormat format;
  rex::xenos::Endian endianness;
  uint32_t count, guest_base, length;
};
}
