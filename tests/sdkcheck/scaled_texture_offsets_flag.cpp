// The shared texture option must link without any D3D12 translator sources.
#include "gpu/xenos/xenos_settings.h"

int main() {
  struct Settings final : rex::gpu::XenosSettings {} settings;
  if (!settings.draw_resolution_scaled_texture_offsets.Get()) return 1;
  if (!settings.draw_resolution_scaled_texture_offsets.Set(false)) return 2;
  if (settings.draw_resolution_scaled_texture_offsets.Get()) return 3;
  if (!settings.draw_resolution_scaled_texture_offsets.Set(true)) return 4;
  return settings.draw_resolution_scaled_texture_offsets.Get() ? 0 : 5;
}
