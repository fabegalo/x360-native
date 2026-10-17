#include "../../src/native/textures/texture_load.h"
#include <rex/graphics/pipeline/texture/info.h>
#include <catch2/catch_test_macros.hpp>
#include <d3d12.h>
#include <wrl/client.h>
#include <cstring>
#include <random>
#include <sstream>
#include <vector>
using Microsoft::WRL::ComPtr;
using namespace x360n::native;
using namespace rex::graphics;
using namespace rex::graphics::xenos;
TEST_CASE("Native inactive texture alternatives do not require guest memory") {
  xe_gpu_texture_fetch_t f{}; f.type = FetchConstantType::kTexture;
  f.dimension = DataDimension::k2DOrStacked; f.format = TextureFormat::k_2_10_10_10; f.swizzle = 0x688;
  REQUIRE(NeedsCpuConversion(f.format, true, false));
  REQUIRE_FALSE(TextureViewActive(f, FetchOpDimension::k2D, true));
  REQUIRE(TextureViewActive(f, FetchOpDimension::k2D, false));
  REQUIRE_FALSE(TextureViewActive(f, FetchOpDimension::k3DOrStacked, false));
  REQUIRE_FALSE(TextureViewActive(f, FetchOpDimension::kCube, false));
  f.sign_x = TextureSign::kSigned;
  REQUIRE(TextureViewActive(f, FetchOpDimension::k2D, true));
  REQUIRE(TextureViewActive(f, FetchOpDimension::k2D, false));
  f.sign_y = f.sign_z = f.sign_w = TextureSign::kSigned;
  REQUIRE_FALSE(TextureViewActive(f, FetchOpDimension::k2D, false));
  f.dimension = DataDimension::k3D;
  REQUIRE(TextureViewActive(f, FetchOpDimension::k3DOrStacked, true));
  REQUIRE(TextureViewActive(f, FetchOpDimension::k2D, true));
  f.dimension = DataDimension::kCube;
  REQUIRE(TextureViewActive(f, FetchOpDimension::kCube, true));
  REQUIRE_FALSE(TextureViewActive(f, FetchOpDimension::k2D, true));
  f.type = FetchConstantType::kInvalidTexture;
  REQUIRE_FALSE(TextureViewActive(f, FetchOpDimension::kCube, true));
}
namespace {
struct Gpu {
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  ComPtr<ID3D12Fence> fence;
  ComPtr<ID3D12DescriptorHeap> heap;
  ComPtr<ID3D12RootSignature> root;
  uint64_t serial = 0;
  ComPtr<ID3D12Resource> Buffer(uint64_t size, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
    D3D12_HEAP_PROPERTIES heap_props{}; heap_props.Type = type;
    D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = size; d.Height = 1; d.DepthOrArraySize = 1;
    d.MipLevels = 1; d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; d.Flags = flags;
    ComPtr<ID3D12Resource> r;
    REQUIRE(SUCCEEDED(device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(&r))));
    return r;
  }
  void Submit() {
    REQUIRE(SUCCEEDED(list->Close()));
    ID3D12CommandList* lists[]{list.Get()};
    queue->ExecuteCommandLists(1, lists);
    REQUIRE(SUCCEEDED(queue->Signal(fence.Get(), ++serial)));
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    REQUIRE(SUCCEEDED(fence->SetEventOnCompletion(serial, event)));
    WaitForSingleObject(event, 10000); CloseHandle(event);
    REQUIRE(SUCCEEDED(device->GetDeviceRemovedReason()));
    REQUIRE(SUCCEEDED(allocator->Reset()));
    REQUIRE(SUCCEEDED(list->Reset(allocator.Get(), nullptr)));
  }
};
DXGI_FORMAT HostFormat(TextureFormat f) {
  switch (GetBaseFormat(f)) {
    case TextureFormat::k_8: return DXGI_FORMAT_R8_UNORM;
    case TextureFormat::k_8_8: return DXGI_FORMAT_R8G8_UNORM;
    case TextureFormat::k_8_8_8_8: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case TextureFormat::k_2_10_10_10: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case TextureFormat::k_16_16_FLOAT: return DXGI_FORMAT_R16G16_FLOAT;
    case TextureFormat::k_16_16_16_16: return DXGI_FORMAT_R16G16B16A16_UNORM;
    case TextureFormat::k_32_FLOAT: case TextureFormat::k_24_8: case TextureFormat::k_24_8_FLOAT: return DXGI_FORMAT_R32_FLOAT;
    case TextureFormat::k_DXT1: return DXGI_FORMAT_BC1_UNORM;
    case TextureFormat::k_DXT4_5: return DXGI_FORMAT_BC3_UNORM;
    case TextureFormat::k_DXN: return DXGI_FORMAT_BC5_UNORM;
    case TextureFormat::k_DXT5A: return DXGI_FORMAT_BC4_UNORM;
    default: FAIL("format not in test table"); return DXGI_FORMAT_UNKNOWN;
  }
}
// Loads random guest bytes both ways and compares every texel row byte.
void Compare(Gpu& gpu, TextureFormat format, DataDimension dimension, bool tiled, Endian endian,
    uint32_t width, uint32_t height, uint32_t depth, uint32_t max_level, bool packed, bool host_volume, std::mt19937& random,
    bool mip_only = false) {
  const uint32_t pitch = tiled ? (width + 31) / 32 : (width + 31) / 32;
  TextureLoadInput t{format, dimension, endian, tiled, packed, width, height, depth, pitch, 0x10000, 0x800000, max_level,
      host_volume, dimension == DataDimension::kCube || (dimension == DataDimension::k2DOrStacked && depth > 1) ? depth : 1,
      HostFormat(format), false, false};
  if (mip_only) t.base = 0;
  const auto shader = GpuLoadShader(t);
  REQUIRE(shader != LoadShader::kUnknown);
  const auto layout = t.Layout();
  const uint32_t base_bytes = (layout.base.level_data_extent_bytes + 3u) & ~3u;
  const uint32_t mip_bytes = max_level ? (layout.mips_total_extent_bytes + 3u) & ~3u : 0;
  REQUIRE(t.base + base_bytes <= t.mip);
  const uint64_t memory_size = uint64_t(t.mip) + mip_bytes + 65536;
  std::vector<uint8_t> memory(memory_size);
  for (auto& b : memory) b = uint8_t(random());
  if (format == TextureFormat::k_24_8_FLOAT) {
    // Include zero, denormal and maximal exponents.
    for (uint32_t i = 0; i + 4 <= memory.size(); i += 4) if ((i >> 2) % 7 == 0) { memory[i] &= 0x0F; memory[i + 1] = 0; }
  }
  const unsigned levels = max_level + 1;
  D3D12_RESOURCE_DESC d{}; d.Dimension = host_volume ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  d.Width = width; d.Height = height; d.DepthOrArraySize = UINT16(host_volume ? depth : t.array_size); d.MipLevels = UINT16(levels);
  d.Format = t.host_format; d.SampleDesc.Count = 1;
  const unsigned subresources = levels * (host_volume ? 1 : t.array_size);
  std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(subresources);
  std::vector<UINT> rows(subresources); std::vector<UINT64> row_bytes(subresources);
  uint64_t total;
  gpu.device->GetCopyableFootprints(&d, 0, subresources, 0, footprints.data(), rows.data(), row_bytes.data(), &total);
  std::vector<uint8_t> expected(total);
  ConvertTextureCpu(t, {memory.data() + t.base, base_bytes}, {memory.data() + t.mip, mip_bytes}, footprints, expected.data());
  // GPU: upload guest memory, load into the texture, read it back.
  auto upload = gpu.Buffer(memory_size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
  void* mapped; D3D12_RANGE none{};
  REQUIRE(SUCCEEDED(upload->Map(0, &none, &mapped))); std::memcpy(mapped, memory.data(), memory_size); upload->Unmap(0, nullptr);
  auto mirror = gpu.Buffer(memory_size, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
  gpu.list->CopyBufferRegion(mirror.Get(), 0, upload.Get(), 0, memory_size);
  D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition = {mirror.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
  gpu.list->ResourceBarrier(1, &b);
  const auto plan = PlanGpuTextureLoad(t, shader);
  constexpr uint64_t scratch_offset = 65536;
  auto scratch = gpu.Buffer(scratch_offset + plan.scratch_bytes + 4096, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  D3D12_HEAP_PROPERTIES heap_props{}; heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
  ComPtr<ID3D12Resource> texture;
  REQUIRE(SUCCEEDED(gpu.device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture))));
  const UINT increment = gpu.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  auto cpu = gpu.heap->GetCPUDescriptorHandleForHeapStart(); auto gpu_handle = gpu.heap->GetGPUDescriptorHandleForHeapStart();
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format = UintPow2Format(plan.source_bpe_log2); srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Buffer.NumElements = UINT(memory_size >> plan.source_bpe_log2);
  gpu.device->CreateShaderResourceView(mirror.Get(), &srv, cpu);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav{}; uav.Format = UintPow2Format(plan.dest_bpe_log2); uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = UINT((scratch_offset + plan.scratch_bytes + 4096) >> plan.dest_bpe_log2);
  cpu.ptr += increment; gpu.device->CreateUnorderedAccessView(scratch.Get(), nullptr, &uav, cpu);
  auto pipeline = CreateTextureLoadPipeline(gpu.device.Get(), gpu.root.Get(), shader);
  ID3D12DescriptorHeap* heaps[]{gpu.heap.Get()}; gpu.list->SetDescriptorHeaps(1, heaps);
  gpu.list->SetComputeRootSignature(gpu.root.Get()); gpu.list->SetPipelineState(pipeline.Get());
  gpu.list->SetComputeRootDescriptorTable(1, gpu_handle);
  gpu_handle.ptr += increment; gpu.list->SetComputeRootDescriptorTable(2, gpu_handle);
  RecordTextureLoad(gpu.list.Get(), plan, scratch_offset, scratch.Get(), texture.Get());
  b.Transition = {texture.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE};
  gpu.list->ResourceBarrier(1, &b);
  auto readback = gpu.Buffer(total, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
  for (unsigned s = 0; s < subresources; ++s) {
    if (mip_only && s % levels == 0) continue;
    D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = readback.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = footprints[s];
    D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = texture.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.SubresourceIndex = s;
    gpu.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  }
  gpu.Submit();
  D3D12_RANGE range{0, size_t(total)};
  REQUIRE(SUCCEEDED(readback->Map(0, &range, &mapped)));
  const auto* actual = static_cast<const uint8_t*>(mapped);
  size_t mismatches = 0; std::ostringstream first;
  for (unsigned s = 0; s < subresources; ++s) {
    if (mip_only && s % levels == 0) continue;
    const auto& fp = footprints[s];
    for (unsigned z = 0; z < fp.Footprint.Depth; ++z)
      for (unsigned row = 0; row < rows[s]; ++row) {
        const uint64_t offset = fp.Offset + (uint64_t(z) * rows[s] + row) * fp.Footprint.RowPitch;
        for (uint64_t x = 0; x < row_bytes[s]; ++x)
          if (actual[offset + x] != expected[offset + x] && !mismatches++)
            first << "subresource " << s << " z " << z << " row " << row << " byte " << x << " gpu " << unsigned(actual[offset + x])
                  << " cpu " << unsigned(expected[offset + x]);
      }
  }
  readback->Unmap(0, &none);
  INFO("format=" << uint32_t(format) << " dim=" << uint32_t(dimension) << " tiled=" << tiled << " endian=" << uint32_t(endian)
       << " size=" << width << "x" << height << "x" << depth << " levels=" << levels << " packed=" << packed << " " << first.str());
  CHECK(mismatches == 0);
}
}
TEST_CASE("Native mip-only loads omit the unavailable base", "[native-mip-only]") {
  for (bool packed : {false, true}) for (uint32_t width : {16u, 64u}) {
    TextureLoadInput t{TextureFormat::k_8_8_8_8, DataDimension::kCube, Endian::k8in32,
        true, packed, width, width, 6, (width + 31) / 32, 0, 0x800000, 4, false, 6,
        DXGI_FORMAT_R8G8B8A8_UNORM, false, false};
    CHECK(t.Layout().base.level_data_extent_bytes == 0);
    const auto plan = PlanGpuTextureLoad(t, GpuLoadShader(t));
    REQUIRE(plan.copies.size() == 4 * 6);
    for (const auto& copy : plan.copies) CHECK(copy.subresource % 5 != 0);
    for (const auto& dispatch : plan.dispatches) CHECK(dispatch.constants.guest_offset >= t.mip);
    // The CPU fallback must leave the absent level untouched, even with an empty source.
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(30);
    uint64_t bytes = 0;
    for (unsigned slice = 0; slice < 6; ++slice) for (unsigned level = 0; level < 5; ++level) {
      auto& fp = footprints[slice * 5 + level]; fp.Offset = bytes;
      fp.Footprint.RowPitch = 256;
      bytes += 256 * std::max(width >> level, 1u);
    }
    std::vector<uint8_t> output(bytes, 0xA5), mips(t.Layout().mips_total_extent_bytes);
    ConvertTextureCpu(t, {}, mips, footprints, output.data());
    for (unsigned slice = 0; slice < 6; ++slice)
      CHECK(output[footprints[slice * 5].Offset] == 0xA5);
  }
}

TEST_CASE("Native mip-only GPU bytes equal CPU conversion", "[native-mip-only]") {
  Gpu gpu;
  REQUIRE(SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&gpu.device))));
  D3D12_COMMAND_QUEUE_DESC q{}; q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  REQUIRE(SUCCEEDED(gpu.device->CreateCommandQueue(&q, IID_PPV_ARGS(&gpu.queue))));
  REQUIRE(SUCCEEDED(gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&gpu.allocator))));
  REQUIRE(SUCCEEDED(gpu.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, gpu.allocator.Get(), nullptr, IID_PPV_ARGS(&gpu.list))));
  REQUIRE(SUCCEEDED(gpu.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gpu.fence))));
  D3D12_DESCRIPTOR_HEAP_DESC h{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
  REQUIRE(SUCCEEDED(gpu.device->CreateDescriptorHeap(&h, IID_PPV_ARGS(&gpu.heap))));
  gpu.root = CreateTextureLoadRoot(gpu.device.Get());
  std::mt19937 random(4321);
  for (bool tiled : {false, true}) for (bool packed : {false, true})
    for (uint32_t width : {16u, 64u}) for (auto format : {TextureFormat::k_8_8_8_8, TextureFormat::k_DXT1}) {
      Compare(gpu, format, DataDimension::kCube, tiled, Endian::k8in32, width, width, 6, 4, packed, false, random, true);
      Compare(gpu, format, DataDimension::k3D, tiled, Endian::k8in32, width, width, 8, 4, packed, true, random, true);
    }
}

TEST_CASE("Native GPU texture loads equal the CPU conversion") {
  Gpu gpu;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&gpu.device)))) SKIP("D3D12 device unavailable");
  D3D12_COMMAND_QUEUE_DESC q{}; q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  REQUIRE(SUCCEEDED(gpu.device->CreateCommandQueue(&q, IID_PPV_ARGS(&gpu.queue))));
  REQUIRE(SUCCEEDED(gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&gpu.allocator))));
  REQUIRE(SUCCEEDED(gpu.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, gpu.allocator.Get(), nullptr, IID_PPV_ARGS(&gpu.list))));
  REQUIRE(SUCCEEDED(gpu.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gpu.fence))));
  D3D12_DESCRIPTOR_HEAP_DESC h{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
  REQUIRE(SUCCEEDED(gpu.device->CreateDescriptorHeap(&h, IID_PPV_ARGS(&gpu.heap))));
  gpu.root = CreateTextureLoadRoot(gpu.device.Get());
  std::mt19937 random(1234);
  const TextureFormat formats[]{TextureFormat::k_8, TextureFormat::k_8_8, TextureFormat::k_8_8_8_8, TextureFormat::k_2_10_10_10,
      TextureFormat::k_16_16_FLOAT, TextureFormat::k_16_16_16_16, TextureFormat::k_32_FLOAT, TextureFormat::k_DXT1,
      TextureFormat::k_DXT4_5, TextureFormat::k_DXN, TextureFormat::k_DXT5A, TextureFormat::k_24_8, TextureFormat::k_24_8_FLOAT};
  for (auto format : formats)
    for (bool tiled : {false, true})
      for (Endian endian : {Endian::kNone, Endian::k8in16, Endian::k8in32, Endian::k16in32}) {
        Compare(gpu, format, DataDimension::k2DOrStacked, tiled, endian, 256, 128, 1, 0, false, false, random);
        Compare(gpu, format, DataDimension::k2DOrStacked, tiled, endian, 1280, 720, 1, 0, false, false, random);
      }
  for (auto format : {TextureFormat::k_8_8_8_8, TextureFormat::k_DXT1, TextureFormat::k_8}) {
    Compare(gpu, format, DataDimension::k2DOrStacked, true, Endian::k8in32, 256, 256, 1, 8, true, false, random);
    Compare(gpu, format, DataDimension::k2DOrStacked, true, Endian::k8in32, 200, 100, 1, 5, true, false, random);
    Compare(gpu, format, DataDimension::k2DOrStacked, true, Endian::k8in32, 64, 64, 4, 3, true, false, random);
    Compare(gpu, format, DataDimension::kCube, true, Endian::k8in32, 64, 64, 6, 6, true, false, random);
    Compare(gpu, format, DataDimension::k3D, true, Endian::k8in32, 32, 32, 8, 0, false, true, random);
    Compare(gpu, format, DataDimension::k3D, true, Endian::k8in32, 32, 32, 8, 0, false, false, random);
  }
  for (auto format : {TextureFormat::k_8_8_8_8, TextureFormat::k_16_16_FLOAT, TextureFormat::k_16_16_16_16})
    for (bool tiled : {false, true})
      for (bool packed : {false, true}) {
        Compare(gpu, format, DataDimension::k3D, tiled, Endian::k8in32, 16, 16, 16, 0, packed, true, random);
        Compare(gpu, format, DataDimension::k3D, tiled, Endian::k8in32, 16, 16, 16, 4, packed, true, random);
      }
}
