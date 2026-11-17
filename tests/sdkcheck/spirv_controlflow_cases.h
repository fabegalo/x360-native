#pragma once
#include <rex/xenos/ucode.h>
#include <SPIRV/spirv.hpp>
#include <array>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

namespace x360n::test::spirv {
inline void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
struct Phi { uint32_t block; std::vector<uint32_t> inputs; };
inline void CheckPhiPredecessors(const std::vector<uint8_t>& binary, bool check_replay) {
  Require(binary.size() >= 20 && !(binary.size() % 4), "SPIR-V length");
  std::vector<uint32_t> words(binary.size() / 4);
  std::memcpy(words.data(), binary.data(), binary.size());
  Require(words[0] == spv::MagicNumber, "SPIR-V magic");
  std::map<uint32_t, std::set<uint32_t>> predecessors;
  std::map<uint32_t, Phi> phis;
  std::map<uint32_t, uint32_t> constants;
  std::set<uint32_t> switch_selectors;
  uint32_t block = 0;
  for (size_t offset = 5; offset < words.size();) {
    const uint32_t count = words[offset] >> 16;
    const auto op = spv::Op(words[offset] & 0xFFFF);
    Require(count && offset + count <= words.size(), "SPIR-V instruction extent");
    const auto* args = words.data() + offset + 1;
    if (op == spv::OpLabel) block = args[0];
    else if (op == spv::OpConstant && count == 4) constants[args[1]] = args[2];
    else if (op == spv::OpPhi) {
      Require(count >= 5 && (count & 1), "SPIR-V Phi pairs");
      phis.emplace(args[1], Phi{block, {args + 2, args + count - 1}});
    } else if (op == spv::OpBranch) predecessors[args[0]].insert(block);
    else if (op == spv::OpBranchConditional) {
      predecessors[args[1]].insert(block); predecessors[args[2]].insert(block);
    } else if (op == spv::OpSwitch) {
      // All translated guest switches have a 32-bit program counter.
      switch_selectors.insert(args[0]);
      predecessors[args[1]].insert(block);
      for (uint32_t i = 3; i + 1 < count; i += 2) predecessors[args[i]].insert(block);
    }
    offset += count;
  }
  for (const auto& [id, phi] : phis) {
    std::set<uint32_t> incoming;
    for (size_t i = 1; i < phi.inputs.size(); i += 2)
      Require(incoming.insert(phi.inputs[i]).second, "duplicate Phi predecessor");
    Require(incoming == predecessors[phi.block], "Phi does not cover exactly its CFG predecessors");
  }
  if (check_replay) {
    bool found_replay = false;
    for (const auto selector : switch_selectors) {
      const auto header = phis.find(selector);
      if (header == phis.end()) continue;
      for (size_t i = 0; i < header->second.inputs.size(); i += 2) {
        const auto next = phis.find(header->second.inputs[i]);
        if (next == phis.end()) continue;
        for (size_t n = 0; n < next->second.inputs.size(); n += 2) {
          const auto value = constants.find(next->second.inputs[n]);
          if (value != constants.end() && value->second == 0) found_replay = true;
        }
      }
    }
    Require(found_replay, "rectangle replay does not restart the guest PC at zero");
  }
}
inline std::vector<uint32_t> VertexMicrocode(unsigned jump) {
  using namespace rex::xenos::ucode;
  auto opcode = [](ControlFlowOpcode op) { return uint64_t(op) << 44; };
  if (jump == 4) {
    // The jump after EXEC_END is unreachable, but its target still creates a
    // switch label. The ordinary VS has no reachable continuation predecessor.
    const std::array<uint64_t, 6> cf{
        (uint64_t(AllocType::kVsPosition) << 41) | opcode(ControlFlowOpcode::kAlloc),
        2 | (uint64_t(AllocType::kVsInterpolators) << 41) | opcode(ControlFlowOpcode::kAlloc),
        3 | (uint64_t(4) << 12) | opcode(ControlFlowOpcode::kExecEnd),
        4 | (uint64_t(1) << 13) | opcode(ControlFlowOpcode::kCondJmp),
        3 | (uint64_t(4) << 12) | opcode(ControlFlowOpcode::kExecEnd), 0};
    std::vector<uint32_t> words;
    for (size_t i = 0; i < cf.size(); i += 2)
      words.insert(words.end(), {uint32_t(cf[i]), uint32_t(cf[i] >> 32) | uint32_t(cf[i + 1] << 16),
                                uint32_t(cf[i + 1] >> 16)});
    for (const uint32_t output : {62u, 0u, 1u, 2u})
      words.insert(words.end(), {output | (1u << 15) | (15u << 16), 0,
          (uint32_t(AluVectorOpcode::kMax) << 24) | (3u << 30)});
    return words;
  }
  const std::array<uint64_t, 8> cf{
      (uint64_t(AllocType::kVsPosition) << 41) | opcode(ControlFlowOpcode::kAlloc),
      2 | (uint64_t(AllocType::kVsInterpolators) << 41) | opcode(ControlFlowOpcode::kAlloc),
      jump ? 5 | (uint64_t(jump == 2) << 14) | (uint64_t(jump == 3) << 13) |
                 (uint64_t(1) << 42) | opcode(ControlFlowOpcode::kCondJmp) : 0,
      4 | (uint64_t(1) << 12) | opcode(ControlFlowOpcode::kExec),
      jump ? 6 | (uint64_t(1) << 13) | opcode(ControlFlowOpcode::kCondJmp) : 0,
      5 | (uint64_t(1) << 12) | opcode(ControlFlowOpcode::kExec),
      6 | (uint64_t(4) << 12) | opcode(ControlFlowOpcode::kExecEnd), 0};
  std::vector<uint32_t> words;
  for (size_t i = 0; i < cf.size(); i += 2)
    words.insert(words.end(), {uint32_t(cf[i]), uint32_t(cf[i] >> 32) | uint32_t(cf[i + 1] << 16),
                              uint32_t(cf[i + 1] >> 16)});
  for (uint32_t constant = 0; constant < 2; ++constant)
    words.insert(words.end(), {15u << 16, 0,
        (uint32_t(AluVectorOpcode::kMax) << 24) | (constant << 16) | (constant << 8)});
  for (const uint32_t output : {62u, 0u, 1u, 2u})
    words.insert(words.end(), {output | (1u << 15) | (15u << 16), 0,
        (uint32_t(AluVectorOpcode::kMax) << 24) | (3u << 30)});
  return words;
}
}  // namespace x360n::test::spirv
