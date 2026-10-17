#include "../../src/native/pipeline/pipeline_store.h"
#include <catch2/catch_test_macros.hpp>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <fstream>
#include <chrono>
#include <thread>
using Microsoft::WRL::ComPtr;
using x360n::native::PipelineStore;
TEST_CASE("Native pipeline store validates full inputs and incomplete files") {
  ComPtr<ID3D12Device> device;
  REQUIRE(SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device))));
  D3D12_ROOT_SIGNATURE_DESC rd{};
  ComPtr<ID3DBlob> root_blob, errors;
  REQUIRE(SUCCEEDED(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &root_blob, &errors)));
  ComPtr<ID3D12RootSignature> root;
  REQUIRE(SUCCEEDED(device->CreateRootSignature(0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(), IID_PPV_ARGS(&root))));
  const char* source = "float4 vs(uint id:SV_VertexID):SV_Position{return float4(id&1,id>>1,0,1);} float4 ps():SV_Target{return float4(0.2,0.5,0.8,1);}";
  ComPtr<ID3DBlob> vs, ps;
  REQUIRE(SUCCEEDED(D3DCompile(source, strlen(source), nullptr, nullptr, nullptr, "vs", "vs_5_1", 0, 0, &vs, &errors)));
  REQUIRE(SUCCEEDED(D3DCompile(source, strlen(source), nullptr, nullptr, nullptr, "ps", "ps_5_1", 0, 0, &ps, &errors)));
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
  pd.pRootSignature = root.Get(); pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()}; pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
  pd.SampleMask = UINT_MAX; pd.SampleDesc.Count = 1;
  pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pd.NumRenderTargets = 1; pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  ComPtr<ID3D12PipelineState> pso;
  REQUIRE(SUCCEEDED(device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso))));
  auto directory = std::filesystem::current_path() / ("pipeline-store-test-" + std::to_string(GetCurrentProcessId()));
  PipelineStore store; store.SetDirectory(directory); store.SetRoot(root_blob->GetBufferPointer(), root_blob->GetBufferSize());
  auto miss = store.Load(pd); REQUIRE(miss.blob.empty());
  store.Save(miss, pso.Get());
  INFO(miss.path.string()); REQUIRE(std::filesystem::exists(miss.path));
  auto hit = store.Load(pd); REQUIRE_FALSE(hit.blob.empty()); REQUIRE(hit.key == miss.key);
  auto cached = pd; cached.CachedPSO = {hit.blob.data(), hit.blob.size()};
  ComPtr<ID3D12PipelineState> reused;
  REQUIRE(SUCCEEDED(device->CreateGraphicsPipelineState(&cached, IID_PPV_ARGS(&reused))));
  auto changed = pd; changed.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
  auto other = store.Load(changed); REQUIRE(other.path != hit.path); REQUIRE(other.blob.empty());
  // Same contents at a new bytecode address must retain their cache identity.
  std::vector<char> relocated(static_cast<char*>(ps->GetBufferPointer()), static_cast<char*>(ps->GetBufferPointer()) + ps->GetBufferSize());
  changed = pd; changed.PS.pShaderBytecode = relocated.data();
  REQUIRE_FALSE(store.Load(changed).blob.empty());
  relocated.back() ^= 1; REQUIRE(store.Load(changed).path != hit.path);
  // A hash filename alone is insufficient: exact key mismatch must be a miss.
  { std::fstream file(hit.path, std::ios::binary | std::ios::in | std::ios::out); file.seekp(24); char byte = hit.key[0] ^ 1; file.write(&byte, 1); }
  REQUIRE(store.Load(pd).blob.empty());
  store.Save(miss, pso.Get());
  std::filesystem::resize_file(hit.path, 32);
  REQUIRE(store.Load(pd).blob.empty());
  std::filesystem::remove(hit.path);
  // Windows scanners may briefly retain a delete-pending file handle. Keep
  // cleanup bounded and require an empty directory rather than deleting peers.
  std::error_code cleanup_error;
  for (unsigned attempt = 0; attempt < 100; ++attempt) {
    std::filesystem::remove(directory, cleanup_error);
    if (!cleanup_error) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  REQUIRE_FALSE(cleanup_error);
}
