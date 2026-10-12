// x360n-content apply-patch reads the AES master keys from a local file that is not
// part of the repository. These tests use no key material: only a missing file,
// malformed files and a zero key, which no real XEX uses for its body.
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "../supervisor/scratch.h"
#include "child_process.h"

namespace fs = std::filesystem;
using namespace x360n;
using namespace std::chrono_literals;

namespace {

std::string Slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

struct Scratch {
  fs::path dir = UniqueTempPath("apply_patch_keys_test");
  Scratch() {
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::ofstream(dir / "base.xex") << "not an xex";
    std::ofstream(dir / "patch.xexp") << "not a patch";
  }
  ~Scratch() {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  ProcessResult Run(const fs::path& keys) const {
    return RunProcess({X360N_CONTENT_EXE,
                       {"apply-patch", (dir / "base.xex").string(), (dir / "patch.xexp").string(),
                        (dir / "out").string(), "--keys", keys.string()},
                       dir, dir / "out.log", dir / "err.log", 60s});
  }
  fs::path Keys(const std::string& text) const {
    std::ofstream(dir / "keys.txt") << text;
    return dir / "keys.txt";
  }
  std::string Err() const { return Slurp(dir / "err.log"); }
};

}  // namespace

TEST_CASE("apply-patch fails explicitly without the local key file", "[content]") {
  Scratch s;
  auto r = s.Run(s.dir / "absent.txt");
  REQUIRE(r.launched);
  CHECK(r.exit_code == 1);
  CHECK(s.Err().find("key file '") != std::string::npos);
  CHECK(s.Err().find("not found") != std::string::npos);
  CHECK(s.Err().find("ignored by Git") != std::string::npos);
  CHECK_FALSE(fs::exists(s.dir / "out"));
}

TEST_CASE("apply-patch rejects malformed key files", "[content]") {
  Scratch s;
  auto r = s.Run(s.Keys("xex9-retail 00000000000000000000000000000000\n"));
  REQUIRE(r.launched);
  CHECK(r.exit_code == 1);
  CHECK(s.Err().find("line 1: unknown key name 'xex9-retail'") != std::string::npos);

  r = s.Run(s.Keys("# comment\nxex2-devkit 0000\n"));
  REQUIRE(r.launched);
  CHECK(r.exit_code == 1);
  CHECK(s.Err().find("line 2: 'xex2-devkit' is not 32 hex digits") != std::string::npos);

  r = s.Run(s.Keys("xex2-devkit 00000000000000000000000000000000\n"
                   "xex2-devkit 00000000000000000000000000000000\n"));
  REQUIRE(r.launched);
  CHECK(r.exit_code == 1);
  CHECK(s.Err().find("line 2: 'xex2-devkit' given twice") != std::string::npos);

  r = s.Run(s.Keys("# nothing\n\n"));
  REQUIRE(r.launched);
  CHECK(r.exit_code == 1);
  CHECK(s.Err().find("holds no key") != std::string::npos);
}

TEST_CASE("apply-patch reads a valid key file before the inputs", "[content]") {
  Scratch s;
  auto r = s.Run(s.Keys("xex2-devkit 00000000000000000000000000000000  # zero key\n"));
  REQUIRE(r.launched);
  CHECK(r.exit_code == 1);
  CHECK(s.Err().find("key file") == std::string::npos);
  CHECK(s.Err().find("not an XEX2 header") != std::string::npos);
  CHECK_FALSE(fs::exists(s.dir / "out"));
}
