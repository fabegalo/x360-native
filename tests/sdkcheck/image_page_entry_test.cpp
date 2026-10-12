// KeGetImagePageTableEntry for pages of the 4 KB-page XEX heap (patch 0005).
//
// dash.xex 2.0.17559.0 validates the buffers of its Lua chunks (0x928EF408): the
// address must be in 0x80000000-0x9FFFFFFF and bit 0 of KeGetImagePageTableEntry
// for its page must be set, else KeBugCheck(0xF7). At f5337cdc the export returned
// the page index | 0x40000000, so bit 0 was clear for every even page: the dashboard
// stopped on 0x92FD2204 (run 20261012T011201Z-dash17559-diag-luabuf).
// patches/rexglue-sdk/0005 sets bit 0 for that heap, as Xenia Canary 331ddf79bf96.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <system_error>

#include <rex/kernel/init.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>

using rex::X_STATUS;  // X_STATUS_SUCCESS expands to a cast to it

// The kernel export as the generated code calls it (rexruntime exports all symbols).
extern "C" REX_FUNC(__imp__KeGetImagePageTableEntry);

namespace {

namespace fs = std::filesystem;

uint32_t PageEntry(rex::Runtime& rt, uint32_t address) {
  auto ctx = std::make_unique<PPCContext>();
  ctx->r3.u64 = address;
  __imp__KeGetImagePageTableEntry(*ctx, rt.memory()->virtual_membase());
  return ctx->r3.u32;
}

}  // namespace

TEST_CASE("KeGetImagePageTableEntry sets bit 0 for pages of the 4 KB-page XEX heap",
          "[sdk][kernel]") {
  const fs::path dir = fs::temp_directory_path() / "x360n_image_page_entry_test";
  fs::create_directories(dir);
  {
    rex::Runtime rt(dir);
    rex::RuntimeConfig config{};
    config.kernel_init = rex::kernel::InitializeKernel;
    config.tool_mode = true;
    REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);

    // 0x90000000 XEX heap, 4 KB pages: index | 0x40000000 | bit 0.
    CHECK(PageEntry(rt, 0x92FD2204) == 0x40002FD3);  // even page: bit 0 was clear before
    CHECK(PageEntry(rt, 0x92135820) == 0x40002135);  // odd page: unchanged
    CHECK(PageEntry(rt, 0x90000000) == 0x40000001);
    // 0x80000000 XEX heap, 64 KB pages (titles): page index only, unchanged.
    CHECK(PageEntry(rt, 0x82020000) == 0x202);
    CHECK(PageEntry(rt, 0x82010000) == 0x201);
    // Not an XEX heap.
    CHECK(PageEntry(rt, 0x40000000) == 0);
  }
  std::error_code ec;
  fs::remove_all(dir, ec);
}
