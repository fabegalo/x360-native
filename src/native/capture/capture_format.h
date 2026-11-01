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
//
// Every payload is decoded by a Decode* function that checks its exact size,
// indices, enumerations and guest ranges before anything reads or copies it;
// ValidateFile runs all of them over a whole capture without a GPU.
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
// 2: present records carry the presenter's final output (LUT, overlay, post effect).
// 3: that output is mandatory; a present without a composed image is invalid.
inline constexpr uint32_t kVersion = 3;
inline constexpr uint32_t kPhysicalSize = 0x20000000;
inline constexpr uint32_t kPageSize = 4096;
inline constexpr uint32_t kMaxShaderDwords = 65536;

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
  kEdram = 13,             // u64 blob, u32 sample_counting: EDRAM (+ sample counters) when recording began
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
inline bool Recorded(const Hash& hash) { return hash[0] || hash[1]; }
inline constexpr uint32_t kMaxOutputSide = 16384;
// Hash of a presenter image (R8 G8 B8 X8 rows, `stride` bytes apart, the last
// row possibly unpadded) and its size. X is undefined and not hashed. Returns
// false, without hashing, for an empty, oversized or truncated image: a check
// against a missing image must fail, never compare fewer rows.
bool HashRgbxImage(const uint8_t* data, size_t size, uint32_t width, uint32_t height, size_t stride, Hash& out);

// Guest-visible results checked after a frame: the scanout image, every range
// resolves and memory exports wrote during the frame (after publication), and
// the presenter's composed output (gamma LUT, overlay plane, post effect).
struct WrittenRange { uint32_t address, length; Hash hash; };
struct PresentRecord {
  uint32_t texture[9];  // ScanoutTexture: address, width, height, pitch, tiled, endian, swizzle, bytes, rgb10
  uint32_t post_effect, has_overlay;
  uint32_t overlay[17]; // ScanoutOverlay: surface[9], x, y, right, bottom, offset_x, offset_y, display_width, display_height
  std::vector<uint32_t> lut;
  Hash frontbuffer;
  std::vector<WrittenRange> written;
  uint32_t output_width = 0, output_height = 0;  // Composed output size, never 0 in a valid file.
  Hash output{};
  uint32_t aspect_x = 0, aspect_y = 0;  // FrameLayout display aspect the host declared ({0,0}: guest).
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

// Whole capture in memory; records are framed on demand by Next().
class Reader {
 public:
  explicit Reader(const std::filesystem::path&);
  explicit Reader(std::vector<uint8_t> bytes);
  const Header& header() const { return header_; }
  void Rewind() { position_ = sizeof(Header); }
  // Returns false at the end. Blob records are consumed internally.
  bool Next(Record& type, const uint8_t*& payload, uint64_t& size);
  std::span<const uint8_t> blob(uint64_t id) const;
 private:
  void Open();
  std::vector<uint8_t> data_;
  Header header_{};
  size_t position_ = sizeof(Header);
  std::vector<std::span<const uint8_t>> blobs_;
};

// Typed, validated records. Guest ranges are inside physical memory; enum
// values are in the ranges the renderer accepts; sizes are exact.
struct MemoryRecord { uint32_t address, length; std::span<const uint8_t> bytes; };
struct EdramRecord { std::span<const uint8_t> bytes; bool sample_counting; };
struct RegisterWrite { uint32_t index, value; };
struct ShaderRecord { uint32_t id, type; std::vector<uint32_t> words; };
struct DrawRecord {
  uint32_t vs, ps, primitive, count, purpose;
  bool indexed;
  uint32_t index_format, index_endian, index_count, index_base, index_length;
};
struct SyncRecord { uint32_t source, opcode; };
struct ReadRecord { uint32_t address, length, opcode; };
struct SampleRecord { std::array<uint32_t, 8> counts; bool flag; };
MemoryRecord DecodeMemory(const Reader&, const uint8_t*, uint64_t);
EdramRecord DecodeEdram(const Reader&, const uint8_t*, uint64_t);
std::vector<RegisterWrite> DecodeRegisters(const uint8_t*, uint64_t, uint32_t register_count);
ShaderRecord DecodeShader(const uint8_t*, uint64_t);
DrawRecord DecodeDraw(const uint8_t*, uint64_t);
SyncRecord DecodeCompleteGuestWrites(const uint8_t*, uint64_t);
ReadRecord DecodeSyncGuestRead(const uint8_t*, uint64_t);
SampleRecord DecodeReadSampleCounts(const uint8_t*, uint64_t);  // flag: counting_after
SampleRecord DecodeSetSampleCounts(const uint8_t*, uint64_t);   // flag: visibility_only
PresentRecord DecodePresent(const uint8_t*, uint64_t);
void DecodeEmpty(Record, uint64_t);
// Decodes every record of a capture (no GPU, no guest memory): the replay runs
// it before touching the renderer. Returns the number of presented frames.
uint32_t ValidateFile(Reader&, uint32_t register_count);
}
