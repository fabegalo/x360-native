#include <catch2/catch_test_macros.hpp>
#include <rex/codegen/codegen.h>
#include "synthetic_xex.h"
#include <fstream>
#include <iterator>

using namespace x360n::test;

TEST_CASE("Callbacks materialized by gap discovered code remain callable",
          "[sdk][codegen][gapfill][late-callback]") {
  for (unsigned variant = 0; variant < 6; ++variant) {
    CAPTURE(variant);
    auto bytes = BuildXex({}, nullptr, 0, Entry::kReturnZero);
    const auto put = [&](uint32_t address, uint32_t word) {
      for (unsigned i = 0; i < 4; ++i)
        bytes.at(0x1000 + address - kXexBase + i) = uint8_t(word >> (24 - 8 * i));
    };
    // A callback follows an argument-storing virtual tail, without PDATA or BL.
    const uint32_t thunk[] = {0x81640000, 0x90640008, 0x7C832378,
                             0x814B0020, 0x7D4903A6, 0x4E800420};
    for (unsigned i = 0; i < 6; ++i) put(kXexText + 8 + 4 * i, thunk[i]);
    put(kXexText + 32, 0x38600019); put(kXexText + 36, 0x4E800020);
    if (variant == 4) put(kXexText + 28, 0x4D820420); // Conditional predecessor.
    // This constructor is found by gap fill, after the initial reference scan.
    put(kXexText + 64, 0x3D608201); // lis r11,0x8201
    put(kXexText + 68, variant == 2 ? 0x39600000 : 0x60000000);
    put(kXexText + 72, 0x392B0020); // addi r9,r11,callback@l
    put(kXexText + 76, 0x91230000); // stw r9,0(r3)
    put(kXexText + 80, 0x4E800020);
    if (variant == 3) put(kXexPdata + 4, (1u << 30) | (10u << 8));
    Workdir work("x360n_discovery", "late_callback");
    work.write("test.xex", bytes);
    std::ofstream config(work.dir / "test.toml");
    config << "project_name='late_callback'\nfile_path='test.xex'\nout_directory_path='out'\n";
    if (variant == 1) config << "[functions]\n0x82010040={size=20}\n";
    if (variant == 5) config << "[functions]\n0x82010008={size=32}\n";
    config.close();
    auto pipeline = rex::codegen::CodegenPipeline::Create(work.dir / "test.toml");
    REQUIRE(pipeline.has_value());
    REQUIRE(pipeline->RunAnalyze().has_value());
    CHECK(pipeline->context().graph.isEntryPoint(kXexText + 32) == (variant < 2));
    REQUIRE(pipeline->RunWrite().has_value());
    std::ifstream init(work.dir / "out" / "late_callback_init.cpp");
    REQUIRE(init.good());
    const std::string registration(std::istreambuf_iterator<char>(init), {});
    CHECK((registration.find("0x82010020, sub_82010020") != std::string::npos) ==
          (variant < 2));
  }
}
