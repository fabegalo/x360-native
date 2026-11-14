#include "d3d12_hybrid.h"
#include "d3d12_render.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

REXCVAR_DEFINE_BOOL(native_d3d12_hybrid_color, false, "GPU/Native",
    "Experimental D3D12 opaque HDR host targets with explicit ROV depth/stencil and other-case routing");

namespace x360n::native {
using namespace rex::graphics;
using namespace rex::graphics::xenos;
static_assert(kEdramTileWidthSamples == 80 && kEdramTileHeightSamples == 16 && kEdramTileCount == 2048,
    "Hybrid bridge must follow the pinned EDRAM layout");
namespace {
void Check(HRESULT result, const char* what) {
  if (FAILED(result)) throw std::runtime_error(fmt::format("Native hybrid: {} HRESULT={:08X}", what, uint32_t(result)));
}
// EDRAM layout and 7e3 conversion follow pinned Xenia/ReXGlue (BSD-3-Clause).
// Copyright 2021, 2022 Ben Vanik; ReXGlue adaptation Tom Clay, 2026.
// Original BSD-3-Clause terms remain in licenses/rexglue-sdk.txt.
constexpr char kBridge[] = R"hlsl(
cbuffer Bridge : register(b0) { uint width, height, pitch_tiles, base_tiles, msaa, left, top, right, bottom; };
RWBuffer<uint> edram : register(u0);
#if MULTISAMPLED
Texture2DMS<float4> image : register(t0);
#else
Texture2D<float4> image : register(t0);
#endif
uint guest_sample(uint host) {
  return msaa == 2 ? (host == 1 ? 3 : host == 3 ? 1 : host) : msaa == 1 ? (host == 2 ? 0 : 1) : 0;
}
uint host_sample(uint guest) {
  return msaa == 2 ? (guest == 1 ? 3 : guest == 3 ? 1 : guest) : msaa == 1 ? (guest == 0 ? 2 : 1) : 0;
}
uint address(uint2 pixel, uint sample) {
  uint2 sample_xy = (pixel << uint2(msaa == 2, msaa != 0)) + uint2(sample >> 1, sample & 1);
  uint2 tile = sample_xy / uint2(80,16);
  uint2 local = sample_xy - tile * uint2(80,16);
  return (((base_tiles + tile.y * pitch_tiles + tile.x) * 1280) + local.y * 80 + local.x) % (EDRAM_TILE_COUNT * 1280);
}
float unpack7(uint v) {
  uint m = v & 127, e = v >> 7;
  return e == 0 ? float(m) * (1.0 / 512.0) : asfloat(((e + 124) << 23) | (m << 16));
}
uint pack7(float value) {
  uint v = asuint(clamp(value, 0.0, 31.875));
  uint biased = v < 0x3e800000 ? ((v & 0x7fffff) | 0x800000) >> min(125 - (v >> 23), 24) : v + 0xc2000000;
  return ((biased + 0x7fff + ((biased >> 16) & 1)) >> 16) & 1023;
}
float4 unpack(uint v) {
  return float4(unpack7(v & 1023), unpack7((v >> 10) & 1023), unpack7((v >> 20) & 1023), float(v >> 30) / 3.0);
}
uint pack(float4 v) {
  return pack7(v.x) | (pack7(v.y) << 10) | (pack7(v.z) << 20) | (uint(round(saturate(v.w) * 3.0)) << 30);
}
float4 LoadVS(uint id : SV_VertexID) : SV_Position {
  float2 p = float2((id << 1) & 2, id & 2);
  return float4(p * float2(2,-2) + float2(-1,1), 0, 1);
}
float4 LoadPS(float4 p : SV_Position, uint sample : SV_SampleIndex) : SV_Target {
  return unpack(edram[address(uint2(p.xy), guest_sample(sample))]);
}
[numthreads(8,8,1)]
void StoreCS(uint3 id : SV_DispatchThreadID) {
  if (id.x >= right-left || id.y >= bottom-top) return;
  id.xy += uint2(left,top);
  uint count = 1 << msaa;
  for (uint i = 0; i < count; ++i) {
#if MULTISAMPLED
    float4 value = image.Load(int2(id.xy), host_sample(i));
#else
    float4 value = image.Load(int3(id.xy,0));
#endif
    edram[address(id.xy,i)] = pack(value);
  }
}
)hlsl";
Microsoft::WRL::ComPtr<ID3DBlob> Compile(const char* entry, const char* profile, bool multisampled, uint32_t edram_tile_count) {
  Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
  const auto tile_count = std::to_string(edram_tile_count);
  D3D_SHADER_MACRO macros[]{{"MULTISAMPLED", multisampled ? "1" : "0"}, {"EDRAM_TILE_COUNT", tile_count.c_str()}, {nullptr,nullptr}};
  auto result = D3DCompile(kBridge, sizeof(kBridge)-1, "native-hybrid-color", macros, nullptr,
      entry, profile, D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &code, &errors);
  if (FAILED(result) && errors)
    throw std::runtime_error(std::string("Native hybrid shader: ") + static_cast<const char*>(errors->GetBufferPointer()));
  Check(result, "compile bridge shader");
  return code;
}
}
bool NativeHybridColorEnabled() { return REXCVAR_GET(native_d3d12_hybrid_color); }
void D3D12Render::EndHybridColor() { if (hybrid_color_) hybrid_color_->End(); }

D3D12HybridColor::D3D12HybridColor(D3D12Render& render) : render_(render) {
  D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS feature{};
  feature.Format=DXGI_FORMAT_R16G16B16A16_FLOAT; feature.SampleCount=4;
  Check(render_.device_->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &feature, sizeof(feature)), "query HDR MSAA support");
  msaa4_ = feature.NumQualityLevels != 0;
  REXLOG_INFO("Native D3D12 hybrid color enabled: opaque 7e3 HDR; ROV depth/stencil; 4x host support={}", msaa4_);
}
std::optional<HybridColorKey> D3D12HybridColor::Classify(const RegisterFile& regs, uint32_t mask) const {
  if (render_.scale_x_ != 1 || render_.scale_y_ != 1 || !mask || (mask & ~15u)) return {};
  auto info = regs.Get<reg::RB_COLOR_INFO>();
  if (info.color_format != ColorRenderTargetFormat::k_2_10_10_10_FLOAT &&
      info.color_format != ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16) return {};
  if ((regs[reg::RB_BLENDCONTROL::rt_register_indices[0]] & 0x1fff1fff) != 0x00010001) return {};
  auto surface = regs.Get<reg::RB_SURFACE_INFO>();
  const unsigned msaa = unsigned(surface.msaa_samples);
  if (!surface.surface_pitch || msaa > 2 || (msaa && !msaa4_)) return {};
  const unsigned x = msaa == 2, y = msaa != 0;
  const unsigned pitch = ((surface.surface_pitch << x) + 79) / 80;
  draw_util::Scissor scissor{}; draw_util::GetScissor(regs, scissor);
  const uint64_t right = uint64_t(scissor.offset[0]) + scissor.extent[0];
  const uint64_t bottom = uint64_t(scissor.offset[1]) + scissor.extent[1];
  const unsigned tile_height = 16 >> y;
  const uint64_t height = (bottom + tile_height - 1) / tile_height * tile_height;
  const unsigned width = (pitch * 80) >> x;
  const uint64_t tiles = uint64_t(pitch) * (((bottom + tile_height - 1) / tile_height) - scissor.offset[1]/tile_height);
  if (!height || height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION || width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
      right > width || !scissor.extent[0] || !scissor.extent[1] || !tiles || tiles > render_.edram_tile_count_ ||
      uint64_t(width)*height*8*(msaa?4:1) > (256ull<<20)) return {};
  auto depth = draw_util::GetNormalizedDepthControl(regs);
  const unsigned depth_base = regs.Get<reg::RB_DEPTH_INFO>().depth_base & (render_.edram_tile_count_ - 1);
  const unsigned base = info.color_base & (render_.edram_tile_count_ - 1);
  // Equal bases have depth disabled by the existing Native ROV contract.
  // Reject every other overlapping periodic depth/color footprint.
  if ((depth.z_enable || depth.stencil_enable) && depth_base != base &&
      (((depth_base + render_.edram_tile_count_ - base) % render_.edram_tile_count_) < tiles ||
       ((base + render_.edram_tile_count_ - depth_base) % render_.edram_tile_count_) < tiles)) return {};
  return HybridColorKey{base,pitch,width,uint32_t(height),msaa,
      scissor.offset[0],scissor.offset[1],uint32_t(right),uint32_t(bottom)};
}
void D3D12HybridColor::Initialize() {
  if (root_) return;
  D3D12_DESCRIPTOR_RANGE ranges[2]{{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,0},
                                  {D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0}};
  D3D12_ROOT_PARAMETER parameters[3]{};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[0].Descriptor.ShaderRegister = 0;
  for (unsigned i=0;i<2;++i) {
    parameters[i+1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[i+1].DescriptorTable = {1,&ranges[i]};
  }
  D3D12_ROOT_SIGNATURE_DESC desc{3,parameters,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
  Ptr<ID3DBlob> blob, errors;
  Check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors), "serialize bridge root");
  Check(render_.device_->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root_)), "create bridge root");
  auto vs=Compile("LoadVS","vs_5_1",false,render_.edram_tile_count_), ps=Compile("LoadPS","ps_5_1",false,render_.edram_tile_count_);
  for (unsigned i=0;i<2;++i) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{}; pd.pRootSignature=root_.Get();
    pd.VS={vs->GetBufferPointer(),vs->GetBufferSize()}; pd.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID; pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable=TRUE;
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask=15; pd.SampleMask=UINT_MAX;
    pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets=1; pd.RTVFormats[0]=DXGI_FORMAT_R16G16B16A16_FLOAT; pd.SampleDesc.Count=i?4:1;
    if (!i || msaa4_) Check(render_.device_->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&load_[i])), "create bridge load pipeline");
    auto cs=Compile("StoreCS","cs_5_1",i!=0,render_.edram_tile_count_);
    D3D12_COMPUTE_PIPELINE_STATE_DESC cd{}; cd.pRootSignature=root_.Get(); cd.CS={cs->GetBufferPointer(),cs->GetBufferSize()};
    Check(render_.device_->CreateComputePipelineState(&cd,IID_PPV_ARGS(&store_[i])), "create bridge store pipeline");
  }
}
void D3D12HybridColor::BindBridge(bool store) {
  auto& r=render_; const auto& key=active_->key;
  const std::array<uint32_t,12> constants{key.width,key.height,key.pitch_tiles,key.base,
      key.msaa,key.left,key.top,key.right,key.bottom};
  auto address=r.Constants(constants.data(),sizeof(constants));
  r.MemoryTable(false);
  auto edram=r.Gpu(r.arena_memory_tables_[0]+2);
  ID3D12DescriptorHeap* heaps[]{r.views_.Get(),r.samplers_.Get()}; r.cmd_.SetDescriptorHeaps(2,heaps);
  if (store) {
    r.cmd_.SetComputeRootSignature(root_.Get()); r.cmd_.SetPipelineState(store_[key.msaa!=0].Get());
    r.cmd_.SetComputeRootConstantBufferView(0,address); r.cmd_.SetComputeRootDescriptorTable(1,edram);
    const auto view=r.Views(1);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.ViewDimension=key.msaa?D3D12_SRV_DIMENSION_TEXTURE2DMS:D3D12_SRV_DIMENSION_TEXTURE2D;
    if (!key.msaa) srv.Texture2D.MipLevels=1;
    r.device_->CreateShaderResourceView(active_->texture.Get(),&srv,r.Cpu(view));
    r.cmd_.SetComputeRootDescriptorTable(2,r.Gpu(view));
  } else {
    r.cmd_.SetGraphicsRootSignature(root_.Get()); r.cmd_.SetPipelineState(load_[key.msaa!=0].Get());
    r.cmd_.SetGraphicsRootConstantBufferView(0,address); r.cmd_.SetGraphicsRootDescriptorTable(1,edram);
  }
}
void D3D12HybridColor::Begin(const HybridColorKey& key) {
  if (active_ && active_->key==key) return;
  End(); Initialize();
  Surface* found=nullptr;
  for (auto& surface:surfaces_) if (surface->key==key) {found=surface.get();break;}
  if (!found) {
    const uint64_t size=uint64_t(key.width)*key.height*8*(key.msaa?4:1);
    // Bounded host cache; drain before releasing any descriptors/resources.
    constexpr uint64_t budget=256ull<<20;
    if (bytes_+size>budget) {
      render_.Flush(D3D12Render::kFlushDrain);
      while (bytes_+size>budget && !surfaces_.empty()) {
        auto oldest=std::min_element(surfaces_.begin(),surfaces_.end(),[](auto& a,auto& b){return a->last_use<b->last_use;});
        bytes_-=(*oldest)->bytes; surfaces_.erase(oldest);
      }
    }
    auto surface=std::make_unique<Surface>(); surface->key=key; surface->bytes=size;
    D3D12_RESOURCE_DESC desc{}; desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width=key.width; desc.Height=key.height; desc.DepthOrArraySize=1; desc.MipLevels=1;
    desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT; desc.SampleDesc.Count=key.msaa?4:1;
    desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    Check(render_.device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
        surface->state,nullptr,IID_PPV_ARGS(&surface->texture)), "create host color target");
    D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV; hd.NumDescriptors=1;
    Check(render_.device_->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&surface->rtv)), "create color target descriptor");
    render_.device_->CreateRenderTargetView(surface->texture.Get(),nullptr,surface->rtv->GetCPUDescriptorHandleForHeapStart());
    found=surface.get(); bytes_+=size; surfaces_.push_back(std::move(surface));
  }
  active_=found; active_->last_use=++use_;
  render_.Transition(render_.edram_.Get(),render_.edram_state_,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  render_.Barrier();
  render_.Transition(active_->texture.Get(),active_->state,D3D12_RESOURCE_STATE_RENDER_TARGET);
  BindBridge(false);
  auto rtv=target(); render_.cmd_.OMSetRenderTargets(1,&rtv,FALSE,nullptr);
  D3D12_VIEWPORT vp{0,0,float(key.width),float(key.height),0,1}; render_.cmd_.RSSetViewports(1,&vp);
  D3D12_RECT sc{LONG(key.left),LONG(key.top),LONG(key.right),LONG(key.bottom)}; render_.cmd_.RSSetScissorRects(1,&sc);
  render_.cmd_.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  render_.cmd_.DrawInstanced(3,1,0,0);
  ++imports_;
}
void D3D12HybridColor::End() {
  if (!active_) return;
  auto& r=render_; const auto key=active_->key;
  r.cmd_.OMSetRenderTargets(0,nullptr,FALSE,nullptr);
  r.Transition(active_->texture.Get(),active_->state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  r.Transition(r.edram_.Get(),r.edram_state_,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  BindBridge(true); r.cmd_.Dispatch((key.right-key.left+7)/8,(key.bottom-key.top+7)/8,1);
  r.Barrier(); active_=nullptr; ++exports_;
}
D3D12_CPU_DESCRIPTOR_HANDLE D3D12HybridColor::target() const { return active_->rtv->GetCPUDescriptorHandleForHeapStart(); }
void D3D12HybridColor::LogStats() const {
  REXLOG_INFO("Native hybrid color draws={} imports={} exports={} host_bytes={} cached_surfaces={}",draws_,imports_,exports_,bytes_,surfaces_.size());
}
}
