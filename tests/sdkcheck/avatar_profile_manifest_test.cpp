#include <format>
// Profile avatar metadata (setting 0x63E80044) through the real XamAvatar ABI of
// titles/common/avatar/avatar_profile_manifest.cpp and the SDK profile storage.
#include <catch2/catch_test_macros.hpp>
#include "kernel/kernel_fixture.h"
#include "temp_folder.h"
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/guest_memory.h>
#include <rex/kernel/heap.h>
#include <rex/ppc/recompiled_code.h>
#include <rex/xam/title_lifecycle.h>
#include <rex/xam/user_profile.h>
#include <chrono>
#include <cstring>
#include <filesystem>

extern "C" REX_FUNC(__imp__XamAvatarGetManifestLocalUser);
extern "C" REX_FUNC(__imp__XamAvatarSetManifest);

namespace {
struct ManifestFixture {
  rex::test::TempFolder folder{"x360n_avatar_manifest_"};
  std::filesystem::path root = folder.path;
  std::unique_ptr<rex::test::KernelFixture> fixture;
  uint32_t block = 0;
  ManifestFixture() { Open(); }
  void Open() {
    fixture.reset();
    fixture = std::make_unique<rex::test::KernelFixture>(
        std::function<void(const std::filesystem::path&)>{}, std::vector<rex::IModule*>{}, "", "", "", root);
    block = fixture->GetRuntime().GetGuestHeap().SystemHeapAlloc(4096); REQUIRE(block);
    std::memset(base() + block, 0, 4096);
  }
  uint8_t* base() {
    auto bytes = fixture->GetRuntime().GetGuestMemory().GetWritableSpan(block, 4096);
    REQUIRE(bytes);
    return reinterpret_cast<uint8_t*>(bytes->data()) - block;
  }
  auto* profile() { return &REX_INTERFACE(rex::xam::IXAMTitleServices)->GetUserProfile(); }
  uint32_t Call(bool write, uint32_t user, uint32_t pointer, uint32_t overlap = 0) {
    uint32_t result = 0;
    fixture->RunOnGuestThread([&](rex::test::Caller& caller) {
      auto& ctx = caller.GetContext(); ctx.r3.u64 = user; ctx.r4.u64 = pointer; ctx.r5.u64 = overlap;
      if (write) __imp__XamAvatarSetManifest(ctx, caller.GetBase());
      else __imp__XamAvatarGetManifestLocalUser(ctx, caller.GetBase());
      result = ctx.r3.u32;
    });
    return result;
  }
  uint32_t Get(uint32_t user) { return Call(false, user, block + 2048); }
  uint32_t Set(uint32_t user) { return Call(true, user, block); }
};
}  // namespace

TEST_CASE("Profile avatar metadata persists in the signed-in profile", "[sdk][avatar-profile]") {
  ManifestFixture f;
  CHECK(f.Get(0) == 0x80070525);  // no signed-in user
  REQUIRE(f.profile()->CreateLocal("Avatar") == 0);
  REQUIRE(f.profile()->SetLocalSession(f.profile()->GetXUID()) == 0);
  const auto xuid = f.profile()->GetXUID();
  CHECK(f.Get(0) == 0x80070490);  // no saved avatar; nothing is invented
  CHECK(f.Get(1) == 0x80070525);
  CHECK(f.Set(1) == 0x80070525);
  for (uint32_t i = 0; i < 1000; ++i) f.base()[f.block + i] = uint8_t(i * 7 + 3);
  CHECK(f.Call(true, 0, 0xFFFFFFF0) == 0x80070057);
  CHECK(f.Call(true, 0, f.block, 0xFFFFFFF0) == 0x80070057);
  CHECK(f.Call(false, 0, 0xFFFFFFF0) == 0x80070057);
  REQUIRE(f.Set(0) == 0);
  REQUIRE(f.Get(0) == 0);
  CHECK(std::memcmp(f.base() + f.block, f.base() + f.block + 2048, 1000) == 0);
  const auto stored = f.root / std::format("{:016X}", xuid) / "FFFE07D1" / "00010000" /
      std::format("{:016X}", xuid) / "settings-v1" / "63E80044.dat";
  CHECK(std::filesystem::exists(stored));

  f.Open();
  REQUIRE(f.profile()->SetLocalSession(xuid) == 0);
  for (uint32_t i = 0; i < 1000; ++i) f.base()[f.block + i] = uint8_t(i * 7 + 3);
  REQUIRE(f.Get(0) == 0);
  CHECK(std::memcmp(f.base() + f.block, f.base() + f.block + 2048, 1000) == 0);
}
