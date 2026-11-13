// The shared texture option must link without any D3D12 translator sources.
#include <rex/graphics/flags.h>

int main() {
  if (!REXCVAR_GET(draw_resolution_scaled_texture_offsets)) return 1;
  REXCVAR_SET(draw_resolution_scaled_texture_offsets, false);
  if (REXCVAR_GET(draw_resolution_scaled_texture_offsets)) return 2;
  REXCVAR_SET(draw_resolution_scaled_texture_offsets, true);
  return REXCVAR_GET(draw_resolution_scaled_texture_offsets) ? 0 : 3;
}
