// The original 17559 avatar catalogs (system storage pack FFFE07DF00000002, extracted
// from the official update) through x360n::avatar::AvatarToc. Structural checks only;
// skipped when the user-supplied files are absent.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "../../titles/common/avatar/avatar_metadata.h"
#include "../../titles/common/avatar/avatar_toc.h"

namespace fs = std::filesystem;
using namespace x360n::avatar;

namespace {

fs::path PackDir() {
  return fs::path(X360N_SOURCE_ROOT) / ".local/inputs/dashboards/17559/retail_system_assets" /
         "storage_packs/FFFE07DF00000002.extracted";
}

std::vector<uint8_t> Slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void CheckAllBlobs(const AvatarToc& toc, size_t* strb, size_t* empty) {
  for (size_t i = 0; i < toc.size(); ++i) {
    auto blob = toc.Blob(i);
    if (blob.empty()) {
      ++*empty;
      continue;
    }
    REQUIRE(blob.size() >= 4);
    if (blob[0] == 'S' && blob[1] == 'T' && blob[2] == 'R' && blob[3] == 'B') ++*strb;
  }
}

}  // namespace

TEST_CASE("17559 AvatarAssetPack.toc parses with every blob STRB or empty", "[avatar]") {
  const auto path = PackDir() / "AvatarAssetPack.toc";
  if (!fs::exists(path)) SKIP("not present: " + path.string());
  std::string error;
  auto toc = AvatarToc::Parse(Slurp(path), &error);
  INFO(error);
  REQUIRE(toc);
  CHECK(toc->header().format == TocFormat::kCurrent);
  CHECK(toc->header().column_count == 18);
  CHECK(toc->size() == 1282);
  size_t strb = 0, empty = 0;
  CheckAllBlobs(*toc, &strb, &empty);
  CHECK(strb == 1103);
  CHECK(empty == 179);
}

TEST_CASE("17559 AvatarAssetPackLegacyV1.toc parses with 13 columns", "[avatar]") {
  const auto path = PackDir() / "AvatarAssetPackLegacyV1.toc";
  if (!fs::exists(path)) SKIP("not present: " + path.string());
  std::string error;
  auto toc = AvatarToc::Parse(Slurp(path), &error);
  INFO(error);
  REQUIRE(toc);
  CHECK(toc->header().format == TocFormat::kLegacyV1);
  CHECK(toc->header().column_count == 13);
  CHECK(toc->size() == 44);
  size_t strb = 0, empty = 0;
  CheckAllBlobs(*toc, &strb, &empty);
  CHECK(strb == 44);
  CHECK(empty == 0);
}

// Same-version pair: the 13604 descriptors (FFFE07DF00000006) against the 13604
// catalog (FFFE07DF00000002), both from the collection's 13604 set.
TEST_CASE("13604 avatar descriptors resolve against the 13604 catalog", "[avatar]") {
  const fs::path set = fs::path(X360N_SOURCE_ROOT) / ".local/inputs/dashboards/13604/kinect_retail_full_readme";
  const auto toc_path = set / "FFFE07DF00000002 Extracted/AvatarAssetPack.toc";
  const auto desc_dir = set / "FFFE07DF00000006 Extracted";
  if (!fs::exists(toc_path) || !fs::exists(desc_dir)) SKIP("13604 set not present under " + set.string());
  std::string error;
  auto toc = AvatarToc::Parse(Slurp(toc_path), &error);
  INFO(error);
  REQUIRE(toc);
  size_t descriptors = 0, ids = 0;
  for (const auto& e : fs::directory_iterator(desc_dir)) {
    if (e.path().extension() != ".Avatar") continue;
    auto m = Slurp(e.path());
    REQUIRE(m.size() == kMetadataSize);
    ++descriptors;
    std::vector<size_t> at;
    for (size_t s = 0; s < kComponentSlotCount; ++s) at.push_back(kComponentSlotsAt + 16 * s);
    at.push_back(kBodyAssetAt);
    for (size_t o : at) {
      const auto id = AssetId::FromBytes(std::span<const uint8_t, 16>(m.data() + o, 16));
      // A slot is an asset ID only when it carries the catalog stamp; one original slot
      // holds a non-ID value (0x3F800000 at +0x8C of Avatar6400.Avatar).
      if (id.stamp != AssetStampFor(*toc)) continue;
      INFO(e.path().filename().string() << " +0x" << std::hex << o);
      CHECK(ResolveAsset(*toc, id, &error));
      ++ids;
    }
    const uint32_t body = BodyType(std::span<const uint8_t, kMetadataSize>(m.data(), kMetadataSize));
    CHECK((body == 1 || body == 2));
  }
  CHECK(descriptors == 3);
  CHECK(ids == 21);
}
