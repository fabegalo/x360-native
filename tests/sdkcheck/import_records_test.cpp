// Function imports whose value slot and thunk are in different import library records.
//
// At f5337cdc, XexModule::SetupLibraryImports paired the two records of a function
// import (type 0 = value slot, type 1 = thunk) by ordinal only inside one library
// record. dash.xex 2.0.17559.0 has xam split over two records, with the slot of
// xam 0x8E2 in the first and its thunk in the second: the slot half became an import
// symbol at address 0, the codegen wrote { 0x0, __imp__XamNuiGetSupportString } as the
// first PPCFuncMappings entry, and the runtime, which stops at guest == 0, registered
// no function (docs/dashboard.md §0.7). patches/rexglue-sdk/0003 pairs the halves
// across the records of one library and makes a function import without any thunk a
// codegen validation error instead of a function at address 0.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <rex/codegen/codegen.h>
#include <rex/codegen/codegen_context.h>
#include <rex/codegen/function_graph.h>
#include <rex/codegen/function_node.h>
#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/system/export_resolver.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <rex/system/xex_module.h>

#include "synthetic_xex.h"

using rex::X_STATUS;  // X_STATUS_SUCCESS expands to a cast to it
using namespace x360n::test;

namespace {

namespace fs = std::filesystem;

// xam ordinals of the SDK export table, checked against the resolver below.
constexpr uint16_t kFnSplit = 0x8E2;  // XamNuiGetSupportString
constexpr uint16_t kFnA = 0x033;      // NetDll_XNetStartup
constexpr uint16_t kFnB = 0x034;      // NetDll_XNetCleanup
constexpr uint16_t kVar = 0x3DB;      // g_XuiAutomation

constexpr const char* kGroup = "x360n_import_records_test";

// The loader in tool mode, as the codegen and x360n-content use it.
struct Loaded {
  std::unique_ptr<rex::Runtime> runtime;
  rex::runtime::XexModule* xex = nullptr;
};
Loaded Load(const Workdir& w) {
  Loaded l;
  l.runtime = std::make_unique<rex::Runtime>(w.dir);
  rex::RuntimeConfig config{};
  config.kernel_init = rex::kernel::InitializeKernel;
  config.tool_mode = true;
  REQUIRE(l.runtime->Setup(std::move(config)) == X_STATUS_SUCCESS);
  REQUIRE(l.runtime->LoadXexImage("game:\\test.xex") == X_STATUS_SUCCESS);
  auto module = l.runtime->kernel_state()->GetExecutableModule();
  REQUIRE(module);
  l.xex = module->xex_module();
  REQUIRE(l.xex);
  return l;
}

void RequireExport(rex::Runtime* rt, uint16_t ordinal, rex::runtime::Export::Type type) {
  auto* e = rt->export_resolver()->GetExportByOrdinal("xam.xex", ordinal);
  REQUIRE(e);
  REQUIRE(e->type == type);
}

// PPCFuncMappings from <project>_init.cpp, read the way Runtime::Setup reads the array:
// up to the first entry with guest == 0.
struct Mappings {
  std::vector<std::pair<uint32_t, std::string>> all;  // every line, terminator included
  size_t registered = 0;                                // entries before the first guest 0
};
Mappings ReadMappings(const fs::path& init_cpp) {
  std::ifstream in(init_cpp);
  REQUIRE(in);
  std::stringstream ss;
  ss << in.rdbuf();
  const std::string text = ss.str();
  const auto begin = text.find("PPCFuncMappings[] = {");
  REQUIRE(begin != std::string::npos);
  const auto end = text.find("};", begin);
  REQUIRE(end != std::string::npos);
  const std::string body = text.substr(begin, end - begin);
  Mappings m;
  static const std::regex line(R"(\{ (0x[0-9A-Fa-f]+|0), ([A-Za-z0-9_]+) \})");
  for (auto it = std::sregex_iterator(body.begin(), body.end(), line); it != std::sregex_iterator();
       ++it) {
    m.all.emplace_back(uint32_t(std::stoul((*it)[1].str(), nullptr, 16)), (*it)[2].str());
  }
  while (m.registered < m.all.size() && m.all[m.registered].first != 0) ++m.registered;
  return m;
}
size_t CountMapping(const Mappings& m, uint32_t guest, const std::string& name) {
  size_t n = 0;
  for (size_t i = 0; i < m.registered; ++i) n += m.all[i].first == guest && m.all[i].second == name;
  return n;
}
bool HasGuest(const Mappings& m, uint32_t guest) {
  for (size_t i = 0; i < m.registered; ++i) {
    if (m.all[i].first == guest) return true;
  }
  return false;
}

void WriteConfig(const Workdir& w) {
  std::ofstream(w.dir / "test_config.toml") << "project_name = \"imports\"\n"
                                               "file_path = \"test.xex\"\n"
                                               "out_directory_path = \"out\"\n";
}

}  // namespace

TEST_CASE("a function import split over two library records keeps its slot and thunk together",
          "[sdk][xex][codegen]") {
  // Record 1: A (slot + thunk), the slot of the split import, a variable.
  // Record 2: the thunk of the split import, B (slot + thunk).
  std::vector<Placed> placed;
  const auto xex = BuildXex({{{kFnA, true, true}, {kFnSplit, true, false}, {kVar, true, false}},
                             {{kFnSplit, false, true}, {kFnB, true, true}}},
                            &placed, kFnSplit);
  uint32_t split_slot = 0, split_thunk = 0, thunk_a = 0, thunk_b = 0;
  for (const auto& p : placed) {
    if (p.ordinal == kFnSplit && p.slot) split_slot = p.slot;
    if (p.ordinal == kFnSplit && p.thunk) split_thunk = p.thunk;
    if (p.ordinal == kFnA) thunk_a = p.thunk;
    if (p.ordinal == kFnB) thunk_b = p.thunk;
  }
  REQUIRE(split_slot != 0);
  REQUIRE(split_thunk != 0);
  Workdir w(kGroup, "split");
  w.write("test.xex", xex);

  SECTION("the loader pairs the two halves into one import") {
    auto l = Load(w);
    RequireExport(l.runtime.get(), kFnSplit, rex::runtime::Export::Type::kFunction);
    RequireExport(l.runtime.get(), kVar, rex::runtime::Export::Type::kVariable);
    REQUIRE(l.xex->import_libraries()->size() == 2);  // both library records are kept

    size_t entries = 0;
    for (const auto& lib : *l.xex->import_libraries()) {
      for (const auto& imp : lib.imports) {
        if (imp.ordinal != kFnSplit) continue;
        ++entries;
        CHECK(imp.value_address == split_slot);
        CHECK(imp.thunk_address == split_thunk);
      }
    }
    CHECK(entries == 1);

    size_t symbols = 0;
    const auto sym_name = fmt::format("xam@{}", kFnSplit);
    for (const auto& sym : l.xex->binary_symbols()) {
      if (sym.name != sym_name) continue;
      ++symbols;
      CHECK(sym.address == split_thunk);
    }
    CHECK(symbols == 1);
  }

  SECTION("the codegen maps the import at its thunk and nothing at guest address 0") {
    WriteConfig(w);
    auto pipeline = rex::codegen::CodegenPipeline::Create(w.dir / "test_config.toml");
    REQUIRE(pipeline.has_value());
    auto* resolver = pipeline->context().resolver();
    REQUIRE(resolver);
    auto* split_export = resolver->GetExportByOrdinal("xam.xex", kFnSplit);
    REQUIRE(split_export);
    const std::string split_fn = std::string("__imp__") + split_export->name;
    const std::string a_fn =
        std::string("__imp__") + resolver->GetExportByOrdinal("xam.xex", kFnA)->name;
    const std::string b_fn =
        std::string("__imp__") + resolver->GetExportByOrdinal("xam.xex", kFnB)->name;

    auto run = pipeline->Run(false);
    INFO((run.has_value() ? std::string() : run.error().message));
    for (const auto& e : pipeline->context().errors.Entries()) {
      UNSCOPED_INFO(rex::codegen::AnalysisErrors::CategoryName(e.category) << ": " << e.message);
    }
    REQUIRE(run.has_value());
    CHECK(pipeline->context().errors.Count() == 0);
    CHECK(pipeline->context().graph.getFunction(0) == nullptr);
    const auto* node = pipeline->context().graph.getFunction(split_thunk);
    REQUIRE(node);
    CHECK(node->name() == split_fn);

    const auto m = ReadMappings(w.dir / "out" / "imports_init.cpp");
    REQUIRE(!m.all.empty());
    CHECK(m.all.back() == std::pair<uint32_t, std::string>{0, "nullptr"});
    CHECK(m.registered == m.all.size() - 1);  // the only guest 0 is the terminator
    CHECK(CountMapping(m, split_thunk, split_fn) == 1);
    CHECK(CountMapping(m, thunk_a, a_fn) == 1);
    CHECK(CountMapping(m, thunk_b, b_fn) == 1);
    CHECK(HasGuest(m, kXexText));  // the entry point
  }
}

TEST_CASE("two complete records of one ordinal stay two imports", "[sdk][xex]") {
  // Not a split: each record has its own slot and thunk. Both thunks are code the
  // title can call, so neither may be dropped by the pairing.
  std::vector<Placed> placed;
  const auto xex = BuildXex({{{kFnA, true, true}}, {{kFnA, true, true}}}, &placed);
  Workdir w(kGroup, "complete");
  w.write("test.xex", xex);
  auto l = Load(w);

  std::vector<uint32_t> thunks;
  for (const auto& lib : *l.xex->import_libraries()) {
    for (const auto& imp : lib.imports) {
      if (imp.ordinal != kFnA) continue;
      CHECK(imp.value_address != 0);
      thunks.push_back(imp.thunk_address);
    }
  }
  REQUIRE(placed.size() == 2);
  CHECK(thunks == std::vector<uint32_t>{placed[0].thunk, placed[1].thunk});
}

TEST_CASE("a function import with a value slot and no thunk is a codegen error, not a mapping at 0",
          "[sdk][codegen]") {
  // kFnSplit has only its slot here, in the only record; nothing pairs it.
  std::vector<Placed> placed;
  const auto xex = BuildXex({{{kFnA, true, true}, {kFnSplit, true, false}, {kVar, true, false}}},
                            &placed, kFnA);
  Workdir w(kGroup, "slot_only");
  w.write("test.xex", xex);
  WriteConfig(w);

  auto pipeline = rex::codegen::CodegenPipeline::Create(w.dir / "test_config.toml");
  REQUIRE(pipeline.has_value());
  auto analyzed = pipeline->RunAnalyze();
  CHECK_FALSE(analyzed.has_value());  // validation fails: the import cannot be honoured
  // By name, so that this also builds against the unpatched SDK (before/after runs).
  size_t without_thunk = 0;
  for (const auto& e : pipeline->context().errors.Entries()) {
    without_thunk += std::string(rex::codegen::AnalysisErrors::CategoryName(e.category)) ==
                     "ImportWithoutThunk";
  }
  CHECK(without_thunk == 1);
  CHECK(pipeline->context().graph.getFunction(0) == nullptr);
  CHECK(pipeline->context().graph.getFunction(placed[0].thunk) != nullptr);  // A is intact

  // Without --force nothing is generated.
  auto written = pipeline->RunWrite(false);
  CHECK_FALSE(written.has_value());
  CHECK_FALSE(fs::exists(w.dir / "out" / "imports_init.cpp"));
}
