#pragma once
namespace x360n::native {
// Each invocation owns an entire destination dword, including 8/16-bit formats.
// Reading one canonical subpixel preserves guest representation/endian exactly.
inline constexpr char kResolveDownsampleHlsl[] = R"(
cbuffer Constants : register(b0) { uint byte_count, pixel_log2, area, sample_index, source_base, dest_base; };
ByteAddressBuffer source_buffer : register(t0);
RWByteAddressBuffer dest_buffer : register(u0);
[numthreads(64,1,1)] void main(uint3 id : SV_DispatchThreadID) {
  uint address=id.x*4; if(address>=byte_count) return;
  uint pixel_mask=(1u<<pixel_log2)-1, result=0;
  [unroll] for(uint b=0;b<4;++b) {
    uint offset=source_base+(((address+b)>>pixel_log2)*area+sample_index)*(1u<<pixel_log2)+((address+b)&pixel_mask);
    result|=((source_buffer.Load(offset&~3u)>>((offset&3u)*8))&255u)<<(b*8);
  }
  dest_buffer.Store(dest_base+address,result);
}
)";
inline constexpr char kResolveDownsampleGlsl[] = R"(#version 450
layout(local_size_x=64) in;
layout(push_constant) uniform Constants { uint byte_count; uint pixel_log2; uint area; uint sample_index; uint source_base; uint dest_base; } c;
layout(std430,set=0,binding=0) readonly buffer Source { uint data[]; } source_buffer;
layout(std430,set=1,binding=0) writeonly buffer Destination { uint data[]; } dest_buffer;
void main() {
  uint address=gl_GlobalInvocationID.x*4u; if(address>=c.byte_count) return;
  uint pixel_mask=(1u<<c.pixel_log2)-1u,result=0u;
  for(uint b=0u;b<4u;++b) {
    uint offset=c.source_base+(((address+b)>>c.pixel_log2)*c.area+c.sample_index)*(1u<<c.pixel_log2)+((address+b)&pixel_mask);
    result|=((source_buffer.data[offset>>2u]>>((offset&3u)*8u))&255u)<<(b*8u);
  }
  dest_buffer.data[(c.dest_base+address)>>2u]=result;
}
)";
}
