// STFS block addressing follows the pinned ReXGlue/Xenia container reader:
// Copyright 2020 Ben Vanik; adapted by Tom Clay, 2026. BSD-3-Clause,
// licenses/rexglue-sdk.txt. This preflight never extracts or interprets files.
#include "update_container.h"
#include "paths.h"
#include <rex/filesystem/devices/stfs_container_device.h>
#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>
#include <vector>

namespace fs=std::filesystem;
namespace x360n::content {
void ValidateUpdateContainer(const fs::path& path) {
  using namespace rex::filesystem;
  const auto header=StfsContainerDevice::ReadPackageHeader(path);
  const auto size=fs::file_size(path);
  if (!header || header->header.magic!=XContentPackageType::kPirs ||
      header->metadata.volume_type!=XContentVolumeType::kStfs || header->metadata.data_file_count!=0 ||
      !header->metadata.volume_descriptor.stfs.flags.bits.read_only_format || size>(512ull<<20))
    throw std::runtime_error("PIRS: expected a bounded read-only STFS update container");
  const auto& desc=header->metadata.volume_descriptor.stfs;
  const uint64_t start=(uint64_t(header->header.header_size)+4095)&~uint64_t(4095);
  const uint32_t blocks=desc.total_block_count, tables=desc.file_table_block_count;
  if (start<sizeof(StfsHeader) || start>=size || !blocks || blocks>size/4096 || !tables || tables>64)
    throw std::runtime_error("PIRS: invalid/excessive allocation or file table");
  std::ifstream input(path,std::ios::binary);
  auto read=[&](uint64_t offset,void* buffer,size_t count) {
    if (offset>size || count>size-offset) throw std::runtime_error("PIRS: block outside package");
    input.clear();input.seekg(offset);input.read(static_cast<char*>(buffer),count);
    if (size_t(input.gcount())!=count) throw std::runtime_error("PIRS: truncated block");
  };
  auto offset=[&](uint32_t block) {
    if (block>=blocks) throw std::runtime_error("PIRS: block index outside allocation");
    uint64_t physical=block;
    for (uint64_t level:{170ull,28900ull,4913000ull}) {
      physical+=(block+level)/level;if(block<level)break;
    }
    const uint64_t result=start+physical*4096;
    if (result>size || 4096>size-result) throw std::runtime_error("PIRS: data block outside package");
    return result;
  };
  auto next=[&](uint32_t block) {
    uint64_t hash=block<170?0:(block/170)*171+(block/28900)+1+(block>=28900?1:0);
    unsigned char bytes[3];read(start+hash*4096+(block%170)*24+21,bytes,3);
    return uint32_t(bytes[0])<<16 | uint32_t(bytes[1])<<8 | bytes[2];
  };
  struct Parent {bool directory;unsigned depth;};
  std::vector<Parent> parents;
  std::set<uint32_t> used;
  std::set<std::pair<uint16_t,std::string>> names;
  uint64_t declared=0;
  uint32_t table=desc.file_table_block_number();
  for (uint32_t n=0;n<tables;++n) {
    if (!used.insert(table).second) throw std::runtime_error("PIRS: overlapping/cyclic table blocks");
    StfsDirectoryBlock directory{};read(offset(table),&directory,sizeof(directory));
    for (const auto& e:directory.entries) {
      if (!e.name[0]) break;
      const auto len=e.flags.name_length & 0x3F;
      if (!len || len>sizeof(e.name)) throw std::runtime_error("PIRS: invalid filename length");
      std::string name(reinterpret_cast<const char*>(e.name),len);
      if (!IsSafePackageEntryName(name)) throw std::runtime_error("PIRS: unsafe entry name");
      for(auto& c:name)if(c>='A'&&c<='Z')c+='a'-'A';
      const uint16_t parent=e.directory_index;
      if (!names.emplace(parent,name).second) throw std::runtime_error("PIRS: duplicate/conflicting entry");
      unsigned depth=0;
      if (parent!=0xFFFF) {
        if (parent>=parents.size() || !parents[parent].directory) throw std::runtime_error("PIRS: invalid parent index");
        depth=parents[parent].depth+1;
      }
      if (depth>32 || parents.size()>=4096) throw std::runtime_error("PIRS: directory/entry limit exceeded");
      parents.push_back({bool(e.flags.directory),depth});
      if (e.flags.directory) continue;
      const uint64_t length=e.length;
      if (length>(512ull<<20) || length>(2ull<<30)-declared) throw std::runtime_error("PIRS: declared extraction limit exceeded");
      declared+=length;
      const auto allocated=(length+4095)/4096;
      if (allocated!=e.allocated_data_blocks()) throw std::runtime_error("PIRS: allocation length mismatch");
      auto block=e.start_block_number();
      for (uint64_t i=0;i<allocated;++i) {
        offset(block);
        if (!used.insert(block).second) throw std::runtime_error("PIRS: overlapping/cyclic file blocks");
        block=next(block);
      }
      if (allocated && block!=0xFFFFFF) throw std::runtime_error("PIRS: unterminated file chain");
    }
    table=next(table);
  }
  if (table!=0xFFFFFF) throw std::runtime_error("PIRS: file table chain length mismatch");
}
}
