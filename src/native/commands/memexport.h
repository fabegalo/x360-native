#pragma once
#include <rex/graphics/pipeline/shader/shader.h>
#include <rex/graphics/register_file.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>
namespace x360n::native {
struct MemExportRange { uint32_t address, length; };

// A zero SQ_*_CONST is the unprogrammed layout accepted by the pinned
// translator. Otherwise honor the guest's base in both uploads and exports.
inline uint32_t FloatConstantBase(const rex::graphics::RegisterFile& regs, bool vertex) {
  using namespace rex::graphics;
  const uint32_t value = regs[vertex ? XE_GPU_REG_SQ_VS_CONST : XE_GPU_REG_SQ_PS_CONST];
  return value ? (vertex ? regs.Get<reg::SQ_VS_CONST>().base : regs.Get<reg::SQ_PS_CONST>().base)
               : vertex ? 0u : 256u;
}

// Run once per analyzed shader. The SDK's set contains recognized constants
// only: a shader may ALSO write eA dynamically, so a nonempty set alone does
// not prove that all destinations are covered.
inline bool MemExportAddressesKnown(const rex::graphics::Shader& shader) {
  using namespace rex::graphics;
  for (uint32_t i = 0; i < shader.cf_pair_index_bound(); ++i) {
    ucode::ControlFlowInstruction pair[2];
    ucode::UnpackControlFlowInstructions(shader.ucode_dwords() + i * 3, pair);
    for (const auto& cf : pair) {
      if (!ucode::IsControlFlowOpcodeExec(cf.opcode())) continue;
      const auto& exec = cf.exec; // Address/count/sequence occupy the same bits in all exec forms.
      if (uint64_t(exec.address() + exec.count()) * 3 > shader.ucode_dword_count())
        throw std::runtime_error("Native MEMEXPORT: exec outside shader");
      for (uint32_t j = 0; j < exec.count(); ++j) {
        if ((exec.sequence() >> (j * 2)) & 1) continue; // Fetch, not ALU.
        ucode::AluInstruction alu;
        std::memcpy(&alu, shader.ucode_dwords() + (exec.address() + j) * 3, sizeof(alu));
        ParsedAluInstruction parsed;
        ParseAluInstruction(alu, shader.type(), parsed);
        if ((parsed.vector_and_constant_result.storage_target == InstructionStorageTarget::kExportAddress &&
             parsed.vector_and_constant_result.GetUsedResultComponents() && parsed.GetMemExportStreamConstant() == UINT32_MAX) ||
            (parsed.scalar_result.storage_target == InstructionStorageTarget::kExportAddress &&
             parsed.scalar_result.GetUsedResultComponents())) return false;
      }
    }
  }
  return !shader.memexport_stream_constants().empty();
}

// Exact storage widths of the formats implemented by the shared DXBC
// MEMEXPORT translator (the AS_16 aliases still store their packed width).
inline unsigned MemExportElementBytes(rex::graphics::xenos::ColorFormat format) {
  using F = rex::graphics::xenos::ColorFormat;
  switch (format) {
    case F::k_8: case F::k_8_A: case F::k_8_B: return 1;
    case F::k_1_5_5_5: case F::k_5_6_5: case F::k_6_5_5: case F::k_8_8:
    case F::k_4_4_4_4: case F::k_16: case F::k_16_FLOAT: return 2;
    case F::k_8_8_8_8: case F::k_8_8_8_8_A: case F::k_8_8_8_8_AS_16_16_16_16:
    case F::k_2_10_10_10: case F::k_2_10_10_10_AS_16_16_16_16:
    case F::k_10_11_11: case F::k_10_11_11_AS_16_16_16_16:
    case F::k_11_11_10: case F::k_11_11_10_AS_16_16_16_16:
    case F::k_16_16: case F::k_16_16_FLOAT: case F::k_32_FLOAT: return 4;
    case F::k_16_16_16_16: case F::k_16_16_16_16_FLOAT: case F::k_32_32_FLOAT: return 8;
    case F::k_32_32_32_32_FLOAT: return 16;
    default: throw std::runtime_error("Native MEMEXPORT: unsupported stream format");
  }
}

inline void AddMemExportRanges(const rex::graphics::RegisterFile& regs,
    const rex::graphics::Shader& shader, bool addresses_known, std::vector<MemExportRange>& ranges) {
  using namespace rex::graphics;
  if (!shader.memexport_eM_written()) return;
  if (!addresses_known) {
    // Computed eA is valid guest code. Keep every possible byte resident and
    // publish the actual GPU result; do not silently drop unknown destinations.
    ranges.push_back({0, 0x20000000});
    return;
  }
  const auto base = FloatConstantBase(regs, shader.type() == xenos::ShaderType::kVertex);
  for (auto index : shader.memexport_stream_constants()) {
    if (base + index >= 512) throw std::runtime_error("Native MEMEXPORT: stream constant outside register file");
    const auto stream = regs.GetMemExportStream(base + index);
    // The translated shader applies these same address validity tests.
    if (stream.const_0x1 != 1 || stream.const_0x4b0 != 0x4B0 || stream.const_0x96 != 0x96 || !stream.index_count) continue;
    const uint64_t address = uint64_t(stream.base_address) * 4;
    const uint64_t length = uint64_t(stream.index_count) * MemExportElementBytes(stream.format);
    if (address + length > 0x20000000) throw std::runtime_error("Native MEMEXPORT: stream outside guest physical memory");
    ranges.push_back({uint32_t(address), uint32_t(length)});
  }
}
inline void MergeMemExportRanges(std::vector<MemExportRange>& ranges) {
  std::sort(ranges.begin(), ranges.end(), [](const auto& a, const auto& b) { return a.address < b.address; });
  size_t count = 0;
  for (const auto range : ranges) {
    if (count && uint64_t(ranges[count - 1].address) + ranges[count - 1].length >= range.address) {
      auto& previous = ranges[count - 1];
      previous.length = std::max(previous.length, range.address + range.length - previous.address);
    } else ranges[count++] = range;
  }
  ranges.resize(count);
}
}
