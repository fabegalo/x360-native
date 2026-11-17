#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cstring>
#include <fstream>
#include <rex/kernel/module.h>
#include <rex/kernel/object/kernel_thread.h>
#include <rex/kernel/thread_registry.h>
#include <rex/platform/file.h>
#include <rex/xam/title_lifecycle.h>
#include "offline_achievement_fixture.h"
#include "synthetic_xex.h"
#include "kernel/kernel_fixture.h"
#include "kernel/title_state.h"
#include "xam/xam_state.h"

TEST_CASE("Guest achievement writes commit per account before original UI notification", "[sdk][xam][achievement-writes]") {
  constexpr uint32_t title = 0x12345678;
  rex::test::KernelFixture fixture([](const auto& root) {
    auto image = x360n::test::BuildXex({}, nullptr, 0, x360n::test::Entry::kReturnZero);
    auto put = [&](size_t offset, uint32_t value) {
      for (unsigned i = 0; i < 4; ++i) image[offset + i] = uint8_t(value >> (24 - i * 8));
    };
    put(0x14, 4); put(0x30, 0x00040006); put(0x34, 0x60); put(0x6C, title);
    std::ofstream out(root / "achievement-test.xex", std::ios::binary);
    out.write(reinterpret_cast<const char*>(image.data()), image.size());
    REQUIRE(out.good());
  });
  auto& xam = rex::xam::CurrentXAM();
  auto& profile = xam.GetUserProfile();
  auto& store = REX_INTERFACE(rex::xam::IXAMTitleServices)->GetOfflineAchievements();
  CHECK(&store == &xam.GetOfflineAchievements());
  rex::u64 first = 0, second = 0;
  REQUIRE(profile.CreateLocal("First", &first) == 0);
  REQUIRE(profile.CreateLocal("Second", &second) == 0);
  REQUIRE(store.ImportTitle(title, offline_achievement_fixture::Metadata()) == rex::xam::OfflineAchievementResult::kSuccess);
  unsigned notifications = 0;
  xam.GetSystemUI().SetAchievementUnlockedCallback([&](rex::u64 owner, rex::u32 user, rex::u32 id, rex::u32 achievement) {
    CHECK(owner == first); CHECK(user == 0); CHECK(id == title); CHECK(achievement == 1);
    rex::xam::OfflineAchievementTotals committed;
    REQUIRE(store.Totals(owner, committed) == rex::xam::OfflineAchievementResult::kSuccess);
    CHECK(committed.unlocked_count == 1); CHECK(committed.gamerscore == 10);
    ++notifications;
  });
  fixture.RunOnGuestThread([&](rex::test::Caller& c) {
    REQUIRE(rex::kernel::StateOf(c.GetRuntime()).LoadXEXImage("game:\\achievement-test.xex"));
    REQUIRE(xam.GetTitleID() == title);
    const auto packet = c.Alloc(8), entries = c.Alloc(32), overlapped = c.Alloc(32);
    auto write = [&](std::initializer_list<std::pair<uint32_t, uint32_t>> ids, bool async = false, uint32_t length = 8) {
      c.Store32(packet, static_cast<uint32_t>(ids.size())); c.Store32(packet + 4, entries);
      unsigned i = 0;
      for (auto [user, id] : ids) { c.Store32(entries + i * 8, user); c.Store32(entries + i * 8 + 4, id); ++i; }
      if (async) {
        std::memset(c.GetBase() + overlapped, 0, 32);
        return c.XAM("XMsgStartIORequest", {0xFB, 0xB0008, overlapped, packet, length});
      }
      return c.XAM("XMsgInProcessCall", {0xFB, 0xB0008, packet, length});
    };
    CHECK(write({{0, 1}}) == 0x80070525);
    REQUIRE(profile.SetLocalSession(first) == 0);
    CHECK(write({{0, 1}}, true) == 0);
    CHECK(c.Load32(overlapped) == 0); CHECK(c.Load32(overlapped + 24) == 0);
    CHECK(notifications == 1);
    CHECK(write({{0, 1}, {0, 1}}, false, 0) == 0); CHECK(notifications == 1);
    CHECK(write({{0, 2}, {0, 999}}) == 0x80070490);
    CHECK(write({{1, 2}}) == 0x80070525);
    CHECK(write({{4, 2}}) == 0x80070057);
    CHECK(write({}) == 0x80070057); CHECK(write({{0, 2}}, false, 7) == 0x80070057);
    CHECK(write({{0, 999}}, true) == 0);
    CHECK(c.Load32(overlapped) == 1627); CHECK(c.Load32(overlapped + 24) == 0x80070490);
    CHECK(c.XAM("XMsgStartIORequest", {0xFB, 0xB0008, 0, packet, 8}) == 0);
    CHECK(rex::xam::CurrentTitle().threads.GetCallingThread()->GetLastError() == 0x80070490);
    c.Store32(packet, 2); c.Store32(packet + 4, 0xFFFFFFFC);
    CHECK(c.XAM("XMsgInProcessCall", {0xFB, 0xB0008, packet, 8}) == 0x80070057);
    CHECK(c.XAM("XMsgInProcessCall", {0xFB, 0xB0008, 0, 8}) == 0x80070057);
    CHECK(c.XAM("XMsgStartIORequest", {0xFB, 0xB0008, 0xFFFFFFFC, packet, 8}) == 0x80070057);
    rex::xam::OfflineAchievementTotals totals;
    REQUIRE(store.Totals(first, totals) == rex::xam::OfflineAchievementResult::kSuccess);
    CHECK(totals.gamerscore == 10); CHECK(totals.unlocked_count == 1);
    REQUIRE(profile.SetLocalSession(second) == 0);
    // No notification for this second account is requested by this fixture.
    xam.GetSystemUI().SetAchievementUnlockedCallback({});
    REQUIRE(write({{0, 2}}) == 0);
    REQUIRE(store.Totals(second, totals) == rex::xam::OfflineAchievementResult::kSuccess);
    CHECK(totals.gamerscore == 25); CHECK(totals.unlocked_count == 1);
    REQUIRE(store.Totals(first, totals) == rex::xam::OfflineAchievementResult::kSuccess);
    CHECK(totals.gamerscore == 10); CHECK(totals.unlocked_count == 1);
    CHECK(notifications == 1);
    const auto settings = c.Alloc(20), accounts = c.Alloc(16), capacity = c.Alloc(4), result = c.Alloc(1024);
    for (unsigned i = 0; i < 5; ++i) c.Store32(settings + i * 4,
        std::array<uint32_t, 5>{0x10040006, 0x10040013, 0x10040038, 0x10040039, 0x10040012}[i]);
    c.Store64(accounts, first); c.Store64(accounts + 8, second);
    REQUIRE(profile.RecordTitlePlayed(title));
    auto read = [&](bool extended, bool async, uint32_t users = 2) {
      c.Store32(capacity, 1024);
      if (async) std::memset(c.GetBase() + overlapped, 0, 32);
      if (extended) return c.XAM("XamUserReadProfileSettingsEx",
          {title, 0, users, accounts, 5, settings, 0, capacity, result, async ? overlapped : 0});
      return c.XAM("XamUserReadProfileSettings",
          {title, 0, users, accounts, 5, settings, capacity, result, async ? overlapped : 0});
    };
    for (const bool extended : {false, true}) {
      for (const bool async : {false, true}) {
        REQUIRE(read(extended, async) == (async ? 997 : 0));
        if (async) { CHECK(c.Load32(overlapped) == 0); CHECK(c.Load32(overlapped + 24) == 0); }
        CHECK(c.Load32(result) == 10);
        for (unsigned i = 0; i < 10; ++i) {
          const auto entry = result + 8 + i * 40;
          CHECK(c.Load32(entry) == 1);
          CHECK(c.Load32(entry + 8) == static_cast<uint32_t>((i < 5 ? first : second) >> 32));
          CHECK(c.Load32(entry + 12) == static_cast<uint32_t>(i < 5 ? first : second));
          CHECK(c.Load32(entry + 32) == std::array<uint32_t, 10>{10, 1, 10, 1, 0, 25, 1, 25, 1, 1}[i]);
        }
      }
    }
    const auto path = rex::xam::CurrentXAM().GetUserProfile().PackagePath(first) / "achievements.dat";
    const auto original = rex::platform::ReadHostFile(path);
    REQUIRE(original);
    const std::array<uint8_t, 3> corrupt{'b', 'a', 'd'};
    REQUIRE(rex::platform::WriteHostFile(path, corrupt, true));
    for (const bool async : {false, true}) {
      std::memset(c.GetBase() + result, 0xAB, 1024);
      CHECK(read(true, async) == (async ? 997 : 13));
      if (async) { CHECK(c.Load32(overlapped) == 13); CHECK(c.Load32(overlapped + 24) == 0x8007000D); }
      CHECK(c.Load32(result) == 0xABABABAB);
    }
    REQUIRE(rex::platform::WriteHostFile(path, *original, true));
    REQUIRE(read(false, false) == 0);
  });
  xam.GetSystemUI().SetAchievementUnlockedCallback({});
}
