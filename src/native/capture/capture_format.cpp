#include "capture_format.h"
#include <xxhash.h>
namespace x360n::native::capture {
Hash HashBytes(const void* data, size_t size) {
  const auto hash = XXH3_128bits(data, size);
  return {hash.low64, hash.high64};
}
Writer::Writer(const std::filesystem::path& path, const Header& header) : path_(path) {
  if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
  out_.open(path, std::ios::binary | std::ios::trunc);
  if (!out_) throw std::runtime_error("Native capture: cannot create " + path.string());
  out_.write(reinterpret_cast<const char*>(&header), sizeof(header));
  bytes_ = sizeof(header);
}
Writer::~Writer() = default;
void Writer::Begin(Record type, uint64_t size) {
  if (pending_) throw std::logic_error("Native capture: unfinished record");
  const uint32_t value = uint32_t(type);
  out_.write(reinterpret_cast<const char*>(&value), 4);
  out_.write(reinterpret_cast<const char*>(&size), 8);
  bytes_ += 12;
  pending_ = size;
}
void Writer::Put(const void* data, size_t size) {
  if (size > pending_) throw std::logic_error("Native capture: record overflow");
  out_.write(static_cast<const char*>(data), std::streamsize(size));
  pending_ -= size;
  bytes_ += size;
}
uint64_t Writer::Blob(const void* data, size_t size) {
  const auto hash = HashBytes(data, size);
  if (auto found = blobs_.find(hash); found != blobs_.end()) return found->second;
  const uint64_t id = next_blob_++;
  Begin(Record::kBlob, 8 + size);
  Put(id);
  Put(data, size);
  blobs_.emplace(hash, id);
  blob_bytes_ += size;
  return id;
}
void Writer::Simple(Record type, std::initializer_list<uint32_t> words) {
  Begin(type, words.size() * 4);
  for (uint32_t word : words) Put(word);
}
void Writer::Present(const PresentRecord& p) {
  const uint64_t size = 4 * (9 + 2 + 17 + 1 + p.lut.size() + 1) + 16 + p.written.size() * (8 + 16);
  Begin(Record::kPresent, size);
  Put(p.texture);
  Put(p.post_effect);
  Put(p.has_overlay);
  Put(p.overlay);
  Put(uint32_t(p.lut.size()));
  Put(p.lut.data(), p.lut.size() * 4);
  Put(p.frontbuffer);
  Put(uint32_t(p.written.size()));
  for (const auto& range : p.written) { Put(range.address); Put(range.length); Put(range.hash); }
}
void Writer::Finish(uint32_t frames) {
  if (pending_) throw std::logic_error("Native capture: unfinished record at finish");
  out_.seekp(offsetof(Header, frames));
  out_.write(reinterpret_cast<const char*>(&frames), 4);
  out_.close();
  if (!out_) throw std::runtime_error("Native capture: write failed for " + path_.string());
}

Reader::Reader(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("Native capture: cannot open " + path.string());
  data_.assign(std::istreambuf_iterator<char>(in), {});
  if (data_.size() < sizeof(Header)) throw std::runtime_error("Native capture: truncated header");
  std::memcpy(&header_, data_.data(), sizeof(Header));
  if (std::memcmp(header_.magic, kMagic, sizeof(kMagic)) || header_.version != kVersion)
    throw std::runtime_error("Native capture: not a version 1 Native frame capture");
}
bool Reader::Next(Record& type, const uint8_t*& payload, uint64_t& size) {
  for (;;) {
    if (position_ == data_.size()) return false;
    if (data_.size() - position_ < 12) throw std::runtime_error("Native capture: truncated record header");
    uint32_t raw; std::memcpy(&raw, data_.data() + position_, 4);
    std::memcpy(&size, data_.data() + position_ + 4, 8);
    position_ += 12;
    if (size > data_.size() - position_) throw std::runtime_error("Native capture: truncated record");
    payload = data_.data() + position_;
    position_ += size;
    type = Record(raw);
    if (type != Record::kBlob) return true;
    if (size < 8) throw std::runtime_error("Native capture: invalid blob");
    uint64_t id; std::memcpy(&id, payload, 8);
    if (id == blobs_.size()) blobs_.emplace_back(payload + 8, size - 8);
    else if (id > blobs_.size()) throw std::runtime_error("Native capture: blob out of order");
  }
}
std::span<const uint8_t> Reader::blob(uint64_t id) const {
  if (id >= blobs_.size()) throw std::runtime_error("Native capture: reference to an unknown blob");
  return blobs_[id];
}
PresentRecord Reader::ParsePresent(const uint8_t* payload, uint64_t size) {
  PresentRecord p;
  size_t word = 0;
  for (auto& value : p.texture) value = Word(payload, size, word++);
  p.post_effect = Word(payload, size, word++);
  p.has_overlay = Word(payload, size, word++);
  for (auto& value : p.overlay) value = Word(payload, size, word++);
  const uint32_t lut = Word(payload, size, word++);
  for (uint32_t i = 0; i < lut; ++i) p.lut.push_back(Word(payload, size, word++));
  size_t offset = word * 4;
  if (offset + 16 + 4 > size) throw std::runtime_error("Native capture: truncated present");
  std::memcpy(p.frontbuffer.data(), payload + offset, 16); offset += 16;
  uint32_t written; std::memcpy(&written, payload + offset, 4); offset += 4;
  if (offset + uint64_t(written) * 24 > size) throw std::runtime_error("Native capture: truncated written ranges");
  for (uint32_t i = 0; i < written; ++i) {
    WrittenRange range;
    std::memcpy(&range.address, payload + offset, 4);
    std::memcpy(&range.length, payload + offset + 4, 4);
    std::memcpy(range.hash.data(), payload + offset + 8, 16);
    p.written.push_back(range);
    offset += 24;
  }
  return p;
}
}
