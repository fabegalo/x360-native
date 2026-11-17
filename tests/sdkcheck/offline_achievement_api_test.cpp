#include <catch2/catch_test_macros.hpp>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include "kernel/title_fixture.h"
#include "kernel/test_access.h"
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/object/object_table.h>
#include <rex/kernel/object/event_object.h>
#include <rex/kernel/thread_registry.h>
#include <rex/kernel/thread_state.h>
#include <rex/kernel/function_dispatcher.h>
#include <rex/kernel/types/io.h>
#include <rex/xam/system_ui.h>
#include "xam/xam_state.h"
#include "offline_achievement_fixture.h"
#include "synthetic_xex.h"

#define IMPORT(name) extern "C" void __imp__##name(PPCContext&, uint8_t*)
IMPORT(XMsgInProcessCall); IMPORT(XMsgStartIORequest); IMPORT(XMsgStartIORequestEx);
IMPORT(XamUserCreateAchievementEnumerator); IMPORT(XamEnumerate);
IMPORT(XamUserCreateTitlesPlayedEnumerator);
IMPORT(XamUserReadProfileSettings); IMPORT(XamUserReadProfileSettingsEx);
IMPORT(XamReadTile); IMPORT(XamReadTileEx); IMPORT(XamReadTileToTexture);
IMPORT(NtClose);

using namespace rex;
using namespace rex::kernel;
using namespace rex::xam;
namespace {
using LedgerResult = OfflineAchievementResult;
constexpr uint32_t kTitleA = 0x1234C901, kTitleB = 0x1234C902;
struct TestDirectory {
  std::filesystem::path path = std::filesystem::current_path() /
      ("x360n-achievement-api-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  TestDirectory() { std::filesystem::create_directories(path); }
  ~TestDirectory() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};

struct AchievementGuest {
  rex::test::KernelHost host;
  TitleRuntime runtime{rex::ppc::PPCImage{}};
  std::filesystem::path root;
  uint32_t block = 0;
  uint32_t last_error = 0;
  uint8_t* base = nullptr;
  explicit AchievementGuest(const std::filesystem::path& path, uint32_t title = kTitleA)
      : host((rex::test::ReleaseTestMemory(),path)), root(path) {
    SelectTitle(title);
    block = runtime.GetGuestHeap().SystemHeapAlloc(16384);
    REQUIRE(block);
    base = runtime.GetMembase().GetBase();
    std::memset(base + block, 0, 16384);
  }
  ~AchievementGuest() { runtime.GetGuestHeap().SystemHeapFree(block); runtime.Unload();
    CHECK(runtime.GetTitlePaths().entry.Reset()); CHECK(runtime.GetTitlePaths().imports.Reset());
    CHECK(runtime.GetTitlePaths().game_root.Reset()); }
  void SelectTitle(uint32_t title) {
    auto image = x360n::test::BuildXex({}, nullptr, 0, x360n::test::Entry::kReturnZero);
    memory::store_and_swap<uint32_t>(image.data() + 0x14, 4);
    memory::store_and_swap<uint32_t>(image.data() + 0x30, 0x00040006);
    memory::store_and_swap<uint32_t>(image.data() + 0x34, 0x60);
    memory::store_and_swap<uint32_t>(image.data() + 0x6C, title);
    { std::ofstream out(root / "default.xex", std::ios::binary);
      out.write(reinterpret_cast<const char*>(image.data()), image.size()); }
    REQUIRE(runtime.GetTitlePaths().game_root.Set(root));
    REQUIRE(runtime.GetTitlePaths().entry.Set("default.xex")); REQUIRE(runtime.GetTitlePaths().imports.Set(""));
    REQUIRE(runtime.Load()); REQUIRE(runtime.GetTitleID()==title);
    REQUIRE(GetFunctionDispatcher(runtime).InitializeFunctionTable(0x8F000000,0x10000,0x8F000000,0x100000,true));
    StartDPCQueue(runtime);
  }
  uint32_t Call(void (*function)(PPCContext&, uint8_t*), std::initializer_list<uint64_t> args) {
    uint32_t result = ~0u;
    auto thread = runtime.GetThreadRegistry().LaunchHostThread("achievement contract",128*1024,[&] {
      auto& ctx = *KernelThread::GetCallingThread()->GetThreadState()->GetContext();
      ctx.r1.u64 = block + 14336;  // Preserve the 16-byte PPC stack alignment.
      PPCRegister* regs[] = {&ctx.r3, &ctx.r4, &ctx.r5, &ctx.r6, &ctx.r7, &ctx.r8, &ctx.r9, &ctx.r10};
      unsigned i = 0;
      for (const auto arg : args) {
        if (i < 8) regs[i]->u64 = arg;
        else memory::store_and_swap<uint32_t>(base + ctx.r1.u32 + 0x54 + (i - 8) * 8, uint32_t(arg));
        ++i;
      }
      function(ctx, base);
      result = ctx.r3.u32;
      last_error = KernelThread::GetCallingThread()->GetLastError();
      return 0;
    });
    REQUIRE(thread);
    uint64_t timeout = uint64_t(-50'000'000ll);
    REQUIRE(runtime.GetThreadRegistry().GetObjectWaiter().WaitGuestObject(*thread,0,0,0,&timeout)==0);
    CloseOwned(runtime.GetObjectTable(),*thread);
    return result;
  }
  void Put(uint32_t offset, uint32_t value) { memory::store_and_swap<uint32_t>(base + block + offset, value); }
  uint32_t Word(uint32_t offset) const { return memory::load_and_swap<uint32_t>(base + block + offset); }
  uint32_t Write(std::initializer_list<std::pair<uint32_t, uint32_t>> entries, bool async = false,
                 uint32_t length = 8) {
    Put(0, static_cast<uint32_t>(entries.size())); Put(4, block + 64);
    unsigned i = 0;
    for (auto [user, id] : entries) { Put(64 + i * 8, user); Put(68 + i * 8, id); ++i; }
    if (async) {
      std::memset(base + block + 256, 0, 32);
      return Call(__imp__XMsgStartIORequest, {0xFB, 0xB0008, block + 256, block, length});
    }
    return Call(__imp__XMsgInProcessCall, {0xFB, 0xB0008, block, length});
  }
  uint32_t Create(uint32_t title, uint64_t xuid = 0, uint32_t offset = 0, uint32_t count = 1,
                  uint32_t flags = 7, uint32_t user = 0) {
    return Call(__imp__XamUserCreateAchievementEnumerator,
                {title, user, xuid, flags, offset, count, block + 300, block + 304});
  }
  uint32_t Enumerate(uint32_t handle, uint32_t size, bool async = false, uint32_t flags = 0) {
    if (!async) return Call(__imp__XamEnumerate, {handle, flags, block + 1024, size, block + 308, 0});
    std::memset(base + block + 256, 0, 32);
    const auto result = Call(__imp__XamEnumerate, {handle, flags, block + 1024, size, 0, block + 256});
    REQUIRE(result == X_ERROR_IO_PENDING);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (Word(256) == X_ERROR_IO_PENDING && std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(Word(256) != X_ERROR_IO_PENDING);
    return Word(256);
  }
  std::array<uint32_t, 4> ReadCounters(uint32_t title, uint64_t xuid = 0, bool ex = false,
                                      bool async = false) {
    for (unsigned i = 0; i < 4; ++i) Put(512 + i * 4, std::array<uint32_t, 4>{
        0x10040006, 0x10040013, 0x10040038, 0x10040039}[i]);
    memory::store_and_swap<uint64_t>(base + block + 544, xuid);
    Put(540, 512);
    uint32_t result;
    if (ex) result = Call(__imp__XamUserReadProfileSettingsEx,
        {title, 0, xuid ? 1u : 0u, xuid ? block + 544 : 0u, 4, block + 512, 0,
         block + 540, block + 2048, async ? block + 256 : 0u});
    else result = Call(__imp__XamUserReadProfileSettings,
        {title, 0, xuid ? 1u : 0u, xuid ? block + 544 : 0u, 4, block + 512,
         block + 540, block + 2048, async ? block + 256 : 0u});
    REQUIRE(result == (async ? X_ERROR_IO_PENDING : X_ERROR_SUCCESS));
    if (async) CHECK(Word(256) == X_ERROR_SUCCESS);
    std::array<uint32_t, 4> counters{};
    for (unsigned i = 0; i < 4; ++i) counters[i] = Word(2048 + 8 + i * 40 + 32);
    return counters;
  }
};
}  // namespace

TEST_CASE("Offline achievement XAM writes are durable atomic scoped and asynchronous", "[sdk][offline-achievement-api]") {
  TestDirectory directory;
  uint64_t first = 0, second = 0;
  {
    AchievementGuest guest(directory.path);
    auto* state = (&rex::xam::CurrentXAM());
    REQUIRE(state->GetOfflineAchievements().ImportTitle(kTitleA, offline_achievement_fixture::Metadata()) == LedgerResult::kSuccess);
    REQUIRE(state->GetOfflineAchievements().ImportTitle(kTitleB, offline_achievement_fixture::Metadata(40)) == LedgerResult::kSuccess);
    auto* profile = (&state->GetUserProfile());
    REQUIRE(profile->CreateLocal("Alpha", &first) == 0);
    REQUIRE(profile->CreateLocal("Bravo", &second) == 0);
    CHECK(guest.Write({{0, 1}}) == X_E_NO_SUCH_USER);
    REQUIRE(profile->SetLocalSession(first) == 0);
    unsigned notifications = 0;
    auto xam = (&REX_INTERFACE(rex::xam::IXAMTitleServices)->GetSystemUI());
    xam->SetAchievementUnlockedCallback([&](uint64_t owner, uint32_t user, uint32_t title, uint32_t id) {
      CHECK(owner == first); CHECK(user == 0); CHECK(title == kTitleA); CHECK(id == 1);
      ++notifications;
    });
    CHECK(guest.Write({{0, 1}}, true) == X_E_SUCCESS);
    CHECK(guest.Word(256) == X_ERROR_SUCCESS);
    CHECK(guest.Word(280) == X_E_SUCCESS);
    CHECK(notifications == 1);
    CHECK(guest.Write({{0, 1}, {0, 1}}, false, 0) == X_E_SUCCESS);
    CHECK(notifications == 1);
    CHECK(guest.ReadCounters(kTitleA, 0, true, true) == std::array<uint32_t, 4>{10, 1, 10, 1});
    CHECK(guest.Write({{0, 2}, {0, 999}}) == X_E_NOTFOUND);
    CHECK(guest.ReadCounters(kTitleA) == std::array<uint32_t, 4>{10, 1, 10, 1});
    CHECK(guest.Write({{1, 2}}) == X_E_NO_SUCH_USER);
    CHECK(guest.Write({{4, 2}}) == X_E_INVALIDARG);
    CHECK(guest.Write({}) == X_E_INVALIDARG);
    CHECK(guest.Write({{0, 2}}, false, 7) == X_E_INVALIDARG);
    CHECK(guest.Write({{0, 999}}, true) == X_E_SUCCESS);
    CHECK(guest.Word(256) == X_ERROR_FUNCTION_FAILED);
    CHECK(guest.Word(280) == X_E_NOTFOUND);
    // The original XUser wrapper converts accepted synchronous completion
    // using the sign of the thread's extended HRESULT (0 or 1627).
    CHECK(guest.Call(__imp__XMsgStartIORequest,
        {0xFB, 0xB0008, 0, guest.block, 8}) == X_E_SUCCESS);
    CHECK(guest.last_error == X_E_NOTFOUND);
    guest.Put(0, 2); guest.Put(4, 0xFFFFFFFC);
    CHECK(guest.Call(__imp__XMsgInProcessCall, {0xFB, 0xB0008, guest.block, 8}) == X_E_INVALIDARG);
    CHECK(guest.Call(__imp__XMsgInProcessCall, {0xFB, 0xB0008, 0, 8}) == X_E_INVALIDARG);
    CHECK(guest.Call(__imp__XMsgStartIORequest,
        {0xFB, 0xB0008, 0xFFFFFFFC, guest.block, 8}) == X_E_INVALIDARG);
    auto event=guest.runtime.GetObjectTable().Emplace<EventObject>(AddOptions{},false,false);
    REQUIRE(event);
    rex::ScopeGuard close_event([&] {CloseOwned(guest.runtime.GetObjectTable(),*event);});
    guest.Put(0, 1); guest.Put(4, guest.block + 64); guest.Put(64, 0); guest.Put(68, 1);
    std::memset(guest.base + guest.block + 256, 0, 32);
    guest.Put(268, *event);
    CHECK(guest.Call(__imp__XMsgStartIORequest,
        {0xFB, 0xB0008, guest.block + 256, guest.block, 8}) == X_E_SUCCESS);
    uint64_t timeout = 0;
    CHECK(guest.runtime.GetThreadRegistry().GetObjectWaiter().WaitGuestObject(*event,0,0,0,&timeout) == X_STATUS_SUCCESS);
    CHECK(guest.Word(256) == 0);
    CHECK(guest.Call(__imp__XMsgStartIORequestEx,
        {0xFB, 0xB0008, 0, guest.block, 8, 0}) == X_E_SUCCESS);
    CHECK(guest.last_error == X_E_SUCCESS);
    CHECK(notifications == 1);
    xam->SetAchievementUnlockedCallback({});
    REQUIRE(profile->SetLocalSession(second) == 0);
    CHECK(guest.ReadCounters(kTitleA) == std::array<uint32_t, 4>{0, 0, 0, 0});
    CHECK(guest.Write({{0, 2}}) == X_E_SUCCESS);
    CHECK(guest.ReadCounters(kTitleA) == std::array<uint32_t, 4>{25, 1, 25, 1});
    CHECK(guest.ReadCounters(kTitleA, first) == std::array<uint32_t, 4>{10, 1, 10, 1});
  }
  {
    AchievementGuest guest(directory.path, kTitleB);
    REQUIRE((&rex::xam::CurrentXAM().GetUserProfile())->SetLocalSession(first) == 0);
    CHECK(guest.Write({{0, 1}}) == X_E_SUCCESS);
    CHECK(guest.ReadCounters(kTitleB) == std::array<uint32_t, 4>{50, 2, 40, 1});
    CHECK(guest.ReadCounters(kTitleA, first, true) == std::array<uint32_t, 4>{50, 2, 10, 1});
    CHECK(guest.ReadCounters(kTitleB, second) == std::array<uint32_t, 4>{25, 1, 0, 0});
  }
  {
    AchievementGuest guest(directory.path);
    CHECK(guest.ReadCounters(kTitleA) == std::array<uint32_t, 4>{50, 2, 10, 1});
    CHECK(guest.ReadCounters(kTitleB, second) == std::array<uint32_t, 4>{25, 1, 0, 0});
  }
}

TEST_CASE("Offline achievement enumeration honors PPC XUID Title ID offsets strings and capacity", "[sdk][offline-achievement-api]") {
  TestDirectory directory;
  AchievementGuest guest(directory.path);
  auto* state = (&rex::xam::CurrentXAM());
  REQUIRE(state->GetOfflineAchievements().ImportTitle(kTitleA, offline_achievement_fixture::Metadata()) == LedgerResult::kSuccess);
  REQUIRE(state->GetOfflineAchievements().ImportTitle(kTitleB, offline_achievement_fixture::Metadata(40)) == LedgerResult::kSuccess);
  uint64_t first = 0, second = 0;
  REQUIRE((&state->GetUserProfile())->CreateLocal("Alpha", &first) == 0);
  REQUIRE((&state->GetUserProfile())->CreateLocal("Bravo", &second) == 0);
  REQUIRE((&state->GetUserProfile())->SetLocalSession(first) == 0);
  REQUIRE(guest.Write({{0, 1}}) == X_E_SUCCESS);
  REQUIRE(guest.Create(kTitleA, first) == X_ERROR_SUCCESS);
  CHECK(guest.Word(300) == 500);
  const auto handle = guest.Word(304);
  std::memset(guest.base + guest.block + 1024, 0xA5, 501);
  CHECK(guest.Call(__imp__XamEnumerate,
      {handle, 0, guest.block + 1024, 500, 0xFFFFFFFC, 0}) == X_ERROR_INVALID_PARAMETER);
  CHECK(guest.Call(__imp__XamEnumerate,
      {handle, 1, guest.block + 1024, 500, guest.block + 308, 0}) == X_ERROR_INVALID_PARAMETER);
  CHECK(guest.base[guest.block + 1024] == 0xA5);
  CHECK(guest.Enumerate(handle, 499) == X_ERROR_INSUFFICIENT_BUFFER);
  CHECK(guest.base[guest.block + 1024] == 0xA5);
  CHECK(guest.Enumerate(handle, 500) == X_ERROR_SUCCESS);
  CHECK(guest.Word(308) == 1); CHECK(guest.Word(1024) == 1);
  CHECK(guest.Word(1044) == 10); CHECK((guest.Word(1056) & 0x30000) == 0x20000);
  CHECK((guest.Word(1048) || guest.Word(1052)));
  CHECK(memory::load_and_swap<uint16_t>(guest.base + guest.Word(1028)) == u'F');
  CHECK(guest.base[guest.block + 1524] == 0xA5);
  CHECK(guest.Enumerate(handle, 500, true) == X_ERROR_SUCCESS);
  CHECK(guest.Word(1024) == 2); CHECK((guest.Word(1056) & 0x30000) == 0);
  CHECK(guest.Enumerate(handle, 500) == X_ERROR_NO_MORE_FILES);
  CHECK(guest.Word(308) == 0);
  CHECK(guest.Call(__imp__NtClose, {handle}) == 0);
  CHECK(guest.Call(__imp__XamEnumerate,
      {handle, 0, guest.block + 1024, 500, 0xFFFFFFFC, 0}) == X_ERROR_INVALID_HANDLE);
  REQUIRE(guest.Create(kTitleB, first, 0, 1, 0) == 0);
  CHECK(guest.Word(300) == 36);
  REQUIRE(guest.Enumerate(guest.Word(304), 36) == 0);
  CHECK(guest.Word(1044) == 40); CHECK(guest.Word(1028) == 0);
  CHECK((guest.Word(1056) & 0x30000) == 0);
  REQUIRE(guest.Create(kTitleA, second) == 0);
  REQUIRE(guest.Enumerate(guest.Word(304), 500) == 0);
  CHECK((guest.Word(1056) & 0x30000) == 0);
  REQUIRE(guest.Create(0, first, 1) == 0);
  REQUIRE(guest.Enumerate(guest.Word(304), 500) == 0);
  CHECK(guest.Word(1024) == 2);
  REQUIRE(guest.Create(kTitleA, first, UINT32_MAX) == 0);
  CHECK(guest.Enumerate(guest.Word(304), 500) == X_ERROR_NO_MORE_FILES);
  CHECK(guest.Create(kTitleA, first ^ 0x100000000ull) == X_ERROR_NO_SUCH_USER);
  CHECK(guest.Create(0x12349999, first) == X_ERROR_NOT_FOUND);
  CHECK(guest.Create(kTitleA, first, 0, UINT32_MAX) == X_ERROR_INVALID_PARAMETER);
  // Original 17559 81670190 validates user, count and output pointers, never
  // the flags: only flags & 7 select the 500-byte string records. GamerProfile's
  // LoadAchievementsScene (90117710) requests flags 0x27.
  REQUIRE(guest.Create(kTitleA, first, 0, 2, 0x27) == X_ERROR_SUCCESS);
  CHECK(guest.Word(300) == 1000);
  REQUIRE(guest.Enumerate(guest.Word(304), 1000) == X_ERROR_SUCCESS);
  CHECK(guest.Word(308) == 2); CHECK(guest.Word(1024) == 1);
  CHECK(memory::load_and_swap<uint16_t>(guest.base + guest.Word(1028)) == u'F');
  REQUIRE(guest.Create(kTitleA, first, 0, 1, 0x20) == X_ERROR_SUCCESS);
  CHECK(guest.Word(300) == 36);
  REQUIRE(guest.Enumerate(guest.Word(304), 36) == X_ERROR_SUCCESS);
  CHECK(guest.Word(1024) == 1); CHECK(guest.Word(1028) == 0);
  CHECK(guest.Create(kTitleA, first, 0, 1, 7, 4) == X_ERROR_INVALID_PARAMETER);
  REQUIRE((&state->GetUserProfile())->RecordTitlePlayed(kTitleA));
  REQUIRE((&state->GetUserProfile())->RecordTitlePlayed(kTitleB));
  REQUIRE(guest.Call(__imp__XamUserCreateTitlesPlayedEnumerator,
      {0, 0, first, 0, 2, guest.block + 300, guest.block + 304}) == 0);
  CHECK(guest.Word(300) == 336);
  REQUIRE(guest.Enumerate(guest.Word(304), 336) == 0);
  CHECK(guest.Word(308) == 2);
  CHECK(guest.Word(1024) == kTitleA); CHECK(guest.Word(1028) == 2);
  CHECK(guest.Word(1032) == 1); CHECK(guest.Word(1036) == 35); CHECK(guest.Word(1040) == 10);
  CHECK(guest.Word(1192) == kTitleB); CHECK(guest.Word(1204) == 65); CHECK(guest.Word(1208) == 0);
}

TEST_CASE("Original direct enumeration flag preserves title and achievement pages", "[sdk][offline-achievement-api][enumerate-direct]") {
  TestDirectory directory; AchievementGuest guest(directory.path);
  auto* state = (&rex::xam::CurrentXAM());
  REQUIRE(state->GetOfflineAchievements().ImportTitle(kTitleA, offline_achievement_fixture::Metadata()) == LedgerResult::kSuccess);
  uint64_t xuid = 0;
  REQUIRE((&state->GetUserProfile())->CreateLocal("Direct", &xuid) == 0);
  REQUIRE((&state->GetUserProfile())->SetLocalSession(xuid) == 0);
  REQUIRE((&state->GetUserProfile())->RecordTitlePlayed(kTitleA));
  for (const bool titles : {false,true}) {
    if (titles) REQUIRE(guest.Call(__imp__XamUserCreateTitlesPlayedEnumerator,
        {0,0,xuid,0,1,guest.block+300,guest.block+304}) == 0);
    else REQUIRE(guest.Create(kTitleA,xuid,0,1,0x27) == 0);
    const auto handle=guest.Word(304), size=guest.Word(300);
    // Invalid output/capacity/flags must not consume a page.
    CHECK(guest.Call(__imp__XamEnumerate,{handle,2,guest.block+1024,size,0xFFFFFFFC,0})==87);
    CHECK(guest.Enumerate(handle,size-1,false,2)==122);
    CHECK(guest.Enumerate(handle,size,false,1)==87);
    REQUIRE(guest.Enumerate(handle,size,false,2)==0);
    CHECK(guest.Word(308)==1);CHECK(guest.Word(1024)==(titles?kTitleA:1));
    if (!titles) {
      std::memset(guest.base+guest.block+256,0,32);
      REQUIRE(guest.Call(__imp__XamEnumerate,{handle,2,guest.block+1024,size,0,guest.block+256})==997);
      // Bit 1 dispatches directly, completing before the export returns.
      CHECK(guest.Word(256)==0);CHECK(guest.Word(260)==1);CHECK(guest.Word(1024)==2);
    }
    CHECK(guest.Enumerate(handle,size,false,2)==X_ERROR_NO_MORE_FILES);
    CHECK(guest.Word(308)==0);
    CHECK(guest.Call(__imp__NtClose,{handle})==0);
  }
}

TEST_CASE("Offline achievement tile APIs preserve encoded resources and decode real pixels", "[sdk][offline-achievement-api]") {
  TestDirectory directory;
  AchievementGuest guest(directory.path);
  // A valid 1x1 RGBA PNG (11 22 33 FF), with a stored zlib block and
  // checksummed chunks. Synthetic fixture only; the runtime decoder supports PNG.
  const std::vector<uint8_t> png{
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
      0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
      0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
      0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89,
      0x00, 0x00, 0x00, 0x10, 0x49, 0x44, 0x41, 0x54,
      0x78, 0x01, 0x01, 0x05, 0x00, 0xfa, 0xff,
      0x00, 0x11, 0x22, 0x33, 0xff, 0x02, 0x14, 0x01, 0x66,
      0xb9, 0xe1, 0xfa, 0xc1, 0x00, 0x00, 0x00, 0x00,
      0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
  auto* state = (&rex::xam::CurrentXAM());
  REQUIRE(state->GetOfflineAchievements().ImportTitle(kTitleA,
      offline_achievement_fixture::Metadata(10, 2, png)) == LedgerResult::kSuccess);
  uint64_t xuid = 0;
  REQUIRE((&state->GetUserProfile())->CreateLocal("Alpha", &xuid) == 0);
  REQUIRE((&state->GetUserProfile())->SetLocalSession(xuid) == 0);
  guest.Put(300, 0);
  CHECK(guest.Call(__imp__XamReadTile, {0, kTitleA, 100, 0, 0, guest.block + 300, 0}) == X_ERROR_INSUFFICIENT_BUFFER);
  CHECK(guest.Word(300) == png.size());
  guest.Put(300, 100);
  CHECK(guest.Call(__imp__XamReadTileEx,
      {0, kTitleA, 100, 0, 0, 0, guest.block + 1024, guest.block + 300, guest.block + 256}) == X_ERROR_IO_PENDING);
  CHECK(guest.Word(256) == X_ERROR_SUCCESS);
  CHECK(std::memcmp(guest.base + guest.block + 1024, png.data(), png.size()) == 0);
  CHECK(guest.Call(__imp__XamReadTileToTexture,
      {0, kTitleA, 100, 0, guest.block + 1024, 8, 1, 0}) == 0);
  CHECK(guest.Word(1024) == 0xFF112233); CHECK(guest.Word(1028) == 0);
  CHECK(guest.Call(__imp__XamReadTileToTexture,
      {0, kTitleA, 100, 0, guest.block + 1024, 3, 1, 0}) == X_ERROR_INSUFFICIENT_BUFFER);
  CHECK(guest.Call(__imp__XamReadTile, {0, kTitleB, 100, 0, 0, guest.block + 300, 0}) == X_ERROR_NOT_FOUND);
  CHECK(guest.Call(__imp__XamReadTile, {2, kTitleA, 100, 0, 0, guest.block + 300, 0}) == X_ERROR_NOT_FOUND);
  CHECK(guest.Call(__imp__XamReadTile,
      {0, kTitleA, 0x100000064ull, 0, 0, guest.block + 300, 0}) == X_ERROR_INVALID_PARAMETER);
  CHECK(guest.Call(__imp__XamReadTile,
      {0, kTitleA, 100, 0, 0, guest.block + 300, 0xFFFFFFFC}) == X_ERROR_INVALID_PARAMETER);
}
