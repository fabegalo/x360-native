#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <sstream>
#include <thread>
#include <rex/platform/perf.h>
#include <rex/string/text.h>
#include <rex/utility.h>
#include "base/core_settings.h"
#include "codegen/tool_runtime.h"
#include "object_app.h"
#include "temp_folder.h"

namespace {
struct Counters {
  Counters() {
    REQUIRE(rex::GetPerfSettings().counters.Reset());
    REQUIRE(rex::GetPerfSettings().log_csv.Reset());
    rex::platform::InitCounters();
  }
  ~Counters() {
    rex::platform::CloseCSV();
    rex::platform::SetCountersEnabled(false);
    (void)rex::GetPerfSettings().counters.Reset();
    (void)rex::GetPerfSettings().log_csv.Reset();
  }
};
std::vector<std::string> Lines(const std::filesystem::path& path) {
  std::ifstream file(path);
  std::vector<std::string> lines;
  for (std::string line; std::getline(file, line);) lines.push_back(std::move(line));
  return lines;
}
class CaptureApp final : public rex::test::ObjectApp<rex::codegen::ToolApp> {
 public:
  CaptureApp(const std::filesystem::path& app_root, const std::filesystem::path& csv)
      : ObjectApp("optional_perf_capture", app_root), csv_(csv) {}
  bool observed_enabled = false;
 protected:
  rex::Result<void> OnConfigure(rex::SettingsRegistry& registry) override {
    REX_TRY_VOID(ToolApp::OnConfigure(registry));
    REX_TRY_VOID(rex::GetPerfSettings().log_csv.SetText(rex::SettingLayer::kTitle, rex::string::PathToUTF8(csv_)));
    const auto probe = rex::platform::RegisterCounter("startup_capture_probe");
    if (!probe) return std::unexpected(probe.error());
    return rex::Ok();
  }
  rex::Result<void> OnInit() override {
    observed_enabled = rex::platform::CountersEnabled() && rex::platform::CSVLoggingActive();
    REX_PERF_SET("startup_capture_probe", 7);
    rex::platform::Profiler::EndFrame();
    return rex::Ok();
  }
 private:
  std::filesystem::path csv_;
};
}

TEST_CASE("Optional counters start off and do not evaluate disabled instrumentation payloads", "[optional-perf]") {
  Counters reset;
  using namespace rex::platform;
  CHECK_FALSE(CountersEnabled()); CHECK_FALSE(CSVLoggingActive());
  int evaluated = 0;
  REX_PERF_SET("frame_time_us", ++evaluated);
  SetCounter(CounterID::kFps, 123);
  IncrementCounter(CounterID::kFps);
  ResetFrameCounters();
  CHECK(evaluated == 0); CHECK(GetCounter(CounterID::kFps) == 0);
  CHECK(GetSnapshotCounter(CounterID::kFps) == 0);
  SetCountersEnabled(true);
  REX_PERF_SET("frame_time_us", ++evaluated);
  CHECK(evaluated == 1); CHECK(GetCounter(CounterID::kFrameTimeUs) == 1);
  ResetFrameCounters(); CHECK(GetSnapshotCounter(CounterID::kFrameTimeUs) == 1);
  SetCountersEnabled(false);
  REX_PERF_SET("frame_time_us", ++evaluated);
  CHECK(evaluated == 1);
}

TEST_CASE("Optional scoped timers count completed scopes only while enabled", "[optional-perf]") {
  Counters reset;
  using namespace rex::platform;
  { ScopedCounterTimer off("frame_time_us", "fps"); }
  CHECK(GetCounter(CounterID::kFps) == 0);
  SetCountersEnabled(true);
  {
    ScopedCounterTimer on("frame_time_us", "fps");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK(GetCounter(CounterID::kFps) == 1);
  CHECK(GetCounter(CounterID::kFrameTimeUs) > 0);
  SetCountersEnabled(false);
  { ScopedCounterTimer off("frame_time_us", "fps"); }
  CHECK(GetCounter(CounterID::kFps) == 1);
}

TEST_CASE("CSV capture owns optional counters and retains elapsed CPU and snapshot columns", "[optional-perf]") {
  Counters reset;
  rex::test::TempFolder folder("rex_optional_perf_");
  rex::ScopeGuard close([] { rex::platform::CloseCSV(); });
  const auto path = folder.path / std::filesystem::path(u8"capture \u00E7 # $.csv");
  using namespace rex::platform;
  SetCSVLogPath(rex::string::PathToUTF8(path));
  REQUIRE(CSVLoggingActive()); CHECK(CountersEnabled());
  SetCountersEnabled(false); CHECK(CountersEnabled());
  for (int frame = 1; frame <= 2; ++frame) {
    SetCounter(CounterID::kFrameTimeUs, frame * 1000);
    SetCounter(CounterID::kFps, 60);
    ResetFrameCounters(); WriteCSVFrame();
  }
  CloseCSV(); CHECK_FALSE(CSVLoggingActive()); CHECK_FALSE(CountersEnabled());
  const auto lines = Lines(path);
  REQUIRE(lines.size() == 3);
  CHECK(lines[0].starts_with("runtime_elapsed_us,process_cpu_us,frame_time_us,fps"));
  uint64_t previous = 0;
  for (size_t i = 1; i < lines.size(); ++i) {
    CHECK(std::count(lines[i].begin(), lines[i].end(), ',') == std::count(lines[0].begin(), lines[0].end(), ','));
    std::istringstream fields(lines[i]);
    std::string elapsed, cpu, duration, fps;
    REQUIRE(std::getline(fields, elapsed, ',')); REQUIRE(std::getline(fields, cpu, ','));
    REQUIRE(std::getline(fields, duration, ',')); REQUIRE(std::getline(fields, fps, ','));
    CHECK(std::stoull(elapsed) >= previous); previous = std::stoull(elapsed);
    CHECK_FALSE(cpu.empty()); CHECK(std::stoll(cpu) >= 0);
    CHECK(std::stoi(duration) == static_cast<int>(i) * 1000); CHECK(fps == "60");
  }
  SetCSVLogPath(rex::string::PathToUTF8(folder.path / "missing" / "capture.csv"));
  CHECK_FALSE(CSVLoggingActive()); CHECK_FALSE(CountersEnabled());
}

TEST_CASE("Core startup configures optional capture after counter registration", "[optional-perf]") {
  Counters reset;
  rex::test::TempFolder folder("rex_perf_startup_");
  const auto csv = folder.path / "startup.csv";
  {
    CaptureApp app(folder.path, csv);
    REQUIRE(app.Start());
    CHECK(app.observed_enabled);
    CHECK(app.Shutdown() == 0);
  }
  const auto lines = Lines(csv);
  REQUIRE(lines.size() == 2);
  CHECK(lines[0].find("startup_capture_probe") != std::string::npos);
  CHECK(std::count(lines[0].begin(), lines[0].end(), ',') == std::count(lines[1].begin(), lines[1].end(), ','));
  CHECK_FALSE(rex::platform::CSVLoggingActive());
}
