// Integration tests over user-supplied files in .local/inputs (never in git).
// With no files present these tests SKIP; they never pass vacuously.
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <vector>

#include "sha256.h"
#include "xex_identity.h"

namespace fs = std::filesystem;
using namespace x360n;

TEST_CASE("every XEX in .local/inputs has a readable XEX2 identity", "[local]") {
  fs::path inputs = fs::path(X360N_SOURCE_ROOT) / ".local" / "inputs";
  std::vector<fs::path> xexes;
  std::error_code ec;
  if (fs::is_directory(inputs, ec)) {
    for (auto it = fs::recursive_directory_iterator(inputs, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (ec) break;
      auto ext = it->path().extension().string();
      if (it->is_regular_file(ec) && (ext == ".xex" || ext == ".XEX")) xexes.push_back(it->path());
    }
  }
  if (xexes.empty()) SKIP("no .xex files under " << inputs.string());

  for (const auto& p : xexes) {
    INFO(p.string());
    auto digest = DigestFile(p);
    REQUIRE(digest);
    std::ifstream in(p, std::ios::binary);
    std::vector<uint8_t> head(size_t(std::min<uint64_t>(digest->size, 64ull << 20)));
    in.read(reinterpret_cast<char*>(head.data()), std::streamsize(head.size()));
    auto r = ParseXexIdentity(head);
    INFO(r.error);
    CHECK(r.identity);
  }
}
