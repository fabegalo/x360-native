// Explicit synthetic hardware test; no window, title, Native backend or console claim.
#include <rex/graphics/pipeline/shader/dxbc_translator.h>
#include <d3d12.h>
#include <d3d12shader.h>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace rex::graphics;
namespace {
void Need(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
D3D12_RESOURCE_DESC BufferDesc(UINT64 bytes) {
  D3D12_RESOURCE_DESC d{};
  d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; d.Width=bytes; d.Height=1;
  d.DepthOrArraySize=d.MipLevels=1; d.SampleDesc.Count=1; d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  return d;
}
}

void CheckUnorm8Gpu(const std::vector<uint8_t>& baseline, const std::vector<uint8_t>& candidate) {
  Need(!baseline.empty() && !candidate.empty(), "UNORM8 translation failed");
  struct Library { HMODULE value; ~Library() { if (value) FreeLibrary(value); } } library{LoadLibraryW(L"D3DCompiler_47.dll")};
  const HMODULE compiler=library.value;
  Need(compiler!=nullptr, "D3DCompiler unavailable");
  const auto compile=reinterpret_cast<pD3DCompile>(GetProcAddress(compiler,"D3DCompile"));
  const auto reflect=reinterpret_cast<decltype(&D3DReflect)>(GetProcAddress(compiler,"D3DReflect"));
  Need(compile && reflect,"D3D compiler entry points unavailable");
  ComPtr<ID3D12Debug> debug;
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
  ComPtr<IDXGIFactory4> factory; ComPtr<IDXGIAdapter1> adapter;
  Need(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))),"DXGI factory");
  Need(SUCCEEDED(factory->EnumAdapters1(0,&adapter)),"hardware adapter");
  DXGI_ADAPTER_DESC1 adapter_desc{}; adapter->GetDesc1(&adapter_desc);
  Need(!(adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE),"software GPU is not this test");
  ComPtr<ID3D12Device> device;
  Need(SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))),"D3D12 device");
  std::cout<<"Hardware D3D12 vendor="<<std::hex<<adapter_desc.VendorId<<" device="<<adapter_desc.DeviceId<<std::dec<<'\n';
  D3D12_COMMAND_QUEUE_DESC queue_desc{}; ComPtr<ID3D12CommandQueue> queue;
  Need(SUCCEEDED(device->CreateCommandQueue(&queue_desc,IID_PPV_ARGS(&queue))),"queue");
  ComPtr<ID3D12ShaderReflection> reflection;
  Need(SUCCEEDED(reflect(candidate.data(),candidate.size(),IID_PPV_ARGS(&reflection))),"PS reflection");
  D3D12_SHADER_DESC shader_desc{}; reflection->GetDesc(&shader_desc);
  std::vector<D3D12_SHADER_INPUT_BIND_DESC> bindings(shader_desc.BoundResources);
  std::vector<D3D12_ROOT_PARAMETER> params;
  std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
  std::vector<D3D12_STATIC_SAMPLER_DESC> samplers;
  params.reserve(bindings.size()); ranges.reserve(bindings.size());
  for (UINT i=0;i<bindings.size();++i) {
    Need(SUCCEEDED(reflection->GetResourceBindingDesc(i,&bindings[i])),"binding reflection");
    const auto& b=bindings[i]; Need(b.BindCount==1,"unexpected array binding");
    if (b.Type==D3D_SIT_SAMPLER) {
      D3D12_STATIC_SAMPLER_DESC s{};
      s.Filter=D3D12_FILTER_MIN_MAG_MIP_POINT;
      s.AddressU=s.AddressV=s.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
      s.ShaderRegister=b.BindPoint; s.RegisterSpace=b.Space; s.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
      samplers.push_back(s); continue;
    }
    D3D12_ROOT_PARAMETER p{}; p.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    if (b.Type==D3D_SIT_CBUFFER) {
      p.ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
      p.Descriptor.ShaderRegister=b.BindPoint; p.Descriptor.RegisterSpace=b.Space;
    } else {
      Need(b.Type==D3D_SIT_TEXTURE,"unexpected non-texture resource");
      D3D12_DESCRIPTOR_RANGE r{}; r.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
      r.NumDescriptors=1; r.BaseShaderRegister=b.BindPoint; r.RegisterSpace=b.Space;
      r.OffsetInDescriptorsFromTableStart=D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
      ranges.push_back(r); p.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      p.DescriptorTable={1,&ranges.back()};
    }
    params.push_back(p);
  }
  D3D12_ROOT_SIGNATURE_DESC root_desc{};
  root_desc.NumParameters=UINT(params.size()); root_desc.pParameters=params.data();
  root_desc.NumStaticSamplers=UINT(samplers.size()); root_desc.pStaticSamplers=samplers.data();
  ComPtr<ID3DBlob> serialized,errors;
  Need(SUCCEEDED(D3D12SerializeRootSignature(&root_desc,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors)),"root serialization");
  ComPtr<ID3D12RootSignature> root;
  Need(SUCCEEDED(device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&root))),"root signature");
  const char vs_source[]=
      "struct V{float4 t[16]:TEXCOORD0;float4 p:SV_Position;};"
      "V main(uint i:SV_VertexID){V v;float2 t=float2((i<<1)&2,i&2);"
      "v.p=float4(t*float2(2,-2)+float2(-1,1),0,1);"
      "[unroll]for(uint j=0;j<16;++j)v.t[j]=float4(t,0,0);return v;}";
  ComPtr<ID3DBlob> vs;
  Need(SUCCEEDED(compile(vs_source,sizeof(vs_source)-1,nullptr,nullptr,nullptr,"main","vs_5_1",0,0,&vs,&errors)),"VS compile");
  std::array<ComPtr<ID3D12PipelineState>,2> pipelines;
  for (unsigned mode=0;mode<2;++mode) {
    const auto& ps=mode?candidate:baseline;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};
    p.pRootSignature=root.Get(); p.VS={vs->GetBufferPointer(),vs->GetBufferSize()}; p.PS={ps.data(),ps.size()};
    p.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    p.SampleMask=UINT_MAX; p.SampleDesc.Count=1;
    p.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID; p.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    p.RasterizerState.DepthClipEnable=TRUE;
    p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    p.NumRenderTargets=1; p.RTVFormats[0]=DXGI_FORMAT_R32G32B32A32_FLOAT;
    const auto created=device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&pipelines[mode]));
    if (FAILED(created)) {
      std::cerr<<"PS pipeline mode="<<mode<<" HRESULT="<<std::hex<<unsigned(created)<<std::dec<<'\n';
      ComPtr<ID3D12InfoQueue> info;
      if (SUCCEEDED(device.As(&info))) for (UINT64 i=0;i<std::min<UINT64>(info->GetNumStoredMessages(),16);++i) {
        SIZE_T size=0; info->GetMessage(i,nullptr,&size); std::vector<uint8_t> message(size);
        if (SUCCEEDED(info->GetMessage(i,reinterpret_cast<D3D12_MESSAGE*>(message.data()),&size)))
          std::cerr<<reinterpret_cast<D3D12_MESSAGE*>(message.data())->pDescription<<'\n';
      }
      throw std::runtime_error("translated PS pipeline");
    }
  }
  auto buffer=[&](UINT64 bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state) {
    ComPtr<ID3D12Resource> result; D3D12_HEAP_PROPERTIES heap{}; heap.Type=type;
    auto desc=BufferDesc(bytes);
    Need(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&result))),"buffer allocation");
    return result;
  };
  struct Case { DXGI_FORMAT host; xenos::TextureFormat guest; unsigned channels; bool signed_value,integer,floating; };
  const Case cases[]={
      {DXGI_FORMAT_R8_UNORM,xenos::TextureFormat::k_8,1,false,false,false},
      {DXGI_FORMAT_R8G8_UNORM,xenos::TextureFormat::k_8_8,2,false,false,false},
      {DXGI_FORMAT_R8G8B8A8_UNORM,xenos::TextureFormat::k_8_8_8_8,4,false,false,false},
      {DXGI_FORMAT_R8_SNORM,xenos::TextureFormat::k_8,1,true,false,false},
      {DXGI_FORMAT_R8_UNORM,xenos::TextureFormat::k_8,1,false,true,false},
      {DXGI_FORMAT_R32G32B32A32_FLOAT,xenos::TextureFormat::k_32_32_32_32_FLOAT,4,false,false,true}};
  unsigned checks=0;
  for (const auto& c:cases) {
    std::array<std::array<float,1024>,2> results{};
    for (unsigned mode=0;mode<2;++mode) {
      D3D12_RESOURCE_DESC td{};
      td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; td.Width=256; td.Height=1;
      td.DepthOrArraySize=td.MipLevels=1; td.SampleDesc.Count=1; td.Format=c.host;
      D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_DEFAULT;
      ComPtr<ID3D12Resource> texture,output;
      Need(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&texture))),"texture allocation");
      auto out_desc=td; out_desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT; out_desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
      Need(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&out_desc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&output))),"output allocation");
      D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT64 bytes=0;
      device->GetCopyableFootprints(&td,0,1,0,&footprint,nullptr,nullptr,&bytes);
      auto upload=buffer(bytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
      uint8_t* data=nullptr; Need(SUCCEEDED(upload->Map(0,nullptr,reinterpret_cast<void**>(&data))),"texture upload map");
      for (unsigned x=0;x<256;++x) for (unsigned channel=0;channel<c.channels;++channel) {
        if (c.floating) {float v=channel==3?1.0f:float(x)/255.0f; std::memcpy(data+4*(x*c.channels+channel),&v,4);}
        else data[x*c.channels+channel]=uint8_t(channel==3?255:x);
      }
      upload->Unmap(0,nullptr);
      D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors=UINT(ranges.size()); hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
      ComPtr<ID3D12DescriptorHeap> srvs,rtvs;
      Need(SUCCEEDED(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&srvs))),"SRV heap");
      hd={}; hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV; hd.NumDescriptors=1;
      Need(SUCCEEDED(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtvs))),"RTV heap");
      auto rtv=rtvs->GetCPUDescriptorHandleForHeapStart(); device->CreateRenderTargetView(output.Get(),nullptr,rtv);
      auto cpu=srvs->GetCPUDescriptorHandleForHeapStart();
      const auto stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      D3D12_SHADER_RESOURCE_VIEW_DESC view{}; view.Format=c.host; view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
      view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; view.Texture2D.MipLevels=1;
      for (size_t i=0;i<ranges.size();++i) {device->CreateShaderResourceView(texture.Get(),&view,cpu); cpu.ptr+=stride;}
      ComPtr<ID3D12CommandAllocator> allocator; ComPtr<ID3D12GraphicsCommandList> commands;
      Need(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))),"allocator");
      Need(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),pipelines[mode].Get(),IID_PPV_ARGS(&commands))),"command list");
      D3D12_TEXTURE_COPY_LOCATION from{},to{}; from.pResource=upload.Get(); from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; from.PlacedFootprint=footprint;
      to.pResource=texture.Get(); to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      commands->CopyTextureRegion(&to,0,0,0,&from,nullptr);
      D3D12_RESOURCE_BARRIER barrier{}; barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Transition={texture.Get(),0,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
      commands->ResourceBarrier(1,&barrier); commands->SetGraphicsRootSignature(root.Get());
      ID3D12DescriptorHeap* heaps[]={srvs.Get()}; commands->SetDescriptorHeaps(1,heaps);
      std::vector<ComPtr<ID3D12Resource>> constants;
      unsigned root_index=0,texture_index=0;
      for (const auto& b:bindings) {
        if (b.Type==D3D_SIT_SAMPLER) continue;
        if (b.Type==D3D_SIT_CBUFFER) {
          auto cb=buffer(4096,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
          void* mapping=nullptr; Need(SUCCEEDED(cb->Map(0,nullptr,&mapping)),"constant map"); std::memset(mapping,0,4096);
          if (b.BindPoint==UINT(DxbcShaderTranslator::CbufferRegister::kSystemConstants)) {
            DxbcShaderTranslator::SystemConstants system{};
            system.flags=DxbcShaderTranslator::kSysFlag_AlphaPassIfLess|DxbcShaderTranslator::kSysFlag_AlphaPassIfEqual|DxbcShaderTranslator::kSysFlag_AlphaPassIfGreater;
            for (auto& bias:system.color_exp_bias) bias=1.0f;
            system.texture_swizzled_signs[0]=c.signed_value?0x55:0;
            std::memcpy(mapping,&system,sizeof(system));
          } else if (b.BindPoint==UINT(DxbcShaderTranslator::CbufferRegister::kFetchConstants)) {
            xenos::xe_gpu_texture_fetch_t fetch{}; fetch.type=xenos::FetchConstantType::kTexture; fetch.format=c.guest; fetch.num_format=c.integer;
            fetch.dimension=xenos::DataDimension::k2DOrStacked; fetch.size_2d.width=255;
            std::memcpy(mapping,&fetch,sizeof(fetch));
          }
          cb->Unmap(0,nullptr); commands->SetGraphicsRootConstantBufferView(root_index,cb->GetGPUVirtualAddress()); constants.push_back(cb);
        } else {auto gpu=srvs->GetGPUDescriptorHandleForHeapStart(); gpu.ptr+=UINT64(texture_index++)*stride; commands->SetGraphicsRootDescriptorTable(root_index,gpu);}
        ++root_index;
      }
      D3D12_VIEWPORT viewport{0,0,256,1,0,1}; D3D12_RECT scissor{0,0,256,1};
      commands->RSSetViewports(1,&viewport); commands->RSSetScissorRects(1,&scissor);
      const float clear[]={-99,-99,-99,-99}; commands->ClearRenderTargetView(rtv,clear,0,nullptr);
      commands->OMSetRenderTargets(1,&rtv,FALSE,nullptr); commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); commands->DrawInstanced(3,1,0,0);
      barrier.Transition={output.Get(),0,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE}; commands->ResourceBarrier(1,&barrier);
      device->GetCopyableFootprints(&out_desc,0,1,0,&footprint,nullptr,nullptr,&bytes);
      auto readback=buffer(bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
      from={}; from.pResource=output.Get(); from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      to={}; to.pResource=readback.Get(); to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; to.PlacedFootprint=footprint;
      commands->CopyTextureRegion(&to,0,0,0,&from,nullptr); Need(SUCCEEDED(commands->Close()),"close list");
      ID3D12CommandList* lists[]={commands.Get()}; queue->ExecuteCommandLists(1,lists);
      ComPtr<ID3D12Fence> fence; Need(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))),"fence");
      HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); Need(event!=nullptr,"fence event");
      const bool completed=SUCCEEDED(queue->Signal(fence.Get(),1)) && SUCCEEDED(fence->SetEventOnCompletion(1,event)) && WaitForSingleObject(event,10000)==WAIT_OBJECT_0;
      CloseHandle(event); Need(completed,"GPU test exceeded ten-second fence limit");
      void* mapping=nullptr; Need(SUCCEEDED(readback->Map(0,nullptr,&mapping)),"readback map");
      std::memcpy(results[mode].data(),mapping,sizeof(results[mode])); readback->Unmap(0,nullptr);
    }
    const bool affected=!c.signed_value&&!c.integer&&!c.floating;
    for (unsigned x=0;x<256;++x) for(unsigned channel=0;channel<4;++channel) {
      const auto before=results[0][x*4+channel],after=results[1][x*4+channel];
      Need(std::isfinite(before)&&std::isfinite(after)&&before!=-99,"shader did not produce finite output");
      if (affected) {
        const unsigned code=channel<c.channels?(channel==3?255:x):(channel==3?255:0);
        const float normalized=float(code)/255.0f;
        if (std::abs(before-normalized)>=1e-6f) {
          std::cerr<<"Baseline format="<<unsigned(c.guest)<<" pixel="<<x<<" channel="<<channel
                   <<" actual="<<before<<" expected="<<normalized<<" candidate="<<after<<'\n';
          throw std::runtime_error("baseline sample differs from input byte");
        }
        const float expected=std::ceil(double(code)*65536.0/255.0)/65536.0;
        Need(after==expected,"experimental GPU normalization differs from fixed16 hypothesis");
        Need(std::lround(after*255.0f)==code,"UNORM8 byte round trip changed");
      } else Need(std::bit_cast<uint32_t>(before)==std::bit_cast<uint32_t>(after),"excluded format changed");
      ++checks;
    }
    if (c.host==DXGI_FORMAT_R8_UNORM&&!c.integer) {
      const float threshold=std::bit_cast<float>(0x3F008100u);
      Need(results[0][128*4]<threshold&&results[1][128*4]>=threshold,"stencil boundary negative control failed");
    }
  }
  std::cout<<checks<<" real-GPU channel checks passed across UNORM8, SNORM8, integer-mode and float controls\n";
}
