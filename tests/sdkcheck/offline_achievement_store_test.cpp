#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include <rex/system/offline_achievement_store.h>
#include "offline_achievement_fixture.h"
using offline_achievement_fixture::Metadata;

using rex::system::OfflineAchievementRecord;
using rex::system::OfflineAchievementResult;
using rex::system::OfflineAchievementStore;
using rex::system::OfflineAchievementTotals;
using Result = OfflineAchievementResult;

namespace {
struct StoreDirectory {
  std::filesystem::path path = std::filesystem::current_path() /
      ("x360n-achievements-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  StoreDirectory() { std::filesystem::create_directories(path); }
  ~StoreDirectory() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};
constexpr uint64_t kUserA = 0xE000000000000001ULL;
constexpr uint64_t kUserB = 0xE000000000000002ULL;
constexpr uint32_t kTitleA = 0x12345678;
constexpr uint32_t kTitleB = 0x87654321;
std::vector<uint8_t> FileBytes(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
}  // namespace

TEST_CASE("Offline achievements persist per account and title with exact original metadata",
          "[sdk][offline-achievements]") {
  StoreDirectory directory;
  uint64_t timestamp = 0;
  {
    OfflineAchievementStore store;
    store.Configure(directory.path);
    REQUIRE(store.ImportTitle(kTitleA, Metadata()) == Result::kSuccess);
    REQUIRE(store.ImportTitle(kTitleB, Metadata(15)) == Result::kSuccess);
    std::string name;
    REQUIRE(store.TitleName(kTitleA, name) == Result::kSuccess);
    CHECK(name == "Test resource title");
    std::vector<uint8_t> image;
    REQUIRE(store.GetImage(kTitleA, 100, image) == Result::kSuccess);
    CHECK(image == std::vector<uint8_t>{0x89, 'P', 'N', 'G', 13, 10, 26, 10});
    CHECK(store.GetImage(kTitleA, 999, image) == Result::kNotFound);
    CHECK(image.empty());
    std::vector<OfflineAchievementRecord> events;
    REQUIRE(store.UnlockBatch(kUserA, kTitleA, std::array{1u, 1u}, events) == Result::kSuccess);
    REQUIRE(events.size() == 1);
    CHECK(events[0].info.label == "First"); CHECK(events[0].info.gamerscore == 10);
    timestamp = events[0].unlock_filetime;
    CHECK(timestamp > 116444736000000000ULL);
    REQUIRE(store.UnlockBatch(kUserA, kTitleA, std::array{1u}, events) == Result::kSuccess);
    CHECK(events.empty());
    REQUIRE(store.UnlockBatch(kUserA, kTitleB, std::array{1u}, events) == Result::kSuccess);
    REQUIRE(store.UnlockBatch(kUserB, kTitleA, std::array{2u}, events) == Result::kSuccess);
    OfflineAchievementTotals totals;
    REQUIRE(store.Totals(kUserA, totals) == Result::kSuccess);
    CHECK(totals.gamerscore == 25); CHECK(totals.unlocked_count == 2);
    REQUIRE(store.Totals(kUserB, totals) == Result::kSuccess);
    CHECK(totals.gamerscore == 25); CHECK(totals.unlocked_count == 1);
  }
  // Destroy all state and reopen without importing metadata again, as the
  // dashboard does when looking up a previously played game after restart.
  OfflineAchievementStore reopened;
  reopened.Configure(directory.path);
  std::vector<OfflineAchievementRecord> records;
  REQUIRE(reopened.List(kUserA, kTitleA, records) == Result::kSuccess);
  REQUIRE(records.size() == 2);
  CHECK(records[0].unlock_filetime == timestamp); CHECK(records[1].unlock_filetime == 0);
  CHECK(records[0].info.description == "Completed first");
  CHECK(records[0].info.unachieved_description == "Complete first");
  REQUIRE(reopened.List(kUserB, kTitleA, records) == Result::kSuccess);
  CHECK(records[0].unlock_filetime == 0); CHECK(records[1].unlock_filetime != 0);
  REQUIRE(reopened.List(kUserB, kTitleB, records) == Result::kSuccess);
  CHECK(records[0].unlock_filetime == 0); CHECK(records[1].unlock_filetime == 0);
  OfflineAchievementTotals totals;
  REQUIRE(reopened.Totals(kUserA, totals) == Result::kSuccess);
  CHECK(totals.gamerscore == 25); CHECK(totals.unlocked_count == 2);
}

TEST_CASE("Offline achievement batches validate completely and preserve storage on errors",
          "[sdk][offline-achievements]") {
  StoreDirectory directory;
  OfflineAchievementStore store;
  std::vector<OfflineAchievementRecord> events;
  CHECK(store.UnlockBatch(kUserA, kTitleA, std::array{1u}, events) == Result::kStorageError);
  store.Configure(directory.path);
  REQUIRE(store.ImportTitle(kTitleA, Metadata()) == Result::kSuccess);
  CHECK(store.UnlockBatch(0, kTitleA, std::array{1u}, events) == Result::kInvalidArgument);
  CHECK(store.UnlockBatch(kUserA, 0, std::array{1u}, events) == Result::kInvalidArgument);
  CHECK(store.UnlockBatch(kUserA, kTitleA, {}, events) == Result::kInvalidArgument);
  CHECK(store.UnlockBatch(kUserA, kTitleB, std::array{1u}, events) == Result::kNotFound);
  CHECK(store.UnlockBatch(kUserA, kTitleA, std::array{1u, 999u}, events) == Result::kNotFound);
  CHECK(events.empty());
  std::vector<OfflineAchievementRecord> records;
  REQUIRE(store.List(kUserA, kTitleA, records) == Result::kSuccess);
  CHECK(records[0].unlock_filetime == 0);
  REQUIRE(store.UnlockBatch(kUserA, kTitleA, std::array{1u}, events) == Result::kSuccess);
  const auto unlock_path = directory.path / "offline-achievements-v1/profiles/E000000000000001.dat";
  const auto original = FileBytes(unlock_path);
  // An occupied staging pathname forces a genuine write failure, including
  // when the process has broad privileges that would defeat chmod tests.
  auto staging = unlock_path; staging += ".tmp";
  std::filesystem::create_directory(staging);
  { std::ofstream obstruction(staging / "keep"); obstruction << "occupied"; }
  CHECK(store.UnlockBatch(kUserA, kTitleA, std::array{2u}, events) == Result::kStorageError);
  CHECK(events.empty()); CHECK(FileBytes(unlock_path) == original);
  REQUIRE(store.List(kUserA, kTitleA, records) == Result::kSuccess);
  CHECK(records[1].unlock_filetime == 0);
}

TEST_CASE("Offline achievements reject corrupt or foreign snapshots without replacing them",
          "[sdk][offline-achievements]") {
  StoreDirectory directory;
  OfflineAchievementStore store;
  store.Configure(directory.path);
  REQUIRE(store.ImportTitle(kTitleA, Metadata()) == Result::kSuccess);
  std::vector<OfflineAchievementRecord> events;
  REQUIRE(store.UnlockBatch(kUserA, kTitleA, std::array{1u}, events) == Result::kSuccess);
  const auto profile_dir = directory.path / "offline-achievements-v1/profiles";
  const auto path_a = profile_dir / "E000000000000001.dat";
  const auto path_b = profile_dir / "E000000000000002.dat";
  std::filesystem::copy_file(path_a, path_b);
  OfflineAchievementTotals totals;
  CHECK(store.Totals(kUserB, totals) == Result::kCorruptData);
  CHECK(totals.unlocked_count == 0); CHECK(totals.gamerscore == 0);
  auto bytes = FileBytes(path_a); bytes[24] ^= 1;
  { std::ofstream corrupt(path_a, std::ios::binary | std::ios::trunc);
    corrupt.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size())); }
  CHECK(store.UnlockBatch(kUserA, kTitleA, std::array{2u}, events) == Result::kCorruptData);
  CHECK(events.empty()); CHECK(FileBytes(path_a) == bytes);
  CHECK(store.Totals(kUserA, totals) == Result::kCorruptData);
  auto invalid = Metadata(); invalid.resize(32);
  CHECK(store.ImportTitle(kTitleB, invalid) == Result::kCorruptData);
}

TEST_CASE("Original achievement metadata updates cannot rewrite earned scores",
          "[sdk][offline-achievements]") {
  StoreDirectory directory;
  OfflineAchievementStore store;
  store.Configure(directory.path);
  REQUIRE(store.ImportTitle(kTitleA, Metadata(10, 1)) == Result::kSuccess);
  REQUIRE(store.ImportTitle(kTitleA, Metadata()) == Result::kSuccess);
  CHECK(store.ImportTitle(kTitleA, Metadata(11)) == Result::kMetadataMismatch);
  std::vector<OfflineAchievementRecord> records;
  CHECK(store.List(kUserA, kTitleA, records) == Result::kMetadataMismatch);
  CHECK(store.UnlockBatch(kUserA, kTitleA, std::array{1u}, records) == Result::kMetadataMismatch);
  CHECK(records.empty());
  CHECK(store.ImportTitle(kTitleA, Metadata(10, 1)) == Result::kMetadataMismatch);
  REQUIRE(store.ImportTitle(kTitleA, Metadata()) == Result::kSuccess);
  std::vector<OfflineAchievementRecord> events;
  REQUIRE(store.UnlockBatch(kUserA, kTitleA, std::array{1u}, events) == Result::kSuccess);
  REQUIRE(events.size() == 1); CHECK(events[0].info.gamerscore == 10);
}

TEST_CASE("Achievement ID zero is accepted only when present in the original catalog",
          "[sdk][offline-achievements]") {
  StoreDirectory directory;
  std::vector<OfflineAchievementRecord> events, records;
  {
    OfflineAchievementStore initial;
    initial.Configure(directory.path);
    REQUIRE(initial.ImportTitle(kTitleA, Metadata(10, 2, {}, 0)) == Result::kSuccess);
    REQUIRE(initial.ImportTitle(kTitleB, Metadata()) == Result::kSuccess);
    REQUIRE(initial.UnlockBatch(kUserA, kTitleA, std::array{0u}, events) == Result::kSuccess);
    REQUIRE(events.size() == 1); CHECK(events[0].info.id == 0);
    CHECK(initial.UnlockBatch(kUserA, kTitleB, std::array{0u}, events) == Result::kNotFound);
  }
  OfflineAchievementStore reopened;
  reopened.Configure(directory.path);
  REQUIRE(reopened.List(kUserA, kTitleA, records) == Result::kSuccess);
  REQUIRE(records.size() == 2);
  CHECK(records[0].info.id == 0); CHECK(records[0].unlock_filetime != 0);
  REQUIRE(reopened.UnlockBatch(kUserA, kTitleA, std::array{0u}, events) == Result::kSuccess);
  CHECK(events.empty());
  OfflineAchievementTotals totals;
  REQUIRE(reopened.Totals(kUserA, totals) == Result::kSuccess);
  CHECK(totals.unlocked_count == 1); CHECK(totals.gamerscore == 10);
}

TEST_CASE("Interrupted achievement staging never replaces the last committed snapshot",
          "[sdk][offline-achievements]") {
  StoreDirectory directory;
  std::vector<OfflineAchievementRecord> events, records;
  {
    OfflineAchievementStore initial;
    initial.Configure(directory.path);
    REQUIRE(initial.ImportTitle(kTitleA, Metadata()) == Result::kSuccess);
    REQUIRE(initial.UnlockBatch(kUserA, kTitleA, std::array{1u}, events) == Result::kSuccess);
  }
  const auto staging = directory.path / "offline-achievements-v1/profiles/E000000000000001.dat.tmp";
  { std::ofstream interrupted(staging, std::ios::binary); interrupted << "incomplete new snapshot"; }
  OfflineAchievementStore reopened;
  reopened.Configure(directory.path);
  REQUIRE(reopened.List(kUserA, kTitleA, records) == Result::kSuccess);
  CHECK(records[0].unlock_filetime != 0); CHECK(records[1].unlock_filetime == 0);
  REQUIRE(reopened.UnlockBatch(kUserA, kTitleA, std::array{2u}, events) == Result::kSuccess);
  CHECK(events.size() == 1); CHECK_FALSE(std::filesystem::exists(staging));
  OfflineAchievementTotals totals;
  REQUIRE(reopened.Totals(kUserA, totals) == Result::kSuccess);
  CHECK(totals.gamerscore == 35); CHECK(totals.unlocked_count == 2);
}

TEST_CASE("Concurrent duplicate achievement writes publish only one new unlock",
          "[sdk][offline-achievements]") {
  StoreDirectory directory;
  OfflineAchievementStore store;
  store.Configure(directory.path);
  REQUIRE(store.ImportTitle(kTitleA, Metadata()) == Result::kSuccess);
  std::atomic<unsigned> errors{0}, event_count{0};
  std::vector<std::thread> workers;
  for (unsigned i = 0; i < 8; ++i) workers.emplace_back([&] {
    std::vector<OfflineAchievementRecord> events;
    if (store.UnlockBatch(kUserA, kTitleA, std::array{1u}, events) != Result::kSuccess) ++errors;
    event_count += unsigned(events.size());
  });
  for (auto& worker : workers) worker.join();
  CHECK(errors == 0); CHECK(event_count == 1);
  OfflineAchievementTotals totals;
  REQUIRE(store.Totals(kUserA, totals) == Result::kSuccess);
  CHECK(totals.unlocked_count == 1); CHECK(totals.gamerscore == 10);
}

TEST_CASE("Shared profile root reconfiguration isolates achievement state",
          "[sdk][offline-achievements]") {
  StoreDirectory first, second;
  OfflineAchievementStore store;
  std::vector<OfflineAchievementRecord> events, records;
  store.Configure(first.path);
  REQUIRE(store.ImportTitle(kTitleA, Metadata()) == Result::kSuccess);
  REQUIRE(store.UnlockBatch(kUserA, kTitleA, std::array{1u}, events) == Result::kSuccess);
  store.Configure(second.path);
  CHECK(store.List(kUserA, kTitleA, records) == Result::kNotFound);
  REQUIRE(store.ImportTitle(kTitleA, Metadata()) == Result::kSuccess);
  REQUIRE(store.List(kUserA, kTitleA, records) == Result::kSuccess);
  CHECK(records[0].unlock_filetime == 0);
  store.Configure(first.path);
  REQUIRE(store.List(kUserA, kTitleA, records) == Result::kSuccess);
  CHECK(records[0].unlock_filetime != 0);
}

TEST_CASE("Independent achievement stores serialize updates without losing either unlock",
          "[sdk][offline-achievements]") {
  StoreDirectory directory;
  OfflineAchievementStore first, second;
  first.Configure(directory.path); second.Configure(directory.path);
  REQUIRE(first.ImportTitle(kTitleA, Metadata()) == Result::kSuccess);
  std::atomic<bool> start{false};
  std::atomic<unsigned> errors{0}, event_count{0};
  auto write = [&](OfflineAchievementStore& store, uint32_t id) {
    while (!start.load()) std::this_thread::yield();
    std::vector<OfflineAchievementRecord> events;
    if (store.UnlockBatch(kUserA, kTitleA, std::array{id}, events) != Result::kSuccess) ++errors;
    event_count += unsigned(events.size());
  };
  std::thread a([&] { write(first, 1); }), b([&] { write(second, 2); });
  start = true;
  a.join(); b.join();
  CHECK(errors == 0); CHECK(event_count == 2);
  OfflineAchievementTotals totals;
  REQUIRE(first.Totals(kUserA, totals) == Result::kSuccess);
  CHECK(totals.unlocked_count == 2); CHECK(totals.gamerscore == 35);
}

TEST_CASE("Profile and title achievement counters share a committed snapshot during writes",
          "[sdk][offline-achievements]") {
  StoreDirectory directory;
  OfflineAchievementStore reader, writer;
  reader.Configure(directory.path); writer.Configure(directory.path);
  REQUIRE(reader.ImportTitle(kTitleA, Metadata()) == Result::kSuccess);
  REQUIRE(reader.ImportTitle(kTitleB, Metadata(40)) == Result::kSuccess);
  std::vector<OfflineAchievementRecord> events;
  REQUIRE(writer.UnlockBatch(kUserA, kTitleB, std::array{1u}, events) == Result::kSuccess);
  OfflineAchievementTotals global, title;
  REQUIRE(reader.Totals(kUserA, global, kTitleA, &title) == Result::kSuccess);
  CHECK(global.gamerscore == 40); CHECK(global.unlocked_count == 1);
  CHECK(title.gamerscore == 0); CHECK(title.unlocked_count == 0);
  std::atomic<bool> start{false};
  std::atomic<unsigned> errors{0}, invalid_snapshots{0};
  std::thread write([&] {
    while (!start.load()) std::this_thread::yield();
    for (uint32_t id : {1u, 2u}) {
      std::vector<OfflineAchievementRecord> written;
      if (writer.UnlockBatch(kUserA, kTitleA, std::array{id}, written) != Result::kSuccess) ++errors;
    }
  });
  start = true;
  for (unsigned i = 0; i < 100; ++i) {
    if (reader.Totals(kUserA, global, kTitleA, &title) != Result::kSuccess) { ++errors; continue; }
    const bool before = global.gamerscore == 40 && global.unlocked_count == 1 &&
                        title.gamerscore == 0 && title.unlocked_count == 0;
    const bool middle = global.gamerscore == 50 && global.unlocked_count == 2 &&
                        title.gamerscore == 10 && title.unlocked_count == 1;
    const bool after = global.gamerscore == 75 && global.unlocked_count == 3 &&
                       title.gamerscore == 35 && title.unlocked_count == 2;
    if (!before && !middle && !after) ++invalid_snapshots;
  }
  write.join();
  CHECK(errors == 0); CHECK(invalid_snapshots == 0);
  REQUIRE(reader.Totals(kUserA, global, kTitleA, &title) == Result::kSuccess);
  CHECK(global.gamerscore == 75); CHECK(global.unlocked_count == 3);
  CHECK(title.gamerscore == 35); CHECK(title.unlocked_count == 2);
  REQUIRE(reader.Totals(kUserB, global, kTitleA, &title) == Result::kSuccess);
  CHECK(global.gamerscore == 0); CHECK(global.unlocked_count == 0);
  CHECK(title.gamerscore == 0); CHECK(title.unlocked_count == 0);
  CHECK(reader.Totals(kUserA, global, 0, &title) == Result::kInvalidArgument);
  CHECK(reader.Totals(kUserA, global, 0x12349999, &title) == Result::kSuccess);
  CHECK(global.gamerscore == 75); CHECK(global.unlocked_count == 3);
  CHECK(title.gamerscore == 0); CHECK(title.unlocked_count == 0);
}

TEST_CASE("Offline achievements survive independent write and read processes",
          "[sdk][offline-achievements][offline-achievement-process]") {
  const auto* path = std::getenv("X360N_ACHIEVEMENT_TEST_STORE");
  const auto* phase = std::getenv("X360N_ACHIEVEMENT_TEST_PHASE");
  if (!path || !phase) SKIP("Run tests/local/offline-achievements-process.ps1 for independent process evidence");
  OfflineAchievementStore store;
  store.Configure(std::filesystem::u8path(path));
  if (std::string(phase) == "write") {
    REQUIRE(store.ImportTitle(kTitleA, Metadata()) == Result::kSuccess);
    REQUIRE(store.ImportTitle(kTitleB, Metadata(15)) == Result::kSuccess);
    std::vector<OfflineAchievementRecord> events;
    REQUIRE(store.UnlockBatch(kUserA, kTitleA, std::array{1u}, events) == Result::kSuccess);
    REQUIRE(events.size() == 1);
    REQUIRE(store.UnlockBatch(kUserA, kTitleB, std::array{1u}, events) == Result::kSuccess);
    REQUIRE(store.UnlockBatch(kUserB, kTitleA, std::array{2u}, events) == Result::kSuccess);
  } else {
    REQUIRE(std::string(phase) == "read");
    std::vector<OfflineAchievementRecord> records;
    REQUIRE(store.List(kUserA, kTitleA, records) == Result::kSuccess);
    REQUIRE(records.size() == 2);
    CHECK(records[0].unlock_filetime != 0); CHECK(records[1].unlock_filetime == 0);
    REQUIRE(store.List(kUserB, kTitleA, records) == Result::kSuccess);
    CHECK(records[0].unlock_filetime == 0); CHECK(records[1].unlock_filetime != 0);
    REQUIRE(store.List(kUserB, kTitleB, records) == Result::kSuccess);
    CHECK(records[0].unlock_filetime == 0); CHECK(records[1].unlock_filetime == 0);
    OfflineAchievementTotals totals;
    REQUIRE(store.Totals(kUserA, totals) == Result::kSuccess);
    CHECK(totals.gamerscore == 25); CHECK(totals.unlocked_count == 2);
    REQUIRE(store.Totals(kUserB, totals) == Result::kSuccess);
    CHECK(totals.gamerscore == 25); CHECK(totals.unlocked_count == 1);
  }
}
