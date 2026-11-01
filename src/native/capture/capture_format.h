#pragma once
// Native frame capture file: every call the command processor makes into a
// ScanoutBackend, in order, with the guest memory each call reads. Replaying
// the calls against a fresh backend reproduces the renderer's work for those
// frames without running the title (see frame_replay.h).
//
// Layout (little endian): Header, then records {u32 type, u64 size, payload}.
// Memory is content-addressed: a Blob record precedes the first Memory or
// Edram record that uses it; identical bytes are stored once.
// Captures hold guest data from the title: keep them under .local/.
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
namespace x360n::native::capture {
inline constexpr char kMagic[8] = {'X', '3', '6', '0', 'N', 'F', 'C', '1'};
inline constexpr uint32_t kVersion = 1;
inline constexpr uint32_t kPhysicalSize = 0x20000000;
inline constexpr uint32_t kPageSize = 4096;

enum class Record : uint32_t {
  kBlob = 1,               // u64 id, bytes
  kMemory = 2,             // u32 address, u32 length, u64 blob
  kRegisters = 3,          // u32 count, {u32 index, u32 value}...
  kShader = 4,             // u32 id, u32 type, u32 dwords, words (as given by the command processor)
  kDraw = 5,               // u32 vs, u32 ps, u32 primitive, u32 count, u32 purpose, u32 indexed, u32 indices[5]
  kResolve = 6,            // -
  kCompleteGuestWrites = 7,// u32 source, u32 opcode
  kSyncGuestRead = 8,      // u32 address, u32 length, u32 opcode
  kReadSampleCounts = 9,   // u32 counting_after, u32 result[8] (recorded result)
  kSetSampleCounts = 10,   // u32 counts[8], u32 visibility_only
  kPresent = 11,           // see PresentRecord
  kDrain = 12,             // -
  kEdram = 13,             // u64 blob: EDRAM (+ sample counters) when recording began
};
inline constexpr uint32_t kNoShader = UINT32_MAX;

struct Header {
  char magic[8];
  uint32_t version, register_count, scale_x, scale_y;
  char backend[16];
  uint32_t frames, reserved[9];
};
static_assert(sizeof(Header) == 80);

using Hash = std::array<uint64_t, 2>;
Hash HashBytes(const void* data, size_t size);

// Guest-visible results checked after a frame: the scanout image and every
// range resolves and memory exports wrote during the frame, after publication.
struct WrittenRange { uint32_t address, length; Hash hash; };
struct PresentRecord {
  uint32_t texture[9];  // ScanoutTexture: address, width, height, pitch, tiled, endian, swizzle, bytes, rgb10
  uint32_t post_effect, has_overlay;
  uint32_t overlay[17]; // ScanoutOverlay: surface[9], x, y, right, bottom, offset_x, offset_y, display_width, display_height
  std::vector<uint32_t> lut;
  Hash frontbuffer;
  std::vector<WrittenRange> written;
};

class Writer {
 public:
  Writer(const std::filesystem::path&, const Header&);
  ~Writer();
  // Emits a Blob record unless identical bytes were stored before.
  uint64_t Blob(const void*, size_t);
  void Begin(Record, uint64_t size);
  void Put(const void* data, size_t size);
  template <class T> void Put(const T& value) { Put(&value, sizeof(T)); }
  void Simple(Record type, std::initializer_list<uint32_t> words);
  void Present(const PresentRecord&);
  // Rewrites the frame count and closes the file.
  void Finish(uint32_t frames);
  uint64_t bytes() const { return bytes_; }
  uint64_t blob_bytes() const { return blob_bytes_; }
 private:
  std::ofstream out_;
  std::filesystem::path path_;
  struct HashKey { size_t operator()(const Hash& h) const { return size_t(h[0] ^ (h[1] * 0x9E3779B97F4A7C15ull)); } };
  std::unordered_map<Hash, uint64_t, HashKey> blobs_;
  uint64_t next_blob_ = 0, bytes_ = 0, blob_bytes_ = 0, pending_ = 0;
};

// Whole capture in memory; records are decoded on demand by Next().
class Reader {
 public:
  explicit Reader(const std::filesystem::path&);
  const Header& header() const { return header_; }
  void Rewind() { position_ = sizeof(Header); }
  // Returns false at the end. Blob records are consumed internally.
  bool Next(Record& type, const uint8_t*& payload, uint64_t& size);
  std::span<const uint8_t> blob(uint64_t id) const;
  static PresentRecord ParsePresent(const uint8_t* payload, uint64_t size);
 private:
  std::vector<uint8_t> data_;
  Header header_{};
  size_t position_ = sizeof(Header);
  std::vector<std::span<const uint8_t>> blobs_;
};

// Little helpers to decode fixed payloads.
inline uint32_t Word(const uint8_t* payload, uint64_t size, size_t index) {
  if ((index + 1) * 4 > size) throw std::runtime_error("Native capture: truncated record");
  uint32_t value; std::memcpy(&value, payload + index * 4, 4); return value;
}
}
