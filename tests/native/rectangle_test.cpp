#include "rectangle_shader.h"
#include <catch2/catch_test_macros.hpp>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <string>
using Microsoft::WRL::ComPtr;

TEST_CASE("Native rectangles link with the guest translator packed stage ABI") {
  ComPtr<ID3D12Device> device;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
    SKIP("D3D12 device unavailable");
  D3D12_ROOT_SIGNATURE_DESC root_desc{};
  ComPtr<ID3DBlob> root_blob;
  REQUIRE(SUCCEEDED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &root_blob, nullptr)));
  ComPtr<ID3D12RootSignature> root;
  REQUIRE(SUCCEEDED(device->CreateRootSignature(0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(), IID_PPV_ARGS(&root))));
  for (unsigned n : {0u, 1u, 4u, 16u}) {
    INFO("interpolators=" << n);
    std::string source = "struct V {";
    for (unsigned i = 0; i < n; ++i)
      source += "float4 t" + std::to_string(i) + ":TEXCOORD" + std::to_string(i) + ";";
    source += "float4 p:SV_Position;}; V vs(uint id:SV_VertexID) {V v; v.p=float4(id,0,0,1);";
    for (unsigned i = 0; i < n; ++i) source += "v.t" + std::to_string(i) + "=float4(id,1,2,3);";
    source += "return v;} float4 ps(V v):SV_Target {float4 c=v.p;";
    for (unsigned i = 0; i < n; ++i) source += "c+=v.t" + std::to_string(i) + ";";
    source += "return c;}";
    ComPtr<ID3DBlob> vs, ps;
    REQUIRE(SUCCEEDED(D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr, "vs", "vs_5_1", 0, 0, &vs, nullptr)));
    REQUIRE(SUCCEEDED(D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr, "ps", "ps_5_1", 0, 0, &ps, nullptr)));
    auto gs = x360n::native::CompileRectangleShader(n, 0);
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = root.Get();
    desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    desc.GS = {gs.data(), gs.size()};
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = UINT_MAX;
    desc.SampleDesc.Count = 1;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    ComPtr<ID3D12PipelineState> pipeline;
    REQUIRE(SUCCEEDED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline))));
  }
}
