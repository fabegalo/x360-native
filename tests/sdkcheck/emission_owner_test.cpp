#include <catch2/catch_test_macros.hpp>
#include "codegen/pipeline.h"
#include "codegen/codegen_context.h"
#include "synthetic_xex.h"
#include <fstream>
#include <iterator>

using namespace x360n::test;
namespace {
void Put(std::vector<uint8_t>& bytes, uint32_t address, uint32_t word) {
  const auto offset = 0x1000 + address - kXexBase;
  for (unsigned i = 0; i < 4; ++i) bytes.at(offset + i) = uint8_t(word >> (24 - 8 * i));
}
std::string Emitted(const Workdir& work) {
  std::string result;
  for (const auto& entry : std::filesystem::directory_iterator(work.dir / "out")) {
    if (entry.path().filename().string().find("_recomp.") == std::string::npos) continue;
    std::ifstream in(entry.path(), std::ios::binary);
    result.append(std::istreambuf_iterator<char>(in), {});
  }
  return result;
}
}

TEST_CASE("A generated module exports its dispatch capability exactly once",
          "[sdk][codegen][module-registration]") {
  Workdir work("x360n_emission", "module_registration");
  work.write("test.xex", BuildXex({}, nullptr, 0, Entry::kReturnZero));
  std::ofstream(work.dir / "test.toml")
      << "project_name='module_registration'\nfile_path='test.xex'\nout_directory_path='out'\n";
  auto pipeline = rex::codegen::CodegenPipeline::Create(work.dir / "test.toml", work.GetTools());
  REQUIRE(pipeline);
  REQUIRE(pipeline->RunAnalyze());
  pipeline->GetContext().SetDLLModule(true);
  REQUIRE(pipeline->RunWrite());
  std::ifstream file(work.dir / "out/module_registration_register.cpp", std::ios::binary);
  REQUIRE(file.good());
  const std::string emitted(std::istreambuf_iterator<char>(file), {});
  const std::string capability = "bool ReXModule_UsesHostFunctionTable()";
  const auto definition = emitted.find(capability);
  REQUIRE(definition != std::string::npos);
  CHECK(emitted.find(capability, definition + capability.size()) == std::string::npos);
  CHECK(emitted.find("ReXRegisterModule(rex::kernel::IFunctionRegistrar*") != std::string::npos);
  CHECK(emitted.find("const rex::ppc::PPCImage* ReXGetModuleImage()") != std::string::npos);
}

TEST_CASE("Emission uses the owning function for out of line conditional labels",
          "[sdk][codegen][emission-owner]") {
  auto bytes = BuildXex({}, nullptr, 0, Entry::kReturnZero);
  Put(bytes, kXexPdata + 4, (1u << 30) | (22u << 8));
  for (unsigned offset = 0; offset < 88; offset += 4) Put(bytes, kXexText + offset, 0x60000000);
  Put(bytes, kXexText, 0x48000040);  // b text+64
  Put(bytes, kXexText + 64, 0x2C030000);  // cmpwi r3,0
  Put(bytes, kXexText + 68, 0x4182000C);  // beq text+80
  Put(bytes, kXexText + 72, 0x38600001);
  Put(bytes, kXexText + 76, 0x4E800020);
  Put(bytes, kXexText + 80, 0x38600002);
  Put(bytes, kXexText + 84, 0x4E800020);
  Workdir work("x360n_emission", "out_of_line_owner");
  work.write("test.xex", bytes);
  std::ofstream(work.dir / "test.toml") << "project_name='owner'\nfile_path='test.xex'\nout_directory_path='out'\n";
  auto pipeline = rex::codegen::CodegenPipeline::Create(work.dir / "test.toml", work.GetTools());
  REQUIRE(pipeline.has_value());
  REQUIRE(pipeline->RunAnalyze().has_value());
  auto& graph = pipeline->GetContext().graph;
  const auto* owner = graph.GetFunction(kXexText);
  REQUIRE(owner);
  REQUIRE(owner->ContainsAddress(kXexText + 68));
  REQUIRE(owner->ContainsAddress(kXexText + 80));
  // A separate entry in the address gap shadows the owner's discontiguous
  // blocks in the nearest-base lookup. Emission already has the exact owner.
  auto* between = graph.AddFunction(kXexText + 32, 4, rex::codegen::FunctionAuthority::kDiscovered);
  REQUIRE(between);
  between->Discover({{kXexText + 32, 4}}, {}, {});
  between->Seal();
  REQUIRE(graph.GetFunctionContaining(kXexText + 68) == nullptr);
  REQUIRE(pipeline->RunWrite().has_value());
  const auto emitted = Emitted(work);
  CHECK(emitted.find("loc_82010050:") != std::string::npos);
  CHECK(emitted.find("Unresolved branch") == std::string::npos);
}

TEST_CASE("Merge records a tail edge when an internal label becomes a callable entry",
          "[sdk][codegen][emission-owner]") {
  auto bytes = BuildXex({}, nullptr, 0, Entry::kReturnZero);
  for (unsigned offset = 32; offset < 104; offset += 4) Put(bytes, kXexText + offset, 0x60000000);
  Put(bytes, kXexText + 32, 0x48000040);  // b text+96
  Put(bytes, kXexText + 96, 0x38600025);
  Put(bytes, kXexText + 100, 0x4E800020);
  Workdir work("x360n_emission", "late_callable_label");
  work.write("test.xex", bytes);
  std::ofstream(work.dir / "test.toml") << "project_name='late_entry'\nfile_path='test.xex'\nout_directory_path='out'\n";
  auto pipeline = rex::codegen::CodegenPipeline::Create(work.dir / "test.toml", work.GetTools());
  REQUIRE(pipeline.has_value());
  REQUIRE(pipeline->RunAnalyze().has_value());
  auto& graph = pipeline->GetContext().graph;
  auto* caller = graph.GetFunction(kXexText + 32);
  REQUIRE(caller);
  REQUIRE(caller->ContainsAddress(kXexText + 96));
  REQUIRE_FALSE(graph.IsEntryPoint(kXexText + 96));
  // Model an entry found after this caller's blocks were discovered. Its
  // original branch was a label, so it has no unresolved jump to resolve.
  auto* target = graph.AddFunction(kXexText + 96, 8, rex::codegen::FunctionAuthority::kDiscovered, true);
  REQUIRE(target);
  target->Discover({{kXexText + 96, 8}}, {}, {});
  target->Seal();
  REQUIRE(rex::codegen::detail::MergeFunctions(pipeline->GetContext()).has_value());
  REQUIRE(pipeline->RunWrite().has_value());
  const auto emitted = Emitted(work);
  CHECK(emitted.find("Unresolved call") == std::string::npos);
  CHECK(emitted.find("sub_82010060(ctx, base);") != std::string::npos);
}
