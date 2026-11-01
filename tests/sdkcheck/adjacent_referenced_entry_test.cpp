#include <catch2/catch_test_macros.hpp>
#include <rex/codegen/codegen.h>
#include "synthetic_xex.h"
#include <fstream>
#include <iterator>

using namespace x360n::test;

TEST_CASE("Data-referenced frameless entries with unmodeled instructions stay callable",
          "[sdk][codegen][gapfill][adjacent-entry]") {
  for (unsigned variant = 0; variant < 3; ++variant) {
    CAPTURE(variant);
    auto bytes = BuildXex({}, nullptr, 0, Entry::kReturnZero);
    const auto put = [&](uint32_t address, uint32_t word) {
      for (unsigned i = 0; i < 4; ++i)
        bytes.at(0x1000 + address - kXexBase + i) = uint8_t(word >> (24 - 8 * i));
    };
    // Two reference-count adjusters without PDATA, each ending in a virtual
    // tail. mfmsr/mtmsrd and lwarx/stwcx. are absent from the analysis opcode
    // table, and the adjacent pairs end decoded regions before the tails. Only
    // adjacent descriptor slots in .rdata name them; no code branches to them.
    const auto adjuster = [&](uint32_t start, uint32_t delta) {
      const uint32_t code[] = {0x81630004, 0x390B0034, 0x7D2000A6, 0x7DA10164,
                               0x7D404028, 0x394A0000 | delta, 0x7D40412D, 0x7D210164,
                               0x4082FFE8, 0x80E30000, 0x38800001, 0x80C70000,
                               0x7CC903A6, 0x4E800420};
      for (unsigned i = 0; i < 14; ++i) put(start + 4 * i, code[i]);
    };
    const uint32_t first = kXexText + 0x40, second = first + 56;
    adjuster(first, 0x0001);
    adjuster(second, 0xFFFF);
    // A word neither decoder accepts is not code, even before a terminator.
    if (variant == 1) put(second + 4, 0x00000001);
    // Original metadata still owns the bytes after the first tail.
    if (variant == 2) put(kXexPdata + 4, (1u << 30) | (32u << 8));
    put(kXexSlots + 0x40, first);
    put(kXexSlots + 0x4C, second);

    Workdir work("x360n_discovery", "adjacent_entry");
    work.write("test.xex", bytes);
    std::ofstream config(work.dir / "test.toml");
    config << "project_name='adjacent_entry'\nfile_path='test.xex'\nout_directory_path='out'\n";
    config.close();
    auto pipeline = rex::codegen::CodegenPipeline::Create(work.dir / "test.toml");
    REQUIRE(pipeline.has_value());
    REQUIRE(pipeline->RunAnalyze().has_value());
    const auto& graph = pipeline->context().graph;
    CHECK(graph.isEntryPoint(second) == (variant == 0));
    if (variant == 0) {
      const auto* adjust = graph.getFunction(first);
      REQUIRE(adjust);
      CHECK_FALSE(adjust->containsAddress(second));
    }
    REQUIRE(pipeline->RunWrite().has_value());
    std::ifstream init(work.dir / "out" / "adjacent_entry_init.cpp");
    REQUIRE(init.good());
    const std::string registration(std::istreambuf_iterator<char>(init), {});
    CHECK((registration.find("0x82010078, sub_82010078") != std::string::npos) == (variant == 0));
  }
}
