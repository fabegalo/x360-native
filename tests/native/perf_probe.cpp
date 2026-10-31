// Headless production VulkanRender workloads. Synthetic throughput is not FPS.
#include "../../src/native/vulkan/vulkan_render.h"
#include "../../src/native/vulkan/vulkan_storage_buffer_range.h"
#include "../../src/native/textures/texture_load.h"
#include "../../src/native/commands/primitive_indices.h"
#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/cvar.h>
#include <rex/chrono/frame_timing.h>
#include <rex/logging.h>
#include <rex/graphics/format/ucode.h>
#include <Windows.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <bit>
#include <span>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <condition_variable>
#include <mutex>
#include <thread>
using namespace x360n::native;
using namespace rex::graphics;
using namespace rex::graphics::xenos;
namespace {
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void CheckBufferDependencies() {
  VulkanBufferAccess state;
  const auto shader_write = VK_ACCESS_SHADER_WRITE_BIT;
  const auto shader_read = VK_ACCESS_SHADER_READ_BIT;
  const auto transfer_read = VK_ACCESS_TRANSFER_READ_BIT;
  state.Use(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, shader_write);
  auto dependency = state.Use(VK_PIPELINE_STAGE_TRANSFER_BIT, transfer_read);
  Require(dependency && dependency->stages == VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT &&
      dependency->access == shader_write, "compute write to readback memory dependency");
  Require(!state.Use(VK_PIPELINE_STAGE_TRANSFER_BIT, transfer_read), "repeated readback added a dependency");
  dependency = state.Use(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, shader_read);
  Require(dependency && dependency->stages == VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT &&
      dependency->access == shader_write, "new shader reader lost the preceding compute writer");
  Require(!state.Use(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, shader_read), "repeated shader read added a dependency");
  dependency = state.Use(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, shader_write);
  const auto all_stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT |
      VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
  Require(dependency && (dependency->stages & all_stages) == all_stages &&
      (dependency->access & (shader_write | shader_read | transfer_read)) == (shader_write | shader_read | transfer_read),
      "next buffer write did not wait for every reader and writer");
  Require(bool(state.Use(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, shader_write)), "consecutive writes lost WAW dependency");
  Require(bool(state.Use(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, shader_read)), "new write retained stale read visibility");
  Require(bool(state.Use(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, shader_read, true)), "forced dependency was removed");
  dependency = state.Use(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, shader_read);
  Require(dependency && dependency->stages == VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT &&
      dependency->access == shader_write, "forced read discarded the last writer");
  VulkanBufferAccess split;
  split.Use(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, shader_write);
  split.Use(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_ACCESS_UNIFORM_READ_BIT);
  split.Use(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, shader_read);
  Require(bool(split.Use(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, shader_read)),
      "independent read scopes incorrectly covered a new stage/access pair");
  std::cout << "PASS Vulkan buffer dependencies: all readers see their writer; WAR/WAW and repeated readers preserved\n";
}
std::vector<uint32_t> ExportShader() {
  using namespace ucode;
  const uint64_t alloc = (uint64_t(AllocType::kMemory) << 41) | (uint64_t(ControlFlowOpcode::kAlloc) << 44);
  const uint64_t exec = 1 | (uint64_t(2) << 12) | (uint64_t(ControlFlowOpcode::kExecEnd) << 44);
  return {uint32_t(alloc), uint32_t(alloc >> 32) | uint32_t(exec << 16), uint32_t(exec >> 16),
      32 | (1u << 15) | (15u << 16), 0x6Cu << 16, (1u << 8) | (1u << 31) | (uint32_t(AluVectorOpcode::kMad) << 24),
      33 | (1u << 15) | (15u << 16), 0, (uint32_t(AluVectorOpcode::kMax) << 24) | (2u << 16) | (2u << 8)};
}
std::vector<uint32_t> TextureExportShader(FetchOpDimension dimension = FetchOpDimension::k2D) {
  using namespace ucode;
  const uint64_t alloc = (uint64_t(AllocType::kMemory) << 41) | (uint64_t(ControlFlowOpcode::kAlloc) << 44);
  // eA = r0.x * c1 + c0; r1 = c2; tfetch r2, r1, tf0; eM0 = r2.
  const uint64_t exec = 1 | (uint64_t(4) << 12) | (uint64_t(1) << 20) |
      (uint64_t(ControlFlowOpcode::kExecEnd) << 44);
  return {uint32_t(alloc), uint32_t(alloc >> 32) | uint32_t(exec << 16), uint32_t(exec >> 16),
      32 | (1u << 15) | (15u << 16), 0x6Cu << 16,
      (1u << 8) | (1u << 31) | (uint32_t(AluVectorOpcode::kMad) << 24),
      1 | (15u << 16), 0, (uint32_t(AluVectorOpcode::kMax) << 24) | (2u << 16) | (2u << 8),
      uint32_t(FetchOpcode::kTextureFetch) | (1u << 5) | (2u << 12) | (0x24u << 26),
      0x688u | (3u << 12) | (3u << 14) | (3u << 16) | (7u << 18) | (3u << 24) | (3u << 26),
      uint32_t(dimension) << 14,
      33 | (1u << 15) | (15u << 16), 0,
      (uint32_t(AluVectorOpcode::kMax) << 24) | (2u << 16) | (2u << 8) | (3u << 30)};
}
std::vector<uint32_t> RasterShader(bool vertex) {
  using namespace ucode;
  const uint64_t alloc = (uint64_t(vertex ? AllocType::kVsPosition : AllocType::kPsColors) << 41) |
      (uint64_t(ControlFlowOpcode::kAlloc) << 44);
  const uint64_t exec = 1 | (uint64_t(vertex ? 2 : 1) << 12) | (uint64_t(vertex) << 16) |
      (uint64_t(ControlFlowOpcode::kExecEnd) << 44);
  std::vector<uint32_t> code{uint32_t(alloc), uint32_t(alloc >> 32) | uint32_t(exec << 16), uint32_t(exec >> 16)};
  if (vertex) code.insert(code.end(), {(1u << 12) | (1u << 19),
      0x688u | (1u << 13) | (uint32_t(VertexFormat::k_32_32_32_32_FLOAT) << 16), 4});
  code.insert(code.end(), {(vertex ? 62u : 0u) | (1u << 15) | (15u << 16), 0,
      (uint32_t(AluVectorOpcode::kMax) << 24) | (vertex ? (1u << 16) | (1u << 8) | (3u << 30) : 0)});
  return code;
}
uint64_t CpuNs() {
  FILETIME c, e, k, u;
  Require(GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u), "process CPU clock");
  auto ticks=[](FILETIME t) { return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime; };
  return (ticks(k)+ticks(u))*100;
}
DXGI_QUERY_VIDEO_MEMORY_INFO VideoMemory(const rex::ui::vulkan::VulkanDevice& device) {
  VkPhysicalDeviceProperties properties;
  device.vulkan_instance()->functions().vkGetPhysicalDeviceProperties(device.physical_device(),&properties);
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
  Require(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))),"memory budget DXGI factory");
  Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
  for(unsigned i=0;factory->EnumAdapters1(i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i) {
    DXGI_ADAPTER_DESC1 desc; Require(SUCCEEDED(adapter->GetDesc1(&desc)),"memory budget adapter");
    if(desc.VendorId!=properties.vendorID||desc.DeviceId!=properties.deviceID) continue;
    Microsoft::WRL::ComPtr<IDXGIAdapter3> queried;
    Require(SUCCEEDED(adapter.As(&queried)),"memory budget interface");
    DXGI_QUERY_VIDEO_MEMORY_INFO result;
    Require(SUCCEEDED(queried->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&result)),"local video memory usage");
    return result;
  }
  throw std::runtime_error("Vulkan adapter missing from DXGI budget enumeration");
}
void CheckResolveContracts(VulkanRender& render, RegisterFile& regs,
    rex::memory::Memory& memory, uint32_t output) {
  const uint32_t physical=memory.GetPhysicalAddress(output)+65536;
  reg::RB_COPY_CONTROL control{}; control.copy_command=CopyCommand(1); control.color_clear_enable=1;
  regs[XE_GPU_REG_RB_COPY_CONTROL]=control.value;
  auto resolve=[&](unsigned index,uint32_t clear) {
    regs[XE_GPU_REG_RB_COPY_DEST_BASE]=physical+index*4096;
    regs[XE_GPU_REG_RB_COLOR_CLEAR]=clear;
    regs[XE_GPU_REG_RB_COLOR_CLEAR_LO]=clear;
    render.Resolve(regs,memory); render.Flush(false);
  };
  constexpr uint32_t a=0x332211FF,b=0x11CC22FF;
  resolve(0,a); resolve(1,b); resolve(2,0);
  render.CompleteGuestWrites(kGuestSyncEvent,0);
  for(unsigned i=0;i<1024;++i) {
    Require(memory.TranslatePhysical<const uint32_t*>(physical+4096)[i]==a,"colored resolve A bytes");
    Require(memory.TranslatePhysical<const uint32_t*>(physical+8192)[i]==b,"colored resolve B bytes");
  }
  ScanoutTexture input{physical+4096,32,32,32,1,0,0|(1<<3)|(2<<6)|(3<<9),4096,false},scaled;
  std::vector<uint8_t> bytes;
  const bool enhanced=render.PrepareScanout(input,memory.TranslatePhysical(input.address),scaled,bytes);
  if(GetNativeQualitySettings().scale_x>1) {
    Require(enhanced,"scaled resolve cache missing completed ticket");
    for(unsigned y=0;y<scaled.height;++y) for(unsigned x=0;x<scaled.width;++x) {
      uint32_t actual; std::memcpy(&actual,bytes.data()+(size_t(y)*scaled.pitch+x)*4,4);
      Require(actual==a,"scaled ticket/scanout bytes");
    }
    *memory.TranslateVirtual<uint32_t*>(output+65536+4096)=0x89ABCDEF;
    Require(!render.PrepareScanout(input,memory.TranslatePhysical(input.address),scaled,bytes),"CPU write kept stale scaled cache");
  }
  std::cout<<"PASS colored resolves and scaled snapshots byte-for-byte\n";
}
void CheckTextureResolveDependency(VulkanRender& render, const RegisterFile& draw_template,
    const RegisterFile& resolve_template, rex::memory::Memory& memory, uint32_t output) {
  const uint32_t physical = memory.GetPhysicalAddress(output);
  const uint32_t source = physical + 0x40000, destination = physical + 0x50000;
  RegisterFile draw(draw_template), resolve(resolve_template);
  reg::SQ_PROGRAM_CNTL program{}; program.vs_num_reg = 3;
  draw[XE_GPU_REG_SQ_PROGRAM_CNTL] = program.value;
  xe_gpu_memexport_stream_t stream{};
  stream.base_address = destination >> 2; stream.const_0x1 = 1;
  stream.const_0x4b000000 = 0x4B000000; stream.format = ColorFormat::k_32_32_32_32_FLOAT;
  stream.num_format = SurfaceNumberFormat::kFloat; stream.const_0x4b0 = 0x4B0;
  stream.index_count = 1; stream.const_0x96 = 0x96;
  std::memcpy(&draw[XE_GPU_REG_SHADER_CONSTANT_000_X], &stream, sizeof(stream));
  const float address_scale[]{0, 1, 0, 0}, uv[]{0.5f, 0.5f, 0, 0};
  std::memcpy(&draw[XE_GPU_REG_SHADER_CONSTANT_000_X + 4], address_scale, sizeof(address_scale));
  std::memcpy(&draw[XE_GPU_REG_SHADER_CONSTANT_000_X + 8], uv, sizeof(uv));
  auto code = TextureExportShader();
  for (auto& word : code) word = rex::byte_swap(word);
  auto* vs = render.LoadShader(ShaderType::kVertex, code.data(), uint32_t(code.size()));
  // Exercise the GPU load and the RGBA4 CPU conversion from a real resolve.
  // Both fetch a uniform white texture, producing an exactly representable
  // float4 through the guest vertex shader's MEMEXPORT.
  for (const bool cpu_conversion : {false, true}) {
    render.Flush(true);
    std::memset(memory.TranslateVirtual(output + 0x40000), 0xCD, 4096);
    std::memset(memory.TranslateVirtual(output + 0x50000), 0xCD, 4096);
    reg::RB_COPY_CONTROL control{}; control.copy_command = CopyCommand(1); control.color_clear_enable = 1;
    resolve[XE_GPU_REG_RB_COPY_CONTROL] = control.value;
    resolve[XE_GPU_REG_RB_COPY_DEST_BASE] = physical + 0x60000;
    resolve[XE_GPU_REG_RB_COLOR_CLEAR] = resolve[XE_GPU_REG_RB_COLOR_CLEAR_LO] = UINT32_MAX;
    render.Resolve(resolve, memory);
    control.color_clear_enable = 0;
    resolve[XE_GPU_REG_RB_COPY_CONTROL] = control.value;
    resolve[XE_GPU_REG_RB_COPY_DEST_BASE] = source;
    render.Resolve(resolve, memory);
    xe_gpu_texture_fetch_t fetch{}; fetch.type = FetchConstantType::kTexture;
    fetch.dimension = DataDimension::k2DOrStacked;
    fetch.format = cpu_conversion ? TextureFormat::k_4_4_4_4 : TextureFormat::k_8_8_8_8;
    fetch.base_address = source >> 12; fetch.pitch = 1; fetch.tiled = true;
    fetch.size_2d.width = fetch.size_2d.height = 31; fetch.swizzle = 0x688;
    std::memcpy(&draw[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0], &fetch, sizeof(fetch));
    const auto before = NativeVulkanPerformance();
    render.Draw(draw, memory, vs, nullptr, PrimitiveType::kTriangleList, 1, nullptr);
    const auto submitted = NativeVulkanPerformance().submissions - before.submissions;
    const auto published = NativeVulkanPerformance().readbacks - before.readbacks;
    render.CompleteGuestWrites(kGuestSyncPacket, 0);
    const std::array<float, 4> white{1, 1, 1, 1};
    Require(!std::memcmp(memory.TranslatePhysical(destination), white.data(), sizeof(white)),
        "resolve texture fetch/MEMEXPORT bytes");
    const bool cpu_read = cpu_conversion || GetNativeQualitySettings().scale_x > 1;
    if (before.enabled) {
      Require(cpu_read ? submitted != 0 && published != 0 : submitted == 0 && published == 0,
          cpu_read ? "CPU texture conversion did not publish its resolve dependency" :
                     "GPU texture load waited for CPU resolve publication");
    }
  }
  std::cout << "PASS resolve-to-texture dependencies: GPU stays queued, CPU conversion publishes, sampled bytes exact\n";
}
// Same contract as the D3D12 probe: an unaligned copy of identical guest
// indices selects the CPU-normalized reference; the aligned copy must render
// identical pixels from the resident mirror, or keep the documented fallback.
void CheckResidentIndices(VulkanRender& render, RegisterFile draw, RegisterFile resolve,
    rex::memory::Memory& memory, uint32_t output, const rex::ui::vulkan::VulkanDevice& device) {
  auto& perf = NativeVulkanPerformance();
  Require(perf.enabled, "resident index contracts require native_vulkan_perf");
  const auto vertices = memory.SystemHeapAlloc(4096, 4096, rex::memory::kSystemHeapPhysical);
  const auto index_storage = memory.SystemHeapAlloc(4096, 4096, rex::memory::kSystemHeapPhysical);
  Require(vertices && index_storage, "resident index allocations");
  auto load = [&](bool vertex, std::vector<uint32_t> words) {
    for (auto& word : words) word = rex::byte_swap(word);
    return render.LoadShader(vertex ? ShaderType::kVertex : ShaderType::kPixel, words.data(), uint32_t(words.size()));
  };
  auto* vs = load(true, RasterShader(true));
  auto* ps = load(false, RasterShader(false));
  const float triangle[]{-1, -1, .5f, 1, -1, 3, .5f, 1, 3, -1, .5f, 1};
  xe_gpu_vertex_fetch_t fetch{};
  fetch.type = FetchConstantType::kVertex; fetch.address = memory.GetPhysicalAddress(vertices) >> 2; fetch.size = 16;
  draw[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0] = fetch.dword_0;
  draw[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_1] = fetch.dword_1;
  draw[XE_GPU_REG_RB_MODECONTROL] = uint32_t(EdramMode::kColorDepth);
  draw[XE_GPU_REG_RB_SURFACE_INFO] = 80;
  draw[XE_GPU_REG_RB_COLOR_MASK] = 15;
  draw[XE_GPU_REG_RB_COLOR_INFO] = 0; // RGBA8 at EDRAM base 0.
  reg::RB_BLENDCONTROL blend{};
  blend.color_srcblend = blend.alpha_srcblend = BlendFactor::kOne;
  draw[XE_GPU_REG_RB_BLENDCONTROL0] = blend.value;
  draw[XE_GPU_REG_PA_CL_VTE_CNTL] = 0x3F;
  auto fp = [&](unsigned reg, float value) { draw[reg] = std::bit_cast<uint32_t>(value); };
  fp(XE_GPU_REG_PA_CL_VPORT_XSCALE, 16); fp(XE_GPU_REG_PA_CL_VPORT_XOFFSET, 16);
  fp(XE_GPU_REG_PA_CL_VPORT_YSCALE, -16); fp(XE_GPU_REG_PA_CL_VPORT_YOFFSET, 16);
  fp(XE_GPU_REG_PA_CL_VPORT_ZSCALE, 1); fp(XE_GPU_REG_PA_CL_VPORT_ZOFFSET, 0);
  draw[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR] = 32 | (32 << 16);
  draw[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL] = 0x80000000;
  draw[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR] = 32 | (32 << 16);
  draw[XE_GPU_REG_PA_SU_VTX_CNTL] = 1;
  const std::array<float, 4> color{.25f, .5f, .75f, 1};
  std::memcpy(&draw[XE_GPU_REG_SHADER_CONSTANT_000_X + 256 * 4], color.data(), sizeof(color));
  reg::RB_COPY_CONTROL copy{}; copy.copy_command = CopyCommand(1); copy.color_clear_enable = 1;
  resolve[XE_GPU_REG_RB_COPY_CONTROL] = copy.value;
  resolve[XE_GPU_REG_RB_COLOR_CLEAR] = resolve[XE_GPU_REG_RB_COLOR_CLEAR_LO] = 0;
  resolve[XE_GPU_REG_RB_COPY_DEST_BASE] = memory.GetPhysicalAddress(output) + 4096;
  auto capture = [&] {
    render.Resolve(resolve, memory); render.Flush(true);
    render.CompleteGuestWrites(kGuestSyncPacket, 0);
    const auto* bytes = memory.TranslatePhysical<const uint8_t*>(resolve[XE_GPU_REG_RB_COPY_DEST_BASE]);
    return std::vector<uint8_t>(bytes, bytes + 4096);
  };
  capture(); // Clear EDRAM before the first comparison.
  const uint32_t indices_physical = memory.GetPhysicalAddress(index_storage);
  auto encode = [&](std::span<const uint32_t> indices, IndexFormat format, Endian endian, bool unaligned) {
    const unsigned bytes = format == IndexFormat::kInt16 ? 2 : 4;
    const unsigned size = (unsigned(indices.size()) * bytes + 3) & ~3u;
    std::vector<uint8_t> encoded(size);
    for (unsigned i = 0; i < indices.size(); ++i) for (unsigned b = 0; b < bytes; ++b) {
      const unsigned offset = (i * bytes + b) ^ (endian == Endian::k8in16 ? 1u : endian == Endian::k8in32 ? 3u : endian == Endian::k16in32 ? 2u : 0u);
      encoded.at(offset) = uint8_t(indices[i] >> (b * 8));
    }
    const unsigned offset = unaligned ? 65 : bytes == 2 ? 2 : 0;
    std::memcpy(memory.TranslateVirtual<uint8_t*>(index_storage) + offset, encoded.data(), encoded.size());
    return DrawIndices{format, endian, uint32_t(indices.size()), indices_physical + offset, size};
  };
  unsigned comparisons = 0;
  auto compare = [&](std::span<const uint32_t> indices, IndexFormat format, Endian endian,
                     PrimitiveType primitive, bool restart, int32_t offset, uint32_t minimum, uint32_t maximum) {
    std::memset(memory.TranslateVirtual(vertices), 0, 4096);
    std::memcpy(memory.TranslateVirtual<uint8_t*>(vertices) + minimum * 16, triangle, sizeof(triangle));
    draw[XE_GPU_REG_VGT_INDX_OFFSET] = uint32_t(offset);
    draw[XE_GPU_REG_VGT_MIN_VTX_INDX] = minimum; draw[XE_GPU_REG_VGT_MAX_VTX_INDX] = maximum;
    reg::VGT_DRAW_INITIATOR initiator{}; initiator.prim_type = primitive;
    draw[XE_GPU_REG_VGT_DRAW_INITIATOR] = initiator.value;
    reg::PA_SU_SC_MODE_CNTL mode{}; mode.multi_prim_ib_ena = restart;
    draw[XE_GPU_REG_PA_SU_SC_MODE_CNTL] = mode.value;
    draw[XE_GPU_REG_VGT_MULTI_PRIM_IB_RESET_INDX] = 0xFFFFFF;
    auto cpu = encode(indices, format, endian, true);
    render.Draw(draw, memory, vs, ps, primitive, uint32_t(indices.size()), &cpu);
    const auto expected = capture();
    Require(std::any_of(expected.begin(), expected.end(), [](uint8_t b) { return b != 0; }),
        "reference indexed triangle did not render");
    auto gpu = encode(indices, format, endian, false);
    const auto before = perf;
    render.Draw(draw, memory, vs, ps, primitive, uint32_t(indices.size()), &gpu);
    const auto after = perf;
    const auto actual = capture();
    Require(actual == expected, "resident and normalized index pixels differ");
    const bool direct = !restart && !NeedsPrimitiveConversion(primitive) &&
        (format == IndexFormat::kInt16 || device.properties().fullDrawIndexUint32) &&
        (format == IndexFormat::kInt32 || endian == Endian::kNone || endian == Endian::k8in16);
    Require(after.gpu_index_draws - before.gpu_index_draws == unsigned(direct),
        "resident index path/fallback selection changed");
    Require(after.normalized_indices - before.normalized_indices == (direct ? 0 : indices.size()),
        "resident indices were normalized on the CPU");
    ++comparisons;
  };
  for (const auto format : {IndexFormat::kInt16, IndexFormat::kInt32})
    for (const auto endian : {Endian::kNone, Endian::k8in16, Endian::k8in32, Endian::k16in32}) {
      const std::array<uint32_t, 3> ordinary{0, 1, 2};
      compare(ordinary, format, endian, PrimitiveType::kTriangleList, false, 0, 0, 0xFFFFFF);
      const std::array<uint32_t, 3> high = format == IndexFormat::kInt16
          ? std::array<uint32_t, 3>{65533, 65534, 65535}
          : std::array<uint32_t, 3>{0xAB000000, 0xAB000001, 0xAB000002};
      compare(high, format, endian, PrimitiveType::kTriangleList, false,
          format == IndexFormat::kInt16 ? -65533 : 0, 0, 0xFFFFFF);
    }
  const std::array<uint32_t, 3> clamped{0, 2, 999};
  compare(clamped, IndexFormat::kInt32, Endian::k8in32, PrimitiveType::kTriangleList, false, 0, 1, 3);
  const std::array<uint32_t, 3> strip{0, 1, 2};
  compare(strip, IndexFormat::kInt16, Endian::k8in16, PrimitiveType::kTriangleStrip, false, 0, 0, 0xFFFFFF);
  compare(strip, IndexFormat::kInt32, Endian::k8in32, PrimitiveType::kTriangleFan, false, 0, 0, 0xFFFFFF);
  const std::array<uint32_t, 7> restarted{0, 1, 2, 0xFFFFFF, 0, 1, 2};
  compare(restarted, IndexFormat::kInt32, Endian::k8in32, PrimitiveType::kTriangleStrip, true, 0, 0, 0xFFFFFF);

  // A GPU producer updates the index bytes in the resident mirror. The draw
  // must consume them without waiting for their guest readback publication.
  draw[XE_GPU_REG_VGT_INDX_OFFSET] = draw[XE_GPU_REG_VGT_MIN_VTX_INDX] = 0;
  draw[XE_GPU_REG_VGT_MAX_VTX_INDX] = 0xFFFFFF; draw[XE_GPU_REG_PA_SU_SC_MODE_CNTL] = 0;
  reg::VGT_DRAW_INITIATOR initiator{}; initiator.prim_type = PrimitiveType::kTriangleList;
  draw[XE_GPU_REG_VGT_DRAW_INITIATOR] = initiator.value;
  std::memcpy(memory.TranslateVirtual(vertices), triangle, sizeof(triangle));
  std::memset(memory.TranslateVirtual(index_storage), 0xCC, 16);
  RegisterFile producer;
  producer[XE_GPU_REG_SQ_PROGRAM_CNTL] = draw[XE_GPU_REG_SQ_PROGRAM_CNTL];
  producer[XE_GPU_REG_SQ_VS_CONST] = 0x000FF000;
  producer[XE_GPU_REG_VGT_MAX_VTX_INDX] = 0xFFFFFF;
  producer[XE_GPU_REG_RB_MODECONTROL] = uint32_t(EdramMode::kNoOperation);
  xe_gpu_memexport_stream_t stream{}; stream.base_address = indices_physical >> 2; stream.const_0x1 = 1;
  stream.const_0x4b000000 = 0x4B000000; stream.format = ColorFormat::k_16_16_16_16;
  stream.num_format = SurfaceNumberFormat::kUnsignedInteger; stream.const_0x4b0 = 0x4B0;
  stream.index_count = 1; stream.const_0x96 = 0x96;
  std::memcpy(&producer[XE_GPU_REG_SHADER_CONSTANT_000_X], &stream, sizeof(stream));
  producer[XE_GPU_REG_SHADER_CONSTANT_000_X + 5] = std::bit_cast<uint32_t>(1.f);
  const std::array<float, 4> produced{0, 1, 2, 0};
  std::memcpy(&producer[XE_GPU_REG_SHADER_CONSTANT_000_X + 8], produced.data(), sizeof(produced));
  auto* exporter = load(true, ExportShader());
  render.Draw(producer, memory, exporter, nullptr, PrimitiveType::kTriangleList, 1, nullptr);
  const auto before = perf;
  DrawIndices produced_indices{IndexFormat::kInt16, Endian::kNone, 3, indices_physical, 8};
  render.Draw(draw, memory, vs, ps, PrimitiveType::kTriangleList, 3, &produced_indices);
  Require(perf.submissions == before.submissions && perf.readbacks == before.readbacks,
      "GPU index consumer waited for CPU readback");
  Require(perf.gpu_index_draws == before.gpu_index_draws + 1, "GPU-produced indices left the resident path");
  const auto produced_pixels = capture();
  const std::array<uint16_t, 4> expected_indices{0, 1, 2, 0};
  Require(!std::memcmp(memory.TranslatePhysical(indices_physical), expected_indices.data(), 8), "GPU index producer bytes");
  const auto cpu = encode(strip, IndexFormat::kInt16, Endian::kNone, true);
  render.Draw(draw, memory, vs, ps, PrimitiveType::kTriangleList, 3, &cpu);
  Require(capture() == produced_pixels, "GPU-produced resident indices differ from CPU-normalized pixels");
  for (const auto invalid : {DrawIndices{IndexFormat::kInt16, Endian::kNone, 3, indices_physical, 5},
                            DrawIndices{IndexFormat::kInt32, Endian::kNone, 3, 0x1FFFFFF8, 12},
                            DrawIndices{IndexFormat::kInt32, Endian::kNone, 2, indices_physical, 12}}) {
    bool rejected = false;
    try { render.Draw(draw, memory, vs, ps, PrimitiveType::kTriangleList, 3, &invalid); }
    catch (const std::runtime_error&) { rejected = true; }
    Require(rejected, "resident indices bypassed extent validation");
  }
  render.Flush(true);
  memory.SystemHeapFree(index_storage); memory.SystemHeapFree(vertices);
  std::cout << "PASS Vulkan resident indices: " << comparisons
      << " normalized/GPU pixel pairs, endian, offset/clamp, restart/conversion fallbacks, CPU writes, GPU producer and bounds\n";
}
void CheckStorageRangeSelection() {
  constexpr VkDeviceSize mib = 1ull << 20, buffer = 512 * mib, limit = 128 * mib;
  auto range = TextureStorageBufferRange(buffer, 129 * mib, 4096, UINT32_MAX, 256);
  Require(range.offset == 0 && range.size == buffer, "large-range device lost whole-buffer binding");
  range = TextureStorageBufferRange(buffer, 129 * mib, 4096, limit, 256);
  Require(range.offset == limit && range.size == limit, "aligned source window");
  const auto adjacent = TextureStorageBufferRange(buffer, 130 * mib, 4096, limit, 256);
  Require(adjacent.offset == range.offset && adjacent.size == range.size, "adjacent loads cannot reuse a source window");
  range = TextureStorageBufferRange(buffer, limit - 11, 32, limit, 256);
  Require(range.offset == limit - 256 && range.size == limit, "cross-window load was truncated or misaligned");
  range = TextureStorageBufferRange(144 * mib, 128 * mib, 16 * mib, limit, 256);
  Require(range.offset == limit && range.size == 16 * mib, "scratch tail range");
  range = TextureStorageBufferRange(buffer, buffer - 16, 16, limit, 256);
  Require(range.offset + range.size == buffer, "last guest vector range");
  bool rejected = false;
  try { TextureStorageBufferRange(buffer, 0, limit + 16, limit, 256); }
  catch (const std::runtime_error& e) { rejected = std::string(e.what()).find("maxStorageBufferRange") != std::string::npos; }
  Require(rejected, "oversized texture dispatch did not report the device storage limit");
  rejected = false;
  try { TextureStorageBufferRange(buffer, buffer - 8, 16, limit, 256); }
  catch (const std::runtime_error&) { rejected = true; }
  Require(rejected, "texture descriptor exceeded its backing buffer");
  std::cout << "PASS storage range selection: aligned/cross-window/tail spans, reuse and explicit oversized rejection\n";
}
// The physical GPU still has its real limit. This fixture lowers Native's
// advertised limit and checks every descriptor passed to the real driver;
// it does not claim validation on hardware with a native 128 MiB limit.
struct StorageLimitFixture {
  using Device = rex::ui::vulkan::VulkanDevice;
  Device::Properties& properties;
  Device::Functions& functions;
  uint32_t original_limit;
  PFN_vkCreateBuffer create;
  PFN_vkDestroyBuffer destroy;
  PFN_vkUpdateDescriptorSets update;
  PFN_vkAllocateDescriptorSets allocate_sets;
  PFN_vkResetDescriptorPool reset_pool;
  std::map<VkBuffer, VkDeviceSize> buffers;
  std::map<VkDescriptorPool, uint32_t> pool_counts;
  unsigned source_sets = 0, rebased_scratch_sets = 0;
  unsigned pool_rollovers = 0;
  bool invalid_range = false;
  inline static StorageLimitFixture* active = nullptr;
  explicit StorageLimitFixture(const Device& device)
      : properties(const_cast<Device::Properties&>(device.properties())),
        functions(const_cast<Device::Functions&>(device.functions())),
        original_limit(properties.maxStorageBufferRange), create(functions.vkCreateBuffer),
        destroy(functions.vkDestroyBuffer), update(functions.vkUpdateDescriptorSets),
        allocate_sets(functions.vkAllocateDescriptorSets), reset_pool(functions.vkResetDescriptorPool) {
    properties.maxStorageBufferRange = 128u << 20;
    active = this;
    functions.vkCreateBuffer = Create;
    functions.vkDestroyBuffer = Destroy;
    functions.vkUpdateDescriptorSets = Update;
    functions.vkAllocateDescriptorSets = AllocateSets;
    functions.vkResetDescriptorPool = ResetPool;
  }
  ~StorageLimitFixture() {
    functions.vkCreateBuffer = create;
    functions.vkDestroyBuffer = destroy;
    functions.vkUpdateDescriptorSets = update;
    functions.vkAllocateDescriptorSets = allocate_sets;
    functions.vkResetDescriptorPool = reset_pool;
    properties.maxStorageBufferRange = original_limit;
    active = nullptr;
  }
  static VKAPI_ATTR VkResult VKAPI_CALL AllocateSets(VkDevice device, const VkDescriptorSetAllocateInfo* info,
      VkDescriptorSet* sets) {
    auto& fixture = *active;
    auto& count = fixture.pool_counts[info->descriptorPool];
    // Force real descriptor allocations to span several pools with a small
    // bounded workload. Both retryable Vulkan allocation errors are covered.
    if (count + info->descriptorSetCount > 8)
      return ++fixture.pool_rollovers & 1 ? VK_ERROR_OUT_OF_POOL_MEMORY : VK_ERROR_FRAGMENTED_POOL;
    const auto result = fixture.allocate_sets(device, info, sets);
    if (result == VK_SUCCESS) count += info->descriptorSetCount;
    return result;
  }
  static VKAPI_ATTR VkResult VKAPI_CALL ResetPool(VkDevice device, VkDescriptorPool pool, VkDescriptorPoolResetFlags flags) {
    const auto result = active->reset_pool(device, pool, flags);
    if (result == VK_SUCCESS) active->pool_counts[pool] = 0;
    return result;
  }
  static VKAPI_ATTR VkResult VKAPI_CALL Create(VkDevice device, const VkBufferCreateInfo* info,
      const VkAllocationCallbacks* allocator, VkBuffer* buffer) {
    const auto result = active->create(device, info, allocator, buffer);
    if (result == VK_SUCCESS) active->buffers.emplace(*buffer, info->size);
    return result;
  }
  static VKAPI_ATTR void VKAPI_CALL Destroy(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* allocator) {
    active->buffers.erase(buffer);
    active->destroy(device, buffer, allocator);
  }
  static VKAPI_ATTR void VKAPI_CALL Update(VkDevice device, uint32_t count, const VkWriteDescriptorSet* writes,
      uint32_t copy_count, const VkCopyDescriptorSet* copies) {
    auto& fixture = *active;
    for (uint32_t w = 0; w < count; ++w) {
      const auto& write = writes[w];
      if (write.descriptorType != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) continue;
      for (uint32_t b = 0; b < write.descriptorCount; ++b) {
        const auto& info = write.pBufferInfo[b];
        const auto found = fixture.buffers.find(info.buffer);
        if (found == fixture.buffers.end()) { fixture.invalid_range = true; continue; }
        const auto size = found->second;
        const auto range = info.range == VK_WHOLE_SIZE ? size - info.offset : info.range;
        fixture.invalid_range |= info.offset >= size || !range || range > size - info.offset ||
            range > fixture.properties.maxStorageBufferRange ||
            info.offset % fixture.properties.minStorageBufferOffsetAlignment != 0;
        if (write.descriptorCount == 1 && size == GuestMemoryMirror::kSize) ++fixture.source_sets;
        if (size > fixture.properties.maxStorageBufferRange && size != GuestMemoryMirror::kSize && info.offset)
          ++fixture.rebased_scratch_sets;
      }
    }
    fixture.update(device, count, writes, copy_count, copies);
  }
};
void CheckTextureStorageLimit(rex::ui::vulkan::VulkanProvider& provider, const RegisterFile& draw_template,
    rex::memory::Memory& memory, uint32_t output) {
  CheckStorageRangeSelection();
  StorageLimitFixture fixture(*provider.vulkan_device());
  struct Allocations {
    rex::memory::Memory& memory;
    std::vector<uint32_t> addresses;
    void Add(uint32_t physical, uint32_t size) {
      const uint32_t address = 0xA0000000u | physical;
      Require(memory.LookupHeap(address)->AllocFixed(address, size, 65536,
          rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit,
          rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite), "storage-limit fixture physical allocation");
      addresses.push_back(address);
    }
    ~Allocations() { for (auto address : addresses) memory.LookupHeap(address)->Release(address); }
  } allocations{memory};
  allocations.Add(0x07FF0000, 0x40000);
  allocations.Add(0x18000000, 0x20000);
  // Nine 16 MiB array layers force a real scratch buffer larger than the
  // emulated limit, while each indivisible shader dispatch still fits.
  allocations.Add(0x08040000, 144u << 20);
  VulkanRender render(provider);
  RegisterFile draw(draw_template);
  reg::SQ_PROGRAM_CNTL program{}; program.vs_num_reg = 3;
  draw[XE_GPU_REG_SQ_PROGRAM_CNTL] = program.value;
  auto shader2d = TextureExportShader(), shader3d = TextureExportShader(FetchOpDimension::k3DOrStacked);
  for (auto* words : {&shader2d, &shader3d}) for (auto& word : *words) word = rex::byte_swap(word);
  auto* vs2d = render.LoadShader(ShaderType::kVertex, shader2d.data(), uint32_t(shader2d.size()));
  auto* vs3d = render.LoadShader(ShaderType::kVertex, shader3d.data(), uint32_t(shader3d.size()));
  std::vector<std::array<float, 4>> expected;
  const uint32_t destination = memory.GetPhysicalAddress(output) + 0x70000;
  auto sample = [&](const xe_gpu_texture_fetch_t& fetch, bool use_3d, std::array<float, 4> uv,
                    std::array<float, 4> color) {
    xe_gpu_memexport_stream_t stream{};
    stream.base_address = (destination + uint32_t(expected.size()) * 16) >> 2; stream.const_0x1 = 1;
    stream.const_0x4b000000 = 0x4B000000; stream.format = ColorFormat::k_32_32_32_32_FLOAT;
    stream.num_format = SurfaceNumberFormat::kFloat; stream.const_0x4b0 = 0x4B0;
    stream.index_count = 1; stream.const_0x96 = 0x96;
    const std::array<float, 4> stride{0, 1, 0, 0};
    std::memcpy(&draw[XE_GPU_REG_SHADER_CONSTANT_000_X], &stream, sizeof(stream));
    std::memcpy(&draw[XE_GPU_REG_SHADER_CONSTANT_000_X + 4], stride.data(), sizeof(stride));
    std::memcpy(&draw[XE_GPU_REG_SHADER_CONSTANT_000_X + 8], uv.data(), sizeof(uv));
    std::memcpy(&draw[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0], &fetch, sizeof(fetch));
    render.Draw(draw, memory, use_3d ? vs3d : vs2d, nullptr, PrimitiveType::kTriangleList, 1, nullptr);
    expected.push_back(color);
  };
  auto fill = [&](uint32_t physical, size_t size, uint32_t rgba) {
    auto* begin = memory.TranslateVirtual<uint32_t*>(0xA0000000u | physical);
    std::fill_n(begin, size / 4, rgba);
  };
  xe_gpu_texture_fetch_t fetch{}; fetch.type = FetchConstantType::kTexture;
  fetch.dimension = DataDimension::k2DOrStacked; fetch.format = TextureFormat::k_8_8_8_8; fetch.swizzle = 0x688;
  fetch.size_2d.width = 64; fetch.size_2d.height = 31; fetch.pitch = 3;
  fetch.base_address = 0x07FFF000 >> 12;
  fill(0x07FFF000, 0x4000, 0xFF0000FF);
  sample(fetch, false, {.99f, .99f, 0, 0}, {1, 0, 0, 1}); // Crosses the 128 MiB window; non-vector width.
  fetch.tiled = true; fetch.base_address = 0x08010000 >> 12;
  fetch.size_2d.height = 64;
  fill(0x08010000, 0x10000, 0xFF00FF00);
  sample(fetch, false, {.99f, .99f, 0, 0}, {0, 1, 0, 1});
  fetch.size_2d.width = fetch.size_2d.height = 63; fetch.pitch = 2;
  fetch.base_address = 0x08020000 >> 12; fetch.mip_address = 0x18000000 >> 12;
  fetch.mip_min_level = fetch.mip_max_level = 1;
  fill(0x08020000, 0x10000, 0xFF0000FF); fill(0x18000000, 0x10000, 0xFFFF0000);
  sample(fetch, false, {.5f, .5f, 0, 0}, {0, 0, 1, 1}); // Base and mip occupy different windows.
  fetch.mip_min_level = fetch.mip_max_level = fetch.mip_address = 0;
  fetch.dimension = DataDimension::k3D;
  fetch.size_3d.width = 63; fetch.size_3d.height = 31; fetch.size_3d.depth = 3;
  fetch.base_address = 0x18010000 >> 12;
  fill(0x18010000, 0x10000, 0xFFFFFFFF);
  sample(fetch, true, {.99f, .99f, .99f, 0}, {1, 1, 1, 1});
  // Reuse all arena slots, changing a watched texture each time. Previously
  // recorded descriptors and upload bytes must survive until their own fence.
  fetch.dimension = DataDimension::k2DOrStacked;
  fetch.size_2d.width = fetch.size_2d.height = 31; fetch.size_2d.stack_depth = 0;
  fetch.pitch = 1; fetch.base_address = 0x08020000 >> 12;
  for (unsigned i = 0; i < 4; ++i) {
    fill(0x08020000, 4096, i & 1 ? 0xFFFFFFFF : 0);
    const float value = float(i & 1);
    sample(fetch, false, {.5f, .5f, 0, 0}, {value, value, value, value});
    render.Flush(false);
  }
  fetch.tiled = false; fetch.stacked = true; fetch.pitch = 128;
  fetch.size_2d.width = 4095; fetch.size_2d.height = 1023; fetch.size_2d.stack_depth = 8;
  fetch.base_address = 0x08040000 >> 12;
  fill(0x08040000, 144u << 20, 0);
  fill(0x10040000, 16u << 20, 0xFFFFFFFF);
  sample(fetch, true, {.99f, .99f, .5f / 9, 0}, {0, 0, 0, 0});
  sample(fetch, true, {.99f, .99f, 8.5f / 9, 0}, {1, 1, 1, 1});
  render.CompleteGuestWrites(kGuestSyncPacket, 0);
  render.Flush(true);
  for (size_t i = 0; i < expected.size(); ++i) {
    const auto* actual = memory.TranslatePhysical<const float*>(destination + uint32_t(i) * 16);
    if (std::memcmp(actual, expected[i].data(), 16)) {
      std::cerr << "storage sample " << i << " actual=" << actual[0] << ',' << actual[1] << ',' << actual[2] << ',' << actual[3] << '\n';
      throw std::runtime_error("storage-limit texture sample differs from exact float4");
    }
  }
  Require(!fixture.invalid_range, "texture load created an invalid storage descriptor under emulated 128 MiB limit");
  Require(fixture.source_sets > 0 && fixture.rebased_scratch_sets > 0, "storage test did not exercise source and scratch windows");
  Require(fixture.pool_rollovers >= 2, "storage test did not exercise descriptor pool exhaustion/fragmentation");
  std::cout << "PASS emulated maxStorageBufferRange=128 MiB (actual=" << fixture.original_limit
      << "): linear/tiled/3D/mip/array texture samples, 144 MiB scratch and arena reuse; source_sets="
      << fixture.source_sets << " rebased_scratch_sets=" << fixture.rebased_scratch_sets
      << " pool_rollovers=" << fixture.pool_rollovers << " (emulated 8 sets/pool)\n";
}
void CheckPendingContracts(VulkanRender& render, RegisterFile& regs,
    rex::memory::Memory& memory, rex::graphics::Shader* vs, uint32_t output,
    const rex::ui::vulkan::VulkanDevice& device) {
  const uint32_t physical=memory.GetPhysicalAddress(output);
  VkEventCreateInfo info{VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};
  VkEvent event;
  CheckVulkan(LoadNativeVulkanFunction<PFN_vkCreateEvent>(device,"vkCreateEvent")(
      device.device(),&info,nullptr,&event),"test gate event");
  struct EventOwner {
    const rex::ui::vulkan::VulkanDevice& device; VkEvent event;
    ~EventOwner() { LoadNativeVulkanFunction<PFN_vkDestroyEvent>(device,"vkDestroyEvent")(device.device(),event,nullptr); }
  } owner{device,event};
  VulkanSubmissionRing gate(device);
  auto commands=gate.Begin();
  LoadNativeVulkanFunction<PFN_vkCmdWaitEvents>(device,"vkCmdWaitEvents")(
      commands,1,&event,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      0,nullptr,0,nullptr,0,nullptr);
  gate.Submit();
  std::mutex mutex; std::condition_variable cv; bool release=false, timed_out=false;
  // A bounded watchdog also releases the gate on an exception or regression.
  std::jthread watchdog([&] {
    std::unique_lock lock(mutex);
    timed_out=!cv.wait_for(lock,std::chrono::seconds(2),[&]{return release;});
    CheckVulkan(LoadNativeVulkanFunction<PFN_vkSetEvent>(device,"vkSetEvent")(device.device(),event),"release test gate");
  });
  auto draw=[&](uint32_t address,float value) {
    regs[XE_GPU_REG_SHADER_CONSTANT_000_X]=(address>>2)|(1u<<30);
    for(unsigned c=0;c<4;++c) regs[XE_GPU_REG_SHADER_CONSTANT_000_X+8+c]=std::bit_cast<uint32_t>(value);
    render.Draw(regs,memory,vs,nullptr,PrimitiveType::kTriangleList,1,nullptr);
    render.Flush(false);
  };
  draw(physical,0.75f);
  *memory.TranslateVirtual<uint32_t*>(output)=0x12345678;
  draw(physical+16,0.875f); // Revalidate the CPU-written page with a new epoch.
  draw(physical+16,0.125f); // Later overlapping submission must win.
  // A disjoint guest read must leave the three gated submissions in flight.
  render.SyncGuestRead(physical+4096,4,0xFF);
  { std::lock_guard lock(mutex); release=true; } cv.notify_all(); watchdog.join();
  Require(!timed_out,"Flush(false) or disjoint read waited for pending GPU writebacks");
  // Reuse all three slots; the gate guarantees real pending resource lifetimes.
  render.SetSampleCounts({},false); render.Flush(false);
  render.SyncGuestRead(physical+16,16,0xFF);
  Require(*memory.TranslateVirtual<uint32_t*>(output)==0x12345678,"stale readback overwrote CPU epoch");
  const std::array<float,4> final{.125f,.125f,.125f,.125f};
  Require(!std::memcmp(memory.TranslatePhysical(physical+16),final.data(),16),"overlapping serial publication order");
  draw(physical+32,0.25f);
  const SampleCounts counts{1,2,3,4,5,6,7,8};
  render.SetSampleCounts(counts,false);
  Require(render.ReadSampleCounts(false)==counts,"counter readback reused live staging");
  render.CompleteGuestWrites(kGuestSyncEvent,0);
  gate.Drain();
  std::cout<<"PASS gated in-flight writebacks: disjoint read, ring reuse, CPU epochs, overlap order, counters, events\n";
}
void CheckStagingGrowth(VulkanRender& render, RegisterFile& regs,
    rex::memory::Memory& memory, rex::graphics::Shader* vs) {
  constexpr uint32_t bytes=17u<<20;
  const auto allocation=memory.SystemHeapAlloc(bytes,4096,rex::memory::kSystemHeapPhysical);
  Require(allocation!=0,"large staging allocation");
  std::memset(memory.TranslateVirtual(allocation),0xCD,bytes);
  xe_gpu_memexport_stream_t stream;
  std::memcpy(&stream,&regs[XE_GPU_REG_SHADER_CONSTANT_000_X],16);
  stream.base_address=memory.GetPhysicalAddress(allocation)>>2;
  stream.index_count=bytes/16;
  std::memcpy(&regs[XE_GPU_REG_SHADER_CONSTANT_000_X],&stream,16);
  render.Draw(regs,memory,vs,nullptr,PrimitiveType::kTriangleList,1,nullptr);
  render.Flush(false);
  // Change the extent again before the first large staging buffer retires.
  stream.index_count=1;
  std::memcpy(&regs[XE_GPU_REG_SHADER_CONSTANT_000_X],&stream,16);
  render.Draw(regs,memory,vs,nullptr,PrimitiveType::kTriangleList,1,nullptr);
  render.CompleteGuestWrites(kGuestSyncPacket,0);
  Require(!std::memcmp(memory.TranslateVirtual(allocation),&regs[XE_GPU_REG_SHADER_CONSTANT_000_X+8],16),"grown staging data");
  const auto* result=memory.TranslateVirtual<const uint8_t*>(allocation);
  for(uint32_t i=16;i<bytes;++i) Require(result[i]==0xCD,"grown staging padding or offsets");
  memory.SystemHeapFree(allocation);
  std::cout<<"PASS staging overflow/growth: 17 MiB checked byte-for-byte\n";
}
// Hold real compute creation while shader storage is initialized after Resolve.
// The cache merge must not race the compilation workers (Vulkan host contract).
struct ComputeGate {
  PFN_vkCreateComputePipelines original;
  std::mutex mutex;
  std::condition_variable cv;
  bool entered = false, release = false, timed_out = false;
  inline static ComputeGate* active = nullptr;
  static VKAPI_ATTR VkResult VKAPI_CALL Create(VkDevice device, VkPipelineCache cache,
      uint32_t count, const VkComputePipelineCreateInfo* info,
      const VkAllocationCallbacks* allocator, VkPipeline* pipelines) {
    auto& gate = *active;
    {
      std::unique_lock lock(gate.mutex);
      gate.entered = true;
      gate.cv.notify_all();
      if (!gate.cv.wait_for(lock, std::chrono::seconds(3), [&] { return gate.release; }))
        gate.timed_out = true;
    }
    return gate.original(device, cache, count, info, allocator, pipelines);
  }
};
void CheckCacheMerge(rex::ui::vulkan::VulkanProvider& provider, RegisterFile& regs,
    rex::memory::Memory& memory, const std::filesystem::path& output) {
  const auto cache = output / "late-cache";
  { VulkanRender seed(provider); seed.SetPipelineDirectory(cache); }
  Require(std::filesystem::file_size(cache / "pipelines.bin") >= 32, "seed driver cache");
  auto& functions = const_cast<rex::ui::vulkan::VulkanDevice::Functions&>(provider.vulkan_device()->functions());
  ComputeGate gate{functions.vkCreateComputePipelines};
  ComputeGate::active = &gate;
  functions.vkCreateComputePipelines = ComputeGate::Create;
  struct Restore {
    rex::ui::vulkan::VulkanDevice::Functions& functions; ComputeGate& gate;
    ~Restore() { functions.vkCreateComputePipelines = gate.original; ComputeGate::active = nullptr; }
  } restore{functions, gate};
  VulkanRender render(provider);
  render.Resolve(regs, memory);
  {
    std::unique_lock lock(gate.mutex);
    Require(gate.cv.wait_for(lock, std::chrono::seconds(2), [&] { return gate.entered; }), "compute worker entered");
  }
  auto merge = std::async(std::launch::async, [&] { render.SetPipelineDirectory(cache); });
  const bool waited = merge.wait_for(std::chrono::milliseconds(150)) == std::future_status::timeout;
  { std::lock_guard lock(gate.mutex); gate.release = true; }
  gate.cv.notify_all();
  merge.get();
  render.Flush(true);
  Require(!gate.timed_out && waited, "cache merge did not wait for active compute creation");
  std::cout << "PASS late cache merge waits for compute workers\n";
}
}
int main(int argc, char** argv) {
  try {
    CheckBufferDependencies();
    Require(argc >= 3, "usage: perf OUTPUT export|resolve|sync|none [iterations=4096] [scale=1] [instrument=1] [contracts] [pipeline_threads]");
    const std::filesystem::path out=argv[1]; std::filesystem::create_directories(out);
    const std::string mode=argv[2];
    const unsigned iterations=argc>3?std::stoul(argv[3]):4096;
    const unsigned scale=argc>4?std::stoul(argv[4]):1;
    const bool measured=argc<=5 || std::stoi(argv[5])!=0;
    Require(iterations>0,"positive iteration count");
    Require(mode=="export"||mode=="resolve"||mode=="sync"||mode=="none"||mode=="batch","unknown workload");
    rex::InitLogging((out/"runtime.log").string().c_str());
    if (argc > 7) rex::cvar::SetFlagByName("native_pipeline_threads", argv[7]);
    rex::cvar::SetFlagByName("native_vulkan_perf", measured?"true":"false");
    if (scale>1) {
      rex::chrono::FrameTiming::ActivateNative();
      rex::cvar::SetFlagByName("native_enhanced", "true");
      rex::cvar::SetFlagByName("native_scale_x", std::to_string(scale));
      rex::cvar::SetFlagByName("native_scale_y", std::to_string(scale));
    }
    rex::Runtime runtime(out, out/"user");
    rex::RuntimeConfig config; config.tool_mode=true; config.kernel_init=rex::kernel::InitializeKernel;
    Require(runtime.Setup(std::move(config))==0,"runtime setup");
    Require(GetNativeQualitySettings().scale_x==scale,"requested Enhanced scale is inactive");
    auto& memory=*runtime.memory();
    auto provider=rex::ui::vulkan::VulkanProvider::Create(true,true);
    Require(bool(provider),"Vulkan provider");
    std::cout<<"device="<<provider->vulkan_device()->properties().deviceName<<"\n";
    const auto output=memory.SystemHeapAlloc(1<<20,4096,rex::memory::kSystemHeapPhysical);
    const auto vertices=memory.SystemHeapAlloc(4096,4096,rex::memory::kSystemHeapPhysical);
    Require(output&&vertices,"physical allocations");
    const uint32_t physical=memory.GetPhysicalAddress(output);
    std::memset(memory.TranslateVirtual(output),0xCD,1<<20);
    {
      VulkanRender render(*provider);
      RegisterFile draw, resolve;
      reg::SQ_PROGRAM_CNTL program{}; program.vs_num_reg=2; program.ps_num_reg=1;
      draw[XE_GPU_REG_SQ_PROGRAM_CNTL]=program.value;
      draw[XE_GPU_REG_VGT_MAX_VTX_INDX]=0xFFFFFF;
      draw[XE_GPU_REG_RB_MODECONTROL]=uint32_t(EdramMode::kNoOperation);
      draw[XE_GPU_REG_SQ_VS_CONST]=0x000FF000; draw[XE_GPU_REG_SQ_PS_CONST]=0x000FF100;
      xe_gpu_memexport_stream_t stream{}; stream.base_address=physical>>2; stream.const_0x1=1;
      stream.const_0x4b000000=0x4B000000; stream.format=ColorFormat::k_32_32_32_32_FLOAT;
      stream.num_format=SurfaceNumberFormat::kFloat; stream.const_0x4b0=0x4B0;
      stream.index_count=1; stream.const_0x96=0x96;
      const std::array<float,4> data{.25f,.5f,.75f,1.f};
      std::memcpy(&draw[XE_GPU_REG_SHADER_CONSTANT_000_X],&stream,16);
      draw[XE_GPU_REG_SHADER_CONSTANT_000_X+5]=std::bit_cast<uint32_t>(1.f);
      std::memcpy(&draw[XE_GPU_REG_SHADER_CONSTANT_000_X+8],data.data(),16);
      auto shader=ExportShader();
      for(auto& word:shader) word=rex::byte_swap(word);
      auto* vs=render.LoadShader(ShaderType::kVertex,shader.data(),uint32_t(shader.size()));
      const float xy[]{0,0,32,0,0,32}; std::memcpy(memory.TranslateVirtual(vertices),xy,sizeof(xy));
      xe_gpu_vertex_fetch_t fetch{}; fetch.type=FetchConstantType::kVertex;
      fetch.address=memory.GetPhysicalAddress(vertices)>>2; fetch.size=6;
      resolve[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0]=fetch.dword_0;
      resolve[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_1]=fetch.dword_1;
      resolve[XE_GPU_REG_RB_MODECONTROL]=uint32_t(EdramMode::kCopy);
      resolve[XE_GPU_REG_RB_SURFACE_INFO]=80;
      resolve[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL]=0x80000000;
      resolve[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR]=32|(32<<16);
      resolve[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR]=32|(32<<16);
      resolve[XE_GPU_REG_RB_COPY_DEST_BASE]=physical+4096;
      resolve[XE_GPU_REG_RB_COPY_DEST_PITCH]=32|(32<<16);
      resolve[XE_GPU_REG_RB_COPY_DEST_INFO]=uint32_t(TextureFormat::k_8_8_8_8)<<7;
      reg::RB_COPY_CONTROL control{}; control.copy_command=CopyCommand(1);
      resolve[XE_GPU_REG_RB_COPY_CONTROL]=control.value;
      auto workload=[&](unsigned i) {
        if(mode=="resolve") {
          resolve[XE_GPU_REG_RB_COPY_DEST_BASE]=physical+4096+(i%8)*4096;
          render.Resolve(resolve,memory);
        } else if(mode!="none") {
          // Adjacent and overlapping writes, within and across submissions.
          draw[XE_GPU_REG_SHADER_CONSTANT_000_X]=((physical+(i%8)*16)>>2)|(1u<<30);
          render.Draw(draw,memory,vs,nullptr,PrimitiveType::kTriangleList,1,nullptr);
        } else render.SetSampleCounts({},false);
        if(mode!="batch") render.Flush(false);
        if(mode=="sync") render.SyncGuestRead(physical,128,0xFF);
      };
      for(unsigned i=0;i<64;++i) workload(i);
      render.Flush(true);
      auto& counters=NativeVulkanPerformance();
      const auto before=counters;
      const auto cpu=CpuNs();
      const auto start=std::chrono::steady_clock::now();
      std::vector<double> frames;
      for(unsigned i=0;i<iterations;++i) {
        const auto frame=std::chrono::steady_clock::now(); workload(i);
        frames.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-frame).count());
      }
      render.CompleteGuestWrites(kGuestSyncPacket,0);
      render.Flush(true);
      const auto wall=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
      const auto cpu_ns=CpuNs()-cpu;
      const auto video=VideoMemory(*provider->vulkan_device());
      if(mode=="export"||mode=="sync"||mode=="batch") for(unsigned i=0;i<8;++i)
        Require(!std::memcmp(memory.TranslatePhysical(physical+i*16),data.data(),16),"MEMEXPORT byte equality");
      if(mode=="resolve") for(unsigned i=0;i<8*4096;++i)
        Require(memory.TranslatePhysical<const uint8_t*>(physical+4096)[i]==0,"resolve byte equality");
      std::ofstream bytes(out/"bytes.bin",std::ios::binary);
      bytes.write(memory.TranslatePhysical<const char*>(physical),1<<20);
      std::sort(frames.begin(),frames.end());
      auto pct=[&](double q){return frames[std::min(frames.size()-1,size_t(std::ceil(q*frames.size()))-1)];};
      double low=0; const size_t n=std::max(size_t(1),(frames.size()+99)/100);
      for(size_t i=0;i<n;++i) low+=frames[frames.size()-1-i];
      std::ofstream json(out/"metrics.json");
      json<<"{\"mode\":\""<<mode<<"\",\"scale\":"<<scale<<",\"iterations\":"<<iterations
          <<",\"wall_ms\":"<<wall<<",\"cpu_ms\":"<<cpu_ns/1e6
          <<",\"p50_ms\":"<<pct(.50)<<",\"p95_ms\":"<<pct(.95)<<",\"p99_ms\":"<<pct(.99)
          <<",\"synthetic_1pct_low\":"<<1000*n/low
          <<",\"submissions\":"<<counters.submissions-before.submissions<<",\"waits\":"<<counters.waits-before.waits
          <<",\"blocked_waits\":"<<counters.blocked_waits-before.blocked_waits
          <<",\"wait_ms\":"<<(counters.wait_ns-before.wait_ns)/1e6
          <<",\"submit_ms\":"<<(counters.submit_ns-before.submit_ns)/1e6
          <<",\"gpu_ms\":"<<(counters.gpu_ns-before.gpu_ns)/1e6
          <<",\"gpu_samples\":"<<counters.gpu_samples-before.gpu_samples
          <<",\"readbacks\":"<<counters.readbacks-before.readbacks
          <<",\"readback_bytes\":"<<counters.readback_bytes-before.readback_bytes
          <<",\"buffers\":"<<counters.buffers-before.buffers<<",\"peak_buffer_bytes\":"<<counters.peak_bytes
          <<",\"vram_usage_bytes\":"<<video.CurrentUsage<<",\"vram_budget_bytes\":"<<video.Budget<<"}\n";
      std::cout<<"PASS "<<mode<<" iterations="<<iterations<<" wall_ms="<<wall<<" waits="<<counters.waits-before.waits<<"\n";
      if(argc>6 && std::string(argv[6])=="contracts") {
        CheckPendingContracts(render,draw,memory,vs,output,*provider->vulkan_device());
        CheckStagingGrowth(render,draw,memory,vs);
        CheckResolveContracts(render,resolve,memory,output);
        CheckTextureResolveDependency(render,draw,resolve,memory,output);
        if (argc <= 7 || std::stoi(argv[7]) != 0) CheckCacheMerge(*provider, resolve, memory, out);
      }
      if(argc>6 && std::string(argv[6])=="storage-contracts")
        CheckTextureStorageLimit(*provider, draw, memory, output);
      if(argc>6 && std::string(argv[6])=="index-contracts")
        CheckResidentIndices(render, draw, resolve, memory, output, *provider->vulkan_device());
    }
    Require(!measured||NativeVulkanPerformance().live_bytes==0,"Native buffers survived renderer destruction");
    if(argc>6) for(unsigned i=0;i<3;++i) {
      VulkanRender recreated(*provider); recreated.SetSampleCounts({},false); recreated.Flush(false);
    }
    Require(!measured||NativeVulkanPerformance().live_bytes==0,"Native buffers survived renderer recreation");
    memory.SystemHeapFree(output); memory.SystemHeapFree(vertices);
    runtime.Shutdown(); rex::FlushLogging();
    return 0;
  } catch(const std::exception& error) {std::cerr<<"FAIL "<<error.what()<<"\n";return 1;}
}
