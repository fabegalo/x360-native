#include <catch2/catch_test_macros.hpp>
#include <rex/codegen/codegen.h>
#include "synthetic_xex.h"
#include <fstream>

using namespace x360n::test;

TEST_CASE("GapFill separates adjacent virtual tail thunks without pdata or RTTI",
          "[sdk][codegen][gapfill][aot-discovery]") {
  std::vector<Placed> placed;
  auto bytes = BuildXex({}, &placed, 0, Entry::kReturnZero);
  const auto put = [&](uint32_t address, uint32_t word) {
    const size_t offset = 0x1000 + address - kXexBase;
    for (unsigned i = 0; i < 4; ++i) bytes.at(offset + i) = uint8_t(word >> (24 - i * 8));
  };
  // The first leaf has pdata. Two virtual tail thunks and a leaf have none.
  const uint32_t thunks[] = {
      0x81630000, 0x816B0034, 0x7D6903A6, 0x4E800420,  // r11, vtable slot 52
      0x81830000, 0x818C0038, 0x7D8903A6, 0x4E800420,  // r12, vtable slot 56
      0x38600025, 0x4E800020};
  for (unsigned i = 0; i < std::size(thunks); ++i) put(kXexText + 8 + 4 * i, thunks[i]);
  Workdir work("x360n_discovery", "virtual_thunks");
  work.write("test.xex", bytes);
  std::ofstream(work.dir / "test.toml") << "project_name='thunks'\nfile_path='test.xex'\nout_directory_path='out'\n";
  auto pipeline = rex::codegen::CodegenPipeline::Create(work.dir / "test.toml");
  REQUIRE(pipeline.has_value());
  REQUIRE(pipeline->RunAnalyze().has_value());
  const auto& graph = pipeline->context().graph;
  const auto* first = graph.getFunction(kXexText + 8);
  const auto* second = graph.getFunction(kXexText + 24);
  const auto* leaf = graph.getFunction(kXexText + 40);
  REQUIRE(first);
  REQUIRE(second);
  REQUIRE(leaf);
  CHECK(first->blocks().size() == 1);
  CHECK(first->blocks().front().size == 16);
  CHECK(second->blocks().size() == 1);
  CHECK(second->blocks().front().size == 16);
  CHECK(leaf->blocks().front().size == 8);
  CHECK(first->jumpTables().empty());
  CHECK(second->jumpTables().empty());
  CHECK(graph.getFunction(kXexText)->authority() == rex::codegen::FunctionAuthority::PDATA);
  REQUIRE(pipeline->RunWrite().has_value());
}

TEST_CASE("GapFill does not split calls or ambiguous CTR dispatch as virtual thunks",
          "[sdk][codegen][gapfill][aot-discovery]") {
  // A bctrl must preserve fall-through. Other near-matches may be dispatch
  // inside a larger function: the new recognition must not claim a boundary.
  const uint32_t variants[][4] = {
      {0x81630000, 0x816B0034, 0x7D6903A6, 0x4E800421},  // bctrl
      {0x81630000, 0x816B0034, 0x7D6903A6, 0x4C820420},  // conditional bcctr
      {0x81630000, 0x818B0034, 0x7D6903A6, 0x4E800420},  // mismatched load
      {0x81630000, 0x816B0034, 0x7D8903A6, 0x4E800420},  // mismatched CTR
      {0x81630004, 0x816B0034, 0x7D6903A6, 0x4E800420},  // other object member
      {0x81630000, 0x816B0035, 0x7D6903A6, 0x4E800420},  // unaligned slot
      {0x81630000, 0x816BFFFC, 0x7D6903A6, 0x4E800420},  // before vtable
      {0x83E30000, 0x83FF0034, 0x7FE903A6, 0x4E800420},  // nonvolatile r31
      {0x81630000, 0x7D6B582E, 0x7D6903A6, 0x4E800420},  // indexed load / table
  };
  for (const auto& code : variants) {
    CAPTURE(code[0], code[1], code[2], code[3]);
    std::vector<Placed> placed;
    auto bytes = BuildXex({}, &placed, 0, Entry::kReturnZero);
    const auto put = [&](uint32_t address, uint32_t word) {
      const size_t offset = 0x1000 + address - kXexBase;
      for (unsigned i = 0; i < 4; ++i) bytes.at(offset + i) = uint8_t(word >> (24 - i * 8));
    };
    for (unsigned i = 0; i < 4; ++i) put(kXexText + 8 + 4 * i, code[i]);
    put(kXexText + 24, 0x38600025);
    put(kXexText + 28, 0x4E800020);
    Workdir work("x360n_discovery", "ambiguous_ctr");
    work.write("test.xex", bytes);
    std::ofstream(work.dir / "test.toml") << "project_name='thunks'\nfile_path='test.xex'\nout_directory_path='out'\n";
    auto pipeline = rex::codegen::CodegenPipeline::Create(work.dir / "test.toml");
    REQUIRE(pipeline.has_value());
    REQUIRE(pipeline->RunAnalyze().has_value());
    CHECK_FALSE(pipeline->context().graph.isEntryPoint(kXexText + 24));
  }
}

TEST_CASE("Virtual thunk discovery respects declared extents including unreachable holes",
          "[sdk][codegen][gapfill][aot-discovery]") {
  for (bool configExtent : {false, true}) {
    CAPTURE(configExtent);
    std::vector<Placed> placed;
    auto bytes = BuildXex({}, &placed, 0, Entry::kReturnZero);
    const auto put = [&](uint32_t address, uint32_t word) {
      const size_t offset = 0x1000 + address - kXexBase;
      for (unsigned i = 0; i < 4; ++i) bytes.at(offset + i) = uint8_t(word >> (24 - i * 8));
    };
    const uint32_t code[] = {0x81630000, 0x816B0034, 0x7D6903A6, 0x4E800420,
                            0x81830000, 0x818C0038, 0x7D8903A6, 0x4E800420,
                            0x38600025, 0x4E800020};
    for (unsigned i = 0; i < std::size(code); ++i) put(kXexText + 8 + 4 * i, code[i]);
    if (!configExtent) put(kXexPdata + 4, (1u << 30) | (12u << 8));
    Workdir work("x360n_discovery", "protected_thunks");
    work.write("test.xex", bytes);
    std::ofstream config(work.dir / "test.toml");
    config << "project_name='thunks'\nfile_path='test.xex'\nout_directory_path='out'\n";
    if (configExtent) config << "[functions]\n0x82010000 = { size = 48 }\n";
    config.close();
    auto pipeline = rex::codegen::CodegenPipeline::Create(work.dir / "test.toml");
    REQUIRE(pipeline.has_value());
    REQUIRE(pipeline->RunAnalyze().has_value());
    CHECK_FALSE(pipeline->context().graph.isEntryPoint(kXexText + 24));
    CHECK_FALSE(pipeline->context().graph.isEntryPoint(kXexText + 40));
  }
}

TEST_CASE("Manual switch targets retain ownership across a virtual thunk boundary",
          "[sdk][codegen][gapfill][aot-discovery][manual-switch]") {
  std::vector<Placed> placed;
  auto bytes = BuildXex({}, &placed, 0, Entry::kReturnZero);
  const uint32_t code[] = {0x81630000, 0x816B0034, 0x7D6903A6, 0x4E800420,
                          0x38600025, 0x4E800020};
  for (unsigned i = 0; i < std::size(code); ++i) {
    const size_t offset = 0x1000 + kXexText + 8 + 4 * i - kXexBase;
    for (unsigned j = 0; j < 4; ++j) bytes.at(offset + j) = uint8_t(code[i] >> (24 - j * 8));
  }
  Workdir work("x360n_discovery", "manual_thunk_switch");
  work.write("test.xex", bytes);
  std::ofstream(work.dir / "test.toml") <<
      "project_name='thunks'\nfile_path='test.xex'\nout_directory_path='out'\n"
      "[[switch_tables]]\naddress=0x82010014\nregister=3\nlabels=[0x82010018]\n";
  auto pipeline = rex::codegen::CodegenPipeline::Create(work.dir / "test.toml");
  REQUIRE(pipeline.has_value());
  REQUIRE(pipeline->RunAnalyze().has_value());
  const auto& graph = pipeline->context().graph;
  const auto* thunk = graph.getFunction(kXexText + 8);
  REQUIRE(thunk);
  REQUIRE(thunk->jumpTables().size() == 1);
  CHECK(thunk->jumpTables().front().targets == std::vector<uint32_t>{kXexText + 24});
  CHECK(thunk->containsAddress(kXexText + 24));
  CHECK_FALSE(graph.isEntryPoint(kXexText + 24));
  CHECK(thunk->isLabel(kXexText + 24));
  REQUIRE(pipeline->RunWrite().has_value());
}
