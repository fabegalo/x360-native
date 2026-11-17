#include <rex/kernel/heap.h>
#include "kernel/kernel_fixture.h"
#include "kernel/title_context.h"
#include "kernel/title_state.h"
#include "../../src/native/memory/physical_write.h"
#include "../../src/native/memory/guest_memory_mirror.h"
#include <catch2/catch_test_macros.hpp>

using namespace x360n::native;
TEST_CASE("Native physical packet writes retire watches in every allocated alias", "[sdk][native-memory]") {
  rex::test::KernelFixture fixture;
  auto& memory = *rex::kernel::StateOf(fixture.GetRuntime()).GetAddressSpace();
  rex::kernel::TitleContext context(fixture.GetRuntime());
  const uint32_t allocated = memory.SystemHeapAlloc(4096,4096,rex::kernel::kSystemHeapPhysical);
  REQUIRE(allocated);
  for (auto [address,size] : {std::pair{allocated,4096u},std::pair{0xA4000000u,65536u},
                             std::pair{0xC8000000u,16u<<20}}) {
    if (address != allocated)
      REQUIRE(memory.LookupHeap(address)->AllocFixed(address,size,size,
          rex::kernel::kMemoryAllocationReserve|rex::kernel::kMemoryAllocationCommit,
          rex::kernel::kMemoryProtectRead|rex::kernel::kMemoryProtectWrite));
    struct Watch final : rex::IPhysicalWriteListener {
      uint32_t address; unsigned calls = 0;
      explicit Watch(uint32_t at) : address(at) {}
      rex::PhysicalRange OnPhysicalWrite(rex::PhysicalRange range, bool) override {
        if (range.address <= address && uint64_t(range.address) + range.length > address) ++calls;
        return {0, UINT32_MAX};
      }
    } watch(memory.GetPhysicalAddress(address));
    auto registration = context.AddWriteListener(watch);
    context.WatchPhysicalWrites({watch.address, 4});
    NotifyPhysicalWrite(context,watch.address,4);
    CHECK(watch.calls==1);
    NotifyPhysicalWrite(context,watch.address,4);
    CHECK(watch.calls==1); // one-shot until a reader rearms the watch
    registration.Reset();
  }
  unsigned uploads=0;
  GuestMemoryMirror mirror(context, context,[&](uint32_t,uint32_t){++uploads;});
  const auto physical=memory.GetPhysicalAddress(allocated);
  mirror.RequestRange(physical,4096);
  std::vector<uint64_t> old;
  mirror.RangeWrittenByGpu(physical,4,&old);
  *memory.TranslatePhysical<uint32_t*>(physical)=0x12345678;
  NotifyPhysicalWrite(context,physical,4);
  CHECK_FALSE(mirror.ReadbackCurrent(physical,physical,old));
  mirror.RequestRange(physical,4096);
  const uint32_t stale=0xDEADBEEF;
  mirror.CopyReadbackToGuest(physical,reinterpret_cast<const uint8_t*>(&stale),4,old);
  CHECK(*memory.TranslatePhysical<uint32_t*>(physical)==0x12345678);
  CHECK(uploads==2);
}
