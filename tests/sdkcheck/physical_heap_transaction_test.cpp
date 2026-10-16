#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <barrier>
#include <chrono>
#include <future>
#include <thread>
#include <rex/system/xmemory.h>

using namespace rex::memory;

TEST_CASE("Physical allocations serialize parent and alias metadata", "[sdk][physical-transaction]") {
  Memory memory;
  REQUIRE(memory.Initialize());
  for (uint32_t page : {0x1000u, 0x10000u, 0x1000000u}) {
    auto* heap = memory.LookupHeapByType(true, page);
    auto guard = rex::thread::global_critical_region::AcquireDirect();
    std::promise<void> entered;
    uint32_t address = 0;
    auto allocation = std::async(std::launch::async, [&] {
      entered.set_value();
      return heap->Alloc(page, page, kMemoryAllocationReserve | kMemoryAllocationCommit,
                         kMemoryProtectRead | kMemoryProtectWrite, true, &address);
    });
    entered.get_future().wait();
    const auto status = allocation.wait_for(std::chrono::milliseconds(100));
    guard.unlock();
    const bool allocated = allocation.get();
    CHECK(status == std::future_status::timeout);
    REQUIRE(allocated);
    CHECK(heap->Release(address));
  }
}

TEST_CASE("Concurrent physical reuse preserves allocation metadata and page counts",
          "[sdk][physical-transaction]") {
  Memory memory;
  REQUIRE(memory.Initialize());
  auto* heap = memory.LookupHeapByType(true, 0x1000);
  const auto free_before = heap->GetUnreservedPageCount();
  std::atomic<unsigned> failures{0};
  std::barrier start(3);
  auto work = [&] {
    start.arrive_and_wait();
    for (unsigned i = 0; i < 1000; ++i) {
      uint32_t address = 0;
      if (!heap->AllocRange(0xF8000000, 0xF810FFFF, 0x3000, 0x1000,
                            kMemoryAllocationReserve | kMemoryAllocationCommit,
                            kMemoryProtectRead | kMemoryProtectWrite, true, &address)) {
        ++failures;
        continue;
      }
      uint32_t allocation_base = address, size = 0;
      if (!heap->QueryBaseAndSize(&allocation_base, &size) ||
          allocation_base + heap->heap_base() != address || size != 0x3000) ++failures;
      if (!heap->Release(address)) ++failures;
    }
  };
  std::thread first(work), second(work);
  start.arrive_and_wait();
  first.join(); second.join();
  CHECK(failures.load() == 0);
  CHECK(heap->GetUnreservedPageCount() == free_before);
}
