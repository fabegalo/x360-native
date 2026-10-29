// Extraction-only process: no PPC execution, XEX keys or title runtime.
#include "extraction.h"
#include "system_update.h"
#include <filesystem>
#include <iostream>
#include <string_view>
namespace {
int Run(std::string_view command, const std::filesystem::path& source, const std::filesystem::path& destination) {
  if (command == "extract-system-update") return x360n::content::ExtractSystemUpdate(source, destination);
  if (command == "extract") return x360n::content::Extract(source, destination);
  if (command == "extract-disc") return x360n::content::ExtractDisc(source, destination);
  std::cerr << "usage: x360n-extract <extract|extract-disc|extract-system-update> <source> <destination>\n";
  return 3;
}
}  // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
  try {
    if (argc != 4) return Run({}, {}, {});
    const std::wstring_view command(argv[1]);
    const std::string narrow(command.begin(), command.end());  // commands are ASCII
    return Run(narrow, argv[2], argv[3]);
#else
// Arguments are UTF-8 byte paths, as the supervisor passes them.
int main(int argc, char** argv) {
  try {
    if (argc != 4) return Run({}, {}, {});
    return Run(argv[1], argv[2], argv[3]);
#endif
  } catch (const std::exception& e) {
    std::cerr << "Extraction incomplete: " << e.what() << '\n';
    return 1;
  }
}
