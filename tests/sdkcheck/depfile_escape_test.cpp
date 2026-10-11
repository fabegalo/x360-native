// Diagnosis for the failing SDK test "Depfile escapes characters make treats
// as syntax" (tests/unit/codegen/output_stamp_test.cpp:227-228 at v0.10.0).
// That test spells the expected text as "has\ space.toml", an unknown escape
// that clang compiles to "has space.toml" (-Wunknown-escape-sequence). This
// check uses the intended literal and shows WriteDepfile itself escapes
// correctly, so the upstream failure is in the test, not in codegen.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <rex/codegen/output_stamp.h>

namespace fs = std::filesystem;

TEST_CASE("WriteDepfile escapes space and hash with a backslash", "[depfile]") {
  fs::path root = fs::temp_directory_path() / "x360n_depfile_check";
  fs::remove_all(root);
  fs::create_directories(root);
  std::vector<fs::path> inputs{root / "has space.toml", root / "hash#.toml", root / "dollar$.toml"};
  REQUIRE(rex::codegen::WriteDepfile(root / "escaped.d", root / "out.stamp", inputs));

  std::ifstream in(root / "escaped.d", std::ios::binary);
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  CHECK(content.find("has\\ space.toml") != std::string::npos);
  CHECK(content.find("hash\\#.toml") != std::string::npos);
  CHECK(content.find("dollar$$.toml") != std::string::npos);
  // The literal the upstream test searches for is not present, as expected.
  CHECK(content.find("has space.toml") == std::string::npos);
  in.close();
  fs::remove_all(root);
}
