#include <catch2/catch_test_macros.hpp>
#include <rex/codegen/codegen.h>
#include <rex/codegen/codegen_context.h>
#include "synthetic_xex.h"
#include <fstream>
using namespace x360n::test;

TEST_CASE("GapFill preserves a leaf after a pdata out-of-line return", "[sdk][codegen][gapfill]") {
  std::vector<Placed> placed;
  auto bytes = BuildXex({}, &placed, 0, Entry::kReturnZero);
  const auto put = [&](uint32_t address, uint32_t word) {
    const size_t offset = 0x1000 + address - kXexBase;
    for (unsigned i = 0; i < 4; ++i) bytes.at(offset + i) = uint8_t(word >> (24 - i * 8));
  };
  // Two independent functions, each with an out-of-line block after blr.
  // Only the first has pdata; there is no padding between the functions.
  const uint32_t code[] = {0x2C030000, 0x4182000C, 0x38600001, 0x4E800020,
      0x38600002, 0x4BFFFFF8, 0x2C040000, 0x4182000C, 0x38600003, 0x4E800020,
      0x38600004, 0x4BFFFFF8};
  for (unsigned i = 0; i < std::size(code); ++i) put(kXexText + 4 * i, code[i]);
  put(kXexPdata + 4, (1u << 30) | (6u << 8));
  Workdir work("x360n_gapfill", "pdata_end");
  work.write("test.xex", bytes);
  std::ofstream(work.dir / "test.toml") << "project_name='gapfill'\nfile_path='test.xex'\nout_directory_path='out'\n";
  auto pipeline = rex::codegen::CodegenPipeline::Create(work.dir / "test.toml");
  REQUIRE(pipeline.has_value());
  const auto run = pipeline->Run(false);
  INFO((run.has_value() ? "ok" : run.error().message));
  REQUIRE(run.has_value());
  const auto& graph = pipeline->context().graph;
  const auto* leaf = graph.getFunction(kXexText + 24);
  REQUIRE(leaf);
  CHECK(leaf->containsAddress(kXexText + 36));
  CHECK(leaf->containsAddress(kXexText + 40));
  CHECK_FALSE(graph.isEntryPoint(kXexText + 40));
}
