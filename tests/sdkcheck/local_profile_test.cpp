#include <catch2/catch_test_macros.hpp>
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
