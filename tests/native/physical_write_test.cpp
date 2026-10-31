#include "../../src/native/memory/physical_write.h"
#include "../../src/native/memory/guest_memory_mirror.h"
#include <catch2/catch_test_macros.hpp>

using namespace x360n::native;
TEST_CASE("Native physical packet writes retire watches in every allocated alias", "[native-memory]") {
  rex::memory::Memory memory;
  REQUIRE(memory.Initialize());
  const uint32_t allocated = memory.SystemHeapAlloc(4096,4096,rex::memory::kSystemHeapPhysical);
  REQUIRE(allocated);
  for (auto [address,size] : {std::pair{allocated,4096u},std::pair{0xA4000000u,65536u},
                             std::pair{0xC8000000u,16u<<20}}) {
    if (address != allocated)
      REQUIRE(memory.LookupHeap(address)->AllocFixed(address,size,size,
          rex::memory::kMemoryAllocationReserve|rex::memory::kMemoryAllocationCommit,
          rex::memory::kMemoryProtectRead|rex::memory::kMemoryProtectWrite));
    struct Watch { uint32_t address; unsigned calls=0; } watch{memory.GetPhysicalAddress(address)};
    auto* handle = memory.RegisterPhysicalMemoryInvalidationCallback(
        [](void* context,uint32_t first,uint32_t length,bool) {
          auto& watch=*static_cast<Watch*>(context);
          if (first<=watch.address && uint64_t(first)+length>watch.address) ++watch.calls;
          return std::pair<uint32_t,uint32_t>{0,UINT32_MAX};
        },&watch);
    memory.EnablePhysicalMemoryAccessCallbacks(watch.address,4,true,false);
    NotifyPhysicalWrite(memory,watch.address,4);
    CHECK(watch.calls==1);
    NotifyPhysicalWrite(memory,watch.address,4);
    CHECK(watch.calls==1); // one-shot until a reader rearms the watch
    memory.UnregisterPhysicalMemoryInvalidationCallback(handle);
  }
  unsigned uploads=0;
  GuestMemoryMirror mirror(memory,[&](uint32_t,uint32_t){++uploads;});
  const auto physical=memory.GetPhysicalAddress(allocated);
  mirror.RequestRange(physical,4096);
  std::vector<uint64_t> old;
  mirror.RangeWrittenByGpu(physical,4,&old);
  *memory.TranslatePhysical<uint32_t*>(physical)=0x12345678;
  NotifyPhysicalWrite(memory,physical,4);
  CHECK_FALSE(mirror.ReadbackCurrent(physical,physical,old));
  mirror.RequestRange(physical,4096);
  const uint32_t stale=0xDEADBEEF;
  mirror.CopyReadbackToGuest(physical,reinterpret_cast<const uint8_t*>(&stale),4,old);
  CHECK(*memory.TranslatePhysical<uint32_t*>(physical)==0x12345678);
  CHECK(uploads==2);
}
