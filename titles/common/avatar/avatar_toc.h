// Reader for the avatar asset catalog (AvatarAssetPack.toc /
// AvatarAssetPackLegacyV1.toc) inside the system storage pack FFFE07DF00000002.
// Layout from XAM 2.0.17559.0 (0x8197CA98 and the readers it selects) and from the
// original 17559 pack; docs/avatar-system.md §4. Big-endian throughout.
//
//   header   format GUID (16), stamp (8), u32 +0x18, u32 record count (+0x1C),
//            u32 +0x20, u32 +0x24, u32 column count (+0x28), u32 +0x2C
//            -- 48 bytes (current), 40 (legacy V1), 32 (oldest); XAM fills the
//            missing fields: +0x20 = 1 (32-byte format), +0x28 = 13 (both short ones)
//   records  count x (0xBC + 4 * columns + 8) bytes, right after the header:
//            +0x00 u32, +0x04..+0xBB opaque, +0xBC column offsets, then
//            blob offset and blob size
//   strings  per column (language id 0..columns-1): offset of a NUL-terminated
//            UTF-16BE string; strings are shared between columns and records
//   blobs    asset binaries ("STRB"), contiguous in record order, ending at the end
//            of the file; unused slots have blob 0/0 and all columns 0
//
// Only structure is interpreted here. Field meanings marked as hypotheses in the
// document are exposed as raw values.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace x360n::avatar {

enum class TocFormat {
  kCurrent,   // 580a07d6-4bcd-40be-bf37-254be826fb0b, 48-byte header
  kLegacyV1,  // 8a762df4-c067-4b66-b4ee-56466b1b8280, 40-byte header
  kOldest,    // 9ad6ebce-6262-4eeb-8a82-a3f70b817369, 32-byte header
};

struct TocHeader {
  TocFormat format;
  std::array<uint8_t, 16> guid;
  uint64_t stamp;
  uint32_t field_18;
  uint32_t record_count;
  uint32_t field_20;  // 1 when absent from the file (XAM default)
  uint32_t field_24;
  uint32_t column_count;  // 13 when absent from the file (XAM default)
  uint32_t header_size;
  uint32_t record_size;
};

struct TocRecord {
  uint32_t offset;    // of the record in the catalog
  // Unused slot: blob offset and size 0 and every column 0 (179 of the 1282 records
  // of the 17559 catalog; their other bytes are not all zero).
  bool empty;
  uint32_t field_00;  // hypothesis: category/component mask
  std::vector<uint32_t> string_offsets;
  uint32_t blob_offset;
  uint32_t blob_size;
};

class AvatarToc {
 public:
  // Parses and validates the whole catalog: known GUID, header and record table
  // inside the data, every string of a used record NUL-terminated inside the string
  // area, used records' blobs contiguous in record order from the first one to the
  // end of the data, unused slots fully empty (no strings, blob 0/0). On failure
  // returns nothing and sets *error.
  static std::optional<AvatarToc> Parse(std::vector<uint8_t> data, std::string* error);

  const TocHeader& header() const { return header_; }
  size_t size() const { return records_.size(); }
  const TocRecord& record(size_t i) const { return records_.at(i); }
  // Name of record i in column (language) `column`; empty for an unused slot.
  std::u16string Name(size_t i, uint32_t column) const;
  std::span<const uint8_t> Blob(size_t i) const;
  // Bytes +0x04..+0xBB of record i, not interpreted yet.
  std::span<const uint8_t> Opaque(size_t i) const;
  uint32_t strings_begin() const { return strings_begin_; }
  uint32_t blobs_begin() const { return blobs_begin_; }

 private:
  std::vector<uint8_t> data_;
  TocHeader header_{};
  std::vector<TocRecord> records_;
  uint32_t strings_begin_ = 0;
  uint32_t blobs_begin_ = 0;
};

// The three catalog GUIDs, as stored in XAM 2.0.17559.0 at 0x816341E0 (current),
// 0x816341F0 (legacy V1) and 0x81634200 (oldest).
std::optional<TocFormat> FormatFromGuid(std::span<const uint8_t, 16> guid);

}  // namespace x360n::avatar
