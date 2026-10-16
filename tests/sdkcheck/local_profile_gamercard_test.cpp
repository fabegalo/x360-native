#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <array>
#include <cstring>
#include <vector>
#include <rex/kernel/init.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xthread.h>

extern "C" void __imp__XamUserReadProfileSettingsEx(PPCContext&, uint8_t*);
extern "C" void __imp__XamUserReadProfileSettings(PPCContext&, uint8_t*);
extern "C" void __imp__XamReadTileEx(PPCContext&, uint8_t*);
extern "C" void __imp__XamUserIsGuest(PPCContext&, uint8_t*);
extern "C" void __imp__XamUserGetCachedUserFlags(PPCContext&, uint8_t*);

using rex::X_RESULT;
using rex::X_STATUS;

namespace {
struct GamercardStore {
  std::filesystem::path path = std::filesystem::current_path() /
      ("x360n-gamercard-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  GamercardStore() { std::filesystem::create_directories(path); }
  ~GamercardStore() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};
void SetupGamercard(rex::Runtime& rt) {
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == 0);
}
constexpr uint32_t kTitlesPlayed = 0x10040012;
constexpr uint32_t kCred = 0x10040006;
constexpr uint32_t kAchievements = 0x10040013;
}  // namespace

TEST_CASE("Profile settings sizing works before sign-in without reading account data",
          "[sdk][local-profile][profile-sizing]") {
  GamercardStore store;
  rex::Runtime rt(store.path, store.path);
  SetupGamercard(rt);
  auto* profile = rt.kernel_state()->user_profile();
  REQUIRE(profile->signin_state() == 0);
  auto* memory = rt.memory();
  auto* base = memory->virtual_membase();
  const auto block = memory->SystemHeapAlloc(4096);
  REQUIRE(block);
  const uint32_t ids = block, size = block + 16, buffer = block + 64;
  rex::memory::store_and_swap<uint32_t>(base + ids, kCred);
  auto read = [&](bool ex, uint32_t user, uint32_t capacity, uint32_t output) {
    rex::memory::store_and_swap<uint32_t>(base + size, capacity);
    PPCContext ctx{};
    ctx.r1.u64 = block + 2048;
    ctx.r3.u64 = 0xFFFE07D1; ctx.r4.u64 = user;
    ctx.r7.u64 = 1; ctx.r8.u64 = ids;
    if (ex) {
      ctx.r10.u64 = size;
      rex::memory::store_and_swap<uint32_t>(base + ctx.r1.u32 + 0x54, output);
      rex::memory::store_and_swap<uint32_t>(base + ctx.r1.u32 + 0x5C, 0);
      __imp__XamUserReadProfileSettingsEx(ctx, base);
    } else {
      ctx.r9.u64 = size; ctx.r10.u64 = output;
      rex::memory::store_and_swap<uint32_t>(base + ctx.r1.u32 + 0x54, 0);
      __imp__XamUserReadProfileSettings(ctx, base);
    }
    return ctx.r3.u32;
  };
  for (bool ex : {false, true}) {
    for (uint32_t user : {0u, 255u}) {
      CHECK(read(ex, user, 0, 0) == X_ERROR_INSUFFICIENT_BUFFER);
      CHECK(rex::memory::load_and_swap<uint32_t>(base + size) == 48);
      std::memset(base + buffer, 0xA5, 48);
      CHECK(read(ex, user, 48, buffer) == X_ERROR_NOT_LOGGED_ON);
      CHECK(base[buffer] == 0xA5);
      CHECK(base[buffer + 47] == 0xA5);
    }
    CHECK(read(ex, 0, 48, 0) == X_ERROR_INVALID_PARAMETER);
  }
  uint64_t xuid = 0;
  REQUIRE(profile->CreateLocal("Sizing", &xuid) == 0);
  REQUIRE(profile->SetLocalSession(xuid) == 0);
  for (bool ex : {false, true}) {
    CHECK(read(ex, 255, 0, 0) == X_ERROR_INSUFFICIENT_BUFFER);
    CHECK(read(ex, 0, 48, buffer) == 0);
    CHECK(rex::memory::load_and_swap<uint32_t>(base + buffer) == 1);
  }
  memory->SystemHeapFree(block);
}

TEST_CASE("Gamercard statistics follow the signed-in local account and its played titles",
          "[sdk][local-profile][gamercard]") {
  GamercardStore store;
  uint64_t first = 0, second = 0;
  auto read_settings = [](rex::Runtime& rt, bool ex) {
    auto* memory = rt.memory();
    auto* base = memory->virtual_membase();
    const auto block = memory->SystemHeapAlloc(4096);
    const auto stack = memory->SystemHeapAlloc(512);
    REQUIRE(block);
    REQUIRE(stack);
    const uint32_t ids = block, size = block + 16, buffer = block + 64;
    for (uint32_t i = 0; i < 3; ++i)
      rex::memory::store_and_swap<uint32_t>(base + ids + i * 4,
                                            i == 0 ? kTitlesPlayed : i == 1 ? kCred : kAchievements);
    rex::memory::store_and_swap<uint32_t>(base + size, 1024);
    PPCContext ctx{};
    ctx.r1.u64 = stack + 128;
    ctx.r3.u64 = 0xFFFE07D1; ctx.r4.u64 = 0; ctx.r5.u64 = 0; ctx.r6.u64 = 0;
    ctx.r7.u64 = 3; ctx.r8.u64 = ids;
    if (ex) {
      // XAM 17559 816B16A8: flags in r9, size pointer in r10, buffer and
      // overlapped as the first two stack arguments.
      ctx.r9.u64 = 0; ctx.r10.u64 = size;
      rex::memory::store_and_swap<uint32_t>(base + stack + 128 + 0x54, buffer);
      rex::memory::store_and_swap<uint32_t>(base + stack + 128 + 0x5C, 0);
      __imp__XamUserReadProfileSettingsEx(ctx, base);
    } else {
      ctx.r9.u64 = size; ctx.r10.u64 = buffer;
      rex::memory::store_and_swap<uint32_t>(base + stack + 128 + 0x54, 0);
      __imp__XamUserReadProfileSettings(ctx, base);
    }
    REQUIRE(ctx.r3.u32 == 0);
    std::array<int32_t, 3> values{};
    // X_USER_READ_PROFILE_SETTINGS header (8 bytes), then 40-byte settings
    // with the setting ID at +16 and the data value at +32.
    for (uint32_t i = 0; i < 3; ++i) {
      const uint32_t setting = buffer + 8 + i * 40;
      CHECK(rex::memory::load_and_swap<uint32_t>(base + setting + 16) ==
            (i == 0 ? kTitlesPlayed : i == 1 ? kCred : kAchievements));
      values[i] = rex::memory::load_and_swap<int32_t>(base + setting + 32);
    }
    memory->SystemHeapFree(stack);
    memory->SystemHeapFree(block);
    return values;
  };
  {
    rex::Runtime rt(store.path, store.path);
    SetupGamercard(rt);
    auto* p = rt.kernel_state()->user_profile();
    REQUIRE(p->CreateLocal("Alpha", &first) == 0);
    REQUIRE(p->CreateLocal("Bravo", &second) == 0);
    REQUIRE(p->SetLocalSession(first) == 0);
    CHECK(read_settings(rt, true) == std::array<int32_t, 3>{0, 0, 0});
    CHECK_FALSE(p->RecordTitlePlayed(0xFFFE07D1));  // the dashboard is not a game
    CHECK(p->RecordTitlePlayed(0x4D5309C9));
    CHECK(p->RecordTitlePlayed(0x58410A5D));
    CHECK(p->RecordTitlePlayed(0x4D5309C9));  // once per title
    CHECK(read_settings(rt, true) == std::array<int32_t, 3>{2, 0, 0});
    CHECK(read_settings(rt, false) == std::array<int32_t, 3>{2, 0, 0});
    REQUIRE(p->SetLocalSession(second) == 0);
    CHECK(read_settings(rt, true) == std::array<int32_t, 3>{0, 0, 0});
    REQUIRE(p->SetLocalSession(0) == 0);
    CHECK_FALSE(p->RecordTitlePlayed(0x4D5309C9));  // nobody signed in
    REQUIRE(p->SetLocalSession(first) == 0);
  }
  {
    rex::Runtime rt(store.path, store.path);
    SetupGamercard(rt);
    auto* p = rt.kernel_state()->user_profile();
    CHECK(p->xuid() == first);
    CHECK(p->TitlesPlayed(first) == std::vector<uint32_t>{0x4D5309C9, 0x58410A5D});
    CHECK(p->TitlesPlayed(second).empty());
    CHECK(read_settings(rt, true) == std::array<int32_t, 3>{2, 0, 0});
  }
}

TEST_CASE("Local account tiles are absent and complete through the overlapped",
          "[sdk][local-profile][gamercard]") {
  GamercardStore store;
  rex::Runtime rt(store.path, store.path);
  SetupGamercard(rt);
  auto* memory = rt.memory();
  auto* base = memory->virtual_membase();
  const auto block = memory->SystemHeapAlloc(4096);
  REQUIRE(block);
  const uint32_t stack = block + 1024, overlapped = block + 64, size = block + 16;
  PPCContext ctx{};
  auto read_tile = [&](uint32_t overlapped_ptr) {
    // Overlapped completion records the calling guest thread.
    uint32_t result = ~0u;
    auto thread = rex::system::object_ref<rex::system::XHostThread>(
        new rex::system::XHostThread(rt.kernel_state(), 128 * 1024, 0, [&]() {
      PPCContext call{};
      call.r1.u64 = stack;
      call.r3.u64 = 2; call.r4.u64 = 0xFFFE07D1; call.r5.u64 = 0; call.r6.u64 = 0;
      call.r7.u64 = 0; call.r8.u64 = 0; call.r9.u64 = block + 256; call.r10.u64 = size;
      rex::memory::store_and_swap<uint32_t>(base + size, 64);
      rex::memory::store_and_swap<uint32_t>(base + stack + 0x54, overlapped_ptr);
      __imp__XamReadTileEx(call, base);
      result = call.r3.u32;
      return 0;
    }));
    REQUIRE(thread->Create() == X_STATUS_SUCCESS);
    uint64_t timeout = uint64_t(-50'000'000ll);
    REQUIRE(thread->Wait(0, 0, 0, &timeout) == X_STATUS_SUCCESS);
    return result;
  };
  CHECK(read_tile(0) == X_ERROR_NOT_FOUND);
  std::memset(base + overlapped, 0, 28);
  CHECK(read_tile(overlapped) == X_ERROR_IO_PENDING);
  CHECK(rex::memory::load_and_swap<uint32_t>(base + overlapped) == X_ERROR_NOT_FOUND);
  for (uint32_t user = 0; user < 4; ++user) {
    ctx.r3.u64 = user;
    __imp__XamUserIsGuest(ctx, base);
    CHECK(ctx.r3.u32 == 0);
    ctx.r3.u64 = user;
    __imp__XamUserGetCachedUserFlags(ctx, base);
    CHECK(ctx.r3.u32 == 0);
  }
  memory->SystemHeapFree(block);
}
