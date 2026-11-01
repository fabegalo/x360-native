#include "capture_format.h"
#include <xxhash.h>
#include <algorithm>
#include <string>
namespace x360n::native::capture {
namespace {
constexpr uint32_t kMaxPrimitiveType = 0x16;   // xenos::PrimitiveType::k2DTriStrip
constexpr uint32_t kMaxDrawPurpose = 3;        // DrawPurpose::kGuestWithoutVertexExports
constexpr uint32_t kGuestSyncSources = 5;      // GuestSync::kGuestSyncSources
constexpr uint32_t kMaxShaderId = 1u << 24;
[[noreturn]] void Fail(const std::string& what) { throw std::runtime_error("Native capture: " + what); }
void Require(bool ok, const char* what) { if (!ok) Fail(what); }
void Range(uint64_t address, uint64_t length, const char* what) {
  if (length > kPhysicalSize || address > kPhysicalSize - length) Fail(std::string(what) + " outside guest physical memory");
}
uint32_t U32(const uint8_t* p, uint64_t size, size_t index) {
  if ((uint64_t(index) + 1) * 4 > size) Fail("truncated record");
  uint32_t value; std::memcpy(&value, p + index * 4, 4); return value;
}
uint64_t U64(const uint8_t* p, uint64_t size, size_t offset) {
  if (offset + 8 > size) Fail("truncated record");
  uint64_t value; std::memcpy(&value, p + offset, 8); return value;
}
Hash HashAt(const uint8_t* p, uint64_t size, size_t offset) {
  if (offset + 16 > size) Fail("truncated record");
  Hash hash; std::memcpy(hash.data(), p + offset, 16); return hash;
}
bool Flag(uint32_t value, const char* what) { if (value > 1) Fail(std::string("invalid ") + what); return value != 0; }
}
Hash HashBytes(const void* data, size_t size) {
  const auto hash = XXH3_128bits(data, size);
  return {hash.low64, hash.high64};
}
bool HashRgbxImage(const uint8_t* data, size_t size, uint32_t width, uint32_t height, size_t stride, Hash& out) {
  if (!data || !width || !height || width > kMaxOutputSide || height > kMaxOutputSide) return false;
  const size_t row = size_t(width) * 4;
  if (stride < row || size < stride * (height - 1) + row) return false;
  XXH3_state_t* state = XXH3_createState();
  if (!state) return false;
  XXH3_128bits_reset(state);
  const uint32_t extent[2]{width, height};
  XXH3_128bits_update(state, extent, sizeof(extent));
  std::vector<uint8_t> rgb(size_t(width) * 3);
  for (uint32_t y = 0; y < height; ++y) {
    const uint8_t* source = data + stride * y;
    for (uint32_t x = 0; x < width; ++x) std::memcpy(&rgb[x * 3], source + x * 4, 3);
    XXH3_128bits_update(state, rgb.data(), rgb.size());
  }
  const auto hash = XXH3_128bits_digest(state);
  XXH3_freeState(state);
  out = {hash.low64, hash.high64};
  return true;
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
  if (!p.output_width || !p.output_height)
    throw std::runtime_error("Native capture: present without the composed image (not recordable)");
  const uint64_t size = 4 * (9 + 2 + 17 + 1 + p.lut.size() + 1) + 16 + p.written.size() * (8 + 16) + 8 + 16 + 8;
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
  Put(p.output_width);
  Put(p.output_height);
  Put(p.output);
  Put(p.aspect_x);
  Put(p.aspect_y);
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
  Open();
}
Reader::Reader(std::vector<uint8_t> bytes) : data_(std::move(bytes)) { Open(); }
void Reader::Open() {
  Require(data_.size() >= sizeof(Header), "truncated header");
  std::memcpy(&header_, data_.data(), sizeof(Header));
  Require(!std::memcmp(header_.magic, kMagic, sizeof(kMagic)), "not a Native frame capture");
  if (header_.version != kVersion)
    Fail("capture version " + std::to_string(header_.version) + ", this build reads version " + std::to_string(kVersion) + " (record it again)");
  Require(header_.register_count && header_.register_count <= 0x10000, "invalid register count");
  Require(header_.scale_x >= 1 && header_.scale_x <= 4 && header_.scale_y >= 1 && header_.scale_y <= 4, "invalid resolution scale");
  Require(std::find(std::begin(header_.backend), std::end(header_.backend), '\0') != std::end(header_.backend),
          "unterminated backend name");
}
bool Reader::Next(Record& type, const uint8_t*& payload, uint64_t& size) {
  for (;;) {
    if (position_ == data_.size()) return false;
    Require(data_.size() - position_ >= 12, "truncated record header");
    uint32_t raw; std::memcpy(&raw, data_.data() + position_, 4);
    std::memcpy(&size, data_.data() + position_ + 4, 8);
    position_ += 12;
    Require(size <= data_.size() - position_, "truncated record");
    payload = data_.data() + position_;
    position_ += size;
    Require(raw >= uint32_t(Record::kBlob) && raw <= uint32_t(Record::kEdram), "unknown record type");
    type = Record(raw);
    if (type != Record::kBlob) return true;
    const uint64_t id = U64(payload, size, 0);
    // Blobs are numbered in file order; a rewound pass sees each one again.
    if (id == blobs_.size()) blobs_.emplace_back(payload + 8, size - 8);
    else Require(id < blobs_.size() && blobs_[id].data() == payload + 8, "blob out of order");
  }
}
std::span<const uint8_t> Reader::blob(uint64_t id) const {
  Require(id < blobs_.size(), "reference to an unknown blob");
  return blobs_[id];
}

MemoryRecord DecodeMemory(const Reader& reader, const uint8_t* p, uint64_t size) {
  Require(size == 16, "memory record size");
  MemoryRecord record{U32(p, size, 0), U32(p, size, 1), reader.blob(U64(p, size, 8))};
  Require(record.length != 0, "empty memory record");
  Range(record.address, record.length, "memory record");
  Require(record.bytes.size() == record.length, "memory record length differs from its blob");
  return record;
}
EdramRecord DecodeEdram(const Reader& reader, const uint8_t* p, uint64_t size) {
  Require(size == 12, "EDRAM record size");
  EdramRecord record{reader.blob(U64(p, size, 0)), Flag(U32(p, size, 2), "EDRAM sample counting flag")};
  Require(record.bytes.size() > 32 && record.bytes.size() % 4 == 0, "EDRAM snapshot size");
  return record;
}
std::vector<RegisterWrite> DecodeRegisters(const uint8_t* p, uint64_t size, uint32_t register_count) {
  const uint32_t count = U32(p, size, 0);
  Require(size == 4 + uint64_t(count) * 8, "register record size");
  std::vector<RegisterWrite> writes(count);
  for (uint32_t i = 0; i < count; ++i) {
    writes[i] = {U32(p, size, 1 + i * 2), U32(p, size, 2 + i * 2)};
    Require(writes[i].index < register_count, "register outside the register file");
  }
  return writes;
}
ShaderRecord DecodeShader(const uint8_t* p, uint64_t size) {
  ShaderRecord record{U32(p, size, 0), U32(p, size, 1), {}};
  const uint32_t count = U32(p, size, 2);
  Require(record.id < kMaxShaderId, "shader id");
  Require(record.type <= 1, "shader type");
  Require(count && count <= kMaxShaderDwords, "shader size");
  Require(size == 12 + uint64_t(count) * 4, "shader record size");
  record.words.resize(count);
  std::memcpy(record.words.data(), p + 12, size_t(count) * 4);
  return record;
}
DrawRecord DecodeDraw(const uint8_t* p, uint64_t size) {
  Require(size == 44, "draw record size");
  DrawRecord d{U32(p, size, 0), U32(p, size, 1), U32(p, size, 2), U32(p, size, 3), U32(p, size, 4),
               Flag(U32(p, size, 5), "draw index flag"), U32(p, size, 6), U32(p, size, 7), U32(p, size, 8),
               U32(p, size, 9), U32(p, size, 10)};
  Require(d.vs < kMaxShaderId, "draw without a valid vertex shader");
  Require(d.ps == kNoShader || d.ps < kMaxShaderId, "draw pixel shader id");
  Require(d.primitive <= kMaxPrimitiveType, "draw primitive type");
  Require(d.purpose <= kMaxDrawPurpose, "draw purpose");
  if (d.indexed) {
    Require(d.index_format <= 1 && d.index_endian <= 3, "draw index format");
    Range(d.index_base, d.index_length, "index buffer");
  }
  return d;
}
SyncRecord DecodeCompleteGuestWrites(const uint8_t* p, uint64_t size) {
  Require(size == 8, "guest sync record size");
  SyncRecord record{U32(p, size, 0), U32(p, size, 1)};
  Require(record.source < kGuestSyncSources, "guest sync source");
  return record;
}
ReadRecord DecodeSyncGuestRead(const uint8_t* p, uint64_t size) {
  Require(size == 12, "guest read record size");
  ReadRecord record{U32(p, size, 0), U32(p, size, 1), U32(p, size, 2)};
  Range(record.address, record.length, "guest read");
  return record;
}
SampleRecord DecodeReadSampleCounts(const uint8_t* p, uint64_t size) {
  Require(size == 36, "sample read record size");
  SampleRecord record{{}, Flag(U32(p, size, 0), "counting flag")};
  for (unsigned i = 0; i < 8; ++i) record.counts[i] = U32(p, size, 1 + i);
  return record;
}
SampleRecord DecodeSetSampleCounts(const uint8_t* p, uint64_t size) {
  Require(size == 36, "sample write record size");
  SampleRecord record{{}, Flag(U32(p, size, 8), "visibility flag")};
  for (unsigned i = 0; i < 8; ++i) record.counts[i] = U32(p, size, i);
  return record;
}
PresentRecord DecodePresent(const uint8_t* payload, uint64_t size) {
  PresentRecord p;
  size_t word = 0;
  for (auto& value : p.texture) value = U32(payload, size, word++);
  p.post_effect = U32(payload, size, word++);
  p.has_overlay = U32(payload, size, word++);
  for (auto& value : p.overlay) value = U32(payload, size, word++);
  const uint32_t lut = U32(payload, size, word++);
  Require(lut == (p.texture[8] ? 128u * 3 : 256u), "present gamma table size");
  for (uint32_t i = 0; i < lut; ++i) p.lut.push_back(U32(payload, size, word++));
  size_t offset = word * 4;
  p.frontbuffer = HashAt(payload, size, offset); offset += 16;
  const uint32_t written = U32(payload, size, offset / 4); offset += 4;
  Require(size == offset + uint64_t(written) * 24 + 8 + 16 + 8, "present record size");
  for (uint32_t i = 0; i < written; ++i) {
    WrittenRange range{U32(payload, size, offset / 4), U32(payload, size, offset / 4 + 1), HashAt(payload, size, offset + 8)};
    Range(range.address, range.length, "GPU-written range");
    p.written.push_back(range);
    offset += 24;
  }
  p.output_width = U32(payload, size, offset / 4);
  p.output_height = U32(payload, size, offset / 4 + 1);
  p.output = HashAt(payload, size, offset + 8);
  p.aspect_x = U32(payload, size, offset / 4 + 6);
  p.aspect_y = U32(payload, size, offset / 4 + 7);
  Require((p.aspect_x == 0) == (p.aspect_y == 0) && p.aspect_x <= 1024 && p.aspect_y <= 1024, "display aspect");
  const uint32_t width = p.texture[1], height = p.texture[2], bytes = p.texture[7];
  Require(width && height && width <= 8192 && height <= 8192 && bytes, "scanout size");
  Range(p.texture[0], bytes, "scanout");
  Flag(p.texture[8], "scanout format flag");
  Require(p.post_effect <= 2, "post effect");
  if (Flag(p.has_overlay, "overlay flag")) {
    Require(p.overlay[1] && p.overlay[2] && p.overlay[1] <= 8192 && p.overlay[2] <= 8192 && p.overlay[7], "overlay size");
    Range(p.overlay[0], p.overlay[7], "overlay");
    Flag(p.overlay[8], "overlay format flag");
  }
  Require(p.output_width && p.output_height, "presenter output missing (the frame has no composed image)");
  Require(p.output_width <= kMaxOutputSide && p.output_height <= kMaxOutputSide, "presenter output size");
  return p;
}
void DecodeEmpty(Record type, uint64_t size) {
  Require(size == 0, type == Record::kResolve ? "resolve record size" : "drain record size");
}
uint32_t ValidateFile(Reader& reader, uint32_t register_count) {
  Require(reader.header().register_count == register_count, "register count differs from this build");
  reader.Rewind();
  std::vector<bool> shaders;
  uint32_t frames = 0;
  Record type; const uint8_t* p; uint64_t size;
  while (reader.Next(type, p, size)) {
    switch (type) {
      case Record::kMemory: DecodeMemory(reader, p, size); break;
      case Record::kEdram: DecodeEdram(reader, p, size); break;
      case Record::kRegisters: DecodeRegisters(p, size, register_count); break;
      case Record::kShader: {
        const auto shader = DecodeShader(p, size);
        if (shader.id >= shaders.size()) shaders.resize(shader.id + 1);
        shaders[shader.id] = true;
        break;
      }
      case Record::kDraw: {
        const auto draw = DecodeDraw(p, size);
        Require(draw.vs < shaders.size() && shaders[draw.vs], "draw before its vertex shader");
        Require(draw.ps == kNoShader || (draw.ps < shaders.size() && shaders[draw.ps]), "draw before its pixel shader");
        break;
      }
      case Record::kResolve: case Record::kDrain: DecodeEmpty(type, size); break;
      case Record::kCompleteGuestWrites: DecodeCompleteGuestWrites(p, size); break;
      case Record::kSyncGuestRead: DecodeSyncGuestRead(p, size); break;
      case Record::kReadSampleCounts: DecodeReadSampleCounts(p, size); break;
      case Record::kSetSampleCounts: DecodeSetSampleCounts(p, size); break;
      case Record::kPresent: DecodePresent(p, size); ++frames; break;
      case Record::kBlob: Fail("unexpected blob record");
    }
  }
  Require(!reader.header().frames || reader.header().frames == frames, "frame count differs from the header");
  reader.Rewind();
  return frames;
}
}
