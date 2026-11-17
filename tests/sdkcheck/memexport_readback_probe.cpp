// 0246: real Xenos shaders and GPU before/after snapshots. The delayed path
// must preserve CPU writes to stream bytes that the shader never exported.
#include "../native/probe_main.h"
#include "../../src/gpu_probe_app.h"
#include "../tools/tool_sdk.h"
#include <rex/xenos/register.h>
#include <rex/xenos/packet.h>
#include <rex/xenos/ucode.h>
#include "gpu/xenos/xenos_graphics_driver_base.h"
#if X360N_PROBE_HAS_VULKAN
#include "gpu/xenos/host/vulkan/vulkan_provider.h"
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

using namespace rex::gpu;
using namespace rex::xenos;

namespace {
void Require(bool value, const char* what) {
  if (!value) throw std::runtime_error(what);
}
uint32_t Packet(uint32_t opcode, uint32_t count) {
  return 0xC0000000 | ((count - 1) << 16) | (opcode << 8);
}
std::vector<uint32_t> ExportShader() {
  using namespace ucode;
  const uint64_t alloc = (uint64_t(AllocType::kMemory) << 41) |
      (uint64_t(ControlFlowOpcode::kAlloc) << 44);
  const uint64_t exec = 1 | (uint64_t(2) << 12) |
      (uint64_t(ControlFlowOpcode::kExecEnd) << 44);
  return {uint32_t(alloc), uint32_t(alloc >> 32) | uint32_t(exec << 16), uint32_t(exec >> 16),
      // eA = vertex index * (0,1,0,0) + stream descriptor c0.
      32 | (1u << 15) | (15u << 16), 0x6Cu << 16,
      (1u << 8) | (1u << 31) | (uint32_t(AluVectorOpcode::kMad) << 24),
      // eM0 = c2.
      33 | (1u << 15) | (15u << 16), 0,
      (uint32_t(AluVectorOpcode::kMax) << 24) | (2u << 16) | (2u << 8)};
}

#if X360N_PROBE_HAS_VULKAN
// Publish a CPU write after real GPU submission but before its CPU readback.
// No production test hook and no fake Vulkan results: call the original driver.
struct ConcurrentCpuWrite {
  inline static PFN_vkQueueSubmit original = nullptr;
  inline static PFN_vkCreateBuffer original_create = nullptr;
  inline static std::atomic<uint8_t*> pending{nullptr};
  inline static std::atomic<uint32_t> calls{0};
  inline static std::atomic<uint32_t> readback_allocations{0};
  static VKAPI_ATTR VkResult VKAPI_CALL CreateBuffer(VkDevice device,
      const VkBufferCreateInfo* info, const VkAllocationCallbacks* allocator, VkBuffer* buffer) {
    const auto result = original_create(device, info, allocator, buffer);
    if (result == VK_SUCCESS && info->usage == VK_BUFFER_USAGE_TRANSFER_DST_BIT) {
      readback_allocations.fetch_add(1, std::memory_order_relaxed);
    }
    return result;
  }
  static VKAPI_ATTR VkResult VKAPI_CALL Submit(VkQueue queue, uint32_t count,
      const VkSubmitInfo* info, VkFence fence) {
    const auto result = original(queue, count, info, fence);
    if (auto* bytes = pending.exchange(nullptr, std::memory_order_acq_rel)) {
      std::memset(bytes, 0xA5, 32);
      calls.fetch_add(1, std::memory_order_release);
    }
    return result;
  }
  static void Install(XenosGraphicsDriverBase& gpu) {
    auto& device = *static_cast<rex::gpu::vulkan::VulkanProvider*>(gpu.GetProvider())->GetVulkanDevice();
    auto& functions = const_cast<rex::gpu::vulkan::VulkanDevice::Functions&>(device.GetFunctions());
    original = functions.vkQueueSubmit;
    original_create = functions.vkCreateBuffer;
    functions.vkQueueSubmit = Submit;
    functions.vkCreateBuffer = CreateBuffer;
  }
};
#endif

void Run(XenosGraphicsDriverBase& gpu, rex::kernel::GuestAddressSpace& memory,
         bool fast, bool concurrent) {
  const auto ring = memory.SystemHeapAlloc(65536, 4096, rex::kernel::kSystemHeapPhysical);
  const auto outputs = memory.SystemHeapAlloc(4096, 4096, rex::kernel::kSystemHeapPhysical);
  const auto done = memory.SystemHeapAlloc(4096, 4096, rex::kernel::kSystemHeapPhysical);
  Require(ring && outputs && done, "probe allocations");
  std::memset(memory.TranslateVirtual(outputs), 0xCD, 4096);
  std::memset(memory.TranslateVirtual(done), 0, 4);
  auto* output_base = memory.TranslateVirtual<uint8_t*>(outputs);
  auto* marker = memory.TranslateVirtual<volatile uint32_t*>(done);
  gpu.InitializeRingBuffer(memory.GetPhysicalAddress(ring), 13);
  uint32_t cursor = 0;
  uint32_t first_draw_readback_allocations = 0;
  const std::array<float, 4> data{1.25f, -2.5f, 4.0f, .75f};
  for (unsigned iteration = 0; iteration < 8; ++iteration) {
    // Synchronous draws use distinct streams. They must share one staging
    // allocation, while delayed draws exercise both slots of the same stream.
    const uint32_t output_offset = fast ? 0 : iteration * 256;
    auto* output = output_base + output_offset;
    std::vector<uint32_t> words;
    auto set = [&](uint32_t index, uint32_t value) { words.insert(words.end(), {index, value}); };
    auto fp = [&](uint32_t index, float value) { set(index, std::bit_cast<uint32_t>(value)); };
    reg::SQ_PROGRAM_CNTL program{}; program.vs_num_reg = 2; program.ps_num_reg = 1;
    set(uint32_t(Register::SQ_PROGRAM_CNTL), program.value);
    set(uint32_t(Register::VGT_MAX_VTX_INDX), 0xFFFFFF);
    set(uint32_t(Register::RB_MODECONTROL), uint32_t(EDRAMMode::kNoOperation));
    set(uint32_t(Register::SQ_VS_CONST), 0x000FF000);
    set(uint32_t(Register::SQ_PS_CONST), 0x000FF100);
    MemExportStream stream{};
    stream.base_address = memory.GetPhysicalAddress(outputs + output_offset) >> 2;
    stream.const_0x1 = 1; stream.const_0x4b000000 = 0x4B000000;
    stream.format = ColorFormat::k_32_32_32_32_FLOAT;
    stream.num_format = SurfaceNumberFormat::kFloat;
    stream.const_0x4b0 = 0x4B0; stream.index_count = 16; stream.const_0x96 = 0x96;
    for (unsigned c = 0; c < 4; ++c) {
      set(uint32_t(Register::SHADER_CONSTANT_000_X) + c, reinterpret_cast<const uint32_t*>(&stream)[c]);
      fp(uint32_t(Register::SHADER_CONSTANT_000_X) + 4 + c, c == 1 ? 1.0f : 0.0f);
      fp(uint32_t(Register::SHADER_CONSTANT_000_X) + 8 + c, data[c]);
    }
    const auto shader = ExportShader();
    words.insert(words.end(), {Packet(PM4_IM_LOAD_IMMEDIATE, uint32_t(shader.size() + 2)),
                               0, uint32_t(shader.size())});
    words.insert(words.end(), shader.begin(), shader.end());
    reg::VGT_DRAW_INITIATOR draw{};
    draw.prim_type = PrimitiveType::kTriangleList;
    draw.source_select = SourceSelect::kAutoIndex;
    draw.num_indices = 3;
    words.insert(words.end(), {Packet(PM4_DRAW_INDX_2, 1), draw.value});
    words.insert(words.end(), {Packet(PM4_MEM_WRITE, 2), memory.GetPhysicalAddress(done), iteration + 1});
    Require((cursor + words.size()) * 4 <= 65536, "ring capacity");
    for (auto word : words) {
      rex::memory::store_and_swap<uint32_t>(memory.TranslateVirtual(ring + cursor * 4), word);
      ++cursor;
    }
    if (iteration) std::memset(output + 128, 0xA5, 32);
#if X360N_PROBE_HAS_VULKAN
    if (concurrent && iteration == 0) ConcurrentCpuWrite::pending.store(output + 128);
#else
    Require(!concurrent, "concurrent fixture requires Vulkan");
#endif
    gpu.WriteRegister(uint32_t(Register::CP_RB_WPTR),cursor);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (*marker != iteration + 1 && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Require(*marker == iteration + 1, "MEMEXPORT draw completion timeout");
#if X360N_PROBE_HAS_VULKAN
    if (!iteration && concurrent && !fast) {
      // Includes lazily created occlusion-query staging. Subsequent streams
      // must reuse the MEMEXPORT staging allocated for the first draw.
      first_draw_readback_allocations = ConcurrentCpuWrite::readback_allocations.load();
    }
#endif
    for (unsigned vertex = 0; vertex < 3; ++vertex) {
      Require(!std::memcmp(output + vertex * 16, data.data(), 16), "GPU exported values differ");
    }
    if (iteration || concurrent) {
      const bool preserved = std::all_of(output + 128, output + 160,
                                         [](uint8_t value) { return value == 0xA5; });
      std::cout << "iteration=" << iteration << " fast=" << fast
                << " untouched CPU bytes=" << (preserved ? "PASS" : "FAIL") << '\n';
      Require(preserved, "MEMEXPORT readback overwrote CPU bytes not exported by the shader");
    }
  }
#if X360N_PROBE_HAS_VULKAN
  if (concurrent) Require(ConcurrentCpuWrite::calls.load() == 1, "concurrent CPU fixture did not run");
  if (concurrent && !fast) {
    std::cout << "readback allocations first=" << first_draw_readback_allocations
              << " final=" << ConcurrentCpuWrite::readback_allocations.load() << '\n';
    Require(first_draw_readback_allocations != 0 &&
            ConcurrentCpuWrite::readback_allocations.load() == first_draw_readback_allocations,
            "synchronous readback retained separate allocations for distinct streams");
  }
#endif
}
}  // namespace

X360N_PROBE_MAIN {
  try {
    Require(argc == 4 || argc == 5,
            "usage: memexport_readback_probe OUTPUT d3d12|vulkan fast|full [concurrent]");
    const bool vulkan = ProbeArg(argv[2]) == X360N_PROBE_ARG("vulkan");
    Require(vulkan || ProbeArg(argv[2]) == X360N_PROBE_ARG("d3d12"), "invalid API");
    const bool fast = ProbeArg(argv[3]) == X360N_PROBE_ARG("fast");
    Require(fast || ProbeArg(argv[3]) == X360N_PROBE_ARG("full"), "invalid readback mode");
    const bool concurrent = argc == 5 && ProbeArg(argv[4]) == X360N_PROBE_ARG("concurrent");
    Require(argc == 4 || concurrent, "invalid concurrency mode");
    Require(!concurrent || vulkan, "concurrent fixture requires Vulkan");
    const std::filesystem::path output(argv[1]);
    std::filesystem::create_directories(output);
    std::vector<std::string> options{
      "--gpu.xenos.async_shader_compilation=false","--gpu.xenos.readback_memexport=true",
      std::string("--gpu.xenos.readback_memexport_fast=")+(fast?"true":"false")};
    if(!vulkan)options.push_back("--gpu.d3d12.debug=true");
    const std::string api=vulkan?"vulkan":"d3d12";
    x360n::GPUProbeApp app(output,"rexgpu-xenos-"+api,api);
    const auto initialized=app.Init(rex::CLIArgList::Parse(options));
    if(!initialized)throw std::runtime_error(initialized.error().GetDescription());
    auto& graphics=static_cast<rex::gpu::IGraphicsDriver&>(app.GetProbeDriver());
    auto* gpu=static_cast<XenosGraphicsDriverBase*>(graphics.AsXenos());
    Require(gpu && app.GetProbeDriver().GetName()==api,"exact Xenos driver selection");
    Require(bool(graphics.SetSurface({})),"presentation setup");
    rex::kernel::TitleRuntime runtime(rex::ppc::PPCImage{});
    Require(bool(runtime.GetTitlePaths().game_root.Set(output)) &&
        bool(runtime.GetTitlePaths().entry.Set(std::string{})) && bool(runtime.Load()),"runtime setup");
    Require(bool(graphics.AttachTitle(rex::TitleInfo{},runtime.GetPhysicalMemory(),
        runtime.GetPhysicalWriteWatch(),runtime.GetGuestInterrupts())),"guest GPU setup");
    rex::ScopeGuard detach([&]{graphics.DetachTitle();});
#if X360N_PROBE_HAS_VULKAN
    if(concurrent)ConcurrentCpuWrite::Install(*gpu);
#endif
    Run(*gpu,rex::kernel::GetAddressSpace(runtime),fast,concurrent);
    std::cout << "PASS real GPU MEMEXPORT snapshots preserve unwritten CPU bytes\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
