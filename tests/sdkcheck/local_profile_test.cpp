#include <catch2/catch_test_macros.hpp>
#include <array>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <rex/kernel/init.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xnotifylistener.h>
#include <rex/system/xthread.h>
#include <rex/system/xio.h>
#include <rex/system/thread_state.h>

using rex::X_STATUS;
using rex::X_RESULT;
using rex::X_HRESULT;
extern "C" void __imp__XamUserLogon(PPCContext&, uint8_t*);
extern "C" void __imp__XamUserLogonEx(PPCContext&, uint8_t*);
extern "C" void __imp__XamUserGetSigninInfo(PPCContext&, uint8_t*);
extern "C" void __imp__XamUserGetXUID(PPCContext&, uint8_t*);
extern "C" void __imp__XamUserGetSigninState(PPCContext&, uint8_t*);
extern "C" void __imp__XamProfileCreateEnumerator(PPCContext&, uint8_t*);
extern "C" void __imp__XamProfileEnumerate(PPCContext&, uint8_t*);
extern "C" void __imp__XamProfileFindAccount(PPCContext&, uint8_t*);
extern "C" void __imp__XamProfileLoadAccountInfo(PPCContext&, uint8_t*);
extern "C" void __imp__XamUserGetIndexFromXUID(PPCContext&, uint8_t*);
extern "C" void __imp__XamUserGetUserIndexMask(PPCContext&, uint8_t*);
extern "C" void __imp__NtClose(PPCContext&, uint8_t*);
extern "C" void __imp__XMemCpy(PPCContext&, uint8_t*);
extern "C" void __imp__XMemSet(PPCContext&, uint8_t*);
extern "C" void __imp__lstrcpynW(PPCContext&, uint8_t*);
extern "C" void __imp__RtlCompareMemoryUlong(PPCContext&, uint8_t*);

TEST_CASE("Local profile passcode comparison returns matching bytes in guest order",
          "[sdk][passcode-contract]") {
  // 17559's profile constructor tests account+56 against four zero bytes.
  // Returning one word instead of four bytes incorrectly marks it protected.
  std::array<uint8_t, 64> bytes{};
  PPCContext ctx{};
  auto compare = [&](uint32_t offset, uint32_t length, uint32_t pattern) {
    ctx.r3.u64 = offset; ctx.r4.u64 = length; ctx.r5.u64 = pattern;
    __imp__RtlCompareMemoryUlong(ctx, bytes.data());
    return ctx.r3.u32;
  };
  CHECK(compare(16, 4, 0) == 4);
  CHECK(compare(16, 16, 0) == 16);
  CHECK(compare(16, 0, 0) == 0);
  rex::memory::store_and_swap<uint32_t>(bytes.data() + 16, 0x01020304u);
  CHECK(compare(16, 4, 0) == 0); // configured passcode stays protected
  CHECK(compare(16, 4, 0x01020304u) == 4);
  rex::memory::store_and_swap<uint32_t>(bytes.data() + 24, 0x01020304u);
  CHECK(compare(16, 12, 0x01020304u) == 4); // stop at first differing word
  CHECK(compare(16, 12, 0) == 0);
  CHECK(compare(17, 4, 0) == 0); // preserve alignment rejection
  CHECK(compare(16, 3, 0) == 0);
}

namespace {
struct Store {
  // CTest's build directory is writable, including atomic rename operations.
  std::filesystem::path path = std::filesystem::current_path() /
      ("x360n-local-profile-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  Store() { std::filesystem::create_directories(path); }
  ~Store() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};
void Setup(rex::Runtime& rt) {
  rex::InitLogging("c72-profile.log", spdlog::level::err);
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
}

TEST_CASE("Local profile enumerated account has no passcode without changing session",
          "[sdk][passcode-contract]") {
  Store store;
  rex::Runtime rt(store.path, store.path);
  Setup(rt);
  auto* profile = rt.kernel_state()->user_profile();
  REQUIRE(profile->CreateLocal("NoPasscode") == 0);
  auto* memory = rt.memory();
  auto* base = memory->virtual_membase();
  const auto data = memory->SystemHeapAlloc(512);
  auto* bytes = memory->TranslateVirtual<uint8_t*>(data);
  PPCContext ctx{};
  ctx.r3.u64 = 0; ctx.r4.u64 = data;
  __imp__XamProfileCreateEnumerator(ctx, base);
  REQUIRE(ctx.r3.u32 == 0);
  const auto handle = rex::memory::load_and_swap<uint32_t>(bytes);
  ctx.r3.u64 = handle; ctx.r4.u64 = 2;
  ctx.r5.u64 = data + 16; ctx.r6.u64 = 0;
  __imp__XamProfileEnumerate(ctx, base);
  REQUIRE(ctx.r3.u32 == 0);
  CHECK(rex::memory::load_and_swap<uint64_t>(bytes + 16) == profile->xuid());
  CHECK(rex::memory::load_and_swap<uint64_t>(bytes + 24) == 0); // account control fields
  CHECK(rex::memory::load_and_swap<uint64_t>(bytes + 64) == 0); // Live XUID
  // Original 92925530 stores the account at object+24; 92925658 compares
  // object+80 (account+56). Its vtable+20 method returns (matched != 4).
  ctx.r3.u64 = data + 24 + 56; ctx.r4.u64 = 4; ctx.r5.u64 = 0;
  __imp__RtlCompareMemoryUlong(ctx, base);
  REQUIRE(ctx.r3.u32 == 4);
  CHECK(profile->signin_state() == 0);
  ctx.r3.u64 = handle;
  __imp__NtClose(ctx, base);
  CHECK(ctx.r3.u32 == 0);
  memory->SystemHeapFree(data);
}

TEST_CASE("Local profile enumerates the original Sign-in record and commits only on confirmation",
          "[sdk][local-profile][signin-profile]") {
  Store store;
  uint64_t xuid = 0;
  {
    rex::Runtime rt(store.path, store.path);
    Setup(rt);
    auto* profile = rt.kernel_state()->user_profile();
    REQUIRE(profile->CreateLocal("Selected") == 0);
    xuid = profile->xuid();
    auto* memory = rt.memory();
    const auto data = memory->SystemHeapAlloc(4096);
    auto* bytes = memory->TranslateVirtual<uint8_t*>(data);
    auto* base = memory->virtual_membase();
    auto listener = rex::system::object_ref<rex::system::XNotifyListener>(
        new rex::system::XNotifyListener(rt.kernel_state()));
    listener->Initialize(1, 10);
    PPCContext ctx{};
    auto create_enum = [&] {
      ctx.r3.u64 = 0; ctx.r4.u64 = data;
      __imp__XamProfileCreateEnumerator(ctx, base);
      REQUIRE(ctx.r3.u32 == 0);
      return rex::memory::load_and_swap<uint32_t>(bytes);
    };
    const auto first = create_enum();
    const auto second = create_enum();
    auto enumerate = [&](uint32_t handle, uint32_t flags = 2) {
      ctx.r3.u64 = handle; ctx.r4.u64 = flags;
      ctx.r5.u64 = data + 16; ctx.r6.u64 = 0;
      __imp__XamProfileEnumerate(ctx, base);
      return ctx.r3.u32;
    };
    std::memset(bytes + 16, 0xCD, 393);
    CHECK(enumerate(first, 4) == X_ERROR_INVALID_PARAMETER);
    REQUIRE(enumerate(first) == 0);
    CHECK(rex::memory::load_and_swap<uint64_t>(bytes + 16) == xuid);
    for (unsigned i = 0; i < 8; ++i)
      CHECK(rex::memory::load_and_swap<uint16_t>(bytes + 32 + i * 2) == uint16_t("Selected"[i]));
    CHECK(rex::memory::load_and_swap<uint16_t>(bytes + 48) == 0);
    CHECK(rex::memory::load_and_swap<uint64_t>(bytes + 64) == 0); // no Live identity
    CHECK(rex::memory::load_and_swap<uint32_t>(bytes + 404) == 0);
    CHECK(bytes[408] == 0xCD); // exactly 392 bytes
    CHECK(enumerate(first) == X_ERROR_NO_MORE_FILES);
    CHECK(enumerate(second) == 0); // cursors are independent
    // Closing/canceling an enumeration must not select a user or emit a change.
    ctx.r3.u64 = first; __imp__NtClose(ctx, base);
    CHECK(ctx.r3.u32 == 0);
    CHECK(enumerate(first) == X_ERROR_INVALID_HANDLE);
    uint32_t param = 0;
    CHECK(profile->signin_state() == 0);
    CHECK_FALSE(listener->DequeueNotification(0xA, &param));

    ctx.r3.u64 = xuid; ctx.r4.u64 = data + 512; ctx.r5.u64 = 0;
    __imp__XamProfileFindAccount(ctx, base);
    CHECK(ctx.r3.u32 == 0);
    CHECK(std::memcmp(bytes + 24, bytes + 512, 380) == 0);
    ctx.r3.u64 = 0; ctx.r4.u64 = xuid; ctx.r5.u64 = data + 1024;
    __imp__XamProfileLoadAccountInfo(ctx, base);
    CHECK(ctx.r3.u32 == 0);
    CHECK(std::memcmp(bytes + 512, bytes + 1024, 380) == 0);

    std::memset(bytes + 16 + 392, 0, 392 * 3);
    auto confirm = [&](uint32_t flags) {
      ctx.r3.u64 = data + 16; ctx.r4.u64 = flags; ctx.r5.u64 = 0;
      __imp__XamUserLogonEx(ctx, base);
      return ctx.r3.u32;
    };
    CHECK(confirm(0x120424) == 0x80004001u); // unsupported mode cannot mutate
    CHECK(profile->signin_state() == 0);
    REQUIRE(confirm(0x120420) == 0); // normal original Sign-in confirmation
    REQUIRE(listener->DequeueNotification(0xA, &param));
    CHECK(param == 1);
    CHECK(confirm(0x120420) == 0);
    CHECK_FALSE(listener->DequeueNotification(0xA, &param));
    ctx.r3.u64 = xuid; ctx.r4.u64 = 1; ctx.r5.u64 = data;
    __imp__XamUserGetIndexFromXUID(ctx, base);
    CHECK(ctx.r3.u32 == 0);
    CHECK(rex::memory::load_and_swap<uint32_t>(bytes) == 0);
    ctx.r3.u64 = 1; __imp__XamUserGetUserIndexMask(ctx, base);
    CHECK(ctx.r3.u32 == 1);
    ctx.r3.u64 = 2; __imp__XamUserGetUserIndexMask(ctx, base);
    CHECK(ctx.r3.u32 == 0);
    ctx.r3.u64 = 0; ctx.r4.u64 = 0; ctx.r5.u64 = data + 2048;
    __imp__XamUserGetSigninInfo(ctx, base);
    CHECK(ctx.r3.u32 == 0);
    CHECK(rex::memory::load_and_swap<uint64_t>(bytes + 2048) == xuid);
    CHECK(std::string(reinterpret_cast<char*>(bytes + 2072)) == "Selected");
    // Cancellation while already connected also preserves the session.
    ctx.r3.u64 = second; __imp__NtClose(ctx, base);
    CHECK(profile->signin_state() == 1);
    CHECK_FALSE(listener->DequeueNotification(0xA, &param));
    memory->SystemHeapFree(data);
  }
  rex::Runtime recovered(store.path, store.path);
  Setup(recovered);
  CHECK(recovered.kernel_state()->user_profile()->xuid() == xuid);
  CHECK(recovered.kernel_state()->user_profile()->name() == "Selected");
  CHECK(recovered.kernel_state()->user_profile()->signin_state() == 1);
}

TEST_CASE("Local profile Sign-in imports copy request and bounded UTF16 identity", "[sdk][local-profile][signin-profile]") {
  Store store;
  rex::Runtime rt(store.path, store.path);
  Setup(rt);
  auto* memory = rt.memory();
  const auto address = memory->SystemHeapAlloc(128);
  auto* bytes = memory->TranslateVirtual<uint8_t*>(address);
  std::memset(bytes, 0xCD, 128);
  const uint32_t request[] = {0, 0xFF, 1, 0, 0};
  for (unsigned i = 0; i < 5; ++i)
    rex::memory::store_and_swap<uint32_t>(bytes + i * 4, request[i]);
  PPCContext ctx{};
  ctx.r3.u64 = address + 32; ctx.r4.u64 = address; ctx.r5.u64 = 20;
  __imp__XMemCpy(ctx, memory->virtual_membase());
  CHECK(ctx.r3.u32 == address + 32);
  CHECK(std::memcmp(bytes, bytes + 32, 20) == 0);
  CHECK(bytes[52] == 0xCD);
  ctx.r3.u64 = address + 32; ctx.r4.u64 = 0; ctx.r5.u64 = 20;
  __imp__XMemSet(ctx, memory->virtual_membase());
  CHECK(ctx.r3.u32 == address + 32);
  for (unsigned i = 32; i < 52; ++i) CHECK(bytes[i] == 0);
  CHECK(bytes[52] == 0xCD);
  for (unsigned i = 0; i < 5; ++i)
    rex::memory::store_and_swap<uint16_t>(bytes + i * 2, uint16_t(u"Name"[i]));
  for (auto count : {0u, 1u, 3u, 8u}) {
    std::memset(bytes + 32, 0xCD, 32);
    ctx.r3.u64 = address + 32; ctx.r4.u64 = address; ctx.r5.u64 = count;
    __imp__lstrcpynW(ctx, memory->virtual_membase());
    CHECK(ctx.r3.u32 == address + 32);
    const auto length = count ? std::min(count - 1, 4u) : 0;
    for (unsigned i = 0; i < length; ++i)
      CHECK(rex::memory::load_and_swap<uint16_t>(bytes + 32 + i * 2) == uint16_t(u"Name"[i]));
    if (count) CHECK(rex::memory::load_and_swap<uint16_t>(bytes + 32 + length * 2) == 0);
    CHECK(bytes[32 + (count ? length + 1 : 0) * 2] == 0xCD);
  }
  memory->SystemHeapFree(address);
}
}

TEST_CASE("Local profile commits real XAM identity and recovers session on restart", "[sdk][local-profile]") {
  Store store;
  uint64_t saved_xuid = 0;
  {
    rex::Runtime rt(store.path, store.path);
    Setup(rt);
    auto* profile = rt.kernel_state()->user_profile();
    auto listener = rex::system::object_ref<rex::system::XNotifyListener>(
        new rex::system::XNotifyListener(rt.kernel_state()));
    listener->Initialize(1, 10);
    uint32_t param = 0;
    CHECK_FALSE(listener->DequeueNotification(0xA, &param));
    REQUIRE(profile->signin_state() == 0);
    REQUIRE(profile->CreateLocal("Marina") == X_ERROR_SUCCESS);
    saved_xuid = profile->xuid();
    CHECK((saved_xuid & 0xFFF0000000000000ull) == 0xE000000000000000ull);
    CHECK((saved_xuid & 0x00C0000000000000ull) == 0);
    CHECK_FALSE(listener->DequeueNotification(0xA, &param));
    CHECK(profile->CreateLocal("Another") == X_ERROR_ALREADY_EXISTS);

    auto* mem = rt.memory();
    const auto users = mem->SystemHeapAlloc(392 * 4);
    const auto info = mem->SystemHeapAlloc(40);
    std::memset(mem->TranslateVirtual(users), 0, 392 * 4);
    rex::memory::store_and_swap<uint64_t>(mem->TranslateVirtual(users), saved_xuid);
    PPCContext ctx{};
    auto logon = [&](uint32_t flags = 0) {
      ctx.r3.u64 = users; ctx.r4.u64 = flags; ctx.r5.u64 = 0;
      __imp__XamUserLogon(ctx, mem->virtual_membase());
      return ctx.r3.u32;
    };
    CHECK(logon(0x80) == 0x80004001u);
    CHECK(profile->signin_state() == 0);
    REQUIRE(logon() == 0);
    REQUIRE(listener->DequeueNotification(0xA, &param));
    CHECK(param == 1);
    CHECK(logon() == 0);
    CHECK_FALSE(listener->DequeueNotification(0xA, &param));

    ctx.r3.u64 = 0; ctx.r4.u64 = 0; ctx.r5.u64 = info;
    __imp__XamUserGetSigninInfo(ctx, mem->virtual_membase());
    REQUIRE(ctx.r3.u32 == 0);
    auto* data = mem->TranslateVirtual<uint8_t*>(info);
    CHECK(rex::memory::load_and_swap<uint64_t>(data) == saved_xuid);
    CHECK(rex::memory::load_and_swap<uint32_t>(data + 12) == 1);
    CHECK(std::string(reinterpret_cast<char*>(data + 24)) == "Marina");
    ctx.r3.u64 = 0; ctx.r4.u64 = 2; ctx.r5.u64 = info;
    __imp__XamUserGetXUID(ctx, mem->virtual_membase());
    CHECK(ctx.r3.u32 == uint32_t(X_E_NO_SUCH_USER));
    CHECK(rex::memory::load_and_swap<uint64_t>(data) == 0);

    // Reject unsupported slots as a whole, preserving the previous session.
    rex::memory::store_and_swap<uint64_t>(mem->TranslateVirtual(users + 8), saved_xuid);
    CHECK(logon() == 0x80004001u);
    CHECK(profile->signin_state() == 1);
    CHECK_FALSE(listener->DequeueNotification(0xA, &param));
    mem->SystemHeapFree(info);
    mem->SystemHeapFree(users);
  }
  {
    rex::Runtime rt(store.path, store.path);
    Setup(rt);
    auto* profile = rt.kernel_state()->user_profile();
    CHECK(profile->xuid() == saved_xuid);
    CHECK(profile->name() == "Marina");
    CHECK(profile->signin_state() == 1);
    auto* mem = rt.memory();
    const auto users = mem->SystemHeapAlloc(392 * 4);
    std::memset(mem->TranslateVirtual(users), 0, 392 * 4);
    PPCContext ctx{};
    ctx.r3.u64 = users; ctx.r4.u64 = 0; ctx.r5.u64 = 0;
    __imp__XamUserLogonEx(ctx, mem->virtual_membase());
    CHECK(ctx.r3.u32 == 0);
    ctx.r3.u64 = 0;
    __imp__XamUserGetSigninState(ctx, mem->virtual_membase());
    CHECK(ctx.r3.u32 == 0);
    ctx.r3.u64 = 0; ctx.r4.u64 = 0; ctx.r5.u64 = users;
    __imp__XamUserGetSigninInfo(ctx, mem->virtual_membase());
    CHECK(ctx.r3.u32 == uint32_t(X_E_NO_SUCH_USER));
    CHECK(rex::memory::load_and_swap<uint64_t>(mem->TranslateVirtual(users)) == 0);
    mem->SystemHeapFree(users);
  }
  {
    rex::Runtime rt(store.path, store.path);
    Setup(rt);
    const auto identity = rt.kernel_state()->user_profile()->identity();
    CHECK(identity.xuid == saved_xuid);
    CHECK(identity.name == "Marina");
    CHECK(identity.signin_state == 0);
  }
}

TEST_CASE("Local profile rejects corruption and failed disk writes without publishing state", "[sdk][local-profile]") {
  Store store;
  rex::Runtime rt(store.path, store.path);
  Setup(rt);
  auto* profile = rt.kernel_state()->user_profile();
  CHECK(profile->CreateLocal("") == X_ERROR_INVALID_PARAMETER);
  CHECK(profile->CreateLocal("1234567890123456") == X_ERROR_INVALID_PARAMETER);
  CHECK(profile->CreateLocal(" Name") == X_ERROR_INVALID_PARAMETER);
  REQUIRE(profile->CreateLocal("Local") == 0);
  const auto xuid = profile->xuid();
  CHECK(profile->SetLocalSession(xuid + 2) == X_ERROR_NO_SUCH_USER);
  // A directory occupying the staging filename deterministically denies writes.
  std::filesystem::create_directory(store.path / "profiles/local-v1.dat.tmp");
  CHECK(profile->SetLocalSession(xuid) == X_ERROR_FUNCTION_FAILED);
  CHECK(profile->signin_state() == 0);
  rex::system::xam::UserProfile recovered;
  REQUIRE(recovered.InitializeLocal(store.path));
  CHECK(recovered.xuid() == xuid);
  CHECK(recovered.signin_state() == 0);
  { std::ofstream corrupt(store.path / "profiles/local-v1.dat", std::ios::binary); corrupt << "broken"; }
  CHECK_FALSE(recovered.InitializeLocal(store.path));
  CHECK(recovered.xuid() == 0);
  CHECK(recovered.CreateLocal("Replacement") == X_ERROR_FUNCTION_FAILED);
  CHECK(std::filesystem::file_size(store.path / "profiles/local-v1.dat") == 6);
}

TEST_CASE("Local profile logon completes overlapped with the real result", "[sdk][local-profile]") {
  Store store;
  rex::Runtime rt(store.path, store.path);
  Setup(rt);
  auto* profile = rt.kernel_state()->user_profile();
  REQUIRE(profile->CreateLocal("Async") == 0);
  auto* memory = rt.memory();
  const auto users = memory->SystemHeapAlloc(32);
  const auto overlap = memory->SystemHeapAlloc(sizeof(rex::system::XAM_OVERLAPPED));
  std::memset(memory->TranslateVirtual(users), 0, 32);
  auto* ov = memory->TranslateVirtual<rex::system::XAM_OVERLAPPED*>(overlap);
  uint32_t result = ~0u, failure = 0, success_result = ~0u, failure_result = 0;
  auto thread = rex::system::object_ref<rex::system::XHostThread>(
      new rex::system::XHostThread(rt.kernel_state(), 128 * 1024, 0, [&]() {
    auto* ctx = rex::system::XThread::GetCurrentThread()->thread_state()->context();
    auto call = [&](uint64_t xuid) {
      *ov = {};
      ov->result = 997;
      rex::memory::store_and_swap<uint64_t>(memory->TranslateVirtual(users), xuid);
      ctx->r3.u64 = users; ctx->r4.u64 = 0; ctx->r5.u64 = overlap;
      __imp__XamUserLogon(*ctx, memory->virtual_membase());
      return ctx->r3.u32;
    };
    result = call(profile->xuid());
    success_result = ov->result;
    failure = call(profile->xuid() + 2);
    failure_result = ov->result;
    return 0;
  }));
  REQUIRE(thread->Create() == X_STATUS_SUCCESS);
  uint64_t timeout = uint64_t(-50'000'000ll);
  REQUIRE(thread->Wait(0, 0, 0, &timeout) == X_STATUS_SUCCESS);
  CHECK(result == 0);
  CHECK(success_result == 0);
  CHECK(failure == uint32_t(X_E_NO_SUCH_USER));
  CHECK(failure_result == uint32_t(X_ERROR_NO_SUCH_USER));
  CHECK(profile->signin_state() == 1);
  memory->SystemHeapFree(overlap);
  memory->SystemHeapFree(users);
}
