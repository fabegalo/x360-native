#include <catch2/catch_test_macros.hpp>
#include <rex/xam/user_profile.h>
#include <rex/xam/content_manager.h>
#include <rex/kernel/types/io.h>
#include <rex/kernel/thread_registry.h>
#include <rex/kernel/object/kernel_thread.h>
#include <rex/platform/process.h>
#include <rex/string/text.h>
#include "kernel/kernel_fixture.h"
#include "xam/xam_state.h"
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <thread>
#if defined(_WIN32)
#include <windows.h>
#endif
#include "picture_write_fixture.h"

extern "C" void __imp__XamWriteGamerTileEx(PPCContext&, uint8_t*);
extern "C" void __imp__XamWriteGamerTile(PPCContext&, uint8_t*);
namespace {
using namespace picture_write_fixture;
struct PictureDirectory {
  std::filesystem::path root = std::filesystem::current_path() /
      ("x360n-picture-write-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  PictureDirectory() { if (!std::filesystem::create_directory(root)) throw std::runtime_error("Picture fixture collision"); }
  ~PictureDirectory() {
    std::error_code ec;
    const auto resolved = std::filesystem::weakly_canonical(root, ec);
    if (!ec && resolved.parent_path() == std::filesystem::current_path() &&
        resolved.filename().string().starts_with("x360n-picture-write-")) std::filesystem::remove_all(resolved, ec);
  }
};
std::vector<uint8_t> ReadPictureBytes(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), {}};
}

}

TEST_CASE("Raw gamer picture writes persist distinct public and personal files per XUID",
          "[sdk][profile-picture-write]") {
  PictureDirectory directory;
  uint64_t first = 0, second = 0;
  std::filesystem::path first_package, second_package;
  {
    rex::test::KernelFixture fixture({}, {}, {}, {}, {}, directory.root);
    auto* profiles = &rex::xam::CurrentXAM().GetUserProfile();
    REQUIRE(profiles->CreateLocal("PictureA", &first) == 0);
    REQUIRE(profiles->CreateLocal("PictureB", &second) == 0);
    REQUIRE(profiles->SetLocalSession(first) == 0);
    first_package = profiles->PackagePath(first); second_package = profiles->PackagePath(second);
    const auto account_a = ReadPictureBytes(first_package / "account.dat");
    const auto account_b = ReadPictureBytes(second_package / "account.dat");
    fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
      const auto block = caller.Alloc(32768); REQUIRE(block);
      auto* base = caller.GetBase();
      auto put = [&](uint32_t address, uint32_t value) { rex::memory::store_and_swap<uint32_t>(base + address, value); };
      auto write = [&](uint32_t flags, const auto& large, const auto& small_png, uint32_t overlap = 0, uint32_t user = 0) {
        std::memcpy(base + block + 512, large.data(), large.size());
        std::memcpy(base + block + 1024, small_png.data(), small_png.size());
        PPCContext ctx{}; ctx.r1.u64 = block + 16384;
        ctx.r3.u64 = user; ctx.r4.u64 = flags; ctx.r5.u64 = 0x12345678;
        ctx.r6.u64 = 17; ctx.r7.u64 = 33; ctx.r8.u64 = 0;
        ctx.r9.u64 = block + 512; ctx.r10.u64 = block + 1024;
        put(ctx.r1.u32 + 0x54, uint32_t(large.size())); put(ctx.r1.u32 + 0x5C, uint32_t(small_png.size()));
        put(ctx.r1.u32 + 0x64, overlap);
        __imp__XamWriteGamerTileEx(ctx, base);
        return ctx.r3.u32;
      };
      const auto public_result = write(0x12, A64, A32);
      INFO("picture write HRESULT=" << caller.GetRuntime().GetThreadRegistry().GetCallingThread()->GetLastError()
           << "; " << (public_result ? PictureStageDescription(first_package) : "transaction removed"));
      CHECK(public_result == 0);
      CHECK(ReadPictureBytes(first_package / "tile_64.png") == A64);
      CHECK(ReadPictureBytes(first_package / "tile_32.png") == A32);
      CHECK_FALSE(std::filesystem::exists(first_package / "pp_64.png"));
      CHECK(write(0x22, B64, B32) == 0);
      CHECK(ReadPictureBytes(first_package / "pp_64.png") == B64);
      CHECK(ReadPictureBytes(first_package / "pp_32.png") == B32);
      CHECK(ReadPictureBytes(first_package / "tile_64.png") == A64);
      const auto key = ReadPictureBytes(first_package / "settings-v1" / "4064000F.dat");
      CHECK(key.size() == 50);
      const auto* selected = static_cast<rex::xam::UserProfile::UnicodeSetting*>(profiles->GetSetting(0x4064000F));
      REQUIRE(selected); CHECK_FALSE(selected->load_failed);
      CHECK(selected->value == u"123456780000001100000021");
      CHECK(write(0xB2, A64, A32) == 0); // Both pictures; preserve the selected key.
      CHECK(ReadPictureBytes(first_package / "settings-v1" / "4064000F.dat") == key);
      CHECK(ReadPictureBytes(first_package / "pp_64.png") == A64);
      const auto overlap = block + 4096;
      std::memset(base + overlap, 0, sizeof(rex::kernel::XOVERLAPPED));
      CHECK(write(0x32, B64, B32, overlap) == 997);
      auto* io = reinterpret_cast<rex::kernel::XOVERLAPPED*>(base + overlap);
      CHECK(io->result == 0); CHECK(io->extended_error == 0);
      CHECK(ReadPictureBytes(first_package / "tile_64.png") == B64);
      auto corrupt = A64; corrupt[40] ^= 1;
      std::memset(base + overlap, 0, sizeof(*io));
      CHECK(write(0x32, corrupt, A32, overlap) == 997);
      CHECK(io->result == 1627); CHECK(io->extended_error == 0x8007000D);
      CHECK(ReadPictureBytes(first_package / "tile_64.png") == B64);
      CHECK(ReadPictureBytes(first_package / "pp_32.png") == B32);
      CHECK(write(0x32, A64, A32, 5) == 87);
      CHECK(write(0x32, A64, A32, 0, 1) == 1627);
      PPCContext unsupported{}; unsupported.r3.u64 = 0;
      __imp__XamWriteGamerTile(unsupported, base);
      CHECK(unsupported.r3.u32 == 1627); // Unimplemented awards cannot report a stored picture.
      CHECK(profiles->SetLocalSession(second) == 0);
      CHECK(write(0x32, A64, A32) == 0);
      CHECK(ReadPictureBytes(second_package / "tile_64.png") == A64);
      CHECK(ReadPictureBytes(first_package / "tile_64.png") == B64);
      CHECK(ReadPictureBytes(first_package / "account.dat") == account_a);
      CHECK(ReadPictureBytes(second_package / "account.dat") == account_b);
      CHECK_FALSE(std::filesystem::exists(first_package / ".x360n-picture-update"));
      CHECK_FALSE(std::filesystem::exists(second_package / ".x360n-picture-update"));
    });
  }
  {
    rex::test::KernelFixture fixture({}, {}, {}, {}, {}, directory.root);
    auto* profiles = &rex::xam::CurrentXAM().GetUserProfile();
    CHECK(profiles->FindLocal(first).xuid == first); CHECK(profiles->FindLocal(second).xuid == second);
    CHECK(ReadPictureBytes(profiles->PackagePath(first) / "pp_64.png") == B64);
    CHECK(ReadPictureBytes(profiles->PackagePath(second) / "tile_32.png") == A32);
  }
}

#if defined(_WIN32)
TEST_CASE("Profile picture cleanup preserves bytes with a delete-shared reader",
          "[sdk][profile-picture-write][picture-delete-pending]") {
  PictureDirectory directory;
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, directory.root);
  auto& profiles = rex::xam::CurrentXAM().GetUserProfile();
  uint64_t xuid = 0;
  REQUIRE(profiles.CreateLocal("PendingPicture", &xuid) == 0);
  const auto package = profiles.PackagePath(xuid);
  const auto stage = package / ".x360n-picture-update";
  std::jthread reader;
  rex::xam::UserProfile::PictureUpdate update;
  update.public_picture = true;
  update.large_png.assign(A64.begin(), A64.end());
  update.small_png.assign(A32.begin(), A32.end());
  const auto result = profiles.WritePictures(xuid, update, nullptr, [&](uint32_t checkpoint) {
    if (checkpoint != 6) return;
    HANDLE file = CreateFileW((stage / "new0").c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    REQUIRE(file != INVALID_HANDLE_VALUE);
    reader = std::jthread([file] {
      for (unsigned i = 0; i < 1000; ++i) {
        FILE_STANDARD_INFO info{};
        if (GetFileInformationByHandleEx(file, FileStandardInfo, &info, sizeof(info)) && info.DeletePending) {
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      CloseHandle(file);
    });
  });
  if (reader.joinable()) reader.join();
  CHECK(result == 0);
  CHECK_FALSE(std::filesystem::exists(stage));
  CHECK(ReadPictureBytes(package / "tile_64.png") == A64);
  CHECK(ReadPictureBytes(package / "tile_32.png") == A32);
}
#endif

namespace {
rex::xam::UserProfile::PictureUpdate PictureSet(bool second) {
  rex::xam::UserProfile::PictureUpdate update;
  update.large_png = second ? B64 : A64; update.small_png = second ? B32 : A32;
  update.public_picture = update.personal_picture = true;
  update.key = second ? u"876543210000000200000003" : u"123456780000001100000021";
  return update;
}
}

TEST_CASE("Picture quota includes growth of every published image while backups remain",
          "[sdk][profile-picture-quota]") {
  PictureDirectory directory;
  uint64_t xuid = 0, used = 0;
  const uint64_t enlarged = 4096;
  const uint64_t previous = 2 * (A64.size() + A32.size()) + 50;
  const uint64_t staged = 144 + 4 * enlarged + 50 + previous;
  { std::ofstream config(directory.root / "storage.toml"); config << "version=1\n[hdd]\ncapacity_bytes=8388608\n"; }
  {
    rex::test::KernelFixture fixture({}, {}, {}, {}, {}, directory.root);
    auto* profile = &rex::xam::CurrentXAM().GetUserProfile();
    REQUIRE(profile->CreateLocal("PictureQuota", &xuid) == 0);
    REQUIRE(profile->SetLocalSession(xuid) == 0);
    REQUIRE(profile->WritePictures(xuid, PictureSet(false)) == 0);
    const auto device = rex::xam::CurrentXAM().GetContentManager().GetDevice(1); REQUIRE(device);
    used = device->total_bytes - device->free_bytes;
  }
  // Room for backups, input snapshots and one temporary file, but not for
  // the growth of all four public names before those snapshots are removed.
  { std::ofstream config(directory.root / "storage.toml");
    config << "version=1\n[hdd]\ncapacity_bytes=" << used + staged + enlarged << "\n"; }
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, directory.root);
  auto* profile = &rex::xam::CurrentXAM().GetUserProfile();
  const auto package = profile->PackagePath(xuid); REQUIRE_FALSE(package.empty());
  auto update = PictureSet(true);
  update.large_png.resize(enlarged); update.small_png.resize(enlarged);
  bool committed = true;
  CHECK(profile->WritePictures(xuid, update, &committed) == 112);
  CHECK_FALSE(committed);
  CHECK(ReadPictureBytes(package / "tile_64.png") == A64);
  CHECK(ReadPictureBytes(package / "pp_32.png") == A32);
  CHECK_FALSE(std::filesystem::exists(package / ".x360n-picture-update"));
}

TEST_CASE("Picture staging is reclaimed before the final quota check",
          "[sdk][profile-picture-quota]") {
  PictureDirectory directory;
  uint64_t xuid = 0, used = 0;
  std::filesystem::path package;
  { std::ofstream config(directory.root / "storage.toml"); config << "version=1\n[hdd]\ncapacity_bytes=8388608\n"; }
  {
    rex::test::KernelFixture fixture({}, {}, {}, {}, {}, directory.root);
    auto* profile = &rex::xam::CurrentXAM().GetUserProfile();
    REQUIRE(profile->CreateLocal("PictureReclaim", &xuid) == 0);
    REQUIRE(profile->SetLocalSession(xuid) == 0);
    REQUIRE(profile->WritePictures(xuid, PictureSet(false)) == 0);
    package = profile->PackagePath(xuid);
    const auto device = rex::xam::CurrentXAM().GetContentManager().GetDevice(1); REQUIRE(device);
    used = device->total_bytes - device->free_bytes;
  }
  { std::ofstream config(directory.root / "storage.toml");
    config << "version=1\n[hdd]\ncapacity_bytes=" << used + 4096 << "\n"; }
  const auto stage = package / ".x360n-picture-update";
  REQUIRE(std::filesystem::create_directory(stage));
  { std::ofstream partial(stage / "new0", std::ios::binary); partial << std::string(8192, 'x'); }
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, directory.root);
  auto* profile = &rex::xam::CurrentXAM().GetUserProfile();
  CHECK(profile->WritePictures(xuid, PictureSet(true)) == 0);
  CHECK(ReadPictureBytes(package / "tile_64.png") == B64);
  CHECK_FALSE(std::filesystem::exists(stage));
}

#if defined(_WIN32)
TEST_CASE("Picture publication rolls back after a real locked second destination",
          "[sdk][profile-picture-recovery]") {
  PictureDirectory directory;
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, directory.root);
  auto* profile = &rex::xam::CurrentXAM().GetUserProfile(); uint64_t xuid = 0;
  REQUIRE(profile->CreateLocal("PictureLock", &xuid) == 0); REQUIRE(profile->SetLocalSession(xuid) == 0);
  REQUIRE(profile->WritePictures(xuid, PictureSet(false)) == 0);
  const auto package = profile->PackagePath(xuid);
  const auto key = ReadPictureBytes(package / "settings-v1" / "4064000F.dat");
  HANDLE locked = INVALID_HANDLE_VALUE; bool committed = true;
  const auto result = profile->WritePictures(xuid, PictureSet(true), &committed, [&](uint32_t phase) {
    if (!phase) {
      locked = CreateFileW((package / "tile_32.png").c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      CHECK(locked != INVALID_HANDLE_VALUE);
    }
  });
  CHECK(result == ERROR_ACCESS_DENIED); CHECK_FALSE(committed); // MoveFileExW reports 5 for this locked replacement.
  CHECK(ReadPictureBytes(package / "tile_64.png") == A64);
  CHECK(ReadPictureBytes(package / "tile_32.png") == A32);
  CHECK(ReadPictureBytes(package / "pp_64.png") == A64);
  CHECK(ReadPictureBytes(package / "settings-v1" / "4064000F.dat") == key);
  CHECK_FALSE(std::filesystem::exists(package / ".x360n-picture-update"));
  if (locked != INVALID_HANDLE_VALUE) CloseHandle(locked);
  CHECK(profile->WritePictures(xuid, PictureSet(true)) == 0);
  CHECK(ReadPictureBytes(package / "tile_64.png") == B64);
}

TEST_CASE("Committed picture cleanup preserves newer public bytes and retries private cleanup",
          "[sdk][profile-picture-recovery]") {
  PictureDirectory directory;
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, directory.root);
  auto* profile = &rex::xam::CurrentXAM().GetUserProfile(); uint64_t xuid = 0;
  REQUIRE(profile->CreateLocal("PictureCleanup", &xuid) == 0); REQUIRE(profile->SetLocalSession(xuid) == 0);
  REQUIRE(profile->WritePictures(xuid, PictureSet(false)) == 0);
  const auto package = profile->PackagePath(xuid);
  HANDLE locked = INVALID_HANDLE_VALUE; bool committed = false;
  const auto result = profile->WritePictures(xuid, PictureSet(true), &committed, [&](uint32_t phase) {
    if (phase == 6) {
      locked = CreateFileW((package / ".x360n-picture-update" / "old0").c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      CHECK(locked != INVALID_HANDLE_VALUE);
    }
  });
  CHECK(result == ERROR_SHARING_VIOLATION); CHECK(committed);
  CHECK(ReadPictureBytes(package / "tile_64.png") == B64);
  { std::ofstream newer(package / "tile_64.png", std::ios::binary); newer.write(reinterpret_cast<const char*>(A64.data()), A64.size()); }
  if (locked != INVALID_HANDLE_VALUE) CloseHandle(locked);
  CHECK(profile->RecoverPictures(xuid) == 0);
  CHECK(ReadPictureBytes(package / "tile_64.png") == A64);
  CHECK(ReadPictureBytes(package / "pp_64.png") == B64);
  CHECK_FALSE(std::filesystem::exists(package / ".x360n-picture-update"));
}

TEST_CASE("Picture update independent process fixture", "[sdk][profile-picture-process-child]") {
  const auto input = rex::platform::GetEnvironmentValue("X360N_PICTURE_PROCESS_ROOT");
  const auto requested = rex::platform::GetEnvironmentValue("X360N_PICTURE_PROCESS_PHASE");
  if (!input || !requested) SKIP("Run tests/local/profile-pictures-process.ps1");
  const auto root = std::filesystem::weakly_canonical(std::filesystem::u8path(*input));
  REQUIRE(root.parent_path() == std::filesystem::weakly_canonical(std::filesystem::current_path() / ".local/diagnostics"));
  REQUIRE(root.filename().string().starts_with("picture-process-"));
  REQUIRE(ReadPictureBytes(root / ".picture-process-fixture") ==
          std::vector<uint8_t>{'x','3','6','0','n','-','p','i','c','t','u','r','e','-','p','r','o','c','e','s','s','-','v','1'});
  const std::string phase(*requested);
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, root);
  auto* profile = &rex::xam::CurrentXAM().GetUserProfile();
  if (phase == "seed") {
    uint64_t xuid = 0; REQUIRE(profile->CreateLocal("PictureProc", &xuid) == 0);
    REQUIRE(profile->SetLocalSession(xuid) == 0);
    REQUIRE(profile->WritePictures(xuid, PictureSet(false)) == 0);
    const auto text = profile->PackagePath(xuid).u8string();
    std::ofstream pointer(root / "package-path.txt", std::ios::binary);
    pointer.write(reinterpret_cast<const char*>(text.data()), text.size());
    REQUIRE(bool(pointer));
    return;
  }
  const auto identity = profile->identity(); REQUIRE(identity.xuid); REQUIRE(identity.name == "PictureProc");
  const auto package = profile->PackagePath(identity.xuid); REQUIRE_FALSE(package.empty());
  if (phase == "interrupt") {
    const auto value = rex::platform::GetEnvironmentValue("X360N_PICTURE_PROCESS_CHECKPOINT"); REQUIRE(value);
    const auto checkpoint = std::strtoul(value->c_str(), nullptr, 10); REQUIRE(checkpoint <= 6);
    profile->WritePictures(identity.xuid, PictureSet(true), nullptr, [&](uint32_t current) {
      if (current == checkpoint) std::_Exit(93);
    });
    FAIL("Interruption checkpoint was not reached");
  }
  if (phase == "corrupt" || phase == "conflict") {
    CHECK(profile->RecoverPictures(identity.xuid) == (phase == "corrupt" ? 13u : 183u));
    CHECK(std::filesystem::exists(package / ".x360n-picture-update" / "ready"));
    return;
  }
  REQUIRE((phase == "old" || phase == "new"));
  const bool next = phase == "new";
  std::vector<uint8_t> bytes;
  REQUIRE(profile->ReadPicture(identity.xuid, false, false, bytes) == 0); CHECK(bytes == (next ? B64 : A64));
  REQUIRE(profile->ReadPicture(identity.xuid, false, true, bytes) == 0); CHECK(bytes == (next ? B32 : A32));
  REQUIRE(profile->ReadPicture(identity.xuid, true, false, bytes) == 0); CHECK(bytes == (next ? B64 : A64));
  REQUIRE(profile->ReadPicture(identity.xuid, true, true, bytes) == 0); CHECK(bytes == (next ? B32 : A32));
  const auto* key = static_cast<rex::xam::UserProfile::UnicodeSetting*>(profile->GetSetting(0x4064000F));
  REQUIRE(key); CHECK_FALSE(key->load_failed); CHECK(key->value == *PictureSet(next).key);
  CHECK_FALSE(std::filesystem::exists(package / ".x360n-picture-update"));
}
#endif

TEST_CASE("Profile tile reads preserve full XUID and distinct public and personal namespaces",
          "[sdk][profile-picture-write]") {
  rex::test::KernelFixture fixture;
  auto& profiles = rex::xam::CurrentXAM().GetUserProfile();
  uint64_t first = 0, second = 0;
  REQUIRE(profiles.CreateLocal("PictureReadA", &first) == 0);
  REQUIRE(profiles.CreateLocal("PictureReadB", &second) == 0);
  const auto first_write = profiles.WritePictures(first, PictureSet(false));
  INFO("picture write error=" << first_write << "; "
       << (first_write ? PictureStageDescription(profiles.PackagePath(first)) : "transaction removed"));
  REQUIRE(first_write == 0);
  REQUIRE(profiles.WritePictures(second, PictureSet(true)) == 0);
  fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
    const auto output = caller.Alloc(32768), size = caller.Alloc(4);
    auto read = [&](uint32_t type, uint64_t xuid, uint32_t user) {
      caller.Store32(size, 32768);
      std::memset(caller.GetBase() + output, 0xAB, 32768);
      return caller.XAM("XamReadTile", {type, 0, xuid, user, output, size, 0});
    };
    CHECK(read(10, first, 0xFE) == 0);
    CHECK(caller.Load32(size) == A64.size());
    CHECK(std::equal(A64.begin(), A64.end(), caller.GetBase() + output));
    CHECK(read(11, second, 0xFE) == 0);
    CHECK(caller.Load32(size) == B32.size());
    CHECK(std::equal(B32.begin(), B32.end(), caller.GetBase() + output));
    CHECK(read(10, first ^ 0x100000000ull, 0xFE) == 1168);
    CHECK(caller.GetBase()[output] == 0xAB);
    CHECK(read(2, first, 0) == 1168);
    REQUIRE(profiles.SetLocalSession(first) == 0);
    CHECK(read(2, 0, 0) == 0);
    CHECK(std::equal(A64.begin(), A64.end(), caller.GetBase() + output));
    CHECK(read(10, second, 0) == 1168);
    CHECK(read(10, first, 1) == 1317);
    CHECK(read(10, first, 4) == 87);
    CHECK(read(0, first, 0) == 87);
    CHECK(read(10, second, 0xFE) == 0);
    CHECK(std::equal(B64.begin(), B64.end(), caller.GetBase() + output));
  });
}
