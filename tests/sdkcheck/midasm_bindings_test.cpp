#include <catch2/catch_test_macros.hpp>
#include <toml++/toml.hpp>
#include <filesystem>
#include <fstream>
#include "codegen/config.h"

TEST_CASE("Title mid-asm bindings satisfy the pinned codegen register contract", "[codegen][midasm-bindings]") {
  namespace fs = std::filesystem;
  const auto titles = fs::path(X360N_SOURCE_ROOT) / "titles";
  size_t hooks = 0;
  for (auto it = fs::recursive_directory_iterator(titles); it != fs::recursive_directory_iterator(); ++it) {
    if (it->is_directory() && it->path().filename() == "generated") {
      it.disable_recursion_pending();
      continue;
    }
    if (!it->is_regular_file() || it->path().extension() != ".toml") continue;
    INFO(it->path().generic_string());
    std::ifstream input(it->path());
    REQUIRE(input);
    const auto table = toml::parse(input);
    const auto* entries = table["midasm_hook"].as_array();
    if (!entries) continue;
    for (const auto& entry : *entries) {
      const auto* binding = entry.as_table();
      REQUIRE(binding);
      rex::codegen::RecompilerConfig config;
      config.file_path = "fixture.xex";
      config.out_directory_path = "out";
      rex::codegen::MidAsmHook hook;
      hook.name = (*binding)["name"].value_or(std::string{});
      INFO(hook.name);
      if (const auto* registers = (*binding)["registers"].as_array())
        for (const auto& reg : *registers) {
          REQUIRE(reg.is_string());
          hook.registers.push_back(reg.value_or(std::string{}));
        }
      config.mid_asm_hooks.emplace((*binding)["address"].value_or(uint32_t{}), std::move(hook));
      const auto validation = config.Validate();
      for (const auto& error : validation.errors) INFO(error);
      CHECK(validation.valid);
      ++hooks;
    }
  }
  CHECK(hooks > 0);
}
