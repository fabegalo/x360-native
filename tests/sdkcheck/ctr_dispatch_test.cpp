#include <catch2/catch_test_macros.hpp>
#include <rex/codegen/codegen.h>
#include "synthetic_xex.h"
#include <fstream>
#include <regex>
#include <set>

using namespace x360n::test;

TEST_CASE("Byte computed dispatch preserves CTR without guessing table length",
          "[sdk][codegen][ctr-dispatch]") {
  for (const unsigned pdataWords : {11u, 15u}) {
    for (const bool declaredNeighbor : {false, true}) {
      CAPTURE(pdataWords);
      CAPTURE(declaredNeighbor);
      std::vector<Placed> placed;
      auto bytes = BuildXex({}, &placed, 0, Entry::kReturnZero);
      auto put = [&](uint32_t address, uint32_t word) {
        const size_t offset = 0x1000 + address - kXexBase;
        for (unsigned i = 0; i < 4; ++i)
          bytes.at(offset + i) = uint8_t(word >> (24 - 8 * i));
      };
      // Unknown index loaded from an object; no bounds check. Data after the
      // two intended entries also decodes as executable targets, inside the
      // next body.
      const uint32_t code[] = {0x81630000, 0x3D808200, 0x398C1200, 0x7C0C58AE,
                               0x5400103A, 0x3D808201, 0x60000000, 0x398C002C,
                               0x7D8C0214, 0x7D8903A6, 0x4E800420, 0x3860000B,
                               0x4E800020, 0x38600016, 0x4E800020, 0x38600021,
                               0x3860002C, 0x4E800020};
      for (unsigned i = 0; i < std::size(code); ++i)
        put(kXexText + i * 4, code[i]);
      // MSVC may put case bodies beyond the metadata extent, before the next
      // entry.
      put(kXexPdata + 4, (1u << 30) | (pdataWords << 8));
      if (pdataWords == 11 && declaredNeighbor)
        put(kXexText + 44,
            0x48000005); // bl to next word captures PC, not a new entry
      if (!declaredNeighbor) {
        put(kXexText + 128, 0x4BFFFFBD); // bl 0x8201003C from another function
        put(kXexText + 132, 0x4E800020);
      }
      for (size_t i = 0; i < 512; ++i)
        bytes.at(0x2200 + i) = 5;
      bytes.at(0x2200) = 0;
      bytes.at(0x2201) = 2;
      Workdir work("x360n_codegen", "byte_ctr");
      work.write("test.xex", bytes);
      std::ofstream config(work.dir / "test.toml");
      config << "project_name='byte_ctr'\nfile_path='test.xex'\nout_directory_"
                "path='out'\n[functions]\n";
      config << (declaredNeighbor ? "0x8201003C={size=12}\n"
                                  : "0x82010080={size=8}\n");
      config.close();
      auto pipeline =
          rex::codegen::CodegenPipeline::Create(work.dir / "test.toml");
      REQUIRE(pipeline.has_value());
      REQUIRE(pipeline->RunAnalyze().has_value());
      auto &ctx = pipeline->context();
      const auto *node = ctx.graph.getFunction(kXexText);
      REQUIRE(node);
      CHECK(node->authority() == rex::codegen::FunctionAuthority::PDATA);
      CHECK(node->end() == kXexText + 60);
      for (const auto &block : node->blocks())
        CHECK(block.end() <= kXexText + 60);
      const rex::codegen::EmitContext emit{
          ctx.binary(), ctx.Config(), ctx.graph,
          static_cast<uint32_t>(ctx.analysisState().entryPoint),
          ctx.resolver()};
      const auto source = node->emitCpp(emit);
      CHECK(source.find("REX_LOAD_U8") != std::string::npos);
      CHECK(source.find("switch (ctx.ctr.u32 & ~3u)") != std::string::npos);
      CHECK(source.find("case 0x8201002C:") != std::string::npos);
      CHECK(source.find("case 0x82010034:") != std::string::npos);
      CHECK(source.find("REX_CALL_INDIRECT_FUNC(ctx.ctr.u32 & ~3u)") !=
            std::string::npos);
      std::set<std::string> defined;
      const std::regex definition("(loc_[0-9A-F]+):"),
          jump("goto (loc_[0-9A-F]+);");
      for (auto it =
               std::sregex_iterator(source.begin(), source.end(), definition);
           it != std::sregex_iterator(); ++it)
        defined.insert((*it)[1]);
      for (auto it = std::sregex_iterator(source.begin(), source.end(), jump);
           it != std::sregex_iterator(); ++it)
        CHECK(defined.contains((*it)[1]));
      REQUIRE(pipeline->RunWrite().has_value());
    }
  }
}

TEST_CASE(
    "Finite byte destinations retain complete out-of-line successor paths",
    "[sdk][codegen][ctr-dispatch]") {
  auto bytes = BuildXex({}, nullptr, 0, Entry::kReturnZero);
  auto put = [&](uint32_t address, uint32_t word) {
    const size_t offset = 0x1000 + address - kXexBase;
    for (unsigned i = 0; i < 4; ++i)
      bytes.at(offset + i) = uint8_t(word >> (24 - 8 * i));
  };
  // Unscaled byte domain ends at offset 296. The reachable path continues to
  // offset 400, beyond both the domain and PDATA, before any other entry.
  const uint32_t code[] = {0x81630000, 0x3D808200, 0x398C1200, 0x7C0C58AE,
                           0x60000000, 0x3D808201, 0x60000000, 0x398C002C,
                           0x7D8C0214, 0x7D8903A6, 0x4E800420};
  for (unsigned i = 0; i < std::size(code); ++i)
    put(kXexText + i * 4, code[i]);
  for (unsigned offset = 44; offset < 396; offset += 4)
    put(kXexText + offset, 0x60000000);
  put(kXexText + 396, 0x3860002A);
  put(kXexText + 400, 0x4E800020);
  put(kXexPdata + 4, (1u << 30) | (11u << 8));
  Workdir work("x360n_codegen", "byte_successors");
  work.write("test.xex", bytes);
  std::ofstream(work.dir / "test.toml")
      << "project_name='byte_successors'\nfile_path='test.xex'\nout_directory_"
         "path='out'\n";
  auto pipeline = rex::codegen::CodegenPipeline::Create(work.dir / "test.toml");
  REQUIRE(pipeline);
  REQUIRE(pipeline->RunAnalyze());
  const auto *node = pipeline->context().graph.getFunction(kXexText);
  REQUIRE(node);
  CHECK(node->end() == kXexText + 404);
  bool includesReturn = false;
  for (const auto &block : node->blocks())
    includesReturn |= block.contains(kXexText + 400);
  CHECK(includesReturn);
  REQUIRE(pipeline->RunWrite());
}

TEST_CASE("Byte dispatch follows register definitions and exact PPC masks",
          "[sdk][codegen][ctr-dispatch]") {
  // Independent synthetic instruction variants: a different CTR source,
  // commuted add, signed base immediate, a wrapping mask, and clobbered chains.
  for (unsigned variant = 0; variant < 7; ++variant) {
    CAPTURE(variant);
    auto bytes = BuildXex({}, nullptr, 0, Entry::kReturnZero);
    auto put = [&](uint32_t address, uint32_t word) {
      const size_t offset = 0x1000 + address - kXexBase;
      for (unsigned i = 0; i < 4; ++i) bytes.at(offset + i) = uint8_t(word >> (24 - 8 * i));
    };
    uint32_t code[] = {
        0x81630000, 0x3D808200, 0x398C1200, 0x7C0C58AE, 0x5400103A,
        0x3D808201, 0x60000000, 0x398C002C, 0x7D8C0214, 0x7D8903A6, 0x4E800420,
        0x3860000B, 0x4E800020, 0x38600016, 0x4E800020};
    if (variant == 0) { code[8] = 0x7D406214; code[9] = 0x7D4903A6; } // add r10,r0,r12; mtctr r10
    if (variant == 1) code[7] = 0x398CFFFC; // base 0x8200FFFC (signed low)
    if (variant == 2) code[4] = (21u << 26) | (30u << 6) | (1u << 1); // rotate 0, wrapping mask
    if (variant == 3) code[4] = 0x60000000; // unscaled byte offsets, bctr clears low bits
    if (variant == 4) code[6] = 0x38000004; // li r0,4 clobbers loaded byte
    if (variant == 5) code[9] = 0x7D4903A6; // mismatched CTR source
    if (variant == 6) code[3] = 0x7C0C582E; // lwzx has no byte-sized domain
    for (unsigned i = 0; i < std::size(code); ++i) put(kXexText + i * 4, code[i]);
    put(kXexPdata + 4, (1u << 30) | (15u << 8));
    // Sufficient valid data for legacy fallback in the deliberately rejected variants.
    for (unsigned i = 0; i < 16; ++i) put(kXexBase + 0x1200 + i * 4, kXexText + 44);
    Workdir work("x360n_codegen", "byte_variants");
    work.write("test.xex", bytes);
    std::ofstream(work.dir / "test.toml") <<
        "project_name='byte_variants'\nfile_path='test.xex'\nout_directory_path='out'\n";
    auto pipeline = rex::codegen::CodegenPipeline::Create(work.dir / "test.toml");
    REQUIRE(pipeline);
    REQUIRE(pipeline->RunAnalyze());
    const auto* node = pipeline->context().graph.getFunction(kXexText);
    REQUIRE(node);
    bool targetDispatch = false;
    for (const auto& jt : node->jumpTables()) {
      if (jt.bctrAddress != kXexText + 40 || !jt.dispatchByTarget) continue;
      targetDispatch = true;
      std::set<uint32_t> expected;
      for (unsigned byte = 0; byte < 256; ++byte) {
        const auto offset = variant == 2 ? (byte & 0xC0000003u) : variant == 3 ? byte : byte * 4;
        const uint32_t base = variant == 1 ? kXexText - 4 : kXexText + 44;
        const uint32_t target = (base + offset) & ~3u;
        if (target >= kXexText && target < kXexText + 60) expected.insert(target);
      }
      CHECK(std::set<uint32_t>(jt.targets.begin(), jt.targets.end()) == expected);
    }
    CHECK(targetDispatch == (variant < 4));
  }
}
