// patch 0189: stacked texture layer clamp in DXBC (Xenia Canary 04085efaa).
// patch 0193: locked mip grid for unnormalized fetches (8486e97a0).
// patch 0194: out-of-bounds vertex fetch words read 0 (9e9d3cdd3).
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
#include <string>
#include <d3dcompiler.h>
#include <windows.h>

using namespace rex::graphics;

namespace {

std::vector<uint32_t> Microcode(bool unnormalized, bool offset_z,
                                xenos::FetchOpDimension dimension =
                                    xenos::FetchOpDimension::k3DOrStacked) {
  using namespace rex::graphics::ucode;
  const uint64_t alloc = (uint64_t(AllocType::kPsColors) << 41) |
                         (uint64_t(ControlFlowOpcode::kAlloc) << 44);
  const uint64_t exec = 1 | (uint64_t(2) << 12) | (uint64_t(1) << 16) |
                        (uint64_t(ControlFlowOpcode::kExecEnd) << 44);
  return {uint32_t(alloc), uint32_t(alloc >> 32) | uint32_t(exec << 16), uint32_t(exec >> 16),
          // tfetch3D r0.xyzw, r0.xyz, tf0, all filters from the fetch constant.
          uint32_t(FetchOpcode::kTextureFetch) | (uint32_t(unnormalized) << 25) | (0x24u << 26),
          0x688u | (3u << 12) | (3u << 14) | (3u << 16) | (7u << 18) | (3u << 24) | (3u << 26),
          (uint32_t(dimension) << 14) |
              (uint32_t(offset_z ? 2 : 0) << 26),
          // oC0 = max(r0, r0).
          (1u << 15) | (15u << 16), 0, uint32_t(AluVectorOpcode::kMax) << 24};
}

struct Op {
  uint32_t opcode;
  bool half, minus_half;
};

std::vector<Op> ProgramOps(const std::vector<uint8_t>& blob) {
  std::vector<uint32_t> words(blob.size() / 4);
  std::memcpy(words.data(), blob.data(), words.size() * 4);
  if (words.size() < 8 || words[0] != 0x43425844) return {};  // "DXBC"
  const uint32_t chunk_count = words[7];
  for (uint32_t i = 0; i < chunk_count; ++i) {
    const uint32_t chunk = words[8 + i] / 4;
    if (words[chunk] != 0x58454853 && words[chunk] != 0x52444853) continue;  // SHEX / SHDR
    const uint32_t begin = chunk + 2, end = std::min<size_t>(begin + words[begin + 1], words.size());
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
    return ops;
  }
  return {};
}

// Counts max(.., 0.5) immediately followed by add(.., -0.5) and min.
uint32_t CountLayerClamps(const std::vector<uint8_t>& blob) {
  const auto ops = ProgramOps(blob);
  uint32_t count = 0;
  for (size_t j = 0; j + 2 < ops.size(); ++j) {
    count += ops[j].opcode == 0x34 /* max */ && ops[j].half &&
             ops[j + 1].opcode == 0x00 /* add */ && ops[j + 1].minus_half &&
             ops[j + 2].opcode == 0x33 /* min */;
  }
  return count;
}

// Counts ubfe (mip min/max), ieq, ushr, umax, movc: the locked mip size.
uint32_t CountLockedMipSizes(const std::vector<uint8_t>& blob) {
  const auto ops = ProgramOps(blob);
  const uint32_t sequence[] = {0x8A, 0x20, 0x55, 0x53, 0x37};
  uint32_t count = 0;
  for (size_t j = 0; j + std::size(sequence) <= ops.size(); ++j) {
    bool match = true;
    for (size_t k = 0; k < std::size(sequence); ++k) match &= ops[j + k].opcode == sequence[k];
    count += match;
  }
  return count;
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

// Counts ubfe (size), and (base), umad (end), iadd (word addresses), ult.
uint32_t CountVertexFetchBounds(const std::vector<uint8_t>& blob) {
  const auto ops = ProgramOps(blob);
  const uint32_t sequence[] = {0x8A, 0x01, 0x52, 0x1E, 0x4F};
  uint32_t count = 0;
  for (size_t j = 0; j + std::size(sequence) <= ops.size(); ++j) {
    bool match = true;
    for (size_t k = 0; k < std::size(sequence); ++k) match &= ops[j + k].opcode == sequence[k];
    count += match;
  }
  return count;
}

std::vector<uint8_t> Translate(const std::vector<uint32_t>& code,
                                xenos::ShaderType type = xenos::ShaderType::kPixel,
                                bool rov = false, bool param_gen = false) {
  Shader shader(type, 1, code.data(), code.size(), std::endian::native);
  rex::string::StringBuffer disassembly;
  shader.AnalyzeUcode(disassembly);
  DxbcShaderTranslator translator(rex::ui::GraphicsProvider::GpuVendorID::kNvidia, false, rov);
  uint64_t modification = type == xenos::ShaderType::kPixel
      ? translator.GetDefaultPixelShaderModification(2) : translator.GetDefaultVertexShaderModification(2);
  if (param_gen && type == xenos::ShaderType::kPixel) {
    DxbcShaderTranslator::Modification m(modification);
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
  {
    const auto blob = Translate(Microcode(true, false, xenos::FetchOpDimension::k2D));
    check(!blob.empty(), "unnormalized tfetch2D DXBC translation");
    check(CountLockedMipSizes(blob) >= 1, "unnormalized 2D size uses the locked mip grid");
    const auto normalized = Translate(Microcode(false, false, xenos::FetchOpDimension::k2D));
    check(!normalized.empty() && !CountLockedMipSizes(normalized),
          "normalized 2D size is unchanged");
  }
  {
    const auto blob = Translate(VertexFetchMicrocode(), xenos::ShaderType::kVertex);
    check(!blob.empty(), "vfetch DXBC translation");
    check(CountVertexFetchBounds(blob) >= 1, "out-of-bounds vertex fetch words read 0");
  }
  if (HMODULE compiler = LoadLibraryW(L"D3DCompiler_47.dll")) {
    auto disassemble = reinterpret_cast<pD3DDisassemble>(GetProcAddress(compiler, "D3DDisassemble"));
    for (bool rov : {false, true}) {
      const auto blob = Translate(Microcode(false, false), xenos::ShaderType::kPixel, rov);
      ID3DBlob* assembly = nullptr;
      const bool valid = disassemble && !blob.empty() && SUCCEEDED(disassemble(
          blob.data(), blob.size(), 0, nullptr, &assembly));
      check(valid, "real RTV/ROV DXBC translation accepted by D3DDisassemble");
      if (assembly) {
        const std::string text(static_cast<const char*>(assembly->GetBufferPointer()), assembly->GetBufferSize());
        if (rov) check(text.find("vCoverage") != std::string::npos,
                       "ROV DXBC includes raster coverage input");
        assembly->Release();
      }
    }
    for (bool rov : {false, true}) {
      const auto blob = Translate(Microcode(false,false), xenos::ShaderType::kPixel, rov, true);
      ID3DBlob* assembly=nullptr;
      bool valid=disassemble&&!blob.empty()&&SUCCEEDED(disassemble(blob.data(),blob.size(),0,nullptr,&assembly));
      check(valid,"PsParamGen RTV/ROV DXBC validates");
      if(assembly) {
        std::string text(static_cast<const char*>(assembly->GetBufferPointer()),assembly->GetBufferSize());
        const unsigned slot=offsetof(DxbcShaderTranslator::SystemConstants,param_gen_window_offset)/16;
        const std::string operand="]["+std::to_string(slot)+"].xy";
        bool adds=false;
        for(size_t pos=0;pos<text.size();) {
          auto end=text.find('\n',pos); if(end==std::string::npos)end=text.size();
          auto line=text.substr(pos,end-pos);
          adds|=line.find("add ")!=std::string::npos&&line.find(operand)!=std::string::npos;
          pos=end+1;
        }
        check(adds,"PsParamGen adds original XY offset from actual DXBC constant slot");
        assembly->Release();
      }
    }
    FreeLibrary(compiler);
  } else {
    std::cout << "D3DDisassemble unavailable; DXBC disassembly check skipped\n";
  }
  std::cout << (checks - failures) << '/' << checks << " checks passed\n";
  return failures ? 1 : 0;
}
