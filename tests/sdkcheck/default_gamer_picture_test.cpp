#include <catch2/catch_test_macros.hpp>
#include <rex/runtime.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/kernel/init.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xthread.h>
#include <rex/system/xam/user_profile.h>
#include <chrono>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include "picture_write_fixture.h"

extern "C" void __imp__XamWriteGamerTile(PPCContext&, uint8_t*);

namespace {
std::vector<uint8_t> ReadBytes(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), {}};
}
// XUIZ v3 as in shrdres.xzp: magic, version, size, 0, directory size, count,
// then {size, offset, name length, name} and the data.
std::vector<uint8_t> Xuiz(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files) {
  std::vector<uint8_t> directory, data;
  auto be32 = [](std::vector<uint8_t>& v, uint32_t x) { for (int s = 24; s >= 0; s -= 8) v.push_back(uint8_t(x >> s)); };
  for (const auto& [name, bytes] : files) {
    be32(directory, uint32_t(bytes.size())); be32(directory, uint32_t(data.size()));
    directory.push_back(uint8_t(name.size())); directory.insert(directory.end(), name.begin(), name.end());
    data.insert(data.end(), bytes.begin(), bytes.end());
  }
  std::vector<uint8_t> out{'X', 'U', 'I', 'Z'};
  be32(out, 3); be32(out, uint32_t(22 + directory.size() + data.size())); be32(out, 0);
  be32(out, uint32_t(directory.size()));
  out.push_back(uint8_t(files.size() >> 8)); out.push_back(uint8_t(files.size()));
  out.insert(out.end(), directory.begin(), directory.end());
  out.insert(out.end(), data.begin(), data.end());
  return out;
}
}  // namespace

// XAM 17559 serves dashboard default gamer pictures from its shrdres cache
// (8191BCB8/8191C098); CreateProfile 9010B370 writes one with
// XamWriteGamerTile(0, FFFE07D1, 0002000N, 0001000N, 0, overlapped).
TEST_CASE("Catalog gamer picture writes store the dashboard default from shrdres",
          "[sdk][default-gamer-picture]") {
  using namespace picture_write_fixture;
  const auto root = std::filesystem::current_path() /
      ("x360n-default-picture-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  REQUIRE(std::filesystem::create_directory(root));
  const auto system_root = root / "system"; std::filesystem::create_directory(system_root);
  auto corrupt = B64; corrupt[40] ^= 1;
  const auto archive = Xuiz({{"64_fffe07d10002000300010003.png", A64}, {"32_fffe07d10002000300010003.png", A32},
                             {"64_fffe07d10002000400010004.png", corrupt}, {"32_fffe07d10002000400010004.png", B32}});
  { std::ofstream out(system_root / "shrdres.xzp", std::ios::binary);
    out.write(reinterpret_cast<const char*>(archive.data()), archive.size()); }
  {
    rex::Runtime rt(root, root);
    rex::RuntimeConfig config{}; config.tool_mode = true; config.kernel_init = rex::kernel::InitializeKernel;
    REQUIRE(rt.Setup(std::move(config)) == 0);
    auto device = std::make_unique<rex::filesystem::HostPathDevice>("\\SystemRoot", system_root, true);
    REQUIRE(device->Initialize());
    REQUIRE(rt.file_system()->RegisterDevice(std::move(device)));
    REQUIRE(rt.file_system()->RegisterSymbolicLink("media:", "\\SystemRoot"));
    auto* profiles = rt.kernel_state()->user_profile();
    uint64_t xuid = 0;
    REQUIRE(profiles->CreateLocal("DefaultPic", &xuid) == 0);
    REQUIRE(profiles->SetLocalSession(xuid) == 0);
    const auto package = profiles->PackagePath(xuid);
    auto* base = rt.memory()->virtual_membase();
    uint32_t stored = ~0u, missing = ~0u, invalid = ~0u, other_title = ~0u, out_of_range = ~0u;
    auto caller = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
        rt.kernel_state(), 128 * 1024, 0, [&] {
      auto write = [&](uint32_t title, uint32_t index) {
        PPCContext ctx{};
        ctx.r3.u64 = 0; ctx.r4.u64 = title; ctx.r5.u64 = 0x20000 | index; ctx.r6.u64 = 0x10000 | index;
        __imp__XamWriteGamerTile(ctx, base);
        return ctx.r3.u32;
      };
      stored = write(0xFFFE07D1, 3);
      missing = write(0xFFFE07D1, 1);
      invalid = write(0xFFFE07D1, 4);
      other_title = write(0x12345678, 3);
      out_of_range = write(0xFFFE07D1, 13);
      return 0;
    }));
    REQUIRE(caller->Create() == 0); uint64_t timeout = uint64_t(-50'000'000ll);
    REQUIRE(caller->Wait(0, 0, 0, &timeout) == 0);
    CHECK(stored == 0);
    CHECK(ReadBytes(package / "tile_64.png") == A64);
    CHECK(ReadBytes(package / "tile_32.png") == A32);
    CHECK_FALSE(std::filesystem::exists(package / "pp_64.png"));
    const auto* key = static_cast<rex::system::xam::UserProfile::UnicodeSetting*>(profiles->GetSetting(0x4064000F));
    REQUIRE(key); CHECK(key->value == u"fffe07d10002000300010003");
    // Absent, corrupt, foreign and out-of-cache pictures fail and keep the stored one.
    CHECK(missing == 1627); CHECK(invalid == 1627); CHECK(other_title == 1627); CHECK(out_of_range == 1627);
    CHECK(ReadBytes(package / "tile_64.png") == A64);
  }
  std::error_code ec; std::filesystem::remove_all(root, ec);
}

extern "C" void __imp__XamBuildSharedSystemResourceLocator(PPCContext&, uint8_t*);

// gamerprofile 901223E0 names each default picture and asks for its shrdres
// locator; the guest name is big-endian and must reach the locator unchanged.
TEST_CASE("Shared system resource locators keep the guest resource name",
          "[sdk][default-gamer-picture]") {
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{}; config.tool_mode = true; config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == 0);
  auto* memory = rt.memory(); auto* base = memory->virtual_membase();
  const uint32_t block = memory->SystemHeapAlloc(1024); REQUIRE(block);
  const std::u16string name = u"64_fffe07d10002000300010003.png";
  for (size_t i = 0; i <= name.size(); ++i)
    rex::memory::store_and_swap<uint16_t>(base + block + 2 * i, i < name.size() ? name[i] : 0);
  PPCContext ctx{}; ctx.r3.u64 = block; ctx.r4.u64 = block + 256; ctx.r5.u64 = 128;
  __imp__XamBuildSharedSystemResourceLocator(ctx, base);
  CHECK(ctx.r3.u32 == 0);
  std::u16string locator;
  for (uint32_t i = 0; uint16_t c = rex::memory::load_and_swap<uint16_t>(base + block + 256 + 2 * i); ++i)
    locator.push_back(c);
  CHECK(locator == u"file://media:/shrdres.xzp#64_fffe07d10002000300010003.png");
  memory->SystemHeapFree(block);
}
