#include "pipeline_workers.h"
#include <algorithm>
namespace x360n::native {
PipelineWorkers::PipelineWorkers(unsigned threads) {
  threads_.reserve(threads);
  for (unsigned i = 0; i < threads; ++i) threads_.emplace_back([this] { Run(); });
}
PipelineWorkers::~PipelineWorkers() {
  {
    std::lock_guard lock(mutex_);
    stop_ = true;
  }
  wake_.notify_all();
  // Jobs not started yet are dropped (their futures report broken_promise):
  // teardown does not wait for compiles nobody will bind. Running jobs finish.
  for (auto& thread : threads_) thread.join();
  jobs_.clear();
}
void PipelineWorkers::Run() {
  for (;;) {
    std::function<void()> job;
    {
      std::unique_lock lock(mutex_);
      wake_.wait(lock, [this] { return stop_ || !jobs_.empty(); });
      if (stop_) return;
      job = std::move(jobs_.front());
      jobs_.pop_front();
    }
    job();
  }
}
unsigned PipelineWorkers::DefaultThreads() {
  return std::clamp(std::thread::hardware_concurrency() * 3 / 4, 1u, 16u);
}
}
