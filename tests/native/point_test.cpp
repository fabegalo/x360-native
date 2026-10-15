#include "point_shader.h"
#include "resolve_coverage.h"
#include <catch2/catch_test_macros.hpp>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
using namespace x360n::native;
using namespace rex::graphics;
using Microsoft::WRL::ComPtr;
TEST_CASE("Native point constants preserve independent size and vertex limits") {
  RegisterFile r;
  r[XE_GPU_REG_PA_SU_POINT_SIZE] = 0x00080008;
  r[XE_GPU_REG_PA_SU_POINT_MINMAX] = 0x04000010;
  draw_util::ViewportInfo v{}; v.xy_extent[0] = 1280; v.xy_extent[1] = 720;
  DxbcShaderTranslator::SystemConstants c{};
  SetPointConstants(r, v, c);
  CHECK(c.point_constant_diameter[0] == 1.f);
  CHECK(c.point_constant_diameter[1] == 1.f);
  CHECK(c.point_vertex_diameter_min == 2.f);
  CHECK(c.point_vertex_diameter_max == 128.f);
  CHECK(c.point_screen_diameter_to_ndc_radius[0] == 1.f / 1280.f);
  CHECK(c.point_screen_diameter_to_ndc_radius[1] == 1.f / 720.f);
  r[XE_GPU_REG_PA_SU_POINT_SIZE] = 0x00100020;
  v.xy_extent[0] = v.xy_extent[1] = 0;
  SetPointConstants(r, v, c);
  CHECK(c.point_constant_diameter[0] == 2.f);
  CHECK(c.point_constant_diameter[1] == 4.f);
  CHECK(c.point_screen_diameter_to_ndc_radius[0] == 1.f);
}
TEST_CASE("Native empty resolve coverage respects clipping and eight pixel alignment") {
  RegisterFile r;
  r[XE_GPU_REG_RB_SURFACE_INFO] = 1280;
  r[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR] = (512u << 16) | 1280;
  r[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR] = (8192u << 16) | 8192;
  const float outside[]{-.5f,511.5f,1279.5f,511.5f,1279.5f,719.5f};
  CHECK(ResolveCoverageEmpty(r, outside));
  r[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR] = (720u << 16) | 1280;
  CHECK_FALSE(ResolveCoverageEmpty(r, outside));
  const float inside[]{-.5f,-.5f,1279.5f,-.5f,1279.5f,511.5f};
  CHECK_FALSE(ResolveCoverageEmpty(r, inside));
  // Collapsed but non-aligned extents are expanded by the shared resolve ABI.
  const float subpixel[]{8.5f,8.5f,8.5f,8.5f,8.5f,8.5f};
  CHECK_FALSE(ResolveCoverageEmpty(r, subpixel));
  r[XE_GPU_REG_RB_SURFACE_INFO] = 3u << 16;
  CHECK_FALSE(ResolveCoverageEmpty(r, outside));
}
TEST_CASE("Native point geometry links constant and exported size with sprite coordinates") {
  ComPtr<ID3D12Device> device;
  REQUIRE(SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));
  D3D12_ROOT_PARAMETER cb{}; cb.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  D3D12_ROOT_SIGNATURE_DESC rd{1, &cb};
  ComPtr<ID3DBlob> blob;
  REQUIRE(SUCCEEDED(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, nullptr)));
  ComPtr<ID3D12RootSignature> root;
  REQUIRE(SUCCEEDED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root))));
  for (unsigned n : {0u,1u,4u,16u}) for (bool size : {false,true}) for (bool coordinates : {false,true}) {
    INFO("interpolators=" << n << " size=" << size << " coordinates=" << coordinates);
    std::string source="struct V {";
    for (unsigned i=0;i<n;++i) source += "float4 t"+std::to_string(i)+":TEXCOORD"+std::to_string(i)+";";
    source += "float4 p:SV_Position;";
    if(size) source += "float s:XEPSIZE;";
    source += "}; V vs(uint id:SV_VertexID) {V v;v.p=float4(id,0,0,1);";
    for(unsigned i=0;i<n;++i) source += "v.t"+std::to_string(i)+"=float4(id,1,2,3);";
    if(size) source += "v.s=float(id);";
    source += "return v;}";
    ComPtr<ID3DBlob> vs, ps;
    REQUIRE(SUCCEEDED(D3DCompile(source.data(),source.size(),nullptr,nullptr,nullptr,"vs","vs_5_1",0,0,&vs,nullptr)));
    std::string pixel = "struct P {";
    for (unsigned i=0;i<n;++i) pixel += "float4 t"+std::to_string(i)+":TEXCOORD"+std::to_string(i)+";";
    if(coordinates) pixel += "float2 uv:XESPRITETEXCOORD;";
    pixel += "float4 p:SV_Position;}; float4 ps(P v):SV_Target {float4 c=v.p;";
    for (unsigned i=0;i<n;++i) pixel += "c+=v.t"+std::to_string(i)+";";
    if(coordinates) pixel += "c.xy+=v.uv;";
    pixel += "return c;}";
    REQUIRE(SUCCEEDED(D3DCompile(pixel.data(),pixel.size(),nullptr,nullptr,nullptr,"ps","ps_5_1",0,0,&ps,nullptr)));
    GeometryShaderKey key; key.type=PipelineGeometryShader::kPointList;key.interpolator_count=n;key.has_point_size=size;key.has_point_coordinates=coordinates;
    std::vector<uint32_t> gs;CreateDxbcGeometryShader(key,gs);
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};d.pRootSignature=root.Get();
    d.VS={vs->GetBufferPointer(),vs->GetBufferSize()};d.PS={ps->GetBufferPointer(),ps->GetBufferSize()};d.GS={gs.data(),gs.size()*4};
    d.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;d.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    d.BlendState.RenderTarget[0].RenderTargetWriteMask=15;d.SampleMask=UINT_MAX;d.SampleDesc.Count=1;
    d.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;d.NumRenderTargets=1;d.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
    ComPtr<ID3D12PipelineState> pipeline;
    REQUIRE(SUCCEEDED(device->CreateGraphicsPipelineState(&d,IID_PPV_ARGS(&pipeline))));
  }
}


#include "primitive_indices.h"
TEST_CASE("Native primitive restart splits strips and fans without bridging segments") {
  using P = xenos::PrimitiveType;
  const uint32_t indices[]{0,1,2,0xFFFFFFFFu,3,4,5};
  auto strip = ConvertIndices(indices, P::kTriangleStrip, true, 0xFFFFFF);
  REQUIRE(strip.ranges.size() == 2);
  CHECK(strip.ranges[0].count == 3); CHECK(strip.ranges[1].offset == 3);
  CHECK(strip.values == std::vector<uint32_t>{0,1,2,3,4,5});
  auto fan = ConvertIndices(indices, P::kTriangleFan, true, 0xFFFFFF);
  CHECK(fan.values == std::vector<uint32_t>{0,1,2,3,4,5});
  auto list = ConvertIndices(indices, P::kTriangleList, true, 0xFFFFFF);
  CHECK(list.values.size() == 7); CHECK(list.ranges.size() == 1);
  const uint32_t short_segments[]{42,0,42,1,2,42,3,4,5,6,42};
  fan = ConvertIndices(short_segments, P::kTriangleFan, true, 42);
  CHECK(fan.values == std::vector<uint32_t>{3,4,5,3,5,6});
}

#include "texture.h"
TEST_CASE("Native depth textures preserve float24 denormals and ignore stencil") {
  CHECK(DecodeDepthTexture(0x000000FF, true) == 0.f);
  CHECK(DecodeDepthTexture(0x00000100, true) == std::bit_cast<float>(uint32_t(93u << 23)));
  CHECK(DecodeDepthTexture(0xF0000000, true) == 1.f);
  CHECK(DecodeDepthTexture(0xF80000FF, true) == 1.5f);
  CHECK(DecodeDepthTexture(0xFFFFFF00, false) == 1.f);
  CHECK(DecodeDepthTexture(0x000000FF, false) == 0.f);
  CHECK(IsFloatTexture(xenos::TextureFormat::k_32_FLOAT));
  CHECK_FALSE(IsFloatTexture(xenos::TextureFormat::k_16));
}

#include "texture_block.h"
#include <chrono>
#include <iostream>
TEST_CASE("Native texture block endian normalization matches byte addressing") {
  std::vector<uint8_t> source(256);
  for (unsigned i=0;i<256;++i) source[i]=uint8_t(i);
  // 64 covers whole linear rows normalized in one call.
  for (unsigned bytes : {1u,2u,4u,8u,16u,64u}) for (unsigned endian=0;endian<4;++endian) {
    for(unsigned offset=0;offset+bytes<=256;offset+=std::min(bytes,4u)) {
      uint8_t result[64]; ReadTextureBlock(source,offset,bytes,xenos::Endian(endian),result);
      const unsigned mask[]{0,1,3,2};
      for(unsigned b=0;b<bytes;++b) CHECK(result[b]==source[(offset+b)^mask[endian]]);
    }
  }
  uint8_t output[16];
  CHECK_THROWS(ReadTextureBlock(source,256,4,xenos::Endian::kNone,output));
  CHECK_THROWS(ReadTextureBlock(std::span(source).first(1),0,1,xenos::Endian::k8in32,output));
}
TEST_CASE("Native texture block conversion costs", "[.native-block-cost]") {
  constexpr unsigned size = 1280 * 736 * 4;
  std::vector<uint8_t> source(size), before(size), after(size);
  for(unsigned i=0;i<size;++i)source[i]=uint8_t(i*17);
  auto old = [](std::span<const uint8_t> source, unsigned offset, unsigned bytes, xenos::Endian endian, uint8_t* dest) {
    for(unsigned b=0;b<bytes;++b) {
      unsigned byte=offset+b;
      switch(endian) {case xenos::Endian::k8in16:byte^=1;break;case xenos::Endian::k8in32:byte^=3;break;case xenos::Endian::k16in32:byte^=2;break;default:break;}
      if(byte>=source.size())throw std::runtime_error("extent");
      dest[b]=source[byte];
    }
  };
  // Function pointers keep the runtime endian/size dispatch used by bindings.
  using Read = void(*)(std::span<const uint8_t>,size_t,unsigned,xenos::Endian,uint8_t*);
  Read fast=ReadTextureBlock;
  using Old = void(*)(std::span<const uint8_t>,unsigned,unsigned,xenos::Endian,uint8_t*);
  Old slow=old;
  for(unsigned sample=0;sample<8;++sample) {
    double times[2]{};
    for(unsigned step=0;step<2;++step) {
      bool optimized=bool(step ^ (sample&1));
      auto start=std::chrono::steady_clock::now();
      for(unsigned repeat=0;repeat<8;++repeat) for(unsigned offset=0;offset<size;offset+=4) {
        if(optimized)fast(source,offset,4,xenos::Endian::k8in32,after.data()+offset);
        else slow(source,offset,4,xenos::Endian::k8in32,before.data()+offset);
      }
      times[optimized]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    }
    REQUIRE(before==after);
    std::cout << "block_cost," << sample << "," << times[0] << "," << times[1] << '\n';
  }
}
#include <rex/graphics/pipeline/texture/util.h>
TEST_CASE("Native tiled texture runs match per-block tiled addressing") {
  using rex::graphics::texture_util::GetTiledOffset2D;
  for (unsigned log2 = 0; log2 <= 4; ++log2) for (unsigned pitch : {32u, 40u, 64u, 160u, 1280u}) {
    const unsigned run = TiledContiguousBlocks(log2);
    REQUIRE(run >= 1); REQUIRE((run & (run - 1)) == 0);
    for (int y = 0; y < 72; ++y) for (int x = 0; x < int(pitch); x += int(run)) {
      const int32_t first = GetTiledOffset2D(x, y, pitch, log2);
      for (unsigned i = 1; i < run; ++i) {
        INFO("log2=" << log2 << " pitch=" << pitch << " x=" << x << " y=" << y << " i=" << i);
        CHECK(GetTiledOffset2D(x + int(i), y, pitch, log2) == first + int32_t(i << log2));
      }
    }
  }
  // Unaligned word tails stay on the exact byte path.
  std::vector<uint8_t> source(16);
  for (unsigned i = 0; i < 16; ++i) source[i] = uint8_t(i);
  uint8_t out[5];
  ReadTextureBlock(source, 4, 5, xenos::Endian::k8in32, out);
  for (unsigned b = 0; b < 5; ++b) CHECK(out[b] == source[(4 + b) ^ 3]);
}
