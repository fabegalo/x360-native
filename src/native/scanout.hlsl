// Native scanout, not a replacement guest shader. Xenos address permutation
// follows texture/util.cpp in the pinned SDK. Address permutation derived from
// Xenia, Copyright 2022 Ben Vanik, BSD-3-Clause (see third-party notices).
// SDK adaptation: Tom Clay, 2026. Native HLSL adaptation: x360-native, 2026.
ByteAddressBuffer pixels : register(t0);
StructuredBuffer<uint> lut : register(t1);
RWTexture2D<float4> output_image : register(u0);
cbuffer Parameters : register(b0) {
  uint width, height, pitch, tiled;
  uint endian, swizzle, unused0, unused1;
};
uint GuestByteOffset(uint2 p) {
  uint result = (p.y * pitch + p.x) * 4;
  if (tiled != 0) {
    uint macro = ((p.x >> 5) + (p.y >> 5) * (pitch >> 5)) << 9;
    uint micro = ((p.x & 7) + ((p.y & 14) << 2)) << 2;
    uint offset = macro + ((micro & ~15) << 1) + (micro & 15) + ((p.y & 1) << 4);
    result = ((offset & ~511) << 3) + ((p.y & 16) << 7) + ((offset & 448) << 2) +
             (((((p.y & 8) >> 2) + (p.x >> 3)) & 3) << 6) + (offset & 63);
  }
  return result;
}
float channel(float4 c, uint s) {
  return s < 4 ? c[s] : (s == 5 ? 1.0 : 0.0);
}
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= width || id.y >= height) return;
  uint v = pixels.Load(GuestByteOffset(id.xy));
  if (endian == 1 || endian == 2) v = ((v & 0x00ff00ff) << 8) | ((v >> 8) & 0x00ff00ff);
  if (endian == 2 || endian == 3) v = (v << 16) | (v >> 16);
  float4 c = float4(v & 255, (v >> 8) & 255, (v >> 16) & 255, v >> 24);
  uint3 index = uint3(channel(c, swizzle & 7), channel(c, (swizzle >> 3) & 7),
                     channel(c, (swizzle >> 6) & 7));
  // Constant-one swizzle must address the last LUT entry, not index one.
  if ((swizzle & 7) == 5) index.x = 255;
  if (((swizzle >> 3) & 7) == 5) index.y = 255;
  if (((swizzle >> 6) & 7) == 5) index.z = 255;
  output_image[id.xy] = float4(float3(lut[index.x] & 1023,
      (lut[index.y] >> 10) & 1023, (lut[index.z] >> 20) & 1023) / 1023.0, 1.0);
}
