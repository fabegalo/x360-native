#include <catch2/catch_test_macros.hpp>
#include <array>
#include "kernel/kernel_fixture.h"
#include <rex/kernel/title_runtime.h>
#include <rex/filesystem/file_system.h>
#include <rex/memory/endian.h>
#include <cstring>
#include <string>
#include <string_view>
#include "filesystem/fixture.h"
#include "../../titles/dash_17559/src/system_ext_partition.h"

TEST_CASE("System extension originals precede raw partitions without replacing storage owners",
          "[sdk][package-mount]") {
  using namespace rex::filesystem;
  test::Fixture fixture;
  auto& files = fixture.GetFileSystem();
  const std::string raw = "\\Device\\Harddisk0";
  const std::string system = raw + "\\SystemExtPartition";
  const MountEntry null{raw, ProviderType::kNull, {}, MountFlags::kNone,
                        {"\\Partition0", "\\Cache0", "\\Cache1"}};
  SECTION("originals are read only and raw partitions remain accessible") {
    test::WriteHost(fixture.GetTemp() / "systemext" / "system.manifest", "original");
    REQUIRE(files.Mount(null));
    REQUIRE(dash17559::MountSystemExtPartition(files, fixture.GetTemp()));
    auto opened = files.Open(system + "\\system.manifest", {});
    REQUIRE(opened);
    CHECK(test::ReadText(*opened->file) == "original");
    const std::array<rex::u8, 1> replacement{'x'};
    CHECK_FALSE(opened->file->Write(replacement, 0));
    CHECK(test::ReadText(*opened->file) == "original");
    CHECK(files.Open(raw + "\\Partition0", {}));
    CHECK(files.Open(raw + "\\Cache0", {}));
    CHECK(files.Open(raw + "\\Cache1", {}));
    const auto mounts = files.GetMounts();
    REQUIRE(mounts.size() == 2);
    CHECK(mounts.front().path == system);
    CHECK(mounts.back().null_paths == null.null_paths);
  }
  SECTION("missing originals preserve the existing mount") {
    REQUIRE(files.Mount(null));
    CHECK_FALSE(dash17559::MountSystemExtPartition(files, fixture.GetTemp()));
    REQUIRE(files.GetMounts().size() == 1);
    CHECK(files.GetMounts().front().null_paths == null.null_paths);
    CHECK(files.Open(raw + "\\Partition0", {}));
  }
  SECTION("a host storage owner is never replaced") {
    test::WriteHost(fixture.GetTemp() / "systemext" / "system.manifest", "original");
    fixture.MountTemp(raw);
    CHECK_FALSE(dash17559::MountSystemExtPartition(files, fixture.GetTemp()));
    REQUIRE(files.GetMounts().size() == 1);
    CHECK(files.GetMounts().front().type == ProviderType::kHostDirectory);
  }
}

TEST_CASE("ObTranslateSymbolicLink and XeCryptMd5 follow the XAM 17559 package mount ABI",
          "[sdk][package-mount]") {
  rex::test::KernelFixture fixture;
  REQUIRE(fixture.GetRuntime().GetFileSystem().Link("SysExt:", "\\Device\\Harddisk0\\SystemExtPartition"));
  fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
    auto* base = caller.GetBase();
    const uint32_t block = caller.Alloc(4096); REQUIRE(block);
    auto put32 = [&](uint32_t at, uint32_t v) { rex::memory::store_and_swap<uint32_t>(base + at, v); };
    auto put16 = [&](uint32_t at, uint16_t v) { rex::memory::store_and_swap<uint16_t>(base + at, v); };
    auto* ctx = &caller.GetContext();
    // Same call shape as XAM 8167FCD8: OBJECT_ATTRIBUTES {root, ANSI_STRING*, 0x40}.
    auto translate = [&](std::string_view name, uint32_t root, uint16_t capacity) {
      std::memset(base + block, 0xCD, 4096);
      std::memcpy(base + block + 0x100, name.data(), name.size());
      put16(block + 0x20, uint16_t(name.size())); put16(block + 0x22, uint16_t(name.size()));
      put32(block + 0x24, block + 0x100);
      put32(block + 0x00, root); put32(block + 0x04, block + 0x20); put32(block + 0x08, 0x40);
      put16(block + 0x40, 0); put16(block + 0x42, capacity); put32(block + 0x44, block + 0x400);
      caller.Kernel("ObTranslateSymbolicLink", {block, block + 0x40});
      const auto length = rex::memory::load_and_swap<uint16_t>(base + block + 0x40);
      return std::string(reinterpret_cast<const char*>(base + block + 0x400), length);
    };
    CHECK(translate("SysExt:\\Content\\FFFE07DF00000002", uint32_t(-3), 260) ==
          "\\Device\\Harddisk0\\SystemExtPartition\\Content\\FFFE07DF00000002");
    CHECK(ctx->r3.u32 == 0);
    CHECK(base[block + 0x400 + 61] == 0xCD);  // no terminator: XAM writes its own
    CHECK(translate("\\Device\\Harddisk0\\Partition1\\a", 0, 260) == "\\Device\\Harddisk0\\Partition1\\a");
    CHECK(ctx->r3.u32 == 0);
    translate("NoSuchLink:\\a", uint32_t(-3), 260);
    CHECK(ctx->r3.u32 == 0xC0000034);
    translate("SysExt:\\a", uint32_t(-3), 8);
    CHECK(ctx->r3.u32 == 0xC0000023);

    // XeCryptMd5(in1, n1, in2, n2, in3, n3, digest, digest_size): MD5("abc") in two pieces.
    std::memset(base + block, 0xCD, 4096);
    std::memcpy(base + block, "abc", 3);
    caller.Kernel("XeCryptMd5", {block, 1, block + 1, 2, 0, 0, block + 0x100, 8});
    const uint8_t expected[8] = {0x90, 0x01, 0x50, 0x98, 0x3C, 0xD2, 0x4F, 0xB0};
    CHECK(std::memcmp(base + block + 0x100, expected, 8) == 0);
    CHECK(base[block + 0x108] == 0xCD);
  });
}
