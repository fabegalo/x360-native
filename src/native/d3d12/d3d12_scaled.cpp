// GPU-resident scaled resolve memory. Buffer/heap arrangement follows the
// pinned ReXGlue D3D12TextureCache (Xenia, BSD-3-Clause): 2 GB reserved
// buffers start every 1 GB, so any range of up to 1 GB is addressable through
// one of them, and 16 MB heaps are mapped into every buffer covering them.
#include "d3d12_render.h"
#include "../shaders/scaled_memory.h"
#include <d3dcompiler.h>
#include <stdexcept>
#include <string>
namespace x360n::native {
namespace {
constexpr uint32_t kHeapLog2 = 24;
constexpr uint64_t kGigabyte = uint64_t(1) << 30;
void Check(HRESULT h, const char* text) { if (FAILED(h)) throw std::runtime_error(std::string("Native scaled memory: ") + text); }
}
D3D12Render::ScaledWindow D3D12Render::UseScaled(uint32_t start, uint64_t length, D3D12_RESOURCE_STATES state) {
  const uint64_t area = uint64_t(scale_x_) * scale_y_, total = uint64_t(GuestMemoryMirror::kSize) * area;
  const uint64_t first = uint64_t(start) * area, end = (uint64_t(start) + length) * area;
  if (!length || end > total || end - first > kGigabyte) throw std::runtime_error("Native scaled memory: range exceeds 1 GB or physical memory");
  if (scaled_buffers_.empty()) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
    Check(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)), "feature query");
    if (options.TiledResourcesTier == D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED)
      throw std::runtime_error("Native: resolution scale needs D3D12 tiled resources");
    // Every buffer exists before any heap, so each heap is mapped into all of them once.
    for (uint64_t base = 0; base < total; base += kGigabyte) {
      D3D12_RESOURCE_DESC desc{};
      desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width = std::min(2 * kGigabyte, total - base);
      desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1; desc.SampleDesc.Count = 1;
      desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
      ScaledBuffer buffer{nullptr, D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
      Check(device_->CreateReservedResource(&desc, buffer.state, nullptr, IID_PPV_ARGS(&buffer.resource)), "reserved buffer");
      scaled_buffers_.push_back(std::move(buffer));
    }
    scaled_heaps_.resize(size_t(total >> kHeapLog2));
    scaled_gigabytes_.assign(scaled_buffers_.size(), UINT8_MAX);
  }
  for (uint64_t heap = first >> kHeapLog2; heap <= (end - 1) >> kHeapLog2; ++heap) {
    auto& h = scaled_heaps_[size_t(heap)];
    if (h) continue;
    D3D12_HEAP_DESC desc{}; desc.SizeInBytes = uint64_t(1) << kHeapLog2; desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    Check(device_->CreateHeap(&desc, IID_PPV_ARGS(&h)), "tile heap");
    const uint64_t address = heap << kHeapLog2;
    const size_t gigabyte = size_t(address >> 30);
    for (size_t index : {gigabyte, gigabyte - 1}) {
      if (index >= scaled_buffers_.size()) continue;  // gigabyte - 1 wraps for the first one.
      D3D12_TILED_RESOURCE_COORDINATE coordinate{UINT((address - (uint64_t(index) << 30)) / D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES), 0, 0, 0};
      D3D12_TILE_REGION_SIZE size{UINT(desc.SizeInBytes / D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES), FALSE, 0, 0, 0};
      D3D12_TILE_RANGE_FLAGS flags = D3D12_TILE_RANGE_FLAG_NONE;
      UINT heap_offset = 0, tiles = size.NumTiles;
      // Queue order puts the mapping before the list being recorded.
      provider_.GetDirectQueue()->UpdateTileMappings(scaled_buffers_[index].resource.Get(), 1, &coordinate, &size,
          h.Get(), 1, &flags, &heap_offset, &tiles, D3D12_TILE_MAPPING_FLAG_NONE);
    }
  }
  const size_t index = size_t(first >> 30);
  auto& buffer = scaled_buffers_[index];
  for (size_t gigabyte = index; gigabyte <= size_t((end - 1) >> 30); ++gigabyte) {
    auto& current = scaled_gigabytes_[gigabyte];
    if (current != UINT8_MAX && current != index) {
      D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
      b.Aliasing = {scaled_buffers_[current].resource.Get(), buffer.resource.Get()};
      cmd_.ResourceBarrier(1, &b);
    }
    current = uint8_t(index);
  }
  if (buffer.state == state && state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
    D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; b.UAV.pResource = buffer.resource.Get();
    cmd_.ResourceBarrier(1, &b);
  }
  Transition(buffer.resource.Get(), buffer.state, state);
  return {buffer.resource.Get(), first - (uint64_t(index) << 30)};
}
void D3D12Render::ReplicateScaled(uint32_t start, uint32_t length, unsigned pixel_log2, bool mark) {
  const auto runs = scaled_.Stale(start, length);
  if (runs.empty()) return;
  if (!scaled_root_) scaled_root_ = ComputeRoot(8, true);
  auto compile = [&](const char* source, size_t size, const char* name) {
    Ptr<ID3DBlob> code, errors;
    Check(D3DCompile(source, size - 1, name, nullptr, nullptr, "main", "cs_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors), name);
    return ComputePSO(scaled_root_.Get(), code->GetBufferPointer(), code->GetBufferSize());
  };
  if (!replicate_pipeline_.valid()) replicate_pipeline_ = compile(kScaledReplicateHlsl, sizeof(kScaledReplicateHlsl), "native-scaled-replicate");
  const uint32_t area = scale_x_ * scale_y_;
  ID3D12DescriptorHeap* heaps[]{views_.Get(), samplers_.Get()};
  for (auto [run_start, run_length] : runs) {
    mirror_->RequestRange(run_start, run_length);
    Transition(memory_.Get(), memory_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    const auto window = UseScaled(run_start, run_length, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const unsigned v = Views(2);
    D3D12_UNORDERED_ACCESS_VIEW_DESC u{}; u.Format = DXGI_FORMAT_R32_TYPELESS; u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    u.Buffer.FirstElement = window.offset / 4; u.Buffer.NumElements = UINT(uint64_t(run_length) * area / 4); u.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    device_->CreateUnorderedAccessView(window.resource, nullptr, &u, Cpu(v));
    D3D12_SHADER_RESOURCE_VIEW_DESC s{}; s.Format = DXGI_FORMAT_R32_TYPELESS; s.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    s.Buffer.NumElements = GuestMemoryMirror::kSize / 4; s.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
    device_->CreateShaderResourceView(memory_.Get(), &s, Cpu(v + 1));
    cmd_.SetDescriptorHeaps(2, heaps);
    cmd_.SetComputeRootSignature(scaled_root_.Get()); cmd_.SetPipelineState(replicate_pipeline_, &replicate_pipeline_);
    cmd_.SetComputeRootDescriptorTable(1, Gpu(v)); cmd_.SetComputeRootDescriptorTable(2, Gpu(v + 1));
    for (uint32_t done = 0; done < run_length; done += kScaledReplicateChunkBytes) {
      const uint32_t chunk = std::min(run_length - done, kScaledReplicateChunkBytes);
      const uint32_t constants[]{chunk * area, pixel_log2, scale_x_, scale_y_, run_start + done, done * area};
      cmd_.SetComputeRoot32BitConstants(0, 6, constants, 0);
      cmd_.Dispatch((chunk * area / 4 + 63) / 64, 1, 1);
    }
    if (mark) scaled_.MarkCurrent(run_start, run_length, false);
  }
}
D3D12_GPU_VIRTUAL_ADDRESS D3D12Render::PrepareScanout(const ScanoutTexture& input, ScanoutTexture& output) {
  if (!mirror_ || !scaled_.Overlaps(input.address, input.bytes)) return 0;
  const uint64_t width = uint64_t(input.width) * scale_x_, height = uint64_t(input.height) * scale_y_;
  const uint64_t pitch = (width + 31) & ~uint64_t(31), size = pitch * height * 4;
  if (!width || !height || size > UINT32_MAX || width > 16384 || height > 16384 || (input.address & 15))
    throw std::runtime_error("Native: requested scale exceeds scanout texture limits");
  output = input; output.width = uint32_t(width); output.height = uint32_t(height); output.pitch = uint32_t(pitch);
  output.tiled = 0; output.bytes = uint32_t(size);
  ReplicateScaled(input.address, input.bytes, 2, true);
  if (!scaled_scanout_pipeline_.valid()) {
    Ptr<ID3DBlob> code, errors;
    Check(D3DCompile(kScaledScanoutHlsl, sizeof(kScaledScanoutHlsl) - 1, "native-scaled-scanout", nullptr, nullptr, "main", "cs_5_1",
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors), "compile scaled scanout");
    if (!scaled_root_) scaled_root_ = ComputeRoot(8, true);
    scaled_scanout_pipeline_ = ComputePSO(scaled_root_.Get(), code->GetBufferPointer(), code->GetBufferSize());
  }
  if (scanout_size_ < size) {
    if (scanout_buffer_) transient_.push_back(scanout_buffer_);  // A recorded scanout may still read it.
    scanout_buffer_ = Buffer(size, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    scanout_size_ = size; scanout_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  }
  const auto window = UseScaled(input.address, input.bytes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  Transition(scanout_buffer_.Get(), scanout_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  const unsigned v = Views(2);
  D3D12_UNORDERED_ACCESS_VIEW_DESC u{}; u.Format = DXGI_FORMAT_R32_TYPELESS; u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  u.Buffer.NumElements = UINT(size / 4); u.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  device_->CreateUnorderedAccessView(scanout_buffer_.Get(), nullptr, &u, Cpu(v));
  D3D12_SHADER_RESOURCE_VIEW_DESC s{}; s.Format = DXGI_FORMAT_R32_TYPELESS; s.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  s.Buffer.FirstElement = window.offset / 4; s.Buffer.NumElements = UINT(uint64_t(input.bytes) * scale_x_ * scale_y_ / 4);
  s.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  device_->CreateShaderResourceView(window.resource, &s, Cpu(v + 1));
  ID3D12DescriptorHeap* heaps[]{views_.Get(), samplers_.Get()}; cmd_.SetDescriptorHeaps(2, heaps);
  cmd_.SetComputeRootSignature(scaled_root_.Get()); cmd_.SetPipelineState(scaled_scanout_pipeline_, &scaled_scanout_pipeline_);
  const uint32_t constants[]{output.width, output.height, output.pitch, input.pitch, input.tiled, scale_x_, scale_y_};
  cmd_.SetComputeRoot32BitConstants(0, 7, constants, 0);
  cmd_.SetComputeRootDescriptorTable(1, Gpu(v)); cmd_.SetComputeRootDescriptorTable(2, Gpu(v + 1));
  cmd_.Dispatch((output.width + 7) / 8, (output.height + 7) / 8, 1);
  Transition(scanout_buffer_.Get(), scanout_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  return scanout_buffer_->GetGPUVirtualAddress();
}
}
