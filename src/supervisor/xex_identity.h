// Reads the identity of an XEX2 file from its headers. The input is untrusted:
// every offset and size is bounds-checked against the header block.
// Structure layouts come from the SDK (rex/system/util/xex2_info.h).
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace x360n {

struct XexVersion {
  uint32_t raw = 0;
  std::string ToString() const;  // major.minor.build.qfe
};

struct XexImportLibrary {
  std::string name;
  XexVersion version;
  XexVersion version_min;
  // Raw import_table length. Function imports use two entries (record + thunk),
  // variable imports one, so this is not a function count. Ordinals live in
  // the (possibly compressed/encrypted) image and are not decoded here.
  uint32_t table_entries = 0;
};

struct XexIdentity {
  uint32_t module_flags = 0;
  uint32_t header_size = 0;
  uint32_t header_count = 0;

  // XEX_HEADER_EXECUTION_INFO (absent in some system modules)
  bool has_execution_info = false;
  uint32_t title_id = 0;
  uint32_t media_id = 0;
  XexVersion version;
  XexVersion base_version;
  uint8_t platform = 0;
  uint8_t executable_type = 0;
  uint8_t disc_number = 0;
  uint8_t disc_count = 0;
  uint32_t savegame_id = 0;

  std::optional<uint32_t> entry_point;
  std::optional<uint32_t> image_base;
  std::optional<std::string> original_pe_name;
  std::optional<uint16_t> encryption_type;
  std::optional<uint16_t> compression_type;
  std::optional<uint32_t> image_size;    // security info
  std::optional<uint32_t> load_address;  // security info
  std::optional<uint32_t> region;        // security info
  std::vector<XexImportLibrary> imports;
  std::vector<std::string> warnings;     // malformed optional data that was skipped
};

struct XexParseResult {
  std::optional<XexIdentity> identity;
  std::string error;  // set when identity is empty
};

// `header` must contain at least the first header_size bytes of the file;
// the parser reports an error if it is shorter.
XexParseResult ParseXexIdentity(std::span<const uint8_t> header);

bool IsDashboardTitleId(uint32_t title_id);  // 0xFFFE07D1, as in Xenia's kDashboardID
std::vector<std::string> DescribeModuleFlags(uint32_t flags);

}  // namespace x360n
