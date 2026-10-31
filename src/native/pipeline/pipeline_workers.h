#pragma once
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <vector>
namespace x360n::native {
// Creates host pipelines off the recording thread. Drivers compile pipeline
// state objects concurrently; serial creation at first use stalled the render
// thread for the whole compile of every new pipeline in a batch (PERF-GPU-01:
// 500-562 ms per cold D3D12 PSO). Jobs run in submission order on idle workers;
// with zero workers Submit runs the job inline, as the synchronous path did.
// Results and exceptions travel through the returned shared_future.
class PipelineWorkers {
 public:
  explicit PipelineWorkers(unsigned threads);
  ~PipelineWorkers();
  PipelineWorkers(const PipelineWorkers&) = delete;
  PipelineWorkers& operator=(const PipelineWorkers&) = delete;
  template <class T>
  std::shared_future<T> Submit(std::function<T()> job) {
    auto task = std::make_shared<std::packaged_task<T()>>(std::move(job));
    std::shared_future<T> result = task->get_future().share();
    if (threads_.empty()) { (*task)(); return result; }
    {
      std::lock_guard lock(mutex_);
      jobs_.emplace_back([task] { (*task)(); });
    }
    wake_.notify_one();
    return result;
  }
  unsigned threads() const { return unsigned(threads_.size()); }
  // Default worker count: three quarters of the logical processors, 1..16.
  // Creation scaled up to the logical processor count in the cold pipeline
  // probe (4-CPU host: 2/3/4/8 workers took 0.52/0.37/0.34/0.34 of the
  // synchronous time); the rest stays with the title's own threads during
  // compile bursts. Idle workers only wait on a condition variable.
  static unsigned DefaultThreads();
 private:
  void Run();
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<std::function<void()>> jobs_;
  bool stop_ = false;
  std::vector<std::thread> threads_;
};
}
