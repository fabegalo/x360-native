#pragma once
#include <rex/graphics/pipeline/shader/spirv_translator.h>
#include <fmt/format.h>
#include <string>
namespace x360n::native {
inline std::string PolygonPointGeometryGlsl(uint32_t key) {
  using C = rex::graphics::SpirvShaderTranslator::SystemConstants;
  const unsigned interpolators=key&31, clips=(key>>5)&15, culls=(key>>9)&15;
  const bool point_size=(key>>13)&1;
  std::string s="#version 450\nlayout(triangles) in; layout(triangle_strip,max_vertices=12) out;\n";
  s += "in gl_PerVertex {vec4 gl_Position;";
  if(clips) s+=fmt::format("float gl_ClipDistance[{}];",clips);
  if(culls) s+=fmt::format("float gl_CullDistance[{}];",culls);
  s += "} gl_in[]; out gl_PerVertex {vec4 gl_Position;";
  if(clips) s+=fmt::format("float gl_ClipDistance[{}];",clips);
  if(culls) s+=fmt::format("float gl_CullDistance[{}];",culls);
  s += "};\n";
  for(unsigned i=0;i<interpolators;++i) s+=fmt::format("layout(location={0}) in vec4 input_{0}[]; layout(location={0}) out vec4 output_{0};\n",i);
  if(point_size) s+=fmt::format("layout(location={}) in float input_size[];\n",interpolators);
  s += "layout(set=1,binding=0,std140) uniform Constants {";
  s += fmt::format("layout(offset={}) float minimum_size;",offsetof(C,point_vertex_diameter_min));
  s += fmt::format("layout(offset={}) float maximum_size;",offsetof(C,point_vertex_diameter_max));
  s += fmt::format("layout(offset={}) vec2 constant_size;",offsetof(C,point_constant_diameter));
  s += fmt::format("layout(offset={}) vec2 ndc_radius;",offsetof(C,point_screen_diameter_to_ndc_radius));
  s += R"(};
void main() {
 vec2 a=gl_in[1].gl_Position.xy/gl_in[1].gl_Position.w-gl_in[0].gl_Position.xy/gl_in[0].gl_Position.w;
 vec2 b=gl_in[2].gl_Position.xy/gl_in[2].gl_Position.w-gl_in[0].gl_Position.xy/gl_in[0].gl_Position.w;
 float direction=(a.x*b.y-a.y*b.x)<0?-1:1;
 for(uint i=0;i<3;++i) {
  vec2 size=constant_size;
)";
  if(point_size) s+="if(input_size[i]>=0) size=vec2(clamp(input_size[i],minimum_size,maximum_size));";
  s+=R"(
  for(uint j=0;j<4;++j) {
   vec2 corner=vec2((j&1)!=0?1:-1,(j&2)!=0?1:-1); corner.x*=direction;
   gl_Position=gl_in[i].gl_Position; gl_Position.xy+=corner*size*ndc_radius*gl_Position.w;
)";
  for(unsigned i=0;i<interpolators;++i) s+=fmt::format("output_{0}=input_{0}[i];",i);
  for(unsigned i=0;i<clips;++i) s+=fmt::format("gl_ClipDistance[{0}]=gl_in[i].gl_ClipDistance[{0}];",i);
  for(unsigned i=0;i<culls;++i) s+=fmt::format("gl_CullDistance[{0}]=gl_in[i].gl_CullDistance[{0}];",i);
  s+="EmitVertex();} EndPrimitive();}}";
  return s;
}
}
