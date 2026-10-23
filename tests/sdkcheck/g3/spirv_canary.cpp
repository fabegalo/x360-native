// patch 0188: SPIR-V fixes from Xenia Canary (e519d59e4, 2d5b41080, 654a8cacf).
// CPU-only: translate real guest microcode and check the emitted SPIR-V data
// flow. No Vulkan device or game is used.
#include <SPIRV/GLSL.std.450.h>
#include <rex/graphics/format/ucode.h>
#include <rex/graphics/pipeline/shader/spirv_translator.h>
#include <rex/string/buffer.h>

#include <bit>
#include <cstring>
#include <iostream>
#include <unordered_map>
#include <vector>

using namespace rex::graphics;

namespace {

struct Module {
  struct Instruction {
    spv::Op op;
    std::vector<uint32_t> args;
  };
  std::vector<Instruction> instructions;
  std::unordered_map<uint32_t, size_t> defs;  // Result ID -> instruction.
  std::unordered_map<uint32_t, uint32_t> constants;

  explicit Module(const std::vector<uint8_t>& binary) {
    std::vector<uint32_t> words(binary.size() / 4);
    std::memcpy(words.data(), binary.data(), words.size() * 4);
    for (size_t i = 5; i < words.size();) {
      const uint32_t count = words[i] >> 16;
      if (!count) break;
      Instruction instruction{spv::Op(words[i] & 0xFFFF),
                              {words.begin() + i + 1, words.begin() + i + count}};
      if (instruction.op == spv::OpConstant) constants[instruction.args[1]] = instruction.args[2];
      // Only value instructions checked below; decorations and names also
      // have a second word, which is not a result ID.
      switch (instruction.op) {
        case spv::OpConstant:
        case spv::OpFAdd:
        case spv::OpFSub:
        case spv::OpVectorTimesScalar:
        case spv::OpBitFieldUExtract:
        case spv::OpExtInst:
          defs[instruction.args[1]] = instructions.size();
          break;
        default:
          break;
      }
      instructions.push_back(std::move(instruction));
      i += count;
    }
  }
  const Instruction* Def(uint32_t id, spv::Op op) const {
    auto it = defs.find(id);
    if (it == defs.end() || instructions[it->second].op != op) return nullptr;
    return &instructions[it->second];
  }
  bool IsConstant(uint32_t id, uint32_t bits) const {
    auto it = constants.find(id);
    return it != constants.end() && it->second == bits;
  }
};

std::vector<uint32_t> Microcode(bool texture_fetch_3d) {
  using namespace rex::graphics::ucode;
  const uint64_t alloc = (uint64_t(AllocType::kPsColors) << 41) |
                         (uint64_t(ControlFlowOpcode::kAlloc) << 44);
  const uint32_t count = texture_fetch_3d ? 2 : 1;
  const uint64_t exec = 1 | (uint64_t(count) << 12) | (uint64_t(texture_fetch_3d) << 16) |
                        (uint64_t(ControlFlowOpcode::kExecEnd) << 44);
  std::vector<uint32_t> code{uint32_t(alloc), uint32_t(alloc >> 32) | uint32_t(exec << 16),
                             uint32_t(exec >> 16)};
  if (texture_fetch_3d) {
    // tfetch3D r0.xyzw, r0.xyz, tf0, all filters from the fetch constant.
    code.insert(code.end(),
                {uint32_t(FetchOpcode::kTextureFetch) | (0x24u << 26),
                 0x688u | (3u << 12) | (3u << 14) | (3u << 16) | (7u << 18) | (3u << 24) |
                     (3u << 26),
                 uint32_t(xenos::FetchOpDimension::k3DOrStacked) << 14});
  }
  // oC0 = max(r0, r0).
  code.insert(code.end(), {(1u << 15) | (15u << 16), 0, uint32_t(AluVectorOpcode::kMax) << 24});
  return code;
}

std::vector<uint8_t> Translate(SpirvShaderTranslator& translator, const std::vector<uint32_t>& code) {
  Shader shader(xenos::ShaderType::kPixel, 1, code.data(), code.size(), std::endian::native);
  rex::string::StringBuffer disassembly;
  shader.AnalyzeUcode(disassembly);
  auto& translation =
      *shader.GetOrCreateTranslation(translator.GetDefaultPixelShaderModification(1));
  if (!translator.TranslateAnalyzedShader(translation) || !translation.is_valid()) return {};
  return translation.translated_binary();
}

}  // namespace

int main() {
  int failures = 0, checks = 0;
  auto check = [&](bool ok, const char* name) {
    ++checks;
    if (!ok) {
      ++failures;
      std::cerr << "FAIL " << name << '\n';
    }
  };
  const uint32_t float_0 = std::bit_cast<uint32_t>(0.0f);
  const uint32_t float_1 = std::bit_cast<uint32_t>(1.0f);

  {
    SpirvShaderTranslator translator(SpirvShaderTranslator::Features(true), true, true, false);
    const auto binary = Translate(translator, Microcode(true));
    check(!binary.empty(), "tfetch3D translation");
    const Module module(binary);
    // e519d59e4: layer lerp is first + (second - first) * factor.
    uint32_t lerp_good = 0, lerp_bad = 0;
    // 2d5b41080: NClamp(layer, 0, size - 1).
    uint32_t layer_clamps = 0;
    for (const auto& instruction : module.instructions) {
      if (instruction.op == spv::OpFAdd && instruction.args.size() == 4) {
        const auto* scaled = module.Def(instruction.args[3], spv::OpVectorTimesScalar);
        const auto* difference = scaled ? module.Def(scaled->args[2], spv::OpFSub) : nullptr;
        if (difference) {
          lerp_good += instruction.args[2] == difference->args[3];
          lerp_bad += instruction.args[2] == difference->args[2];
        }
      }
      if (instruction.op == spv::OpExtInst && instruction.args.size() == 7 &&
          instruction.args[3] == GLSLstd450NClamp &&
          module.IsConstant(instruction.args[5], float_0)) {
        const auto* size_minus_1 = module.Def(instruction.args[6], spv::OpFSub);
        layer_clamps += size_minus_1 && module.IsConstant(size_minus_1->args[3], float_1);
      }
    }
    check(lerp_good >= 1 && !lerp_bad, "stacked layer lerp starts from the first layer");
    check(layer_clamps >= 1, "stacked layer index is clamped to [0, size - 1]");
  }

  for (unsigned scale : {1u, 2u}) {
    // 2x MSAA emulated as 4x (no native 2x without attachments).
    SpirvShaderTranslator translator(SpirvShaderTranslator::Features(true), true, false, true,
                                     scale, scale);
    const auto binary = Translate(translator, Microcode(false));
    check(!binary.empty(), "FSI translation");
    const Module module(binary);
    // 654a8cacf: guest sample 1 is host sample 3 with 2x-as-4x.
    uint32_t from_3 = 0, from_other = 0;
    for (const auto& instruction : module.instructions) {
      if (instruction.op != spv::OpBitFieldInsert || instruction.args.size() != 6 ||
          !module.IsConstant(instruction.args[4], 1) ||
          !module.IsConstant(instruction.args[5], 31)) {
        continue;
      }
      const auto* extract = module.Def(instruction.args[3], spv::OpBitFieldUExtract);
      if (!extract || !module.IsConstant(extract->args[4], 1)) continue;
      ++(module.IsConstant(extract->args[3], 3) ? from_3 : from_other);
    }
    check(from_3 >= 1 && !from_other, "2x-as-4x guest sample 1 comes from host sample 3");
  }

  std::cout << (checks - failures) << '/' << checks << " checks passed\n";
  return failures ? 1 : 0;
}
