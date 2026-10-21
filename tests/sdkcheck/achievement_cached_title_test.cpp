#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <rex/kernel/init.h>
#include <rex/kernel/xam/module.h>
#include <rex/runtime.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include "offline_achievement_fixture.h"

extern "C" REX_FUNC(__imp__XamGetCachedTitleName);
extern "C" REX_FUNC(__imp__XamGetCachedTitleNameEx);

namespace {
struct TitleCacheDirectory {
  std::filesystem::path path = std::filesystem::current_path() /
      ("x360n-title-cache-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  TitleCacheDirectory() { std::filesystem::create_directories(path); }
  ~TitleCacheDirectory() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};
}

TEST_CASE("Cached title names use original metadata and character capacity ABI", "[sdk][achievement-ui]") {
  TitleCacheDirectory directory;
  rex::Runtime rt(directory.path);
  rex::RuntimeConfig config{};
  config.tool_mode = true; config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == 0);
  REQUIRE(rt.function_dispatcher()->InitializeFunctionTable(0x82000000, 0x1000, 0x82000000, 0x10000, true, true));
  auto xam = rt.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  const auto regular = xam->GetProcAddressByOrdinal(0x02B6);
  const auto extended = xam->GetProcAddressByOrdinal(0x0481);
  REQUIRE(regular); REQUIRE(extended);
  auto* regular_call = rt.function_dispatcher()->GetFunction(regular);
  auto* extended_call = rt.function_dispatcher()->GetFunction(extended);
  REQUIRE(regular_call); REQUIRE(extended_call);
  auto& store = rt.kernel_state()->offline_achievements();
  store.Configure(directory.path);
  auto metadata = offline_achievement_fixture::Metadata();
  REQUIRE(store.ImportTitle(0x12345678, metadata) == rex::system::OfflineAchievementResult::kSuccess);
  const std::string title = "Test resource title";
  auto first = std::search(metadata.begin(), metadata.end(), title.begin(), title.end());
  REQUIRE(first != metadata.end());
  *first = 'R';
  REQUIRE(store.ImportTitle(0x87654321, metadata) == rex::system::OfflineAchievementResult::kSuccess);
  const uint32_t block = rt.memory()->SystemHeapAlloc(128);
  REQUIRE(block);
  auto* bytes = rt.memory()->TranslateVirtual<uint8_t*>(block);
  auto* output = rt.memory()->TranslateVirtual<rex::be<uint16_t>*>(block);
  auto* count = rt.memory()->TranslateVirtual<rex::be<uint32_t>*>(block + 64);
  auto* base = rt.memory()->virtual_membase();
  PPCContext ctx{};
  auto prepare = [&](uint32_t id, uint32_t capacity) {
    std::memset(bytes, 0xCC, 128);
    *count = capacity;
    ctx = {};
    ctx.r3.u64 = id; ctx.r4.u64 = block; ctx.r5.u64 = block + 64;
  };
  auto read = [&] {
    std::u16string value;
    for (uint32_t i = 0; i < uint32_t(*count) && uint16_t(output[i]); ++i)
      value.push_back(static_cast<char16_t>(uint16_t(output[i])));
    return value;
  };
  prepare(0x12345678, 0xFFFFFFFF);
  regular_call(ctx, base);
  CHECK(ctx.r3.u32 == 0); CHECK(uint32_t(*count) == 20);
  CHECK(read() == u"Test resource title"); CHECK(bytes[40] == 0xCC);
  prepare(0x87654321, 23);
  ctx.r6.u64 = 1; // lookup flags, never an OVERLAPPED pointer
  extended_call(ctx, base);
  CHECK(ctx.r3.u32 == 0); CHECK(read() == u"Rest resource title");
  prepare(0x12345678, 6);
  __imp__XamGetCachedTitleName(ctx, base);
  CHECK(ctx.r3.u32 == 0); CHECK(uint32_t(*count) == 6);
  CHECK(read() == u"Test "); CHECK(bytes[12] == 0xCC);
  prepare(0x12345678, 1);
  __imp__XamGetCachedTitleName(ctx, base);
  CHECK(ctx.r3.u32 == 0); CHECK(uint32_t(*count) == 1); CHECK(uint16_t(output[0]) == 0);
  prepare(0x11112222, 23);
  ctx.r6.u64 = 1;
  __imp__XamGetCachedTitleNameEx(ctx, base);
  CHECK(ctx.r3.u32 == 1168); CHECK(uint32_t(*count) == 0); CHECK(uint16_t(output[0]) == 0);
  prepare(0, 23);
  __imp__XamGetCachedTitleName(ctx, base);
  CHECK(ctx.r3.u32 == 87); CHECK(uint32_t(*count) == 1); CHECK(uint16_t(output[0]) == 0);
  prepare(0x12345678, 0);
  __imp__XamGetCachedTitleName(ctx, base);
  CHECK(ctx.r3.u32 == 87); CHECK(bytes[0] == 0xCC);
  prepare(0x12345678, 23);
  ctx.r4.u64 = block + 1;
  __imp__XamGetCachedTitleName(ctx, base);
  CHECK(ctx.r3.u32 == 87); CHECK(uint32_t(*count) == 23);
  prepare(0x12345678, 23);
  ctx.r5.u64 = 0xFFFFFFFC;
  __imp__XamGetCachedTitleName(ctx, base);
  CHECK(ctx.r3.u32 == 87); CHECK(bytes[0] == 0xCC);
  prepare(0x12345678, 23);
  ctx.r4.u64 = 0xFFFFFFFE;
  __imp__XamGetCachedTitleName(ctx, base);
  CHECK(ctx.r3.u32 == 87); CHECK(uint32_t(*count) == 23);
  const auto cache = directory.path / "offline-achievements-v1" / "metadata" / "12345678.xdbf";
  {
    std::fstream file(cache, std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(file.good());
    file.seekp(-1, std::ios::end); file.put('\0');
  }
  prepare(0x12345678, 23);
  __imp__XamGetCachedTitleName(ctx, base);
  CHECK(ctx.r3.u32 == 13); CHECK(uint32_t(*count) == 0); CHECK(uint16_t(output[0]) == 0);
  rt.memory()->SystemHeapFree(block);
}
