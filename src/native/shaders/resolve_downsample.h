#pragma once
namespace x360n::native {
// Each invocation owns an entire destination dword, including 8/16-bit formats.
// Reading one canonical subpixel preserves guest representation/endian exactly.
// Pinned scaled resolves interleave X blocks of 8 bytes (8bpp) or 16 bytes
// (16..128bpp), then Y subpixels. See ScaledResolveByteOffset for the same CPU
// mapping; per-pixel replication would reorder the image after a resolve.
inline constexpr char kResolveDownsampleHlsl[] = R"(
cbuffer Constants : register(b0) { uint byte_count, pixel_log2, scale_x, scale_y, source_base, dest_base; };
ByteAddressBuffer source_buffer : register(t0);
RWByteAddressBuffer dest_buffer : register(u0);
[numthreads(64,1,1)] void main(uint3 id : SV_DispatchThreadID) {
  uint address=id.x*4; if(address>=byte_count) return;
  uint pixel_size=1u<<pixel_log2, block_size=pixel_log2!=0?16u:8u, result=0;
  uint pixels_per_block=block_size>>pixel_log2;
  [unroll] for(uint b=0;b<4;++b) {
    uint guest_byte=address+b;
    uint host_x=((guest_byte&(block_size-1))>>pixel_log2)*scale_x;
    uint offset=source_base+(guest_byte&~(block_size-1))*scale_x*scale_y+
        (host_x/pixels_per_block)*scale_y*block_size+(host_x%pixels_per_block)*pixel_size+(guest_byte&(pixel_size-1));
    result|=((source_buffer.Load(offset&~3u)>>((offset&3u)*8))&255u)<<(b*8);
  }
  dest_buffer.Store(dest_base+address,result);
}
)";
inline constexpr char kResolveDownsampleGlsl[] = R"(#version 450
layout(local_size_x=64) in;
layout(push_constant) uniform Constants { uint byte_count; uint pixel_log2; uint scale_x; uint scale_y; uint source_base; uint dest_base; } c;
layout(std430,set=0,binding=0) readonly buffer Source { uint data[]; } source_buffer;
layout(std430,set=1,binding=0) writeonly buffer Destination { uint data[]; } dest_buffer;
void main() {
  uint address=gl_GlobalInvocationID.x*4u; if(address>=c.byte_count) return;
  uint pixel_size=1u<<c.pixel_log2,block_size=c.pixel_log2!=0u?16u:8u,result=0u;
  uint pixels_per_block=block_size>>c.pixel_log2;
  for(uint b=0u;b<4u;++b) {
    uint guest_byte=address+b;
    uint host_x=((guest_byte&(block_size-1u))>>c.pixel_log2)*c.scale_x;
    uint offset=c.source_base+(guest_byte&~(block_size-1u))*c.scale_x*c.scale_y+
        (host_x/pixels_per_block)*c.scale_y*block_size+(host_x%pixels_per_block)*pixel_size+(guest_byte&(pixel_size-1u));
    result|=((source_buffer.data[offset>>2u]>>((offset&3u)*8u))&255u)<<(b*8u);
  }
  dest_buffer.data[(c.dest_base+address)>>2u]=result;
}
)";
}
