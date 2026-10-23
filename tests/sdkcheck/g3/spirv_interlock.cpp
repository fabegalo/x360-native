// CPU-only: translate real guest microcode and walk the emitted SPIR-V CFG.
// SPIRV-Tools checks interlock execution modes, but not the dynamic exactly-once
// rule in SPV_EXT_fragment_shader_interlock. No Vulkan device or game is used.
#include <rex/graphics/format/ucode.h>
#include <rex/graphics/pipeline/shader/spirv_translator.h>
#include <rex/string/buffer.h>

#include <bit>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace rex::graphics;
int CheckRectangleControlFlow(const char* dump_directory, const char* capture_directory);
namespace {
void Require(bool condition, const char* reason) {
  if (!condition) throw std::runtime_error(reason);
}
struct Instruction { spv::Op op; std::vector<uint32_t> args; };

// Explore (basic block, interlock state), including both sides of all branches
// and switch cases. Re-visiting a state is enough to close loops; a Begin/End in
// a loop is still rejected on its second visit with the changed state.
void CheckInterlock(const std::vector<uint8_t>& binary) {
  Require(binary.size() >= 20 && !(binary.size() % 4), "invalid module length");
  std::vector<uint32_t> words(binary.size() / 4);
  std::memcpy(words.data(), binary.data(), binary.size());
  Require(words[0] == spv::MagicNumber, "invalid SPIR-V magic");
  uint32_t entry = 0, function = 0, block = 0, first_block = 0;
  std::map<uint32_t, std::vector<Instruction>> blocks;
  for (size_t i = 5; i < words.size();) {
    const size_t count = words[i] >> 16;
    Require(count && count <= words.size() - i, "invalid instruction length");
    const auto op = spv::Op(words[i] & 0xFFFF);
    const std::vector<uint32_t> args(words.begin() + i + 1, words.begin() + i + count);
    if (op == spv::OpEntryPoint && args.at(0) == spv::ExecutionModelFragment)
      entry = args.at(1);
    if (op == spv::OpFunction) function = args.at(1);
    else if (op == spv::OpFunctionEnd) { function = 0; block = 0; }
    else if (op == spv::OpLabel && function == entry) {
      block = args.at(0);
      if (!first_block) first_block = block;
      blocks.try_emplace(block);
    } else if (block) blocks.at(block).push_back({op, args});
    i += count;
  }
  Require(entry && first_block, "missing fragment entry point");
  std::vector<std::pair<uint32_t, unsigned>> pending{{first_block, 0}};
  std::set<std::pair<uint32_t, unsigned>> visited;
  bool returned = false, began = false, ended = false;
  while (!pending.empty()) {
    auto [id, state] = pending.back(); pending.pop_back();
    if (!visited.emplace(id, state).second) continue;
    const auto& instructions = blocks.at(id);
    for (const auto& [op, args] : instructions) {
      if (op == spv::OpBeginInvocationInterlockEXT) {
        if (state != 0) throw std::runtime_error("Begin repeated/out of order in block " + std::to_string(id));
        began = true; state = 1;
      } else if (op == spv::OpEndInvocationInterlockEXT) {
        if (state != 1) throw std::runtime_error("End without exactly one Begin in block " + std::to_string(id));
        ended = true; state = 2;
      } else if (op == spv::OpBranch) pending.emplace_back(args.at(0), state);
      else if (op == spv::OpBranchConditional) {
        pending.emplace_back(args.at(1), state);
        pending.emplace_back(args.at(2), state);
      } else if (op == spv::OpSwitch) {
        pending.emplace_back(args.at(1), state);
        for (size_t i = 3; i < args.size(); i += 2) pending.emplace_back(args[i], state);
      } else if (op == spv::OpReturn || op == spv::OpReturnValue || op == spv::OpKill) {
        if (state != 2) throw std::runtime_error("Invocation exits without Begin/End pair in block " + std::to_string(id));
        returned = true;
      }
    }
  }
  Require(returned && began && ended, "no reachable interlocked return");
}

std::vector<uint32_t> PixelMicrocode(bool depth, bool kill) {
  using namespace rex::graphics::ucode;
  const uint64_t alloc = (uint64_t(AllocType::kPsColors) << 41) |
                         (uint64_t(ControlFlowOpcode::kAlloc) << 44);
  const unsigned count = 1 + unsigned(depth) + unsigned(kill);
  const uint64_t exec = 1 | (uint64_t(count) << 12) |
                        (uint64_t(ControlFlowOpcode::kExecEnd) << 44);
  std::vector<uint32_t> code{uint32_t(alloc), uint32_t(alloc >> 32) | uint32_t(exec << 16), uint32_t(exec >> 16)};
  if (kill) code.insert(code.end(), {0, 0, uint32_t(AluVectorOpcode::kKillEq) << 24});
  // Color 0 adds the alpha-test/alpha-to-coverage guard independently of kill.
  code.insert(code.end(), {(1u << 15) | (15u << 16), 0, uint32_t(AluVectorOpcode::kMax) << 24});
  if (depth) code.insert(code.end(), {61u | (1u << 15) | (1u << 16), 0, uint32_t(AluVectorOpcode::kMax) << 24});
  return code;
}

void Dump(const std::filesystem::path& directory, const std::string& name,
          const std::vector<uint8_t>& binary) {
  if (directory.empty()) return;
  std::filesystem::create_directories(directory);
  std::ofstream stream(directory / (name + ".spv"), std::ios::binary);
  stream.write(reinterpret_cast<const char*>(binary.data()), std::streamsize(binary.size()));
  Require(bool(stream), "writing SPIR-V evidence failed");
}
}

int main(int argc, char** argv) {
  try {
    Require(argc >= 1 && argc <= 3,
            "usage: x360n_spirv_interlock_checks [dump-directory [capture-directory]]");
    const std::filesystem::path directory = argc >= 2 ? argv[1] : "";
    unsigned passed = 0, failed = 0;
    for (bool demote : {false, true}) for (unsigned scale : {1u, 2u})
      for (unsigned query = 0; query < 3; ++query) {
        SpirvShaderTranslator::Features features(true);
        features.demote_to_helper_invocation = demote;
        SpirvShaderTranslator translator(features, true, true, true, scale, scale);
        const std::string suffix = "-demote" + std::to_string(demote) + "-scale" +
                                   std::to_string(scale) + "-query" + std::to_string(query);
        auto check = [&](const std::string& name, const std::vector<uint8_t>& binary) {
          Dump(directory, name, binary);
          try { CheckInterlock(binary); ++passed; }
          catch (const std::exception& error) {
            ++failed; std::cerr << name << ": " << error.what() << '\n';
          }
        };
        for (bool depth : {false, true}) for (bool kill : {false, true}) {
          const auto code = PixelMicrocode(depth, kill);
          Shader shader(xenos::ShaderType::kPixel, 1, code.data(), code.size(), std::endian::native);
          rex::string::StringBuffer disassembly;
          shader.AnalyzeUcode(disassembly);
          Require(shader.writes_depth() == depth && shader.kills_pixels() == kill &&
                      shader.writes_color_targets() == 1, "microcode did not activate intended path");
          SpirvShaderTranslator::Modification modification(translator.GetDefaultPixelShaderModification(1));
          modification.pixel.fsi_sample_counts = query == 1;
          if (query == 2) modification.value |= SpirvShaderTranslator::Modification::kNativeVisibilityQuery;
          auto& translation = *shader.GetOrCreateTranslation(modification.value);
          Require(translator.TranslateAnalyzedShader(translation) && translation.is_valid(), "shader translation failed");
          check(std::string(depth ? "late" : "early") + "-kill" + std::to_string(kill) + suffix,
                translation.translated_binary());
        }
        check("depth-only" + suffix, translator.CreateDepthOnlyFragmentShader(
            SpirvShaderTranslator::Modification::DepthStencilMode::kNoModifiers, query == 1, query == 2));
      }
    std::cout << "SPIR-V interlock CFG: " << passed << " passed, " << failed << " failed\n";
    const int rectangle_result = CheckRectangleControlFlow(argc >= 2 ? argv[1] : nullptr,
                                                           argc >= 3 ? argv[2] : nullptr);
    return failed || rectangle_result ? 1 : 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
