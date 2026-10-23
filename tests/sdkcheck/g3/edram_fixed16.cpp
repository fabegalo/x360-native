// patch 0185: EDRAM k_16_16[_16_16] is s5.10 fixed point (Xenia Canary 42c2b88).
// CPU-only: translate a real pixel shader with ROV (DXBC) and FSI (SPIR-V) and
// check the constants of the emitted EDRAM pack/unpack, plus the PSI clamp.
#include <rex/graphics/format/ucode.h>
#include <rex/graphics/pipeline/render_target/cache.h>
#include <rex/graphics/pipeline/shader/dxbc_translator.h>
#include <rex/graphics/pipeline/shader/spirv_translator.h>
#include <rex/string/buffer.h>

#include <bit>
#include <cstring>
#include <iostream>
#include <vector>

using namespace rex::graphics;

namespace {

// One color export of r0 (oC0 = max(r0, r0)).
std::vector<uint32_t> ColorPixelMicrocode() {
  using namespace rex::graphics::ucode;
  const uint64_t alloc = (uint64_t(AllocType::kPsColors) << 41) |
                         (uint64_t(ControlFlowOpcode::kAlloc) << 44);
  const uint64_t exec = 1 | (uint64_t(1) << 12) | (uint64_t(ControlFlowOpcode::kExecEnd) << 44);
  return {uint32_t(alloc), uint32_t(alloc >> 32) | uint32_t(exec << 16), uint32_t(exec >> 16),
          (1u << 15) | (15u << 16), 0, uint32_t(AluVectorOpcode::kMax) << 24};
}

bool HasFloat(const std::vector<uint8_t>& binary, float value) {
  const uint32_t bits = std::bit_cast<uint32_t>(value);
  for (size_t i = 0; i + 4 <= binary.size(); i += 4) {
    uint32_t word;
    std::memcpy(&word, binary.data() + i, 4);
    if (word == bits) return true;
  }
  return false;
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

  // Largest s5.10 value is 32767/1024; -32 is 0x8000.
  for (auto format : {xenos::ColorRenderTargetFormat::k_16_16,
                      xenos::ColorRenderTargetFormat::k_16_16_16_16}) {
    float rgb_low, alpha_low, rgb_high, alpha_high;
    uint32_t keep_low, keep_high;
    RenderTargetCache::GetPSIColorFormatInfo(format, 0b1111, rgb_low, alpha_low, rgb_high,
                                             alpha_high, keep_low, keep_high);
    check(rgb_low == -32.0f && alpha_low == -32.0f, "PSI fixed16 low clamp is -32");
    check(rgb_high == 32767.0f / 1024.0f && alpha_high == 32767.0f / 1024.0f,
          "PSI fixed16 high clamp is 32767/1024");
  }

  const auto code = ColorPixelMicrocode();
  {
    Shader shader(xenos::ShaderType::kPixel, 1, code.data(), code.size(), std::endian::native);
    rex::string::StringBuffer disassembly;
    shader.AnalyzeUcode(disassembly);
    check(shader.writes_color_targets() == 1, "DXBC microcode writes color 0");
    DxbcShaderTranslator translator(rex::ui::GraphicsProvider::GpuVendorID::kNvidia, false, true);
    auto& translation =
        *shader.GetOrCreateTranslation(translator.GetDefaultPixelShaderModification(1));
    check(translator.TranslateAnalyzedShader(translation) && translation.is_valid(),
          "DXBC ROV translation");
    const auto& binary = translation.translated_binary();
    check(!HasFloat(binary, 32767.0f / 32.0f), "DXBC ROV pack has no 32767/32 scale");
    check(!HasFloat(binary, 32.0f / 32767.0f), "DXBC ROV unpack has no 32/32767 scale");
    check(HasFloat(binary, 1024.0f) && HasFloat(binary, 1.0f / 1024.0f),
          "DXBC ROV uses 10 fractional bits");
  }
  for (unsigned scale : {1u, 2u}) {
    Shader shader(xenos::ShaderType::kPixel, 1, code.data(), code.size(), std::endian::native);
    rex::string::StringBuffer disassembly;
    shader.AnalyzeUcode(disassembly);
    SpirvShaderTranslator translator(SpirvShaderTranslator::Features(true), true, true, true,
                                     scale, scale);
    auto& translation =
        *shader.GetOrCreateTranslation(translator.GetDefaultPixelShaderModification(1));
    check(translator.TranslateAnalyzedShader(translation) && translation.is_valid(),
          "SPIR-V FSI translation");
    const auto& binary = translation.translated_binary();
    check(!HasFloat(binary, 32767.0f / 32.0f), "SPIR-V FSI pack has no 32767/32 scale");
    check(!HasFloat(binary, 32.0f / 32767.0f), "SPIR-V FSI unpack has no 32/32767 scale");
    check(HasFloat(binary, 32767.0f / 1024.0f), "SPIR-V FSI clamps to 32767/1024");
    check(HasFloat(binary, 1024.0f) && HasFloat(binary, 1.0f / 1024.0f),
          "SPIR-V FSI uses 10 fractional bits");
  }

  std::cout << (checks - failures) << '/' << checks << " checks passed\n";
  return failures ? 1 : 0;
}
