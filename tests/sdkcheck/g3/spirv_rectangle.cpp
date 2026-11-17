// Offline regression for rectangle replay entering the guest control-flow loop.
// The source shaders are synthetic; optional captures remain outside the repo.
#include "../spirv_controlflow_cases.h"
#include <rex/xenos/ucode.h>
#include <gpu/xenos/pipeline/shader/spirv_translator.h>
#include <rex/string/text.h>
#include <array>
#include <bit>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace rex::gpu;
namespace xenos=rex::xenos;
namespace ucode=rex::xenos::ucode;
namespace {
using namespace x360n::test::spirv;
rex::gpu::ShaderModificationKey CaptureModification(const std::string& text) {
  Require(text.size() == 16 || text.size() == 32, "capture modification width");
  if (text.size() == 32)
    return {std::stoull(text.substr(16), nullptr, 16), std::stoull(text.substr(0, 16), nullptr, 16)};
  const auto legacy = std::stoull(text, nullptr, 16);
  rex::gpu::ShaderModificationKey result{legacy & ((uint64_t(1) << 49) - 1),
                                            ((legacy >> 49) & 1) << 16};
  for (uint32_t bit = 60; bit < 64; ++bit)
    result.high |= ((legacy >> bit) & 1) << (63 - bit);
  return result;
}
void Dump(const std::filesystem::path& directory, const std::string& name,
          const std::vector<uint8_t>& binary) {
  if (directory.empty()) return;
  std::filesystem::create_directories(directory);
  std::ofstream output(directory / (name + ".spv"), std::ios::binary);
  output.write(reinterpret_cast<const char*>(binary.data()), std::streamsize(binary.size()));
  Require(bool(output), "SPIR-V evidence write");
}
}

int CheckRectangleControlFlow(const char* dump_directory, const char* capture_directory) {
  Require(CaptureModification("F002000000000001") == rex::gpu::ShaderModificationKey{1, 0x1000F},
          "legacy capture flags migrate to the local word");
  Require(CaptureModification("800000000001000F8000000000000001") ==
              rex::gpu::ShaderModificationKey{0x8000000000000001, 0x800000000001000F},
          "128-bit capture identity preserves both words");
  const std::filesystem::path output = dump_directory ? dump_directory : "";
  SPIRVShaderTranslator::Features features(true);
  SPIRVShaderTranslator translator(features, true, true, true);
  unsigned passed = 0, failed = 0;
  for (const bool rectangle : {false, true}) for (unsigned jump = 0; jump < 5; ++jump)
    for (const unsigned mask : {0u, 1u, 7u}) {
      const auto name = "rectangle" + std::to_string(rectangle) + "-jump" + std::to_string(jump) + "-mask" + std::to_string(mask);
      try {
      const auto code = VertexMicrocode(jump);
      Shader shader(xenos::ShaderType::kVertex, 1, code.data(), code.size(), std::endian::native);
      rex::string::StringBuffer disassembly;
      shader.AnalyzeUcode(disassembly, {});
      Require(shader.GetWritesInterpolators() == 7 && (!shader.GetLabelAddresses().empty() == bool(jump)),
              "synthetic shader did not activate intended branch/interpolator paths");
      SPIRVShaderTranslator::Modification modification(translator.GetDefaultVertexShaderModification(0,
          rectangle ? Shader::HostVertexShaderType::kRectangleListAsTriangleStrip : Shader::HostVertexShaderType::kVertex));
      modification.vertex.interpolator_mask = mask;
      auto& translation = *shader.GetOrCreateTranslation(modification.value);
      Require(translator.TranslateAnalyzedShader(translation) && translation.IsValid(), "rectangle translation failed");
      const auto& binary = translation.GetTranslatedBinary();
      Dump(output, name, binary);
      CheckPhiPredecessors(binary, rectangle && jump);
      ++passed;
      } catch (const std::exception& error) {
        ++failed; std::cerr << name << ": " << error.what() << '\n';
      }
    }
  // Retranslate captures with their exact modification keys. No proprietary
  // bytes or shader identities are embedded in this regression fixture.
  if (capture_directory) for (const auto& file : std::filesystem::directory_iterator(capture_directory)) {
    const auto name = file.path().filename().string();
    const auto marker = name.find(".native-vulkan.bin.");
    if (!name.starts_with("shader_") || (marker != 40 && marker != 56)) continue;
    try {
    const auto hash_text = name.substr(7, 16);
    const auto type_text = name.substr(marker + 19);
    Require(type_text == "vert" || type_text == "frag", "capture shader stage");
    const auto path = file.path().parent_path() / ("shader_" + hash_text + ".ucode.bin." + type_text);
    const auto bytes = std::filesystem::file_size(path);
    Require(bytes && bytes % 4 == 0 && bytes <= 262144, "capture microcode extent");
    std::vector<uint32_t> code(bytes / 4);
    std::ifstream input(path, std::ios::binary);
    Require(bool(input.read(reinterpret_cast<char*>(code.data()), std::streamsize(bytes))), "capture microcode read");
    Shader shader(type_text == "vert" ? xenos::ShaderType::kVertex : xenos::ShaderType::kPixel,
        std::stoull(hash_text, nullptr, 16), code.data(), code.size(), std::endian::native);
    rex::string::StringBuffer disassembly;
    shader.AnalyzeUcode(disassembly, {});
    const auto modification = CaptureModification(name.substr(24, marker - 24));
    auto& translation = *shader.GetOrCreateTranslation(modification);
    Require(translator.TranslateAnalyzedShader(translation) && translation.IsValid(), "capture retranslation failed");
    const auto& binary = translation.GetTranslatedBinary();
    Dump(output, name.substr(0, marker) + ".retranslated", binary);
    CheckPhiPredecessors(binary, false);
    ++passed;
    } catch (const std::exception& error) {
      ++failed; std::cerr << name << ": " << error.what() << '\n';
    }
  }
  std::cout << "SPIR-V rectangle CFG: " << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
