#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <chrono>
#include <rex/kernel/guest_memory.h>
#include <rex/kernel/types/io.h>
#include <rex/xam/user_profile.h>
#include "kernel/kernel_fixture.h"
#include "kernel/test_access.h"
#include "xam/xam_state.h"

namespace {
struct Call {
  uint32_t r3 = 0, overlapped_result = 0, overlapped_length = 0, buffer_word = 0;
};
}  // namespace

// Contract of xam.xex 2.0.17559.0 export 0x4F7 (0x816B1F68). The XGI message
// 0x000B0082 validates preference masks; an unsupported mask must fail without
// rewriting the result; before patch 0011 the stub returned the incoming r3 and
// left the OVERLAPPED pending forever (dashboard My Games, run
// 20261012T060338Z-dash17559-lib-nav4).
TEST_CASE("XamUserReadUserPreference reports the XGI result instead of residual r3",
          "[sdk][user-preference]") {
  rex::test::KernelFixture fixture;
  auto* memory = &rex::kernel::GetAddressSpace(fixture.GetRuntime());
  const uint32_t buffer = memory->SystemHeapAlloc(128);
  const uint32_t overlapped = memory->SystemHeapAlloc(sizeof(rex::kernel::XOVERLAPPED));
  REQUIRE(buffer != 0);
  REQUIRE(overlapped != 0);
  auto* buffer_words = memory->TranslateVirtual<rex::be_u32*>(buffer);
  auto* ov = memory->TranslateVirtual<rex::kernel::XOVERLAPPED*>(overlapped);

  Call invalid_user, invalid_user_ff, null_buffer, invalid_span, invalid_overlap, async_call, sync_call;
  fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
    auto call = [&](uint32_t user, uint32_t buf, uint32_t ovl, Call& out) {
      buffer_words[0] = 0xA5A5A5A5;
      *ov = {};
      ov->result = 0x3E5;  // Pending, as a caller leaves it before the request.
      const auto result = caller.XAM("XamUserReadUserPreference", {user, 0x10040000, buf, ovl});
      out = {result, ov->result, ov->length, buffer_words[0]};
    };
    call(4, buffer, overlapped, invalid_user);
    call(0xFFFFFFFF, buffer, overlapped, invalid_user_ff);
    call(0, 0, overlapped, null_buffer);
    call(0, 5, overlapped, invalid_span);
    call(0, buffer, 0xFFFFFFFC, invalid_overlap);
    call(0, buffer, overlapped, async_call);
    call(1, buffer, 0, sync_call);
  });

  // Rejected before the message: ERROR_INVALID_PARAMETER, OVERLAPPED untouched.
  for (const Call* c : {&invalid_user, &invalid_user_ff, &null_buffer, &invalid_span, &invalid_overlap}) {
    CHECK(c->r3 == 87);
    CHECK(c->overlapped_result == 0x3E5);
  }
  // With an OVERLAPPED: ERROR_IO_PENDING, and the OVERLAPPED completes with the
  // XGI failure (invalid preference), never with success.
  CHECK(async_call.r3 == 997);
  CHECK(async_call.overlapped_result == 0x80070057);
  CHECK(async_call.overlapped_length == 0xFFFFFFFF);
  // Without one: the failure maps to ERROR_FUNCTION_FAILED, as in the original.
  CHECK(sync_call.r3 == 1627);
  // Nothing claims a preference value was read.
  for (const Call* c : {&invalid_user, &null_buffer, &async_call, &sync_call}) {
    CHECK(c->buffer_word == 0xA5A5A5A5);
  }
}

TEST_CASE("Original user preference masks read committed per-profile flags", "[sdk][user-preference][profile-preference-flags]") {
  const auto root = std::filesystem::temp_directory_path() /
      ("x360n-user-preference-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  REQUIRE(std::filesystem::create_directory(root));
  struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
  } cleanup{root};
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, root);
  auto* memory = &rex::kernel::GetAddressSpace(fixture.GetRuntime());
  auto* profile = &rex::xam::CurrentXAM().GetUserProfile();
  uint64_t alpha = 0, bravo = 0;
  REQUIRE(profile->CreateLocal("Alpha", &alpha) == 0);
  REQUIRE(profile->CreateLocal("Bravo", &bravo) == 0);
  REQUIRE(profile->SetLocalSession(alpha) == 0);
  REQUIRE(profile->AddSetting(std::make_unique<rex::xam::UserProfile::Int32Setting>(
      0x10040053, 1 | 6 | 24 | 128 | 512)));
  const auto block = memory->SystemHeapAlloc(128); REQUIRE(block);
  auto* words = memory->TranslateVirtual<rex::be_u32*>(block);
  fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
    auto call = [&](uint32_t preference, uint32_t expected, uint32_t result = 0) {
      words[0] = 0xA5A5A5A5;
      auto* ov = memory->TranslateVirtual<rex::kernel::XOVERLAPPED*>(block + 32);
      *ov = {}; ov->result = 997;
      CHECK(caller.XAM("XamUserReadUserPreference", {0, preference, block, block + 32}) == 997);
      CHECK(ov->result == result); CHECK(words[0] == expected);
    };
    call(1, 1); call(6, 2); call(24, 8); call(32, 0); call(64, 0);
    call(128, 128); call(256, 0); call(512, 512);
    REQUIRE(profile->SetLocalSession(bravo) == 0);
    call(128, 0); call(6, 0, 1); call(24, 0, 1);
    REQUIRE(profile->AddSetting(std::make_unique<rex::xam::UserProfile::Int32Setting>(0x10040053, 256)));
    call(256, 256);
    REQUIRE(profile->SetLocalSession(alpha) == 0); call(128, 128);
  });
}
