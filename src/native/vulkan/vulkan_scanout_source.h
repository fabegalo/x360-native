#pragma once
#include <string_view>
namespace x360n::native {
inline constexpr std::string_view kNativeScanoutVertex = R"glsl(#version 450
void main() {
  vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)glsl";

// The same guest scanout contract as shaders/scanout.hlsl, expressed as a
// fragment shader because the runtime's Vulkan guest image is an attachment,
// not a storage image. Xenos address permutation derives from Xenia (Copyright
// 2022 Ben Vanik, BSD-3-Clause), adapted by Tom Clay in ReXGlue, 2026.
inline constexpr std::string_view kNativeScanoutFragment = R"glsl(#version 450
layout(set = 0, binding = 0, std430) readonly buffer Pixels { uint pixels[]; };
layout(set = 0, binding = 1, std430) readonly buffer Lut { uint lut[]; };
layout(set = 0, binding = 2, std430) readonly buffer Overlay { uint overlay_pixels[]; };
layout(push_constant) uniform Parameters {
  uint width, height, pitch, tiled;
  uint endian, swizzle, rgb10, unused1;
  uint overlay_enabled, overlay_width, overlay_height, overlay_pitch;
  uint overlay_tiled, overlay_endian, overlay_swizzle, overlay_rgb10;
  uvec4 overlay_rect;
  uvec2 overlay_offset;
  vec2 screen_scale;
};
layout(location = 0) out vec4 output_color;
uint GuestWordOffset(uvec2 p, uint row_pitch, uint is_tiled) {
  uint result = (p.y * row_pitch + p.x) * 4;
  if (is_tiled != 0) {
    uint macro = ((p.x >> 5) + (p.y >> 5) * (row_pitch >> 5)) << 9;
    uint micro = ((p.x & 7) + ((p.y & 14) << 2)) << 2;
    uint offset = macro + ((micro & ~15u) << 1) + (micro & 15) + ((p.y & 1) << 4);
    result = ((offset & ~511u) << 3) + ((p.y & 16) << 7) + ((offset & 448) << 2) +
             (((((p.y & 8) >> 2) + (p.x >> 3)) & 3) << 6) + (offset & 63);
  }
  return result >> 2;
}
uint Swap(uint v, uint mode) {
  if (mode == 1 || mode == 2) v = ((v & 0x00ff00ffu) << 8) | ((v >> 8) & 0x00ff00ffu);
  if (mode == 2 || mode == 3) v = (v << 16) | (v >> 16);
  return v;
}
float channel(vec4 color, uint index) {
  return index < 4 ? color[index] : (index == 5 ? 1.0 : 0.0);
}
float pwl(uint value, uint component) {
  uint entry = lut[(value >> 3) * 3 + component];
  return clamp((float(entry & 65535u) + float(entry >> 16) * float(value & 7) * 0.125) / 65472.0, 0.0, 1.0);
}
vec4 OverlayTexel(ivec2 p) {
  uint v = Swap(overlay_pixels[GuestWordOffset(uvec2(p), overlay_pitch, overlay_tiled)], overlay_endian);
  vec4 color = overlay_rgb10 != 0
      ? vec4(v & 1023, (v >> 10) & 1023, (v >> 20) & 1023, (v >> 30) * 341) / 1023.0
      : vec4(v & 255, (v >> 8) & 255, (v >> 16) & 255, v >> 24) / 255.0;
  return vec4(channel(color, overlay_swizzle & 7), channel(color, (overlay_swizzle >> 3) & 7),
              channel(color, (overlay_swizzle >> 6) & 7), channel(color, (overlay_swizzle >> 9) & 7));
}
uvec3 ComposedIndex(uvec2 id, vec3 color, float scale) {
  vec2 screen = (vec2(id) + 0.5) * screen_scale;
  if (all(greaterThanEqual(screen, vec2(overlay_rect.xy))) && all(lessThan(screen, vec2(overlay_rect.zw)))) {
    ivec2 p = ivec2(screen - vec2(overlay_offset));
    if (all(greaterThanEqual(p, ivec2(0))) && all(lessThan(p, ivec2(overlay_width, overlay_height)))) {
      vec4 plane = OverlayTexel(p);
      color = mix(color, plane.rgb, plane.a);
    }
  }
  // Xenos composes through FP16 before indexing its gamma pass.
  color = vec3(unpackHalf2x16(packHalf2x16(color.xy)),
               unpackHalf2x16(packHalf2x16(vec2(color.z, 0.0))).x);
  return uvec3(color * scale + 0.5);
}
void main() {
  uvec2 id = uvec2(gl_FragCoord.xy);
  uint v = Swap(pixels[GuestWordOffset(id, pitch, tiled)], endian);
  if (rgb10 != 0) {
    vec4 color = vec4(v & 1023, (v >> 10) & 1023, (v >> 20) & 1023, (v >> 30) * 341);
    uvec3 index = uvec3(channel(color, swizzle & 7), channel(color, (swizzle >> 3) & 7),
                       channel(color, (swizzle >> 6) & 7));
    if ((swizzle & 7) == 5) index.x = 1023;
    if (((swizzle >> 3) & 7) == 5) index.y = 1023;
    if (((swizzle >> 6) & 7) == 5) index.z = 1023;
    if (overlay_enabled != 0) index = ComposedIndex(id, vec3(index) / 1023.0, 1023.0);
    output_color = vec4(pwl(index.x, 0), pwl(index.y, 1), pwl(index.z, 2), 1.0);
  } else {
    vec4 color = vec4(v & 255, (v >> 8) & 255, (v >> 16) & 255, v >> 24);
    uvec3 index = uvec3(channel(color, swizzle & 7), channel(color, (swizzle >> 3) & 7),
                       channel(color, (swizzle >> 6) & 7));
    if ((swizzle & 7) == 5) index.x = 255;
    if (((swizzle >> 3) & 7) == 5) index.y = 255;
    if (((swizzle >> 6) & 7) == 5) index.z = 255;
    if (overlay_enabled != 0) index = ComposedIndex(id, vec3(index) / 255.0, 255.0);
    output_color = vec4(vec3(lut[index.x] & 1023, (lut[index.y] >> 10) & 1023,
                            (lut[index.z] >> 20) & 1023) / 1023.0, 1.0);
  }
}
)glsl";
}
