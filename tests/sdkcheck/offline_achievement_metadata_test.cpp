#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <rex/hash.h>
#include <rex/platform/file.h>
#include <rex/platform/process.h>
#include <rex/string/path.h>
#include <rex/path_settings.h>
#include <rex/kernel/title_paths.h>
#include <rex/xam/offline_achievement_store.h>
#include <rex/xam/user_profile.h>
#include "codegen/tool_runtime.h"
#include "object_app.h"
#include "temp_folder.h"
#include "xam/xam_state.h"

namespace {
class MetadataTool final : public rex::test::ObjectApp<rex::codegen::ToolApp> {
 public:
  MetadataTool(const std::filesystem::path& app_root, std::filesystem::path persistent = {})
      : ObjectApp("achievement_metadata_test", app_root), persistent_(std::move(persistent)) {}
  rex::filesystem::IFileSystemFactory& Factory() {
    return *GetControlFactory().Get<rex::filesystem::IFileSystemFactory>();
  }
 protected:
  rex::Result<void> OnConfigure(rex::SettingsRegistry& settings) override {
    REX_TRY_VOID(ToolApp::OnConfigure(settings));
    REX_TRY_VOID(settings.SetText(rex::SettingLayer::kTitle, "title.game_writable", "false"));
    if (!persistent_.empty())
      REX_TRY_VOID(REX_PATHS().user_root.SetText(rex::SettingLayer::kTitle, persistent_.string()));
    return rex::Ok();
  }
 private:
  std::filesystem::path persistent_;
};

std::filesystem::path OriginalImage() {
  const auto image = rex::platform::GetEnvironmentValue("X360N_TEST_ACHIEVEMENT_IMAGE");
  if (!image) SKIP("Set X360N_TEST_ACHIEVEMENT_IMAGE; metadata only, no guest execution");
  const auto path = rex::platform::HostPath(std::filesystem::absolute(std::filesystem::u8path(*image)));
  REQUIRE(std::filesystem::is_regular_file(path));
  return path;
}

auto InputEntries(const std::filesystem::path& root) {
  std::map<std::filesystem::path, std::pair<std::uintmax_t, std::filesystem::file_time_type>> result;
  for (const auto& entry : std::filesystem::directory_iterator(root))
    result.emplace(entry.path().filename(), std::pair{entry.is_regular_file() ? entry.file_size() : 0,
                                                    entry.last_write_time()});
  return result;
}
}  // namespace

TEST_CASE("Achievement metadata tools preserve their input directory", "[sdk][achievement-tool-isolation]") {
  const auto image = OriginalImage();
  const auto before = InputEntries(image.parent_path());
  const auto hash = rex::HashFile(image);
  REQUIRE_FALSE(hash.empty());
  rex::test::TempFolder app_root("achievement_tool_app_");
  {
    MetadataTool app(app_root.path);
    auto opened = app.Open(image.parent_path(), rex::string::PathToUTF8(image.filename()));
    REQUIRE(opened);
    REQUIRE(rex::xam::CurrentXAM().OpenTitleXDBF()->IsValid());
  }
  CHECK(InputEntries(image.parent_path()) == before);
  CHECK(rex::HashFile(image) == hash);
}

TEST_CASE("Offline achievement metadata matches the original installed XDBF", "[sdk][offline-achievement][local-achievement-metadata]") {
  const auto image = OriginalImage();
  rex::test::TempFolder directory("achievement_metadata_");
  MetadataTool app(directory.path / "app", directory.path / "user");
  rex::u32 title = 0;
  rex::u64 account = 0;
  std::vector<rex::xam::OfflineAchievementRecord> records;
  {
    auto opened = app.Open(image.parent_path(), rex::string::PathToUTF8(image.filename()));
    REQUIRE(opened);
    // Open reads the image and original resources; it never launches the guest.
    auto& xam = rex::xam::CurrentXAM();
    title = xam.GetTitleID();
    const auto db = xam.OpenTitleXDBF();
    REQUIRE(db->IsValid());
    const auto rows = db->GetAchievements();
    const auto language = db->GetSupportedLanguage(xam.GetUserLanguage());
    REQUIRE_FALSE(rows.empty());
    REQUIRE(xam.GetUserProfile().CreateLocal("Metadata", &account) == 0);
    auto& store = xam.GetOfflineAchievements();
    REQUIRE(store.List(account, title, records) == rex::xam::OfflineAchievementResult::kSuccess);
    REQUIRE(records.size() == rows.size());
    for (const auto& row : rows) {
      const auto found = std::find_if(records.begin(), records.end(), [&](const auto& record) {
        return record.info.id == row.id;
      });
      REQUIRE(found != records.end());
      CHECK(found->info.gamerscore == row.gamerscore);
      CHECK(found->info.image_id == row.image_id);
      CHECK(found->info.label == db->GetStringTableEntry(language, row.label_id));
      CHECK(found->info.description == db->GetStringTableEntry(language, row.description_id));
      CHECK(found->info.unachieved_description == db->GetStringTableEntry(language, row.unachieved_id));
      CHECK(found->unlock_filetime == 0); CHECK_FALSE(found->info.unlocked);
      const auto source = db->GetEntry(rex::filesystem::XDBFSection::kImage, row.image_id);
      if (!source || !source.size) continue;
      std::vector<rex::u8> icon;
      REQUIRE(store.GetImage(title, row.image_id, icon) == rex::xam::OfflineAchievementResult::kSuccess);
      CHECK(icon == std::vector<rex::u8>(source.buffer, source.buffer + source.size));
    }
    rex::xam::OfflineAchievementTotals totals;
    REQUIRE(store.Totals(account, totals) == rex::xam::OfflineAchievementResult::kSuccess);
    CHECK(totals.gamerscore == 0); CHECK(totals.unlocked_count == 0);
    if (rex::platform::GetEnvironmentValue("X360N_ACHIEVEMENT_BENCHMARK")) {
      constexpr unsigned iterations = 100;
      const auto begin = std::chrono::steady_clock::now();
      bool ok = true;
      for (unsigned i = 0; i < iterations; ++i)
        ok &= store.List(account, title, records) == rex::xam::OfflineAchievementResult::kSuccess;
      const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
      REQUIRE(ok);
      std::cout << "Original XDBF List: title=" << std::hex << title << std::dec
                << " records=" << records.size() << " iterations=" << iterations << " ms_per_call=" << elapsed / iterations << '\n';
    }
  }
  // A new store can read the same original catalog after the title unloads.
  rex::xam::OfflineAchievementStore reopened(app.Factory());
  reopened.Configure(directory.path / "user");
  std::vector<rex::xam::OfflineAchievementRecord> persisted;
  REQUIRE(reopened.List(account, title, persisted) == rex::xam::OfflineAchievementResult::kSuccess);
  REQUIRE(persisted.size() == records.size());
  for (size_t i = 0; i < records.size(); ++i) {
    CHECK(persisted[i].info.id == records[i].info.id);
    CHECK(persisted[i].info.gamerscore == records[i].info.gamerscore);
    CHECK(persisted[i].unlock_filetime == 0);
  }
}
