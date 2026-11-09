// Profile avatar metadata (setting 0x63E80044) through the real XamAvatar ABI of
// titles/common/avatar/avatar_profile_manifest.cpp and the SDK profile storage.
#include <catch2/catch_test_macros.hpp>
#include <rex/kernel/init.h>
#include <rex/ppc/func.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>

#include <chrono>
#include <cstring>
#include <filesystem>

extern "C" REX_FUNC(__imp__XamAvatarGetManifestLocalUser);
extern "C" REX_FUNC(__imp__XamAvatarSetManifest);

namespace {
struct ManifestFixture {
  std::filesystem::path root = std::filesystem::current_path() /
      ("x360n-avatar-manifest-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::unique_ptr<rex::Runtime> runtime;
  uint32_t block = 0;
  ManifestFixture() { std::filesystem::create_directory(root); Open(); }
  ~ManifestFixture() { runtime.reset(); std::error_code ec; std::filesystem::remove_all(root, ec); }
  void Open() {
    runtime.reset();
    runtime = std::make_unique<rex::Runtime>(root, root);
    rex::RuntimeConfig config{}; config.tool_mode = true; config.kernel_init = rex::kernel::InitializeKernel;
    REQUIRE(runtime->Setup(std::move(config)) == 0);
    block = runtime->memory()->SystemHeapAlloc(4096); REQUIRE(block);
    std::memset(base() + block, 0, 4096);
  }
  uint8_t* base() { return runtime->memory()->virtual_membase(); }
  auto* profile() { return runtime->kernel_state()->user_profile(); }
  uint32_t Get(uint32_t user) {
    PPCContext ctx{}; ctx.r3.u64 = user; ctx.r4.u64 = block + 2048;
    __imp__XamAvatarGetManifestLocalUser(ctx, base()); return ctx.r3.u32;
  }
  uint32_t Set(uint32_t user) {
    PPCContext ctx{}; ctx.r3.u64 = user; ctx.r4.u64 = block;
    __imp__XamAvatarSetManifest(ctx, base()); return ctx.r3.u32;
  }
};
}  // namespace

TEST_CASE("Profile avatar metadata persists in the signed-in profile", "[sdk][avatar-profile]") {
  ManifestFixture f;
  CHECK(f.Get(0) == 0x80070525);  // no signed-in user
  REQUIRE(f.profile()->CreateLocal("Avatar") == 0);
  REQUIRE(f.profile()->SetLocalSession(f.profile()->xuid()) == 0);
  const auto xuid = f.profile()->xuid();
  CHECK(f.Get(0) == 0x80070490);  // no saved avatar; nothing is invented
  CHECK(f.Get(1) == 0x80070525);
  CHECK(f.Set(1) == 0x80070525);
  for (uint32_t i = 0; i < 1000; ++i) f.base()[f.block + i] = uint8_t(i * 7 + 3);
  REQUIRE(f.Set(0) == 0);
  REQUIRE(f.Get(0) == 0);
  CHECK(std::memcmp(f.base() + f.block, f.base() + f.block + 2048, 1000) == 0);
  const auto stored = f.root / fmt::format("{:016X}", xuid) / "FFFE07D1" / "00010000" /
      fmt::format("{:016X}", xuid) / "settings-v1" / "63E80044.dat";
  CHECK(std::filesystem::exists(stored));

  f.Open();
  REQUIRE(f.profile()->SetLocalSession(xuid) == 0);
  for (uint32_t i = 0; i < 1000; ++i) f.base()[f.block + i] = uint8_t(i * 7 + 3);
  REQUIRE(f.Get(0) == 0);
  CHECK(std::memcmp(f.base() + f.block, f.base() + f.block + 2048, 1000) == 0);
}
