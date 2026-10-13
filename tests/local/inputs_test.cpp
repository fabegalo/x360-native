// Integration tests over user-supplied files in .local/inputs (never in git).
// With no files present these tests SKIP; they never pass vacuously.
//
// Two separate questions:
//  - inventory: every .xex under .local/inputs is hashed and classified
//    (XEX2 / XEX1 / other XEX variant / invalid); older formats and invalid files
//    are listed with the reason in .local/runs/inventory/inventory.tsv. It fails
//    only if a file cannot be read or a file with the XEX2 signature is malformed;
//  - integration targets: the executables this project builds and runs, listed
//    explicitly in tests/local/integration_targets.tsv; each one present must be
//    exactly that file (SHA-256, size), XEX2, and read with the expected identity.
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "sha256.h"
#include "paths.h"
#include "../supervisor/scratch.h"
#include "xex_identity.h"

namespace fs = std::filesystem;
using namespace x360n;

namespace {

fs::path Inputs() { return PathFromUtf8(X360N_SOURCE_ROOT) / ".local" / "inputs"; }

struct Classified {
  FileDigest digest;
  XexParseResult parsed;
};

// Reads the header block the way `x360n identify` does and classifies it.
std::optional<Classified> Classify(const fs::path& p) {
  auto digest = DigestFile(p);
  if (!digest) return std::nullopt;
  std::ifstream in(p, std::ios::binary);
  std::vector<uint8_t> head(size_t(std::min<uint64_t>(digest->size, 64ull << 20)));
  in.read(reinterpret_cast<char*>(head.data()), std::streamsize(head.size()));
  return Classified{*digest, ParseXexIdentity(head)};
}

std::string Rel(const fs::path& p, const fs::path& root = Inputs()) {
  auto value = fs::relative(p, root).generic_u8string();
  return {value.begin(), value.end()};
}

}  // namespace

TEST_CASE("inventory diagnostics and classification preserve Unicode paths", "[local]") {
  auto root = UniqueTempPath("inventory-unicode");
  auto file = root / fs::path(u8"João 日本 🎮/amostra.xex");
  fs::create_directories(file.parent_path());
  std::ofstream(file, std::ios::binary) << "XEX1fixture";
  INFO(PathUtf8(file));
  REQUIRE(Classify(file));
  CHECK(Classify(file)->parsed.format == XexFormat::kXex1);
  CHECK(Rel(file, root) == PathUtf8(fs::path(u8"João 日本 🎮")) + "/amostra.xex");
  CHECK(PathFromUtf8(PathUtf8(file)) == file);
  fs::remove_all(root);
}

TEST_CASE("inventory of .local/inputs classifies every XEX", "[local]") {
  std::vector<fs::path> xexes;
  std::error_code ec;
  if (fs::is_directory(Inputs(), ec)) {
    for (auto it = fs::recursive_directory_iterator(Inputs(), fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (ec) break;
      std::string ext = PathUtf8(it->path().extension());
      std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
      if (it->is_regular_file(ec) && ext == ".xex") xexes.push_back(it->path());
    }
  }
  REQUIRE_FALSE(ec);
  if (xexes.empty()) SKIP("no .xex files under " << PathUtf8(Inputs()));
  std::sort(xexes.begin(), xexes.end());

  fs::path report_dir = PathFromUtf8(X360N_SOURCE_ROOT) / ".local" / "runs" / "inventory";
  fs::create_directories(report_dir, ec);
  std::ofstream report(report_dir / "inventory.tsv", std::ios::binary);
  REQUIRE(report);
  report << "# x360n inventory of .local/inputs (.xex files); written by local.inventory\n"
            "path\tsize\tsha256\tformat\tsignature\tstatus\n";
  std::map<std::string, int> counts;
  for (const auto& p : xexes) {
    INFO(PathUtf8(p));
    auto c = Classify(p);
    REQUIRE(c);  // every file must be readable
    const auto& r = c->parsed;
    std::string status = r.identity ? "read" : (r.format == XexFormat::kXex2 ? "MALFORMED XEX2: " : "unsupported: ") + r.error;
    report << Rel(p) << '\t' << c->digest.size << '\t' << c->digest.sha256 << '\t' << XexFormatName(r.format)
           << '\t' << r.signature << '\t' << status << '\n';
    ++counts[std::string(XexFormatName(r.format)) + (r.identity ? " (read)" : "")];
    if (!r.identity && r.format != XexFormat::kXex2) {
      WARN("not supported, listed in the inventory: " << Rel(p) << ": " << r.error);
    }
    // A file with the XEX2 signature that does not read is a failure.
    if (r.format == XexFormat::kXex2) {
      INFO(r.error);
      CHECK(r.identity);
    }
  }
  std::string summary;
  for (const auto& [k, v] : counts) summary += fmt::format("{} {}; ", v, k);
  report << "# " << xexes.size() << " files: " << summary << '\n';
  WARN("inventory: " << xexes.size() << " .xex files: " << summary << "report "
                     << PathUtf8(report_dir / "inventory.tsv"));
  report.flush();
  REQUIRE(report);
}

TEST_CASE("integration targets are the expected XEX2 files", "[local]") {
  std::ifstream list(PathFromUtf8(X360N_SOURCE_ROOT) / "tests" / "local" / "integration_targets.tsv");
  REQUIRE(list);
  int present = 0, absent = 0;
  std::string line;
  while (std::getline(list, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    std::istringstream cols(line);
    std::string rel, sha256, size, title_id, version;
    std::getline(cols, rel, '\t');
    std::getline(cols, sha256, '\t');
    std::getline(cols, size, '\t');
    std::getline(cols, title_id, '\t');
    std::getline(cols, version, '\t');
    REQUIRE_FALSE(version.empty());  // malformed list line
    fs::path p = Inputs() / PathFromUtf8(rel);
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) {
      ++absent;
      WARN("integration target absent here: " << rel);
      continue;
    }
    ++present;
    INFO(rel);
    auto c = Classify(p);
    REQUIRE(c);
    CHECK(c->digest.sha256 == sha256);
    CHECK(std::to_string(c->digest.size) == size);
    CHECK(c->parsed.format == XexFormat::kXex2);
    INFO(c->parsed.error);
    REQUIRE(c->parsed.identity);
    const auto& id = *c->parsed.identity;
    if (title_id == "-") {
      CHECK_FALSE(id.has_execution_info);
    } else {
      REQUIRE(id.has_execution_info);
      CHECK(fmt::format("{:08X}", id.title_id) == title_id);
      CHECK(id.version.ToString() == version);
    }
  }
  if (present == 0) SKIP("none of the " << absent << " integration targets is present under " << PathUtf8(Inputs()));
}
