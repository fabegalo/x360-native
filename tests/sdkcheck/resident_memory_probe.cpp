// Real SharedMemory service without a GPU. Operation costs are not frame rates.
#include <rex/graphics/shared_memory.h>
#include <rex/system/xmemory.h>
#include <rex/thread/mutex.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>

using rex::graphics::SharedMemory;
namespace {
void Require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
class Mirror final : public SharedMemory {
 public:
  explicit Mirror(rex::memory::Memory& memory) : SharedMemory(memory) { InitializeCommon(); }
  uint64_t uploads = 0;
 private:
  bool UploadRanges(const std::vector<std::pair<uint32_t, uint32_t>>& ranges) override {
    for (auto [page, count] : ranges) {
      ++uploads;
      MakeRangeValid(page << page_size_log2(), count << page_size_log2(), false);
    }
    return true;
  }
};
constexpr uint32_t kBase = 0x01000000;
const uint32_t kPage = uint32_t(rex::memory::page_size()), kBytes = 512*kPage;
void Contracts(Mirror& mirror) {
  Require(mirror.RequestRange(kBase, kBytes), "initial upload");
  // Cover both edge masks, complete words, disjoint and unsorted ranges.
  const auto uploads = mirror.uploads;
  for (uint32_t first = 0; first < 128; ++first)
    for (uint32_t count = 1; count <= 128; ++count)
      Require(mirror.RequestRange(kBase+first*kPage+7, count*kPage-13), "resident edge masks");
  const std::pair<uint32_t,uint32_t> ranges[]{{kBase+128*kPage,64*kPage},{kBase+16,512},{kBase+64*kPage,1}};
  Require(mirror.RequestRanges(ranges,3) && mirror.uploads == uploads, "resident batch reuploaded");
  const std::pair<uint32_t,uint32_t> invalid[]{{kBase,4096},{SharedMemory::kBufferSize-1,2}};
  Require(!mirror.RequestRanges(invalid,2) && mirror.uploads == uploads, "invalid batch accepted");

  std::mutex mutex; std::condition_variable cv; bool held=false,release=false,timed_out=false;
  std::jthread holder([&] {
    auto region = rex::thread::global_critical_region::AcquireDirect();
    std::unique_lock lock(mutex); held=true; cv.notify_all();
    timed_out=!cv.wait_for(lock,std::chrono::seconds(2),[&]{return release;});
  });
  { std::unique_lock lock(mutex); cv.wait(lock,[&]{return held;}); }
  Require(mirror.RequestRanges(ranges,3), "resident request under contention");
  { std::lock_guard lock(mutex); release=true; } cv.notify_all(); holder.join();
  Require(!timed_out, "resident request waited for guest-fault critical region");

  // CPU invalidation may race the lock-free reads. Only this thread requests
  // uploads; writers still serialize validity and watches in the region.
  std::atomic<bool> done=false;
  std::jthread writer([&] {
    for (unsigned i=0;i<10000;++i) mirror.MemoryInvalidationCallback(kBase+(i%128)*kPage,4,true);
    done.store(true);
  });
  while (!done.load()) Require(mirror.RequestRange(kBase, 128*kPage), "concurrent invalidation request");
  writer.join();
  mirror.MemoryInvalidationCallback(kBase,4096,true);
  const auto before=mirror.uploads;
  Require(mirror.RequestRange(kBase,4096) && mirror.uploads==before+1, "CPU invalidation lost");
  mirror.RangeWrittenByGpu(kBase,4096);
  const auto gpu_uploads=mirror.uploads;
  mirror.SetSystemPageBlocksValidWithGpuDataWritten();
  Require(mirror.RequestRange(kBase,4096) && mirror.uploads==gpu_uploads, "GPU data overwritten");
  Require(mirror.RequestRange(kBase+kPage,4096), "CPU page upload");
  mirror.SetSystemPageBlocksValidWithGpuDataWritten();
  const auto refreshed=mirror.uploads;
  Require(mirror.RequestRange(kBase+kPage,4096) && mirror.uploads==refreshed+1, "CPU frame refresh lost");
  mirror.InvalidateAllPages();
  const auto cleared=mirror.uploads;
  Require(mirror.RequestRange(kBase,4096) && mirror.uploads==cleared+1, "explicit invalidation lost");
  std::cout << "PASS resident masks/batches, contention, concurrent invalidation, GPU retention and CPU refresh\n";
}
void Benchmark(Mirror& mirror) {
  for (unsigned mode=0;mode<4;++mode) {
    const unsigned iterations=mode==3 ? 10000 : 500000;
    Require(mirror.RequestRange(kBase,kBytes), "benchmark warmup");
    const auto before=mirror.uploads;
    const auto start=std::chrono::steady_clock::now();
    for (unsigned i=0;i<iterations;++i) {
      const uint32_t bytes=mode==1 ? 1<<20 : 4096;
      if (mode==3) mirror.MemoryInvalidationCallback(kBase,4096,true);
      if (mode==2) {
        const std::pair<uint32_t,uint32_t> ranges[]{{kBase+65536,4096},{kBase,4096},{kBase+131072,4096}};
        Require(mirror.RequestRanges(ranges,3), "benchmark batch");
      } else Require(mirror.RequestRange(kBase,bytes), "benchmark request");
    }
    const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    std::cout << "{\"mode\":"<<mode<<",\"iterations\":"<<iterations<<",\"ns\":"<<ns
              <<",\"uploads\":"<<mirror.uploads-before<<"}\n";
  }
}
}
int main(int argc,char** argv) {
  try {
    rex::memory::Memory memory;
    Require(memory.Initialize(), "memory initialization");
    Require(memory.LookupHeap(0xA0000000|kBase)->AllocFixed(0xA0000000|kBase,kBytes,kPage,
        rex::memory::kMemoryAllocationReserve|rex::memory::kMemoryAllocationCommit,
        rex::memory::kMemoryProtectRead|rex::memory::kMemoryProtectWrite), "physical allocation");
    Mirror mirror(memory);
    if (argc>1 && std::string_view(argv[1])=="--benchmark") Benchmark(mirror);
    else Contracts(mirror);
    return 0;
  } catch (const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<'\n'; return 1; }
}
