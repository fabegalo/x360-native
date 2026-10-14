#include "d3d12_render.h"
#include <d3dcompiler.h>
#include <rex/logging.h>
#include <stdexcept>
namespace x360n::native {
using namespace rex::graphics;
namespace kernels {
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_fast_32bpp_1x2xmsaa_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_fast_32bpp_4xmsaa_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_fast_64bpp_1x2xmsaa_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_fast_64bpp_4xmsaa_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_full_8bpp_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_full_16bpp_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_full_32bpp_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_full_64bpp_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_full_128bpp_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_clear_32bpp_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_clear_64bpp_cs.h"
}
namespace {
void Check(HRESULT h, const char* text) { if (FAILED(h)) throw std::runtime_error(std::string("Native resolve: ") + text); }
DXGI_FORMAT UintFormat(unsigned log2) {
  switch(log2) { case 2: return DXGI_FORMAT_R32_UINT; case 3: return DXGI_FORMAT_R32G32_UINT; case 4: return DXGI_FORMAT_R32G32B32A32_UINT; }
  throw std::runtime_error("Native: unsupported resolve element size");
}
}
D3D12Render::Ptr<ID3D12RootSignature> D3D12Render::ComputeRoot(unsigned count, bool source) {
  D3D12_ROOT_PARAMETER p[3]{};
  p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; p[0].Constants = {0, 0, count};
  D3D12_DESCRIPTOR_RANGE ranges[]{{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0}, {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0}};
  for (unsigned i = 1; i <= (source ? 2u : 1u); ++i) {
    p[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[i].DescriptorTable = {1, ranges + i - 1};
  }
  D3D12_ROOT_SIGNATURE_DESC desc{source ? 3u : 2u, p, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
  Ptr<ID3DBlob> blob, errors; Check(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors), "root serialize");
  Ptr<ID3D12RootSignature> result;
  Check(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&result)), "root"); return result;
}
D3D12Render::Ptr<ID3D12PipelineState> D3D12Render::ComputePSO(ID3D12RootSignature* root, const void* data, size_t bytes) {
  D3D12_COMPUTE_PIPELINE_STATE_DESC desc{}; desc.pRootSignature = root; desc.CS = {data, bytes};
  Ptr<ID3D12PipelineState> p; Check(device_->CreateComputePipelineState(&desc, IID_PPV_ARGS(&p)), "pipeline"); return p;
}
void D3D12Render::Resolve(const RegisterFile& r, rex::memory::Memory& memory) {
  Timer timer(stats_enabled_, resolve_ns_);
  draw_util::ResolveInfo info;
  if (!draw_util::GetResolveInfo(r, memory, 1, 1, false, false, info)) throw std::runtime_error("Native: invalid resolve registers/vertices");
  if (!copy_root_) copy_root_ = ComputeRoot(sizeof(draw_util::ResolveCopyShaderConstants) / 4, true);
  if (!clear_root_) clear_root_ = ComputeRoot(sizeof(draw_util::ResolveClearShaderConstants) / 4, false);
  if (view_count_ > 65000) Flush();
  ID3D12DescriptorHeap* heaps[]{views_.Get(), samplers_.Get()}; list_->SetDescriptorHeaps(2, heaps);
  auto length = info.copy_dest_extent_length;
  Ptr<ID3D12Resource> readback;
  if (length) {
    draw_util::ResolveCopyShaderConstants constants; uint32_t gx, gy;
    auto index = info.GetCopyShader(1, 1, constants, gx, gy);
    if (index == draw_util::ResolveCopyShaderIndex::kUnknown) throw std::runtime_error("Native: unsupported resolve conversion");
    const auto& si = draw_util::resolve_copy_shader_info[size_t(index)];
    if (!copy_pipelines_[size_t(index)]) {
      using namespace kernels;
      const D3D12_SHADER_BYTECODE code[]{
        {resolve_fast_32bpp_1x2xmsaa_cs, sizeof(resolve_fast_32bpp_1x2xmsaa_cs)},
        {resolve_fast_32bpp_4xmsaa_cs, sizeof(resolve_fast_32bpp_4xmsaa_cs)},
        {resolve_fast_64bpp_1x2xmsaa_cs, sizeof(resolve_fast_64bpp_1x2xmsaa_cs)},
        {resolve_fast_64bpp_4xmsaa_cs, sizeof(resolve_fast_64bpp_4xmsaa_cs)},
        {resolve_full_8bpp_cs, sizeof(resolve_full_8bpp_cs)}, {resolve_full_16bpp_cs, sizeof(resolve_full_16bpp_cs)},
        {resolve_full_32bpp_cs, sizeof(resolve_full_32bpp_cs)}, {resolve_full_64bpp_cs, sizeof(resolve_full_64bpp_cs)},
        {resolve_full_128bpp_cs, sizeof(resolve_full_128bpp_cs)}};
      copy_pipelines_[size_t(index)] = ComputePSO(copy_root_.Get(), code[size_t(index)].pShaderBytecode, code[size_t(index)].BytecodeLength);
    }
    UploadGuest(memory, info.copy_dest_extent_start, length);
    Transition(memory_.Get(), memory_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Barrier(); Transition(edram_.Get(), edram_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    unsigned v = Views(2);
    D3D12_UNORDERED_ACCESS_VIEW_DESC u{}; u.Format = UintFormat(si.dest_bpe_log2); u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    u.Buffer.NumElements = 0x20000000 >> si.dest_bpe_log2;
    device_->CreateUnorderedAccessView(memory_.Get(), nullptr, &u, Cpu(v));
    D3D12_SHADER_RESOURCE_VIEW_DESC s{}; s.Format = si.source_is_raw ? DXGI_FORMAT_R32_TYPELESS : UintFormat(si.source_bpe_log2);
    s.ViewDimension = D3D12_SRV_DIMENSION_BUFFER; s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    s.Buffer.NumElements = xenos::kEdramSizeBytes >> (si.source_is_raw ? 2 : si.source_bpe_log2);
    s.Buffer.Flags = si.source_is_raw ? D3D12_BUFFER_SRV_FLAG_RAW : D3D12_BUFFER_SRV_FLAG_NONE;
    device_->CreateShaderResourceView(edram_.Get(), &s, Cpu(v + 1));
    list_->SetComputeRootSignature(copy_root_.Get()); list_->SetPipelineState(copy_pipelines_[size_t(index)].Get());
    list_->SetComputeRoot32BitConstants(0, sizeof(constants) / 4, &constants, 0);
    list_->SetComputeRootDescriptorTable(1, Gpu(v)); list_->SetComputeRootDescriptorTable(2, Gpu(v + 1));
    list_->Dispatch(gx, gy, 1);
    Transition(memory_.Get(), memory_state_, D3D12_RESOURCE_STATE_COPY_SOURCE);
    readback = Buffer(length, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    list_->CopyBufferRegion(readback.Get(), 0, memory_.Get(), info.copy_dest_extent_start, length);
  }
  if (info.IsClearingColor() || info.IsClearingDepth()) {
    Transition(edram_.Get(), edram_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); Barrier();
    auto v = Views(1); D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
    u.Format = DXGI_FORMAT_R32G32B32A32_UINT; u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER; u.Buffer.NumElements = xenos::kEdramSizeBytes / 16;
    device_->CreateUnorderedAccessView(edram_.Get(), nullptr, &u, Cpu(v));
    list_->SetComputeRootSignature(clear_root_.Get()); list_->SetComputeRootDescriptorTable(1, Gpu(v));
    auto groups = info.GetClearShaderGroupCount(1, 1);
    auto clear = [&](bool depth) {
      draw_util::ResolveClearShaderConstants c;
      if (depth) info.GetDepthClearShaderConstants(c); else info.GetColorClearShaderConstants(c);
      auto i = unsigned(!depth && info.color_edram_info.format_is_64bpp);
      if (!clear_pipelines_[i]) clear_pipelines_[i] = i ? ComputePSO(clear_root_.Get(), kernels::resolve_clear_64bpp_cs, sizeof(kernels::resolve_clear_64bpp_cs))
          : ComputePSO(clear_root_.Get(), kernels::resolve_clear_32bpp_cs, sizeof(kernels::resolve_clear_32bpp_cs));
      list_->SetPipelineState(clear_pipelines_[i].Get()); list_->SetComputeRoot32BitConstants(0, sizeof(c) / 4, &c, 0);
      list_->Dispatch(groups.first, groups.second, 1); Barrier();
    };
    if (info.IsClearingDepth()) clear(true); if (info.IsClearingColor()) clear(false);
  }
  Flush();
  if (readback) {
    void* data; D3D12_RANGE range{0, length}; Check(readback->Map(0, &range, &data), "readback");
    std::memcpy(memory.TranslatePhysical(info.copy_dest_extent_start), data, length);
    D3D12_RANGE empty{}; readback->Unmap(0, &empty);
  }
  ++resolve_count_;
}
}
