#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstring>
#include "offline_achievement_fixture.h"
#include "kernel/kernel_fixture.h"
#include "xam/xam_state.h"

TEST_CASE("Achievement enumerators snapshot full-XUID ledger state and honor retail string flags", "[sdk][xam][achievement-enumeration]") {
  rex::test::KernelFixture fixture;
  auto& xam = rex::xam::CurrentXAM();
  auto& profile = xam.GetUserProfile();
  auto& store = xam.GetOfflineAchievements();
  rex::u64 first = 0, second = 0;
  REQUIRE(profile.CreateLocal("First", &first) == 0);
  REQUIRE(profile.CreateLocal("Second", &second) == 0);
  REQUIRE(profile.SetLocalSession(first) == 0);
  REQUIRE(store.ImportTitle(0x12345678, offline_achievement_fixture::Metadata()) == rex::xam::OfflineAchievementResult::kSuccess);
  std::vector<rex::xam::OfflineAchievementRecord> newly;
  const std::array<uint32_t, 1> ids{1};
  REQUIRE(store.UnlockBatch(second, 0x12345678, ids, newly) == rex::xam::OfflineAchievementResult::kSuccess);
  REQUIRE(newly.size() == 1);
  fixture.RunOnGuestThread([&](rex::test::Caller& c) {
    const auto size = c.Alloc(4), handle = c.Alloc(4), count = c.Alloc(4), output = c.Alloc(2048);
    auto create = [&](rex::u64 xuid, uint32_t offset, uint32_t page, uint32_t flags) {
      return c.XAM("XamUserCreateAchievementEnumerator", {0x12345678, 0, xuid, flags, offset, page, size, handle});
    };
    REQUIRE(create(second, 0, 2, 0x27) == 0);
    CHECK(c.Load32(size) == 1000);
    const auto snapshot = c.Load32(handle);
    REQUIRE(c.XAM("XamEnumerate", {snapshot, 0, output, 1000, count, 0}) == 0);
    CHECK(c.Load32(count) == 2); CHECK(c.Load32(output) == 1); CHECK(c.Load32(output + 36) == 2);
    CHECK(c.Load32(output + 20) == 10); CHECK(c.Load32(output + 32) == 0x20003);
    CHECK((c.Load32(output + 24) || c.Load32(output + 28)));
    CHECK(c.Load32(output + 68) == 3);
    const auto label = c.Load32(output + 4);
    CHECK(label >= output + 72); CHECK(label < output + 1000);
    CHECK(c.Load32(label) == 0x00460069); // Big-endian UTF-16 "Fi".
    CHECK(c.XAM("XamEnumerate", {snapshot, 0, output, 1000, count, 0}) == 18);
    CHECK(c.Load32(count) == 0);
    CHECK(c.Kernel("NtClose", {snapshot}) == 0);
    REQUIRE(create(first, 0, 1, 0) == 0);
    CHECK(c.Load32(size) == 36);
    REQUIRE(c.XAM("XamEnumerate", {c.Load32(handle), 0, output, 36, count, 0}) == 0);
    CHECK(c.Load32(output + 32) == 3); CHECK(c.Load32(output + 4) == 0);
    CHECK(c.Load32(output + 24) == 0); CHECK(c.Load32(output + 28) == 0);
    CHECK(c.Kernel("NtClose", {c.Load32(handle)}) == 0);
    REQUIRE(create(second, 1, 1, 0) == 0);
    REQUIRE(c.XAM("XamEnumerate", {c.Load32(handle), 0, output, 36, count, 0}) == 0);
    CHECK(c.Load32(output) == 2);
    CHECK(c.Kernel("NtClose", {c.Load32(handle)}) == 0);
    CHECK(create(first ^ 0x100000000ull, 0, 1, 0) == 1317); CHECK(c.Load32(handle) == 0);
    CHECK(create(first, 0, 0xFFFFFFFF, 7) == 87);
    CHECK(c.XAM("XamUserCreateAchievementEnumerator", {0x12345678, 0, first, 7, 0, 1, 0xFFFFFFFC, handle}) == 87);
  });
}

TEST_CASE("Achievement and played-title fixed batches reject invalid spans before advancing", "[sdk][xam][achievement-enumeration]") {
  rex::test::KernelFixture fixture;
  auto& xam = rex::xam::CurrentXAM();
  auto& profile = xam.GetUserProfile();
  REQUIRE(profile.CreateLocal("Pages") == 0);
  const auto xuid = profile.identity().xuid;
  REQUIRE(profile.SetLocalSession(xuid) == 0);
  REQUIRE(xam.GetOfflineAchievements().ImportTitle(0x12345678, offline_achievement_fixture::Metadata()) == rex::xam::OfflineAchievementResult::kSuccess);
  REQUIRE(profile.RecordTitlePlayed(0x12345678));
  REQUIRE(profile.RecordTitlePlayed(0x23456789)); // History without metadata must not gain invented fields.
  fixture.RunOnGuestThread([&](rex::test::Caller& c) {
    const auto size = c.Alloc(4), handle = c.Alloc(4), count = c.Alloc(4), output = c.Alloc(1024), overlapped = c.Alloc(32);
    for (const auto titles : {false, true}) {
      if (titles) REQUIRE(c.XAM("XamUserCreateTitlesPlayedEnumerator", {0, 0, xuid, 0, 1, size, handle}) == 0);
      else REQUIRE(c.XAM("XamUserCreateAchievementEnumerator", {0x12345678, 0, xuid, 7, 0, 1, size, handle}) == 0);
      const auto h = c.Load32(handle), required = c.Load32(size);
      CHECK(required == (titles ? 168 : 500));
      std::memset(c.GetBase() + output, 0xAB, 1024);
      CHECK(c.XAM("XamEnumerate", {h, 0, output, required - 1, count, 0}) == 122);
      CHECK(c.Load32(count) == 0); CHECK(c.Load32(output) == 0xABABABAB);
      CHECK(c.XAM("XamEnumerate", {h, 0, 0xFFFFFFFC, required, count, 0}) == 87);
      CHECK(c.XAM("XamEnumerate", {h, 0, output, required, 0xFFFFFFFC, 0}) == 87);
      CHECK(c.XAM("XamEnumerate", {h, 0, output, required, 0, 0xFFFFFFFC}) == 87);
      CHECK(c.XAM("XamEnumerate", {h, 1, output, required, count, 0}) == 87);
      REQUIRE(c.XAM("XamEnumerate", {h, 2, output, required, 0, overlapped}) == 997);
      CHECK(c.Load32(overlapped) == 0); CHECK(c.Load32(overlapped + 4) == 1);
      CHECK(c.Load32(output) == (titles ? 0x12345678 : 1));
      if (titles) {
        CHECK(c.Load32(output + 4) == 2); CHECK(c.Load32(output + 12) == 35);
        CHECK(c.Load32(output + 8) == 0); CHECK(c.Load32(output + 16) == 0);
        CHECK(c.Load32(output + 40) == 0x00540065); // "Te".
      }
      CHECK(c.Kernel("NtClose", {h}) == 0);
      std::memset(c.GetBase() + overlapped, 0, 32);
    }
  });
}
