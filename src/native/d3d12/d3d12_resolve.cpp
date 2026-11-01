#include "d3d12_render.h"
#include "../commands/resolve_coverage.h"
#include <d3dcompiler.h>
#include <rex/logging.h>
#include <stdexcept>
#include <bit>
#include <rex/graphics/pipeline/texture/info.h>
#include "../shaders/resolve_downsample.h"
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
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_fast_32bpp_1x2xmsaa_scaled_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_fast_32bpp_4xmsaa_scaled_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_fast_64bpp_1x2xmsaa_scaled_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_fast_64bpp_4xmsaa_scaled_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_full_8bpp_scaled_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_full_16bpp_scaled_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_full_32bpp_scaled_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_full_64bpp_scaled_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_full_128bpp_scaled_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_clear_32bpp_scaled_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_clear_64bpp_scaled_cs.h"
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
D3D12Render::PendingPipeline D3D12Render::ComputePSO(ID3D12RootSignature* root, const void* data, size_t bytes) {
  // Downscale bytecode may belong to a temporary compiler blob. Own both
  // inputs until creation completes, including when the job is still queued.
  const auto* begin = static_cast<const uint8_t*>(data);
  return pipeline_workers_->Submit<Ptr<ID3D12PipelineState>>(
      [this, root = Ptr<ID3D12RootSignature>(root), code = std::vector<uint8_t>(begin, begin + bytes)] {
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = root.Get(); desc.CS = {code.data(), code.size()};
    Ptr<ID3D12PipelineState> pipeline;
    Check(device_->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipeline)), "pipeline");
    return pipeline;
  });
}
void D3D12Render::Resolve(const RegisterFile& r, rex::memory::Memory& memory) {
  SubmitEarly();
  recorded_work_ += eager_threshold_;
  Timer timer(stats_enabled_, resolve_ns_);
  guest_memory_ = &memory;
  const auto fetch = r.GetVertexFetch(0);
  if (fetch.type == xenos::FetchConstantType::kVertex && fetch.size == 6 &&
      uint64_t(fetch.address) * 4 + 24 <= 0x20000000) {
    SyncGuestRead(fetch.address << 2, 24, kReadResolveRect); // Rectangle read by the CPU below.
    float vertices[6];
    const auto* guest = memory.TranslatePhysical<const float*>(fetch.address << 2);
    for (unsigned i = 0; i < 6; ++i) vertices[i] = xenos::GpuSwap(guest[i], fetch.endian);
    if (ResolveCoverageEmpty(r, vertices)) {
      // No samples exist to copy or clear: nothing becomes CPU-visible, so as
      // in the pinned executor there is no submission or wait. Pending draws
      // stay ordered in the list; no destination writes are fabricated.
      ++resolve_count_;
      ++empty_resolve_count_;
      return;
    }
  }
  draw_util::ResolveInfo info;
  if (!draw_util::GetResolveInfo(r, memory, scale_x_, scale_y_, false, false, info)) {
    const auto f = r.GetVertexFetch(0);
    REXLOG_ERROR("Native resolve fetch address={:08X} size={} endian={} surface={:08X} scissor TL={:08X} BR={:08X}",
        f.address << 2, f.size, uint32_t(f.endian), r[XE_GPU_REG_RB_SURFACE_INFO],
        r[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL], r[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR]);
    if (f.type == xenos::FetchConstantType::kVertex && f.size == 6) {
      const auto* v = memory.TranslatePhysical<const float*>(f.address << 2);
      REXLOG_ERROR("Native resolve vertices ({},{}) ({},{}) ({},{})", xenos::GpuSwap(v[0], f.endian), xenos::GpuSwap(v[1], f.endian),
          xenos::GpuSwap(v[2], f.endian), xenos::GpuSwap(v[3], f.endian), xenos::GpuSwap(v[4], f.endian), xenos::GpuSwap(v[5], f.endian));
    }
    throw std::runtime_error("Native: invalid resolve registers/vertices");
  }
  if (!copy_root_) copy_root_ = ComputeRoot(sizeof(draw_util::ResolveCopyShaderConstants) / 4, true);
  if (!clear_root_) clear_root_ = ComputeRoot(sizeof(draw_util::ResolveClearShaderConstants) / 4, false);
  auto length = info.copy_dest_extent_length;
  const bool scaled = scaled_.enabled();
  const uint32_t area=scale_x_*scale_y_;
  const uint64_t range=uint64_t(info.copy_dest_extent_start)-info.copy_dest_base+length;
  if (uint64_t(info.copy_dest_base)+range>GuestMemoryMirror::kSize)
    throw std::runtime_error("Native scaled resolve outside physical memory");
  if (scaled && length) {
    const uint32_t begin=info.copy_dest_base&~4095u;
    const uint64_t end=(uint64_t(info.copy_dest_extent_start)+length+4095)&~uint64_t(4095);
    SyncGuestRead(begin,end-begin,kReadResolveRect);
  }
  const uint64_t slot = (uint64_t(length) + 255) & ~uint64_t(255);
  if (view_count_ > 65000) Flush(kFlushCapacity);
  ReserveReadback(slot);
  ID3D12DescriptorHeap* heaps[]{views_.Get(), samplers_.Get()}; cmd_.SetDescriptorHeaps(2, heaps);
  if (length) {
    draw_util::ResolveCopyShaderConstants constants; uint32_t gx, gy;
    auto index = info.GetCopyShader(scale_x_, scale_y_, constants, gx, gy);
    if (index == draw_util::ResolveCopyShaderIndex::kUnknown) throw std::runtime_error("Native: unsupported resolve conversion");
    const auto& si = draw_util::resolve_copy_shader_info[size_t(index)];
    if (!copy_pipelines_[size_t(index)].valid()) {
      using namespace kernels;
      const D3D12_SHADER_BYTECODE code[]{
        {resolve_fast_32bpp_1x2xmsaa_cs, sizeof(resolve_fast_32bpp_1x2xmsaa_cs)},
        {resolve_fast_32bpp_4xmsaa_cs, sizeof(resolve_fast_32bpp_4xmsaa_cs)},
        {resolve_fast_64bpp_1x2xmsaa_cs, sizeof(resolve_fast_64bpp_1x2xmsaa_cs)},
        {resolve_fast_64bpp_4xmsaa_cs, sizeof(resolve_fast_64bpp_4xmsaa_cs)},
        {resolve_full_8bpp_cs, sizeof(resolve_full_8bpp_cs)}, {resolve_full_16bpp_cs, sizeof(resolve_full_16bpp_cs)},
        {resolve_full_32bpp_cs, sizeof(resolve_full_32bpp_cs)}, {resolve_full_64bpp_cs, sizeof(resolve_full_64bpp_cs)},
        {resolve_full_128bpp_cs, sizeof(resolve_full_128bpp_cs)}};
      const D3D12_SHADER_BYTECODE scaled_code[]{
        {resolve_fast_32bpp_1x2xmsaa_scaled_cs, sizeof(resolve_fast_32bpp_1x2xmsaa_scaled_cs)},
        {resolve_fast_32bpp_4xmsaa_scaled_cs, sizeof(resolve_fast_32bpp_4xmsaa_scaled_cs)},
        {resolve_fast_64bpp_1x2xmsaa_scaled_cs, sizeof(resolve_fast_64bpp_1x2xmsaa_scaled_cs)},
        {resolve_fast_64bpp_4xmsaa_scaled_cs, sizeof(resolve_fast_64bpp_4xmsaa_scaled_cs)},
        {resolve_full_8bpp_scaled_cs, sizeof(resolve_full_8bpp_scaled_cs)}, {resolve_full_16bpp_scaled_cs, sizeof(resolve_full_16bpp_scaled_cs)},
        {resolve_full_32bpp_scaled_cs, sizeof(resolve_full_32bpp_scaled_cs)}, {resolve_full_64bpp_scaled_cs, sizeof(resolve_full_64bpp_scaled_cs)},
        {resolve_full_128bpp_scaled_cs, sizeof(resolve_full_128bpp_scaled_cs)}};
      const auto selected = scaled ? scaled_code[size_t(index)] : code[size_t(index)];
      copy_pipelines_[size_t(index)] = ComputePSO(copy_root_.Get(), selected.pShaderBytecode, selected.BytecodeLength);
    }
    // Bytes of the extent outside the written rectangle stay as they are:
    // the mirror is made current (CPU-written pages only) before the copy.
    auto& mirror = Mirror(memory);
    mirror.RequestRange(info.copy_dest_extent_start, length);
    auto ticket=scaled ? scaled_.Capture(info.copy_dest_extent_start,length) : ScaledResolveCache::Ticket{};
    const unsigned pixel_log2 = info.IsCopyingDepth() ? 2u : std::countr_zero(unsigned(FormatInfo::Get(xenos::TextureFormat(info.copy_dest_info.copy_dest_format))->bits_per_pixel / 8));
    Ptr<ID3D12Resource> scaled_buffer, scaled_readback;
    D3D12_RESOURCE_STATES scaled_state=D3D12_RESOURCE_STATE_COPY_DEST;
    if (scaled) {
      if (range*area > (uint64_t(1)<<29)) throw std::runtime_error("Native scaled resolve exceeds D3D12 buffer view limit");
      auto initial=scaled_.Hydrate(info.copy_dest_base,uint32_t(range),pixel_log2,memory);
      scaled_buffer=Buffer(initial.size(),D3D12_HEAP_TYPE_DEFAULT,scaled_state,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
      scaled_readback=Buffer(initial.size(),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
      auto upload=Upload(initial.data(),initial.size());
      cmd_.CopyBufferRegion(scaled_buffer.Get(),0,upload.resource.Get(),upload.offset,initial.size());
      Transition(scaled_buffer.Get(),scaled_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      transient_.push_back(scaled_buffer);
    }
    Transition(memory_.Get(), memory_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Barrier(); Transition(edram_.Get(), edram_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    unsigned v = Views(2);
    D3D12_UNORDERED_ACCESS_VIEW_DESC u{}; u.Format = UintFormat(si.dest_bpe_log2); u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    u.Buffer.NumElements = UINT((scaled ? range*area : 0x20000000ull) >> si.dest_bpe_log2);
    device_->CreateUnorderedAccessView(scaled ? scaled_buffer.Get() : memory_.Get(), nullptr, &u, Cpu(v));
    D3D12_SHADER_RESOURCE_VIEW_DESC s{}; s.Format = si.source_is_raw ? DXGI_FORMAT_R32_TYPELESS : UintFormat(si.source_bpe_log2);
    s.ViewDimension = D3D12_SRV_DIMENSION_BUFFER; s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    s.Buffer.NumElements = edram_size_ >> (si.source_is_raw ? 2 : si.source_bpe_log2);
    s.Buffer.Flags = si.source_is_raw ? D3D12_BUFFER_SRV_FLAG_RAW : D3D12_BUFFER_SRV_FLAG_NONE;
    device_->CreateShaderResourceView(edram_.Get(), &s, Cpu(v + 1));
    cmd_.SetComputeRootSignature(copy_root_.Get()); cmd_.SetPipelineState(copy_pipelines_[size_t(index)], &copy_pipelines_[size_t(index)]);
    cmd_.SetComputeRoot32BitConstants(0, sizeof(constants) / 4, &constants, 0);
    cmd_.SetComputeRootDescriptorTable(1, Gpu(v)); cmd_.SetComputeRootDescriptorTable(2, Gpu(v + 1));
    cmd_.Dispatch(gx, gy, 1);
    // The mirror now holds these bytes: later fetches read them on the GPU;
    // cached textures over the range reload from the mirror.
    if (scaled) {
      Transition(scaled_buffer.Get(),scaled_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      if (!downscale_root_) downscale_root_=ComputeRoot(6,true);
      if (!downscale_pipeline_.valid()) {
        Ptr<ID3DBlob> code,errors;
        Check(D3DCompile(kResolveDownsampleHlsl,sizeof(kResolveDownsampleHlsl)-1,"native-resolve-downsample",nullptr,nullptr,
            "main","cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors),"compile canonical resolve publication");
        downscale_pipeline_=ComputePSO(downscale_root_.Get(),code->GetBufferPointer(),code->GetBufferSize());
      }
      const unsigned views=Views(2);
      D3D12_UNORDERED_ACCESS_VIEW_DESC destination{}; destination.Format=DXGI_FORMAT_R32_TYPELESS;
      destination.ViewDimension=D3D12_UAV_DIMENSION_BUFFER; destination.Buffer.NumElements=GuestMemoryMirror::kSize/4;
      destination.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;
      device_->CreateUnorderedAccessView(memory_.Get(),nullptr,&destination,Cpu(views));
      D3D12_SHADER_RESOURCE_VIEW_DESC source{}; source.Format=DXGI_FORMAT_R32_TYPELESS;
      source.ViewDimension=D3D12_SRV_DIMENSION_BUFFER; source.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      source.Buffer.NumElements=UINT(range*area/4); source.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;
      device_->CreateShaderResourceView(scaled_buffer.Get(),&source,Cpu(views+1));
      const uint32_t downsample[]{length,pixel_log2,scale_x_,scale_y_,(info.copy_dest_extent_start-info.copy_dest_base)*area,info.copy_dest_extent_start};
      cmd_.SetComputeRootSignature(downscale_root_.Get()); cmd_.SetPipelineState(downscale_pipeline_, &downscale_pipeline_);
      cmd_.SetComputeRoot32BitConstants(0,6,downsample,0); cmd_.SetComputeRootDescriptorTable(1,Gpu(views));
      cmd_.SetComputeRootDescriptorTable(2,Gpu(views+1)); cmd_.Dispatch((length+255)/256,1,1);
      Transition(scaled_buffer.Get(),scaled_state,D3D12_RESOURCE_STATE_COPY_SOURCE);
      cmd_.CopyBufferRegion(scaled_readback.Get(),0,scaled_buffer.Get(),0,range*area);
    }
    QueueGuestWrite(info.copy_dest_extent_start, length, scaled ? &ticket.cpu_generations : nullptr);
    if (scaled) {
      scaled_.Begin(ticket);
      scaled_readbacks_.push_back({std::move(ticket),scaled_readback,pixel_log2,
          uint64_t(info.copy_dest_extent_start-info.copy_dest_base)*area});
    }
  }
  if (info.IsClearingColor() || info.IsClearingDepth()) {
    Transition(edram_.Get(), edram_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); Barrier();
    auto v = Views(1); D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
    u.Format = DXGI_FORMAT_R32G32B32A32_UINT; u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER; u.Buffer.NumElements = edram_size_ / 16;
    device_->CreateUnorderedAccessView(edram_.Get(), nullptr, &u, Cpu(v));
    cmd_.SetComputeRootSignature(clear_root_.Get()); cmd_.SetComputeRootDescriptorTable(1, Gpu(v));
    auto groups = info.GetClearShaderGroupCount(scale_x_, scale_y_);
    auto clear = [&](bool depth) {
      draw_util::ResolveClearShaderConstants c;
      if (depth) info.GetDepthClearShaderConstants(c); else info.GetColorClearShaderConstants(c);
      auto i = unsigned(!depth && info.color_edram_info.format_is_64bpp);
      if (!clear_pipelines_[i].valid()) clear_pipelines_[i] = scaled ? (i ? ComputePSO(clear_root_.Get(), kernels::resolve_clear_64bpp_scaled_cs, sizeof(kernels::resolve_clear_64bpp_scaled_cs))
          : ComputePSO(clear_root_.Get(), kernels::resolve_clear_32bpp_scaled_cs, sizeof(kernels::resolve_clear_32bpp_scaled_cs))) : i ? ComputePSO(clear_root_.Get(), kernels::resolve_clear_64bpp_cs, sizeof(kernels::resolve_clear_64bpp_cs))
          : ComputePSO(clear_root_.Get(), kernels::resolve_clear_32bpp_cs, sizeof(kernels::resolve_clear_32bpp_cs));
      cmd_.SetPipelineState(clear_pipelines_[i], &clear_pipelines_[i]); cmd_.SetComputeRoot32BitConstants(0, sizeof(c) / 4, &c, 0);
      cmd_.Dispatch(groups.first, groups.second, 1); Barrier();
    };
    if (info.IsClearingDepth()) clear(true); if (info.IsClearingColor()) clear(false);
  }
  ++resolve_count_;
}
}
