// Extraction-only process: no PPC execution, XEX keys or title runtime.
#include "extraction.h"
#include "system_update.h"
#include <iostream>
#include <string_view>
int wmain(int argc, wchar_t** argv) {
  try {
    if (argc == 4 && std::wstring_view(argv[1]) == L"extract-system-update")
      return x360n::content::ExtractSystemUpdate(argv[2], argv[3]);
    if (argc == 4 && std::wstring_view(argv[1]) == L"extract")
      return x360n::content::Extract(argv[2], argv[3]);
    if (argc == 4 && std::wstring_view(argv[1]) == L"extract-disc")
      return x360n::content::ExtractDisc(argv[2], argv[3]);
    std::cerr << "usage: x360n-extract <extract|extract-disc> <source> <destination>\n";
    return 3;
  } catch (const std::exception& e) {
    std::cerr << "Extraction incomplete: " << e.what() << '\n';
    return 1;
  }
}
