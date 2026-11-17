#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>
#include "filesystem/xdbf_title_data.h"
#include <rex/kernel/types/content.h>

namespace {
using namespace rex::filesystem;
using rex::kernel::XLanguage;
std::vector<uint8_t> Resource() {
  // A real SPA/XDBF layout with one XACH row and one localized string.
  std::vector<uint8_t> bytes(24 + 2 * 18 + 14 + 36 + 14 + 4 + 5);
  auto* header = reinterpret_cast<XDBFHeader*>(bytes.data());
  header->magic = rex::memory::make_fourcc("XDBF");
  header->version = 0x00010000; header->entry_capacity = 2; header->entry_used = 2;
  auto* entries = reinterpret_cast<XDBFEntry*>(bytes.data() + 24);
  entries[0].section = 1; entries[0].id = 0x58414348; entries[0].size = 50;
  entries[1].section = 3; entries[1].id = 1; entries[1].offset = 50; entries[1].size = 23;
  auto* ach = reinterpret_cast<XDBFSectionHeader*>(bytes.data() + 60);
  ach->magic = rex::memory::make_fourcc("XACH"); ach->version = 1; ach->size = 46; ach->count = 1;
  auto* row = reinterpret_cast<XDBFAchievementTableEntry*>(bytes.data() + 74);
  row->id = 155; row->label_id = 7; row->gamerscore = 10;
  auto* str = reinterpret_cast<XDBFSectionHeader*>(bytes.data() + 110);
  str->magic = rex::memory::make_fourcc("XSTR"); str->version = 1; str->size = 19; str->count = 1;
  auto* text = reinterpret_cast<XDBFStringTableEntry*>(bytes.data() + 124);
  text->id = 7; text->string_length = 5;
  std::memcpy(bytes.data() + 128, "Hello", 5);
  return bytes;
}
}

TEST_CASE("Achievement XDBF rejects truncated resources before exposing entries", "[sdk][offline-achievement][xdbf]") {
  const auto original = Resource();
  XDBFTitleData good(original.data(), original.size());
  REQUIRE(good.IsValid());
  REQUIRE(good.GetAchievements().size() == 1);
  CHECK(good.GetAchievements()[0].gamerscore == 10);
  CHECK(good.GetStringTableEntry(XLanguage::kEnglish, 7) == "Hello");
  for (size_t length = 0; length < original.size(); ++length) {
    XDBFTitleData bad(original.data(), length);
    CHECK_FALSE(bad.IsValid());
    CHECK(bad.GetAchievements().empty());
    CHECK(bad.GetStringTableEntry(XLanguage::kEnglish, 7).empty());
    CHECK_FALSE(bad.GetIcon());
  }
}

TEST_CASE("Achievement XDBF bounds counts offsets strings and duplicate keys", "[sdk][offline-achievement][xdbf]") {
  auto data = Resource();
  auto* header = reinterpret_cast<XDBFHeader*>(data.data());
  auto* entries = reinterpret_cast<XDBFEntry*>(data.data() + 24);
  SECTION("entry capacity overflow") { header->entry_capacity = 0xFFFFFFFF; }
  SECTION("unsupported XDBF version") { header->version = 1; }
  SECTION("used entries exceed capacity") { header->entry_used = 3; }
  SECTION("free capacity overflow") { header->free_count = 0xFFFFFFFF; }
  SECTION("entry offset overflow") { entries[0].offset = 0xFFFFFFFF; }
  SECTION("entry length overflow") { entries[0].size = 0xFFFFFFFF; }
  SECTION("duplicate key") { entries[1].section = 1; entries[1].id = 0x58414348; }
  SECTION("truncated achievement table") {
    reinterpret_cast<XDBFSectionHeader*>(data.data() + 60)->count = 2;
  }
  SECTION("truncated string") {
    reinterpret_cast<XDBFStringTableEntry*>(data.data() + 124)->string_length = 6;
  }
  SECTION("section size exceeds entry") {
    reinterpret_cast<XDBFSectionHeader*>(data.data() + 110)->size = 0xFFFFFFFF;
  }
  SECTION("section size below header") {
    reinterpret_cast<XDBFSectionHeader*>(data.data() + 110)->size = 0;
  }
  SECTION("invalid section magic") { data[110] = 0; }
  XDBFTitleData bad(data.data(), data.size());
  CHECK_FALSE(bad.IsValid());
  CHECK(bad.GetAchievements().empty());
  CHECK(bad.GetStringTableEntry(XLanguage::kEnglish, 7).empty());
}
