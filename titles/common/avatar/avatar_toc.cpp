#include "avatar_toc.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace x360n::avatar {
namespace {

constexpr std::array<uint8_t, 16> kGuidCurrent = {0x58, 0x0a, 0x07, 0xd6, 0x4b, 0xcd, 0x40, 0xbe,
                                                  0xbf, 0x37, 0x25, 0x4b, 0xe8, 0x26, 0xfb, 0x0b};
constexpr std::array<uint8_t, 16> kGuidLegacyV1 = {0x8a, 0x76, 0x2d, 0xf4, 0xc0, 0x67, 0x4b, 0x66,
                                                   0xb4, 0xee, 0x56, 0x46, 0x6b, 0x1b, 0x82, 0x80};
constexpr std::array<uint8_t, 16> kGuidOldest = {0x9a, 0xd6, 0xeb, 0xce, 0x62, 0x62, 0x4e, 0xeb,
                                                 0x8a, 0x82, 0xa3, 0xf7, 0x0b, 0x81, 0x73, 0x69};

// Record layout: fixed part up to the column offsets, then blob offset and size.
constexpr uint32_t kColumnsAt = 0xBC;
// Sanity bound, far above the 18 columns of the current format.
constexpr uint32_t kMaxColumns = 64;

uint32_t Be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
uint64_t Be64(const uint8_t* p) { return (uint64_t(Be32(p)) << 32) | Be32(p + 4); }

bool Fail(std::string* error, std::string message) {
  if (error) *error = std::move(message);
  return false;
}

}  // namespace

std::optional<TocFormat> FormatFromGuid(std::span<const uint8_t, 16> guid) {
  if (std::equal(guid.begin(), guid.end(), kGuidCurrent.begin())) return TocFormat::kCurrent;
  if (std::equal(guid.begin(), guid.end(), kGuidLegacyV1.begin())) return TocFormat::kLegacyV1;
  if (std::equal(guid.begin(), guid.end(), kGuidOldest.begin())) return TocFormat::kOldest;
  return std::nullopt;
}

std::optional<AvatarToc> AvatarToc::Parse(std::vector<uint8_t> data, std::string* error) {
  AvatarToc toc;
  toc.data_ = std::move(data);
  const auto& d = toc.data_;
  auto& h = toc.header_;
  const uint64_t size = d.size();

  if (size < 32) {
    Fail(error, "catalog shorter than the smallest header (32 bytes)");
    return std::nullopt;
  }
  std::copy_n(d.begin(), 16, h.guid.begin());
  auto format = FormatFromGuid(std::span<const uint8_t, 16>(d.data(), 16));
  if (!format) {
    Fail(error, "unknown catalog GUID");
    return std::nullopt;
  }
  h.format = *format;
  h.header_size = h.format == TocFormat::kCurrent ? 48 : h.format == TocFormat::kLegacyV1 ? 40 : 32;
  if (size < h.header_size) {
    Fail(error, "catalog shorter than its header");
    return std::nullopt;
  }
  h.stamp = Be64(d.data() + 0x10);
  h.field_18 = Be32(d.data() + 0x18);
  h.record_count = Be32(d.data() + 0x1C);
  h.field_20 = h.header_size > 0x20 ? Be32(d.data() + 0x20) : 1;
  h.field_24 = h.header_size > 0x24 ? Be32(d.data() + 0x24) : 0;
  h.column_count = h.header_size > 0x28 ? Be32(d.data() + 0x28) : 13;
  if (h.column_count == 0 || h.column_count > kMaxColumns) {
    Fail(error, "column count out of range");
    return std::nullopt;
  }
  h.record_size = kColumnsAt + 4 * h.column_count + 8;

  const uint64_t table_end = uint64_t(h.header_size) + uint64_t(h.record_count) * h.record_size;
  if (h.record_count == 0 || table_end > size) {
    Fail(error, "record table does not fit in the catalog");
    return std::nullopt;
  }
  toc.strings_begin_ = uint32_t(table_end);

  toc.records_.reserve(h.record_count);
  for (uint32_t i = 0; i < h.record_count; ++i) {
    const uint8_t* r = d.data() + h.header_size + uint64_t(i) * h.record_size;
    TocRecord rec;
    rec.offset = uint32_t(r - d.data());
    rec.field_00 = Be32(r);
    rec.string_offsets.resize(h.column_count);
    for (uint32_t c = 0; c < h.column_count; ++c) rec.string_offsets[c] = Be32(r + kColumnsAt + 4 * c);
    rec.blob_offset = Be32(r + kColumnsAt + 4 * h.column_count);
    rec.blob_size = Be32(r + kColumnsAt + 4 * h.column_count + 4);
    const bool no_strings = std::all_of(rec.string_offsets.begin(), rec.string_offsets.end(),
                                        [](uint32_t o) { return o == 0; });
    rec.empty = rec.blob_offset == 0 && rec.blob_size == 0 && no_strings;
    if (!rec.empty && (rec.blob_offset == 0 || no_strings)) {
      Fail(error, "record " + std::to_string(i) + " is neither used nor an empty slot");
      return std::nullopt;
    }
    toc.records_.push_back(std::move(rec));
  }

  // Blobs of used records: contiguous in record order, from the first blob to the
  // end of the data.
  auto first = std::find_if(toc.records_.begin(), toc.records_.end(),
                            [](const TocRecord& r) { return !r.empty; });
  if (first == toc.records_.end()) {
    Fail(error, "catalog has no used record");
    return std::nullopt;
  }
  toc.blobs_begin_ = first->blob_offset;
  if (toc.blobs_begin_ < table_end || toc.blobs_begin_ > size) {
    Fail(error, "first blob outside the catalog");
    return std::nullopt;
  }
  uint64_t expected = toc.blobs_begin_;
  for (uint32_t i = 0; i < h.record_count; ++i) {
    const auto& rec = toc.records_[i];
    if (rec.empty) continue;
    if (rec.blob_offset != expected || uint64_t(rec.blob_offset) + rec.blob_size > size) {
      Fail(error, "blob " + std::to_string(i) + " is not contiguous or exceeds the catalog");
      return std::nullopt;
    }
    expected += rec.blob_size;
  }
  if (expected != size) {
    Fail(error, "last blob does not end at the end of the catalog");
    return std::nullopt;
  }

  // Strings of used records: inside [strings_begin, blobs_begin), 2-byte aligned,
  // NUL-terminated there.
  for (uint32_t i = 0; i < h.record_count; ++i) {
    if (toc.records_[i].empty) continue;
    for (uint32_t off : toc.records_[i].string_offsets) {
      if (off < toc.strings_begin_ || off >= toc.blobs_begin_ || ((off - toc.strings_begin_) & 1)) {
        Fail(error, "string offset of record " + std::to_string(i) + " outside the string area");
        return std::nullopt;
      }
      uint32_t p = off;
      while (p + 1 < toc.blobs_begin_ && (d[p] | d[p + 1]) != 0) p += 2;
      if (p + 1 >= toc.blobs_begin_) {
        Fail(error, "unterminated string in record " + std::to_string(i));
        return std::nullopt;
      }
    }
  }
  return toc;
}

std::u16string AvatarToc::Name(size_t i, uint32_t column) const {
  const auto& rec = records_.at(i);
  std::u16string out;
  if (rec.empty) return out;
  for (uint32_t p = rec.string_offsets.at(column); (data_[p] | data_[p + 1]) != 0; p += 2) {
    out.push_back(char16_t((uint16_t(data_[p]) << 8) | data_[p + 1]));
  }
  return out;
}

std::span<const uint8_t> AvatarToc::Opaque(size_t i) const {
  return std::span<const uint8_t>(data_.data() + records_.at(i).offset + 4, kColumnsAt - 4);
}

std::span<const uint8_t> AvatarToc::Blob(size_t i) const {
  const auto& rec = records_.at(i);
  return std::span<const uint8_t>(data_.data() + rec.blob_offset, rec.blob_size);
}

}  // namespace x360n::avatar
