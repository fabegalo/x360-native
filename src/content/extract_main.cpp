// Extraction-only process: no PPC execution or title runtime. Its only XEX keys
// are the pinned SDK loader's, used to read a title's own name and icon.
#include "extraction.h"
#include "fs_io.h"
#include "system_update.h"
#include "xex_patch.h"
#include <filesystem>
#include <iostream>
#include <string_view>
#include <rex/system/util/xdbf_utils.h>
namespace {
// Writes <destination>/title.txt (UTF-8) and title.png from the XEX's own SPA,
// each only when present. Exit 0 with no files when the title carries none.
int TitleArt(const std::filesystem::path& xex, const std::filesystem::path& destination) {
  if (!x360n::io::create_directory(destination)) throw std::runtime_error("destination already exists");
  const auto spa = x360n::content::ReadXexTitleResource(xex);
  const rex::system::util::XdbfGameData db(spa.data(), spa.size());
  if (!db.is_valid()) return 0;
  if (const auto name = db.title(); !name.empty())
    x360n::io::ofstream(destination / "title.txt", std::ios::binary) << name;
  if (const auto icon = db.icon(); icon && icon.size)
    x360n::io::ofstream(destination / "title.png", std::ios::binary)
        .write(reinterpret_cast<const char*>(icon.buffer), std::streamsize(icon.size));
  return 0;
}
int Run(std::string_view command, const std::filesystem::path& source, const std::filesystem::path& destination) {
  if (command == "title-art") return TitleArt(source, destination);
  if (command == "extract-system-update") return x360n::content::ExtractSystemUpdate(source, destination);
  if (command == "extract") return x360n::content::Extract(source, destination);
  if (command == "extract-disc") return x360n::content::ExtractDisc(source, destination);
  std::cerr << "usage: x360n-extract <extract|extract-disc|extract-system-update|title-art> <source> <destination>\n";
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
