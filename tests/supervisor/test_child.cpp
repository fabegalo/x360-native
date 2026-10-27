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
#include <array>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

int main(int argc, char** argv) {
  std::string log_path;
  if (argc >= 2 && !std::strncmp(argv[1], "--log_file=", 11)) {
    log_path = argv[1] + 11;
    ++argv;
    --argc;
  }
  if (argc < 2) return 64;
#ifdef _WIN32
  if (!std::strcmp(argv[1], "crash-memory")) {
    constexpr uintptr_t arena = 0x100000000ull;
    auto* bytes = static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(arena),0x10000,
        MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if (!bytes) return 65;
    std::array<uint64_t,32> registers{};
    registers[24] = 0x2000;
    const uint8_t direct[]{0xDE,0xAD,0xBE,0xEF,0,0,0x30,0};
    const uint8_t nested[]{0xCA,0xFE,0xBA,0xBE};
    std::memcpy(bytes+0x2000,direct,sizeof(direct));
    std::memcpy(bytes+0x3000,nested,sizeof(nested));
    // Pin a synthetic register block in a native GPR at the actual AV.
    asm volatile("movl (%0), %%eax" : : "r"(uintptr_t(4)), "D"(registers.data()) : "rax","memory");
    return 66;
  }
  if (!std::strcmp(argv[1], "console")) {
    std::printf("console=%d\n", GetConsoleWindow() != nullptr);
    return GetConsoleWindow() ? 1 : 0;
  }
#endif
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
