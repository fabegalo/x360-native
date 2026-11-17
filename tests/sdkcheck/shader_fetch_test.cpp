#include <catch2/catch_test_macros.hpp>
#include "scalar_precision_oracle.h"
#include <algorithm>
#include <bit>
#include <cstring>
#include <format>
#include <string>
#include <sstream>
#include <vector>
#include <rex/string/text.h>
#ifdef _WIN32
#include "gpu/xenos/pipeline/shader/dxbc_translator.h"
#include <d3dcompiler.h>
#endif
#include "gpu/xenos/pipeline/shader/shader.h"
#include "gpu/xenos/pipeline/shader/spirv_translator.h"
#include "spirv_controlflow_cases.h"
#include <spirv-tools/libspirv.hpp>
#include <rex/xenos/ucode.h>
#include "gpu/xenos/xenos_settings.h"

namespace {
using namespace rex::gpu;
namespace xenos = rex::xenos;

#ifdef _WIN32
bool HasSequence(const std::string& assembly, std::initializer_list<std::string> expected) {
  std::vector<std::string> instructions;
  std::istringstream lines(assembly);
  for (std::string line; std::getline(lines, line);) {
    std::istringstream instruction(line);
    std::string opcode;
    if (instruction >> opcode) instructions.push_back(opcode);
  }
  return std::search(instructions.begin(), instructions.end(), expected.begin(), expected.end())
         != instructions.end();
}
#endif

std::vector<uint32_t> TextureCode(bool unnormalized, bool offset,
                                 xenos::FetchOpDimension dimension, bool point = false) {
  using namespace rex::xenos::ucode;
  const uint64_t alloc = (uint64_t(AllocType::kPsColors) << 41) |
                         (uint64_t(ControlFlowOpcode::kAlloc) << 44);
  const uint64_t exec = 1 | (uint64_t(2) << 12) | (uint64_t(1) << 16) |
                        (uint64_t(ControlFlowOpcode::kExecEnd) << 44);
  return {uint32_t(alloc), uint32_t(alloc >> 32) | uint32_t(exec << 16), uint32_t(exec >> 16),
          uint32_t(FetchOpcode::kTextureFetch) | (uint32_t(unnormalized) << 25) | (0x24u << 26),
          0x688u | (point ? 0u : (3u << 12) | (3u << 14) | (3u << 16)) |
              (7u << 18) | (3u << 24) | (3u << 26),
          (uint32_t(dimension) << 14) | (uint32_t(offset ? 2 : 0) << 26),
          (1u << 15) | (15u << 16), 0,
          (uint32_t(AluVectorOpcode::kMax) << 24) | (1u << 30) | (1u << 31)};
}

#ifdef _WIN32
std::string Translate(const std::vector<uint32_t>& code, xenos::ShaderType type, bool rov = false,
                      bool count_samples = false, rex::gpu::ShaderModificationKey extra_flags = {},
                      bool unorm8_fixed16 = true,
                      uint32_t edram_tile_count = xenos::kEDRAMTileCount) {
  Shader shader(type, 1, code.data(), code.size(), std::endian::native);
  rex::string::StringBuffer disassembly;
  shader.AnalyzeUcode(disassembly, "");
  struct TestSettings : XenosSettings {} settings;
  DXBCOptions options;
  options.unorm8_fixed16 = unorm8_fixed16;
  DXBCShaderTranslator translator(settings, options, GraphicsProvider::GPUVendorID::kNvidia,
                                  false, rov, false, true, 1, 1, false, edram_tile_count);
  const auto modification = type == xenos::ShaderType::kPixel
      ? translator.GetDefaultPixelShaderModification(1)
      : translator.GetDefaultVertexShaderModification(1, Shader::HostVertexShaderType::kVertex);
  DXBCShaderTranslator::Modification selected(modification);
  selected.value |= extra_flags;
  if (type == xenos::ShaderType::kPixel) selected.pixel.rov_sample_counts = count_samples;
  auto& translation = *shader.GetOrCreateTranslation(selected.value);
  REQUIRE(translator.TranslateAnalyzedShader(translation));
  REQUIRE(translation.IsValid());
  const auto& binary = translation.GetTranslatedBinary();
  ID3DBlob* assembly = nullptr;
  REQUIRE(SUCCEEDED(D3DDisassemble(binary.data(), binary.size(), 0, nullptr, &assembly)));
  REQUIRE(assembly != nullptr);
  std::string text(static_cast<const char*>(assembly->GetBufferPointer()), assembly->GetBufferSize());
  assembly->Release();
  return text;
}
#endif

std::vector<uint8_t> TranslateSPIRV(const std::vector<uint32_t>& code, xenos::ShaderType type,
                                  Shader::HostVertexShaderType host_type,
                                  bool interlock, bool demote, bool count_samples = false,
                                  bool early_depth = false, rex::gpu::ShaderModificationKey extra_flags = {},
                                  bool scaled_offsets = true, uint32_t scale_x = 1,
                                  uint32_t scale_y = 1,
                                  uint32_t edram_tile_count = xenos::kEDRAMTileCount) {
  Shader shader(type, 1, code.data(), code.size(), std::endian::native);
  rex::string::StringBuffer disassembly;
  shader.AnalyzeUcode(disassembly, "");
  SPIRVShaderTranslator::Features features(true);
  features.demote_to_helper_invocation = demote;
  SPIRVShaderTranslator translator(features, false, false, interlock, scale_x, scale_y,
                                   scaled_offsets, false, edram_tile_count);
  const auto modification = type == xenos::ShaderType::kPixel
      ? translator.GetDefaultPixelShaderModification(1)
      : translator.GetDefaultVertexShaderModification(1, host_type);
  SPIRVShaderTranslator::Modification selected(modification);
  selected.value |= extra_flags;
  if (type == xenos::ShaderType::kPixel) selected.pixel.fsi_sample_counts = count_samples;
  if (early_depth) selected.pixel.depth_stencil_mode = SPIRVShaderTranslator::Modification::DepthStencilMode::kEarlyHint;
  auto& translation = *shader.GetOrCreateTranslation(selected.value);
  REQUIRE(translator.TranslateAnalyzedShader(translation));
  REQUIRE(translation.IsValid());
  const auto& binary = translation.GetTranslatedBinary();
  REQUIRE(!binary.empty());
  REQUIRE(binary.size() % 4 == 0);
  std::vector<uint32_t> words(binary.size() / 4);
  std::memcpy(words.data(), binary.data(), binary.size());
  spvtools::SpirvTools validator(SPV_ENV_VULKAN_1_2);
  std::string diagnostic;
  validator.SetMessageConsumer([&](spv_message_level_t, const char*, const spv_position_t&, const char* message) {
    diagnostic += std::string_view(message).substr(0, 4096 - diagnostic.size());
  });
  const bool valid = validator.Validate(words);
  INFO(diagnostic);
  REQUIRE(valid);
  return binary;
}
}

#ifdef _WIN32
TEST_CASE("Scalar shader variants validate without optional integer or float widths",
          "[sdk][gpu][shader-fetch][scalar-precision]") {
  using O = rex::xenos::ucode::AluScalarOpcode;
  for (auto op : {O::kExp, O::kLog, O::kLogc, O::kRcp, O::kRcpc, O::kRcpf,
                  O::kRsq, O::kRsqc, O::kRsqf, O::kSqrt, O::kMulsc0, O::kMulsc1}) {
    const auto code = x360n::test::Program(op);
    for (bool rtz : {false, true}) {
      CAPTURE(op, rtz);
      const auto flags = rtz ? SPIRVShaderTranslator::Modification::kMulscRTZ
                             : ShaderModificationKey{};
      CHECK_FALSE(Translate(code, xenos::ShaderType::kPixel, false, false, flags).empty());
      auto binary = TranslateSPIRV(code, xenos::ShaderType::kPixel,
          Shader::HostVertexShaderType::kVertex, false, false, false, false, flags);
      std::vector<uint32_t> words(binary.size() / 4);
      std::memcpy(words.data(), binary.data(), binary.size());
      bool extended_product = false;
      for (size_t offset = 5; offset < words.size();) {
        const uint32_t count = words[offset] >> 16;
        REQUIRE(count > 0);
        REQUIRE(offset + count <= words.size());
        const auto instruction = static_cast<spv::Op>(words[offset] & 0xFFFF);
        if (instruction == spv::OpCapability) {
          CHECK(words[offset + 1] != spv::CapabilityFloat64);
          CHECK(words[offset + 1] != spv::CapabilityInt64);
        }
        extended_product |= instruction == spv::OpUMulExtended;
        offset += count;
      }
      CHECK(extended_product == (rtz && (op == O::kMulsc0 || op == O::kMulsc1)));
    }
  }
}

TEST_CASE("EDRAM capacities produce independent valid shader variants",
          "[sdk][gpu][shader-fetch][edram-capacity]") {
  const auto code = TextureCode(false, false, xenos::FetchOpDimension::k2D);
  const auto standard = Translate(code, xenos::ShaderType::kPixel, true);
  const auto extended = Translate(code, xenos::ShaderType::kPixel, true, false, {}, true,
                                  xenos::kEDRAMTileCountMax);
  CHECK(standard != extended);
  const auto standard_spv = TranslateSPIRV(code, xenos::ShaderType::kPixel,
      Shader::HostVertexShaderType::kVertex, true, false);
  const auto extended_spv = TranslateSPIRV(code, xenos::ShaderType::kPixel,
      Shader::HostVertexShaderType::kVertex, true, false, false, false, {}, true, 1, 1,
      xenos::kEDRAMTileCountMax);
  CHECK(standard_spv != extended_spv);
  SPIRVShaderTranslator::Features features(true);
  SPIRVShaderTranslator standard_translator(features, false, false, true);
  SPIRVShaderTranslator extended_translator(features, false, false, true, 1, 1, true, false,
                                            xenos::kEDRAMTileCountMax);
  CHECK(standard_translator.GetDefaultPixelShaderModification(1) !=
        extended_translator.GetDefaultPixelShaderModification(1));
  CHECK(standard_translator.GetDefaultVertexShaderModification(1) !=
        extended_translator.GetDefaultVertexShaderModification(1));
}

TEST_CASE("Host shader polygon offset preserves depth and derivative interfaces",
          "[sdk][gpu][shader-fetch][depth-bias]") {
  using D = DXBCShaderTranslator::Modification;
  using S = SPIRVShaderTranslator::Modification;
  const auto code = TextureCode(false, false, xenos::FetchOpDimension::k2D);
  for (const auto modes : {std::pair{D::DepthStencilMode::kPolygonOffset, S::DepthStencilMode::kPolygonOffset},
                          std::pair{D::DepthStencilMode::kFloat24TruncatingPolygonOffset, S::DepthStencilMode::kFloat24TruncatingPolygonOffset},
                          std::pair{D::DepthStencilMode::kFloat24RoundingPolygonOffset, S::DepthStencilMode::kFloat24RoundingPolygonOffset}}) {
    D dxbc_modification;
    dxbc_modification.pixel.depth_stencil_mode = modes.first;
    const auto dxbc = Translate(code, xenos::ShaderType::kPixel, false, false,
                                dxbc_modification.value);
    CHECK(dxbc.find("dcl_output oDepth") != std::string::npos);
    CHECK(dxbc.find("deriv_rtx_coarse") != std::string::npos);
    CHECK(dxbc.find("deriv_rty_coarse") != std::string::npos);
    CHECK(dxbc.find("oDepthLE") == std::string::npos);
    S spirv_modification;
    spirv_modification.pixel.depth_stencil_mode = modes.second;
    const auto binary = TranslateSPIRV(code, xenos::ShaderType::kPixel,
        Shader::HostVertexShaderType::kVertex, false, false, false, false,
        spirv_modification.value);
    std::vector<uint32_t> words(binary.size() / 4);
    std::memcpy(words.data(), binary.data(), binary.size());
    spvtools::SpirvTools tools(SPV_ENV_VULKAN_1_2);
    std::string spirv;
    REQUIRE(tools.Disassemble(words, &spirv));
    CHECK(spirv.find("OpDPdxCoarse") != std::string::npos);
    CHECK(spirv.find("OpDPdyCoarse") != std::string::npos);
    CHECK(spirv.find("BuiltIn FragDepth") != std::string::npos);
    CHECK(spirv.find("BuiltIn FrontFacing") != std::string::npos);
    CHECK((spirv.find("BuiltIn SampleId") != std::string::npos) ==
          (modes.second != S::DepthStencilMode::kPolygonOffset));
  }
}

TEST_CASE("Stacked texture coordinates clamp to layer centers", "[sdk][gpu][shader-fetch]") {
  for (bool unnormalized : {false, true}) {
    for (bool offset : {false, true}) {
      CAPTURE(unnormalized, offset);
      const auto text = Translate(TextureCode(unnormalized, offset,
                                  rex::xenos::FetchOpDimension::k3DOrStacked),
                                  rex::xenos::ShaderType::kPixel);
      CHECK(text.find("0.500000") != std::string::npos);
      CHECK(text.find("-0.500000") != std::string::npos);
      CHECK(text.find("min ") != std::string::npos);
      CHECK(text.find("max ") != std::string::npos);
      CHECK(HasSequence(text, {"max", "add", "min"}));
    }
  }
}

TEST_CASE("Unnormalized texture fetch uses the locked mip grid", "[sdk][gpu][shader-fetch]") {
  const auto text = Translate(TextureCode(true, false, rex::xenos::FetchOpDimension::k2D),
                              rex::xenos::ShaderType::kPixel);
  CHECK(text.find("ieq ") != std::string::npos);
  CHECK(text.find("ushr ") != std::string::npos);
  CHECK(text.find("umax ") != std::string::npos);
  CHECK(HasSequence(text, {"ubfe", "ieq", "ushr", "umax", "movc"}));
}

TEST_CASE("Vertex fetch masks individual words outside the guest buffer", "[sdk][gpu][shader-fetch]") {
  using namespace rex::xenos::ucode;
  const uint64_t exec = 1 | (uint64_t(1) << 12) | (uint64_t(1) << 16) |
                        (uint64_t(ControlFlowOpcode::kExecEnd) << 44);
  const std::vector<uint32_t> code = {uint32_t(exec), uint32_t(exec >> 32), 0,
      uint32_t(FetchOpcode::kVertexFetch) | (1u << 12) | (1u << 19),
      0x688u | (uint32_t(rex::xenos::VertexFormat::k_32_32_32_32_FLOAT) << 16), 4};
  const auto text = Translate(code, rex::xenos::ShaderType::kVertex);
  CHECK(text.find("ult ") != std::string::npos);
  CHECK(text.find("umad ") != std::string::npos);
  CHECK(HasSequence(text, {"ubfe", "and", "umad", "iadd", "ult"}));
}

TEST_CASE("MSAA ROV coverage and per-sample depth produce valid DXBC", "[sdk][gpu][shader-fetch][msaa-positions]") {
  for (bool unnormalized : {false, true}) {
    const auto text = Translate(TextureCode(unnormalized, false,
                                rex::xenos::FetchOpDimension::k2D),
                                rex::xenos::ShaderType::kPixel, true);
    CHECK(text.find("dcl_input vCoverage") != std::string::npos);
    CHECK(text.find("ubfe ") != std::string::npos);
    CHECK(text.find("bfi ") != std::string::npos);
  }
}
#endif

TEST_CASE("SPIR-V texture and alpha paths validate with and without interlock and demotion",
          "[sdk][gpu][shader-fetch][spirv-controlflow]") {
  for (bool interlock : {false, true}) {
    for (bool demote : {false, true}) {
      for (bool unnormalized : {false, true}) {
        for (bool offset : {false, true}) {
          for (auto dimension : {rex::xenos::FetchOpDimension::k1D, rex::xenos::FetchOpDimension::k2D,
                                 rex::xenos::FetchOpDimension::k3DOrStacked, rex::xenos::FetchOpDimension::kCube}) {
            CAPTURE(interlock, demote, unnormalized, offset, dimension);
            TranslateSPIRV(TextureCode(unnormalized, offset, dimension), rex::xenos::ShaderType::kPixel,
                           rex::gpu::Shader::HostVertexShaderType::kVertex, interlock, demote);
          }
        }
      }
    }
  }
}

TEST_CASE("SPIR-V rectangle replay and unreachable jumps keep exact Phi predecessors",
          "[sdk][gpu][shader-fetch][spirv-controlflow]") {
  using namespace x360n::test::spirv;
  for (unsigned jump = 0; jump <= 4; ++jump) {
    for (auto host : {rex::gpu::Shader::HostVertexShaderType::kVertex,
                      rex::gpu::Shader::HostVertexShaderType::kRectangleListAsTriangleStrip}) {
      CAPTURE(jump, host);
      const auto binary = TranslateSPIRV(VertexMicrocode(jump), rex::xenos::ShaderType::kVertex,
                                        host, false, false);
      REQUIRE_NOTHROW(CheckPhiPredecessors(binary,
          jump != 0 && host == rex::gpu::Shader::HostVertexShaderType::kRectangleListAsTriangleStrip));
    }
  }
}

TEST_CASE("Optional MSAA sample counters preserve valid interlock shaders",
          "[sdk][gpu][shader-fetch][msaa-positions]") {
  const auto code = TextureCode(false, false, rex::xenos::FetchOpDimension::k2D);
#ifdef _WIN32
  const auto text = Translate(code, rex::xenos::ShaderType::kPixel, true, true);
  CHECK(text.find("atomic_iadd") != std::string::npos);
#endif
  for (bool early : {false, true}) {
    for (bool demote : {false, true}) {
      CAPTURE(early, demote);
      TranslateSPIRV(code, rex::xenos::ShaderType::kPixel,
                     rex::gpu::Shader::HostVertexShaderType::kVertex, true, demote, true, early);
    }
  }
}

TEST_CASE("Native shader variants preserve valid translations and distinct options",
          "[sdk][gpu][shader-fetch][native-variants]") {
  const auto code = TextureCode(false, false, rex::xenos::FetchOpDimension::k2D);
#ifdef _WIN32
  using DX = rex::gpu::DXBCShaderTranslator::Modification;
  for (auto flags : {DX::kInterpolatorNoPerspective, DX::kNativeHardwareColor,
                     DX::kMemExportDisabled | DX::kNativeVisibilityQuery,
                     DX::kMemExportDisabled | DX::kNativeVisibilityQuery | DX::kNativeStripedSampleCounts}) {
    CAPTURE(flags.low, flags.high);
    const auto text = Translate(code, rex::xenos::ShaderType::kPixel, true, true, flags);
    CHECK_FALSE(text.empty());
    if (flags & DX::kInterpolatorNoPerspective) CHECK(text.find("noperspective") != std::string::npos);
  }
  const auto point_code = TextureCode(false, false, rex::xenos::FetchOpDimension::k2D, true);
  const auto enabled = Translate(point_code, rex::xenos::ShaderType::kPixel, false, false, 0, true);
  const auto disabled = Translate(point_code, rex::xenos::ShaderType::kPixel, false, false, 0, false);
  CHECK(static_cast<bool>(enabled != disabled));
#endif
  using SP = rex::gpu::SPIRVShaderTranslator::Modification;
  for (auto flags : {SP::kInterpolatorNoPerspective,
                     SP::kMemExportDisabled | SP::kNativeVisibilityQuery}) {
    CAPTURE(flags.low, flags.high);
    TranslateSPIRV(code, rex::xenos::ShaderType::kPixel,
                   rex::gpu::Shader::HostVertexShaderType::kVertex, true, false, true, false, flags);
  }
  const auto enabled_spirv = TranslateSPIRV(code, rex::xenos::ShaderType::kPixel,
      rex::gpu::Shader::HostVertexShaderType::kVertex, false, false, false, false, 0, true, 2, 3);
  const auto disabled_spirv = TranslateSPIRV(code, rex::xenos::ShaderType::kPixel,
      rex::gpu::Shader::HostVertexShaderType::kVertex, false, false, false, false, 0, false, 2, 3);
  CHECK(static_cast<bool>(enabled_spirv != disabled_spirv));
}

TEST_CASE("Texture offset options have independent persistent shader keys",
          "[sdk][gpu][shader-fetch][shader-cache]") {
  using namespace rex::gpu;
#ifdef _WIN32
  struct Settings : XenosSettings {} settings;
  DXBCOptions options;
  DXBCShaderTranslator dx(settings, options, GraphicsProvider::GPUVendorID::kNvidia,
                          false, false);
  const auto enabled_dx = dx.GetDefaultPixelShaderModification(1);
  options.scaled_texture_offsets = false;
  dx.SetOptions(options);
  const auto disabled_dx = dx.GetDefaultPixelShaderModification(1);
  CHECK(enabled_dx != disabled_dx);
  CHECK(enabled_dx.low == disabled_dx.low);
  CHECK(static_cast<bool>(disabled_dx & DXBCShaderTranslator::Modification::kScaledTextureOffsetsDisabled));
#endif
  SPIRVShaderTranslator sp(SPIRVShaderTranslator::Features(true), false, false, false, 2, 3);
  const auto enabled_sp = sp.GetDefaultPixelShaderModification(1);
  sp.SetScaledTextureOffsets(false);
  const auto disabled_sp = sp.GetDefaultPixelShaderModification(1);
  CHECK(enabled_sp != disabled_sp);
  CHECK(enabled_sp.low == disabled_sp.low);
  CHECK(static_cast<bool>(disabled_sp & SPIRVShaderTranslator::Modification::kScaledTextureOffsetsDisabled));
  CHECK(std::format("{:016X}", ShaderModificationKey{1, 2}) ==
        "00000000000000020000000000000001");
}

TEST_CASE("Point snapping has independent persistent shader keys and valid translations",
          "[sdk][gpu][shader-fetch][shader-cache]") {
  using namespace rex::gpu;
  const auto code = TextureCode(false, false, rex::xenos::FetchOpDimension::k2D, true);
#ifdef _WIN32
  struct Settings : XenosSettings {} settings;
  DXBCOptions options;
  DXBCShaderTranslator dx(settings, options, GraphicsProvider::GPUVendorID::kNvidia,
                          false, false);
  const auto disabled_dx = dx.GetDefaultPixelShaderModification(1);
  const auto disabled_vs = dx.GetDefaultVertexShaderModification(1, Shader::HostVertexShaderType::kVertex);
  options.texture_point_snap = true;
  dx.SetOptions(options);
  CHECK(dx.GetDefaultPixelShaderModification(1) == (disabled_dx | DXBCShaderTranslator::Modification::kTexturePointSnap));
  CHECK(dx.GetDefaultVertexShaderModification(1, Shader::HostVertexShaderType::kVertex) == (disabled_vs | DXBCShaderTranslator::Modification::kTexturePointSnap));
  const auto disabled = Translate(code, rex::xenos::ShaderType::kPixel);
  const auto enabled = Translate(code, rex::xenos::ShaderType::kPixel, false, false,
                                 DXBCShaderTranslator::Modification::kTexturePointSnap);
  CHECK(disabled != enabled);
#endif
  SPIRVShaderTranslator sp(SPIRVShaderTranslator::Features(true), false, false, false);
  const auto disabled_sp = sp.GetDefaultPixelShaderModification(1);
  const auto disabled_sp_vs = sp.GetDefaultVertexShaderModification(1, Shader::HostVertexShaderType::kVertex);
  sp.SetTexturePointSnap(true);
  CHECK(sp.GetDefaultPixelShaderModification(1) == (disabled_sp | SPIRVShaderTranslator::Modification::kTexturePointSnap));
  CHECK(sp.GetDefaultVertexShaderModification(1, Shader::HostVertexShaderType::kVertex) == (disabled_sp_vs | SPIRVShaderTranslator::Modification::kTexturePointSnap));
  for (bool interlock : {false, true}) {
    const auto disabled_binary = TranslateSPIRV(code, rex::xenos::ShaderType::kPixel,
        Shader::HostVertexShaderType::kVertex, interlock, false);
    const auto enabled_binary = TranslateSPIRV(code, rex::xenos::ShaderType::kPixel,
        Shader::HostVertexShaderType::kVertex, interlock, false, false, false,
        SPIRVShaderTranslator::Modification::kTexturePointSnap);
    CHECK(disabled_binary != enabled_binary);
  }
}
