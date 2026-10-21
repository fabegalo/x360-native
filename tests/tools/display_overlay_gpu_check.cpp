#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <rex/graphics/display_overlay.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;
void Check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("D3D12 HRESULT " + std::to_string(uint32_t(hr))); }

// Offscreen compute only: no HWND, swapchain, guest image, profile or game.
int main(int argc, char** argv) try {
  if (argc < 2 || argc > 3) return 2;
  using namespace rex::graphics;
  RegisterFile regs{};
  regs[XE_GPU_REG_D1OVL_ENABLE] = 1;
  regs[XE_GPU_REG_D1OVL_SURFACE_ADDRESS] = 0x1000;
  regs[XE_GPU_REG_D1OVL_PITCH] = 32;
  regs[XE_GPU_REG_D1OVL_END] = (4 << 16) | 4;
  auto plane = DecodeDisplayOverlay(regs);
  if (!plane.valid) throw std::runtime_error("Invalid fixture surface");
  // Negative control reproduces the previous scanout decoder, not a different shader.
  if (argc == 3 && std::string(argv[2]) == "--legacy-fetch") {
    plane.fetch.endianness = xenos::Endian::k8in32;
    plane.fetch.swizzle = xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA;
  }
  // Little-endian ARGB surface words: transparent white, opaque red/blue,
  // translucent green, grayscale and a fully transparent colored texel.
  const uint32_t pixels[16]{0x00FFFFFF,0xFFFF0000,0xFF0000FF,0x8000FF00,
      0xFF808080,0x80402010,0x0000FFFF,0xFFFFFFFF,
      0xFF00FF00,0xFF000000,0x4080C020,0xFFFF8000,
      0x00FFFFFF,0xFFFF0000,0xFF0000FF,0x8000FF00};
  std::ifstream in(argv[1]);
  std::string source{std::istreambuf_iterator<char>(in), {}};
  ComPtr<ID3DBlob> shader, errors;
  Check(D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr, "main", "cs_5_1",
      D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &shader, &errors));
  ComPtr<ID3D12Device> device;
  Check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
  D3D12_COMMAND_QUEUE_DESC q{};
  ComPtr<ID3D12CommandQueue> queue;
  Check(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)));
  ComPtr<ID3D12CommandAllocator> allocator;
  Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
  ComPtr<ID3D12GraphicsCommandList> list;
  Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)));
  auto resource = [&](D3D12_HEAP_TYPE heap, D3D12_RESOURCE_DESC desc, D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES props{}; props.Type = heap;
    ComPtr<ID3D12Resource> result;
    Check(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&result)));
    return result;
  };
  auto texture_desc = [](DXGI_FORMAT format, bool output) {
    D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = 4; d.Height = 4; d.DepthOrArraySize = 1; d.MipLevels = 1;
    d.Format = format; d.SampleDesc.Count = 1;
    if (output) d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    return d;
  };
  D3D12_RESOURCE_DESC buffer{}; buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer.Width = 2048; buffer.Height = 1; buffer.DepthOrArraySize = 1; buffer.MipLevels = 1;
  buffer.SampleDesc.Count = 1; buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  auto upload = resource(D3D12_HEAP_TYPE_UPLOAD, buffer, D3D12_RESOURCE_STATE_GENERIC_READ);
  auto readback = resource(D3D12_HEAP_TYPE_READBACK, buffer, D3D12_RESOURCE_STATE_COPY_DEST);
  auto title = resource(D3D12_HEAP_TYPE_DEFAULT, texture_desc(DXGI_FORMAT_R32G32B32A32_FLOAT, false), D3D12_RESOURCE_STATE_COPY_DEST);
  auto overlay = resource(D3D12_HEAP_TYPE_DEFAULT, texture_desc(DXGI_FORMAT_R8G8B8A8_UNORM, false), D3D12_RESOURCE_STATE_COPY_DEST);
  auto output = resource(D3D12_HEAP_TYPE_DEFAULT, texture_desc(DXGI_FORMAT_R16G16B16A16_FLOAT, true), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  uint8_t* data = nullptr; Check(upload->Map(0, nullptr, reinterpret_cast<void**>(&data)));
  for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) {
    const float blue[]{0, 0, 1, 1};
    const auto packed = xenos::GpuSwap(pixels[y * 4 + x], plane.fetch.endianness);
    std::memcpy(data + y * 256 + x * 16, blue, 16);
    std::memcpy(data + 1024 + y * 256 + x * 4, &packed, 4);
  }
  upload->Unmap(0, nullptr);
  auto transition = [&](ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
    D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = r; barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = a; barrier.Transition.StateAfter = b;
    list->ResourceBarrier(1, &barrier);
  };
  for (unsigned i = 0; i < 2; ++i) {
    D3D12_TEXTURE_COPY_LOCATION from{}, to{};
    from.pResource = upload.Get(); from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    from.PlacedFootprint.Offset = i * 1024;
    from.PlacedFootprint.Footprint = {i ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R32G32B32A32_FLOAT, 4, 4, 1, 256};
    to.pResource = i ? overlay.Get() : title.Get(); to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    transition(to.pResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  }
  D3D12_ROOT_PARAMETER params[4]{};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[0].Constants.Num32BitValues = 12;
  D3D12_DESCRIPTOR_RANGE ranges[3]{};
  for (unsigned i = 0; i < 3; ++i) {
    ranges[i].RangeType = i ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV : D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[i].NumDescriptors = 1; ranges[i].BaseShaderRegister = i == 2 ? 1 : 0;
    params[i + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[i + 1].DescriptorTable = {1, &ranges[i]};
  }
  D3D12_ROOT_SIGNATURE_DESC root{4, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
  ComPtr<ID3DBlob> root_blob;
  Check(D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1, &root_blob, &errors));
  ComPtr<ID3D12RootSignature> signature;
  Check(device->CreateRootSignature(0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(), IID_PPV_ARGS(&signature)));
  D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{}; pipeline.pRootSignature = signature.Get();
  pipeline.CS = {shader->GetBufferPointer(), shader->GetBufferSize()};
  ComPtr<ID3D12PipelineState> state; Check(device->CreateComputePipelineState(&pipeline, IID_PPV_ARGS(&state)));
  D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  hd.NumDescriptors = 3; hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ComPtr<ID3D12DescriptorHeap> heap; Check(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)));
  auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
  const auto stride = device->GetDescriptorHandleIncrementSize(hd.Type);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav{}; uav.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  device->CreateUnorderedAccessView(output.Get(), nullptr, &uav, cpu);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; srv.Texture2D.MipLevels = 1;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  cpu.ptr += stride; device->CreateShaderResourceView(title.Get(), &srv, cpu);
  srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  srv.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
      plane.fetch.swizzle & 7, (plane.fetch.swizzle >> 3) & 7,
      (plane.fetch.swizzle >> 6) & 7, (plane.fetch.swizzle >> 9) & 7);
  cpu.ptr += stride; device->CreateShaderResourceView(overlay.Get(), &srv, cpu);
  ComPtr<ID3D12Fence> fence; Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
  struct Event { HANDLE value = CreateEventW(nullptr, FALSE, FALSE, nullptr); ~Event() { if(value) CloseHandle(value); } } event;
  if (!event.value) return 1;
  for (unsigned test = 0; test < 4; ++test) {
    if (test) { Check(allocator->Reset()); Check(list->Reset(allocator.Get(), state.Get())); transition(output.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); }
    auto* h = heap.Get(); list->SetDescriptorHeaps(1, &h);
    list->SetComputeRootSignature(signature.Get()); list->SetPipelineState(state.Get());
    auto gpu = heap->GetGPUDescriptorHandleForHeapStart();
    for (unsigned i = 0; i < 3; ++i) { list->SetComputeRootDescriptorTable(i + 1, gpu); gpu.ptr += stride; }
    struct Constants { uint32_t size[2]; float screen[2]; uint32_t rect[4]; float offset[2], scale[2]; } constants{{4,4},{1,1},{1,1,3,3},{1,1},{1,1}};
    if (test == 1) { constants.screen[0] = constants.screen[1] = 2; constants.rect[0] = constants.rect[1] = 2; constants.rect[2] = constants.rect[3] = 6; constants.offset[0] = constants.offset[1] = 2; }
    if (test == 2) { constants.rect[2] = constants.rect[3] = 1; }
    if (test == 3) { constants.rect[0] = constants.rect[1] = 0; constants.rect[2] = constants.rect[3] = 4; constants.offset[0] = constants.offset[1] = 0; }
    list->SetComputeRoot32BitConstants(0, 12, &constants, 0); list->Dispatch(1, 1, 1);
    transition(output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION from{}, to{}; from.pResource = output.Get();
    from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; to.pResource = readback.Get();
    to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    to.PlacedFootprint.Footprint = {DXGI_FORMAT_R16G16B16A16_FLOAT, 4, 4, 1, 256};
    list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr); Check(list->Close());
    ID3D12CommandList* lists[]{list.Get()}; queue->ExecuteCommandLists(1, lists); Check(queue->Signal(fence.Get(), test + 1));
    Check(fence->SetEventOnCompletion(test + 1, event.value));
    if (WaitForSingleObject(event.value, 10000) != WAIT_OBJECT_0) throw std::runtime_error("GPU timeout");
    Check(readback->Map(0, nullptr, reinterpret_cast<void**>(&data)));
    for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) {
      const auto* p = reinterpret_cast<const uint16_t*>(data + 256 * y + 8 * x);
      const bool blended = test == 3 || (test != 2 && x >= 1 && x < 3 && y >= 1 && y < 3);
      float expected[4]{0,0,1,1};
      if (blended) {
        const unsigned sx = test == 3 ? x : test == 1 ? 2*x-1 : x-1;
        const unsigned sy = test == 3 ? y : test == 1 ? 2*y-1 : y-1;
        const uint32_t argb = pixels[sy*4+sx];
        const float alpha = (argb >> 24) / 255.0f;
        expected[0] = ((argb >> 16) & 255) / 255.0f * alpha;
        expected[1] = ((argb >> 8) & 255) / 255.0f * alpha;
        expected[2] = (argb & 255) / 255.0f * alpha + 1-alpha;
      }
      for (unsigned c = 0; c < 4; ++c) {
        const unsigned exponent = (p[c] >> 10) & 31, fraction = p[c] & 1023;
        const float value = exponent ? std::ldexp(1.0f + fraction/1024.0f, int(exponent)-15) : std::ldexp(float(fraction), -24);
        if ((p[c] & 0x8000) || std::abs(value - expected[c]) > 0.001f)
          throw std::runtime_error("Unexpected composed pixel: test=" + std::to_string(test) + " x=" + std::to_string(x) + " y=" + std::to_string(y) + " channel=" + std::to_string(c));
      }
    }
    readback->Unmap(0, nullptr);
  }
  std::cout << "PASS: 64 GPU pixels; BGRA surface decoding, transparent white, primary colors, partial alpha, title preservation, display scaling and empty rectangle\n";
  return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
