#pragma once
#include <cstdint>
namespace x360n::native {
// At 4x4, 512 KiB uses 32768 groups of 64 dwords (limit: 65535).
inline constexpr uint32_t kScaledReplicateChunkBytes = 1u << 19;
// GPU-resident scaled resolve memory (guest address * scale area, the layout
// of ScaledResolveByteOffset).
//
// Replicate: fills scaled memory of guest bytes [guest_start, guest_start +
// byte_count / area) from the canonical 1x mirror. Each invocation owns one
// destination dword; ScaledResolveGuestByte gives the source byte of each of
// its bytes, so 8/16-bit layouts are exact too.
inline constexpr char kScaledReplicateHlsl[] = R"(
cbuffer Constants : register(b0) { uint byte_count, pixel_log2, scale_x, scale_y, guest_start, dest_base; };
ByteAddressBuffer source_buffer : register(t0);
RWByteAddressBuffer dest_buffer : register(u0);
[numthreads(64,1,1)] void main(uint3 id : SV_DispatchThreadID) {
  uint offset=id.x*4; if(offset>=byte_count) return;
  uint block=pixel_log2!=0?16u:8u, per_block=block>>pixel_log2, group_bytes=block*scale_x*scale_y, result=0;
  [unroll] for(uint b=0;b<4;++b) {
    uint o=offset+b, within=o%group_bytes, row=within/block, byte=within%block;
    uint host_x=(row/scale_y)*per_block+(byte>>pixel_log2);
    uint guest=guest_start+(o/group_bytes)*block+((host_x/scale_x)<<pixel_log2)+(byte&((1u<<pixel_log2)-1u));
    result|=((source_buffer.Load(guest&~3u)>>((guest&3u)*8))&255u)<<(b*8);
  }
  dest_buffer.Store(dest_base+offset,result);
}
)";
inline constexpr char kScaledReplicateGlsl[] = R"(#version 450
layout(local_size_x=64) in;
layout(push_constant) uniform Constants { uint byte_count; uint pixel_log2; uint scale_x; uint scale_y; uint guest_start; uint dest_base; } c;
layout(std430,set=0,binding=0) readonly buffer Source { uint data[]; } source_buffer;
layout(std430,set=1,binding=0) writeonly buffer Destination { uint data[]; } dest_buffer;
void main() {
  uint offset=gl_GlobalInvocationID.x*4u; if(offset>=c.byte_count) return;
  uint block=c.pixel_log2!=0u?16u:8u, per_block=block>>c.pixel_log2, group_bytes=block*c.scale_x*c.scale_y, result=0u;
  for(uint b=0u;b<4u;++b) {
    uint o=offset+b, within=o%group_bytes, row=within/block, byte_index=within%block;
    uint host_x=(row/c.scale_y)*per_block+(byte_index>>c.pixel_log2);
    uint guest=c.guest_start+(o/group_bytes)*block+((host_x/c.scale_x)<<c.pixel_log2)+(byte_index&((1u<<c.pixel_log2)-1u));
    result|=((source_buffer.data[guest>>2u]>>((guest&3u)*8u))&255u)<<(b*8u);
  }
  dest_buffer.data[(c.dest_base+offset)>>2u]=result;
}
)";
// Scanout: the scaled 32bpp front buffer as a linear surface with the original
// endian/swizzle (scanout.hlsl decodes it like the 1x guest bytes). Source is
// a view starting at the front buffer's scaled base. Xenos tiling as in
// scanout.hlsl (texture/util.cpp of the pinned SDK; Xenia, BSD-3-Clause).
inline constexpr char kScaledScanoutHlsl[] = R"(
cbuffer Constants : register(b0) { uint width, height, out_pitch, in_pitch, tiled, scale_x, scale_y; };
ByteAddressBuffer source_buffer : register(t0);
RWByteAddressBuffer dest_buffer : register(u0);
uint GuestByteOffset(uint2 p) {
  uint result = (p.y * in_pitch + p.x) * 4;
  if (tiled != 0) {
    uint macro = ((p.x >> 5) + (p.y >> 5) * (in_pitch >> 5)) << 9;
    uint micro = ((p.x & 7) + ((p.y & 14) << 2)) << 2;
    uint offset = macro + ((micro & ~15) << 1) + (micro & 15) + ((p.y & 1) << 4);
    result = ((offset & ~511) << 3) + ((p.y & 16) << 7) + ((offset & 448) << 2) +
             (((((p.y & 8) >> 2) + (p.x >> 3)) & 3) << 6) + (offset & 63);
  }
  return result;
}
[numthreads(8,8,1)] void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= width || id.y >= height) return;
  uint guest = GuestByteOffset(uint2(id.x / scale_x, id.y / scale_y));
  uint host_x = ((guest & 15u) >> 2) * scale_x + id.x % scale_x;
  uint scaled = (guest & ~15u) * scale_x * scale_y + ((host_x >> 2) * scale_y + id.y % scale_y) * 16u + (host_x & 3u) * 4u;
  dest_buffer.Store((id.y * out_pitch + id.x) * 4, source_buffer.Load(scaled));
}
)";
inline constexpr char kScaledScanoutGlsl[] = R"(#version 450
layout(local_size_x=8,local_size_y=8) in;
layout(push_constant) uniform Constants { uint width; uint height; uint out_pitch; uint in_pitch; uint tiled; uint scale_x; uint scale_y; uint source_base; } c;
layout(std430,set=0,binding=0) readonly buffer Source { uint data[]; } source_buffer;
layout(std430,set=1,binding=0) writeonly buffer Destination { uint data[]; } dest_buffer;
uint GuestByteOffset(uvec2 p) {
  uint result = (p.y * c.in_pitch + p.x) * 4u;
  if (c.tiled != 0u) {
    uint macro = ((p.x >> 5) + (p.y >> 5) * (c.in_pitch >> 5)) << 9;
    uint micro = ((p.x & 7u) + ((p.y & 14u) << 2)) << 2;
    uint offset = macro + ((micro & ~15u) << 1) + (micro & 15u) + ((p.y & 1u) << 4);
    result = ((offset & ~511u) << 3) + ((p.y & 16u) << 7) + ((offset & 448u) << 2) +
             (((((p.y & 8u) >> 2) + (p.x >> 3)) & 3u) << 6) + (offset & 63u);
  }
  return result;
}
void main() {
  uvec2 id = gl_GlobalInvocationID.xy;
  if (id.x >= c.width || id.y >= c.height) return;
  uint guest = GuestByteOffset(uvec2(id.x / c.scale_x, id.y / c.scale_y));
  uint host_x = ((guest & 15u) >> 2) * c.scale_x + id.x % c.scale_x;
  uint scaled = c.source_base + (guest & ~15u) * c.scale_x * c.scale_y + ((host_x >> 2) * c.scale_y + id.y % c.scale_y) * 16u + (host_x & 3u) * 4u;
  dest_buffer.data[id.y * c.out_pitch + id.x] = source_buffer.data[scaled >> 2u];
}
)";
}
