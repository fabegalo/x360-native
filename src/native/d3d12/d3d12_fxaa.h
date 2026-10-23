#pragma once
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <bit>
#include <stdexcept>
namespace x360n::native {
namespace fxaa_bytecode {
#include "src/graphics/shaders/bytecode/d3d12_5_1/fxaa_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/fxaa_extreme_cs.h"
}
class D3D12Fxaa {
  template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
  struct Slot { Ptr<ID3D12Resource> image; uint32_t width=0,height=0; };
  ID3D12Device* device_;
  Ptr<ID3D12RootSignature> root_;
  std::array<Ptr<ID3D12PipelineState>,2> pipelines_;
  Ptr<ID3D12DescriptorHeap> heap_;
  std::array<Slot,3> slots_;
  static void Check(HRESULT h) { if(FAILED(h)) throw std::runtime_error("Native D3D12 FXAA resource creation failed"); }
  static void Barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* image,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={image,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};list->ResourceBarrier(1,&b);
  }
 public:
  static constexpr DXGI_FORMAT kFormat=DXGI_FORMAT_R16G16B16A16_UNORM;
  explicit D3D12Fxaa(ID3D12Device* device):device_(device) {
    D3D12_ROOT_PARAMETER p[3]{};
    p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[0].Constants={0,0,4};
    D3D12_DESCRIPTOR_RANGE ranges[2]{{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0}};
    for(unsigned i=0;i<2;++i) {p[1+i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[1+i].DescriptorTable={1,&ranges[i]};}
    D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;sampler.MaxAnisotropy=1;
    sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_NEVER;sampler.BorderColor=D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    D3D12_ROOT_SIGNATURE_DESC desc{3,p,1,&sampler,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    Ptr<ID3DBlob> code,error;Check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&code,&error));
    Check(device_->CreateRootSignature(0,code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&root_)));
    for(unsigned i=0;i<2;++i) {
      D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root_.Get();
      pd.CS=i?D3D12_SHADER_BYTECODE{fxaa_bytecode::fxaa_extreme_cs,sizeof(fxaa_bytecode::fxaa_extreme_cs)}:D3D12_SHADER_BYTECODE{fxaa_bytecode::fxaa_cs,sizeof(fxaa_bytecode::fxaa_cs)};
      Check(device_->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pipelines_[i])));
    }
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,6,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};
    Check(device_->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap_)));
  }
  ID3D12Resource* Begin(unsigned index,uint32_t width,uint32_t height,ID3D12GraphicsCommandList* list) {
    auto& s=slots_[index];
    if(s.width!=width||s.height!=height) {
      s.image.Reset();D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
      D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=width;d.Height=height;
      d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Format=kFormat;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
      Check(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&s.image)));
      s.width=width;s.height=height;
    } else Barrier(list,s.image.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    return s.image.Get();
  }
  void Apply(unsigned index,ID3D12GraphicsCommandList* list,ID3D12Resource* output,DXGI_FORMAT format,
      D3D12_RESOURCE_STATES output_state,bool extreme) {
    auto& s=slots_[index];Barrier(list,s.image.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Barrier(list,output,output_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const auto stride=device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto cpu=heap_->GetCPUDescriptorHandleForHeapStart();auto gpu=heap_->GetGPUDescriptorHandleForHeapStart();cpu.ptr+=index*2*stride;gpu.ptr+=index*2*stride;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=format;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
    device_->CreateUnorderedAccessView(output,nullptr,&uav,cpu);cpu.ptr+=stride;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=kFormat;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    device_->CreateShaderResourceView(s.image.Get(),&srv,cpu);
    ID3D12DescriptorHeap* heaps[]{heap_.Get()};list->SetDescriptorHeaps(1,heaps);list->SetComputeRootSignature(root_.Get());list->SetPipelineState(pipelines_[extreme].Get());
    const uint32_t constants[]{s.width,s.height,std::bit_cast<uint32_t>(1.0f/s.width),std::bit_cast<uint32_t>(1.0f/s.height)};
    list->SetComputeRoot32BitConstants(0,4,constants,0);list->SetComputeRootDescriptorTable(1,gpu);gpu.ptr+=stride;list->SetComputeRootDescriptorTable(2,gpu);
    list->Dispatch((s.width+15)/16,(s.height+7)/8,1);Barrier(list,output,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,output_state);
  }
};
}
