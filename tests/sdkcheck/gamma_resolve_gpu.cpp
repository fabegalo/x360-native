// Executes the shipped full resolve compute kernels, not copies of their math.
#include "gamma_resolve_cases.h"
#include <chrono>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <map>
#include <rex/ui/vulkan/device.h>
#include <rex/ui/vulkan/instance.h>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <windows.h>
#include <wrl/client.h>
namespace dxbc {
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_full_128bpp_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_full_128bpp_scaled_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_full_16bpp_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_full_16bpp_scaled_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_full_32bpp_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_full_32bpp_scaled_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_full_64bpp_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_full_64bpp_scaled_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_full_8bpp_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_full_8bpp_scaled_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_gamma_128bpp_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_fixed16_128bpp_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_gamma_128bpp_scaled_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_fixed16_128bpp_scaled_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_gamma_16bpp_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_fixed16_16bpp_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_gamma_16bpp_scaled_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_fixed16_16bpp_scaled_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_gamma_32bpp_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_fixed16_32bpp_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_gamma_32bpp_scaled_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_fixed16_32bpp_scaled_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_gamma_64bpp_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_fixed16_64bpp_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_gamma_64bpp_scaled_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_fixed16_64bpp_scaled_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_gamma_8bpp_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_fixed16_8bpp_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_gamma_8bpp_scaled_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/resolve_fixed16_8bpp_scaled_cs.h"
} // namespace dxbc
namespace spirv {
#include "src/graphics/shaders/vulkan_spirv/resolve_full_128bpp_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_full_128bpp_scaled_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_full_16bpp_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_full_16bpp_scaled_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_full_32bpp_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_full_32bpp_scaled_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_full_64bpp_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_full_64bpp_scaled_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_full_8bpp_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_full_8bpp_scaled_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_gamma_128bpp_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_fixed16_128bpp_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_gamma_128bpp_scaled_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_fixed16_128bpp_scaled_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_gamma_16bpp_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_fixed16_16bpp_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_gamma_16bpp_scaled_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_fixed16_16bpp_scaled_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_gamma_32bpp_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_fixed16_32bpp_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_gamma_32bpp_scaled_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_fixed16_32bpp_scaled_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_gamma_64bpp_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_fixed16_64bpp_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_gamma_64bpp_scaled_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_fixed16_64bpp_scaled_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_gamma_8bpp_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_fixed16_8bpp_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_gamma_8bpp_scaled_cs.h"
#include "src/graphics/shaders/vulkan_spirv/resolve_fixed16_8bpp_scaled_cs.h"
} // namespace spirv
using namespace gamma_probe;
using Microsoft::WRL::ComPtr;
void Need(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
void HR(HRESULT value, const char *message) { Need(SUCCEEDED(value), message); }
std::filesystem::path shader_directory, baseline_directory;
bool bench_gamma = true;
bool bench_fixed16 = false, bench_source64 = false;
bool fixed16_only = false;
bool extended_fixed16 = false;
bool rgb565_only = false;
bool legacy_only = false;
bool extended_rgb565 = false;
bool bench_rgb565 = false;
bool reference_check = false;
std::vector<Case> TestCases() {
  auto cases = Cases();
  if (extended_fixed16) {
    auto extra = Fixed16ExtendedCases();
    cases.insert(cases.end(), extra.begin(), extra.end());
  }
  if (extended_rgb565) {
    auto extra = Rgb565ExtendedCases();
    cases.insert(cases.end(), extra.begin(), extra.end());
  }
  if (fixed16_only)
    std::erase_if(cases, [](const Case& c) { return !c.fixed16; });
  if (rgb565_only)
    std::erase_if(cases, [](const Case& c) {
      return c.gamma || c.fixed16 || c.format != xenos::ColorFormat::k_5_6_5;
    });
  if (legacy_only)
    std::erase_if(cases, [](const Case& c) {
      return c.gamma || c.fixed16 || c.format != xenos::ColorFormat::k_8;
    });
  return cases;
}
unsigned bench_msaa = 2;
std::vector<uint32_t> Code(const Case &c, bool vk,
                           const std::filesystem::path &directory) {
  const unsigned char *data = nullptr;
  size_t size = 0;
#define PICK(NS, FAMILY, BPP, SCALED)                                          \
  case BPP:                                                                    \
    data = reinterpret_cast<const unsigned char *>(                            \
        NS::resolve_##FAMILY##_##BPP##bpp##SCALED##_cs);                       \
    size = sizeof(NS::resolve_##FAMILY##_##BPP##bpp##SCALED##_cs);             \
    break
#define TABLE(NS, FAMILY, SCALED)                                              \
  switch (8u << c.log) {                                                       \
    PICK(NS, FAMILY, 8, SCALED);                                               \
    PICK(NS, FAMILY, 16, SCALED);                                              \
    PICK(NS, FAMILY, 32, SCALED);                                              \
    PICK(NS, FAMILY, 64, SCALED);                                              \
    PICK(NS, FAMILY, 128, SCALED);                                             \
  }
#define FAMILY(NS, SCALED)                                                     \
  if (c.gamma) {                                                               \
    TABLE(NS, gamma, SCALED)                                                   \
  } else if (c.fixed16) {                                                      \
    TABLE(NS, fixed16, SCALED)                                                 \
  } else {                                                                     \
    TABLE(NS, full, SCALED)                                                    \
  }
  if (vk) {
    if (c.scale > 1) {
      FAMILY(spirv, _scaled)
    } else {
      FAMILY(spirv, )
    }
  } else {
    if (c.scale > 1) {
      FAMILY(dxbc, _scaled)
    } else {
      FAMILY(dxbc, )
    }
  }
#undef FAMILY
#undef TABLE
#undef PICK
  if (!directory.empty()) {
    auto tail = std::to_string(8u << c.log) + "bpp" +
                (c.scale > 1 ? "_scaled" : "") + "_cs" +
                (vk ? ".spv" : ".dxbc");
    auto path =
        directory / ((c.gamma ? "resolve_gamma_" : "resolve_full_") + tail);
    if (c.fixed16 && std::filesystem::exists(directory / ("resolve_fixed16_" + tail)))
      path = directory / ("resolve_fixed16_" + tail);
    // Explicit old-resource directories are used to demonstrate the red test.
    if (c.gamma && !std::filesystem::exists(path))
      path = directory / ("resolve_full_" + tail);
    if (c.gamma || std::filesystem::exists(path)) {
      std::ifstream input(path, std::ios::binary | std::ios::ate);
      Need(bool(input), "shader file");
      size = size_t(input.tellg());
      Need(size % 4 == 0, "shader alignment");
      std::vector<uint32_t> words(size / 4);
      input.seekg(0);
      input.read(reinterpret_cast<char *>(words.data()), size);
      Need(bool(input), "shader read");
      return words;
    }
  }
  Need(size && size % 4 == 0, "embedded shader");
  std::vector<uint32_t> words(size / 4);
  std::memcpy(words.data(), data, size);
  return words;
}
struct Result {
  std::vector<unsigned char> data;
  double gpu_ns = 0, prepare_ns = 0, submit_ns = 0, upload_ns = 0;
};
using Clock = std::chrono::steady_clock;
double Ns(Clock::time_point a, Clock::time_point b) {
  return std::chrono::duration<double, std::nano>(b - a).count();
}
class Dx {
  std::filesystem::path resources_;
  ComPtr<ID3D12Device> device_;
  ComPtr<ID3D12CommandQueue> queue_;
  ComPtr<ID3D12CommandAllocator> allocator_;
  ComPtr<ID3D12GraphicsCommandList> cmd_;
  ComPtr<ID3D12Resource> input_, staging_, output_, readback_;
  void *upload_ = nullptr;
  ComPtr<ID3D12DescriptorHeap> heap_;
  ComPtr<ID3D12RootSignature> roots_[2];
  ComPtr<ID3D12PipelineState> pipelines_[2][15];
  ComPtr<ID3D12Fence> fence_;
  ComPtr<ID3D12QueryHeap> timestamps_;
  UINT64 frequency_ = 0, serial_ = 0;
  HANDLE event_ = nullptr;
  unsigned stride_ = 0;
  D3D12_CPU_DESCRIPTOR_HANDLE CPU(unsigned index) const {
    auto h = heap_->GetCPUDescriptorHandleForHeapStart();
    h.ptr += index * stride_;
    return h;
  }
  D3D12_GPU_DESCRIPTOR_HANDLE GPU(unsigned index) const {
    auto h = heap_->GetGPUDescriptorHandleForHeapStart();
    h.ptr += index * stride_;
    return h;
  }
  ComPtr<ID3D12Resource>
  Buffer(unsigned bytes, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state,
         D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = bytes;
    d.Height = d.DepthOrArraySize = d.MipLevels = d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    d.Flags = flags;
    D3D12_HEAP_PROPERTIES h{};
    h.Type = type;
    ComPtr<ID3D12Resource> r;
    HR(device_->CreateCommittedResource(&h, D3D12_HEAP_FLAG_NONE, &d, state,
                                        nullptr, IID_PPV_ARGS(&r)),
       "D3D buffer");
    return r;
  }

public:
  Dx(std::filesystem::path resources = shader_directory)
      : resources_(std::move(resources)) {
    HR(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                         IID_PPV_ARGS(&device_)),
       "D3D device");
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter1> adapter;
    HR(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "DXGI factory");
    HR(factory->EnumAdapterByLuid(device_->GetAdapterLuid(), IID_PPV_ARGS(&adapter)),
       "D3D adapter identity");
    DXGI_ADAPTER_DESC1 adapter_desc{};
    HR(adapter->GetDesc1(&adapter_desc), "D3D adapter description");
    Need(!(adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE), "hardware D3D GPU required");
    std::wstring adapter_name(adapter_desc.Description);
    std::cout << "D3D12 GPU=" << std::string(adapter_name.begin(), adapter_name.end()) << '\n';
    D3D12_COMMAND_QUEUE_DESC q{};
    HR(device_->CreateCommandQueue(&q, IID_PPV_ARGS(&queue_)), "D3D queue");
    HR(queue_->GetTimestampFrequency(&frequency_), "D3D timestamps");
    HR(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       IID_PPV_ARGS(&allocator_)),
       "D3D allocator");
    HR(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                  allocator_.Get(), nullptr,
                                  IID_PPV_ARGS(&cmd_)),
       "D3D list");
    HR(cmd_->Close(), "D3D initial close");
    input_ = Buffer(kEdramBytes, D3D12_HEAP_TYPE_DEFAULT,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    staging_ = Buffer(kEdramBytes, D3D12_HEAP_TYPE_UPLOAD,
                      D3D12_RESOURCE_STATE_GENERIC_READ);
    D3D12_RANGE no_read{0, 0};
    HR(staging_->Map(0, &no_read, &upload_), "D3D input map");
    output_ = Buffer(kDestBytes, D3D12_HEAP_TYPE_DEFAULT,
                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                     D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    readback_ = Buffer(kDestBytes + 16, D3D12_HEAP_TYPE_READBACK,
                       D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.NumDescriptors = 3;
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HR(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap_)), "D3D views");
    stride_ = device_->GetDescriptorHandleIncrementSize(hd.Type);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Format = DXGI_FORMAT_R32_TYPELESS;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Buffer.NumElements = kEdramBytes / 4;
    srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
    device_->CreateShaderResourceView(input_.Get(), &srv, CPU(0));
    for (unsigned n = 1; n <= 2; ++n) {
      D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
      u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
      u.Format =
          n == 1 ? DXGI_FORMAT_R32G32_UINT : DXGI_FORMAT_R32G32B32A32_UINT;
      u.Buffer.NumElements = kDestBytes / (n == 1 ? 8 : 16);
      device_->CreateUnorderedAccessView(output_.Get(), nullptr, &u, CPU(n));
    }
    for (unsigned scaled = 0; scaled < 2; ++scaled) {
      D3D12_DESCRIPTOR_RANGE ranges[2]{};
      ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
      ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0};
      D3D12_ROOT_PARAMETER rp[3]{};
      rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
      rp[0].Constants = {0, 0, scaled ? 4u : 5u};
      for (unsigned i = 1; i < 3; ++i) {
        rp[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[i].DescriptorTable = {1, &ranges[i - 1]};
      }
      D3D12_ROOT_SIGNATURE_DESC rs{};
      rs.NumParameters = 3;
      rs.pParameters = rp;
      ComPtr<ID3DBlob> blob, error;
      HR(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob,
                                     &error),
         "D3D root serialize");
      HR(device_->CreateRootSignature(0, blob->GetBufferPointer(),
                                      blob->GetBufferSize(),
                                      IID_PPV_ARGS(&roots_[scaled])),
         "D3D root");
    }
    HR(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)),
       "D3D fence");
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Need(event_ != nullptr, "D3D event");
    D3D12_QUERY_HEAP_DESC query{};
    query.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    query.Count = 2;
    HR(device_->CreateQueryHeap(&query, IID_PPV_ARGS(&timestamps_)),
       "D3D timestamp heap");
  }
  ~Dx() {
    if (staging_)
      staging_->Unmap(0, nullptr);
    if (event_)
      CloseHandle(event_);
  }
  Result Run(const Case &c, bool refill = true, unsigned repeats = 1) {
    auto upload_begin = Clock::now();
    if (refill)
      Fill(c, upload_);
    auto upload_end = Clock::now();
    auto constants = c.Constants();
    unsigned scaled = c.scale > 1;
    auto &pso = pipelines_[scaled][c.log + (c.gamma ? 5 : c.fixed16 ? 10 : 0)];
    if (!pso) {
      auto code = Code(c, false, resources_);
      D3D12_COMPUTE_PIPELINE_STATE_DESC d{};
      d.pRootSignature = roots_[scaled].Get();
      d.CS = {code.data(), code.size() * 4};
      HR(device_->CreateComputePipelineState(&d, IID_PPV_ARGS(&pso)),
         "D3D resolve PSO");
    }
    auto start = Clock::now();
    HR(allocator_->Reset(), "D3D reset allocator");
    HR(cmd_->Reset(allocator_.Get(), pso.Get()), "D3D reset list");
    if (refill) {
      D3D12_RESOURCE_BARRIER upload{};
      upload.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      upload.Transition = {input_.Get(), 0,
                           D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                           D3D12_RESOURCE_STATE_COPY_DEST};
      cmd_->ResourceBarrier(1, &upload);
      cmd_->CopyBufferRegion(input_.Get(), 0, staging_.Get(), 0, kEdramBytes);
      std::swap(upload.Transition.StateBefore, upload.Transition.StateAfter);
      cmd_->ResourceBarrier(1, &upload);
    }
    ID3D12DescriptorHeap *heaps[] = {heap_.Get()};
    cmd_->SetDescriptorHeaps(1, heaps);
    const UINT clear[4] = {0xcdcdcdcd, 0xcdcdcdcd, 0xcdcdcdcd, 0xcdcdcdcd};
    cmd_->ClearUnorderedAccessViewUint(GPU(2), CPU(2), output_.Get(), clear, 0,
                                       nullptr);
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    b.UAV.pResource = output_.Get();
    cmd_->ResourceBarrier(1, &b);
    cmd_->SetComputeRootSignature(roots_[scaled].Get());
    cmd_->SetComputeRoot32BitConstants(0, scaled ? 4 : 5, &constants, 0);
    cmd_->SetComputeRootDescriptorTable(1, GPU(0));
    cmd_->SetComputeRootDescriptorTable(2, GPU(c.log < 2 ? 1 : 2));
    auto groups = c.Groups();
    cmd_->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
    for (unsigned n = 0; n < repeats; ++n) {
      cmd_->Dispatch(groups[0], groups[1], 1);
      cmd_->ResourceBarrier(1, &b);
    }
    cmd_->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
    cmd_->ResolveQueryData(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2,
                           readback_.Get(), kDestBytes);
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {output_.Get(), 0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_COPY_SOURCE};
    cmd_->ResourceBarrier(1, &b);
    cmd_->CopyBufferRegion(readback_.Get(), 0, output_.Get(), 0, kDestBytes);
    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    cmd_->ResourceBarrier(1, &b);
    HR(cmd_->Close(), "D3D close");
    auto prepared = Clock::now();
    ID3D12CommandList *lists[] = {cmd_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    HR(queue_->Signal(fence_.Get(), ++serial_), "D3D signal");
    auto submitted = Clock::now();
    HR(fence_->SetEventOnCompletion(serial_, event_), "D3D event fence");
    Need(WaitForSingleObject(event_, 5000) == WAIT_OBJECT_0,
         "D3D bounded wait");
    HR(device_->GetDeviceRemovedReason(), "D3D device status");
    void *bytes;
    D3D12_RANGE r{0, kDestBytes + 16};
    HR(readback_->Map(0, &r, &bytes), "D3D readback");
    Result result;
    result.data.resize(kDestBytes);
    std::memcpy(result.data.data(), bytes, kDestBytes);
    UINT64 ticks[2];
    std::memcpy(ticks, static_cast<unsigned char *>(bytes) + kDestBytes, 16);
    readback_->Unmap(0, nullptr);
    result.gpu_ns =
        double(ticks[1] - ticks[0]) * 1e9 / double(frequency_) / repeats;
    result.upload_ns = Ns(upload_begin, upload_end);
    result.prepare_ns = Ns(start, prepared);
    result.submit_ns = Ns(prepared, submitted);
    return result;
  }
};
class Vk {
  std::filesystem::path resources_;
  using Device = rex::ui::vulkan::VulkanDevice;
  std::unique_ptr<rex::ui::vulkan::VulkanInstance> instance_;
  std::unique_ptr<Device> device_;
  struct Buffer {
    VkBuffer handle = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void *mapped = nullptr;
  };
  Buffer input_, staging_, output_, readback_;
  VkCommandPool pool_ = VK_NULL_HANDLE;
  VkCommandBuffer cmd_;
  VkDescriptorSetLayout descriptor_layouts_[2]{};
  VkDescriptorPool descriptors_ = VK_NULL_HANDLE;
  VkDescriptorSet sets_[2]{};
  VkPipelineLayout layouts_[2]{};
  VkPipeline pipelines_[2][15]{};
  VkFence fence_ = VK_NULL_HANDLE;
  VkQueryPool queries_ = VK_NULL_HANDLE;
  PFN_vkCmdFillBuffer fill_buffer_ = nullptr;
  PFN_vkCmdWriteTimestamp write_timestamp_ = nullptr;
  double period_ = 0;
  VkPhysicalDeviceMemoryProperties memory_{};
  Buffer Make(unsigned size, VkBufferUsageFlags usage, bool host) {
    const auto &f = device_->functions();
    auto vk = device_->device();
    Buffer b;
    VkBufferCreateInfo i{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    i.size = size;
    i.usage = usage;
    Need(f.vkCreateBuffer(vk, &i, nullptr, &b.handle) == VK_SUCCESS,
         "Vulkan buffer");
    VkMemoryRequirements req;
    f.vkGetBufferMemoryRequirements(vk, b.handle, &req);
    unsigned t = 0;
    VkMemoryPropertyFlags flags = host
                                      ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                      : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    for (; t < memory_.memoryTypeCount; ++t)
      if ((req.memoryTypeBits & (1u << t)) &&
          (memory_.memoryTypes[t].propertyFlags & flags) == flags)
        break;
    Need(t < memory_.memoryTypeCount, "Vulkan memory type");
    VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    a.allocationSize = req.size;
    a.memoryTypeIndex = t;
    Need(f.vkAllocateMemory(vk, &a, nullptr, &b.memory) == VK_SUCCESS,
         "Vulkan allocation");
    Need(f.vkBindBufferMemory(vk, b.handle, b.memory, 0) == VK_SUCCESS,
         "Vulkan bind");
    if (host)
      Need(f.vkMapMemory(vk, b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped) ==
               VK_SUCCESS,
           "Vulkan map");
    return b;
  }

public:
  Vk(std::filesystem::path resources = shader_directory)
      : resources_(std::move(resources)) {
    instance_ = rex::ui::vulkan::VulkanInstance::Create(false, true);
    Need(bool(instance_), "Vulkan instance");
    std::vector<VkPhysicalDevice> physical;
    instance_->EnumeratePhysicalDevices(physical);
    Need(!physical.empty(), "Vulkan GPU");
    device_ =
        Device::CreateIfSupported(instance_.get(), physical[0], true, false);
    Need(bool(device_), "Vulkan Xenos device");
    const auto &f = device_->functions();
    auto vk = device_->device();
    const auto &inf = instance_->functions();
    inf.vkGetPhysicalDeviceMemoryProperties(physical[0], &memory_);
    VkPhysicalDeviceProperties prop;
    inf.vkGetPhysicalDeviceProperties(physical[0], &prop);
    period_ = prop.limits.timestampPeriod;
    write_timestamp_ = reinterpret_cast<PFN_vkCmdWriteTimestamp>(
        inf.vkGetDeviceProcAddr(vk, "vkCmdWriteTimestamp"));
    fill_buffer_ = reinterpret_cast<PFN_vkCmdFillBuffer>(
        inf.vkGetDeviceProcAddr(vk, "vkCmdFillBuffer"));
    Need(fill_buffer_ && write_timestamp_ && period_ > 0, "Vulkan timestamps");
    std::cout << "Vulkan GPU=" << prop.deviceName
              << " timestampPeriod=" << period_ << '\n';
    unsigned layer_count = 0;
    inf.vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    std::vector<VkLayerProperties> layers(layer_count);
    inf.vkEnumerateInstanceLayerProperties(&layer_count, layers.data());
    bool validation = false;
    for (auto &layer : layers)
      validation |= !strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation");
    std::cout << "Khronos validation available=" << validation << '\n';
    input_ = Make(kEdramBytes,
                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                  false);
    staging_ = Make(kEdramBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    output_ = Make(kDestBytes,
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   false);
    readback_ = Make(kDestBytes + 16, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
    VkDescriptorSetLayoutBinding binding{};
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo di{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    di.bindingCount = 1;
    di.pBindings = &binding;
    for (auto &layout : descriptor_layouts_)
      Need(f.vkCreateDescriptorSetLayout(vk, &di, nullptr, &layout) ==
               VK_SUCCESS,
           "Vulkan set layout");
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2};
    VkDescriptorPoolCreateInfo dpi{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = 2;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    Need(f.vkCreateDescriptorPool(vk, &dpi, nullptr, &descriptors_) ==
             VK_SUCCESS,
         "Vulkan descriptors");
    VkDescriptorSetAllocateInfo ai{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = descriptors_;
    ai.descriptorSetCount = 2;
    ai.pSetLayouts = descriptor_layouts_;
    Need(f.vkAllocateDescriptorSets(vk, &ai, sets_) == VK_SUCCESS,
         "Vulkan sets");
    VkDescriptorBufferInfo buffers[] = {{input_.handle, 0, kEdramBytes},
                                        {output_.handle, 0, kDestBytes}};
    VkWriteDescriptorSet writes[2]{};
    for (unsigned i = 0; i < 2; ++i) {
      writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[i].dstSet = sets_[i];
      writes[i].descriptorCount = 1;
      writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      writes[i].pBufferInfo = &buffers[i];
    }
    f.vkUpdateDescriptorSets(vk, 2, writes, 0, nullptr);
    for (unsigned scaled = 0; scaled < 2; ++scaled) {
      VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0,
                               scaled ? 16u : 20u};
      VkPipelineLayoutCreateInfo pi{
          VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
      pi.setLayoutCount = 2;
      pi.pSetLayouts = descriptor_layouts_;
      pi.pushConstantRangeCount = 1;
      pi.pPushConstantRanges = &push;
      Need(f.vkCreatePipelineLayout(vk, &pi, nullptr, &layouts_[scaled]) ==
               VK_SUCCESS,
           "Vulkan pipeline layout");
    }
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = device_->queue_family_graphics_compute();
    Need(f.vkCreateCommandPool(vk, &pci, nullptr, &pool_) == VK_SUCCESS,
         "Vulkan pool");
    VkCommandBufferAllocateInfo ca{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = pool_;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    Need(f.vkAllocateCommandBuffers(vk, &ca, &cmd_) == VK_SUCCESS,
         "Vulkan commands");
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    Need(f.vkCreateFence(vk, &fi, nullptr, &fence_) == VK_SUCCESS,
         "Vulkan fence");
    VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qi.queryCount = 2;
    Need(f.vkCreateQueryPool(vk, &qi, nullptr, &queries_) == VK_SUCCESS,
         "Vulkan queries");
  }
  ~Vk() {
    if (!device_)
      return;
    const auto &f = device_->functions();
    auto vk = device_->device();
    if (pool_)
      f.vkDestroyCommandPool(vk, pool_, nullptr);
    if (fence_)
      f.vkDestroyFence(vk, fence_, nullptr);
    if (queries_)
      f.vkDestroyQueryPool(vk, queries_, nullptr);
    for (auto &a : pipelines_)
      for (auto pipeline : a)
        if (pipeline)
          f.vkDestroyPipeline(vk, pipeline, nullptr);
    for (auto layout : layouts_)
      if (layout)
        f.vkDestroyPipelineLayout(vk, layout, nullptr);
    if (descriptors_)
      f.vkDestroyDescriptorPool(vk, descriptors_, nullptr);
    for (auto layout : descriptor_layouts_)
      if (layout)
        f.vkDestroyDescriptorSetLayout(vk, layout, nullptr);
    for (auto b : {input_, staging_, output_, readback_}) {
      if (b.mapped)
        f.vkUnmapMemory(vk, b.memory);
      if (b.handle)
        f.vkDestroyBuffer(vk, b.handle, nullptr);
      if (b.memory)
        f.vkFreeMemory(vk, b.memory, nullptr);
    }
  }
  Result Run(const Case &c, bool refill = true, unsigned repeats = 1) {
    auto upload_begin = Clock::now();
    if (refill)
      Fill(c, staging_.mapped);
    auto upload_end = Clock::now();
    auto constants = c.Constants();
    unsigned scaled = c.scale > 1;
    const auto &f = device_->functions();
    auto vk = device_->device();
    auto &pipeline = pipelines_[scaled][c.log + (c.gamma ? 5 : c.fixed16 ? 10 : 0)];
    if (!pipeline) {
      auto code = Code(c, true, resources_);
      VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
      mi.codeSize = code.size() * 4;
      mi.pCode = code.data();
      VkShaderModule module;
      Need(f.vkCreateShaderModule(vk, &mi, nullptr, &module) == VK_SUCCESS,
           "Vulkan module");
      VkComputePipelineCreateInfo pi{
          VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
      pi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      pi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
      pi.stage.module = module;
      pi.stage.pName = "main";
      pi.layout = layouts_[scaled];
      auto status = f.vkCreateComputePipelines(vk, VK_NULL_HANDLE, 1, &pi,
                                               nullptr, &pipeline);
      f.vkDestroyShaderModule(vk, module, nullptr);
      Need(status == VK_SUCCESS, "Vulkan resolve pipeline");
    }
    auto start = Clock::now();
    Need(f.vkResetCommandPool(vk, pool_, 0) == VK_SUCCESS, "Vulkan reset pool");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Need(f.vkBeginCommandBuffer(cmd_, &begin) == VK_SUCCESS, "Vulkan begin");
    if (refill) {
      VkBufferMemoryBarrier u{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
      u.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
      u.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      u.srcQueueFamilyIndex = u.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      u.buffer = input_.handle;
      u.size = kEdramBytes;
      f.vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1,
                             &u, 0, nullptr);
      VkBufferCopy copy{0, 0, kEdramBytes};
      f.vkCmdCopyBuffer(cmd_, staging_.handle, input_.handle, 1, &copy);
      u.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      u.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      f.vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                             nullptr, 1, &u, 0, nullptr);
    }
    f.vkCmdResetQueryPool(cmd_, queries_, 0, 2);
    fill_buffer_(cmd_, output_.handle, 0, kDestBytes, 0xcdcdcdcd);
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = output_.handle;
    barrier.size = kDestBytes;
    f.vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                           1, &barrier, 0, nullptr);
    f.vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    f.vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE,
                              layouts_[scaled], 0, 2, sets_, 0, nullptr);
    f.vkCmdPushConstants(cmd_, layouts_[scaled], VK_SHADER_STAGE_COMPUTE_BIT, 0,
                         scaled ? 16 : 20, &constants);
    auto groups = c.Groups();
    write_timestamp_(cmd_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queries_, 0);
    for (unsigned n = 0; n < repeats; ++n) {
      f.vkCmdDispatch(cmd_, groups[0], groups[1], 1);
      if (n + 1 < repeats) {
        auto ww = barrier;
        ww.srcAccessMask = ww.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        f.vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                               nullptr, 1, &ww, 0, nullptr);
      }
    }
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    f.vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1,
                           &barrier, 0, nullptr);
    write_timestamp_(cmd_, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queries_, 1);
    VkBufferCopy copy{0, 0, kDestBytes};
    f.vkCmdCopyBuffer(cmd_, output_.handle, readback_.handle, 1, &copy);
    f.vkCmdCopyQueryPoolResults(
        cmd_, queries_, 0, 2, readback_.handle, kDestBytes, 8,
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    barrier.buffer = readback_.handle;
    barrier.size = kDestBytes + 16;
    f.vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1,
                           &barrier, 0, nullptr);
    Need(f.vkEndCommandBuffer(cmd_) == VK_SUCCESS, "Vulkan end");
    Need(f.vkResetFences(vk, 1, &fence_) == VK_SUCCESS, "Vulkan reset fence");
    auto prepared = Clock::now();
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd_;
    {
      auto queue =
          device_->AcquireQueue(device_->queue_family_graphics_compute(), 0);
      Need(f.vkQueueSubmit(queue.queue(), 1, &submit, fence_) == VK_SUCCESS,
           "Vulkan submit");
    }
    auto submitted = Clock::now();
    Need(f.vkWaitForFences(vk, 1, &fence_, VK_TRUE, 5'000'000'000ull) ==
             VK_SUCCESS,
         "Vulkan bounded wait");
    Result result;
    result.data.resize(kDestBytes);
    std::memcpy(result.data.data(), readback_.mapped, kDestBytes);
    uint64_t ticks[2];
    std::memcpy(
        ticks, static_cast<unsigned char *>(readback_.mapped) + kDestBytes, 16);
    result.gpu_ns = double(ticks[1] - ticks[0]) * period_ / repeats;
    result.upload_ns = Ns(upload_begin, upload_end);
    result.prepare_ns = Ns(start, prepared);
    result.submit_ns = Ns(prepared, submitted);
    return result;
  }
};
unsigned Check(const Case &c, const std::vector<unsigned char> &got,
               bool dx = false) {
  auto expected = Expected(c, dx);
  unsigned bad = 0;
  for (unsigned i = 0; i < kDestBytes; ++i)
    if (got[i] != expected[i]) {
      if (bad == 0) {
        std::cerr << (dx ? "D3D12 " : "Vulkan ") << c.Name()
                  << " byte=" << i << " got=" << unsigned(got[i])
                  << " expected=" << unsigned(expected[i]) << '\n';
        for (unsigned y = 0; y < c.height * c.scale; ++y)
          for (unsigned x = 0; x < c.width * c.scale; ++x) {
            auto address = DestinationAddress(c, x, y);
            if (i >= address && i < address + (1u << c.log)) {
              auto values = Values(c, x, y);
              std::cerr << " pixel=" << x << ',' << y
                        << " offset=" << i - address;
              for (float v : values)
                std::cerr << " value=" << v << " bits=" << std::hex
                          << std::bit_cast<unsigned>(v) << std::dec;
              if (c.fixed16) {
                unsigned p = (y / c.scale) * c.width + x / c.scale;
                for (unsigned s = 0; s < (1u << c.msaa); ++s) {
                  std::cerr << " sample" << s << "=[";
                  for (unsigned ch = 0; ch < (c.source64 ? 4u : 2u); ++ch)
                    std::cerr << (ch ? "," : "") << FixedCode(c, p, s, ch);
                  std::cerr << "]";
                }
                if (c.log == 4) {
                  float actual[4];
                  std::memcpy(actual, got.data() + address, sizeof(actual));
                  for (float v : actual) std::cerr << " actual=" << v;
                }
              }
              std::cerr << '\n';
            }
          }
      }
      ++bad;
    }
  return bad;
}
template <class Backend> void Validate(const char *name) {
  Backend backend;
  std::unique_ptr<Backend> original;
  unsigned passed = 0, gamma = 0, fixed = 0, controls = 0, legacy_differences = 0;
  for (const auto &c : TestCases()) {
    auto result = backend.Run(c);
    if (c.gamma || c.fixed16) {
      Need(Check(c, result.data, std::is_same_v<Backend, Dx>) == 0,
           "gamma independent oracle");
      if (c.fixed16) ++fixed; else ++gamma;
    } else {
      Need(!baseline_directory.empty(),
           "non-gamma controls require --baseline-dir with original SDK "
           "binaries");
      if (!original)
        original = std::make_unique<Backend>(baseline_directory);
      auto before = original->Run(c);
      Need(result.data == before.data, "non-gamma baseline regression");
      if (rgb565_only || legacy_only || c.format == xenos::ColorFormat::k_5_6_5)
        Need(Check(c, result.data, std::is_same_v<Backend, Dx>) == 0,
             "focused independent oracle");
      ++controls;
      if (result.data != Expected(c, std::is_same_v<Backend, Dx>))
        ++legacy_differences;
    }
    ++passed;
  }
  std::cout << name << " compute resolve: " << passed << " cases, " << gamma
            << " gamma exact to independent oracle, " << controls
            << " non-gamma exact to original GPU baseline ("
            << legacy_differences << " pre-existing oracle differences)\n";
  std::cout << fixed << " fixed16 cases exact to independent oracle\n";
}
void ValidateBoth() {
  Dx dx;
  Vk vk;
  std::unique_ptr<Dx> original_dx;
  std::unique_ptr<Vk> original_vk;
  unsigned passed = 0, gamma = 0, controls = 0, identical = 0, half = 0,
           half_different = 0, legacy_dx = 0, legacy_vk = 0;
  std::map<std::string, unsigned> legacy_groups;
  unsigned fixed = 0, fixed_bad_dx = 0, fixed_bad_vk = 0,
           fixed_identical = 0, fixed_half = 0, fixed_half_different = 0;
  unsigned rgb_bad_dx = 0, rgb_bad_vk = 0;
  for (const auto &c : TestCases()) {
    auto a = dx.Run(c), b = vk.Run(c);
    if (c.fixed16) {
      fixed_bad_dx += Check(c, a.data, true) != 0;
      fixed_bad_vk += Check(c, b.data, false) != 0;
      if (c.log != 3) {
        Need(a.data == b.data, "D3D12/Vulkan fixed16 byte equivalence");
        ++fixed_identical;
      } else {
        ++fixed_half;
        fixed_half_different += a.data != b.data;
      }
      ++fixed;
    } else if (c.gamma) {
      Need(Check(c, a.data, true) == 0, "D3D12 independent gamma oracle");
      Need(Check(c, b.data, false) == 0, "Vulkan independent gamma oracle");
      if (c.log == 3) {
        ++half;
        if (a.data != b.data)
          ++half_different;
      } else {
        Need(a.data == b.data, "D3D12/Vulkan gamma byte equivalence");
        ++identical;
      }
      ++gamma;
    } else {
      Need(!baseline_directory.empty(),
           "non-gamma controls require --baseline-dir with original SDK "
           "binaries");
      if (!original_dx) {
        original_dx = std::make_unique<Dx>(baseline_directory);
        original_vk = std::make_unique<Vk>(baseline_directory);
      }
      auto old_dx = original_dx->Run(c), old_vk = original_vk->Run(c);
      Need(a.data == old_dx.data && b.data == old_vk.data,
           "non-gamma baseline regression");
      const bool legacy_difference = a.data != Expected(c, true);
      if (rgb565_only || legacy_only) {
        Check(c, a.data, true);
        Check(c, b.data, false);
      }
      legacy_dx += legacy_difference;
      if (legacy_difference) {
        auto key = std::string(c.format == xenos::ColorFormat::k_8 ? "R8" :
                              c.format == xenos::ColorFormat::k_5_6_5 ? "RGB565" : "other") +
            " format=" + std::to_string(unsigned(c.format)) +
            " swap=" + std::to_string(c.swap) + " source64=" + std::to_string(c.source64);
        ++legacy_groups[key];
      }
      legacy_vk += b.data != Expected(c, false);
      if (c.format == xenos::ColorFormat::k_5_6_5) {
        rgb_bad_dx += legacy_difference;
        rgb_bad_vk += b.data != Expected(c, false);
      }
      ++controls;
    }
    ++passed;
  }
  std::cout << "D3D12+Vulkan: " << passed << " cases/API; " << gamma
            << " gamma exact to independent oracle, " << identical
            << " gamma byte-identical non-FP16 cases, " << half
            << " gamma FP16 cases (" << half_different
            << " differ: DXBC RTZ / SPIR-V RTE)\n";
  std::cout << controls
            << " non-gamma cases/API exact to original GPU baseline; "
               "pre-existing oracle differences D3D12="
            << legacy_dx << " Vulkan=" << legacy_vk << "\n";
  for (const auto& [name, count] : legacy_groups)
    std::cout << "Unchanged baseline difference: " << name << " cases=" << count << '\n';
  std::cout << "Fixed16: " << fixed << " cases/API; oracle failures D3D12="
            << fixed_bad_dx << " Vulkan=" << fixed_bad_vk << '\n';
  std::cout << "Fixed16 cross-API: " << fixed_identical << " byte-identical non-FP16; "
            << fixed_half << " FP16 (" << fixed_half_different
            << " differ: DXBC RTZ / SPIR-V RTE)\n";
  Need(!fixed_bad_dx && !fixed_bad_vk, "fixed16 independent oracle");
  Need(!rgb_bad_dx && !rgb_bad_vk, "RGB565 independent rational oracle");
  Need(!legacy_only || (!legacy_dx && !legacy_vk), "R8 independent oracle");
}
void ValidateReference() {
  Need(rgb565_only || legacy_only, "reference check requires a focused format");
  Need(!shader_directory.empty(), "reference check requires --shader-dir");
  Dx dx;
  Vk vk;
  unsigned cases = 0, failures = 0;
  for (const auto& c : TestCases()) {
    auto a = dx.Run(c), b = vk.Run(c);
    failures += Check(c, a.data, true) != 0;
    failures += Check(c, b.data, false) != 0;
    Need(a.data == b.data, "D3D12/Vulkan reference byte equivalence");
    ++cases;
  }
  std::cout << "Reference kernels: " << cases << " cases/API, " << failures
            << " oracle failures; D3D12/Vulkan byte-identical\n";
  Need(!failures, "reference independent oracle");
}
void Percentiles(const char *api, const char *metric,
                 std::vector<double> values) {
  std::sort(values.begin(), values.end());
  auto n = values.size();
  auto percentile = [&](unsigned p) { return values[(n * p + 99) / 100 - 1]; };
  std::cout << api << ' ' << metric << " p50=" << percentile(50)
            << " p95=" << percentile(95)
            << " p99=" << percentile(99) << " ns\n";
}
template <class Backend> void Bench(const char *name) {
  Backend backend;
  Case c;
  c.width = 640;
  c.height = 384;
  c.gamma = bench_gamma;
  c.fixed16 = bench_fixed16;
  c.uniform_fixed16 = bench_fixed16;
  c.source64 = bench_source64;
  c.bias = bench_gamma ? 0 : 1;
  c.msaa = bench_msaa;
  c.select = c.msaa == 0 ? 0 : c.msaa == 1 ? 4 : 6;
  c.log = 2;
  c.format = xenos::ColorFormat::k_8_8_8_8;
  if (bench_rgb565) {
    c.gamma = false;
    c.log = 1;
    c.format = xenos::ColorFormat::k_5_6_5;
    c.scale = 2;
    c.bias = 0;
    c.uniform_rgb565 = true;
  }
  // Keep the GPU busy across timestamp batches; short batches let readback
  // and fence latency dominate the duty cycle and destabilize clock states.
  constexpr unsigned repeats = 256;
  auto first = backend.Run(c, true, repeats);
  Need(Check(c, first.data, std::is_same_v<Backend, Dx>) == 0,
       "benchmark must be numerically correct");
  std::vector<double> gpu, cpu, submit;
  for (unsigned i = 0; i < 130; ++i) {
    auto r = backend.Run(c, false, repeats);
    if (i >= 30) {
      gpu.push_back(r.gpu_ns);
      cpu.push_back(r.prepare_ns / repeats);
      submit.push_back(r.submit_ns / repeats);
    }
  }
  Percentiles(name, "GPU resolve", gpu);
  Percentiles(name, "CPU prepare", cpu);
  Percentiles(name, "CPU submit", submit);
  std::sort(gpu.begin(), gpu.end());
  std::cout << name << " samples=" << (1u << c.msaa) << " gamma=" << c.gamma
            << " fixed16=" << c.fixed16 << " source64=" << c.source64
            << " scale=" << c.scale
            << " pixels/resolve=" << c.width * c.height * c.scale * c.scale << " batch=" << repeats
            << " throughput pixels/s="
            << double(c.width * c.height * c.scale * c.scale) * 1e9 / gpu[(gpu.size() + 1) / 2 - 1]
            << " input setup ns=" << first.upload_ns
            << " fixture bytes=" << kEdramBytes * 2 + kDestBytes * 2 + 16
            << '\n';
}
int main(int argc, char **argv) {
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
  try {
    bool bench = false;
    std::string api = "both";
    for (int i = 1; i < argc; ++i) {
      std::string_view a = argv[i];
      if (a == "--bench")
        bench = true;
      else if (a == "--fixed16-only")
        fixed16_only = true;
      else if (a == "--extended-fixed16")
        extended_fixed16 = true;
      else if (a == "--rgb565-only")
        rgb565_only = true;
      else if (a == "--legacy-r8-only")
        legacy_only = true;
      else if (a == "--extended-rgb565")
        extended_rgb565 = true;
      else if (a == "--bench-rgb565")
        bench_rgb565 = true;
      else if (a == "--reference-check")
        reference_check = true;
      else if (a == "--non-gamma")
        bench_gamma = false;
      else if (a == "--bench-fixed16") {
        bench_fixed16 = true;
        bench_gamma = false;
      } else if (a == "--source64")
        bench_source64 = true;
      else if (a == "--msaa" && i + 1 < argc) {
        unsigned n = unsigned(std::stoul(argv[++i]));
        Need(n == 1 || n == 2 || n == 4, "MSAA must be 1,2,4");
        bench_msaa = n == 1 ? 0 : n == 2 ? 1 : 2;
      } else if (a == "--d3d12")
        api = "d3d12";
      else if (a == "--vulkan")
        api = "vulkan";
      else if (a == "--shader-dir" && i + 1 < argc)
        shader_directory = argv[++i];
      else if (a == "--baseline-dir" && i + 1 < argc)
        baseline_directory = argv[++i];
      else
        throw std::runtime_error("unknown argument");
    }
    if (reference_check)
      ValidateReference();
    else if (api == "both" && !bench)
      ValidateBoth();
    else {
      if (api != "vulkan") {
        if (bench)
          Bench<Dx>("D3D12");
        else
          Validate<Dx>("D3D12");
      }
      if (api != "d3d12") {
        if (bench)
          Bench<Vk>("Vulkan");
        else
          Validate<Vk>("Vulkan");
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "FAIL " << e.what() << '\n';
    return 1;
  }
  return 0;
}
