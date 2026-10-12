#include "workspace_lease.h"
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
namespace x360n {
WorkspaceLease::WorkspaceLease(const ProjectPaths& paths) {
  std::filesystem::create_directories(paths.local());
  auto file = paths.local() / "environment.lock";
#ifdef _WIN32
  auto handle = CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle != INVALID_HANDLE_VALUE) handle_ = reinterpret_cast<intptr_t>(handle);
#else
  auto fd = open(file.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
  if (fd >= 0) { if (flock(fd, LOCK_EX | LOCK_NB) == 0) handle_ = fd; else close(fd); }
#endif
  if (handle_ == -1) throw std::runtime_error("Environment busy or not writable. Close the platform/preparation and retry. Ambiente em uso ou sem permissão de gravação.");
}
WorkspaceLease::~WorkspaceLease() {
  if (handle_ == -1) return;
#ifdef _WIN32
  CloseHandle(reinterpret_cast<HANDLE>(handle_));
#else
  close(int(handle_));
#endif
}
}
