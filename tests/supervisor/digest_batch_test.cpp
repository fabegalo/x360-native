#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include "digest_batch.h"
#include "child_process.h"
#include "paths.h"
#include "session_window.h"
#include "session_notification.h"
#include "scratch.h"
#include <fstream>
#include <future>
#include <iostream>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
using namespace x360n;
namespace fs = std::filesystem;
using namespace std::chrono_literals;
TEST_CASE("verification paths preserve canonical spelling for case aliases", "[digest-batch]") {
  const auto root = UniqueTempPath("verification-case"); fs::create_directories(root);
  const auto file = root / "source.bin"; std::ofstream(file) << "original";
  const auto alias = root / "SOURCE.BIN";
  if (fs::exists(alias)) {
    DigestBatch batch;
    CHECK(VerificationPath(alias) == fs::weakly_canonical(alias));
    REQUIRE(DigestFile(alias));
    CHECK(VerificationPath(alias) == fs::weakly_canonical(alias));
  }
  std::error_code ec; fs::remove_all(root, ec);
}
TEST_CASE("verification paths retain ancestors only for their protected transaction", "[digest-batch]") {
  const auto root = UniqueTempPath("verification-path");
  const auto directory = root / fs::path(u8"João 日本");
  fs::create_directories(directory);
  const auto file = directory / "source.bin"; std::ofstream(file) << "original";
  {
    DigestBatch batch;
    CHECK_FALSE(HasProtectedVerificationPath(file));
    CHECK(VerificationPath(file) == fs::weakly_canonical(file));
    CHECK(HasProtectedVerificationPath(file));
    CHECK_FALSE(HasProtectedVerificationPath(directory / "." / "source.bin"));
    REQUIRE(DigestFile(file));
    CHECK(VerificationPath(file) == fs::weakly_canonical(file));
    CHECK(DigestFile(file)->sha256 == DigestFile(file)->sha256);
    CHECK(batch.files == 1);
    CHECK_FALSE(MoveFileExW(directory.c_str(), (root / "moved").c_str(), 0));
    CHECK_FALSE(MoveFileExW(root.c_str(), fs::path(root.native() + L"-moved").c_str(), 0));
  }
  REQUIRE(MoveFileExW(directory.c_str(), (root / "moved").c_str(), 0));
  CHECK_FALSE(HasProtectedVerificationPath(root / "moved/source.bin"));
  CHECK(VerificationPath(root / "moved/source.bin") == fs::weakly_canonical(root / "moved/source.bin"));
  std::error_code ec; fs::remove_all(root, ec);
}
TEST_CASE("reparse ancestors never receive ordinary-path approval", "[digest-batch]") {
  const auto root = UniqueTempPath("verification-junction");
  fs::create_directories(root / "target"); std::ofstream(root / "target/input") << "synthetic";
  wchar_t system[MAX_PATH]{}; GetSystemDirectoryW(system, MAX_PATH);
  ProcessSpec spec{fs::path(system) / "cmd.exe", {"/c", "mklink", "/J", PathUtf8(root / "alias"), PathUtf8(root / "target")},
      root, root / "out", root / "err", 3s};
  spec.no_console = true; REQUIRE(RunProcess(spec).exit_code == 0);
  {
    DigestBatch batch;
    const auto alias = root / "alias/input";
    CHECK(VerificationPath(alias) == fs::weakly_canonical(alias));
    CHECK_FALSE(HasProtectedVerificationPath(alias));
    REQUIRE(DigestFile(alias));
    CHECK_FALSE(HasProtectedVerificationPath(alias));
  }
  std::error_code ec; fs::remove(root / "alias", ec); fs::remove_all(root, ec);
}
TEST_CASE("digest batches retain byte identity only while writes and replacement are excluded", "[digest-batch]") {
  const auto dir = UniqueTempPath("digest-batch"); fs::create_directories(dir);
  const auto file = dir / "input.bin";
  std::ofstream(file) << "first";
  const auto timestamp = fs::last_write_time(file);
  const auto expected = DigestFile(file)->sha256;
  const auto alias = dir / "." / "input.bin";
  {
    DigestBatch batch;
    REQUIRE(DigestFile(file)->sha256 == expected);
    CHECK(DigestFile(file)->sha256 == expected);
    CHECK(batch.files == 1); CHECK(batch.hits == 1);
    const auto writer = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(writer == INVALID_HANDLE_VALUE);
    if (writer != INVALID_HANDLE_VALUE) CloseHandle(writer);
    CHECK_FALSE(DeleteFileW(file.c_str()));
    CHECK(DigestFile(alias)->sha256 == expected); CHECK(batch.files == 1);
    CHECK_FALSE(DigestFile(dir / "absent"));
    auto other = std::async(std::launch::async, [&] { DigestBatch independent; DigestFile(file); return independent.files; });
    CHECK(other.get() == 1); // No global or cross-thread approval cache.
  }
  std::ofstream(file) << "other"; fs::last_write_time(file, timestamp);
  {
    DigestBatch batch;
    CHECK(DigestFile(file)->sha256 != expected);
    CHECK(batch.files == 1);
  }
  bool cancel = false;
  {
    DigestBatch batch([&] { return cancel; });
    DigestFile(file); cancel = true;
    CHECK_THROWS_AS(DigestFile(file), VerificationCancelled);
  }
  std::error_code ec; fs::remove_all(dir, ec);
}
TEST_CASE("fullscreen belongs to the persistent session frame", "[session-fullscreen]") {
  SessionWindow window(false, 0, true);
  const auto hwnd = reinterpret_cast<HWND>(window.handle());
  REQUIRE(IsWindow(hwnd));
  CHECK_FALSE(IsWindowVisible(hwnd));
  CHECK((GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_OVERLAPPEDWINDOW) == 0);
  MONITORINFO monitor{sizeof(monitor)};
  REQUIRE(GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor));
  RECT bounds{}; REQUIRE(GetWindowRect(hwnd, &bounds));
  CHECK(EqualRect(&bounds, &monitor.rcMonitor));
  CHECK_FALSE(window.closed());
  // No dashboard or game is executed by this window-contract test.
}
TEST_CASE("session close event interrupts only its owned process in plain and crash capture modes", "[session-event]") {
  const auto dir = UniqueTempPath("session-event"); fs::create_directories(dir);
  for (const bool debug : {false, true}) {
    SessionNotification closed({}, true);
    SessionWindow window(false, closed.handle());
    const auto hwnd = reinterpret_cast<HWND>(window.handle());
    ProcessSpec unrelated{X360N_TEST_CHILD, {"sleep", "600"}, dir, dir / "other.out", dir / "other.err", 3s};
    unrelated.no_console = true;
    auto other = std::async(std::launch::async, [&] { return RunProcess(unrelated); });
    ProcessSpec spec{X360N_TEST_CHILD, {"sleep", "20000"}, dir, dir / "out", dir / "err", 5s};
    spec.no_console = true; spec.cancel_event = window.close_event(); spec.capture_crash = debug;
    std::promise<void> ready; auto started = ready.get_future();
    spec.on_started = [&] { ready.set_value(); };
    auto child = std::async(std::launch::async, [&] { return RunProcess(spec); });
    REQUIRE(started.wait_for(3s) == std::future_status::ready);
    SendMessageW(hwnd, WM_SIZE, SIZE_MINIMIZED, 0);
    SendMessageW(hwnd, WM_ACTIVATE, WA_INACTIVE, 0);
    CHECK_FALSE(window.closed());
    CHECK(WaitForSingleObject(reinterpret_cast<HANDLE>(window.close_event()), 0) == WAIT_TIMEOUT);
    CHECK(WaitForSingleObject(reinterpret_cast<HANDLE>(closed.handle()), 0) == WAIT_TIMEOUT);
    const auto close = std::chrono::steady_clock::now();
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
    REQUIRE(child.wait_for(2s) == std::future_status::ready);
    const auto result = child.get();
    const auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - close);
    std::cout << "window close-to-exit debug=" << debug << " ms=" << latency.count() << '\n';
    CHECK(latency < 500ms); CHECK(result.cancelled); CHECK_FALSE(result.timed_out); CHECK_FALSE(result.crash.captured);
    CHECK(WaitForSingleObject(reinterpret_cast<HANDLE>(closed.handle()), 100) == WAIT_OBJECT_0);
    const auto other_result = other.get();
    CHECK(other_result.exit_code == 0); CHECK_FALSE(other_result.cancelled); CHECK_FALSE(other_result.timed_out);
    // The same already-signaled event prevents another process creation.
    CHECK_FALSE(RunProcess(spec).launched);
  }
  std::error_code ec; fs::remove_all(dir, ec);
}
TEST_CASE("digest batch does not reuse bytes exposed through a writable mapping", "[digest-batch]") {
  const auto dir = UniqueTempPath("digest-mapping"); fs::create_directories(dir);
  const auto file = dir / "input.bin"; std::ofstream(file) << "first";
  const auto writer = CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
  REQUIRE(writer != INVALID_HANDLE_VALUE);
  const auto mapping = CreateFileMappingW(writer, nullptr, PAGE_READWRITE, 0, 0, nullptr);
  REQUIRE(mapping != nullptr);
  auto* view = static_cast<char*>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, 0));
  REQUIRE(view != nullptr); CloseHandle(writer);
  {
    DigestBatch batch;
    const auto before = DigestFile(file);
    if (before) {
      std::copy_n("other", 5, view); FlushViewOfFile(view, 5);
      const auto after = DigestFile(file);
      REQUIRE(after);
      CHECK(after->sha256 != before->sha256);
    } else CHECK(batch.files == 0);
  }
  UnmapViewOfFile(view); CloseHandle(mapping);
  std::error_code ec; fs::remove_all(dir, ec);
}
TEST_CASE("lifecycle notifications cannot postpone a child timeout", "[session-event]") {
  const auto dir = UniqueTempPath("notification-timeout"); fs::create_directories(dir);
  const auto event = CreateEventW(nullptr, TRUE, TRUE, nullptr);
  REQUIRE(event != nullptr);
  ProcessSpec spec{X360N_TEST_CHILD, {"sleep", "2000"}, dir, dir / "out", dir / "err", 100ms};
  spec.no_console = true; spec.notification_event = reinterpret_cast<uintptr_t>(event);
  spec.on_notification = [] {};
  const auto result = RunProcess(spec);
  CloseHandle(event);
  CHECK(result.launched); CHECK(result.timed_out); CHECK_FALSE(result.cancelled); CHECK(result.duration < 1s);
  std::error_code ec; fs::remove_all(dir, ec);
}
TEST_CASE("nested verification preserves operation cancellation", "[digest-batch]") {
  bool cancelled = false;
  DigestBatch outer([&] { return cancelled; });
  {
    DigestBatch nested;
    CHECK_NOTHROW(CheckVerificationCancellation());
    cancelled = true;
    CHECK_THROWS_AS(CheckVerificationCancellation(), VerificationCancelled);
  }
  CHECK_THROWS_AS(CheckVerificationCancellation(), VerificationCancelled);
  cancelled = false;
  CHECK_NOTHROW(CheckVerificationCancellation());
}
TEST_CASE("protected digests support long Unicode Windows paths", "[digest-batch]") {
  const auto dir = UniqueTempPath("batch-long-path");
  auto deep = dir;
  while (deep.native().size() < 280) deep /= fs::path(u8"conteúdo日本012345678901234567890");
  const auto extended = fs::path(L"\\\\?\\" + deep.native());
  fs::create_directories(extended);
  const auto file = deep / "input.bin"; std::ofstream(extended / "input.bin") << "long-path";
  const auto expected = Sha256Hex({reinterpret_cast<const uint8_t*>("long-path"), 9});
  {
    DigestBatch batch;
    const auto first = DigestFile(file); REQUIRE(first); CHECK(first->sha256 == expected);
    CHECK(DigestFile(file)->sha256 == expected); CHECK(batch.hits == 1);
  }
  std::error_code ec; fs::remove_all(fs::path(L"\\\\?\\" + dir.native()), ec);
}
TEST_CASE("parallel digest read ahead preserves transaction identity and timestamp adversaries", "[digest-batch]") {
  const auto dir = UniqueTempPath("digest-parallel"); fs::create_directories(dir);
  std::vector<fs::path> paths;
  std::vector<std::string> hashes;
  std::vector<fs::file_time_type> times;
  for (unsigned i = 0; i < 9; ++i) {
    paths.push_back(dir / (std::to_string(i) + ".bin"));
    const std::string bytes((1 << 20) + i * 13, char('a' + i));
    std::ofstream(paths.back(), std::ios::binary) << bytes;
    hashes.push_back(Sha256Hex({reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()}));
    times.push_back(fs::last_write_time(paths.back()));
  }
  for (const unsigned workers : {1u, 4u, 20u}) {
    DigestBatch batch;
    auto candidates = paths;
    candidates.push_back(paths.front()); candidates.push_back(dir / "missing");
    PrimeVerificationDigests(candidates, workers);
    REQUIRE(batch.files == paths.size());
    for (size_t i = 0; i < paths.size(); ++i) {
      REQUIRE(DigestFile(paths[i])); CHECK(DigestFile(paths[i])->sha256 == hashes[i]);
      CHECK(HasProtectedVerificationPath(paths[i]));
      CHECK_FALSE(DeleteFileW(paths[i].c_str()));
    }
    CHECK(batch.files == paths.size());
    CHECK_FALSE(DigestFile(dir / "missing"));
  }
  // A new operation must read again even with exactly preserved size/mtime.
  {
    std::fstream file(paths[3], std::ios::in | std::ios::out | std::ios::binary); file.put('z');
  }
  fs::last_write_time(paths[3], times[3]);
  {
    DigestBatch batch;
    PrimeVerificationDigests(paths);
    CHECK(DigestFile(paths[3])->sha256 != hashes[3]);
    CHECK(batch.files == paths.size());
  }
  {
    unsigned checks = 0;
    DigestBatch batch([&] { return ++checks > paths.size() + 1; });
    CHECK_THROWS_AS(PrimeVerificationDigests(paths), VerificationCancelled);
  }
  // Cancellation joins every worker and destruction releases all exclusions.
  CHECK(DeleteFileW(paths[3].c_str()));
  std::error_code ec; fs::remove_all(dir, ec);
}
