// Test fixture for supervisor tests: a child process with scripted behavior.
// Not a title, not a dashboard. Modes:
//   exit <n>     print a line to stdout and stderr, exit with n
//   sleep <ms>   sleep, then exit 0
//   crash        write through a null pointer (access violation)
//   stub-log     print lines in the format REX_STUB logs, exit 0
//   cwd          print the working directory, exit 0
// A leading --log_file=<path> (as passed by `x360n run --rex-log`) sends the
// stub-log lines to that file instead of stdout, like a ReXGlue GUI app.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char** argv) {
  std::string log_path;
  if (argc >= 2 && !std::strncmp(argv[1], "--log_file=", 11)) {
    log_path = argv[1] + 11;
    ++argv;
    --argc;
  }
  if (argc < 2) return 64;
  if (!std::strcmp(argv[1], "exit") && argc >= 3) {
    std::printf("child stdout\n");
    std::fprintf(stderr, "child stderr\n");
    return std::atoi(argv[2]);
  }
  if (!std::strcmp(argv[1], "sleep") && argc >= 3) {
    std::this_thread::sleep_for(std::chrono::milliseconds(std::atoi(argv[2])));
    return 0;
  }
  if (!std::strcmp(argv[1], "crash")) {
    volatile int* p = nullptr;
    *p = 1;
    return 0;
  }
  if (!std::strcmp(argv[1], "stub-log")) {
    const char* lines =
        "[2026-01-01 00:00:00.000] [krnl] [warning] __imp__XamFoo STUB\n"
        "[2026-01-01 00:00:00.001] [krnl] [warning] __imp__XamFoo STUB\n"
        "[2026-01-01 00:00:00.002] [krnl] [warning] __imp__KeBar STUB - returning 0x0\n"
        "not a stub: STUBBED word\n";
    if (log_path.empty()) {
      std::cout << lines;
    } else {
      std::ofstream log(log_path, std::ios::binary);
      if (!log) return 65;
      log << lines;
    }
    return 0;
  }
  if (!std::strcmp(argv[1], "cwd")) {
    std::cout << "cwd " << std::filesystem::current_path().string() << "\n";
    return 0;
  }
  return 64;
}
