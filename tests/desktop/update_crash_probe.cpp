// Process-death injection on synthetic packages only. Never used by the launcher.
#include "update.h"
#include <cstdlib>
#include <iostream>
int main(int argc, char** argv) {
  if (argc != 6) return 2;
  int at = std::atoi(argv[5]), steps = 0;
  auto checkpoint = [&](const char*) { if (++steps == at) std::_Exit(91); };
  try {
    if (std::string(argv[1]) == "recover")
      x360n::desktop::RecoverUpdate(argv[2], argv[4], checkpoint);
    else
      x360n::desktop::ApplyUpdate(argv[2], argv[3], "2.0.0", argv[4], checkpoint);
    std::cout << "checkpoints=" << steps << '\n';
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 3; }
}
