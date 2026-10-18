// A supervised fixture, never a title. The frontend launches it as x360n.exe.
#include <chrono>
#include <fstream>
#include <thread>
#include <string>
#include <iostream>
#include "catalog.h"
#include "digest_batch.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
int main(int argc, char** argv) {
  // The frontend now delegates validation to its supervised process. Exercise
  // that same model before recording synthetic title creation in this fixture.
  try {
    std::string root, id, manifest;
    for (int i = 1; i + 1 < argc; ++i) {
      if (std::string(argv[i]) == "--root") root = argv[i + 1];
      if (std::string(argv[i]) == "--catalog-target") id = argv[i + 1];
      if (std::string(argv[i]) == "session") manifest = argv[i + 1];
    }
    if (!id.empty()) {
      x360n::DigestBatch batch;
      const x360n::ProjectPaths paths{x360n::PathFromUtf8(root)};
      x360n::desktop::VerifyCatalogLaunch(paths, x360n::desktop::ReadCatalog(paths), id, x360n::PathFromUtf8(manifest));
    }
  } catch (const std::exception& error) { std::cerr << error.what(); return 3; }
  std::cout << "X360N_LAUNCH_VERIFIED\n" << std::flush;
  std::string mode; std::ifstream("child-mode.txt") >> mode;
  std::ofstream("child-started.txt") << std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  std::this_thread::sleep_for(std::chrono::milliseconds(mode == "wait" ? 2000 : 80));
  if (mode == "cleanup") {
    std::string event;
    for (int i = 1; i + 1 < argc; ++i) if (std::string(argv[i]) == "--closed-event") event = argv[i + 1];
    const std::wstring name(event.begin(), event.end());
    const auto closed = OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str());
    if (!closed) return 8;
    SetEvent(closed); CloseHandle(closed);
    std::ofstream("child-closed.txt") << std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
  std::ofstream("child-ended.txt") << std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  return mode == "fail" ? 7 : 0;
}
