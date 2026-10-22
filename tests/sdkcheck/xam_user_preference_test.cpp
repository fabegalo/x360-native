#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <chrono>
#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/xio.h>
#include <rex/system/xthread.h>

extern "C" REX_FUNC(__imp__XamUserReadUserPreference);
using rex::X_STATUS;

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
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  auto* memory = rt.memory();
  const uint32_t buffer = memory->SystemHeapAlloc(128);
  const uint32_t overlapped = memory->SystemHeapAlloc(sizeof(rex::system::XAM_OVERLAPPED));
  REQUIRE(buffer != 0);
  REQUIRE(overlapped != 0);
  auto* buffer_words = memory->TranslateVirtual<rex::be<uint32_t>*>(buffer);
  auto* ov = memory->TranslateVirtual<rex::system::XAM_OVERLAPPED*>(overlapped);

  Call invalid_user, invalid_user_ff, null_buffer, invalid_span, invalid_overlap, async_call, sync_call;
  auto thread = rex::system::object_ref<rex::system::XHostThread>(
      new rex::system::XHostThread(rt.kernel_state(), 128 * 1024, 0, [&]() {
    auto* ctx = rex::system::XThread::GetCurrentThread()->thread_state()->context();
    auto call = [&](uint32_t user, uint32_t buf, uint32_t ovl, Call& out) {
      buffer_words[0] = 0xA5A5A5A5;
      *ov = {};
      ov->result = 0x3E5;  // Pending, as a caller leaves it before the request.
      ctx->r3.u64 = user;
      ctx->r4.u64 = 0x10040000;  // Preference id; the value is passed through.
      ctx->r5.u64 = buf;
      ctx->r6.u64 = ovl;
      __imp__XamUserReadUserPreference(*ctx, memory->virtual_membase());
      out = {ctx->r3.u32, ov->result, ov->length, buffer_words[0]};
    };
    call(4, buffer, overlapped, invalid_user);
    call(0xFFFFFFFF, buffer, overlapped, invalid_user_ff);
    call(0, 0, overlapped, null_buffer);
    call(0, 5, overlapped, invalid_span);
    call(0, buffer, 0xFFFFFFFC, invalid_overlap);
    call(0, buffer, overlapped, async_call);
    call(1, buffer, 0, sync_call);
    return 0;
  }));
  REQUIRE(thread->Create() == X_STATUS_SUCCESS);
  uint64_t timeout = uint64_t(-50'000'000ll);
  REQUIRE(thread->Wait(0, 0, 0, &timeout) == X_STATUS_SUCCESS);

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
  rex::Runtime rt(root, root);
  rex::RuntimeConfig config{}; config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == 0);
  auto* memory = rt.memory(); auto* profile = rt.kernel_state()->user_profile();
  uint64_t alpha = 0, bravo = 0;
  REQUIRE(profile->CreateLocal("Alpha", &alpha) == 0);
  REQUIRE(profile->CreateLocal("Bravo", &bravo) == 0);
  REQUIRE(profile->SetLocalSession(alpha) == 0);
  REQUIRE(profile->AddSetting(std::make_unique<rex::system::xam::UserProfile::Int32Setting>(
      0x10040053, 1 | 6 | 24 | 128 | 512)));
  const auto block = memory->SystemHeapAlloc(128); REQUIRE(block);
  auto* words = memory->TranslateVirtual<rex::be<uint32_t>*>(block);
  auto thread = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
      rt.kernel_state(), 128 * 1024, 0, [&] {
    auto& ctx = *rex::system::XThread::GetCurrentThread()->thread_state()->context();
    auto call = [&](uint32_t preference, uint32_t expected, uint32_t result = 0) {
      words[0] = 0xA5A5A5A5;
      auto* ov = memory->TranslateVirtual<rex::system::XAM_OVERLAPPED*>(block + 32);
      *ov = {}; ov->result = 997;
      ctx.r3.u64 = 0; ctx.r4.u64 = preference; ctx.r5.u64 = block; ctx.r6.u64 = block + 32;
      __imp__XamUserReadUserPreference(ctx, memory->virtual_membase());
      CHECK(ctx.r3.u32 == 997); CHECK(ov->result == result); CHECK(words[0] == expected);
    };
    call(1, 1); call(6, 2); call(24, 8); call(32, 0); call(64, 0);
    call(128, 128); call(256, 0); call(512, 512);
    REQUIRE(profile->SetLocalSession(bravo) == 0);
    call(128, 0); call(6, 0, 1); call(24, 0, 1);
    REQUIRE(profile->AddSetting(std::make_unique<rex::system::xam::UserProfile::Int32Setting>(0x10040053, 256)));
    call(256, 256);
    REQUIRE(profile->SetLocalSession(alpha) == 0); call(128, 128);
    return 0;
  }));
  REQUIRE(thread->Create() == 0);
  uint64_t timeout = uint64_t(-50'000'000ll);
  REQUIRE(thread->Wait(0, 0, 0, &timeout) == 0);
}
