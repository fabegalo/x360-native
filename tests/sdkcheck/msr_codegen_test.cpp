#include <catch2/catch_test_macros.hpp>
#include <rex/codegen/codegen.h>
#include "synthetic_xex.h"
#include <fstream>

using namespace x360n::test;

TEST_CASE("MSR interrupt writes produce a compilable native fixture",
          "[sdk][codegen][msr]") {
  auto bytes = BuildXex({}, nullptr, 0, Entry::kReturnZero);
  auto put = [&](uint32_t address, uint32_t word) {
    for (unsigned i = 0; i < 4; ++i)
      bytes.at(0x1000 + address - kXexBase + i) = uint8_t(word >> (24 - 8 * i));
  };
  put(kXexText, 0x7DA10164);       // mtmsrd r13,1: enter interrupt lock
  put(kXexText + 4, 0x7C6000A6);   // mfmsr r3
  put(kXexText + 8, 0x4E800020);   // blr
  put(kXexText + 32, 0x7CE10164);  // mtmsrd r7,1: restore interrupt state
  put(kXexText + 36, 0x7C8000A6);  // mfmsr r4
  put(kXexText + 40, 0x4E800020);
  put(kXexPdata + 4, (1u << 30) | (3u << 8));
  Workdir work("x360n_codegen", "msr");
  work.write("test.xex", bytes);
  std::ofstream(work.dir / "test.toml") <<
      "project_name='msr'\nfile_path='test.xex'\nout_directory_path='out'\n"
      "[functions]\n0x82010020={size=12}\n";
  auto pipeline = rex::codegen::CodegenPipeline::Create(work.dir / "test.toml");
  REQUIRE(pipeline);
  REQUIRE(pipeline->RunAnalyze());
  REQUIRE(pipeline->RunWrite());
}
