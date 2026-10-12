#include <catch2/catch_test_macros.hpp>
#include <filesystem>
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
// 0x000B0082 that performs the read is not implemented, so its failure must
// reach the caller; before patch 0011 the stub returned the incoming r3 and
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

  Call invalid_user, invalid_user_ff, null_buffer, async_call, sync_call;
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
    call(0, buffer, overlapped, async_call);
    call(1, buffer, 0, sync_call);
    return 0;
  }));
  REQUIRE(thread->Create() == X_STATUS_SUCCESS);
  uint64_t timeout = uint64_t(-50'000'000ll);
  REQUIRE(thread->Wait(0, 0, 0, &timeout) == X_STATUS_SUCCESS);

  // Rejected before the message: ERROR_INVALID_PARAMETER, OVERLAPPED untouched.
  for (const Call* c : {&invalid_user, &invalid_user_ff, &null_buffer}) {
    CHECK(c->r3 == 87);
    CHECK(c->overlapped_result == 0x3E5);
  }
  // With an OVERLAPPED: ERROR_IO_PENDING, and the OVERLAPPED completes with the
  // XGI failure (unimplemented message), never with success.
  CHECK(async_call.r3 == 997);
  CHECK(async_call.overlapped_result == 0x80004005);
  CHECK(async_call.overlapped_length == 0xFFFFFFFF);
  // Without one: the failure maps to ERROR_FUNCTION_FAILED, as in the original.
  CHECK(sync_call.r3 == 1627);
  // Nothing claims a preference value was read.
  for (const Call* c : {&invalid_user, &null_buffer, &async_call, &sync_call}) {
    CHECK(c->buffer_word == 0xA5A5A5A5);
  }
}
