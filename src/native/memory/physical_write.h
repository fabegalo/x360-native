#pragma once
#include <rex/system/xmemory.h>
#include <rex/thread/mutex.h>
#include <algorithm>
#include <cstdint>

namespace x360n::native {
// Physical stores bypass the protected guest views. A watch is armed only in
// accessible aliases, so notifying just A0000000 misses 4 KiB system allocations
// in E0000000 (whose physical origin is offset by 0x1000).
inline void NotifyPhysicalWrite(rex::memory::Memory& memory, uint32_t address, uint32_t length) {
  const uint64_t end = uint64_t(address) + length;
  for (uint32_t base : {0xA0000000u, 0xC0000000u, 0xE0000000u}) {
    auto* heap = memory.LookupHeap(base);
    const uint32_t origin = memory.GetPhysicalAddress(base);
    const uint64_t first = std::max<uint64_t>(address, origin);
    const uint64_t last = std::min<uint64_t>(end, uint64_t(origin) + heap->heap_size());
    if (first < last)
      memory.TriggerPhysicalMemoryCallbacks(rex::thread::global_critical_region::AcquireDirect(),
          base + uint32_t(first - origin), uint32_t(last - first), true, true);
  }
}
}
