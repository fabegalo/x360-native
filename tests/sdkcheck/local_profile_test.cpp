#include <catch2/catch_test_macros.hpp>
#include <array>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cstdio>
#include <optional>
#include <cstdlib>
#include "paths.h"
#include "kernel/kernel_fixture.h"
#include "kernel/test_access.h"
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/types/io.h>
#include <rex/kernel/object/object_table.h>
#include <rex/kernel/object/notify_listener_object.h>
#include <rex/kernel/thread_registry.h>
#include <rex/ppc/recompiled_code.h>
#include <rex/xam/user_profile.h>
#include "xam/xam_state.h"
using namespace rex::kernel;
namespace {
void Call(rex::test::KernelFixture& fixture,PPCContext& args,uint8_t* base,
          void (*function)(PPCContext&,uint8_t*)) {
  fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
    auto& context=caller.GetContext(); const auto stack=args.r1.u64;
    if(stack)context.r1.u64=stack;
    context.r3=args.r3;context.r4=args.r4;context.r5=args.r5;context.r6=args.r6;
    context.r7=args.r7;context.r8=args.r8;context.r9=args.r9;context.r10=args.r10;
    function(context,base);args=context;args.r1.u64=stack;
  });
}
}
extern "C" void __imp__XamUserLogon(PPCContext&, uint8_t*);
extern "C" void __imp__XamUserLogonEx(PPCContext&, uint8_t*);
extern "C" void __imp__XamUserFlushLogonQueue(PPCContext&, uint8_t*);
extern "C" void __imp__XamUserGetSigninInfo(PPCContext&, uint8_t*);
extern "C" void __imp__XamUserGetXUID(PPCContext&, uint8_t*);
extern "C" void __imp__XamUserGetSigninState(PPCContext&, uint8_t*);
extern "C" void __imp__XamProfileCreateEnumerator(PPCContext&, uint8_t*);
extern "C" void __imp__XamProfileCreate(PPCContext&, uint8_t*);
extern "C" void __imp__XamProfileGetCreationStatus(PPCContext&, uint8_t*);
extern "C" void __imp__XamProfileRenameAccount(PPCContext&, uint8_t*);
extern "C" void __imp__XamProfileDelete(PPCContext&, uint8_t*);
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
extern "C" void __imp__XamIsDataCenterMode(PPCContext&, uint8_t*);
extern "C" void __imp__XamIsNuiAutomationEnabled(PPCContext&, uint8_t*);
extern "C" void __imp__XamGetCurrentDemand(PPCContext&, uint8_t*);
extern "C" void __imp__XNotifyBroadcast(PPCContext&, uint8_t*);
extern "C" void __imp__XamAreMixedAccountsSignedIn(PPCContext&, uint8_t*);
extern "C" void __imp__XamGetUserNeedTFA(PPCContext&, uint8_t*);
extern "C" void __imp__XamUserGetUserFlags(PPCContext&, uint8_t*);
extern "C" void __imp__GetSystemTimeAsFileTime(PPCContext&, uint8_t*);
extern "C" void __imp__ExGetXConfigSetting(PPCContext&, uint8_t*);
extern "C" void __imp__ExReadModifyWriteXConfigSettingUlong(PPCContext&, uint8_t*);
extern "C" void __imp__XamNuiGetDeviceStatus(PPCContext&, uint8_t*);
extern "C" void __imp__XamNuiIsDeviceReady(PPCContext&, uint8_t*);
extern "C" void __imp__XCustomGetBannerImage(PPCContext&, uint8_t*);
extern "C" void __imp__XamInputGetDeviceStats(PPCContext&, uint8_t*);

TEST_CASE("Missing custom banner returns failure without writing guest memory", "[sdk][custom-banner]") {
  const auto dir = std::filesystem::temp_directory_path() / ("x360n_banner_" + std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(dir);
  struct Cleanup { std::filesystem::path p; ~Cleanup() { std::error_code e; std::filesystem::remove_all(p, e); } } cleanup{dir};
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, dir);
  auto& rt=fixture.GetRuntime();
  auto* memory = (&rt.GetGuestHeap()); auto* base = rt.GetMembase().GetBase();
  const auto buffer = memory->SystemHeapAlloc(16); REQUIRE(buffer);
  std::memset(base + buffer, 0xA5, 16);
  PPCContext ctx{}; ctx.r3.u64 = buffer + 4;
  Call(fixture,ctx,base,__imp__XCustomGetBannerImage);
  CHECK(ctx.r3.u32 == 0x80004001u);
  // The HUD 1888 caller reads its stack word only on success (913E31E8).
  for (unsigned i = 0; i < 16; ++i) CHECK(base[buffer + i] == 0xA5);
  ctx.r3.u64 = 0; Call(fixture,ctx,base,__imp__XCustomGetBannerImage);
  CHECK(ctx.r3.u32 == 0x80070057u);
  // No backend supplies the private device-statistics record. Leave it
  // untouched on failure; user zero must not become an accidental success.
  std::memset(base + buffer, 0xA5, 16);
  for (uint32_t user = 0; user < 4; ++user) {
    ctx.r3.u64 = user; ctx.r4.u64 = buffer;
    Call(fixture,ctx,base,__imp__XamInputGetDeviceStats); CHECK(ctx.r3.u32 == 50);
    for (unsigned i = 0; i < 16; ++i) CHECK(base[buffer + i] == 0xA5);
  }
  memory->SystemHeapFree(buffer);
}

TEST_CASE("Guide device status preserves the caller stack after its 16-byte output",
          "[sdk][guide-contract]") {
  rex::test::KernelFixture fixture;
  // Retail 17559 HUD 913F80F8 places the 16-byte status immediately before
  // saved r29. The previous 24-byte SDK layout zeroed that saved register.
  std::array<uint8_t, 48> bytes;
  bytes.fill(0xA5);
  PPCContext ctx{};
  ctx.r3.u64 = 16;
  Call(fixture,ctx,bytes.data(),__imp__XamNuiGetDeviceStatus);
  for (unsigned i = 0; i < bytes.size(); ++i)
    CHECK(bytes[i] == (i >= 16 && i < 32 ? 0 : 0xA5));
}

TEST_CASE("An absent Kinect is never reported ready from the caller register",
          "[sdk][nui-contract]") {
  rex::test::KernelFixture fixture;
  std::array<uint8_t, 48> bytes;
  bytes.fill(0xA5);
  PPCContext ctx{};
  // The dashboard calls this parameterless query with r3 still holding its
  // scene pointer. A stub leaks that pointer into the original ready branch.
  for (uint64_t previous : {0ull, 1ull, 0x92A00000ull, 0xFFFFFFFFFFFFFFFFull}) {
    ctx.r3.u64 = previous;
    Call(fixture,ctx,bytes.data(),__imp__XamNuiIsDeviceReady);
    CHECK(ctx.r3.u64 == 0);
    for (auto byte : bytes) CHECK(byte == 0xA5);
  }
}

TEST_CASE("Local profile passcode comparison returns matching bytes in guest order",
          "[sdk][passcode-contract]") {
  rex::test::KernelFixture fixture;
  // 17559's profile constructor tests account+56 against four zero bytes.
  // Returning one word instead of four bytes incorrectly marks it protected.
  std::array<uint8_t, 64> bytes{};
  PPCContext ctx{};
  auto compare = [&](uint32_t offset, uint32_t length, uint32_t pattern) {
    ctx.r3.u64 = offset; ctx.r4.u64 = length; ctx.r5.u64 = pattern;
    Call(fixture,ctx,bytes.data(),__imp__RtlCompareMemoryUlong);
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
// As on the console: <device>/<XUID>/FFFE07D1/00010000/<XUID>/.
std::filesystem::path ProfilePackage(const std::filesystem::path& device, uint64_t xuid) {
  char name[17];
  std::snprintf(name, sizeof(name), "%016llX", static_cast<unsigned long long>(xuid));
  return device / name / "FFFE07D1" / "00010000" / name;
}

TEST_CASE("Sign-in system broadcasts reach matching listeners", "[sdk][signin]") {
  Store store;
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, store.path);
  auto& rt=fixture.GetRuntime();
  std::vector<uint32_t> handles;
  rex::ScopeGuard close_listeners([&] {for(auto handle:handles) CloseOwned(rt.GetObjectTable(),handle);});
  auto make_listener = [&](uint64_t mask,uint32_t version) {
    auto handle=rt.GetNotificationBroadcaster().AddNotifyListener(mask,version);REQUIRE(handle);
    handles.push_back(*handle);
    return rex::kernel::Pinned<rex::kernel::INotifyListenerQueue>(rt.GetObjectTable().Get(*handle));
  };
  auto system = make_listener(1, 10);
  auto other = make_listener(2, 10);
  auto older = make_listener(1, 3);
  uint32_t id = 0, parameter = 0;
  // Listener registration queues the runtime's initial UI/system state.
  for (auto* listener : {&*system, &*other, &*older})
    while (listener->Dequeue(&id, &parameter)) {}
  PPCContext ctx{};
  ctx.r3.u64 = 0x80040016;
  ctx.r4.u64 = 0x00FF0002;
  Call(fixture,ctx,rt.GetMembase().GetBase(),__imp__XNotifyBroadcast);
  REQUIRE(system->Dequeue(&id, &parameter));
  CHECK(id == 0x80040016);
  CHECK(parameter == 0x00FF0002);
  CHECK_FALSE(system->Dequeue(&id, &parameter));
  CHECK_FALSE(other->Dequeue(&id, &parameter));
  CHECK_FALSE(older->Dequeue(&id, &parameter));
}

TEST_CASE("Local profile enumerated account has no passcode without changing session",
          "[sdk][passcode-contract]") {
  Store store;
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, store.path);
  auto& rt=fixture.GetRuntime();
  auto* profile = (&rex::xam::CurrentXAM().GetUserProfile());
  REQUIRE(profile->CreateLocal("NoPasscode") == 0);
  auto* memory = (&rt.GetGuestHeap());
  auto* base = rt.GetMembase().GetBase();
  const auto data = memory->SystemHeapAlloc(512);
  auto* bytes = rex::kernel::GetAddressSpace(rt).TranslateVirtual<uint8_t*>(data);
  PPCContext ctx{};
  ctx.r3.u64 = 0; ctx.r4.u64 = data;
  Call(fixture,ctx,base,__imp__XamProfileCreateEnumerator);
  REQUIRE(ctx.r3.u32 == 0);
  const auto handle = rex::memory::load_and_swap<uint32_t>(bytes);
  ctx.r3.u64 = handle; ctx.r4.u64 = 2;
  ctx.r5.u64 = data + 16; ctx.r6.u64 = 0;
  Call(fixture,ctx,base,__imp__XamProfileEnumerate);
  REQUIRE(ctx.r3.u32 == 0);
  CHECK(rex::memory::load_and_swap<uint64_t>(bytes + 16) == profile->GetXUID());
  CHECK(rex::memory::load_and_swap<uint64_t>(bytes + 24) == 0); // account control fields
  CHECK(rex::memory::load_and_swap<uint64_t>(bytes + 64) == 0); // Live XUID
  // Original 92925530 stores the account at object+24; 92925658 compares
  // object+80 (account+56). Its vtable+20 method returns (matched != 4).
  ctx.r3.u64 = data + 24 + 56; ctx.r4.u64 = 4; ctx.r5.u64 = 0;
  Call(fixture,ctx,base,__imp__RtlCompareMemoryUlong);
  REQUIRE(ctx.r3.u32 == 4);
  CHECK(profile->GetSigninState() == 0);
  ctx.r3.u64 = handle;
  Call(fixture,ctx,base,__imp__NtClose);
  CHECK(ctx.r3.u32 == 0);
  memory->SystemHeapFree(data);
}

TEST_CASE("Local profile enumerates the original Sign-in record and commits only on confirmation",
          "[sdk][local-profile][signin-profile]") {
  Store store;
  uint64_t xuid = 0;
  {
    rex::test::KernelFixture fixture({}, {}, {}, {}, {}, store.path);
  auto& rt=fixture.GetRuntime();
    auto* profile = (&rex::xam::CurrentXAM().GetUserProfile());
    REQUIRE(profile->CreateLocal("Selected") == 0);
    xuid = profile->GetXUID();
    auto* memory = (&rt.GetGuestHeap());
    const auto data = memory->SystemHeapAlloc(4096);
    auto* bytes = rex::kernel::GetAddressSpace(rt).TranslateVirtual<uint8_t*>(data);
    auto* base = rt.GetMembase().GetBase();
    auto listener_handle=rt.GetNotificationBroadcaster().AddNotifyListener(1,10);REQUIRE(listener_handle);
    rex::ScopeGuard close_listener([&] {CloseOwned(rt.GetObjectTable(),*listener_handle);});
    rex::kernel::Pinned<rex::kernel::INotifyListenerQueue> listener(rt.GetObjectTable().Get(*listener_handle));
    REQUIRE(listener);
    PPCContext ctx{};
    auto create_enum = [&] {
      ctx.r3.u64 = 0; ctx.r4.u64 = data;
      Call(fixture,ctx,base,__imp__XamProfileCreateEnumerator);
      REQUIRE(ctx.r3.u32 == 0);
      return rex::memory::load_and_swap<uint32_t>(bytes);
    };
    const auto first = create_enum();
    const auto second = create_enum();
    auto enumerate = [&](uint32_t handle, uint32_t flags = 2) {
      ctx.r3.u64 = handle; ctx.r4.u64 = flags;
      ctx.r5.u64 = data + 16; ctx.r6.u64 = 0;
      Call(fixture,ctx,base,__imp__XamProfileEnumerate);
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
    // The profile package is on the hard drive (device 1), as on the console.
    CHECK(rex::memory::load_and_swap<uint32_t>(bytes + 404) == 1);
    CHECK(bytes[408] == 0xCD); // exactly 392 bytes
    CHECK(enumerate(first) == X_ERROR_NO_MORE_FILES);
    CHECK(enumerate(second) == 0); // cursors are independent
    // Closing/canceling an enumeration must not select a user or emit a change.
    ctx.r3.u64 = first; Call(fixture,ctx,base,__imp__NtClose);
    CHECK(ctx.r3.u32 == 0);
    CHECK(enumerate(first) == X_ERROR_INVALID_HANDLE);
    uint32_t param = 0;
    CHECK(profile->GetSigninState() == 0);
    CHECK_FALSE(listener->DequeueByID(0xA, &param));

    ctx.r3.u64 = xuid; ctx.r4.u64 = data + 512; ctx.r5.u64 = 0;
    Call(fixture,ctx,base,__imp__XamProfileFindAccount);
    CHECK(ctx.r3.u32 == 0);
    CHECK(std::memcmp(bytes + 24, bytes + 512, 380) == 0);
    ctx.r3.u64 = 0; ctx.r4.u64 = xuid; ctx.r5.u64 = data + 1024;
    Call(fixture,ctx,base,__imp__XamProfileLoadAccountInfo);
    CHECK(ctx.r3.u32 == 0);
    CHECK(std::memcmp(bytes + 512, bytes + 1024, 380) == 0);

    std::memset(bytes + 16 + 392, 0, 392 * 3);
    auto confirm = [&](uint32_t flags) {
      ctx.r3.u64 = data + 16; ctx.r4.u64 = flags; ctx.r5.u64 = 0;
      Call(fixture,ctx,base,__imp__XamUserLogonEx);
      return ctx.r3.u32;
    };
    CHECK(confirm(0x120428) == 0x80004001u); // unsupported removal mode cannot mutate
    CHECK(profile->GetSigninState() == 0);
    REQUIRE(confirm(0x120424) == 0); // original Sign-in merges the selected local account
    REQUIRE(listener->DequeueByID(0xA, &param));
    CHECK(param == 1);
    CHECK(confirm(0x120420) == 0);
    CHECK_FALSE(listener->DequeueByID(0xA, &param));
    // The original closing snapshot preserves the selection and emits no new change.
    std::memset(bytes + 3072, 0, 32);
    rex::memory::store_and_swap<uint64_t>(bytes + 3072, xuid);
    ctx.r3.u64 = data + 3072; ctx.r4.u64 = 0x63; ctx.r5.u64 = 0;
    Call(fixture,ctx,base,__imp__XamUserLogon);
    CHECK(ctx.r3.u32 == 0);
    CHECK(profile->GetSigninState() == 1);
    CHECK_FALSE(listener->DequeueByID(0xA, &param));
    ctx.r3.u64 = xuid; ctx.r4.u64 = 1; ctx.r5.u64 = data;
    Call(fixture,ctx,base,__imp__XamUserGetIndexFromXUID);
    CHECK(ctx.r3.u32 == 0);
    CHECK(rex::memory::load_and_swap<uint32_t>(bytes) == 0);
    ctx.r3.u64 = 1; Call(fixture,ctx,base,__imp__XamUserGetUserIndexMask);
    CHECK(ctx.r3.u32 == 1);
    ctx.r3.u64 = 2; Call(fixture,ctx,base,__imp__XamUserGetUserIndexMask);
    CHECK(ctx.r3.u32 == 0);
    ctx.r3.u64 = 0; ctx.r4.u64 = 0; ctx.r5.u64 = data + 2048;
    Call(fixture,ctx,base,__imp__XamUserGetSigninInfo);
    CHECK(ctx.r3.u32 == 0);
    CHECK(rex::memory::load_and_swap<uint64_t>(bytes + 2048) == xuid);
    CHECK(std::string(reinterpret_cast<char*>(bytes + 2072)) == "Selected");
    // Cancellation while already connected also preserves the session.
    ctx.r3.u64 = second; Call(fixture,ctx,base,__imp__NtClose);
    CHECK(profile->GetSigninState() == 1);
    CHECK_FALSE(listener->DequeueByID(0xA, &param));
    memory->SystemHeapFree(data);
  }
  rex::test::KernelFixture recovered_fixture({}, {}, {}, {}, {}, store.path);
  CHECK((&rex::xam::CurrentXAM().GetUserProfile())->GetXUID() == xuid);
  CHECK((&rex::xam::CurrentXAM().GetUserProfile())->GetName() == "Selected");
  CHECK((&rex::xam::CurrentXAM().GetUserProfile())->GetSigninState() == 1);
}

TEST_CASE("Sign-in retail mode and local account queries do not leak input registers",
          "[sdk][signin-contract]") {
  Store store;
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, store.path);
  auto& rt=fixture.GetRuntime();
  auto* profile = (&rex::xam::CurrentXAM().GetUserProfile());
  REQUIRE(profile->CreateLocal("Retail") == 0);
  auto* base = rt.GetMembase().GetBase();
  auto address = rt.GetGuestHeap().SystemHeapAlloc(32);
  auto* bytes = base + address;
  PPCContext ctx{};
  ctx.r3.u64 = 0x90100000; // original scene's this pointer
  Call(fixture,ctx,base,__imp__XamIsDataCenterMode);
  CHECK(ctx.r3.u64 == 0);
  ctx.r3.u64 = 0x40123450; // normal frame's object pointer is not automation state
  Call(fixture,ctx,base,__imp__XamIsNuiAutomationEnabled);
  CHECK(ctx.r3.u64 == 0);
  std::memset(bytes, 0xCD, 8);
  ctx.r3.u64 = address; ctx.r4.u64 = address + 4;
  Call(fixture,ctx,base,__imp__XamGetCurrentDemand);
  CHECK(ctx.r3.u64 == 0);
  CHECK(rex::memory::load_and_swap<uint64_t>(bytes) == 0);
  CHECK(profile->GetSigninState() == 0);
  ctx.r3.u64 = 0; ctx.r4.u64 = 1; ctx.r5.u64 = address;
  Call(fixture,ctx,base,__imp__XamUserGetXUID);
  CHECK(ctx.r3.u32 == uint32_t(X_E_NO_SUCH_USER)); // no fabricated signed-in user
  REQUIRE(profile->SetLocalSession(profile->GetXUID()) == 0);
  for (uint32_t slot = 0; slot < 4; ++slot) {
    std::memset(bytes, 0xCD, 8);
    ctx.r3.u64 = slot; ctx.r4.u64 = address; ctx.r5.u64 = address + 4;
    Call(fixture,ctx,base,__imp__XamGetUserNeedTFA);
    CHECK(ctx.r3.u32 == (slot ? uint32_t(X_E_NO_SUCH_USER) : 0));
    CHECK(rex::memory::load_and_swap<uint64_t>(bytes) == (slot ? 0xCDCDCDCDCDCDCDCDull : 0));
    ctx.r3.u64 = slot;
    Call(fixture,ctx,base,__imp__XamUserGetUserFlags);
    CHECK(ctx.r3.u32 == 0);
  }
  ctx.r3.u64 = 0xFFFFFFFF;
  Call(fixture,ctx,base,__imp__XamAreMixedAccountsSignedIn);
  CHECK(ctx.r3.u32 == 0);
  ctx.r3.u64 = address;
  Call(fixture,ctx,base,__imp__GetSystemTimeAsFileTime);
  const auto first = rex::memory::load_and_swap<uint64_t>(bytes);
  CHECK(first > 116444736000000000ull); // FILETIME epoch, not milliseconds or untouched bytes
  ctx.r3.u64 = address;
  Call(fixture,ctx,base,__imp__GetSystemTimeAsFileTime);
  CHECK(rex::memory::load_and_swap<uint64_t>(bytes) >= first);
  rt.GetGuestHeap().SystemHeapFree(address);
}

TEST_CASE("Sign-in console settings persist original dialog decisions", "[sdk][signin-config]") {
  Store store;
  auto exercise = [&](bool recovered) {
    rex::test::KernelFixture fixture({}, {}, {}, {}, {}, store.path);
  auto& rt=fixture.GetRuntime();
    auto* base = rt.GetMembase().GetBase();
    const auto address = rt.GetGuestHeap().SystemHeapAlloc(16);
    PPCContext ctx{};
    auto read = [&] {
      ctx.r3.u64 = 3; ctx.r4.u64 = 12; ctx.r5.u64 = address;
      ctx.r6.u64 = 4; ctx.r7.u64 = address + 8;
      Call(fixture,ctx,base,__imp__ExGetXConfigSetting);
      REQUIRE(ctx.r3.u32 == 0);
      CHECK(rex::memory::load_and_swap<uint16_t>(base + address + 8) == 4);
      return rex::memory::load_and_swap<uint32_t>(base + address);
    };
    CHECK(read() == (recovered ? 0x10000044u : 0x40u));
    if (!recovered) {
      for (auto [mask, value] : {std::pair{~4u, 4u}, std::pair{0xEFFFFFFFu, 0x10000000u}}) {
        ctx.r3.u64 = 3; ctx.r4.u64 = 12; ctx.r5.u64 = mask; ctx.r6.u64 = value;
        Call(fixture,ctx,base,__imp__ExReadModifyWriteXConfigSettingUlong);
        REQUIRE(ctx.r3.u32 == 0);
      }
      CHECK(read() == 0x10000044u);
      // Never claim a write to an unsupported setting succeeded.
      ctx.r3.u64 = 3; ctx.r4.u64 = 0xFFFF; ctx.r5.u64 = 0; ctx.r6.u64 = ~0u;
      Call(fixture,ctx,base,__imp__ExReadModifyWriteXConfigSettingUlong);
      CHECK(ctx.r3.u32 == X_STATUS_INVALID_PARAMETER_2);
      CHECK(read() == 0x10000044u);
    }
    CHECK((&rex::xam::CurrentXAM().GetUserProfile())->GetSigninState() == 0);
    rt.GetGuestHeap().SystemHeapFree(address);
  };
  exercise(false);
  exercise(true);
}

TEST_CASE("Local profile Sign-in imports copy request and bounded UTF16 identity", "[sdk][local-profile][signin-profile]") {
  Store store;
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, store.path);
  auto& rt=fixture.GetRuntime();
  auto* memory = (&rt.GetGuestHeap());
  const auto address = memory->SystemHeapAlloc(128);
  auto* bytes = rex::kernel::GetAddressSpace(rt).TranslateVirtual<uint8_t*>(address);
  std::memset(bytes, 0xCD, 128);
  const uint32_t request[] = {0, 0xFF, 1, 0, 0};
  for (unsigned i = 0; i < 5; ++i)
    rex::memory::store_and_swap<uint32_t>(bytes + i * 4, request[i]);
  PPCContext ctx{};
  ctx.r3.u64 = address + 32; ctx.r4.u64 = address; ctx.r5.u64 = 20;
  Call(fixture,ctx,rt.GetMembase().GetBase(),__imp__XMemCpy);
  CHECK(ctx.r3.u32 == address + 32);
  CHECK(std::memcmp(bytes, bytes + 32, 20) == 0);
  CHECK(bytes[52] == 0xCD);
  ctx.r3.u64 = address + 32; ctx.r4.u64 = 0; ctx.r5.u64 = 20;
  Call(fixture,ctx,rt.GetMembase().GetBase(),__imp__XMemSet);
  CHECK(ctx.r3.u32 == address + 32);
  for (unsigned i = 32; i < 52; ++i) CHECK(bytes[i] == 0);
  CHECK(bytes[52] == 0xCD);
  for (unsigned i = 0; i < 5; ++i)
    rex::memory::store_and_swap<uint16_t>(bytes + i * 2, uint16_t(u"Name"[i]));
  for (auto count : {0u, 1u, 3u, 8u}) {
    std::memset(bytes + 32, 0xCD, 32);
    ctx.r3.u64 = address + 32; ctx.r4.u64 = address; ctx.r5.u64 = count;
    Call(fixture,ctx,rt.GetMembase().GetBase(),__imp__lstrcpynW);
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

TEST_CASE("Local profiles enumerate independently switch atomically and survive restart", "[sdk][local-profile]") {
  Store store;
  uint64_t first = 0, second = 0;
  {
    rex::test::KernelFixture fixture({}, {}, {}, {}, {}, store.path);
  auto& rt=fixture.GetRuntime();
    auto* p = (&rex::xam::CurrentXAM().GetUserProfile());
    REQUIRE(p->CreateLocal("Alpha", &first) == 0);
    REQUIRE(p->SetLocalSession(first) == 0);
    REQUIRE(p->CreateLocal("Bravo", &second) == 0);
    REQUIRE(first != second);
    CHECK(p->GetXUID() == first);
    CHECK(p->GetSigninState() == 1);
    CHECK(p->CreateLocal("ALPHA") == X_ERROR_ALREADY_EXISTS);
    auto* mem = (&rt.GetGuestHeap());
    auto* base = rt.GetMembase().GetBase();
    const auto data = mem->SystemHeapAlloc(4096);
    PPCContext ctx{};
    ctx.r3.u64 = 1; // selected local device from CreateProfile
    ctx.r4.u64 = data;
    Call(fixture,ctx,base,__imp__XamProfileCreateEnumerator);
    REQUIRE(ctx.r3.u32 == 0);
    const auto handle = rex::memory::load_and_swap<uint32_t>(base + data);
    for (auto xuid : {first, second}) {
      ctx.r3.u64 = handle; ctx.r4.u64 = 2; ctx.r5.u64 = data + 16; ctx.r6.u64 = 0;
      Call(fixture,ctx,base,__imp__XamProfileEnumerate);
      REQUIRE(ctx.r3.u32 == 0);
      CHECK(rex::memory::load_and_swap<uint64_t>(base + data + 16) == xuid);
    }
    ctx.r3.u64 = handle; ctx.r4.u64 = 2; ctx.r5.u64 = data + 16; ctx.r6.u64 = 0;
    Call(fixture,ctx,base,__imp__XamProfileEnumerate);
    CHECK(ctx.r3.u32 == X_ERROR_NO_MORE_FILES);
    ctx.r3.u64 = handle; Call(fixture,ctx,base,__imp__NtClose);
    std::memset(base + data, 0, 392 * 4);
    rex::memory::store_and_swap<uint64_t>(base + data, second);
    ctx.r3.u64 = data; ctx.r4.u64 = 0x120424; ctx.r5.u64 = 0;
    Call(fixture,ctx,base,__imp__XamUserLogonEx);
    REQUIRE(ctx.r3.u32 == 0);
    CHECK(p->GetName() == "Bravo");
    CHECK(p->FindLocal(first).signin_state == 0);
    CHECK(p->FindLocal(second).signin_state == 1);
    REQUIRE(p->SetLocalSession(first) == 0);
    CHECK(p->GetName() == "Alpha");
    REQUIRE(p->SetLocalSession(second) == 0);
    mem->SystemHeapFree(data);
  }
  {
    rex::test::KernelFixture fixture({}, {}, {}, {}, {}, store.path);
    auto* p = (&rex::xam::CurrentXAM().GetUserProfile());
    CHECK(p->local_profiles().size() == 2);
    CHECK(p->GetXUID() == second);
    CHECK(p->GetSigninState() == 1);
    CHECK(p->FindLocal(first).name == "Alpha");
    REQUIRE(p->SetLocalSession(0) == 0);
  }
  {
    rex::test::KernelFixture fixture({}, {}, {}, {}, {}, store.path);
    auto* p = (&rex::xam::CurrentXAM().GetUserProfile());
    CHECK(p->local_profiles().size() == 2);
    CHECK(p->GetSigninState() == 0);
    REQUIRE(p->SetLocalSession(first) == 0);
    CHECK(p->GetName() == "Alpha");
  }
}

TEST_CASE("Local profile creation uses guest account and consumable operation handles", "[sdk][local-profile]") {
  Store store;
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, store.path);
  auto& rt=fixture.GetRuntime();
  auto* mem = (&rt.GetGuestHeap());
  const auto data = mem->SystemHeapAlloc(1024);
  auto* base = rt.GetMembase().GetBase();
  std::memset(base + data, 0, 1024);
  auto* profile = (&rex::xam::CurrentXAM().GetUserProfile());
  PPCContext ctx{};
  auto create = [&](const std::u16string& name) {
    std::memset(base + data + 24, 0, 32);
    for (size_t i = 0; i < name.size(); ++i)
      rex::memory::store_and_swap<uint16_t>(base + data + 24 + i * 2, name[i]);
    ctx.r3.u64 = 1; ctx.r4.u64 = data; ctx.r5.u64 = 0; ctx.r6.u64 = data + 16;
    ctx.r7.u64 = 0; ctx.r8.u64 = 0; ctx.r9.u64 = 0; ctx.r10.u64 = data + 512;
    Call(fixture,ctx,base,__imp__XamProfileCreate);
    REQUIRE(ctx.r3.u32 == 0);
    const auto handle = rex::memory::load_and_swap<uint32_t>(base + data + 512);
    REQUIRE(handle != 0);
    ctx.r3.u64 = handle; ctx.r4.u64 = data + 520;
    Call(fixture,ctx,base,__imp__XamProfileGetCreationStatus);
    const auto result = ctx.r3.u32;
    const auto xuid = rex::memory::load_and_swap<uint64_t>(base + data + 520);
    ctx.r3.u64 = handle; ctx.r4.u64 = data + 520;
    Call(fixture,ctx,base,__imp__XamProfileGetCreationStatus);
    CHECK(ctx.r3.u32 == uint32_t(X_E_INVALIDARG));
    return std::pair{result, xuid};
  };
  const auto [r1, x1] = create(u"CreateOne");
  REQUIRE(r1 == 0);
  CHECK(profile->FindLocal(x1).name == "CreateOne");
  const auto [r2, x2] = create(u"CreateTwo");
  REQUIRE(r2 == 0);
  CHECK(x1 != x2);
  CHECK(profile->local_profiles().size() == 2);
  CHECK(profile->GetSigninState() == 0);
  const auto [duplicate, invalid] = create(u"CREATEONE");
  CHECK(duplicate == uint32_t(XHRESULTFromWin32(X_ERROR_ALREADY_EXISTS)));
  CHECK(invalid == 0);
  CHECK(profile->local_profiles().size() == 2);
  // Original CreateProfile 17559 confirms its newly created account with
  // four XUIDs and Logon flags 0x24.
  std::memset(base + data + 600, 0, 96);
  rex::memory::store_and_swap<uint64_t>(base + data + 600, x2);
  ctx.r3.u64 = data + 600; ctx.r4.u64 = 0x24; ctx.r5.u64 = 0;
  Call(fixture,ctx,base,__imp__XamUserLogon);
  REQUIRE(ctx.r3.u32 == 0);
  CHECK(profile->identity().xuid == x2);
  CHECK(profile->identity().name == "CreateTwo");
  CHECK(profile->GetSigninState() == 1);
  mem->SystemHeapFree(data);
}

TEST_CASE("Original profile rename commits a stable XUID and preserves other user data",
          "[sdk][local-profile][profile-rename]") {
  Store store;
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, store.path);
  auto& rt=fixture.GetRuntime();
  auto* profile = (&rex::xam::CurrentXAM().GetUserProfile());
  uint64_t alpha = 0, bravo = 0;
  REQUIRE(profile->CreateLocal("Alpha", &alpha) == 0);
  REQUIRE(profile->CreateLocal("Bravo", &bravo) == 0);
  REQUIRE(profile->SetLocalSession(alpha) == 0);
  const auto epoch = profile->GetStorageEpoch();
  auto listener_handle=rt.GetNotificationBroadcaster().AddNotifyListener(1,10);REQUIRE(listener_handle);
    rex::ScopeGuard close_listener([&] {CloseOwned(rt.GetObjectTable(),*listener_handle);});
    rex::kernel::Pinned<rex::kernel::INotifyListenerQueue> listener(rt.GetObjectTable().Get(*listener_handle));
    REQUIRE(listener);
  uint32_t notification_id = 0, notification_data = 0;
  while (listener->Dequeue(&notification_id, &notification_data)) {}
  const auto marker = store.path / "offline-achievements-v1" / "keep.dat";
  std::filesystem::create_directories(marker.parent_path());
  { std::ofstream out(marker, std::ios::binary); out << "committed-other-data"; }
  const auto buffer = rt.GetGuestHeap().SystemHeapAlloc(64);
  REQUIRE(buffer);
  auto rename = [&](uint64_t xuid, const std::u16string& name) {
    std::memset(rex::kernel::GetAddressSpace(rt).TranslateVirtual(buffer), 0, 64);
    for (size_t i = 0; i < name.size() && i < 24; ++i)
      rex::memory::store_and_swap<uint16_t>(rex::kernel::GetAddressSpace(rt).TranslateVirtual(buffer + uint32_t(i * 2)), name[i]);
    PPCContext ctx{};
    ctx.r3.u64 = xuid; ctx.r4.u64 = buffer;
    Call(fixture,ctx,rt.GetMembase().GetBase(),__imp__XamProfileRenameAccount);
    return ctx.r3.u32;
  };
  SECTION("rename and case-only rename survive recovery") {
    REQUIRE(rename(alpha, u"Renamed") == 0);
    REQUIRE(listener->DequeueByID(0xE, &notification_data));
    CHECK(notification_data == 1);
    CHECK(profile->GetXUID() == alpha); CHECK(profile->GetName() == "Renamed");
    CHECK(profile->GetSigninState() == 1); CHECK(profile->FindLocal(bravo).name == "Bravo");
    REQUIRE(rename(alpha, u"RENAMED") == 0);
    rex::xam::UserProfile recovered;
    REQUIRE(recovered.InitializeLocal(store.path));
    CHECK(recovered.GetXUID() == alpha); CHECK(recovered.GetName() == "RENAMED");
    CHECK(recovered.FindLocal(bravo).name == "Bravo");
    CHECK(profile->GetStorageEpoch() == epoch); // Existing save handles keep their identity.
  }
  SECTION("duplicate invalid and unknown identities leave state unchanged") {
    CHECK(rename(alpha, u"BRAVO") == X_ERROR_ALREADY_EXISTS);
    for (const auto& name : {u"", u" leading", u"trailing ", u"abcdefghijklmnop", u"\u00E9"})
      CHECK(rename(alpha, name) == X_ERROR_INVALID_PARAMETER);
    CHECK(rename(alpha + 2, u"Missing") == X_ERROR_NO_SUCH_USER);
    CHECK(profile->GetName() == "Alpha"); CHECK(profile->FindLocal(bravo).name == "Bravo");
    CHECK_FALSE(listener->DequeueByID(0xE, &notification_data));
    PPCContext ctx{};
    ctx.r3.u64 = alpha; ctx.r4.u64 = 5;
    Call(fixture,ctx,rt.GetMembase().GetBase(),__imp__XamProfileRenameAccount);
    CHECK(ctx.r3.u32 == X_ERROR_INVALID_PARAMETER);
  }
  SECTION("failed atomic replace preserves committed name") {
    const auto staging = ProfilePackage(store.path, alpha) / "account.dat.tmp";
    std::filesystem::create_directory(staging);
    CHECK(rename(alpha, u"MustNotCommit") == X_ERROR_FUNCTION_FAILED);
    CHECK(profile->GetName() == "Alpha");
    rex::xam::UserProfile recovered;
    REQUIRE(recovered.InitializeLocal(store.path));
    CHECK(recovered.GetName() == "Alpha"); CHECK(recovered.GetXUID() == alpha);
  }
  CHECK(profile->local_profiles().size() == 2);
  std::string preserved;
  { std::ifstream in(marker, std::ios::binary); std::getline(in, preserved); }
  CHECK(preserved == "committed-other-data");
  rt.GetGuestHeap().SystemHeapFree(buffer);
}

TEST_CASE("Guide sign-out removes only the active local identity", "[sdk][local-profile]") {
  Store store;
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, store.path);
  auto& rt=fixture.GetRuntime();
  auto* profile = (&rex::xam::CurrentXAM().GetUserProfile());
  uint64_t alpha = 0, bravo = 0;
  REQUIRE(profile->CreateLocal("Alpha", &alpha) == 0);
  REQUIRE(profile->CreateLocal("Bravo", &bravo) == 0);
  REQUIRE(profile->SetLocalSession(alpha) == 0);
  const auto users = rt.GetGuestHeap().SystemHeapAlloc(32);
  auto* base = rt.GetMembase().GetBase();
  std::memset(base + users, 0, 32);
  auto remove = [&](uint64_t xuid) {
    rex::memory::store_and_swap<uint64_t>(base + users, xuid);
    PPCContext ctx{};
    ctx.r3.u64 = users; ctx.r4.u64 = 0x28; ctx.r5.u64 = 0;
    Call(fixture,ctx,base,__imp__XamUserLogon);
    return ctx.r3.u32;
  };
  CHECK(remove(bravo) == uint32_t(XHRESULTFromWin32(X_ERROR_NO_SUCH_USER)));
  CHECK(profile->identity().xuid == alpha);
  CHECK(profile->GetSigninState() == 1);
  REQUIRE(remove(alpha) == 0);
  CHECK(profile->GetSigninState() == 0);
  CHECK(profile->local_profiles().size() == 2);
  rex::xam::UserProfile recovered;
  REQUIRE(recovered.InitializeLocal(store.path));
  CHECK(recovered.GetSigninState() == 0);
  CHECK(recovered.FindLocal(alpha).name == "Alpha");
  CHECK(recovered.FindLocal(bravo).name == "Bravo");
  rt.GetGuestHeap().SystemHeapFree(users);
}

TEST_CASE("Confirmed profile deletion removes only the profile package and keeps account content",
          "[sdk][local-profile][profile-delete]") {
  Store store;
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, store.path);
  auto& rt=fixture.GetRuntime();
  auto* profile = (&rex::xam::CurrentXAM().GetUserProfile());
  uint64_t alpha = 0, bravo = 0;
  REQUIRE(profile->CreateLocal("Alpha", &alpha) == 0);
  REQUIRE(profile->CreateLocal("Bravo", &bravo) == 0);
  REQUIRE(profile->SetLocalSession(alpha) == 0);
  auto remove = [&](uint64_t xuid, uint32_t device = 1) {
    PPCContext ctx{};
    ctx.r3.u64 = device; ctx.r4.u64 = xuid;
    Call(fixture,ctx,rt.GetMembase().GetBase(),__imp__XamProfileDelete);
    return ctx.r3.u32;
  };
  auto read = [](const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  };
  auto header = [&](uint64_t xuid) {
    const auto package = ProfilePackage(store.path, xuid);
    return package.parent_path().parent_path() / "Headers" / "00010000" /
           (package.filename().string() + ".header");
  };
  for (auto xuid : {alpha, bravo}) {
    REQUIRE(std::filesystem::is_regular_file(ProfilePackage(store.path, xuid) / "account.dat"));
    REQUIRE(std::filesystem::is_regular_file(header(xuid)));
  }
  const auto marker = store.path / "offline-achievements-v1" / "keep.dat";
  // A saved game of the deleted account stays on the device, as on the console.
  const auto save = ProfilePackage(store.path, alpha).parent_path().parent_path().parent_path() /
                    "4D5307E6" / "00000001" / "Slot" / "keep.dat";
  for (const auto& path : {marker, save}) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary); out << "unchanged-account-content";
  }
  const auto epoch = profile->GetStorageEpoch();
  SECTION("inactive removal preserves the active identity") {
    REQUIRE(remove(bravo) == 0);
    CHECK(profile->GetXUID() == alpha); CHECK(profile->GetSigninState() == 1);
    CHECK(profile->local_profiles().size() == 1); CHECK(profile->FindLocal(bravo).xuid == 0);
    CHECK(profile->GetStorageEpoch() == epoch);
    CHECK_FALSE(std::filesystem::exists(ProfilePackage(store.path, bravo)));
    CHECK_FALSE(std::filesystem::exists(header(bravo)));
    rex::xam::UserProfile recovered;
    REQUIRE(recovered.InitializeLocal(store.path));
    CHECK(recovered.GetXUID() == alpha); CHECK(recovered.local_profiles().size() == 1);
  }
  SECTION("active removal signs out and revokes the previous storage identity") {
    REQUIRE(remove(alpha) == 0);
    CHECK(profile->GetXUID() == bravo); CHECK(profile->GetSigninState() == 0);
    CHECK(profile->GetStorageEpoch() > epoch);
    rex::xam::UserProfile recovered;
    REQUIRE(recovered.InitializeLocal(store.path));
    CHECK(recovered.GetXUID() == bravo); CHECK(recovered.GetSigninState() == 0);
  }
  SECTION("final removal leaves no profile") {
    REQUIRE(remove(bravo) == 0); REQUIRE(remove(alpha) == 0);
    CHECK(profile->local_profiles().empty()); CHECK(profile->GetXUID() == 0);
    rex::xam::UserProfile recovered;
    REQUIRE(recovered.InitializeLocal(store.path));
    CHECK(recovered.local_profiles().empty()); CHECK(recovered.GetSigninState() == 0);
    REQUIRE(recovered.CreateLocal("NewIdentity") == 0);
    CHECK(recovered.local_profiles().size() == 1); CHECK(recovered.GetXUID() != alpha);
  }
  SECTION("failed session commit keeps the old state and the package") {
    std::filesystem::create_directories(store.path / "profiles/session-v1.dat.tmp");
    CHECK(remove(alpha) == uint32_t(XHRESULTFromWin32(X_ERROR_FUNCTION_FAILED)));
    CHECK(profile->GetXUID() == alpha); CHECK(profile->local_profiles().size() == 2);
    CHECK(profile->GetSigninState() == 1); CHECK(profile->GetStorageEpoch() == epoch);
    CHECK(std::filesystem::is_regular_file(ProfilePackage(store.path, alpha) / "account.dat"));
  }
  CHECK(remove(alpha + 2) == uint32_t(XHRESULTFromWin32(X_ERROR_NO_SUCH_USER)));
  CHECK(remove(alpha, 2) == uint32_t(XHRESULTFromWin32(X_ERROR_DEVICE_NOT_CONNECTED)));
  CHECK(read(marker) == "unchanged-account-content");
  CHECK(read(save) == "unchanged-account-content");
}

TEST_CASE("Profiles are storage content listed refreshed and locked while signed in",
          "[sdk][local-profile][profile-content]") {
  using namespace rex::kernel;
  Store store;
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, store.path);
  auto* profile = (&rex::xam::CurrentXAM().GetUserProfile());
  auto* content = (&rex::xam::CurrentXAM().GetContentManager());
  uint64_t alpha = 0, bravo = 0;
  REQUIRE(profile->CreateLocal("Alpha", &alpha) == 0);
  REQUIRE(profile->CreateLocal("Bravo", &bravo) == 0);
  REQUIRE(profile->SetLocalSession(alpha) == 0);
  auto find = [&](uint64_t xuid) {
    std::optional<rex::kernel::XCONTENT_AGGREGATE_DATA> found;
    for (const auto& item : content->ListContent(1, xuid, XContentType::kProfile, 0xFFFE07D1))
      if (uint64_t(item.xuid) == xuid) found = item;
    return found;
  };
  const auto active = find(alpha), inactive = find(bravo);
  REQUIRE(active); REQUIRE(inactive);
  CHECK(active->GetDisplayName() == u"Alpha");
  CHECK(inactive->GetDisplayName() == u"Bravo");
  CHECK(uint32_t(active->title_id) == 0xFFFE07D1u);
  // The signed-in profile cannot be deleted or moved from Storage.
  const auto rejected=content->DeleteContent(alpha,*active); REQUIRE_FALSE(rejected);
  CHECK(ToXResult(rejected.error())==X_ERROR_ACCESS_DENIED);
  CHECK(content->TransferContent(alpha, *active, 1, alpha, true, {}) == X_ERROR_ACCESS_DENIED);
  CHECK(profile->FindLocal(alpha).xuid == alpha);
  // Deleting a signed-out profile package removes the account at once.
  REQUIRE(content->DeleteContent(bravo, *inactive));
  CHECK(profile->FindLocal(bravo).xuid == 0);
  CHECK(profile->local_profiles().size() == 1);
  CHECK(profile->GetXUID() == alpha); CHECK(profile->GetSigninState() == 1);
  // Profile-wide data lives inside the package.
  CHECK(profile->PackagePath(alpha) == rex::platform::HostPath(ProfilePackage(store.path, alpha)));
  CHECK(profile->PackagePath(bravo).empty());
}

TEST_CASE("Local profile commits real XAM identity and recovers session on restart", "[sdk][local-profile]") {
  Store store;
  uint64_t saved_xuid = 0;
  {
    rex::test::KernelFixture fixture({}, {}, {}, {}, {}, store.path);
  auto& rt=fixture.GetRuntime();
    auto* profile = (&rex::xam::CurrentXAM().GetUserProfile());
    auto listener_handle=rt.GetNotificationBroadcaster().AddNotifyListener(1,10);REQUIRE(listener_handle);
    rex::ScopeGuard close_listener([&] {CloseOwned(rt.GetObjectTable(),*listener_handle);});
    rex::kernel::Pinned<rex::kernel::INotifyListenerQueue> listener(rt.GetObjectTable().Get(*listener_handle));
    REQUIRE(listener);
    uint32_t param = 0;
    CHECK_FALSE(listener->DequeueByID(0xA, &param));
    REQUIRE(profile->GetSigninState() == 0);
    REQUIRE(profile->CreateLocal("Marina") == X_ERROR_SUCCESS);
    saved_xuid = profile->GetXUID();
    CHECK((saved_xuid & 0xFFF0000000000000ull) == 0xE000000000000000ull);
    CHECK((saved_xuid & 0x00C0000000000000ull) == 0);
    CHECK_FALSE(listener->DequeueByID(0xA, &param));
    CHECK(profile->CreateLocal("marina") == X_ERROR_ALREADY_EXISTS);

    auto* mem = (&rt.GetGuestHeap());
    const auto users = mem->SystemHeapAlloc(392 * 4);
    const auto info = mem->SystemHeapAlloc(40);
    std::memset(rex::kernel::GetAddressSpace(rt).TranslateVirtual(users), 0, 392 * 4);
    rex::memory::store_and_swap<uint64_t>(rex::kernel::GetAddressSpace(rt).TranslateVirtual(users), saved_xuid);
    PPCContext ctx{};
    auto logon = [&](uint32_t flags = 0) {
      ctx.r3.u64 = users; ctx.r4.u64 = flags; ctx.r5.u64 = 0;
      Call(fixture,ctx,rt.GetMembase().GetBase(),__imp__XamUserLogon);
      return ctx.r3.u32;
    };
    CHECK(logon(0x80) == 0x80004001u);
    CHECK(profile->GetSigninState() == 0);
    REQUIRE(logon() == 0);
    REQUIRE(listener->DequeueByID(0xA, &param));
    CHECK(param == 1);
    CHECK(logon() == 0);
    CHECK_FALSE(listener->DequeueByID(0xA, &param));

    ctx.r3.u64 = 0; ctx.r4.u64 = 0; ctx.r5.u64 = info;
    Call(fixture,ctx,rt.GetMembase().GetBase(),__imp__XamUserGetSigninInfo);
    REQUIRE(ctx.r3.u32 == 0);
    auto* data = rex::kernel::GetAddressSpace(rt).TranslateVirtual<uint8_t*>(info);
    CHECK(rex::memory::load_and_swap<uint64_t>(data) == saved_xuid);
    CHECK(rex::memory::load_and_swap<uint32_t>(data + 12) == 1);
    CHECK(std::string(reinterpret_cast<char*>(data + 24)) == "Marina");
    ctx.r3.u64 = 0; ctx.r4.u64 = 2; ctx.r5.u64 = info;
    Call(fixture,ctx,rt.GetMembase().GetBase(),__imp__XamUserGetXUID);
    CHECK(ctx.r3.u32 == uint32_t(X_E_NO_SUCH_USER));
    CHECK(rex::memory::load_and_swap<uint64_t>(data) == 0);

    // Reject unsupported slots as a whole, preserving the previous session.
    rex::memory::store_and_swap<uint64_t>(rex::kernel::GetAddressSpace(rt).TranslateVirtual(users + 8), saved_xuid);
    CHECK(logon() == 0x80004001u);
    CHECK(profile->GetSigninState() == 1);
    CHECK_FALSE(listener->DequeueByID(0xA, &param));
    mem->SystemHeapFree(info);
    mem->SystemHeapFree(users);
  }
  {
    rex::test::KernelFixture fixture({}, {}, {}, {}, {}, store.path);
  auto& rt=fixture.GetRuntime();
    auto* profile = (&rex::xam::CurrentXAM().GetUserProfile());
    CHECK(profile->GetXUID() == saved_xuid);
    CHECK(profile->GetName() == "Marina");
    CHECK(profile->GetSigninState() == 1);
    auto* mem = (&rt.GetGuestHeap());
    const auto users = mem->SystemHeapAlloc(392 * 4);
    std::memset(rex::kernel::GetAddressSpace(rt).TranslateVirtual(users), 0, 392 * 4);
    PPCContext ctx{};
    ctx.r3.u64 = users; ctx.r4.u64 = 0; ctx.r5.u64 = 0;
    Call(fixture,ctx,rt.GetMembase().GetBase(),__imp__XamUserLogonEx);
    CHECK(ctx.r3.u32 == 0);
    ctx.r3.u64 = 0;
    Call(fixture,ctx,rt.GetMembase().GetBase(),__imp__XamUserGetSigninState);
    CHECK(ctx.r3.u32 == 0);
    ctx.r3.u64 = 0; ctx.r4.u64 = 0; ctx.r5.u64 = users;
    Call(fixture,ctx,rt.GetMembase().GetBase(),__imp__XamUserGetSigninInfo);
    CHECK(ctx.r3.u32 == uint32_t(X_E_NO_SUCH_USER));
    CHECK(rex::memory::load_and_swap<uint64_t>(rex::kernel::GetAddressSpace(rt).TranslateVirtual(users)) == 0);
    mem->SystemHeapFree(users);
  }
  {
    rex::test::KernelFixture fixture({}, {}, {}, {}, {}, store.path);
    const auto identity = (&rex::xam::CurrentXAM().GetUserProfile())->identity();
    CHECK(identity.xuid == saved_xuid);
    CHECK(identity.name == "Marina");
    CHECK(identity.signin_state == 0);
  }
}

TEST_CASE("Local profile rejects corruption and failed disk writes without publishing state", "[sdk][local-profile]") {
  Store store;
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, store.path);
  auto* profile = (&rex::xam::CurrentXAM().GetUserProfile());
  CHECK(profile->CreateLocal("") == X_ERROR_INVALID_PARAMETER);
  CHECK(profile->CreateLocal("1234567890123456") == X_ERROR_INVALID_PARAMETER);
  CHECK(profile->CreateLocal(" Name") == X_ERROR_INVALID_PARAMETER);
  REQUIRE(profile->CreateLocal("Local") == 0);
  const auto xuid = profile->GetXUID();
  CHECK(profile->SetLocalSession(xuid + 2) == X_ERROR_NO_SUCH_USER);
  // A directory occupying the staging filename deterministically denies writes.
  std::filesystem::create_directories(store.path / "profiles/session-v1.dat.tmp");
  CHECK(profile->SetLocalSession(xuid) == X_ERROR_FUNCTION_FAILED);
  CHECK(profile->GetSigninState() == 0);
  rex::xam::UserProfile recovered;
  REQUIRE(recovered.InitializeLocal(store.path));
  CHECK(recovered.GetXUID() == xuid);
  CHECK(recovered.GetSigninState() == 0);
  std::filesystem::remove(store.path / "profiles/session-v1.dat.tmp");
  { std::ofstream corrupt(store.path / "profiles/session-v1.dat", std::ios::binary); corrupt << "broken"; }
  CHECK_FALSE(recovered.InitializeLocal(store.path));
  CHECK(recovered.GetXUID() == 0);
  CHECK(recovered.CreateLocal("Replacement") == X_ERROR_FUNCTION_FAILED);
  CHECK(std::filesystem::file_size(store.path / "profiles/session-v1.dat") == 6);
}

TEST_CASE("Local profile logon completes overlapped with the real result", "[sdk][local-profile]") {
  Store store;
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, store.path);
  auto& rt=fixture.GetRuntime();
  auto* profile = (&rex::xam::CurrentXAM().GetUserProfile());
  REQUIRE(profile->CreateLocal("Async") == 0);
  auto* memory = (&rt.GetGuestHeap());
  const auto users = memory->SystemHeapAlloc(32);
  const auto overlap = memory->SystemHeapAlloc(sizeof(rex::kernel::XOVERLAPPED));
  std::memset(rex::kernel::GetAddressSpace(rt).TranslateVirtual(users), 0, 32);
  auto* ov = rex::kernel::GetAddressSpace(rt).TranslateVirtual<rex::kernel::XOVERLAPPED*>(overlap);
  uint32_t result = ~0u, failure = 0, success_result = ~0u, failure_result = 0;
  uint32_t barrier = ~0u, barrier_result = ~0u;
  fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
    auto* ctx=&caller.GetContext();
    auto call = [&](uint64_t xuid) {
      *ov = {};
      ov->result = 997;
      rex::memory::store_and_swap<uint64_t>(rex::kernel::GetAddressSpace(rt).TranslateVirtual(users), xuid);
      ctx->r3.u64 = users; ctx->r4.u64 = 0x24; ctx->r5.u64 = overlap;
      __imp__XamUserLogon(*ctx, rt.GetMembase().GetBase());
      return ctx->r3.u32;
    };
    result = call(profile->GetXUID());
    success_result = ov->result;
    failure = call(profile->GetXUID() + 2);
    failure_result = ov->result;
    *ov = {};
    ov->result = 997;
    ctx->r3.u64 = overlap;
    __imp__XamUserFlushLogonQueue(*ctx, rt.GetMembase().GetBase());
    barrier = ctx->r3.u32;
    barrier_result = ov->result;
  });
  CHECK(result == 0);
  CHECK(success_result == 0);
  CHECK(failure == uint32_t(X_E_NO_SUCH_USER));
  CHECK(failure_result == uint32_t(X_ERROR_NO_SUCH_USER));
  CHECK(barrier == 0);
  CHECK(barrier_result == 0);
  CHECK(profile->GetSigninState() == 1);
  memory->SystemHeapFree(overlap);
  memory->SystemHeapFree(users);
}

extern "C" void __imp__XGetOverlappedResult(PPCContext&, uint8_t*);
extern "C" void __imp__XGetOverlappedExtendedError(PPCContext&, uint8_t*);
TEST_CASE("Profile device completion queries return results and honor nonblocking waits", "[sdk][local-profile]") {
  Store store;
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, store.path);
  auto& rt=fixture.GetRuntime();
  auto* mem = (&rt.GetGuestHeap());
  const auto data = mem->SystemHeapAlloc(64);
  auto* ov = rex::kernel::GetAddressSpace(rt).TranslateVirtual<rex::kernel::XOVERLAPPED*>(data);
  *ov = {};
  ov->result = X_ERROR_IO_PENDING;
  PPCContext ctx{};
  ctx.r3.u64 = data; ctx.r4.u64 = data + 32; ctx.r5.u64 = 0;
  Call(fixture,ctx,rt.GetMembase().GetBase(),__imp__XGetOverlappedResult);
  CHECK(ctx.r3.u32 == X_ERROR_IO_INCOMPLETE);
  ctx.r3.u64 = data;
  Call(fixture,ctx,rt.GetMembase().GetBase(),__imp__XGetOverlappedExtendedError);
  CHECK(ctx.r3.u32 == X_ERROR_IO_INCOMPLETE);
  ov->result = X_ERROR_SUCCESS; ov->length = 7; ov->extended_error = 0;
  ctx.r3.u64 = data; ctx.r4.u64 = data + 32; ctx.r5.u64 = 1;
  Call(fixture,ctx,rt.GetMembase().GetBase(),__imp__XGetOverlappedResult);
  CHECK(ctx.r3.u32 == 0);
  CHECK(rex::memory::load_and_swap<uint32_t>(rex::kernel::GetAddressSpace(rt).TranslateVirtual(data + 32)) == 7);
  ov->result = X_ERROR_CANCELLED; ov->extended_error = X_ERROR_CANCELLED;
  ctx.r3.u64 = data;
  Call(fixture,ctx,rt.GetMembase().GetBase(),__imp__XGetOverlappedExtendedError);
  CHECK(ctx.r3.u32 == X_ERROR_CANCELLED);
  mem->SystemHeapFree(data);
}

TEST_CASE("Profile packages and failed session writes preserve the last committed identities",
          "[sdk][local-profile]") {
  Store store;
  rex::xam::UserProfile profile;
  REQUIRE(profile.InitializeLocal(store.path));
  uint64_t first = 0, second = 0;
  REQUIRE(profile.CreateLocal("First", &first) == 0);
  REQUIRE(profile.SetLocalSession(first) == 0);
  // Each profile is its own package on the device; there is no profile index.
  CHECK(std::filesystem::is_regular_file(ProfilePackage(store.path, first) / "account.dat"));
  CHECK(profile.CreateLocal("Second", nullptr, 2) == X_ERROR_DEVICE_NOT_CONNECTED);
  REQUIRE(profile.CreateLocal("Second", &second) == 0);
  CHECK(std::filesystem::is_regular_file(ProfilePackage(store.path, second) / "account.dat"));
  CHECK_FALSE(std::filesystem::exists(store.path / "profiles/local-v2.dat"));
  const auto staging = store.path / "profiles/session-v1.dat.tmp";
  std::filesystem::create_directory(staging);
  CHECK(profile.SetLocalSession(second) == X_ERROR_FUNCTION_FAILED);
  CHECK(profile.GetXUID() == first);
  rex::xam::UserProfile recovered;
  REQUIRE(recovered.InitializeLocal(store.path));
  CHECK(recovered.local_profiles().size() == 2);
  CHECK(recovered.GetXUID() == first);
  CHECK(recovered.GetSigninState() == 1);
  std::filesystem::remove(staging);
  // A damaged package is not a profile; the others remain.
  { std::ofstream corrupt(ProfilePackage(store.path, second) / "account.dat", std::ios::binary); corrupt << "broken"; }
  REQUIRE(recovered.InitializeLocal(store.path));
  CHECK(recovered.local_profiles().size() == 1);
  CHECK(recovered.FindLocal(second).xuid == 0);
  // A damaged session record keeps everyone signed out and refuses writes.
  { std::ofstream corrupt(store.path / "profiles/session-v1.dat", std::ios::binary); corrupt << "broken"; }
  CHECK_FALSE(recovered.InitializeLocal(store.path));
  CHECK(recovered.local_profiles().empty());
  CHECK(recovered.CreateLocal("Replacement") == X_ERROR_FUNCTION_FAILED);
  CHECK(std::filesystem::file_size(store.path / "profiles/session-v1.dat") == 6);
}

TEST_CASE("Older XUID-array logon confirms persisted local identities and rejects online modes",
          "[sdk][local-logon-array]") {
  Store store;
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, store.path);
  auto& rt=fixture.GetRuntime();
  auto* profile = (&rex::xam::CurrentXAM().GetUserProfile());
  uint64_t xuid = 0;
  REQUIRE(profile->CreateLocal("Blades", &xuid) == 0);
  auto* memory = (&rt.GetGuestHeap());
  const auto users = memory->SystemHeapAlloc(32);
  REQUIRE(users);
  auto* bytes = rex::kernel::GetAddressSpace(rt).TranslateVirtual<uint8_t*>(users);
  std::memset(bytes, 0, 32);
  rex::memory::store_and_swap<uint64_t>(bytes, xuid);
  PPCContext ctx{};
  auto logon = [&](uint32_t flags) {
    ctx.r3.u64 = users; ctx.r4.u64 = flags; ctx.r5.u64 = 0;
    Call(fixture,ctx,rt.GetMembase().GetBase(),__imp__XamUserLogon);
    return ctx.r3.u32;
  };
  CHECK(logon(0x421) == 0x80004001u);
  CHECK(profile->GetSigninState() == 0);
  for (uint32_t flags : {0x420u, 0x424u, 0x23u}) {
    REQUIRE(logon(flags) == 0);
    CHECK(profile->identity().xuid == xuid);
    CHECK(profile->GetSigninState() == 1);
  }
  CHECK(logon(0x425) == 0x80004001u);
  CHECK(profile->GetSigninState() == 1);
  rex::memory::store_and_swap<uint64_t>(bytes + 8, xuid);
  CHECK(logon(0x420) == 0x80004001u); // multi-user rejection is atomic
  CHECK(profile->GetSigninState() == 1);
  rex::memory::store_and_swap<uint64_t>(bytes + 8, 0);
  rex::memory::store_and_swap<uint64_t>(bytes, xuid + 1);
  CHECK(logon(0x420) != 0); // cannot fabricate an account
  CHECK(profile->identity().xuid == xuid);
  memory->SystemHeapFree(users);
}

TEST_CASE("Existing profile copy joins slot zero and survives runtime restart",
          "[.existing-profile-copy]") {
  namespace fs = std::filesystem;
  const char* configured = std::getenv("X360N_TEST_PROFILE_COPY_ROOT");
  REQUIRE(configured);
  const auto copy = fs::canonical(x360n::PathFromUtf8(configured));
  const auto evidence = fs::canonical(fs::path(X360N_SOURCE_ROOT) / ".local/evidence");
  const auto relative = fs::relative(copy, evidence);
  // This opt-in test may persist a selection only in an isolated evidence copy.
  REQUIRE_FALSE(relative.empty());
  REQUIRE_FALSE(relative.is_absolute());
  REQUIRE(*relative.begin() != "..");
  uint64_t selected = 0;
  size_t count = 0;
  {
    // The copy is the whole hard drive: profile packages and session.
    rex::test::KernelFixture fixture({}, {}, {}, {}, {}, copy);
  auto& rt=fixture.GetRuntime();
    auto* profile = (&rex::xam::CurrentXAM().GetUserProfile());
    const auto accounts = profile->local_profiles();
    REQUIRE_FALSE(accounts.empty());
    count = accounts.size();
    selected = profile->GetSigninState() ? profile->GetXUID() : accounts.front().xuid;
    REQUIRE(selected != 0);
    auto* memory = (&rt.GetGuestHeap());
    const auto users = memory->SystemHeapAlloc(392 * 4);
    const auto info = memory->SystemHeapAlloc(40);
    REQUIRE(users != 0);
    REQUIRE(info != 0);
    std::memset(rex::kernel::GetAddressSpace(rt).TranslateVirtual(users), 0, 392 * 4);
    rex::memory::store_and_swap<uint64_t>(rex::kernel::GetAddressSpace(rt).TranslateVirtual(users), selected);
    PPCContext ctx{};
    ctx.r3.u64 = users; ctx.r4.u64 = 0; ctx.r5.u64 = 0;
    Call(fixture,ctx,rt.GetMembase().GetBase(),__imp__XamUserLogon);
    REQUIRE(ctx.r3.u32 == 0);
    CHECK(profile->GetXUID() == selected);
    CHECK(profile->GetSigninState() == 1);
    CHECK(profile->local_profiles().size() == count);
    ctx.r3.u64 = 0; ctx.r4.u64 = 0; ctx.r5.u64 = info;
    Call(fixture,ctx,rt.GetMembase().GetBase(),__imp__XamUserGetSigninInfo);
    REQUIRE(ctx.r3.u32 == 0);
    CHECK(rex::memory::load_and_swap<uint64_t>(rex::kernel::GetAddressSpace(rt).TranslateVirtual(info)) == selected);
    CHECK(rex::memory::load_and_swap<uint32_t>(rex::kernel::GetAddressSpace(rt).TranslateVirtual(info + 12)) == 1);
    ctx.r3.u64 = 0; ctx.r4.u64 = 0; ctx.r5.u64 = info;
    Call(fixture,ctx,rt.GetMembase().GetBase(),__imp__XamUserGetXUID);
    CHECK(ctx.r3.u32 == 0x80070057u); // zero is not a valid XUID type mask
    ctx.r3.u64 = 0; ctx.r4.u64 = 1; ctx.r5.u64 = info; // local identities
    Call(fixture,ctx,rt.GetMembase().GetBase(),__imp__XamUserGetXUID);
    REQUIRE(ctx.r3.u32 == 0);
    CHECK(rex::memory::load_and_swap<uint64_t>(rex::kernel::GetAddressSpace(rt).TranslateVirtual(info)) == selected);
    // The extended account record ABI must join the same existing identity.
    ctx.r3.u64 = users; ctx.r4.u64 = 0; ctx.r5.u64 = 0;
    Call(fixture,ctx,rt.GetMembase().GetBase(),__imp__XamUserLogonEx);
    REQUIRE(ctx.r3.u32 == 0);
    CHECK(profile->GetXUID() == selected);
    CHECK(profile->local_profiles().size() == count);
    memory->SystemHeapFree(info); memory->SystemHeapFree(users);
  }
  {
    rex::test::KernelFixture fixture({}, {}, {}, {}, {}, copy);
    auto* profile = (&rex::xam::CurrentXAM().GetUserProfile());
    CHECK(profile->GetXUID() == selected);
    CHECK(profile->GetSigninState() == 1);
    CHECK(profile->local_profiles().size() == count);
  }
}
