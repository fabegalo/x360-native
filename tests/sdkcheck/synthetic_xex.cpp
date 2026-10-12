#include "synthetic_xex.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <system_error>

namespace x360n::test {

namespace {

class Bytes {
 public:
  explicit Bytes(size_t n) : v_(n, 0) {}
  void be32(size_t o, uint32_t x) {
    for (int i = 0; i < 4; ++i) v_.at(o + i) = uint8_t(x >> (24 - 8 * i));
  }
  void be16(size_t o, uint16_t x) {
    v_.at(o) = uint8_t(x >> 8);
    v_.at(o + 1) = uint8_t(x);
  }
  void le32(size_t o, uint32_t x) {
    for (int i = 0; i < 4; ++i) v_.at(o + i) = uint8_t(x >> (8 * i));
  }
  void le16(size_t o, uint16_t x) {
    v_.at(o) = uint8_t(x);
    v_.at(o + 1) = uint8_t(x >> 8);
  }
  void str(size_t o, const char* s) { std::memcpy(&v_.at(o), s, std::strlen(s)); }
  const std::vector<uint8_t>& data() const { return v_; }

 private:
  std::vector<uint8_t> v_;
};

constexpr uint32_t kHeaderSize = 0x1000;  // XEX headers before the image

}  // namespace

std::vector<uint8_t> BuildXex(const std::vector<LibraryRecord>& libraries, std::vector<Placed>* placed,
                              uint16_t call_ordinal, Entry entry_kind) {
  Bytes x(kHeaderSize + kXexImageSize);
  const size_t img = kHeaderSize;  // file offset of the image (guest kXexBase)
  auto at = [&](uint32_t guest) { return img + (guest - kXexBase); };

  // Place slots and thunks; write the record words into the image.
  uint32_t next_slot = kXexSlots, next_thunk = kXexThunks;
  for (size_t l = 0; l < libraries.size(); ++l) {
    for (const auto& r : libraries[l]) {
      Placed p{l, r.ordinal};
      if (r.slot) {
        p.slot = next_slot;
        next_slot += 4;
        x.be32(at(p.slot), 0x00000000u | r.ordinal);  // type 0, library index 0
      }
      if (r.thunk) {
        p.thunk = next_thunk;
        next_thunk += 16;
        x.be32(at(p.thunk), 0x01000000u | r.ordinal);  // type 1: the import record
        x.be32(at(p.thunk + 4), 0x02000000u | r.ordinal);
        x.be32(at(p.thunk + 8), 0x7D6903A6);   // mtctr r11
        x.be32(at(p.thunk + 12), 0x4E800420);  // bctr
      }
      placed->push_back(p);
    }
  }
  REQUIRE(next_slot <= kXexPdata);
  REQUIRE(next_thunk <= kXexText + kXexTextSize);

  // Entry point and its .pdata entry (the codegen requires the exception directory):
  // begin address, then PrologLength | FunctionLength << 8 | ThirtyTwoBit << 30.
  const uint32_t entry = kXexText;
  std::vector<uint32_t> code;
  uint32_t prolog = 0;
  if (entry_kind == Entry::kReturnZero) {
    REQUIRE(call_ordinal == 0);
    code = {0x38600000,   // li r3,0
            0x4E800020};  // blr
  } else {
    code = {0x7D8802A6,   // mflr r12
            0x9181FFF8,   // stw r12,-8(r1)
            0x9421FFA0,   // stwu r1,-96(r1)
            0x60000000,   // nop, or bl <call>
            0x38210060,   // addi r1,r1,96
            0x8181FFF8,   // lwz r12,-8(r1)
            0x7D8803A6,   // mtlr r12
            0x4E800020};  // blr
    prolog = 3;
    uint32_t call = 0;
    for (const auto& p : *placed) {
      if (call_ordinal && !call && p.ordinal == call_ordinal && p.thunk) call = p.thunk;
    }
    REQUIRE((call_ordinal == 0) == (call == 0));
    if (call) {
      const uint32_t site = entry + 12;
      code[3] = 0x48000001 | ((call - site) & 0x03FFFFFC);
    }
  }
  for (size_t i = 0; i < code.size(); ++i) x.be32(at(entry + 4 * uint32_t(i)), code[i]);
  x.be32(at(kXexPdata), entry);
  x.be32(at(kXexPdata + 4), prolog | (uint32_t(code.size()) << 8) | (1u << 30));

  // PE headers (little-endian), as XexModule::ReadPEHeaders checks them.
  x.str(img + 0, "MZ");
  x.le16(img + 2, 0x0090);  // is_valid_executable() reads 4D 5A 90 00
  x.le32(img + 0x3C, 0x80);
  const size_t nt = img + 0x80;
  x.str(nt, "PE");
  x.le16(nt + 4, 0x01F2);   // IMAGE_FILE_MACHINE_POWERPCBE
  x.le16(nt + 6, 2);        // sections
  x.le16(nt + 20, 224);     // IMAGE_SIZEOF_NT_OPTIONAL_HEADER
  x.le16(nt + 22, 0x0102);  // EXECUTABLE_IMAGE | 32BIT_MACHINE
  const size_t opt = nt + 24;
  x.le16(opt + 0, 0x10B);  // IMAGE_NT_OPTIONAL_HDR32_MAGIC
  x.le32(opt + 16, entry - kXexBase);
  x.le32(opt + 28, kXexBase);
  x.le32(opt + 32, kXexPage);
  x.le32(opt + 36, 0x200);
  x.le32(opt + 56, kXexImageSize);
  x.le32(opt + 60, 0x1000);
  x.le16(opt + 68, 14);  // IMAGE_SUBSYSTEM_XBOX
  x.le32(opt + 92, 16);
  x.le32(opt + 96 + 3 * 8, kXexPdata - kXexBase);  // IMAGE_DIRECTORY_ENTRY_EXCEPTION
  x.le32(opt + 96 + 3 * 8 + 4, 8);
  const size_t sec = opt + 224;
  x.str(sec, ".rdata");
  x.le32(sec + 8, 0x100);
  x.le32(sec + 12, kXexSlots - kXexBase);
  x.le32(sec + 16, 0x100);
  x.le32(sec + 20, kXexSlots - kXexBase);
  x.le32(sec + 36, 0x40000040);  // INITIALIZED_DATA | MEM_READ
  x.str(sec + 40, ".text");
  x.le32(sec + 48, kXexTextSize);
  x.le32(sec + 52, kXexText - kXexBase);
  x.le32(sec + 56, kXexTextSize);
  x.le32(sec + 60, kXexText - kXexBase);
  x.le32(sec + 76, 0x60000020);  // CNT_CODE | MEM_EXECUTE | MEM_READ

  // XEX2 header and optional headers (big-endian).
  x.be32(0x00, 0x58455832);  // "XEX2"
  x.be32(0x04, 0x00000001);  // XEX_MODULE_TITLE
  x.be32(0x08, kHeaderSize);
  x.be32(0x10, 0x800);  // security info
  x.be32(0x14, 3);
  x.be32(0x18, 0x00010100);  // XEX_HEADER_ENTRY_POINT (value)
  x.be32(0x1C, entry);
  x.be32(0x20, 0x000003FF);  // XEX_HEADER_FILE_FORMAT_INFO (offset)
  x.be32(0x24, 0x040);
  x.be32(0x28, 0x000103FF);  // XEX_HEADER_IMPORT_LIBRARIES (offset)
  x.be32(0x2C, 0x100);
  x.be32(0x40, 8);  // info_size; encryption and compression 0 (none)

  // Import libraries: string table with "xam.xex", then one xex2_import_library per record.
  const size_t imp = 0x100;
  x.be32(imp + 4, 8);
  x.be32(imp + 8, 1);
  x.str(imp + 12, "xam.xex");
  size_t lib = imp + 20;
  for (size_t l = 0; l < libraries.size(); ++l) {
    std::vector<uint32_t> table;
    for (const auto& p : *placed) {
      if (p.library != l) continue;
      if (p.slot) table.push_back(p.slot);
      if (p.thunk) table.push_back(p.thunk);
    }
    const uint32_t size = 0x28 + 4 * uint32_t(table.size());
    x.be32(lib, size);
    x.be16(lib + 0x24, 0);  // name_index -> "xam.xex"
    x.be16(lib + 0x26, uint16_t(table.size()));
    for (size_t i = 0; i < table.size(); ++i) x.be32(lib + 0x28 + 4 * i, table[i]);
    lib += size;
  }
  x.be32(imp, uint32_t(lib - imp));
  REQUIRE(lib <= 0x800);

  // Security info: image size, load address, two page descriptors (rdata, code).
  const size_t s = 0x800;
  x.be32(s + 0x000, 0x184 + 2 * 0x18);
  x.be32(s + 0x004, kXexImageSize);
  x.be32(s + 0x110, kXexBase);
  x.be32(s + 0x180, 2);
  x.be32(s + 0x184, (1u << 4) | 3);         // 1 page, XEX_SECTION_READONLY_DATA
  x.be32(s + 0x184 + 0x18, (1u << 4) | 1);  // 1 page, XEX_SECTION_CODE
  return x.data();
}

Workdir::Workdir(const char* group, const char* name)
    : dir(std::filesystem::temp_directory_path() / group / name) {
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir);
}

Workdir::~Workdir() {
  // X360N_KEEP_TEST_DIRS=1 keeps it, to rerun a probe by hand (x360n run --crash-stack).
  if (const char* keep = std::getenv("X360N_KEEP_TEST_DIRS"); keep && std::string(keep) == "1") {
    return;
  }
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

void Workdir::write(const char* file, const std::vector<uint8_t>& bytes) const {
  std::ofstream(dir / file, std::ios::binary)
      .write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

}  // namespace x360n::test
