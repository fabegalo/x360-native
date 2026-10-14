#include <catch2/catch_test_macros.hpp>
#include <rex/perf/counter.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

TEST_CASE("Performance counters are optional and preserve concurrent totals", "[perf]") {
  using namespace rex::perf;
  Init();
  REQUIRE_FALSE(IsEnabled());
  IncrementCounter(CounterId::kDrawCalls, 42);
  REQUIRE(GetCounter(CounterId::kDrawCalls) == 0);
  SetEnabled(true);
  std::vector<std::thread> workers;
  for (int t = 0; t < 4; ++t) workers.emplace_back([] {
    for (int i = 0; i < 10000; ++i) IncrementCounter(CounterId::kDrawCalls);
  });
  for (auto& worker : workers) worker.join();
  SetCounter(CounterId::kActiveThreads, 4);
  ResetFrameCounters();
  REQUIRE(GetSnapshotCounter(CounterId::kDrawCalls) == 40000);
  REQUIRE(GetCounter(CounterId::kDrawCalls) == 0);
  REQUIRE(GetCounter(CounterId::kActiveThreads) == 4);
  ResetFrameCounters();
  REQUIRE(GetSnapshotCounter(CounterId::kDrawCalls) == 0);
  REQUIRE(GetSnapshotCounter(CounterId::kActiveThreads) == 4);
  auto path = std::filesystem::temp_directory_path() / "x360n-perf-test.csv";
  SetCsvLogPath(path.string());
  { ScopedCounterTimer timer(CounterId::kShaderTranslationUs, CounterId::kShaderTranslations); }
  ResetFrameCounters();
  WriteCsvFrame();
  FlushCsv();
  std::ifstream input(path);
  std::string header, row;
  std::getline(input, header); std::getline(input, row);
  REQUIRE(header.starts_with("runtime_elapsed_us,process_cpu_us,frame_time_us"));
  REQUIRE(header.find("shader_translation_us") != std::string::npos);
  REQUIRE_FALSE(row.empty());
  REQUIRE(GetSnapshotCounter(CounterId::kShaderTranslations) == 1);
  input.close(); std::filesystem::remove(path);
  SetEnabled(false);
}
