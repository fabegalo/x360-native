#pragma once

#include <cstring>
#include <rex/system/xmemory.h>

namespace dash17559 {

// Compatibility hypothesis only. Xenia Canary 2372a54105d9d598ab5bc7ba94a72ef1bea4b271,
// kernel_state.cc / kernel_state.h: fixed dashboard page, actual data unknown.
// One instance per runtime; called during setup before guest threads start.
class ExperimentalRegion {
 public:
  static constexpr uint32_t kBase = 0x8E030000;
  static constexpr uint32_t kSize = 0x10000;
  enum class Result { Disabled, Mapped, AlreadyMapped, Overlap, AllocationFailed };

  Result Map(rex::memory::Memory& memory, bool enabled) {
    using namespace rex::memory;
    if (!enabled) return Result::Disabled;
    if (mapped_) return Result::AlreadyMapped;
    auto* heap = memory.LookupHeap(kBase);
    HeapAllocationInfo info{};
    if (!heap || !heap->QueryRegionInfo(kBase, &info)) return Result::AllocationFailed;
    if (info.state || info.region_size < kSize) return Result::Overlap;
    // Reserve separately: the SDK's reserve|commit path permits existing pages.
    if (!heap->AllocFixed(kBase, kSize, kSize, kMemoryAllocationReserve,
                          kMemoryProtectRead | kMemoryProtectWrite))
      return Result::AllocationFailed;
    if (!heap->AllocFixed(kBase, kSize, kSize, kMemoryAllocationCommit,
                          kMemoryProtectRead | kMemoryProtectWrite)) {
      heap->Release(kBase);
      return Result::AllocationFailed;
    }
    std::memset(memory.TranslateVirtual(kBase), 0, kSize);
    mapped_ = true;
    return Result::Mapped;
  }

 private:
  bool mapped_ = false;
};
}  // namespace dash17559
