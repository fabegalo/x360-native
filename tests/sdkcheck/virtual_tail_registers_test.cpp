#include <catch2/catch_test_macros.hpp>
#include "codegen/pipeline.h"
#include "synthetic_xex.h"
#include <fstream>

using namespace x360n::test;

TEST_CASE("Virtual tail boundaries preserve distinct volatile load registers",
          "[sdk][codegen][gapfill][virtual-tail-registers]") {
  for (uint32_t object : {10u, 11u, 12u}) {
    for (uint32_t target : {10u, 11u, 12u}) {
      CAPTURE(object, target);
      auto bytes = BuildXex({}, nullptr, 0, Entry::kReturnZero);
      const auto put = [&](uint32_t offset, uint32_t word) {
        for (unsigned i = 0; i < 4; ++i)
          bytes.at(0x1000 + kXexText - kXexBase + offset + i) = uint8_t(word >> (24 - i * 8));
      };
      // A leaf follows a frameless virtual tail call, without padding or pdata.
      // The vtable address and loaded code pointer may use different scratch GPRs.
      put(8, (32u << 26) | (object << 21) | (3u << 16));
      put(12, (32u << 26) | (target << 21) | (object << 16) | 28u);
      put(16, 0x7C0903A6 | (target << 21));
      put(20, 0x4E800420);
      put(24, 0x38600019); // li r3,25
      put(28, 0x4E800020);
      Workdir work("x360n_discovery", "volatile_tail_registers");
      work.write("test.xex", bytes);
      std::ofstream(work.dir / "test.toml")
          << "project_name='tails'\nfile_path='test.xex'\nout_directory_path='out'\n";
      auto pipeline = rex::codegen::CodegenPipeline::Create(work.dir / "test.toml", work.GetTools());
      REQUIRE(pipeline.has_value());
      REQUIRE(pipeline->RunAnalyze().has_value());
      const auto& graph = pipeline->GetContext().graph;
      const auto* thunk = graph.GetFunction(kXexText + 8);
      REQUIRE(thunk);
      REQUIRE(thunk->GetBlocks().size() == 1);
      CHECK(thunk->GetBlocks().front().size == 16);
      const auto* leaf = graph.GetFunction(kXexText + 24);
      REQUIRE(leaf);
      CHECK(leaf->GetBlocks().front().size == 8);
      CHECK(thunk->GetJumpTables().empty());
      REQUIRE(pipeline->RunWrite().has_value());
    }
  }
}
