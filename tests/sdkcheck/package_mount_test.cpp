#include <catch2/catch_test_macros.hpp>
#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/ppc/func.h>
#include <rex/filesystem/vfs.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xthread.h>
#include <cstring>
#include <string>
#include <string_view>

extern "C" REX_FUNC(__imp__ObTranslateSymbolicLink);
extern "C" REX_FUNC(__imp__XeCryptMd5);

TEST_CASE("ObTranslateSymbolicLink and XeCryptMd5 follow the XAM 17559 package mount ABI",
          "[sdk][package-mount]") {
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{}; config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == 0);
  REQUIRE(rt.file_system()->RegisterSymbolicLink("SysExt:", "\\Device\\Harddisk0\\SystemExtPartition"));
  auto* memory = rt.memory(); auto* base = memory->virtual_membase();
  const uint32_t block = memory->SystemHeapAlloc(4096); REQUIRE(block);
  auto put32 = [&](uint32_t at, uint32_t v) { rex::memory::store_and_swap<uint32_t>(base + at, v); };
  auto put16 = [&](uint32_t at, uint16_t v) { rex::memory::store_and_swap<uint16_t>(base + at, v); };
  auto caller = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
      rt.kernel_state(), 128 * 1024, 0, [&] {
    auto* ctx = rex::system::XThread::GetCurrentThread()->thread_state()->context();
    // Same call shape as XAM 8167FCD8: OBJECT_ATTRIBUTES {root, ANSI_STRING*, 0x40}.
    auto translate = [&](std::string_view name, uint32_t root, uint16_t capacity) {
      std::memset(base + block, 0xCD, 4096);
      std::memcpy(base + block + 0x100, name.data(), name.size());
      put16(block + 0x20, uint16_t(name.size())); put16(block + 0x22, uint16_t(name.size()));
      put32(block + 0x24, block + 0x100);
      put32(block + 0x00, root); put32(block + 0x04, block + 0x20); put32(block + 0x08, 0x40);
      put16(block + 0x40, 0); put16(block + 0x42, capacity); put32(block + 0x44, block + 0x400);
      ctx->r3.u64 = block; ctx->r4.u64 = block + 0x40;
      __imp__ObTranslateSymbolicLink(*ctx, base);
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
    ctx->r3.u64 = block; ctx->r4.u64 = 1; ctx->r5.u64 = block + 1; ctx->r6.u64 = 2;
    ctx->r7.u64 = 0; ctx->r8.u64 = 0; ctx->r9.u64 = block + 0x100; ctx->r10.u64 = 8;
    __imp__XeCryptMd5(*ctx, base);
    const uint8_t expected[8] = {0x90, 0x01, 0x50, 0x98, 0x3C, 0xD2, 0x4F, 0xB0};
    CHECK(std::memcmp(base + block + 0x100, expected, 8) == 0);
    CHECK(base[block + 0x108] == 0xCD);
    return 0;
  }));
  REQUIRE(caller->Create() == 0); uint64_t timeout = uint64_t(-50'000'000ll);
  REQUIRE(caller->Wait(0, 0, 0, &timeout) == 0);
  memory->SystemHeapFree(block);
}
