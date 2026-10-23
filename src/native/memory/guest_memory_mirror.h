#pragma once
// Page validity and invalidation of the GPU-resident guest physical memory
// mirror. Adapted from the pinned ReXGlue SharedMemory (Xenia, BSD) with the
// ShiftGlue 1e8968f7 changes (BSD, Tom Clay): lock-free valid fast path,
// watches retired before their callback, readback restricted to pages still
// valid. See THIRD_PARTY_NOTICES.md.
#include <rex/memory.h>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <vector>
namespace x360n::native {
class GuestMemoryMirror {
 public:
  static constexpr uint32_t kSize = 1u << 29;
  // Records a copy of guest bytes [start, start + length) into the mirror.
  using Upload = std::function<void(uint32_t start, uint32_t length)>;
  GuestMemoryMirror(rex::memory::Memory&, Upload);
  ~GuestMemoryMirror();
  GuestMemoryMirror(const GuestMemoryMirror&) = delete;
  GuestMemoryMirror& operator=(const GuestMemoryMirror&) = delete;
  // The process-wide region guest invalidations hold (recursive).
  static std::unique_lock<std::recursive_mutex> Lock();
  // One-shot: called under Lock() when a CPU write or a GPU write (resolve)
  // touches the range, after the watch is retired. Must not watch/unwatch.
  using WatchCallback = void (*)(void* context, void* data, uint64_t argument, bool by_gpu);
  using WatchHandle = void*;
  // Call under Lock().
  WatchHandle Watch(uint32_t start, uint32_t length, WatchCallback, void* context, void* data, uint64_t argument);
  void Unwatch(WatchHandle);
  // Makes the range current in the mirror, uploading pages the CPU wrote
  // since they were last valid. Command processor thread only.
  void RequestRange(uint32_t start, uint32_t length);
  // A GPU write (resolve) now holds the authoritative bytes of the range:
  // fires watches, marks pages valid and GPU-written, and watches CPU writes.
  // RequestRange the range first so unwritten bytes of its pages are current.
  // Optional per-page CPU epochs accompany a deferred readback. A later CPU
  // write must invalidate old snapshots even if another draw revalidates the page.
  void RangeWrittenByGpu(uint32_t start, uint32_t length, std::vector<uint64_t>* cpu_generations = nullptr,
                         std::span<const uint64_t> snapshot_generations = {});
  std::vector<uint64_t> CaptureCpuGenerations(uint32_t start, uint32_t length);
  // Publishes GPU results to guest memory, except pages the CPU wrote after
  // the GPU write made them valid (the newer CPU bytes win).
  void CopyReadbackToGuest(uint32_t start, const uint8_t* source, uint32_t length,
                           std::span<const uint64_t> cpu_generations = {});
  // Same predicate as canonical publication, also used by auxiliary scaled
  // resolve snapshots. Epochs originate inside RangeWrittenByGpu's lock.
  bool ReadbackCurrent(uint32_t address, uint32_t snapshot_start,
                       std::span<const uint64_t> cpu_generations);
  // Frame boundary: pages valid only because they were uploaded are reloaded
  // on their next request (pinned clear_memory_page_state contract).
  void InvalidateCpuPages();
  uint64_t uploaded_bytes() const { return uploaded_bytes_; }
 private:
  struct WatchRange {
    WatchCallback callback; void* context; void* data; uint64_t argument;
    uint32_t page_first, page_last, bucket_refs;
    bool alive;
  };
  static constexpr uint32_t kBucketLog2 = 22;
  static constexpr uint32_t kBuckets = kSize >> kBucketLog2;
  static std::pair<uint32_t, uint32_t> InvalidationThunk(void*, uint32_t, uint32_t, bool);
  std::pair<uint32_t, uint32_t> Invalidate(uint32_t start, uint32_t length, bool exact);
  void MakeValid(uint32_t start, uint32_t length, bool by_gpu, std::vector<uint64_t>* cpu_generations = nullptr,
                 std::span<const uint64_t> snapshot_generations = {});
  void Fire(uint32_t page_first, uint32_t page_last, bool by_gpu);
  void Release(WatchRange*);
  rex::memory::Memory& memory_;
  Upload upload_;
  uint32_t page_log2_;
  void* invalidation_handle_ = nullptr;
  // Writers hold Lock(); all live accesses to valid_ use atomic_ref because
  // RequestRange also reads it without the lock. Storage is fixed after ctor.
  std::vector<uint64_t> valid_, gpu_written_;
  // Guarded by Lock(), incremented only on CPU invalidation, never revalidation.
  std::vector<uint64_t> cpu_generations_;
  std::vector<WatchRange*> buckets_[kBuckets];
  std::vector<std::pair<uint32_t, uint32_t>> uploads_;
  uint64_t uploaded_bytes_ = 0;
};
}
