// miniz is the public-domain copy supplied by the pinned SDL dependency.
// Use callbacks for Unicode paths, bounded streaming and exclusive outputs.
#include <cstdlib>
#include <cstring>
#define SDL_malloc std::malloc
#define SDL_free std::free
#define SDL_realloc std::realloc
#define MINIZ_NO_DEFLATE_APIS
#define MINIZ_NO_STDIO
#define MINIZ_NO_ARCHIVE_WRITING_APIS
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#define MINIZ_NO_TIME
#define MINIZ_STATIC static
#include <miniz.h>
#undef SDL_malloc
#undef SDL_free
#undef SDL_realloc
#include "safe_zip.h"
#include "paths.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;
namespace x360n::content {
namespace {
constexpr uint64_t kMaxFile = 512ull << 20, kMaxTotal = 2ull << 30;
struct Zip {
  std::ifstream input;
  mz_zip_archive archive{};
  explicit Zip(const fs::path& path) : input(path, std::ios::binary) {
    const auto size = fs::file_size(path);
    if (!input || size > kMaxTotal) throw std::runtime_error("ZIP: archive exceeds size limit or cannot be read");
    // Bound central-directory allocation before entering miniz. Only ordinary,
    // single-volume ZIP is needed by the USB update (ZIP64 fails closed).
    std::vector<unsigned char> tail(size_t(std::min<uint64_t>(size,65557)));
    input.seekg(std::streamoff(size-tail.size()));
    input.read(reinterpret_cast<char*>(tail.data()),tail.size());
    bool eocd=false;
    for (size_t end=tail.size();end>=22;--end) {
      const auto p=end-22;
      if (tail[p]!=0x50 || tail[p+1]!=0x4B || tail[p+2]!=5 || tail[p+3]!=6) continue;
      auto le=[&](size_t o,size_t n) {uint64_t v=0;for(size_t j=0;j<n;++j)v|=uint64_t(tail[p+o+j])<<(8*j);return v;};
      if (end+le(20,2)!=tail.size()) continue;
      const auto offset=le(16,4),bytes=le(12,4),count=le(10,2);
      if (le(4,2) || le(6,2) || le(8,2)!=count || count>4096 ||
          bytes>(4u<<20) || offset>size-tail.size()+p || bytes>size-tail.size()+p-offset)
        throw std::runtime_error("ZIP: unsupported volume or central directory exceeds limits");
      eocd=true;break;
    }
    if (!eocd) throw std::runtime_error("ZIP: missing/truncated end record");
    archive.m_pIO_opaque = this;
    archive.m_pRead = [](void* p, mz_uint64 offset, void* buffer, size_t size) -> size_t {
      auto& in = static_cast<Zip*>(p)->input;
      in.clear(); in.seekg(std::streamoff(offset));
      in.read(static_cast<char*>(buffer), std::streamsize(size)); return size_t(in.gcount());
    };
    if (!mz_zip_reader_init(&archive, size, 0)) throw std::runtime_error("ZIP: invalid or unsupported archive (ZIP64 is not supported)");
  }
  ~Zip() { mz_zip_reader_end(&archive); }
};
struct Output {
  std::ofstream stream;
  uint64_t size = 0, limit;
};
}
void ExtractUpdateZip(const fs::path& source, const fs::path& dest) {
  Zip zip(source);
  auto* z = &zip.archive;
  if (z->m_total_files > 4096) throw std::runtime_error("ZIP: too many entries");
  struct Item { mz_uint index; fs::path path; uint64_t size; bool directory; };
  std::vector<Item> items;
  std::map<std::string, bool> names;
  std::map<uint64_t,uint64_t> ranges;
  uint64_t total = 0;
  for (mz_uint i = 0; i < z->m_total_files; ++i) {
    mz_zip_archive_file_stat stat{};
    if (!mz_zip_reader_file_stat(z, i, &stat)) throw std::runtime_error("ZIP: invalid entry");
    const auto length = mz_zip_reader_get_filename(z, i, nullptr, 0);
    if (length < 2 || length > 240) throw std::runtime_error("ZIP: unsafe filename length");
    std::string name(length, '\0');
    mz_zip_reader_get_filename(z, i, name.data(), length);
    name.pop_back();
    if (name.find('\0') != std::string::npos || name.find('\\') != std::string::npos)
      throw std::runtime_error("ZIP: unsafe filename");
    unsigned char local[30]{};
    if (stat.m_local_header_ofs > z->m_central_directory_file_ofs ||
        sizeof(local) > z->m_central_directory_file_ofs-stat.m_local_header_ofs ||
        z->m_pRead(z->m_pIO_opaque,stat.m_local_header_ofs,local,sizeof(local)) != sizeof(local))
      throw std::runtime_error("ZIP: invalid local header range");
    auto le16=[&](size_t p){return unsigned(local[p]) | unsigned(local[p+1])<<8;};
    auto le32=[&](size_t p){return uint32_t(le16(p)) | uint32_t(le16(p+2))<<16;};
    if (std::string_view(reinterpret_cast<char*>(local),4) != std::string_view("PK\3\4",4) ||
        le16(6)!=stat.m_bit_flag || le16(8)!=stat.m_method || le16(26)!=name.size())
      throw std::runtime_error("ZIP: conflicting local and central headers");
    if (!(stat.m_bit_flag&8) && (le32(14)!=stat.m_crc32 || le32(18)!=stat.m_comp_size || le32(22)!=stat.m_uncomp_size))
      throw std::runtime_error("ZIP: conflicting local CRC/lengths");
    const uint64_t begin=stat.m_local_header_ofs;
    const uint64_t data=begin+30+le16(26)+le16(28);
    if (data>z->m_central_directory_file_ofs || stat.m_comp_size>z->m_central_directory_file_ofs-data)
      throw std::runtime_error("ZIP: data outside local file region");
    std::string local_name(name.size(),'\0');
    if (z->m_pRead(z->m_pIO_opaque,begin+30,local_name.data(),local_name.size())!=local_name.size() || local_name!=name)
      throw std::runtime_error("ZIP: conflicting local filename");
    uint64_t end=data+stat.m_comp_size;
    if (stat.m_bit_flag&8) {
      unsigned char descriptor[16]{};
      if (12>z->m_central_directory_file_ofs-end || z->m_pRead(z->m_pIO_opaque,end,descriptor,12)!=12)
        throw std::runtime_error("ZIP: truncated data descriptor");
      auto word=[&](size_t p){return uint32_t(descriptor[p]) | uint32_t(descriptor[p+1])<<8 |
                                     uint32_t(descriptor[p+2])<<16 | uint32_t(descriptor[p+3])<<24;};
      const size_t signature=word(0)==0x08074B50?4:0;
      if (signature && (16>z->m_central_directory_file_ofs-end || z->m_pRead(z->m_pIO_opaque,end+12,descriptor+12,4)!=4))
        throw std::runtime_error("ZIP: truncated signed data descriptor");
      if (word(signature)!=stat.m_crc32 || word(signature+4)!=stat.m_comp_size || word(signature+8)!=stat.m_uncomp_size)
        throw std::runtime_error("ZIP: conflicting data descriptor");
      end+=12+signature;
    }
    const auto next=ranges.lower_bound(begin);
    if ((next!=ranges.end() && next->first<end) || (next!=ranges.begin() && std::prev(next)->second>begin))
      throw std::runtime_error("ZIP: overlapping local entries");
    ranges.emplace(begin,end);
    const bool directory = name.back() == '/';
    if (directory) name.pop_back();
    std::string key = name;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (names.contains(key)) throw std::runtime_error("ZIP: duplicate/conflicting entry: " + name);
    names.emplace(key, directory);
    fs::path relative;
    size_t start = 0, depth = 0;
    do {
      const auto end = name.find('/', start);
      const auto part = name.substr(start, end == std::string::npos ? end : end-start);
      if (!IsSafePackageEntryName(part) || ++depth > 16) throw std::runtime_error("ZIP: unsafe entry: " + name);
      relative /= PathFromUtf8(part);
      if (end == std::string::npos) break;
      start = end + 1;
    } while (true);
    const auto mode = (stat.m_external_attr >> 16) & 0170000;
    if ((mode && mode != (directory ? 0040000u : 0100000u)) ||
        (stat.m_external_attr & 0x400) || (stat.m_bit_flag & (1 | 0x40 | 0x2000)) ||
        (stat.m_method != 0 && stat.m_method != 8))
      throw std::runtime_error("ZIP: links, encryption or unsupported entry type: " + name);
    if (stat.m_uncomp_size > kMaxFile || stat.m_uncomp_size > kMaxTotal-total ||
        (directory && stat.m_uncomp_size)) throw std::runtime_error("ZIP: extraction size limit exceeded");
    total += stat.m_uncomp_size;
    items.push_back({i, relative, stat.m_uncomp_size, directory});
  }
  // Detect file/directory collisions before writing any entry.
  for (const auto& [name, dir] : names) {
    for (auto p = name.find('/'); p != std::string::npos; p = name.find('/',p+1))
      if (auto it = names.find(name.substr(0,p)); it != names.end() && !it->second)
        throw std::runtime_error("ZIP: file/directory collision");
  }
  if (!fs::create_directory(dest)) throw std::runtime_error("ZIP: destination already exists");
  uint64_t done = 0;
  for (const auto& item : items) {
    const auto path = dest / item.path;
    if (item.directory) { fs::create_directories(path); continue; }
    fs::create_directories(path.parent_path());
    Output out{std::ofstream(path, std::ios::binary | std::ios::out | std::ios::noreplace), 0, item.size};
    if (!out.stream) throw std::runtime_error("ZIP: cannot create exclusive output");
    const auto write = [](void* p, mz_uint64 offset, const void* data, size_t size) -> size_t {
      auto& o = *static_cast<Output*>(p);
      if (offset != o.size || size > o.limit-o.size) return 0;
      o.stream.write(static_cast<const char*>(data), std::streamsize(size));
      if (!o.stream) return 0;
      o.size += size; return size;
    };
    if (!mz_zip_reader_extract_to_callback(z, item.index, write, &out, 0))
      throw std::runtime_error("ZIP: CRC/decompression failed: " + item.path.string());
    out.stream.close();
    if (!out.stream || out.size != item.size) throw std::runtime_error("ZIP: incomplete output");
    done += item.size;
    std::cout << "X360N_STAGE ZIP " << done << "/" << total << " bytes\n" << std::flush;
  }
}
}
