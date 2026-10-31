// Real guest microcode -> Native GPU stores -> aliases -> guest readback.
// Prepared for focused validation; synthetic coverage is not gameplay acceptance.
#include "probe_main.h"
#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/kernel/init.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/mmio_handler.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/format/ucode.h>
#include <rex/cvar.h>
#include "../../src/native/memory/guest_memory_mirror.h"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string_view>
#include <thread>
#include <vector>
using namespace rex::graphics;
using namespace rex::graphics::xenos;
using rex::X_STATUS;
namespace {
void Require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
uint32_t Packet(uint32_t opcode, uint32_t count) { return 0xC0000000 | ((count - 1) << 16) | (opcode << 8); }
std::vector<uint32_t> ExportShader(bool vertex, bool dynamic = false, bool fetch = false) {
  using namespace ucode;
  const uint64_t alloc = (uint64_t(AllocType::kMemory) << 41) | (uint64_t(ControlFlowOpcode::kAlloc) << 44);
  const uint64_t exec = 1 | (uint64_t(fetch ? 3 : 2) << 12) | (uint64_t(fetch) << 16) |
      (uint64_t(ControlFlowOpcode::kExecEnd) << 44);
  std::vector<uint32_t> code{uint32_t(alloc), uint32_t(alloc >> 32) | uint32_t(exec << 16), uint32_t(exec >> 16)};
  if (fetch) code.insert(code.end(), {(1u << 12) | (1u << 19),
      0x688u | (1u << 13) | (uint32_t(VertexFormat::k_32_32_32_32_FLOAT) << 16), 4});
  // Standard eA = r0.x * (0,1,0,0) + stream; PS uses constant zero as index.
  // The dynamic variant copies the same valid address with MAX instead of MAD.
  code.insert(code.end(), {32 | (1u << 15) | (15u << 16), dynamic ? 0u : vertex ? (0x6Cu << 16) : 0u,
      dynamic ? uint32_t(AluVectorOpcode::kMax) << 24 :
      (1u << 8) | (vertex ? (1u << 31) : (3u << 16)) | (uint32_t(AluVectorOpcode::kMad) << 24)});
  code.insert(code.end(), {33 | (1u << 15) | (15u << 16), 0,
      (uint32_t(AluVectorOpcode::kMax) << 24) |
      (fetch ? (1u << 16) | (1u << 8) | (3u << 30) : (2u << 16) | (2u << 8))});
  return code;
}
std::vector<uint32_t> PositionShader() {
  using namespace ucode;
  const uint64_t alloc = (uint64_t(AllocType::kVsPosition) << 41) | (uint64_t(ControlFlowOpcode::kAlloc) << 44);
  const uint64_t exec = 1 | (uint64_t(2) << 12) | (uint64_t(1) << 16) | (uint64_t(ControlFlowOpcode::kExecEnd) << 44);
  return {uint32_t(alloc), uint32_t(alloc >> 32) | uint32_t(exec << 16), uint32_t(exec >> 16),
      (1u << 12) | (1u << 19), 0x688u | (1u << 13) | (uint32_t(VertexFormat::k_32_32_32_32_FLOAT) << 16), 4,
      62 | (1u << 15) | (15u << 16), 0,
      (uint32_t(AluVectorOpcode::kMax) << 24) | (1u << 16) | (1u << 8) | (3u << 30)};
}
void CheckReadbackEpoch(rex::memory::Memory& memory) {
  const auto page = memory.SystemHeapAlloc(4096, 4096, rex::memory::kSystemHeapPhysical);
  Require(page != 0, "epoch probe allocation");
  const auto physical = memory.GetPhysicalAddress(page);
  x360n::native::GuestMemoryMirror mirror(memory, [](uint32_t, uint32_t) {});
  mirror.RequestRange(physical, 4096);
  std::vector<uint64_t> before_cpu, after_cpu;
  mirror.RangeWrittenByGpu(physical, 4, &before_cpu);
  *memory.TranslateVirtual<uint32_t*>(page) = 0x12345678; // Real watched CPU write.
  mirror.RequestRange(physical + 16, 4); // A later draw revalidates this same page.
  mirror.RangeWrittenByGpu(physical + 16, 4, &after_cpu);
  const uint32_t old_result = 0x11223344, new_result = 0xAABBCCDD;
  mirror.CopyReadbackToGuest(physical, reinterpret_cast<const uint8_t*>(&old_result), 4, before_cpu);
  mirror.CopyReadbackToGuest(physical + 16, reinterpret_cast<const uint8_t*>(&new_result), 4, after_cpu);
  Require(*memory.TranslateVirtual<uint32_t*>(page) == 0x12345678, "old GPU snapshot overwrote newer CPU bytes");
  Require(*memory.TranslateVirtual<uint32_t*>(page + 16) == new_result, "current GPU snapshot was lost");
}
void Run(rex::system::IGraphicsSystem& gpu, rex::memory::Memory& memory) {
  CheckReadbackEpoch(memory);
  constexpr uint32_t allocation = 1 << 20;
  const auto ring = memory.SystemHeapAlloc(65536, 4096, rex::memory::kSystemHeapPhysical);
  const auto outputs = memory.SystemHeapAlloc(allocation, 4096, rex::memory::kSystemHeapPhysical);
  const auto vertices = memory.SystemHeapAlloc(4096, 4096, rex::memory::kSystemHeapPhysical);
  const auto done = memory.SystemHeapAlloc(4096, 4096, rex::memory::kSystemHeapPhysical);
  Require(ring && outputs && vertices && done, "MEMEXPORT probe allocations");
  std::memset(memory.TranslateVirtual(outputs), 0xCD, allocation);
  std::memset(memory.TranslateVirtual(done), 0, 4);
  const float triangle[]{-1, -1, .5f, 1, -1, 3, .5f, 1, 3, -1, .5f, 1};
  std::memcpy(memory.TranslateVirtual(vertices), triangle, sizeof(triangle));
  std::vector<uint32_t> words;
  auto set = [&](uint32_t index, uint32_t value) { words.insert(words.end(), {index, value}); };
  auto fp = [&](uint32_t index, float value) { set(index, std::bit_cast<uint32_t>(value)); };
  auto load = [&](bool vertex, const std::vector<uint32_t>& shader) {
    words.insert(words.end(), {Packet(PM4_IM_LOAD_IMMEDIATE, uint32_t(shader.size() + 2)), uint32_t(!vertex), uint32_t(shader.size())});
    words.insert(words.end(), shader.begin(), shader.end());
  };
  auto vertex_fetch = [&](uint32_t address, uint32_t dwords) {
    xe_gpu_vertex_fetch_t fetch{}; fetch.type = FetchConstantType::kVertex; fetch.address = address >> 2; fetch.size = dwords;
    set(XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0, fetch.dword_0); set(XE_GPU_REG_SHADER_CONSTANT_FETCH_00_1, fetch.dword_1);
  };
  reg::SQ_PROGRAM_CNTL program{}; program.vs_num_reg = 2; program.ps_num_reg = 1;
  set(XE_GPU_REG_SQ_PROGRAM_CNTL, program.value); set(XE_GPU_REG_VGT_MAX_VTX_INDX, 0xFFFFFF);
  set(XE_GPU_REG_RB_MODECONTROL, uint32_t(EdramMode::kNoOperation));
  set(XE_GPU_REG_SQ_VS_CONST, 0x000FF000); set(XE_GPU_REG_SQ_PS_CONST, 0x000FF100);
  struct Result { const char* name; uint32_t address; std::array<uint8_t, 64> bytes; };
  std::vector<Result> expected;
  auto issue = [&](const char* name, ColorFormat format, SurfaceNumberFormat numeric, const std::array<float, 4>& value,
                   uint32_t count, unsigned element_bytes, const std::vector<uint8_t>& bytes,
                   bool vertex = true, bool dynamic = false, bool fetch = false, Endian128 endian = Endian128::kNone) {
    Result result{name, memory.GetPhysicalAddress(outputs) + uint32_t(expected.size()) * 4096, {}};
    result.bytes.fill(0xCD); Require(bytes.size() == count * element_bytes && bytes.size() <= result.bytes.size(), "probe expectation extent");
    std::copy(bytes.begin(), bytes.end(), result.bytes.begin());
    xe_gpu_memexport_stream_t stream{}; stream.base_address = result.address >> 2; stream.const_0x1 = 1;
    stream.const_0x4b000000 = 0x4B000000; stream.format = format; stream.num_format = numeric;
    stream.const_0x4b0 = 0x4B0; stream.index_count = count; stream.const_0x96 = 0x96; stream.endianness = endian;
    const uint32_t constants = XE_GPU_REG_SHADER_CONSTANT_000_X + (vertex ? 0 : 256 * 4);
    for (unsigned c = 0; c < 4; ++c) {
      set(constants + c, reinterpret_cast<const uint32_t*>(&stream)[c]);
      fp(constants + 4 + c, c == 1 ? 1.0f : 0.0f); fp(constants + 8 + c, value[c]); fp(constants + 12 + c, 0.0f);
    }
    load(vertex, ExportShader(vertex, dynamic, fetch));
    reg::VGT_DRAW_INITIATOR draw{}; draw.prim_type = PrimitiveType::kTriangleList;
    draw.source_select = SourceSelect::kAutoIndex; draw.num_indices = vertex ? count : 3;
    words.insert(words.end(), {Packet(PM4_DRAW_INDX_2, 1), draw.value});
    expected.push_back(result);
    return result.address;
  };
  auto raw = [](const std::array<float, 4>& value, unsigned repeats = 1, bool swapped = false) {
    std::vector<uint8_t> bytes(repeats * 16);
    for (unsigned i = 0; i < repeats; ++i) for (unsigned c = 0; c < 4; ++c) {
      uint32_t bits = std::bit_cast<uint32_t>(value[c]); if (swapped) bits = rex::byte_swap(bits);
      std::memcpy(bytes.data() + i * 16 + c * 4, &bits, 4);
    }
    return bytes;
  };
  const std::array<float, 4> data{1.25f, -2.5f, 4.0f, .75f}, changed{9, 8, 7, 6};
  const auto source = issue("VS-only incomplete triangle", ColorFormat::k_32_32_32_32_FLOAT, SurfaceNumberFormat::kFloat, data, 2, 16, raw(data, 2));
  // A cache-register poll between a GPU store and its vertex-fetch consumer
  // does not publish memory to the CPU. GPU ordering must preserve the alias.
  set(XE_GPU_REG_COHER_STATUS_HOST, 0);
  words.insert(words.end(), {Packet(PM4_WAIT_REG_MEM, 5), 3, XE_GPU_REG_COHER_STATUS_HOST, 0, 0xffffffff, 0x100});
  vertex_fetch(source, 8);
  issue("GPU vertex alias", ColorFormat::k_32_32_32_32_FLOAT, SurfaceNumberFormat::kFloat, data, 2, 16, raw(data, 2), true, false, true);
  // The corresponding memory poll must publish the export before the shared
  // decoder reads it, otherwise this wait cannot observe the GPU's value.
  words.insert(words.end(), {Packet(PM4_WAIT_REG_MEM, 5), 0x13, source,
      std::bit_cast<uint32_t>(data[0]), 0xffffffff, 0x100});
  words.insert(words.end(), {Packet(PM4_MEM_WRITE, 9), source});
  for (unsigned i = 0; i < 2; ++i) for (float value : changed) words.push_back(std::bit_cast<uint32_t>(value));
  // MEM_WRITE legitimately replaces the first export's guest-visible bytes.
  auto changed_bytes = raw(changed, 2); std::copy(changed_bytes.begin(), changed_bytes.end(), expected[0].bytes.begin());
  issue("PM4 updates resident alias", ColorFormat::k_32_32_32_32_FLOAT, SurfaceNumberFormat::kFloat, changed, 2, 16, raw(changed, 2), true, false, true);
  issue("float endianness", ColorFormat::k_32_32_32_32_FLOAT, SurfaceNumberFormat::kFloat, data, 2, 16, raw(data, 2, true), true, false, false, Endian128::k8in32);
  for (unsigned bits : {8u, 16u}) for (bool normalized : {false, true}) {
    for (float value : {-40000.0f, -1.6f, -1.5f, -1.4f, -.6f, -.5f, -.4f, .4f, .5f, .6f, 1.4f, 1.5f, 1.6f, 40000.0f}) {
      const float limit = float((1u << (bits - 1)) - 1);
      const float number = normalized ? std::clamp(value, -1.0f, 1.0f) * limit : std::clamp(value, -limit - 1, limit);
      const uint32_t packed = uint32_t(int32_t(std::round(number)));
      std::vector<uint8_t> bytes(bits / 8); for (unsigned b = 0; b < bytes.size(); ++b) bytes[b] = uint8_t(packed >> (b * 8));
      issue(normalized ? "signed normalized packing" : "signed integer packing", bits == 8 ? ColorFormat::k_8 : ColorFormat::k_16,
          normalized ? SurfaceNumberFormat::kSignedRepeatingFraction : SurfaceNumberFormat::kSignedInteger,
          {value, value, value, value}, 1, bits / 8, bytes);
    }
  }
  for (unsigned bits : {8u,16u}) {
    for (float value : {-70000.0f,-1.0f,0.0f,.4f,.5f,.6f,1.5f,254.5f,255.0f,256.0f,65535.0f,70000.0f}) {
      const uint32_t packed=uint32_t(std::round(std::clamp(value,0.0f,float((1u<<bits)-1))));
      std::vector<uint8_t> bytes(bits/8);
      for (unsigned b=0;b<bytes.size();++b) bytes[b]=uint8_t(packed>>(b*8));
      issue("unsigned integer saturation/rounding",bits==8 ? ColorFormat::k_8 : ColorFormat::k_16,
          SurfaceNumberFormat::kUnsignedInteger,{value,value,value,value},1,bits/8,bytes);
    }
  }
  for (bool normalized : {false, true}) {
    const std::array<float, 4> value = normalized ? std::array<float, 4>{-.4f, -.5f, .5f, .4f} : std::array<float, 4>{-1.4f, -1.5f, -1.6f, 2.5f};
    std::vector<uint8_t> bytes(8);
    for (unsigned c = 0; c < 4; ++c) {
      const uint16_t packed = uint16_t(int16_t(std::round(normalized ? value[c] * 32767.0f : value[c])));
      bytes[c * 2] = uint8_t(packed); bytes[c * 2 + 1] = uint8_t(packed >> 8);
    }
    issue("signed 16x4 packing", ColorFormat::k_16_16_16_16,
        normalized ? SurfaceNumberFormat::kSignedRepeatingFraction : SurfaceNumberFormat::kSignedInteger,
        value, 1, 8, bytes);
  }
  // Pixel side effects remain live without any color writes.
  set(XE_GPU_REG_RB_MODECONTROL, uint32_t(EdramMode::kColorDepth)); set(XE_GPU_REG_RB_SURFACE_INFO, 80);
  set(XE_GPU_REG_RB_COLOR_MASK, 0); set(XE_GPU_REG_RB_DEPTHCONTROL, 0); set(XE_GPU_REG_PA_CL_VTE_CNTL, 0x3F);
  fp(XE_GPU_REG_PA_CL_VPORT_XSCALE, 8); fp(XE_GPU_REG_PA_CL_VPORT_XOFFSET, 8);
  fp(XE_GPU_REG_PA_CL_VPORT_YSCALE, -8); fp(XE_GPU_REG_PA_CL_VPORT_YOFFSET, 8);
  fp(XE_GPU_REG_PA_CL_VPORT_ZSCALE, 1); fp(XE_GPU_REG_PA_CL_VPORT_ZOFFSET, 0);
  set(XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL, 0); set(XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR, 16 | (16 << 16));
  set(XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL, 0x80000000); set(XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR, 16 | (16 << 16));
  vertex_fetch(memory.GetPhysicalAddress(vertices), 12); load(true, PositionShader());
  issue("pixel MEMEXPORT without color", ColorFormat::k_32_32_32_32_FLOAT, SurfaceNumberFormat::kFloat, data, 1, 16, raw(data), false);
  set(XE_GPU_REG_RB_MODECONTROL, uint32_t(EdramMode::kNoOperation));
  issue("computed export address", ColorFormat::k_32_32_32_32_FLOAT, SurfaceNumberFormat::kFloat, data, 1, 16, raw(data), true, true);
  words.insert(words.end(), {Packet(PM4_MEM_WRITE, 2), memory.GetPhysicalAddress(done), 1});
  Require(words.size() * 4 < 65536, "probe command capacity");
  for (size_t i = 0; i < words.size(); ++i) rex::memory::store_and_swap<uint32_t>(memory.TranslateVirtual(ring + uint32_t(i * 4)), words[i]);
  gpu.InitializeRingBuffer(memory.GetPhysicalAddress(ring), 13);
  auto* mmio = memory.LookupVirtualMappedRange(0x7FC80714); Require(mmio != nullptr, "probe GPU doorbell");
  mmio->write(nullptr, mmio->callback_context, 0x7FC80714, uint32_t(words.size()));
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
  auto* marker = memory.TranslateVirtual<volatile uint32_t*>(done);
  while (*marker != 1 && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  Require(*marker == 1, "MEMEXPORT completion timeout");
  bool passed = true;
  for (const auto& result : expected) {
    const bool equal = std::memcmp(memory.TranslatePhysical(result.address), result.bytes.data(), result.bytes.size()) == 0;
    std::cout << result.name << ' ' << (equal ? "PASS" : "FAIL") << '\n'; passed &= equal;
  }
  Require(passed, "GPU MEMEXPORT bytes differ from contract");
}
}
X360N_PROBE_MAIN {
  try {
    Require(argc == 2 || argc == 3, "usage: x360n_native_memexport_probe OUTPUT [d3d12|vulkan]");
    const ProbeArg api = argc == 3 ? argv[2] : X360N_PROBE_ARG("d3d12");
    Require(api == X360N_PROBE_ARG("d3d12") || api == X360N_PROBE_ARG("vulkan"), "invalid graphics API");
    const std::filesystem::path output(argv[1]); std::filesystem::create_directories(output);
    rex::InitLogging((output / "runtime.log").string().c_str());
    rex::Runtime runtime(output, output / "user");
    rex::RuntimeConfig config; config.tool_mode = true; config.kernel_init = rex::kernel::InitializeKernel;
    Require(runtime.Setup(std::move(config)) == X_STATUS_SUCCESS, "runtime setup");
    if (api == X360N_PROBE_ARG("d3d12")) rex::cvar::SetFlagByName("d3d12_debug", "true");
    auto gpu = rex::system::LoadGpuPlugin("native", api == X360N_PROBE_ARG("d3d12") ? "d3d12" : "vulkan"); Require(bool(gpu), "Native plugin load");
    Require(gpu->SetupPresentation(nullptr) == X_STATUS_SUCCESS, "presentation setup");
    Require(gpu->SetupGuestGpu(runtime.function_dispatcher(), runtime.kernel_state()) == X_STATUS_SUCCESS, "guest GPU setup");
    struct Shutdown { rex::system::IGraphicsSystem* gpu; ~Shutdown() { if (gpu) gpu->Shutdown(); } } shutdown{gpu.get()};
    Run(*gpu, *runtime.memory()); gpu->Shutdown(); shutdown.gpu = nullptr; gpu.reset(); runtime.Shutdown(); rex::FlushLogging();
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
