#include <catch2/catch_test_macros.hpp>
#include <rex/codegen/codegen.h>
#include "synthetic_xex.h"
#include <fstream>
#include <iterator>

using namespace x360n::test;

TEST_CASE("Unsigned split defaults preserve complete large switch tables",
          "[sdk][codegen][ctr-dispatch][split-default]") {
  for (unsigned variant = 0; variant < 9; ++variant) {
    CAPTURE(variant);
    auto bytes = BuildXex({}, nullptr, 0, Entry::kReturnZero);
    // Extend the fixture's real PE .rdata span for all 643 table entries.
    // The existing XEX data page already covers this range.
    for (const auto field : {0x1180u, 0x1188u}) {
      for (unsigned i = 0; i < 4; ++i) bytes.at(field + i) = uint8_t(0x1000u >> (8 * i));
    }
    const auto put = [&](uint32_t address, uint32_t word) {
      for (unsigned i = 0; i < 4; ++i)
        bytes.at(0x1000 + address - kXexBase + i) = uint8_t(word >> (24 - 8 * i));
    };
    uint32_t code[] = {
        0x7C6B1B78, 0x2B0B0282, // mr r11,r3; cmplwi cr6,r11,642
        0x40990008, 0x48000038, // ble dispatch; b default
        0x3D808200, 0x60000000, // lis r12,table@h; nop
        0x5560103A, 0x398C1400, // slwi r0,r11,2; addi r12,r12,table@l
        0x7C0C002E, 0x7C0903A6, 0x4E800420,
        0x3860000B, 0x4E800020, // indices 0..511: 11
        0x38600016, 0x4E800020, // indices 512..641: 22
        0x38600021, 0x4E800020, // index 642: 33
        0x3860002C, 0x4E800020  // default: 44
    };
    if (variant == 1) code[2] = 0x409A0008; // Different tested CR bit.
    if (variant == 2) code[2] = 0x41990008; // bgt admits indices above the bound.
    if (variant == 3) code[2] = 0x4099000C; // Wrong dispatch entry.
    if (variant == 4) code[3] = 0x48000004; // Default also enters the dispatch.
    if (variant == 5) code[5] = 0x39600000; // Index clobbered after the guard.
    if (variant == 6) code[0] = 0x48000010; // Entry bypasses the guard.
    if (variant == 7) code[1] |= 0x00200000; // 64-bit compare is not this proof.
    if (variant == 8) code[1] = 0x2F0B0282; // Signed compare is not an unsigned bound.
    for (unsigned i = 0; i < std::size(code); ++i) put(kXexText + i * 4, code[i]);
    put(kXexPdata + 4, (1u << 30) | (11u << 8));
    for (unsigned i = 0; i < 643; ++i)
      put(kXexBase + 0x1400 + i * 4,
          kXexText + (i < 512 ? 44 : i < 642 ? 52 : 60));

    Workdir work("x360n_codegen", variant == 0 ? "large_switch" : "split_default_negative");
    work.write("test.xex", bytes);
    std::ofstream(work.dir / "test.toml") <<
        "project_name='large_switch'\nfile_path='test.xex'\nout_directory_path='out'\n";
    auto pipeline = rex::codegen::CodegenPipeline::Create(work.dir / "test.toml");
    REQUIRE(pipeline.has_value());
    REQUIRE(pipeline->RunAnalyze().has_value());
    const auto* function = pipeline->context().graph.getFunction(kXexText);
    REQUIRE(function);
    REQUIRE(function->jumpTables().size() == 1);
    const auto& table = function->jumpTables().front();
    CHECK(table.targets.size() == (variant == 0 ? 643 : 512));
    CHECK(table.tableByteLength == table.targets.size() * 4);
    REQUIRE(pipeline->RunWrite().has_value());
    if (variant == 0) {
      std::ifstream source(work.dir / "out" / "large_switch_recomp.0.cpp");
      REQUIRE(source.good());
      const std::string emitted(std::istreambuf_iterator<char>(source), {});
      CHECK(emitted.find("case 642:") != std::string::npos);
    }
  }
}
