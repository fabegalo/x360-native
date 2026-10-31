#include <catch2/catch_test_macros.hpp>
#include <rex/codegen/codegen.h>
#include "synthetic_xex.h"
#include <fstream>
#include <iterator>

using namespace x360n::test;

TEST_CASE("Referenced shared tails retain independent callable entries",
          "[sdk][codegen][gapfill][shared-tail]") {
  for (unsigned variant = 0; variant < 10; ++variant) {
    CAPTURE(variant);
    auto bytes = BuildXex({}, nullptr, 0, Entry::kReturnZero);
    const auto put = [&](uint32_t offset, uint32_t word) {
      for (unsigned i = 0; i < 4; ++i)
        bytes.at(0x1000 + kXexText - kXexBase + offset + i) =
            uint8_t(word >> (24 - 8 * i));
    };
    // An argument-adapting wrapper branches into a separately addressable tail.
    put(8, 0x7CA42B78); // mr r4,r5
    put(12, 0x48000004); // b text+16
    for (unsigned offset = 16; offset < 336; offset += 4) put(offset, 0x60000000);
    put(336, 0x38600001); put(340, 0x4E800020);
    if (variant == 1 || variant == 9) put(16, 0x4E800020);
    if (variant == 2) put(12, 0x48000008); // Branch skips the referenced address.
    if (variant == 3) put(12, 0x41820004); // Conditional predecessor can fall through.
    if (variant == 4) put(kXexPdata - kXexText + 4, (1u << 30) | (86u << 8));
    // A data reference discovers the wrapper before the constructor's tail
    // reference is scanned. Its inferred CFG is not a declared function extent.
    if (variant >= 8) put(kXexSlots - kXexText + 0x80, kXexText + 8);
    put(384, 0x3D608201); // lis r11,0x8201
    put(388, variant == 6 ? 0x39600000 : 0x60000000); // Clobber or nop.
    put(392, variant == 7 ? 0x392B0014 : 0x392B0010); // addi r9,r11,tail@l
    put(396, 0x91230000); put(400, 0x4E800020);

    Workdir work("x360n_discovery", "shared_tail");
    work.write("test.xex", bytes);
    std::ofstream config(work.dir / "test.toml");
    config << "project_name='shared_tail'\nfile_path='test.xex'\nout_directory_path='out'\n";
    if (variant == 5) config << "[functions]\n0x82010008={size=336}\n";
    config.close();
    auto pipeline = rex::codegen::CodegenPipeline::Create(work.dir / "test.toml");
    REQUIRE(pipeline.has_value());
    REQUIRE(pipeline->RunAnalyze().has_value());
    const bool callable = variant < 2 || variant >= 8;
    CHECK(pipeline->context().graph.isEntryPoint(kXexText + 16) == callable);
    REQUIRE(pipeline->RunWrite().has_value());
    std::ifstream init(work.dir / "out" / "shared_tail_init.cpp");
    REQUIRE(init.good());
    const std::string registration(std::istreambuf_iterator<char>(init), {});
    CHECK((registration.find("0x82010010, sub_82010010") != std::string::npos) ==
          callable);
    if (callable) {
      std::string emitted;
      for (const auto& entry : std::filesystem::directory_iterator(work.dir / "out")) {
        if (entry.path().filename().string().find("_recomp.") == std::string::npos)
          continue;
        std::ifstream source(entry.path(), std::ios::binary);
        emitted.append(std::istreambuf_iterator<char>(source), {});
      }
      CHECK(emitted.find("sub_82010010(ctx, base);") != std::string::npos);
      CHECK(emitted.find("Unresolved") == std::string::npos);
    }
  }
}
