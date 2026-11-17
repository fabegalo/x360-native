#include <catch2/catch_test_macros.hpp>
#include <rex/platform/perf.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>
#include <atomic>

TEST_CASE("Performance counters are optional and preserve concurrent totals", "[sdk][project-perf]") {
  using namespace rex::platform;
  const auto counter = [](const char* name, CounterType type = CounterType::kAccumulator) {
    auto id = RegisterCounter(name, type); REQUIRE(id); return *id;
  };
  InitCounters(); SetCountersEnabled(false);
  REQUIRE_FALSE(CountersEnabled());
  IncrementCounter(counter("draw_calls"), 42);
  REQUIRE(GetCounter(counter("draw_calls")) == 0);
  SetCountersEnabled(true);
  const auto draws = counter("draw_calls");
  std::vector<std::thread> workers;
  for (int t = 0; t < 4; ++t) workers.emplace_back([&] {
    for (int i = 0; i < 10000; ++i) IncrementCounter(draws);
  });
  for (auto& worker : workers) worker.join();
  SetCounter(counter("active_threads", CounterType::kGauge), 4);
  ResetFrameCounters();
  REQUIRE(GetSnapshotCounter(counter("draw_calls")) == 40000);
  REQUIRE(GetCounter(counter("draw_calls")) == 0);
  REQUIRE(GetCounter(counter("active_threads", CounterType::kGauge)) == 4);
  ResetFrameCounters();
  REQUIRE(GetSnapshotCounter(counter("draw_calls")) == 0);
  REQUIRE(GetSnapshotCounter(counter("active_threads", CounterType::kGauge)) == 4);
  auto path = std::filesystem::temp_directory_path() / "x360n-perf-test.csv";
  counter("shader_translation_us"); counter("shader_translations");
  SetCSVLogPath(path.string());
  { ScopedCounterTimer timer("shader_translation_us", "shader_translations"); }
  ResetFrameCounters();
  WriteCSVFrame();
  CloseCSV();
  std::ifstream input(path);
  std::string header, row;
  std::getline(input, header); std::getline(input, row);
  REQUIRE(header.starts_with("runtime_elapsed_us,process_cpu_us,frame_time_us"));
  REQUIRE(header.find("shader_translation_us") != std::string::npos);
  REQUIRE_FALSE(row.empty());
  REQUIRE(GetSnapshotCounter(counter("shader_translations")) == 1);
  input.close(); std::filesystem::remove(path);
  SetCountersEnabled(false);
}

TEST_CASE("Performance snapshots retain events from concurrent writers", "[sdk][project-perf]") {
  using namespace rex::platform;
  const auto counter = [](const char* name, CounterType type = CounterType::kAccumulator) {
    auto id = RegisterCounter(name, type); REQUIRE(id); return *id;
  };
  InitCounters(); SetCountersEnabled(false); SetCountersEnabled(true);
  const CounterID ids[] = {counter("draw_calls"), counter("xma_frames_decoded"),
      counter("functions_dispatched"), counter("interrupt_dispatches")};
  std::atomic<int> done{0};
  std::vector<std::thread> workers;
  for (auto id : ids) workers.emplace_back([&, id] {
    for (int i = 0; i < 100000; ++i) IncrementCounter(id);
    done.fetch_add(1);
  });
  int64_t totals[4]{};
  auto collect = [&] {
    ResetFrameCounters();
    for (int i = 0; i < 4; ++i) totals[i] += GetSnapshotCounter(ids[i]);
  };
  while (done.load() < 4) { collect(); std::this_thread::yield(); }
  for (auto& worker : workers) worker.join();
  collect();
  for (auto total : totals) REQUIRE(total == 100000);
  SetCountersEnabled(false);
}
