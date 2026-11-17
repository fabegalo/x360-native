#include "kernel/title_state.h"
#include "codegen/tool_runtime.h"
#include "synthetic_xex.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <rex/kernel/module.h>

using namespace x360n::test;

TEST_CASE("XEX load and key retry preserve other allocations in the image heap",
          "[sdk][xex-heap-ownership]") {
  Workdir work("x360n_xex_heap", "ownership");
  auto opened = work.GetTools().Open(work.dir);
  REQUIRE(opened);
  auto &rt = opened->GetRuntime();
  auto &state = rex::kernel::StateOf(rt);
  auto *memory = state.GetAddressSpace();
  auto *heap = memory->LookupHeap(kXexBase);
  constexpr uint32_t neighbor = kXexBase - 4 * kXexPage;
  constexpr auto reserve_commit = rex::kernel::kMemoryAllocationReserve |
                                  rex::kernel::kMemoryAllocationCommit;
  constexpr auto rw =
      rex::kernel::kMemoryProtectRead | rex::kernel::kMemoryProtectWrite;
  REQUIRE(heap->AllocFixed(neighbor, kXexPage, kXexPage, reserve_commit, rw));
  auto *marker = memory->TranslateVirtual<uint32_t *>(neighbor);
  *marker = 0x12345678;
  const auto baseline = heap->GetUnreservedPageCount();
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
    REQUIRE(heap->AllocFixed(kXexBase, kXexImageSize, kXexPage, reserve_commit,
                             rw));
    std::memset(memory->TranslateVirtual(kXexBase), 0xA5, kXexImageSize);
    expected_load = false;
  }
  rex::kernel::XEXModule module(state.GetFunctionDispatcher(), rt);
  CHECK(module.Load("test.xex", "game:\\test.xex", bytes.data(),
                    bytes.size()) == expected_load);
  uint32_t size = 0;
  CHECK(heap->QuerySize(neighbor, &size));
  CHECK(size == kXexPage);
  CHECK(*marker == 0x12345678);
  if (!expected_load && !collision) {
    CHECK(heap->GetUnreservedPageCount() == baseline);
  }
  CHECK(module.Unload());
  CHECK(module.Unload());
  if (collision) {
    REQUIRE(heap->QuerySize(kXexBase, &size));
    CHECK(size == kXexImageSize);
    auto *data = memory->TranslateVirtual<uint8_t *>(kXexBase);
    CHECK(std::all_of(data, data + kXexImageSize,
                      [](auto b) { return b == 0xA5; }));
    CHECK(heap->Release(kXexBase));
  }
  CHECK(heap->GetUnreservedPageCount() == baseline);
  CHECK(heap->Release(neighbor));
}
