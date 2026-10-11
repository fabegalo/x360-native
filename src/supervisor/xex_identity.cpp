#include "xex_identity.h"

#include <cstddef>
#include <cstring>
#include <type_traits>

#include <fmt/format.h>
#include <rex/system/util/xex2_info.h>

namespace x360n {
using namespace rex;  // SDK XEX structures and enums (xex2_info.h)

namespace {

constexpr uint32_t kXex2Magic = 0x58455832;  // 'XEX2'
constexpr size_t kFixedHeaderSize = offsetof(xex2_header, headers);

uint32_t Be32(std::span<const uint8_t> b, size_t off) {
  return (uint32_t(b[off]) << 24) | (uint32_t(b[off + 1]) << 16) | (uint32_t(b[off + 2]) << 8) |
         uint32_t(b[off + 3]);
}

bool InRange(size_t limit, size_t off, size_t len) { return off <= limit && len <= limit - off; }

// Copies an SDK structure out of the buffer (avoids unaligned/aliased access).
template <typename T>
std::optional<T> ReadStruct(std::span<const uint8_t> b, size_t limit, size_t off) {
  static_assert(std::is_trivially_copyable_v<T>);
  if (!InRange(limit, off, sizeof(T))) return std::nullopt;
  T out;
  std::memcpy(&out, b.data() + off, sizeof(T));
  return out;
}

}  // namespace

std::string XexVersion::ToString() const {
  xex2_version v{raw};
  return fmt::format("{}.{}.{}.{}", uint32_t(v.major), uint32_t(v.minor), uint32_t(v.build),
                     uint32_t(v.qfe));
}

bool IsDashboardTitleId(uint32_t title_id) { return title_id == 0xFFFE07D1; }

std::vector<std::string> DescribeModuleFlags(uint32_t flags) {
  static constexpr std::pair<uint32_t, const char*> kNames[] = {
      {XEX_MODULE_TITLE, "title"},
      {XEX_MODULE_EXPORTS_TO_TITLE, "exports_to_title"},
      {XEX_MODULE_SYSTEM_DEBUGGER, "system_debugger"},
      {XEX_MODULE_DLL_MODULE, "dll_module"},
      {XEX_MODULE_MODULE_PATCH, "module_patch"},
      {XEX_MODULE_PATCH_FULL, "patch_full"},
      {XEX_MODULE_PATCH_DELTA, "patch_delta"},
      {XEX_MODULE_USER_MODE, "user_mode"},
  };
  std::vector<std::string> out;
  uint32_t known = 0;
  for (auto [bit, name] : kNames) {
    known |= bit;
    if (flags & bit) out.emplace_back(name);
  }
  if (flags & ~known) out.push_back(fmt::format("unknown_0x{:08X}", flags & ~known));
  return out;
}

XexParseResult ParseXexIdentity(std::span<const uint8_t> b) {
  XexParseResult r;
  if (b.size() < kFixedHeaderSize) {
    r.error = fmt::format("file too small for an XEX2 header ({} bytes)", b.size());
    return r;
  }
  if (Be32(b, 0) != kXex2Magic) {
    r.error = fmt::format("not an XEX2 file (magic 0x{:08X})", Be32(b, 0));
    return r;
  }

  XexIdentity id;
  id.module_flags = Be32(b, offsetof(xex2_header, module_flags));
  id.header_size = Be32(b, offsetof(xex2_header, header_size));
  uint32_t security_offset = Be32(b, offsetof(xex2_header, security_offset));
  id.header_count = Be32(b, offsetof(xex2_header, header_count));

  if (id.header_size < kFixedHeaderSize) {
    r.error = fmt::format("header_size 0x{:X} is smaller than the fixed header", id.header_size);
    return r;
  }
  if (id.header_size > b.size()) {
    r.error = fmt::format("header_size 0x{:X} exceeds the {} bytes provided", id.header_size,
                          b.size());
    return r;
  }
  const size_t limit = id.header_size;
  if (!InRange(limit, kFixedHeaderSize, size_t(id.header_count) * sizeof(xex2_opt_header))) {
    r.error = fmt::format("header_count {} does not fit in header_size 0x{:X}", id.header_count,
                          id.header_size);
    return r;
  }

  // Variable-size optional data: offset into the header block, first u32 is its size.
  auto variable = [&](uint32_t key, uint32_t offset,
                      size_t min_size) -> std::optional<std::span<const uint8_t>> {
    if (!InRange(limit, offset, 4)) {
      id.warnings.push_back(fmt::format("header 0x{:08X}: offset 0x{:X} out of range", key, offset));
      return std::nullopt;
    }
    uint32_t size = Be32(b, offset);
    if (size < min_size || !InRange(limit, offset, size)) {
      id.warnings.push_back(fmt::format("header 0x{:08X}: size 0x{:X} invalid", key, size));
      return std::nullopt;
    }
    return b.subspan(offset, size);
  };

  for (uint32_t i = 0; i < id.header_count; ++i) {
    size_t entry = kFixedHeaderSize + size_t(i) * sizeof(xex2_opt_header);
    uint32_t key = Be32(b, entry);
    uint32_t value = Be32(b, entry + 4);  // inline value or offset, see GetOptHeader in the SDK
    switch (key) {
      case XEX_HEADER_ENTRY_POINT:
        id.entry_point = value;
        break;
      case XEX_HEADER_IMAGE_BASE_ADDRESS:
        id.image_base = value;
        break;
      case XEX_HEADER_EXECUTION_INFO: {
        auto info = ReadStruct<xex2_opt_execution_info>(b, limit, value);
        if (!info) {
          id.warnings.push_back("execution info out of range");
          break;
        }
        id.has_execution_info = true;
        id.media_id = info->media_id;
        id.version.raw = info->version_value;
        id.base_version.raw = info->base_version_value;
        id.title_id = info->title_id;
        id.platform = info->platform;
        id.executable_type = info->executable_table;
        id.disc_number = info->disc_number;
        id.disc_count = info->disc_count;
        id.savegame_id = info->savegame_id;
        break;
      }
      case XEX_HEADER_FILE_FORMAT_INFO: {
        if (auto data = variable(key, value, 8)) {
          id.encryption_type = uint16_t(((*data)[4] << 8) | (*data)[5]);
          id.compression_type = uint16_t(((*data)[6] << 8) | (*data)[7]);
        }
        break;
      }
      case XEX_HEADER_ORIGINAL_PE_NAME: {
        if (auto data = variable(key, value, 4)) {
          auto chars = data->subspan(4);
          size_t n = 0;
          while (n < chars.size() && chars[n] != 0) ++n;
          id.original_pe_name = std::string(reinterpret_cast<const char*>(chars.data()), n);
        }
        break;
      }
      case XEX_HEADER_IMPORT_LIBRARIES: {
        auto data = variable(key, value, 12);
        if (!data) break;
        uint32_t table_size = Be32(*data, 4);
        uint32_t table_count = Be32(*data, 8);
        if (!InRange(data->size(), 12, table_size)) {
          id.warnings.push_back("import string table out of range");
          break;
        }
        std::vector<std::string> names;
        auto table = data->subspan(12, table_size);
        for (size_t p = 0; p < table.size() && names.size() < table_count;) {
          size_t n = 0;
          while (p + n < table.size() && table[p + n] != 0) ++n;
          names.emplace_back(reinterpret_cast<const char*>(table.data() + p), n);
          p += n + 1;
          if (p % 4) p += 4 - (p % 4);  // entries are 4-byte aligned, as in XexModule
        }
        constexpr size_t kLibFixed = offsetof(xex2_import_library, import_table);
        for (size_t off = 12 + size_t(table_size); off < data->size();) {
          if (!InRange(data->size(), off, kLibFixed)) {
            id.warnings.push_back("truncated import library record");
            break;
          }
          auto lib = data->subspan(off);
          uint32_t lib_size = Be32(lib, 0);
          if (lib_size == 0) break;
          if (lib_size < kLibFixed || lib_size > lib.size()) {
            id.warnings.push_back(fmt::format("import library record size 0x{:X} invalid", lib_size));
            break;
          }
          XexImportLibrary out;
          uint16_t name_index = uint16_t((lib[offsetof(xex2_import_library, name_index)] << 8) |
                                         lib[offsetof(xex2_import_library, name_index) + 1]);
          size_t idx = name_index & 0xFF;
          out.name = idx < names.size() ? names[idx] : fmt::format("<bad name index {}>", idx);
          out.version.raw = Be32(lib, offsetof(xex2_import_library, version_value));
          out.version_min.raw = Be32(lib, offsetof(xex2_import_library, version_min_value));
          out.table_entries = uint32_t((lib[offsetof(xex2_import_library, count)] << 8) |
                                       lib[offsetof(xex2_import_library, count) + 1]);
          id.imports.push_back(std::move(out));
          off += lib_size;
        }
        break;
      }
      default:
        break;
    }
  }

  // Security info sits inside the header block (before the PE image).
  constexpr size_t kSecNeeded = offsetof(xex2_security_info, page_descriptor_count) + 4;
  if (InRange(limit, security_offset, kSecNeeded)) {
    id.image_size = Be32(b, security_offset + offsetof(xex2_security_info, image_size));
    id.load_address = Be32(b, security_offset + offsetof(xex2_security_info, load_address));
    id.region = Be32(b, security_offset + offsetof(xex2_security_info, region));
  } else {
    id.warnings.push_back(fmt::format("security info at 0x{:X} out of range", security_offset));
  }

  r.identity = std::move(id);
  return r;
}

}  // namespace x360n
