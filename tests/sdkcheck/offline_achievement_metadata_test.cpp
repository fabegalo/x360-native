#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <fmt/format.h>
#include <rex/cvar.h>
#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/system/offline_achievement_store.h>
#include <rex/system/user_module.h>

TEST_CASE("Achievement metadata tools preserve their input directory", "[sdk][achievement-tool-isolation]") {
  const auto* image = std::getenv("X360N_TEST_ACHIEVEMENT_IMAGE");
  if (!image) SKIP("Set X360N_TEST_ACHIEVEMENT_IMAGE; metadata only, no guest execution");
  const auto directory = std::filesystem::current_path() /
      ("achievement-tool-input-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(directory);
  struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
  } cleanup{directory};
  std::filesystem::copy_file(image, directory / "default.xex");
  {
    rex::Runtime runtime(directory);
    rex::RuntimeConfig config{};
    config.tool_mode = true;
    config.kernel_init = rex::kernel::InitializeKernel;
    REQUIRE(runtime.Setup(std::move(config)) == 0);
    REQUIRE(runtime.LoadXexImage("game:\\default.xex") == 0);
    REQUIRE(runtime.kernel_state()->title_xdbf().is_valid());
  }
  CHECK_FALSE(std::filesystem::exists(directory / "offline-achievements-v1"));
}

TEST_CASE("Offline achievement metadata matches the original installed XDBF", "[sdk][offline-achievement][local-achievement-metadata]") {
  const auto* path = std::getenv("X360N_TEST_ACHIEVEMENT_IMAGE");
  if (!path) SKIP("Set X360N_TEST_ACHIEVEMENT_IMAGE; metadata only, no guest execution");
  const std::filesystem::path image = path;
  REQUIRE(std::filesystem::is_regular_file(image));
  const auto directory = std::filesystem::current_path() /
      ("achievement-metadata-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(directory);
  struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
  } cleanup{directory};
  uint32_t title = 0;
  uint64_t account = 0;
  std::vector<rex::system::OfflineAchievementRecord> records;
  {
    rex::Runtime runtime(image.parent_path(), directory);
    rex::RuntimeConfig config{};
    config.tool_mode = true;
    config.kernel_init = rex::kernel::InitializeKernel;
    REQUIRE(runtime.Setup(std::move(config)) == 0);
    // Loading image data never starts a guest thread, graphics, or input.
    REQUIRE(runtime.LoadXexImage("game:\\" + image.filename().string()) == 0);
    title = runtime.kernel_state()->title_id();
    if (const auto* output = std::getenv("X360N_TEST_XDBF_OUTPUT")) {
      uint32_t address = 0, size = 0;
      REQUIRE(runtime.kernel_state()->GetExecutableModule()->GetSection(
          fmt::format("{:08X}", title), &address, &size) == 0);
      std::ofstream extracted(output, std::ios::binary | std::ios::trunc);
      REQUIRE(extracted.is_open());
      extracted.write(reinterpret_cast<const char*>(runtime.memory()->TranslateVirtual(address)), size);
      REQUIRE(extracted.good());
    }
    const auto db = runtime.kernel_state()->title_xdbf();
    REQUIRE(db.is_valid());
    const auto rows = db.GetAchievements();
    const auto language = db.GetExistingLanguage(static_cast<rex::system::XLanguage>(
        std::stoul(rex::cvar::GetFlagByName("user_language"))));
    REQUIRE_FALSE(rows.empty());
    REQUIRE(runtime.kernel_state()->user_profile()->CreateLocal("Metadata", &account) == 0);
    auto& store = runtime.kernel_state()->offline_achievements();
    REQUIRE(store.List(account, title, records) == rex::system::OfflineAchievementResult::kSuccess);
    REQUIRE(records.size() == rows.size());
    for (const auto& row : rows) {
      const auto found = std::find_if(records.begin(), records.end(), [&](const auto& record) {
        return record.info.id == row.id;
      });
      REQUIRE(found != records.end());
      CHECK(found->info.gamerscore == row.gamerscore);
      CHECK(found->info.image_id == row.image_id);
      CHECK(found->info.label == db.GetStringTableEntry(language, row.label_id));
      CHECK(found->info.description == db.GetStringTableEntry(language, row.description_id));
      CHECK(found->info.unachieved_description == db.GetStringTableEntry(language, row.unachieved_id));
      CHECK(found->unlock_filetime == 0);
      const auto source = db.GetEntry(rex::system::util::XdbfSection::kImage, uint32_t(row.image_id));
      REQUIRE(source);
      std::vector<uint8_t> icon;
      REQUIRE(store.GetImage(title, row.image_id, icon) == rex::system::OfflineAchievementResult::kSuccess);
      CHECK(icon == std::vector<uint8_t>(source.buffer, source.buffer + source.size));
    }
    rex::system::OfflineAchievementTotals totals;
    REQUIRE(store.Totals(account, totals) == rex::system::OfflineAchievementResult::kSuccess);
    CHECK(totals.gamerscore == 0);
    CHECK(totals.unlocked_count == 0);
    if (std::getenv("X360N_ACHIEVEMENT_BENCHMARK")) {
      constexpr unsigned iterations = 100;
      const auto begin = std::chrono::steady_clock::now();
      bool ok = true;
      for (unsigned i = 0; i < iterations; ++i)
        ok &= store.List(account, title, records) == rex::system::OfflineAchievementResult::kSuccess;
      const auto elapsed = std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - begin).count();
      REQUIRE(ok);
      std::cout << "Original XDBF List: title=" << std::hex << title << std::dec
                << " records=" << records.size() << " iterations=" << iterations
                << " ms_per_call=" << elapsed / iterations << '\n';
    }
  }
  // The dashboard can query this title's original resources after the game has exited.
  rex::system::OfflineAchievementStore reopened;
  reopened.Configure(directory);
  std::vector<rex::system::OfflineAchievementRecord> persisted;
  REQUIRE(reopened.List(account, title, persisted) == rex::system::OfflineAchievementResult::kSuccess);
  REQUIRE(persisted.size() == records.size());
  for (size_t i = 0; i < records.size(); ++i) {
    CHECK(persisted[i].info.id == records[i].info.id);
    CHECK(persisted[i].info.gamerscore == records[i].info.gamerscore);
    CHECK(persisted[i].unlock_filetime == 0);
  }
}
