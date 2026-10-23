#include "rectangle_shader.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <string>
#include <stdexcept>
namespace x360n::native {
std::vector<uint8_t> CompileRectangleShader(unsigned interpolators, unsigned clip_planes) {
  // Xenos rectangles provide three corners. Reflect the remaining corner
  // across the longest edge, preserving the guest's interpolated attributes.
  // DXBC stage linkage requires the packed register order emitted by the
  // guest translator: interpolators first, then position and clip distances.
  std::string source = "struct V {";
  for (unsigned i = 0; i < interpolators; ++i)
    source += "float4 t" + std::to_string(i) + ":TEXCOORD" + std::to_string(i) + ";";
  source += "float4 p:SV_Position;";
  for (unsigned i = 0; i < clip_planes; i += 4)
    source += "float" + std::to_string(std::min(4u, clip_planes - i)) + " c" + std::to_string(i / 4) + ":SV_ClipDistance" + std::to_string(i / 4) + ";";
  source += "}; [maxvertexcount(4)] void main(triangle V v[3], inout TriangleStream<V> s) {"
    "float2 a=v[2].p.xy-v[1].p.xy,b=v[0].p.xy-v[2].p.xy,c=v[1].p.xy-v[0].p.xy;"
    "float x=dot(a,a),y=dot(b,b),z=dot(c,c); uint j=(x>y && x>z)?0:((y>z)?1:2);"
    "V q0=v[j],q1=v[(j+1)%3],q2=v[(j+2)%3],q;"
    "s.Append(q0);s.Append(q1);s.Append(q2);q.p=(-q0.p+q1.p)+q2.p;";
  for (unsigned i = 0; i < interpolators; ++i) {
    auto t = "t" + std::to_string(i);
    source += "q." + t + "=(-q0." + t + "+q1." + t + ")+q2." + t + ";";
  }
  for (unsigned i = 0; i < clip_planes; i += 4) {
    auto t = "c" + std::to_string(i / 4);
    source += "q." + t + "=(-q0." + t + "+q1." + t + ")+q2." + t + ";";
  }
  source += "s.Append(q);s.RestartStrip();}";
  Microsoft::WRL::ComPtr<ID3DBlob> code, error;
  if (FAILED(D3DCompile(source.data(), source.size(), "NativeRectangle", nullptr, nullptr,
      "main", "gs_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_IEEE_STRICTNESS, 0, &code, &error)))
    throw std::runtime_error(error ? static_cast<const char*>(error->GetBufferPointer()) : "Native rectangle compilation failed");
  auto* bytes = static_cast<const uint8_t*>(code->GetBufferPointer());
  return {bytes, bytes + code->GetBufferSize()};
}
std::vector<uint8_t> CompilePolygonPointShader(unsigned interpolators, unsigned clip_planes,
    unsigned cull_planes, bool point_size) {
  // Expand polygon point fill to screen-aligned quads. Keeping the source
  // winding makes the guest two-sided stencil, polygon offset and PS face
  // parameter agree with the original polygon, unlike a host PointStream.
  std::string source = "struct V {";
  for (unsigned i=0; i<interpolators; ++i)
    source += "float4 t"+std::to_string(i)+":TEXCOORD"+std::to_string(i)+";";
  source += "float4 p:SV_Position;";
  for (unsigned i=0; i<clip_planes; i+=4)
    source += "float"+std::to_string(std::min(4u,clip_planes-i))+" c"+std::to_string(i/4)+":SV_ClipDistance"+std::to_string(i/4)+";";
  for (unsigned i=0; i<cull_planes; i+=4)
    source += "float"+std::to_string(std::min(4u,cull_planes-i))+" u"+std::to_string(i/4)+":SV_CullDistance"+std::to_string(i/4)+";";
  if (point_size) source += "float size:XEPSIZE;";
  source += R"(};
cbuffer Constants:register(b0) {
  float minimum_size:packoffset(c8.w); float maximum_size:packoffset(c9.w);
  float2 constant_size:packoffset(c10.x); float2 ndc_radius:packoffset(c10.z);
};
[maxvertexcount(12)] void main(triangle V v[3], inout TriangleStream<V> stream) {
  float2 a=v[1].p.xy/v[1].p.w-v[0].p.xy/v[0].p.w;
  float2 b=v[2].p.xy/v[2].p.w-v[0].p.xy/v[0].p.w;
  float direction=(a.x*b.y-a.y*b.x)<0?-1:1;
  for(uint i=0;i<3;++i) {
    float2 size=constant_size;
)";
  if (point_size) source += "if(v[i].size>=0) size=clamp(v[i].size,minimum_size,maximum_size).xx;";
  source += R"(
    for(uint j=0;j<4;++j) {
      V q=v[i]; float2 corner=float2((j&1)?1:-1,(j&2)?1:-1);
      corner.x*=direction; q.p.xy+=corner*size*ndc_radius*q.p.w;
      stream.Append(q);
    }
    stream.RestartStrip();
  }
})";
  Microsoft::WRL::ComPtr<ID3DBlob> code,error;
  if(FAILED(D3DCompile(source.data(),source.size(),"NativePolygonPoints",nullptr,nullptr,"main","gs_5_1",
      D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_IEEE_STRICTNESS,0,&code,&error)))
    throw std::runtime_error(error?static_cast<const char*>(error->GetBufferPointer()):"Native polygon point compilation failed");
  const auto* bytes=static_cast<const uint8_t*>(code->GetBufferPointer());
  return {bytes,bytes+code->GetBufferSize()};
}

}
