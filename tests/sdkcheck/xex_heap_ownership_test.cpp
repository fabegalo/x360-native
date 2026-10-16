#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstring>
#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/system/xex_module.h>
#include <rex/system/xmemory.h>
#include "synthetic_xex.h"

using rex::X_STATUS;
using namespace x360n::test;

TEST_CASE("XEX load and key retry preserve other allocations in the image heap",
          "[sdk][xex-heap-ownership]") {
  Workdir work("x360n_xex_heap", "ownership");
  rex::Runtime rt(work.dir);
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  auto* heap = rt.memory()->LookupHeap(kXexBase);
  constexpr uint32_t neighbor = kXexBase - 4 * kXexPage;
  constexpr auto reserve_commit = rex::memory::kMemoryAllocationReserve |
                                  rex::memory::kMemoryAllocationCommit;
  constexpr auto rw = rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite;
  REQUIRE(heap->AllocFixed(neighbor, kXexPage, kXexPage, reserve_commit, rw));
  auto* marker = rt.memory()->TranslateVirtual<uint32_t*>(neighbor);
  *marker = 0x12345678;
  const auto baseline = heap->unreserved_page_count();
  std::vector<Placed> placed;
  auto bytes = BuildXex({}, &placed, 0, Entry::kReturnZero);
  bool expected_load = true;
  bool collision = false;
  SECTION("successful image retains neighbor metadata") {}
  SECTION("invalid PE retries without discarding neighbor") {
    // Synthetic header size is 0x1000; break the embedded MZ signature.
    bytes[0x1000] = 0;
    expected_load = false;
  }
  SECTION("occupied image range is refused without replacing its bytes") {
    collision = true;
    REQUIRE(heap->AllocFixed(kXexBase, kXexImageSize, kXexPage, reserve_commit, rw));
    std::memset(rt.memory()->TranslateVirtual(kXexBase), 0xA5, kXexImageSize);
    expected_load = false;
  }
  rex::runtime::XexModule module(rt.function_dispatcher(), rt.kernel_state());
  CHECK(module.Load("test.xex", "game:\\test.xex", bytes.data(), bytes.size()) == expected_load);
  uint32_t size = 0;
  CHECK(heap->QuerySize(neighbor, &size));
  CHECK(size == kXexPage);
  CHECK(*marker == 0x12345678);
  if (!expected_load && !collision) {
    CHECK(heap->unreserved_page_count() == baseline);
  }
  CHECK(module.Unload());
  CHECK(module.Unload());
  if (collision) {
    CHECK(heap->QuerySize(kXexBase, &size));
    CHECK(size == kXexImageSize);
    auto* data = rt.memory()->TranslateVirtual<uint8_t*>(kXexBase);
    CHECK(std::all_of(data, data + kXexImageSize, [](auto b) { return b == 0xA5; }));
    CHECK(heap->Release(kXexBase));
  }
  CHECK(heap->unreserved_page_count() == baseline);
  CHECK(heap->Release(neighbor));
}
