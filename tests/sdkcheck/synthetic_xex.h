// A minimal XEX2 built by the tests: no encryption, no compression, two 64 KB pages
// at 0x82000000 (.rdata with value slots and one .pdata entry, .text with the entry
// point and import thunks in the XDK linker layout). It holds no content of any
// title. Used by sdkcheck/import_records_test.cpp (patch 0003).
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace x360n::test {

constexpr uint32_t kXexBase = 0x82000000;
constexpr uint32_t kXexPage = 0x10000;  // page size of the 0x80000000 XEX heap
constexpr uint32_t kXexImageSize = 2 * kXexPage;
constexpr uint32_t kXexSlots = kXexBase + 0x1000;  // .rdata: value slots
constexpr uint32_t kXexPdata = kXexSlots + 0xF0;   // .rdata: one .pdata entry
constexpr uint32_t kXexText = kXexBase + kXexPage;  // .text; the entry point is here
constexpr uint32_t kXexThunks = kXexText + 0x100;   // import thunks, 16 bytes each
constexpr uint32_t kXexTextSize = 0x200;

// One import of xam.xex in one xex2_import_library record: its value slot
// (record type 0), its thunk (record type 1), or both.
struct Record {
  uint16_t ordinal;
  bool slot;
  bool thunk;
};
using LibraryRecord = std::vector<Record>;

// Where BuildXex placed a record.
struct Placed {
  size_t library;
  uint16_t ordinal;
  uint32_t slot = 0;
  uint32_t thunk = 0;
};

enum class Entry {
  // mflr/stw/stwu, then a nop or `bl` to the thunk of call_ordinal, then the epilogue.
  kFrameCall,
  // li r3,0; blr
  kReturnZero,
};

// Builds the XEX and fills `placed` with the address of every slot and thunk.
std::vector<uint8_t> BuildXex(const std::vector<LibraryRecord>& libraries, std::vector<Placed>* placed,
                              uint16_t call_ordinal = 0, Entry entry = Entry::kFrameCall);

// A fresh directory under %TEMP%/<group>/<name>, removed again on destruction.
struct Workdir {
  std::filesystem::path dir;
  Workdir(const char* group, const char* name);
  ~Workdir();
  Workdir(const Workdir&) = delete;
  Workdir& operator=(const Workdir&) = delete;
  void write(const char* file, const std::vector<uint8_t>& bytes) const;
};

}  // namespace x360n::test
