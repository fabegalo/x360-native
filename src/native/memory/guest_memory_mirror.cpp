#include "guest_memory_mirror.h"
#include <rex/memory/utils.h>
#include <rex/system/xmemory.h>
#include <rex/thread/mutex.h>
#include <algorithm>
#include <atomic>
#include <bit>
#include <cstring>
#include <stdexcept>
namespace x360n::native {
namespace {
// Bits of 64-page block i that lie within [first, last].
uint64_t BlockMask(uint32_t i, uint32_t first, uint32_t last) {
  uint64_t mask = UINT64_MAX;
  if (i == first >> 6) mask &= UINT64_MAX << (first & 63);
  if (i == last >> 6 && (last & 63) != 63) mask &= (uint64_t(1) << ((last & 63) + 1)) - 1;
  return mask;
}
}
std::unique_lock<std::recursive_mutex> GuestMemoryMirror::Lock() {
  return rex::thread::global_critical_region::AcquireDirect();
}
GuestMemoryMirror::GuestMemoryMirror(rex::memory::Memory& memory, Upload upload)
    : memory_(memory), upload_(std::move(upload)) {
  page_log2_ = uint32_t(std::countr_zero(uint64_t(rex::memory::page_size())));
  const size_t blocks = ((kSize >> page_log2_) + 63) / 64;
  valid_.assign(blocks, 0);
  gpu_written_.assign(blocks, 0);
  cpu_generations_.assign(kSize >> page_log2_, 0);
  invalidation_handle_ = memory_.RegisterPhysicalMemoryInvalidationCallback(InvalidationThunk, this);
}
GuestMemoryMirror::~GuestMemoryMirror() {
  memory_.UnregisterPhysicalMemoryInvalidationCallback(invalidation_handle_);
  auto lock = Lock();
  for (auto& bucket : buckets_)
    for (auto* range : bucket) Release(range);
}
GuestMemoryMirror::WatchHandle GuestMemoryMirror::Watch(uint32_t start, uint32_t length, WatchCallback callback,
    void* context, void* data, uint64_t argument) {
  if (!length || start >= kSize) return nullptr;
  length = std::min(length, kSize - start);
  auto* range = new WatchRange{callback, context, data, argument, start >> page_log2_,
      (start + length - 1) >> page_log2_, 0, true};
  for (uint32_t b = (range->page_first << page_log2_) >> kBucketLog2; b <= (range->page_last << page_log2_) >> kBucketLog2; ++b) {
    buckets_[b].push_back(range); ++range->bucket_refs;
  }
  return range;
}
void GuestMemoryMirror::Unwatch(WatchHandle handle) {
  // Dead entries leave their buckets on the next scan of each bucket.
  if (handle) static_cast<WatchRange*>(handle)->alive = false;
}
void GuestMemoryMirror::Release(WatchRange* range) { if (!--range->bucket_refs) delete range; }
void GuestMemoryMirror::Fire(uint32_t page_first, uint32_t page_last, bool by_gpu) {
  auto lock = Lock();
  const uint32_t first = (page_first << page_log2_) >> kBucketLog2;
  const uint32_t last = std::min((page_last << page_log2_) >> kBucketLog2, kBuckets - 1);
  for (uint32_t b = first; b <= last; ++b) {
    auto& bucket = buckets_[b];
    size_t kept = 0;
    for (size_t i = 0; i < bucket.size(); ++i) {
      WatchRange* range = bucket[i];
      if (range->alive && page_first <= range->page_last && page_last >= range->page_first) {
        // Retired before the callback: a nested invalidation cannot re-enter it.
        range->alive = false;
        range->callback(range->context, range->data, range->argument, by_gpu);
      }
      if (range->alive) bucket[kept++] = range;
      else Release(range);
    }
    bucket.resize(kept);
  }
}
void GuestMemoryMirror::MakeValid(uint32_t start, uint32_t length, bool by_gpu, std::vector<uint64_t>* cpu_generations,
    std::span<const uint64_t> snapshot_generations) {
  const uint32_t first = start >> page_log2_, last = (start + length - 1) >> page_log2_;
  {
    auto lock = Lock();
    if (by_gpu) Fire(first, last, true);
    if (!snapshot_generations.empty() && snapshot_generations.size()!=last-first+1)
      throw std::logic_error("Native mirror: snapshot epoch extent mismatch");
    if (cpu_generations) {
      if (snapshot_generations.empty()) cpu_generations->assign(cpu_generations_.begin()+first,cpu_generations_.begin()+last+1);
      else cpu_generations->assign(snapshot_generations.begin(),snapshot_generations.end());
    }
    for (uint32_t i = first >> 6; i <= last >> 6; ++i) {
      const uint64_t mask = BlockMask(i, first, last);
      std::atomic_ref(valid_[i]).fetch_or(mask, std::memory_order_relaxed);
      gpu_written_[i] = by_gpu ? gpu_written_[i] | mask : gpu_written_[i] & ~mask;
      // A CPU write after a staged resolve snapshot wins, even if it preceded
      // publication of the GPU write. Keep that page invalid so the next GPU
      // reader uploads the newer CPU bytes after this recorded GPU work.
      if (!snapshot_generations.empty()) {
        uint64_t stale=0;
        for (uint32_t page=std::max(first,i*64);page<=std::min(last,i*64+63);++page)
          if (snapshot_generations[page-first]!=cpu_generations_[page]) stale|=uint64_t(1)<<(page&63);
        std::atomic_ref(valid_[i]).fetch_and(~stale,std::memory_order_relaxed);
        gpu_written_[i]&=~stale;
      }
    }
  }
  // Outside the region: heap locks are taken before it by guest faults.
  memory_.EnablePhysicalMemoryAccessCallbacks(first << page_log2_, (last - first + 1) << page_log2_, true, false);
}
void GuestMemoryMirror::RequestRange(uint32_t start, uint32_t length) {
  if (!length) return;
  if (start >= kSize || length > kSize - start) throw std::runtime_error("Native mirror request outside guest physical memory");
  const uint32_t first = start >> page_log2_, last = (start + length - 1) >> page_log2_;
  // Nearly every request finds its pages valid. The bitmap never reallocates
  // after construction. Atomic accesses avoid a C++ data race with CPU fault
  // callbacks; volatile alone does not synchronize those accesses. Only the
  // command processor consumes this bitmap and records uploads, so the bit
  // needs atomicity, not publication of another thread's resource contents.
  bool valid = true;
  for (uint32_t i = first >> 6; valid && i <= last >> 6; ++i) {
    const uint64_t mask = BlockMask(i, first, last);
    valid = (std::atomic_ref(valid_[i]).load(std::memory_order_relaxed) & mask) == mask;
  }
  if (valid) return;
  uploads_.clear();
  {
    auto lock = Lock();
    uint32_t open = UINT32_MAX;
    for (uint32_t page = first; page <= last; ++page) {
      const bool page_valid =
          (std::atomic_ref(valid_[page >> 6]).load(std::memory_order_relaxed) >> (page & 63)) & 1;
      if (!page_valid && open == UINT32_MAX) open = page;
      if (page_valid && open != UINT32_MAX) { uploads_.emplace_back(open, page - open); open = UINT32_MAX; }
    }
    if (open != UINT32_MAX) uploads_.emplace_back(open, last + 1 - open);
  }
  for (const auto& [page, count] : uploads_) {
    // Valid and write-watched before the copy: a CPU write during the copy
    // invalidates the page again instead of being lost.
    MakeValid(page << page_log2_, count << page_log2_, false);
    upload_(page << page_log2_, count << page_log2_);
    uploaded_bytes_ += uint64_t(count) << page_log2_;
  }
}
void GuestMemoryMirror::RangeWrittenByGpu(uint32_t start, uint32_t length, std::vector<uint64_t>* cpu_generations,
    std::span<const uint64_t> snapshot_generations) {
  if (!length || start >= kSize) return;
  length = std::min(length, kSize - start);
  MakeValid(start, length, true, cpu_generations, snapshot_generations);
}
void GuestMemoryMirror::CopyReadbackToGuest(uint32_t start, const uint8_t* source, uint32_t length,
    std::span<const uint64_t> cpu_generations) {
  if (!length || start >= kSize) return;
  length = std::min(length, kSize - start);
  const uint32_t page_size = 1u << page_log2_;
  const uint32_t first_page = start >> page_log2_;
  if (!cpu_generations.empty() && cpu_generations.size() != ((start + length - 1) >> page_log2_) - first_page + 1)
    throw std::runtime_error("Native readback: invalid CPU epoch extent");
  // Under the region: a guest write faulting on a still-valid page waits,
  // invalidates it and then lands after these bytes, so it wins as well.
  // The physical view written here carries no write watch.
  auto lock = Lock();
  for (uint32_t offset = 0; offset < length;) {
    const uint32_t address = start + offset;
    const uint32_t chunk = std::min(length - offset, page_size - (address & (page_size - 1)));
    const uint32_t page = address >> page_log2_;
    if (((std::atomic_ref(valid_[page >> 6]).load(std::memory_order_relaxed) >> (page & 63)) & 1) &&
        (cpu_generations.empty() || cpu_generations[page - first_page] == cpu_generations_[page]))
      std::memcpy(memory_.TranslatePhysical(address), source + offset, chunk);
    offset += chunk;
  }
}
std::vector<uint64_t> GuestMemoryMirror::CaptureCpuGenerations(uint32_t start, uint32_t length) {
  if (!length || uint64_t(start)+length>kSize) throw std::invalid_argument("Native mirror: snapshot outside physical memory");
  auto lock=Lock();
  return {cpu_generations_.begin()+(start>>page_log2_),cpu_generations_.begin()+((start+length-1)>>page_log2_)+1};
}
bool GuestMemoryMirror::ReadbackCurrent(uint32_t address, uint32_t snapshot_start,
    std::span<const uint64_t> cpu_generations) {
  if (address >= kSize || address < snapshot_start) return false;
  auto lock = Lock();
  const uint32_t page=address>>page_log2_, first=snapshot_start>>page_log2_;
  return page-first<cpu_generations.size() && cpu_generations[page-first]==cpu_generations_[page] &&
      ((std::atomic_ref(valid_[page>>6]).load(std::memory_order_relaxed)>>(page&63))&1);
}
void GuestMemoryMirror::InvalidateCpuPages() {
  auto lock = Lock();
  for (size_t i = 0; i < valid_.size(); ++i)
    std::atomic_ref(valid_[i]).store(gpu_written_[i], std::memory_order_relaxed);
}
std::pair<uint32_t, uint32_t> GuestMemoryMirror::InvalidationThunk(void* context, uint32_t start, uint32_t length, bool exact) {
  return static_cast<GuestMemoryMirror*>(context)->Invalidate(start, length, exact);
}
std::pair<uint32_t, uint32_t> GuestMemoryMirror::Invalidate(uint32_t start, uint32_t length, bool exact) {
  if (!length || start >= kSize) return {0, UINT32_MAX};
  length = std::min(length, kSize - start);
  uint32_t first = start >> page_log2_, last = (start + length - 1) >> page_log2_;
  auto lock = Lock();
  if (!exact) {
    // As the pinned SharedMemory: widen to the 64-page block, stopping at
    // GPU-written pages, so fewer access violations are taken.
    if (first & 63) {
      const uint64_t below = gpu_written_[first >> 6] & ((uint64_t(1) << (first & 63)) - 1);
      first = (first & ~63u) + (64 - std::countl_zero(below));
    }
    if ((last & 63) != 63) {
      const uint64_t above = gpu_written_[last >> 6] & ~((uint64_t(1) << ((last & 63) + 1)) - 1);
      last = (last & ~63u) + (std::max(std::countr_zero(above), 1) - 1);
    }
  }
  for (uint32_t i = first >> 6; i <= last >> 6; ++i) {
    const uint64_t mask = BlockMask(i, first, last);
    std::atomic_ref(valid_[i]).fetch_and(~mask, std::memory_order_relaxed);
    gpu_written_[i] &= ~mask;
  }
  for (uint32_t page = first; page <= last; ++page) ++cpu_generations_[page];
  Fire(first, last, false);
  return {first << page_log2_, (last - first + 1) << page_log2_};
}
}
