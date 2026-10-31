// Cold pipeline workload of the real Native plugin: N draws in one command
// stream, each with its own vertex shader (a different constant register), so
// each needs its own host pipeline. Every draw exports its constant through
// MEMEXPORT to its own address; all bytes are verified after completion.
// Prints the time from the doorbell to the completion marker. Synthetic: it
// measures pipeline creation, recording and submission, not a game frame rate.
#include "probe_main.h"
#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/kernel/init.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/mmio_handler.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/format/ucode.h>
#include <rex/cvar.h>
#include <array>
#include <bit>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
using namespace rex::graphics;
using namespace rex::graphics::xenos;
using rex::X_STATUS;
namespace {
void Require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
uint32_t Packet(uint32_t opcode, uint32_t count) { return 0xC0000000 | ((count - 1) << 16) | (opcode << 8); }
// eA = r0.x * c1 + c0 (stream constant c0), eM0 = max(c[value], c[value]).
std::vector<uint32_t> ExportShader(uint32_t value) {
  using namespace ucode;
  const uint64_t alloc = (uint64_t(AllocType::kMemory) << 41) | (uint64_t(ControlFlowOpcode::kAlloc) << 44);
  const uint64_t exec = 1 | (uint64_t(2) << 12) | (uint64_t(ControlFlowOpcode::kExecEnd) << 44);
  return {uint32_t(alloc), uint32_t(alloc >> 32) | uint32_t(exec << 16), uint32_t(exec >> 16),
      32 | (1u << 15) | (15u << 16), 0x6Cu << 16,
      (1u << 8) | (1u << 31) | (uint32_t(AluVectorOpcode::kMad) << 24),
      33 | (1u << 15) | (15u << 16), 0,
      (uint32_t(AluVectorOpcode::kMax) << 24) | (value << 16) | (value << 8)};
}
void Run(rex::system::IGraphicsSystem& gpu, rex::memory::Memory& memory, uint32_t draws) {
  const auto ring = memory.SystemHeapAlloc(1 << 20, 4096, rex::memory::kSystemHeapPhysical);
  const auto outputs = memory.SystemHeapAlloc(draws * 256, 4096, rex::memory::kSystemHeapPhysical);
  const auto done = memory.SystemHeapAlloc(4096, 4096, rex::memory::kSystemHeapPhysical);
  Require(ring && outputs && done, "pipeline probe allocations");
  std::memset(memory.TranslateVirtual(outputs), 0xCD, draws * 256);
  std::memset(memory.TranslateVirtual(done), 0, 4);
  std::vector<uint32_t> words;
  auto set = [&](uint32_t index, uint32_t value) { words.insert(words.end(), {index, value}); };
  auto fp = [&](uint32_t index, float value) { set(index, std::bit_cast<uint32_t>(value)); };
  reg::SQ_PROGRAM_CNTL program{}; program.vs_num_reg = 2; program.ps_num_reg = 1;
  set(XE_GPU_REG_SQ_PROGRAM_CNTL, program.value); set(XE_GPU_REG_VGT_MAX_VTX_INDX, 0xFFFFFF);
  set(XE_GPU_REG_RB_MODECONTROL, uint32_t(EdramMode::kNoOperation));
  set(XE_GPU_REG_SQ_VS_CONST, 0x000FF000); set(XE_GPU_REG_SQ_PS_CONST, 0x000FF100);
  for (unsigned c = 0; c < 4; ++c) fp(XE_GPU_REG_SHADER_CONSTANT_000_X + 4 + c, c == 1 ? 1.0f : 0.0f);
  for (uint32_t i = 0; i < draws; ++i) {
    const uint32_t value = 2 + i;
    for (unsigned c = 0; c < 4; ++c) fp(XE_GPU_REG_SHADER_CONSTANT_000_X + value * 4 + c, float(i * 4 + c));
  }
  for (uint32_t i = 0; i < draws; ++i) {
    xe_gpu_memexport_stream_t stream{};
    stream.base_address = (memory.GetPhysicalAddress(outputs) + i * 256) >> 2; stream.const_0x1 = 1;
    stream.const_0x4b000000 = 0x4B000000; stream.format = ColorFormat::k_32_32_32_32_FLOAT;
    stream.num_format = SurfaceNumberFormat::kFloat; stream.const_0x4b0 = 0x4B0; stream.index_count = 1;
    stream.const_0x96 = 0x96; stream.endianness = Endian128::kNone;
    for (unsigned c = 0; c < 4; ++c) set(XE_GPU_REG_SHADER_CONSTANT_000_X + c, reinterpret_cast<const uint32_t*>(&stream)[c]);
    const auto shader = ExportShader(2 + i);
    words.insert(words.end(), {Packet(PM4_IM_LOAD_IMMEDIATE, uint32_t(shader.size() + 2)), 0u, uint32_t(shader.size())});
    words.insert(words.end(), shader.begin(), shader.end());
    reg::VGT_DRAW_INITIATOR draw{}; draw.prim_type = PrimitiveType::kTriangleList;
    draw.source_select = SourceSelect::kAutoIndex; draw.num_indices = 1;
    words.insert(words.end(), {Packet(PM4_DRAW_INDX_2, 1), draw.value});
  }
  words.insert(words.end(), {Packet(PM4_MEM_WRITE, 2), memory.GetPhysicalAddress(done), 1});
  Require(words.size() * 4 < (1u << 20), "probe command capacity");
  for (size_t i = 0; i < words.size(); ++i) rex::memory::store_and_swap<uint32_t>(memory.TranslateVirtual(ring + uint32_t(i * 4)), words[i]);
  gpu.InitializeRingBuffer(memory.GetPhysicalAddress(ring), 18);
  auto* mmio = memory.LookupVirtualMappedRange(0x7FC80714); Require(mmio != nullptr, "probe GPU doorbell");
  const auto start = std::chrono::steady_clock::now();
  mmio->write(nullptr, mmio->callback_context, 0x7FC80714, uint32_t(words.size()));
  auto* marker = memory.TranslateVirtual<volatile uint32_t*>(done);
  while (*marker != 1 && std::chrono::steady_clock::now() < start + std::chrono::seconds(300))
    std::this_thread::sleep_for(std::chrono::microseconds(200));
  const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  Require(*marker == 1, "pipeline probe completion timeout");
  for (uint32_t i = 0; i < draws; ++i) {
    std::array<float, 4> expected{}, actual{};
    for (unsigned c = 0; c < 4; ++c) expected[c] = float(i * 4 + c);
    std::memcpy(actual.data(), memory.TranslatePhysical(memory.GetPhysicalAddress(outputs) + i * 256), sizeof(actual));
    Require(actual == expected, "pipeline probe export differs");
  }
  std::cout << "PASS draws=" << draws << " elapsed_ms=" << elapsed << '\n';
}
}
X360N_PROBE_MAIN {
  try {
    Require(argc >= 3, "usage: x360n_native_pipeline_probe OUTPUT d3d12|vulkan [DRAWS] [cvar=value...]");
    const ProbeArg api = argv[2];
    Require(api == X360N_PROBE_ARG("d3d12") || api == X360N_PROBE_ARG("vulkan"), "invalid graphics API");
    const uint32_t draws = argc >= 4 ? uint32_t(std::stoul(std::filesystem::path(argv[3]).string())) : 64;
    Require(draws >= 1 && draws <= 250, "draws must be in [1, 250] (one VS constant each)");
    const std::filesystem::path output(argv[1]); std::filesystem::create_directories(output);
    rex::InitLogging((output / "runtime.log").string().c_str());
    rex::Runtime runtime(output, output / "user");
    rex::RuntimeConfig config; config.tool_mode = true; config.kernel_init = rex::kernel::InitializeKernel;
    Require(runtime.Setup(std::move(config)) == X_STATUS_SUCCESS, "runtime setup");
    auto gpu = rex::system::LoadGpuPlugin("native", api == X360N_PROBE_ARG("d3d12") ? "d3d12" : "vulkan");
    Require(bool(gpu), "Native plugin load");
    // Plugin cvars exist once the plugin is loaded; the renderer reads them
    // when the guest GPU is set up.
    for (int i = 4; i < argc; ++i) {
      const auto assignment = std::filesystem::path(argv[i]).string();
      const auto equals = assignment.find('=');
      Require(equals != std::string::npos && rex::cvar::SetFlagByName(assignment.substr(0, equals), assignment.substr(equals + 1)),
              "invalid cvar assignment");
      std::cout << "cvar " << assignment << '\n';
    }
    Require(gpu->SetupPresentation(nullptr) == X_STATUS_SUCCESS, "presentation setup");
    Require(gpu->SetupGuestGpu(runtime.function_dispatcher(), runtime.kernel_state()) == X_STATUS_SUCCESS, "guest GPU setup");
    struct Shutdown { rex::system::IGraphicsSystem* gpu; ~Shutdown() { if (gpu) gpu->Shutdown(); } } shutdown{gpu.get()};
    Run(*gpu, *runtime.memory(), draws);
    gpu->Shutdown(); shutdown.gpu = nullptr; gpu.reset(); runtime.Shutdown(); rex::FlushLogging();
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
