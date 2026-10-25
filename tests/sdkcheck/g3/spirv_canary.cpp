// patch 0188: SPIR-V fixes from Xenia Canary (e519d59e4, 2d5b41080, 654a8cacf).
// patch 0193: locked mip grid for unnormalized fetches (8486e97a0).
// patch 0194: out-of-bounds vertex fetch words read 0 (9e9d3cdd3).
// CPU-only: translate real guest microcode and check the emitted SPIR-V data
// flow. No Vulkan device or game is used.
#include <SPIRV/GLSL.std.450.h>
#include <rex/graphics/format/ucode.h>
#include <rex/graphics/pipeline/shader/spirv_translator.h>
#include <rex/string/buffer.h>

#include <bit>
#include <cstring>
#include <functional>
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
        case spv::OpFMul:
        case spv::OpFOrdGreaterThanEqual:
        case spv::OpBitwiseAnd:
        case spv::OpBitwiseOr:
        case spv::OpVectorTimesScalar:
        case spv::OpBitFieldUExtract:
        case spv::OpBitFieldInsert:
        case spv::OpBitReverse:
        case spv::OpShiftRightLogical:
        case spv::OpPhi:
        case spv::OpLoad:
        case spv::OpAccessChain:
        case spv::OpCompositeExtract:
        case spv::OpExtInst:
        case spv::OpIEqual:
        case spv::OpULessThan:
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

std::vector<uint32_t> Microcode(bool texture_fetch_3d,
                                xenos::FetchOpDimension dimension =
                                    xenos::FetchOpDimension::k3DOrStacked,
                                bool unnormalized = false) {
  using namespace rex::graphics::ucode;
  const uint64_t alloc = (uint64_t(AllocType::kPsColors) << 41) |
                         (uint64_t(ControlFlowOpcode::kAlloc) << 44);
  const uint32_t count = texture_fetch_3d ? 2 : 1;
  const uint64_t exec = 1 | (uint64_t(count) << 12) | (uint64_t(texture_fetch_3d) << 16) |
                        (uint64_t(ControlFlowOpcode::kExecEnd) << 44);
  std::vector<uint32_t> code{uint32_t(alloc), uint32_t(alloc >> 32) | uint32_t(exec << 16),
                             uint32_t(exec >> 16)};
  if (texture_fetch_3d) {
    // tfetch r0.xyzw, r0.xyz, tf0, all filters from the fetch constant.
    code.insert(code.end(),
                {uint32_t(FetchOpcode::kTextureFetch) | (uint32_t(unnormalized) << 25) |
                     (0x24u << 26),
                 0x688u | (3u << 12) | (3u << 14) | (3u << 16) | (7u << 18) | (3u << 24) |
                     (3u << 26),
                 uint32_t(dimension) << 14});
  }
  // oC0 = max(r0, r0).
  code.insert(code.end(), {(1u << 15) | (15u << 16), 0, uint32_t(AluVectorOpcode::kMax) << 24});
  return code;
}

// vfetch_full r1.xyzw, r0.x, vf0, k_32_32_32_32_FLOAT, stride 4 dwords.
std::vector<uint32_t> VertexFetchMicrocode() {
  using namespace rex::graphics::ucode;
  const uint64_t exec = 1 | (uint64_t(1) << 12) | (uint64_t(1) << 16) |
                        (uint64_t(ControlFlowOpcode::kExecEnd) << 44);
  return {uint32_t(exec), uint32_t(exec >> 32), 0,
          uint32_t(FetchOpcode::kVertexFetch) | (1u << 12) | (1u << 19),
          0x688u | (uint32_t(xenos::VertexFormat::k_32_32_32_32_FLOAT) << 16), 4};
}

std::vector<uint8_t> Translate(SpirvShaderTranslator& translator, const std::vector<uint32_t>& code,
                               xenos::ShaderType type = xenos::ShaderType::kPixel,
                               bool param_gen = false) {
  Shader shader(type, 1, code.data(), code.size(), std::endian::native);
  rex::string::StringBuffer disassembly;
  shader.AnalyzeUcode(disassembly);
  uint64_t modification = type == xenos::ShaderType::kPixel
      ? translator.GetDefaultPixelShaderModification(2) : translator.GetDefaultVertexShaderModification(2);
  if (param_gen && type == xenos::ShaderType::kPixel) {
    SpirvShaderTranslator::Modification m(modification);
    m.pixel.param_gen_enable = 1;
    m.pixel.param_gen_interpolator = 0;
    modification = m.value;
  }
  auto& translation = *shader.GetOrCreateTranslation(modification);
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

  {
    SpirvShaderTranslator translator(SpirvShaderTranslator::Features(true), true, true, false);
    const auto binary =
        Translate(translator, Microcode(true, xenos::FetchOpDimension::k2D, true));
    check(!binary.empty(), "unnormalized tfetch2D translation");
    const Module module(binary);
    // 8486e97a0: size = locked ? max(size >> mip_min, 1) : size, locked when
    // mip_min (word 4 bits 2:5) equals mip_max (bits 6:9).
    uint32_t locked_selects = 0;
    for (const auto& instruction : module.instructions) {
      if (instruction.op != spv::OpSelect || instruction.args.size() != 5) continue;
      const auto* locked = module.Def(instruction.args[2], spv::OpIEqual);
      if (!locked) continue;
      const auto* mip_min = module.Def(locked->args[2], spv::OpBitFieldUExtract);
      const auto* mip_max = module.Def(locked->args[3], spv::OpBitFieldUExtract);
      locked_selects += mip_min && mip_max && module.IsConstant(mip_min->args[3], 2) &&
                        module.IsConstant(mip_max->args[3], 6);
    }
    check(locked_selects >= 2, "unnormalized 2D size uses the locked mip grid");
  }

  {
    SpirvShaderTranslator translator(SpirvShaderTranslator::Features(true), true, true, false);
    const auto binary =
        Translate(translator, VertexFetchMicrocode(), xenos::ShaderType::kVertex);
    check(!binary.empty(), "vfetch translation");
    const Module module(binary);
    // 9e9d3cdd3: each word is select(address < base + size, word, 0).
    uint32_t bounded_words = 0;
    for (const auto& instruction : module.instructions) {
      if (instruction.op != spv::OpSelect || instruction.args.size() != 5) continue;
      bounded_words += module.Def(instruction.args[2], spv::OpULessThan) &&
                       module.IsConstant(instruction.args[4], 0);
    }
    check(bounded_words >= 4, "out-of-bounds vertex fetch words read 0");
  }

  for (bool native2 : {false, true}) for (unsigned scale : {1u, 2u}) {
    // 2x MSAA emulated as 4x (no native 2x without attachments).
    SpirvShaderTranslator translator(SpirvShaderTranslator::Features(true), true, native2, true,
                                     scale, scale);
    const auto binary = Translate(translator, Microcode(false));
    check(!binary.empty(), "FSI translation");
    const Module module(binary);
    // cc4981a: interlock guest 2x takes host samples 2 and 1. This
    // supersedes 0188's mapping; inspect the translated data flow.
    uint32_t paired = 0;
    for (const auto& instruction : module.instructions) {
      if (instruction.op != spv::OpBitFieldInsert || instruction.args.size() != 6 ||
          !module.IsConstant(instruction.args[4], 1) ||
          !module.IsConstant(instruction.args[5], 31)) {
        continue;
      }
      const auto* extract = module.Def(instruction.args[3], spv::OpBitFieldUExtract);
      if (!extract || !module.IsConstant(extract->args[4], 1)) continue;
      const auto* base = module.Def(instruction.args[2], spv::OpBitFieldUExtract);
      paired += base && module.IsConstant(base->args[3], 2) &&
                module.IsConstant(base->args[4], 1) && module.IsConstant(extract->args[3], 1);
    }
    if (!native2) check(paired >= 1, "2x-as-4x guest samples come from host samples 2 and 1");
    // Evaluate the actual translated coverage expression for every input mask.
    // Input mask and MSAA selection are the only external values of this DAG.
    bool evaluated = false;
    for (const auto& phi : module.instructions) {
      if (phi.op != spv::OpPhi || phi.args.size() != 8) continue;
      const auto* two = module.Def(phi.args[4], native2 ? spv::OpShiftRightLogical
                                                     : spv::OpBitFieldInsert);
      if (!two) continue;
      const uint32_t input = phi.args[2];
      std::function<uint32_t(uint32_t, uint32_t, unsigned)> eval =
          [&](uint32_t id, uint32_t mask, unsigned depth) -> uint32_t {
        if (id == input) return mask;
        if (module.constants.contains(id)) return module.constants.at(id);
        if (depth > 16 || !module.defs.contains(id)) throw 1;
        const auto& op = module.instructions[module.defs.at(id)];
        auto arg = [&](unsigned i) { return eval(op.args[i], mask, depth + 1); };
        if (op.op == spv::OpShiftRightLogical) return arg(2) >> arg(3);
        if (op.op == spv::OpBitReverse) {
          uint32_t x = arg(2), reversed = 0;
          for (unsigned i = 0; i < 32; ++i) reversed |= ((x >> i) & 1) << (31 - i);
          return reversed;
        }
        if (op.op == spv::OpBitFieldUExtract) {
          auto width = arg(4); return (arg(2) >> arg(3)) & (width == 32 ? ~0u : (1u << width) - 1);
        }
        if (op.op == spv::OpBitFieldInsert) {
          auto width = arg(5), offset = arg(4);
          uint32_t bits = (width == 32 ? ~0u : (1u << width) - 1) << offset;
          return (arg(2) & ~bits) | ((arg(3) << offset) & bits);
        }
        throw 1;
      };
      try {
        for (uint32_t mask = 0; mask < 16; ++mask) {
          const uint32_t expected2 = native2 ? ((mask >> 1) & 1) | ((mask & 1) << 1)
                                           : ((mask >> 2) & 1) | (mask & 2);
          const uint32_t expected4 = (mask & 5) | (((mask >> 3) & 1) << 1) |
                                    (((mask >> 1) & 1) << 3);
          check(eval(phi.args[4], mask, 0) == expected2, "translated 2x coverage truth table");
          check(eval(phi.args[6], mask, 0) == expected4, "translated 4x coverage truth table");
          if (mask < 2) check(eval(input, mask, 0) == mask, "translated 1x coverage unchanged");
        }
        evaluated = true; break;
      } catch (int) { continue; }
    }
    check(evaluated, "translated MSAA coverage expression evaluated");
  }

  for (bool interlock : {false, true}) for (bool native2 : {false, true}) {
    SpirvShaderTranslator translator(SpirvShaderTranslator::Features(true), native2, native2, interlock);
    Module module(Translate(translator, Microcode(false)));
    unsigned samples_checked = 0;
    for (const auto& select : module.instructions) {
      if (select.op != spv::OpSelect || select.args.size() != 5) continue;
      const auto* covered = module.Def(select.args[2], spv::OpFOrdGreaterThanEqual);
      if (!covered) continue;
      const auto* threshold = module.Def(covered->args[3], spv::OpFSub);
      if (!threshold || !module.constants.contains(threshold->args[2])) continue;
      const auto* offset = module.Def(threshold->args[3], spv::OpFMul);
      if (!offset || !module.constants.contains(offset->args[3])) continue;
      const float base = std::bit_cast<float>(module.constants.at(threshold->args[2]));
      const float step = std::bit_cast<float>(module.constants.at(offset->args[3]));
      const auto* mask = module.Def(select.args[interlock ? 4 : 3],
                                   interlock ? spv::OpBitwiseAnd : spv::OpBitwiseOr);
      if (!mask || !module.constants.contains(mask->args[3])) continue;
      unsigned guest = 0, host = 0;
      if (step == 0.0625f) {
        if (base == 0.75f) guest = 0;
        else if (base == 0.25f) guest = 1;
        else if (base == 0.5f) guest = 2;
        else if (base == 1.f) guest = 3;
        else continue;
        host = guest;  // Host RT stores/resolves 4x samples in guest index order.
      } else if (step == 0.125f && (base == 0.5f || base == 1.f)) {
        guest = base == 0.5f ? 0 : 1;
        host = native2 ? guest ^ 1u : (guest ? 3 : 0);
      } else if (step != 0.25f || base != 1.f) continue;
      const uint32_t expected = interlock ? ~((1u << guest) | (1u << (guest + 4))) : 1u << host;
      check(module.IsConstant(mask->args[3], expected),
            "A2C translated threshold masks the matching guest/host and deferred depth sample");
      ++samples_checked;
    }
    check(samples_checked == 7, "A2C translated thresholds cover 1x, 2x and 4x");
  }
  for (bool interlock : {false, true}) for (unsigned scale : {1u, 2u}) {
    SpirvShaderTranslator translator(SpirvShaderTranslator::Features(true), true, true, interlock, scale, scale);
    const Module module(Translate(translator, Microcode(true), xenos::ShaderType::kPixel, true));
    uint32_t member = UINT32_MAX;
    for (const auto& op : module.instructions) {
      if (op.op == spv::OpMemberName && op.args.size() > 2 &&
          std::string(reinterpret_cast<const char*>(op.args.data() + 2)) == "param_gen_window_offset")
        member = op.args[1];
    }
    unsigned axes = 0;
    for (const auto& op : module.instructions) {
      if (op.op != spv::OpFAdd || op.args.size() != 4) continue;
      const auto* extract = module.Def(op.args[3], spv::OpCompositeExtract);
      if (!extract || extract->args.size() != 4 || extract->args[3] > 1) continue;
      const auto* load = module.Def(extract->args[2], spv::OpLoad);
      const auto* access = load ? module.Def(load->args[2], spv::OpAccessChain) : nullptr;
      if (access && module.IsConstant(access->args.back(), member)) axes |= 1u << extract->args[3];
    }
    check(axes == 3, "PsParamGen adds both original window offsets in translated SPIR-V");
  }
  std::cout << (checks - failures) << '/' << checks << " checks passed\n";
  return failures ? 1 : 0;
}
