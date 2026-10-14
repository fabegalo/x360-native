// Real rex::perf calls, bounded synthetic workload. Not a game FPS benchmark.
#include <rex/perf/counter.h>
#include <algorithm>
#include <barrier>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

int main() {
  using namespace rex::perf;
  using Clock = std::chrono::steady_clock;
  constexpr int iterations = 1000000;
  constexpr CounterId ids[] = {CounterId::kDrawCalls, CounterId::kXmaFramesDecoded,
      CounterId::kFunctionsDispatched, CounterId::kInterruptDispatches};
  std::cout << "{\"iterations_per_thread\":" << iterations << ",\"trials\":[";
  bool first = true;
  for (bool enabled : {false, true}) for (int workers : {1, 4}) {
    for (int trial = 0; trial < 7; ++trial) {
      Init(); SetEnabled(enabled);
      std::barrier gate(workers + 1);
      std::vector<std::thread> threads;
      for (int t = 0; t < workers; ++t) threads.emplace_back([&, t] {
        gate.arrive_and_wait();
        for (int i = 0; i < iterations; ++i) IncrementCounter(ids[t]);
      });
      const auto start = Clock::now();
      gate.arrive_and_wait();
      for (auto& t : threads) t.join();
      const auto ns = std::chrono::duration<double, std::nano>(Clock::now() - start).count();
      for (int t = 0; t < workers; ++t)
        if (GetCounter(ids[t]) != (enabled ? iterations : 0)) return 2;
      if (!first) std::cout << ',';
      first = false;
      std::cout << "{\"enabled\":" << (enabled ? "true" : "false")
                << ",\"workers\":" << workers << ",\"trial\":" << trial
                << ",\"ns_per_event\":" << ns / (iterations * workers) << '}';
    }
  }
  std::cout << "]}\n";
  SetEnabled(false);
}
