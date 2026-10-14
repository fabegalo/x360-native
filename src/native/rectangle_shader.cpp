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
}
