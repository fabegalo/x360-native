#include <catch2/catch_test_macros.hpp>
#include <rex/codegen/codegen.h>
#include "synthetic_xex.h"
#include <array>
#include <fstream>

using namespace x360n::test;

TEST_CASE("Descriptor tail thunks preserve adjacent callable boundaries",
          "[sdk][codegen][gapfill][descriptor-tail]") {
  for (unsigned variant = 0; variant < 12; ++variant) {
    CAPTURE(variant);
    auto bytes = BuildXex({}, nullptr, 0, Entry::kReturnZero);
    const auto put = [&](uint32_t offset, uint32_t word) {
      for (unsigned i = 0; i < 4; ++i)
        bytes.at(0x1000 + kXexText - kXexBase + offset + i) = uint8_t(word >> (24 - i * 8));
    };
    const uint32_t descriptor = variant == 1 ? 12 : 11;
    const uint32_t object = variant == 1 ? 9 : 10;
    const uint32_t adjustment = variant == 1 ? 10 : 9;
    const auto mr = [](uint32_t to, uint32_t from) {
      return 0x7C000378u | (from << 21) | (to << 16) | (from << 11);
    };
    const auto lwz = [](uint32_t to, uint32_t from, uint32_t offset) {
      return (32u << 26) | (to << 21) | (from << 16) | offset;
    };
    // A closure descriptor holds {callable, this adjustment, object}; the
    // invoker preserves the incoming argument before tailing to its callable.
    std::array<uint32_t, 8> thunk{
        mr(descriptor, 4), mr(4, 3), lwz(object, descriptor, 8),
        lwz(adjustment, descriptor, 4), lwz(descriptor, descriptor, 0),
        0x7C000214u | (3u << 21) | (object << 16) | (adjustment << 11),
        0x7C0903A6u | (descriptor << 21), 0x4E800420};
    if (variant == 2) thunk[0] = mr(descriptor, 5); // Different descriptor argument.
    if (variant == 3) thunk[1] = mr(4, 5);          // Not the preserved argument.
    if (variant == 4) thunk[2] ^= 4;               // Different descriptor layout.
    if (variant == 5) thunk[3] = lwz(object, descriptor, 4); // Clobbers object.
    if (variant == 6) thunk[4] = lwz(descriptor, descriptor, 12);
    if (variant == 7) thunk[5] ^= (1u << 11);      // Adds a different source.
    if (variant == 8) thunk[6] ^= (1u << 21);      // Different CTR source.
    if (variant == 9) thunk[7] = 0x4D820420;       // Conditional transfer.
    for (unsigned i = 0; i < thunk.size(); ++i) put(8 + 4 * i, thunk[i]);
    put(40, 0x38600019); put(44, 0x4E800020);
    // Original metadata and explicit extents still own unreachable bytes.
    if (variant == 10) put(kXexPdata - kXexText + 4, (1u << 30) | (12u << 8));
    Workdir work("x360n_discovery", "descriptor_tail");
    work.write("test.xex", bytes);
    std::ofstream config(work.dir / "test.toml");
    config << "project_name='descriptor'\nfile_path='test.xex'\nout_directory_path='out'\n";
    if (variant == 11) config << "[functions]\n0x82010008 = { size = 40 }\n";
    config.close();
    auto pipeline = rex::codegen::CodegenPipeline::Create(work.dir / "test.toml");
    REQUIRE(pipeline.has_value());
    REQUIRE(pipeline->RunAnalyze().has_value());
    const auto& graph = pipeline->context().graph;
    CHECK(graph.isEntryPoint(kXexText + 40) == (variant < 2));
    if (variant < 2) {
      const auto* invoker = graph.getFunction(kXexText + 8);
      REQUIRE(invoker);
      CHECK(invoker->blocks().front().size == 32);
      CHECK(invoker->jumpTables().empty());
    }
    REQUIRE(pipeline->RunWrite().has_value());
  }
}
