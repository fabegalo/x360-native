// patch 0189: stacked texture layer clamp in DXBC (Xenia Canary 04085efaa).
// CPU-only: translate real tfetch3D microcode and look for the emitted
// max(z, 0.5) / add(size, -0.5) / min sequence in the shader program.
#include <rex/graphics/format/ucode.h>
#include <rex/graphics/pipeline/shader/dxbc_translator.h>
#include <rex/string/buffer.h>

#include <algorithm>
#include <bit>
#include <cstring>
#include <iostream>
#include <vector>

using namespace rex::graphics;

namespace {

std::vector<uint32_t> Microcode(bool unnormalized, bool offset_z) {
  using namespace rex::graphics::ucode;
  const uint64_t alloc = (uint64_t(AllocType::kPsColors) << 41) |
                         (uint64_t(ControlFlowOpcode::kAlloc) << 44);
  const uint64_t exec = 1 | (uint64_t(2) << 12) | (uint64_t(1) << 16) |
                        (uint64_t(ControlFlowOpcode::kExecEnd) << 44);
  return {uint32_t(alloc), uint32_t(alloc >> 32) | uint32_t(exec << 16), uint32_t(exec >> 16),
          // tfetch3D r0.xyzw, r0.xyz, tf0, all filters from the fetch constant.
          uint32_t(FetchOpcode::kTextureFetch) | (uint32_t(unnormalized) << 25) | (0x24u << 26),
          0x688u | (3u << 12) | (3u << 14) | (3u << 16) | (7u << 18) | (3u << 24) | (3u << 26),
          (uint32_t(xenos::FetchOpDimension::k3DOrStacked) << 14) |
              (uint32_t(offset_z ? 2 : 0) << 26),
          // oC0 = max(r0, r0).
          (1u << 15) | (15u << 16), 0, uint32_t(AluVectorOpcode::kMax) << 24};
}

// Counts max(.., 0.5) immediately followed by add(.., -0.5) and min.
uint32_t CountLayerClamps(const std::vector<uint8_t>& blob) {
  std::vector<uint32_t> words(blob.size() / 4);
  std::memcpy(words.data(), blob.data(), words.size() * 4);
  if (words.size() < 8 || words[0] != 0x43425844) return 0;  // "DXBC"
  const uint32_t chunk_count = words[7];
  for (uint32_t i = 0; i < chunk_count; ++i) {
    const uint32_t chunk = words[8 + i] / 4;
    if (words[chunk] != 0x58454853 && words[chunk] != 0x52444853) continue;  // SHEX / SHDR
    const uint32_t begin = chunk + 2, end = std::min<size_t>(begin + words[begin + 1], words.size());
    struct Op {
      uint32_t opcode;
      bool half, minus_half;
    };
    std::vector<Op> ops;
    for (uint32_t at = begin + 2; at < end;) {
      const uint32_t opcode = words[at] & 0x7FF;
      uint32_t length = (words[at] >> 24) & 0x7F;
      if (opcode == 0x35) length = words[at + 1];  // customdata
      if (!length) break;
      Op op{opcode, false, false};
      for (uint32_t w = at + 1; w < at + length && w < end; ++w) {
        op.half |= words[w] == std::bit_cast<uint32_t>(0.5f);
        op.minus_half |= words[w] == std::bit_cast<uint32_t>(-0.5f);
      }
      ops.push_back(op);
      at += length;
    }
    uint32_t count = 0;
    for (size_t j = 0; j + 2 < ops.size(); ++j) {
      count += ops[j].opcode == 0x34 /* max */ && ops[j].half &&
               ops[j + 1].opcode == 0x00 /* add */ && ops[j + 1].minus_half &&
               ops[j + 2].opcode == 0x33 /* min */;
    }
    return count;
  }
  return 0;
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
  for (bool unnormalized : {false, true}) {
    for (bool offset_z : {false, true}) {
      const auto code = Microcode(unnormalized, offset_z);
      Shader shader(xenos::ShaderType::kPixel, 1, code.data(), code.size(), std::endian::native);
      rex::string::StringBuffer disassembly;
      shader.AnalyzeUcode(disassembly);
      DxbcShaderTranslator translator(rex::ui::GraphicsProvider::GpuVendorID::kNvidia, false,
                                      false);
      auto& translation =
          *shader.GetOrCreateTranslation(translator.GetDefaultPixelShaderModification(1));
      const bool translated =
          translator.TranslateAnalyzedShader(translation) && translation.is_valid();
      check(translated, "tfetch3D DXBC translation");
      const char* name = unnormalized ? (offset_z ? "unnormalized offset Z clamped"
                                                  : "unnormalized Z clamped")
                                      : (offset_z ? "normalized offset Z clamped"
                                                  : "normalized Z clamped");
      check(translated && CountLayerClamps(translation.translated_binary()) >= 1, name);
    }
  }
  std::cout << (checks - failures) << '/' << checks << " checks passed\n";
  return failures ? 1 : 0;
}
