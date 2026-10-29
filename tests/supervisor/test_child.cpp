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
#include <csignal>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#include <sys/resource.h>
#include <fcntl.h>
#endif
#ifdef X360N_PLATFORM_CHILD
#include "session_notification.h"
#endif

int main(int argc, char** argv) {
#ifndef _WIN32
  // Synthetic crash contracts must never leave a core dump on the user's HDD.
  const rlimit no_core{0, 0};
  setrlimit(RLIMIT_CORE, &no_core);
#endif
  std::string log_path;
  if (argc >= 2 && !std::strncmp(argv[1], "--log_file=", 11)) {
    log_path = argv[1] + 11;
    ++argv;
    --argc;
  }
  if (argc < 2) return 64;
#ifdef X360N_PLATFORM_CHILD
  if (!std::strcmp(argv[1], "notify") && argc >= 3) {
    x360n::SessionNotification notification(argv[2]);
    if (!notification.Signal()) return 65;
    // Keep the receiver's run loop alive long enough to observe the event.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return 0;
  }
#endif
  if (!std::strcmp(argv[1], "args")) {
    for (int i = 2; i < argc; ++i) std::cout << std::strlen(argv[i]) << ':' << argv[i] << '\n';
    return 0;
  }
#ifndef _WIN32
  if (!std::strcmp(argv[1], "fd-state") && argc >= 3)
    return fcntl(std::atoi(argv[2]), F_GETFD) < 0 && errno == EBADF ? 0 : 1;
  if (!std::strcmp(argv[1], "ignore-term")) {
    std::signal(SIGTERM, SIG_IGN);
    std::cout << "ready\n" << std::flush;
    std::this_thread::sleep_for(std::chrono::seconds(20));
    return 0;
  }
  if (!std::strcmp(argv[1], "term-clean")) {
    std::signal(SIGTERM, [](int) {
      const char message[] = "clean termination\n";
      const auto written = write(STDOUT_FILENO, message, sizeof(message) - 1);
      (void)written;
      _exit(23);
    });
    std::cout << "ready\n" << std::flush;
    std::this_thread::sleep_for(std::chrono::seconds(20));
    return 0;
  }
  if (!std::strcmp(argv[1], "tree") && argc >= 3) {
    // Descendant attempts a delayed write after the supervisor has returned.
    // It deliberately ignores TERM to exercise forced group cleanup.
    const pid_t child = fork();
    if (child < 0) return 65;
    if (child == 0) {
      std::signal(SIGTERM, SIG_IGN);
      std::this_thread::sleep_for(std::chrono::milliseconds(900));
      std::ofstream(argv[2]) << "escaped descendant";
      _exit(0);
    }
    std::cout << "ready\n" << std::flush;
    if (argc >= 4 && !std::strcmp(argv[3], "exit")) return 0;
    std::this_thread::sleep_for(std::chrono::seconds(20));
    return 0;
  }
#endif
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
#if defined(_M_ARM64) || defined(__aarch64__)
    asm volatile("ldr w0, [%0]" : : "r"(uintptr_t(4)), "r"(registers.data()) : "x0", "memory");
#else
    asm volatile("movl (%0), %%eax" : : "r"(uintptr_t(4)), "D"(registers.data()) : "rax","memory");
#endif
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
#ifdef _WIN32
    volatile int* p = nullptr;
    *p = 1;
#else
    std::raise(SIGSEGV);
#endif
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
    const auto path = std::filesystem::current_path().u8string();
    std::cout << "cwd " << std::string(path.begin(), path.end()) << "\n";
    return 0;
  }
  return 64;
}
