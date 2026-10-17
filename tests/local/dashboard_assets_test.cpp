#include <catch2/catch_test_macros.hpp>
#include "../../titles/common/dashboard_assets.h"
#include "../../titles/dash_17559/src/original_assets.h"
#include "xex_identity.h"
#include <fstream>
#include <iterator>

TEST_CASE("Dashboard 17559 original dependency hashes match the active package", "[dashboard-assets]") {
  auto root = std::filesystem::path(X360N_SOURCE_ROOT) / ".local/inputs/dashboards/17559/runtime";
  if (!std::filesystem::exists(root)) SKIP("17559 original package not supplied");
  const auto error = x360n::VerifyDashboardAssets(root, "17559", dash17559::kOriginalAssets);
  INFO(error);
  REQUIRE(error.empty());
  std::ifstream keyboard(root / "vk.xex", std::ios::binary);
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(keyboard)), {});
  const auto parsed = x360n::ParseXexIdentity(bytes);
  REQUIRE(parsed.identity);
  REQUIRE(parsed.identity->has_execution_info);
  CHECK(parsed.identity->version.ToString() == "2.0.17559.0");
  CHECK(parsed.identity->base_version.ToString() == "2.0.17559.0");
  for (const auto& library : parsed.identity->imports) {
    CHECK(library.version.ToString() == "2.0.17559.0");
    CHECK(library.version_min.ToString() == "2.0.17559.0");
  }
}
