// Maintenance-only compiler. Release uses checked-in bytecode, not this tool.
#include <SPIRV/GlslangToSpv.h>
#include <StandAlone/DirStackFileIncluder.h>
#include <StandAlone/ResourceLimits.h>
#include <filesystem>
#include <fstream>
#include <glslang/Public/ShaderLang.h>
#include <iostream>
#include <vector>

int main(int argc, char **argv) {
  if (argc != 3)
    return 2;
  const std::filesystem::path input = std::filesystem::absolute(argv[1]);
  glslang::InitializeProcess();
  int result = 0;
  {
    std::string wrapper =
        "#version 460\n"
        "#extension GL_EXT_control_flow_attributes : require\n"
        "#extension GL_EXT_samplerless_texture_functions : require\n"
        "#extension GL_GOOGLE_include_directive : require\n"
        "#include \"" +
        input.filename().string() + "\"\n";
    const char *text = wrapper.c_str();
    glslang::TShader shader(EShLangCompute);
    shader.setStrings(&text, 1);
    shader.setPreamble("#define SHADING_LANGUAGE_GLSL_XE 1\n");
    shader.setEnvInput(glslang::EShSourceGlsl, EShLangCompute,
                       glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
    const auto messages = EShMessages(EShMsgSpvRules | EShMsgVulkanRules);
    DirStackFileIncluder includer;
    includer.pushExternalLocalDirectory(input.parent_path().string());
    glslang::TProgram program;
    if (!shader.parse(&glslang::DefaultTBuiltInResource, 460, false, messages,
                      includer)) {
      std::cerr << shader.getInfoLog();
      result = 1;
    } else {
      program.addShader(&shader);
      if (!program.link(messages)) {
        std::cerr << program.getInfoLog();
        result = 1;
      } else {
        std::vector<uint32_t> words;
        glslang::GlslangToSpv(*program.getIntermediate(EShLangCompute), words);
        std::ofstream output(argv[2], std::ios::binary);
        output.write(reinterpret_cast<const char *>(words.data()),
                     words.size() * sizeof(uint32_t));
        if (!output)
          result = 1;
      }
    }
  }
  glslang::FinalizeProcess();
  return result;
}
