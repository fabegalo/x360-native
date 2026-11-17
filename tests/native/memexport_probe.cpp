// Real guest microcode -> Native GPU stores -> aliases -> guest readback.
// Prepared for focused validation; synthetic coverage is not gameplay acceptance.
#include "probe_main.h"
#include "../sdkcheck/scalar_precision_oracle.h"
using x360n::test::ScalarExpected;
#include "../../src/gpu_probe_app.h"
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/title_paths.h>
#include <rex/kernel/heap.h>
#include <rex/ppc/image.h>
#include <rex/string/text.h>
#include <rex/gpu/xenos_graphics_driver.h>
#include <rex/xenos/register.h>
#include <rex/xenos/packet.h>
#include <rex/xenos/ucode.h>
#include <rex/xenos/fetch.h>
#include <rex/memory.h>
#include "../../src/native/memory/guest_memory_mirror.h"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <string_view>
#include <thread>
#include <vector>
using namespace rex::xenos;
namespace xenos=rex::xenos;
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
std::vector<uint32_t> ScalarExportShader(ucode::AluScalarOpcode opcode) {
  using namespace ucode;
  auto code = ExportShader(false);
  const bool mulsc = opcode == AluScalarOpcode::kMulsc0;
  if (mulsc) {
    const uint64_t exec = 1 | (uint64_t(3) << 12) |
                          (uint64_t(ControlFlowOpcode::kExecEnd) << 44);
    code[1] = (code[1] & 0xFFFFu) | uint32_t(exec << 16);
    code[2] = uint32_t(exec >> 16);
    // r0 = c3 supplies mulsc's temporary operand.
    code.insert(code.begin() + 3, {15u << 16, 0,
        (uint32_t(AluVectorOpcode::kMax) << 24) | (3u << 16) | (3u << 8)});
  }
  const size_t address = mulsc ? 6 : 3;
  // c4 is zero, independently of c3's mulsc operand.
  code[address + 2] = (4u << 16) | (1u << 8) | (uint32_t(AluVectorOpcode::kMad) << 24);
  // eM0.xyzw = scalar(c2.w[, r0.x]).
  code[code.size() - 3] = 33 | (1u << 15) | (15u << 20) | (uint32_t(opcode) << 26);
  code[code.size() - 2] = 0;
  code[code.size() - 1] = 2 | (uint32_t(AluVectorOpcode::kMax) << 24);
  return code;
}
std::vector<uint32_t> TextureExportShader(FetchOpDimension dimension, bool unnormalized,
                                         bool two_coordinates, uint32_t texture_index) {
  using namespace ucode;
  auto code = ExportShader(false);
  const uint64_t exec = 1 | (uint64_t(4) << 12) | (uint64_t(1) << 18) |
                        (uint64_t(ControlFlowOpcode::kExecEnd) << 44);
  code[1] = (code[1] & 0xFFFFu) | uint32_t(exec << 16);
  code[2] = uint32_t(exec >> 16);
  code.insert(code.begin() + 3, {
      15u << 16, 0, (uint32_t(AluVectorOpcode::kMax) << 24) | (2u << 16) | (2u << 8),
      uint32_t(FetchOpcode::kTextureFetch) | (texture_index << 20) | (uint32_t(unnormalized) << 25) |
          ((dimension == FetchOpDimension::k1D && !two_coordinates ? 0u : 0x24u) << 26),
      0x688u | (7u << 18) | (3u << 24) | (3u << 26), uint32_t(dimension) << 14});
  // eM0 = sampled r0, while eA still uses the independently zero c3.
  code.back() = (uint32_t(AluVectorOpcode::kMax) << 24) | (3u << 30);
  return code;
}
void CheckReadbackEpoch(rex::kernel::GuestAddressSpace& memory, rex::IPhysicalMemory& physical_memory, rex::IPhysicalWriteWatch& watch) {
  const auto page = memory.SystemHeapAlloc(4096, 4096, rex::kernel::kSystemHeapPhysical);
  Require(page != 0, "epoch probe allocation");
  const auto physical = memory.GetPhysicalAddress(page);
  x360n::native::GuestMemoryMirror mirror(physical_memory, watch, [](uint32_t, uint32_t) {});
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
void Run(rex::gpu::IXenosGraphicsDriver& gpu, rex::kernel::GuestAddressSpace& memory,
         rex::IPhysicalMemory& physical_memory, rex::IPhysicalWriteWatch& watch,
         bool precision, bool mulsc_rtz, bool textures, bool snap, bool legacy_unorm8,
         bool scaled_3d) {
  CheckReadbackEpoch(memory,physical_memory,watch);
  constexpr uint32_t allocation = 1 << 20;
  const auto ring = memory.SystemHeapAlloc(65536, 4096, rex::kernel::kSystemHeapPhysical);
  const auto outputs = memory.SystemHeapAlloc(allocation, 4096, rex::kernel::kSystemHeapPhysical);
  const auto vertices = memory.SystemHeapAlloc(4096, 4096, rex::kernel::kSystemHeapPhysical);
  const auto done = memory.SystemHeapAlloc(4096, 4096, rex::kernel::kSystemHeapPhysical);
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
    VertexFetch fetch{}; fetch.type = FetchConstantType::kVertex; fetch.address = address >> 2; fetch.size = dwords;
    set(uint32_t(Register::SHADER_CONSTANT_FETCH_00_0), fetch.dword_0); set(uint32_t(Register::SHADER_CONSTANT_FETCH_00_1), fetch.dword_1);
  };
  reg::SQ_PROGRAM_CNTL program{}; program.vs_num_reg = 2; program.ps_num_reg = 1;
  set(uint32_t(Register::SQ_PROGRAM_CNTL), program.value); set(uint32_t(Register::VGT_MAX_VTX_INDX), 0xFFFFFF);
  set(uint32_t(Register::RB_MODECONTROL), uint32_t(EDRAMMode::kNoOperation));
  set(uint32_t(Register::SQ_VS_CONST), 0x000FF000); set(uint32_t(Register::SQ_PS_CONST), 0x000FF100);
  struct Result {
    std::string name;
    uint32_t address;
    std::array<uint8_t, 64> bytes;
    ucode::AluScalarOpcode scalar = ucode::AluScalarOpcode::kRetainPrev;
    float input = 0;
  };
  std::vector<Result> expected;
  FetchOpDimension texture_dimension = FetchOpDimension::k2D;
  uint32_t texture_index = 1;
  bool texture_unnormalized = false, texture_two_coordinates = false, texture_sample = true;
  auto issue = [&](const char* name, ColorFormat format, SurfaceNumberFormat numeric, const std::array<float, 4>& value,
                   uint32_t count, unsigned element_bytes, const std::vector<uint8_t>& bytes,
                   bool vertex = true, bool dynamic = false, bool fetch = false, Endian128 endian = Endian128::kNone,
                   ucode::AluScalarOpcode scalar = ucode::AluScalarOpcode::kRetainPrev, float mulsc_operand = 0.0f) {
    Result result{name, memory.GetPhysicalAddress(outputs) + uint32_t(expected.size()) * 4096, {}};
    result.scalar = scalar; result.input = value[3];
    result.bytes.fill(0xCD); Require(bytes.size() == count * element_bytes && bytes.size() <= result.bytes.size(), "probe expectation extent");
    std::copy(bytes.begin(), bytes.end(), result.bytes.begin());
    MemExportStream stream{}; stream.base_address = result.address >> 2; stream.const_0x1 = 1;
    stream.const_0x4b000000 = 0x4B000000; stream.format = format; stream.num_format = numeric;
    stream.const_0x4b0 = 0x4B0; stream.index_count = count; stream.const_0x96 = 0x96; stream.endianness = endian;
    const uint32_t constants = uint32_t(Register::SHADER_CONSTANT_000_X) + (vertex ? 0 : 256 * 4);
    for (unsigned c = 0; c < 4; ++c) {
      set(constants + c, reinterpret_cast<const uint32_t*>(&stream)[c]);
      fp(constants + 4 + c, c == 1 ? 1.0f : 0.0f); fp(constants + 8 + c, value[c]); fp(constants + 12 + c, mulsc_operand);
      if (precision) fp(constants + 16 + c, 0.0f);
    }
    load(vertex, (textures || scaled_3d) && texture_sample ? TextureExportShader(texture_dimension, texture_unnormalized, texture_two_coordinates, texture_index) :
        precision && scalar != ucode::AluScalarOpcode::kRetainPrev ?
        ScalarExportShader(scalar) : ExportShader(vertex, dynamic, fetch));
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
  if (!precision && !textures && !scaled_3d) {
  const auto source = issue("VS-only incomplete triangle", ColorFormat::k_32_32_32_32_FLOAT, SurfaceNumberFormat::kFloat, data, 2, 16, raw(data, 2));
  // A cache-register poll between a GPU store and its vertex-fetch consumer
  // does not publish memory to the CPU. GPU ordering must preserve the alias.
  set(uint32_t(Register::COHER_STATUS_HOST), 0);
  words.insert(words.end(), {Packet(PM4_WAIT_REG_MEM, 5), 3, uint32_t(Register::COHER_STATUS_HOST), 0, 0xffffffff, 0x100});
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
  }
  set(uint32_t(Register::RB_MODECONTROL), uint32_t(EDRAMMode::kColorDepth)); set(uint32_t(Register::RB_SURFACE_INFO), 80);
  set(uint32_t(Register::RB_COLOR_MASK), 0); set(uint32_t(Register::RB_DEPTHCONTROL), 0); set(uint32_t(Register::PA_CL_VTE_CNTL), 0x3F);
  fp(uint32_t(Register::PA_CL_VPORT_XSCALE), 8); fp(uint32_t(Register::PA_CL_VPORT_XOFFSET), 8);
  fp(uint32_t(Register::PA_CL_VPORT_YSCALE), -8); fp(uint32_t(Register::PA_CL_VPORT_YOFFSET), 8);
  fp(uint32_t(Register::PA_CL_VPORT_ZSCALE), 1); fp(uint32_t(Register::PA_CL_VPORT_ZOFFSET), 0);
  set(uint32_t(Register::PA_SC_SCREEN_SCISSOR_TL), 0); set(uint32_t(Register::PA_SC_SCREEN_SCISSOR_BR), 16 | (16 << 16));
  set(uint32_t(Register::PA_SC_WINDOW_SCISSOR_TL), 0x80000000); set(uint32_t(Register::PA_SC_WINDOW_SCISSOR_BR), 16 | (16 << 16));
  vertex_fetch(memory.GetPhysicalAddress(vertices), 12); load(true, PositionShader());
  if (scaled_3d) {
    texture_sample = false;
    issue("scaled pixel MEMEXPORT control", ColorFormat::k_32_32_32_32_FLOAT,
          SurfaceNumberFormat::kFloat, {1,1,1,1}, 1, 16, raw({1,1,1,1}), false);
    texture_sample = true;
    const auto render_vertices = memory.SystemHeapAlloc(4096, 4096, rex::kernel::kSystemHeapPhysical);
    const auto rect = memory.SystemHeapAlloc(4096, 4096, rex::kernel::kSystemHeapPhysical);
    const auto volume = memory.SystemHeapAlloc(16384, 4096, rex::kernel::kSystemHeapPhysical);
    Require(render_vertices && rect && volume, "scaled 3D fixture allocations");
    const float shifted[]{-.75f,-1,.5f,1, 1.25f,-1,.5f,1, -.75f,1,.5f,1};
    const float rectangle[]{0,0,32,0,0,32};
    std::memcpy(memory.TranslateVirtual(render_vertices), shifted, sizeof(shifted));
    std::memcpy(memory.TranslateVirtual(rect), rectangle, sizeof(rectangle));
    std::memset(memory.TranslateVirtual(volume), 0, 16384);
    fp(uint32_t(Register::PA_CL_VPORT_XSCALE), 16); fp(uint32_t(Register::PA_CL_VPORT_XOFFSET), 16);
    fp(uint32_t(Register::PA_CL_VPORT_YSCALE), -16); fp(uint32_t(Register::PA_CL_VPORT_YOFFSET), 16);
    set(uint32_t(Register::PA_SC_SCREEN_SCISSOR_BR), 32 | (32 << 16));
    set(uint32_t(Register::PA_SC_WINDOW_SCISSOR_BR), 32 | (32 << 16));
    set(uint32_t(Register::PA_SU_VTX_CNTL), 1); set(uint32_t(Register::RB_COLOR_INFO), 0);
    reg::RB_BLENDCONTROL blend{};
    blend.color_srcblend = blend.alpha_srcblend = BlendFactor::kOne;
    blend.color_destblend = blend.alpha_destblend = BlendFactor::kZero;
    set(uint32_t(Register::RB_BLENDCONTROL0), blend.value);
    reg::VGT_DRAW_INITIATOR draw{}; draw.source_select = SourceSelect::kAutoIndex;
    draw.num_indices = 3;
    auto resolve = [&](bool clear) {
      vertex_fetch(memory.GetPhysicalAddress(rect), 6);
      set(uint32_t(Register::RB_MODECONTROL), uint32_t(EDRAMMode::kCopy));
      set(uint32_t(Register::RB_COPY_DEST_BASE), memory.GetPhysicalAddress(volume));
      set(uint32_t(Register::RB_COPY_DEST_PITCH), 32 | (32 << 16));
      reg::RB_COPY_DEST_INFO dest{}; dest.copy_dest_array = 1;
      dest.copy_dest_format = ColorFormat::k_8_8_8_8;
      set(uint32_t(Register::RB_COPY_DEST_INFO), dest.value);
      reg::RB_COPY_CONTROL copy{}; copy.copy_command = CopyCommand(1);
      copy.color_clear_enable = clear;
      set(uint32_t(Register::RB_COLOR_CLEAR), 0); set(uint32_t(Register::RB_COLOR_CLEAR_LO), 0);
      set(uint32_t(Register::RB_COPY_CONTROL), copy.value);
      draw.prim_type = PrimitiveType::kRectangleList;
      words.insert(words.end(), {Packet(PM4_DRAW_INDX_2, 1), draw.value});
    };
    resolve(true);
    vertex_fetch(memory.GetPhysicalAddress(render_vertices), 12);
    set(uint32_t(Register::RB_MODECONTROL), uint32_t(EDRAMMode::kColorDepth));
    set(uint32_t(Register::RB_COLOR_MASK), 15);
    using namespace ucode;
    const uint64_t alloc = (uint64_t(AllocType::kPsColors) << 41) | (uint64_t(ControlFlowOpcode::kAlloc) << 44);
    const uint64_t exec = 1 | (uint64_t(1) << 12) | (uint64_t(ControlFlowOpcode::kExecEnd) << 44);
    load(false, {uint32_t(alloc), uint32_t(alloc >> 32) | uint32_t(exec << 16), uint32_t(exec >> 16),
        (1u << 15) | (15u << 16), 0, uint32_t(AluVectorOpcode::kMax) << 24});
    const float color[]{.75f,.25f,.5f,1};
    for (unsigned c = 0; c < 4; ++c) fp(uint32_t(Register::SHADER_CONSTANT_000_X) + 256 * 4 + c, color[c]);
    draw.prim_type = PrimitiveType::kTriangleList;
    words.insert(words.end(), {Packet(PM4_DRAW_INDX_2, 1), draw.value});
    resolve(false);
    set(uint32_t(Register::RB_MODECONTROL), uint32_t(EDRAMMode::kColorDepth));
    set(uint32_t(Register::RB_COLOR_MASK), 0);
    vertex_fetch(memory.GetPhysicalAddress(vertices), 12);
    texture_sample = false;
    issue("scaled pixel MEMEXPORT after resolve", ColorFormat::k_32_32_32_32_FLOAT,
          SurfaceNumberFormat::kFloat, {1,1,1,1}, 1, 16, raw({1,1,1,1}), false);
    texture_sample = true;
    TextureFetch fetch{}; fetch.type = FetchConstantType::kTexture;
    fetch.dimension = DataDimension::k3D; fetch.format = TextureFormat::k_8_8_8_8;
    fetch.base_address = memory.GetPhysicalAddress(volume) >> 12; fetch.pitch = 1; fetch.tiled = 1;
    fetch.size_3d.width = fetch.size_3d.height = 31; fetch.size_3d.depth = 3;
    fetch.swizzle = 0x688;
    fetch.mag_filter = fetch.min_filter = fetch.mip_filter = TextureFilter::kPoint;
    fetch.aniso_filter = AnisoFilter::kDisabled;
    fetch.clamp_x = fetch.clamp_y = fetch.clamp_z = ClampMode::kClampToEdge;
    for (unsigned c = 0; c < 6; ++c)
      set(uint32_t(Register::SHADER_CONSTANT_FETCH_00_0) + texture_index * 6 + c, reinterpret_cast<const uint32_t*>(&fetch)[c]);
    // Check both sides of the triangle so an empty or incorrectly sized wrapper cannot pass.
    issue("scaled 3D sampled as 2D", ColorFormat::k_32_32_32_32_FLOAT, SurfaceNumberFormat::kFloat,
          {23.5f/32,16.5f/32,0,0}, 1, 16, raw({0,0,0,0}), false);
    const auto unorm = [](uint32_t v) { return std::nearbyint((float(v)/255.0f)*65536.0f)/65536.0f; };
    issue("scaled 3D painted texel", ColorFormat::k_32_32_32_32_FLOAT, SurfaceNumberFormat::kFloat,
          {8.5f/32,16.5f/32,0,0}, 1, 16, raw({unorm(191),unorm(64),unorm(128),1}), false);
  } else if (precision) {
    using namespace ucode;
    const AluScalarOpcode ops[]{AluScalarOpcode::kExp, AluScalarOpcode::kLog, AluScalarOpcode::kLogc,
        AluScalarOpcode::kRcp, AluScalarOpcode::kRcpc, AluScalarOpcode::kRcpf,
        AluScalarOpcode::kRsq, AluScalarOpcode::kRsqc, AluScalarOpcode::kRsqf, AluScalarOpcode::kSqrt};
    const float values[]{0.0f, -0.0f, .125f, .75f, 1.0f, 2.0f, 3.0f, 16.0f,
        -2.0f, -INFINITY, INFINITY, std::numeric_limits<float>::quiet_NaN()};
    for (auto opcode : ops) for (float value : values) {
      const float result = ScalarExpected(opcode, value);
      const std::array<float, 4> input{value, value, value, value}, output{result, result, result, result};
      const auto name = "scalar opcode=" + std::to_string(unsigned(opcode)) + " input=" +
                        std::to_string(std::bit_cast<uint32_t>(value));
      issue(name.c_str(), ColorFormat::k_32_32_32_32_FLOAT, SurfaceNumberFormat::kFloat,
            input, 1, 16, raw(output), false, false, false, Endian128::kNone, opcode);
    }
    for (float a : {.1f, -.1f, .3f, 1.3f}) for (float b : {.1f, .3f, -1.1f, 9.876543f}) {
      const double exact = double(a) * double(b);
      float result = float(exact);
      if (mulsc_rtz && std::fabs(double(result)) > std::fabs(exact)) result = std::nextafter(result, 0.0f);
      const std::array<float, 4> input{a,a,a,a}, output{result,result,result,result};
      const auto name = "mulsc " + std::to_string(a) + " * " + std::to_string(b);
      issue(name.c_str(), ColorFormat::k_32_32_32_32_FLOAT, SurfaceNumberFormat::kFloat,
            input, 1, 16, raw(output), false, false, false, Endian128::kNone, AluScalarOpcode::kMulsc0, b);
    }
    // Fixed guest encodings, independent of the translator's host FP16 intrinsics.
    const std::pair<float, uint16_t> halves[]{
        {0.0f, 0}, {-0.0f, 0x8000}, {1.0f, 0x3C00}, {-2.0f, 0xC000},
        {65504.0f, 0x7BFF}, {65519.0f, 0x7BFF}, {65520.0f, 0x7C00},
        {65536.0f, 0x7C00}, {70000.0f, 0x7C46}, {-65520.0f, 0xFC00},
        {-70000.0f, 0xFC46}, {131008.0f, 0x7FFF},
        {-131008.0f, 0xFFFF}, {200000.0f, 0x7FFF}, {INFINITY, 0x7FFF},
        {-INFINITY, 0xFFFF}, {NAN, 0}, {0x1p-24f, 1}, {0x1p-25f, 0},
        {0x3p-25f, 2}, {0x1p-14f, 0x0400}};
    for (unsigned components : {1u, 2u, 4u}) {
      const auto format = components == 1 ? ColorFormat::k_16_FLOAT : components == 2 ?
          ColorFormat::k_16_16_FLOAT : ColorFormat::k_16_16_16_16_FLOAT;
      for (size_t h = 0; h < std::size(halves); ++h) {
        std::array<float, 4> input;
        for (unsigned i = 0; i < 4; ++i) input[i] = halves[(h + i) % std::size(halves)].first;
        std::vector<uint8_t> bytes(components * 2);
        for (unsigned i = 0; i < components; ++i) {
          const uint16_t packed = halves[(h + i) % std::size(halves)].second;
          bytes[i * 2] = uint8_t(packed); bytes[i * 2 + 1] = uint8_t(packed >> 8);
        }
        const auto name = "extended FP16x" + std::to_string(components) + " input=" +
            std::to_string(std::bit_cast<uint32_t>(input[0]));
        issue(name.c_str(), format, SurfaceNumberFormat::kFloat,
              input, 1, components * 2, bytes, false);
      }
    }
  } else if (textures) {
    const auto texels = memory.SystemHeapAlloc(2u << 20, 4096, rex::kernel::kSystemHeapPhysical);
    Require(texels != 0, "texture fixture allocation");
    std::memset(memory.TranslateVirtual(texels), 0, 2u << 20);
    uint32_t cursor = 0;
    TextureFetch last_texture{};
    auto bind = [&](TextureFormat format, uint32_t width, uint32_t height, bool integer,
                    TextureSign sign, DataDimension dimension, uint32_t swizzle = 0x688,
                    bool no_data = false, uint32_t gpu_source = 0) {
      cursor = (cursor + 4095) & ~4095u;
      Require(cursor + uint64_t(width) * height * 16 <= 2u << 20, "texture fixture budget");
      const auto address = texels + cursor;
      cursor += ((width * height * 16 + 4095) & ~4095u);
      TextureFetch fetch{};
      fetch.type = FetchConstantType::kTexture; fetch.dimension = dimension;
      fetch.format = format; fetch.base_address = no_data ? 0 :
          (gpu_source ? gpu_source : memory.GetPhysicalAddress(address)) >> 12;
      fetch.pitch = dimension == DataDimension::k1D && width > 8192 ? 256 : std::max(2u, (width + 31) / 32);
      if (dimension == DataDimension::k1D) fetch.size_1d.width = width - 1;
      else { fetch.size_2d.width = width - 1; fetch.size_2d.height = height - 1; }
      fetch.swizzle = swizzle; fetch.num_format = integer;
      fetch.sign_x = fetch.sign_y = fetch.sign_z = fetch.sign_w = sign;
      fetch.mag_filter = fetch.min_filter = fetch.mip_filter = TextureFilter::kPoint;
      fetch.aniso_filter = AnisoFilter::kDisabled;
      fetch.clamp_x = fetch.clamp_y = fetch.clamp_z = ClampMode::kClampToEdge;
      const auto* words_fetch = reinterpret_cast<const uint32_t*>(&fetch);
      for (unsigned c = 0; c < 6; ++c) set(uint32_t(Register::SHADER_CONSTANT_FETCH_00_0) + texture_index * 6 + c, words_fetch[c]);
      last_texture = fetch;
      return memory.TranslateVirtual<uint8_t*>(address);
    };
    struct FixedFormat { TextureFormat format; unsigned bits, components; };
    for (const auto& f : {FixedFormat{TextureFormat::k_8, 8, 1},
                         {TextureFormat::k_8_8, 8, 2}, {TextureFormat::k_8_8_8_8, 8, 4},
                         {TextureFormat::k_4_4_4_4, 4, 4},
                         {TextureFormat::k_16, 16, 1}, {TextureFormat::k_16_16, 16, 2},
                         {TextureFormat::k_16_16_16_16, 16, 4}}) {
      const unsigned maximum = (1u << f.bits) - 1;
      for (bool integer : {false, true}) for (unsigned value : {0u, 1u, maximum / 4, maximum}) {
        auto* bytes = bind(f.format, 1, 1, integer, TextureSign::kUnsigned, DataDimension::k2DOrStacked);
        if (f.bits == 4) { const uint16_t packed = uint16_t(value * 0x1111u); std::memcpy(bytes, &packed, 2); }
        else for (unsigned c = 0; c < f.components; ++c) {
          if (f.bits == 8) bytes[c] = uint8_t(value);
          else { const uint16_t packed = uint16_t(value); std::memcpy(bytes + c * 2, &packed, 2); }
        }
        float result = float(value);
        if (!integer) {
          const float sample = f.bits == 4 ? float(value * 17) / 256.0f : float(value) / float(maximum);
          result = float(f.bits == 8 && legacy_unorm8 ? std::ceil(sample * 65536.0f) :
                         std::nearbyint(sample * 65536.0f)) / 65536.0f;
        }
        const auto name = "tfetch fixed format=" + std::to_string(unsigned(f.format)) +
            " integer=" + std::to_string(integer) + " value=" + std::to_string(value);
        issue(name.c_str(), ColorFormat::k_32_32_32_32_FLOAT, SurfaceNumberFormat::kFloat,
              {.25f,.25f,0,0}, 1, 16, raw({result,result,result,result}), false);
      }
    }
    auto* reused = bind(TextureFormat::k_8, 1, 1, false, TextureSign::kUnsigned, DataDimension::k2DOrStacked);
    reused[0] = 63;
    const float normalized = float(legacy_unorm8 ? std::ceil((63.0f / 255.0f) * 65536.0f) :
                                    std::nearbyint((63.0f / 255.0f) * 65536.0f)) / 65536.0f;
    for (bool integer : {false, true, false}) {
      last_texture.num_format = integer;
      set(uint32_t(Register::SHADER_CONSTANT_FETCH_00_0) + texture_index * 6 + 3, last_texture.dword_3);
      const float value = integer ? 63.0f : normalized;
      issue(integer ? "resident texture changes to integer units" : "resident texture restores normalized units",
            ColorFormat::k_32_32_32_32_FLOAT, SurfaceNumberFormat::kFloat,
            {.25f,.25f,0,0}, 1, 16, raw({value,value,value,value}), false);
    }
    for (auto format : {TextureFormat::k_5_6_5, TextureFormat::k_1_5_5_5}) {
      const bool alpha = format == TextureFormat::k_1_5_5_5;
      const unsigned widths[]{5, alpha ? 5u : 6u, 5, alpha ? 1u : 5u};
      for (bool integer : {false, true}) for (unsigned pattern = 0; pattern < 4; ++pattern) {
        auto* bytes = bind(format, 1, 1, integer, TextureSign::kUnsigned, DataDimension::k2DOrStacked);
        uint16_t packed = 0; unsigned shift = 0;
        std::array<float, 4> result;
        for (unsigned c = 0; c < 4; ++c) {
          const unsigned maximum = (1u << widths[c]) - 1;
          const unsigned value = pattern == 0 ? 0 : pattern == 1 ? 1 : pattern == 2 ? maximum / 4 : maximum;
          result[c] = integer ? float(value) : widths[c] == 1 ? float(value) :
              float(value * ((1u << widths[c]) + 1)) / float(1u << (2 * widths[c]));
          if (c < (alpha ? 4u : 3u)) { packed |= uint16_t(value << shift); shift += widths[c]; }
        }
        std::memcpy(bytes, &packed, 2);
        const auto name = "tfetch packed format=" + std::to_string(unsigned(format)) +
            " integer=" + std::to_string(integer) + " pattern=" + std::to_string(pattern);
        issue(name.c_str(), ColorFormat::k_32_32_32_32_FLOAT, SurfaceNumberFormat::kFloat,
              {.25f,.25f,0,0}, 1, 16, raw(result), false);
      }
    }
    for (auto format : {TextureFormat::k_8, TextureFormat::k_16})
      for (auto sign : {TextureSign::kSigned, TextureSign::kUnsignedBiased}) {
      const unsigned bits = format == TextureFormat::k_8 ? 8 : 16;
      for (int value : {0, 1, -1, -(1 << (bits - 1))}) {
        auto* bytes = bind(format, 1, 1, true, sign, DataDimension::k2DOrStacked);
        const uint16_t packed = uint16_t(value); std::memcpy(bytes, &packed, bits / 8);
        const unsigned stored = unsigned(value) & ((1u << bits) - 1);
        // Unsigned-biased is offset binary, including its negative endpoint.
        const float result = sign == TextureSign::kSigned ? float(std::max(value, -((1 << (bits - 1)) - 1))) :
            float(int(stored) - (1 << (bits - 1)));
        const auto name = "tfetch integer sign=" + std::to_string(unsigned(sign)) +
            " bits=" + std::to_string(bits) + " value=" + std::to_string(value);
        issue(name.c_str(), ColorFormat::k_32_32_32_32_FLOAT, SurfaceNumberFormat::kFloat,
              {.25f,.25f,0,0}, 1, 16, raw({result,result,result,result}), false);
      }
    }
    texture_dimension = FetchOpDimension::k1D; texture_unnormalized = true;
    for (unsigned width : {8193u, 24576u}) {
      auto* wide = bind(TextureFormat::k_32_FLOAT, width, 1, false, TextureSign::kUnsigned, DataDimension::k1D);
      for (unsigned i = 0; i < width; ++i) { const float value = float(i) * .25f + 1; std::memcpy(wide + i * 4, &value, 4); }
      for (unsigned index : {0u, 8191u, 8192u, 16383u, 16384u, 24575u}) {
        if (index >= width) continue;
        const float value = float(index) * .25f + 1;
        const auto name = "tfetch wide 1D width=" + std::to_string(width) + " index=" + std::to_string(index);
        issue(name.c_str(), ColorFormat::k_32_32_32_32_FLOAT, SurfaceNumberFormat::kFloat,
              {float(index)+.25f,0,0,0}, 1, 16, raw({value,value,value,value}), false);
      }
    }
    texture_two_coordinates = true; texture_unnormalized = false;
    auto* grid = bind(TextureFormat::k_32_FLOAT, 2, 2, false, TextureSign::kUnsigned, DataDimension::k2DOrStacked);
    for (unsigned y = 0; y < 2; ++y) for (unsigned x = 0; x < 2; ++x) {
      const float value = float(y * 2 + x + 1); std::memcpy(grid + y * 256 + x * 4, &value, 4);
    }
    issue("tfetch1D uses XY with 2D storage", ColorFormat::k_32_32_32_32_FLOAT, SurfaceNumberFormat::kFloat,
          {.25f,.75f,0,0}, 1, 16, raw({3,3,3,3}), false);
    texture_dimension = FetchOpDimension::k2D;
    const float edge = snap ? 3 : 4;
    issue("point coordinate near texel edge", ColorFormat::k_32_32_32_32_FLOAT, SurfaceNumberFormat::kFloat,
          {.4999f,.75f,0,0}, 1, 16, raw({edge,edge,edge,edge}), false);
    for (bool white : {false, true}) {
      auto* border_texel = bind(TextureFormat::k_8_8_8_8, 1, 1, false, TextureSign::kUnsigned,
                                DataDimension::k2DOrStacked);
      const uint8_t rgba[]{64,128,192,32};
      std::memcpy(border_texel, rgba, sizeof(rgba));
      last_texture.clamp_x = ClampMode::kClampToBorder;
      last_texture.border_color = white ? BorderColor::k_ABGR_White : BorderColor::k_ABGR_Black;
      for (bool force_w : {false, true, false}) {
        last_texture.force_bc_w_to_max = force_w;
        const auto* fetch_words = reinterpret_cast<const uint32_t*>(&last_texture);
        for (unsigned c = 0; c < 6; ++c)
          set(uint32_t(Register::SHADER_CONSTANT_FETCH_00_0) + texture_index * 6 + c, fetch_words[c]);
        const float rgb = white ? 1.0f : 0.0f;
        issue(white ? "white border alpha" : force_w ? "forced black border alpha" : "black border alpha",
              ColorFormat::k_32_32_32_32_FLOAT, SurfaceNumberFormat::kFloat,
              {-2,.25f,0,0}, 1, 16, raw({rgb,rgb,rgb,white || force_w ? 1.0f : 0.0f}), false);
      }
    }
    for (bool mixed : {false, true}) {
      const uint32_t swizzle = mixed ? 4u | (5u << 3) | (4u << 6) | (5u << 9) :
                                      5u | (5u << 3) | (5u << 6) | (5u << 9);
      bind(TextureFormat::k_32_FLOAT, 1, 1, false, TextureSign::kUnsigned,
           DataDimension::k2DOrStacked, swizzle, true);
      issue(mixed ? "constant-only texture 0101" : "constant-only texture 1111",
            ColorFormat::k_32_32_32_32_FLOAT, SurfaceNumberFormat::kFloat,
            {.25f,.25f,0,0}, 1, 16, raw({mixed ? 0.0f : 1.0f,1,mixed ? 0.0f : 1.0f,1}), false);
    }
    texture_index = 31;
    auto* last = bind(TextureFormat::k_8, 1, 1, true, TextureSign::kUnsigned, DataDimension::k2DOrStacked);
    last[0] = 63;
    issue("last fetch constant uses its own conversion metadata", ColorFormat::k_32_32_32_32_FLOAT,
          SurfaceNumberFormat::kFloat, {.25f,.25f,0,0}, 1, 16, raw({63,63,63,63}), false);
    texture_sample = false;
    const auto gpu_texel = issue("GPU writes integer texture backing", ColorFormat::k_16,
        SurfaceNumberFormat::kUnsignedInteger, {1234,1234,1234,1234}, 1, 2, {0xD2,0x04}, false);
    texture_sample = true;
    bind(TextureFormat::k_16, 1, 1, true, TextureSign::kUnsigned, DataDimension::k2DOrStacked,
         0x688, false, gpu_texel);
    issue("GPU-written integer texel is sampled", ColorFormat::k_32_32_32_32_FLOAT,
          SurfaceNumberFormat::kFloat, {.25f,.25f,0,0}, 1, 16, raw({1234,1234,1234,1234}), false);
  } else {
  issue("pixel MEMEXPORT without color", ColorFormat::k_32_32_32_32_FLOAT, SurfaceNumberFormat::kFloat, data, 1, 16, raw(data), false);
  set(uint32_t(Register::RB_MODECONTROL), uint32_t(EDRAMMode::kNoOperation));
  issue("computed export address", ColorFormat::k_32_32_32_32_FLOAT, SurfaceNumberFormat::kFloat, data, 1, 16, raw(data), true, true);
  }
  words.insert(words.end(), {Packet(PM4_MEM_WRITE, 2), memory.GetPhysicalAddress(done), 1});
  Require(words.size() * 4 < 65536, "probe command capacity");
  for (size_t i = 0; i < words.size(); ++i) rex::memory::store_and_swap<uint32_t>(memory.TranslateVirtual(ring + uint32_t(i * 4)), words[i]);
  gpu.InitializeRingBuffer(memory.GetPhysicalAddress(ring), 13);
  gpu.WriteRegister(uint32_t(Register::CP_RB_WPTR), uint32_t(words.size()));
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
  auto* marker = memory.TranslateVirtual<volatile uint32_t*>(done);
  while (*marker != 1 && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  Require(*marker == 1, "MEMEXPORT completion timeout");
  bool passed = true;
  for (const auto& result : expected) {
    bool equal = true;
    const auto* actual = memory.TranslatePhysical<const uint8_t*>(result.address);
    if (precision && result.scalar != ucode::AluScalarOpcode::kRetainPrev) {
      for (unsigned i = 0; i < 4; ++i) {
        float want, got;
        std::memcpy(&want, result.bytes.data() + i * 4, 4);
        std::memcpy(&got, actual + i * 4, 4);
        bool same = std::isnan(want) ? std::isnan(got) : std::bit_cast<uint32_t>(want) == std::bit_cast<uint32_t>(got);
        if ((result.scalar == ucode::AluScalarOpcode::kLog || result.scalar == ucode::AluScalarOpcode::kLogc) &&
            result.input > 0 && std::isfinite(result.input)) {
          int exponent;
          if (std::frexp(result.input, &exponent) != .5f) {
            // Native log is approximate: https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/log--sm4---asm-
            // Require its documented error bound plus half a reduced ULP,
            // and independently require exactly 21 stored mantissa bits.
            const double exact = std::log2(double(result.input));
            const double host_bound = std::ldexp(result.input >= .5f && result.input <= 2 ? 1.0 : std::fabs(exact), -21);
            const double rounding_bound = !std::isfinite(got) || got == 0 ? 0 :
                std::ldexp(1.0, std::ilogb(std::fabs(got)) - 22);
            same = std::isfinite(got) && (std::bit_cast<uint32_t>(got) & 3) == 0 &&
                   std::fabs(double(got) - exact) <= host_bound + rounding_bound;
          }
        }
        if (!same) std::cerr << result.name << " lane=" << i << " got=" << std::hex
                            << std::bit_cast<uint32_t>(got) << " expected=" << std::bit_cast<uint32_t>(want) << std::dec << '\n';
        equal &= same;
      }
      equal &= std::memcmp(actual + 16, result.bytes.data() + 16, result.bytes.size() - 16) == 0;
    } else {
      equal = std::memcmp(actual, result.bytes.data(), result.bytes.size()) == 0;
      if (!equal) {
        if (textures) for (unsigned c = 0; c < 4; ++c) {
          float got, want; std::memcpy(&got, actual + c * 4, 4); std::memcpy(&want, result.bytes.data() + c * 4, 4);
          std::cerr << "channel " << c << " got=" << got << " expected=" << want << '\n';
        }
        uint16_t got, want;
        std::memcpy(&got, actual, 2); std::memcpy(&want, result.bytes.data(), 2);
        std::cerr << result.name << " first half got=" << std::hex << got
                  << " expected=" << want << std::dec << '\n';
      }
    }
    std::cout << result.name << ' ' << (equal ? "PASS" : "FAIL") << '\n'; passed &= equal;
  }
  Require(passed, "GPU MEMEXPORT bytes differ from contract");
  std::cout << expected.size() << " GPU MEMEXPORT cases passed\n";
}
}
X360N_PROBE_MAIN {
  try {
    Require(argc >= 2 && argc <= 8, "usage: x360n_native_memexport_probe OUTPUT [d3d12|vulkan] [--precision|--textures|--scaled-3d] [--xenos] [--mulsc-rtz] [--point-snap] [--legacy-unorm8]");
    const ProbeArg api = argc >= 3 ? argv[2] : X360N_PROBE_ARG("d3d12");
    Require(api == X360N_PROBE_ARG("d3d12") || api == X360N_PROBE_ARG("vulkan"), "invalid graphics API");
    bool precision = false, xenos = false, mulsc_rtz = false;
    bool textures = false, snap = false, legacy_unorm8 = false, scaled_3d = false;
    for (int i = 3; i < argc; ++i) {
      const ProbeArg arg = argv[i];
      if (arg == X360N_PROBE_ARG("--precision")) precision = true;
      else if (arg == X360N_PROBE_ARG("--xenos")) xenos = true;
      else if (arg == X360N_PROBE_ARG("--mulsc-rtz")) mulsc_rtz = true;
      else if (arg == X360N_PROBE_ARG("--textures")) textures = true;
      else if (arg == X360N_PROBE_ARG("--scaled-3d")) scaled_3d = true;
      else if (arg == X360N_PROBE_ARG("--point-snap")) snap = true;
      else if (arg == X360N_PROBE_ARG("--legacy-unorm8")) legacy_unorm8 = true;
      else throw std::runtime_error("unknown MEMEXPORT probe option");
    }
    Require(!mulsc_rtz || precision, "--mulsc-rtz requires --precision");
    Require(!textures || !precision, "texture and scalar precision fixtures are separate");
    Require(!scaled_3d || (xenos && !textures && !precision && !snap && !legacy_unorm8), "scaled 3D fixture requires only --xenos");
    Require(!(snap || legacy_unorm8) || textures, "texture options require --textures");
    const std::filesystem::path output(argv[1]); std::filesystem::create_directories(output);
    const char* backend = api == X360N_PROBE_ARG("d3d12") ? "d3d12" : "vulkan";
    std::vector<std::string> options{
        "--app.user_root=" + rex::string::PathToUTF8(output / "user"),
        "--log.file_name=" + rex::string::PathToUTF8(std::filesystem::absolute(output / "runtime.log"))};
    auto setting=[&](std::string_view name,std::string_view value) {
      options.emplace_back("--"+std::string(name)+"="+std::string(value));
    };
    setting("mulsc_round_toward_zero", mulsc_rtz ? "true" : "false");
    if (textures) {
      setting("texture_point_snap", snap ? "true" : "false");
      setting("dxbc_unorm8_fixed16", legacy_unorm8 ? "true" : "false");
      if (!xenos && api == X360N_PROBE_ARG("d3d12"))
        setting("native_texture_cpu", "false");
    }
    if (xenos) {
      if (scaled_3d) {
        setting("resolution_scale", "2");
        setting("draw_resolution_scale_x", "2");
        setting("draw_resolution_scale_y", "2");
      }
      setting("readback_memexport", "true");
      setting("readback_memexport_fast", "false");
      setting("async_shader_compilation", "false");
      setting(api == X360N_PROBE_ARG("d3d12") ? "render_target_path_d3d12" : "render_target_path_vulkan",
          api == X360N_PROBE_ARG("d3d12") ? "rov" : "fsi");
    }
    x360n::GPUProbeApp app(output, xenos ? "rexgpu-xenos-"+std::string(backend) : "rexgpu-native",
        xenos ? std::string(backend) : "native-"+std::string(backend));
    const auto initialized=app.Init(rex::CLIArgList::Parse(options));
    if(!initialized)throw std::runtime_error(initialized.error().GetDescription());
    auto& graphics = static_cast<rex::gpu::IGraphicsDriver&>(app.GetProbeDriver());
    auto* gpu = graphics.AsXenos();
    Require(gpu != nullptr, "GPU probe requires a Xenos-family driver");
    const auto surface = graphics.SetSurface({});
    if (!surface) throw std::runtime_error(surface.error().GetDescription());
    rex::kernel::TitleRuntime runtime(rex::ppc::PPCImage{});
    const auto game = runtime.GetTitlePaths().game_root.Set(output);
    const auto entry = runtime.GetTitlePaths().entry.Set(std::string{});
    Require(bool(game) && bool(entry), "GPU probe paths unavailable");
    const auto loaded = runtime.Load();
    if (!loaded) throw std::runtime_error(loaded.error().GetDescription());
    auto* memory = dynamic_cast<rex::kernel::GuestAddressSpace*>(&runtime.GetGuestHeap());
    Require(memory != nullptr, "GPU probe requires the canonical address space");
    const auto attached = graphics.AttachTitle(rex::TitleInfo{}, runtime.GetPhysicalMemory(),
        runtime.GetPhysicalWriteWatch(), runtime.GetGuestInterrupts());
    if (!attached) throw std::runtime_error(attached.error().GetDescription());
    rex::ScopeGuard detach([&] { graphics.DetachTitle(); });
    Run(*gpu, *memory, runtime.GetPhysicalMemory(), runtime.GetPhysicalWriteWatch(), precision, mulsc_rtz, textures, snap, legacy_unorm8, scaled_3d); detach.Reset(); runtime.Unload(); app.Shutdown();
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
