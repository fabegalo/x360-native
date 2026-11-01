// Native frame capture file contracts, without a GPU: record order, blob
// deduplication, present expectations, and rejection of every malformed
// payload before any field is used.
#include "../../src/native/capture/capture_format.h"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <vector>
using namespace x360n::native::capture;
namespace {
constexpr uint32_t kRegisters = 0x5003;
std::filesystem::path Temporary(const char* name) {
  return std::filesystem::temp_directory_path() / (std::string("x360n-capture-") + name);
}
Header MakeHeader() {
  Header header{};
  std::memcpy(header.magic, kMagic, sizeof(kMagic));
  header.version = kVersion;
  header.register_count = kRegisters;
  header.scale_x = header.scale_y = 1;
  std::memcpy(header.backend, "vulkan", 7);
  return header;
}
std::vector<uint8_t> Read(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}
bool Throws(const std::function<void()>& f) {
  try { f(); } catch (const std::runtime_error&) { return true; }
  return false;
}
std::vector<uint8_t> Words(std::initializer_list<uint32_t> words) {
  std::vector<uint8_t> bytes(words.size() * 4);
  if (!bytes.empty()) std::memcpy(bytes.data(), std::data(words), bytes.size());
  return bytes;
}
PresentRecord MakePresent() {
  PresentRecord present{};
  const uint32_t texture[9]{0x100000, 32, 32, 1, 1, 0, 0x688, 4096, 0};
  std::copy(std::begin(texture), std::end(texture), present.texture);
  present.lut.assign(256, 7);
  present.frontbuffer = HashBytes("scanout", 7);
  present.written = {{0x2000, 64, HashBytes("a", 1)}};
  present.output_width = 32; present.output_height = 32;
  present.output = HashBytes("output", 6);
  present.aspect_x = 16; present.aspect_y = 9;
  return present;
}
// A small but complete capture: EDRAM, shader, registers, memory, draw,
// resolve, sync, sample counters and one present.
std::vector<uint8_t> MakeCapture() {
  const auto path = Temporary("valid.x360nfc");
  {
    Writer writer(path, MakeHeader());
    const std::vector<uint8_t> edram(4096 + 32, 1), vertices(48, 2);
    const auto edram_blob = writer.Blob(edram.data(), edram.size());
    writer.Begin(Record::kEdram, 12); writer.Put(edram_blob); writer.Put(0u);
    const uint32_t ucode[3]{1, 2, 3};
    writer.Begin(Record::kShader, 12 + sizeof(ucode)); writer.Put(0u); writer.Put(0u); writer.Put(3u); writer.Put(ucode, sizeof(ucode));
    writer.Begin(Record::kRegisters, 4 + 16); writer.Put(2u); writer.Put(0x2000u); writer.Put(5u); writer.Put(0x2001u); writer.Put(6u);
    const auto blob = writer.Blob(vertices.data(), vertices.size());
    writer.Begin(Record::kMemory, 16); writer.Put(0x8000u); writer.Put(uint32_t(vertices.size())); writer.Put(blob);
    writer.Simple(Record::kDraw, {0, kNoShader, 4, 3, 0, 1, 0, 2, 3, 0x9000, 6});
    writer.Simple(Record::kResolve, {});
    writer.Simple(Record::kCompleteGuestWrites, {2, 0x3C});
    writer.Simple(Record::kSyncGuestRead, {0x9000, 16, 0x3C});
    writer.Simple(Record::kReadSampleCounts, {1, 1, 2, 3, 4, 5, 6, 7, 8});
    writer.Simple(Record::kSetSampleCounts, {0, 0, 0, 0, 0, 0, 0, 0, 1});
    writer.Present(MakePresent());
    writer.Simple(Record::kDrain, {});
    writer.Finish(1);
  }
  auto bytes = Read(path);
  std::filesystem::remove(path);
  return bytes;
}
}
TEST_CASE("capture records keep their order and identical memory is stored once", "[native-capture]") {
  const auto path = Temporary("order.x360nfc");
  const std::vector<uint8_t> texture(8192, 0x5A), other(4096, 0x11);
  {
    Writer writer(path, MakeHeader());
    const auto first = writer.Blob(texture.data(), texture.size());
    writer.Begin(Record::kMemory, 16); writer.Put(0x1000u); writer.Put(uint32_t(texture.size())); writer.Put(first);
    writer.Simple(Record::kResolve, {});
    CHECK(writer.Blob(texture.data(), texture.size()) == first);
    const auto second = writer.Blob(other.data(), other.size());
    CHECK(second != first);
    writer.Begin(Record::kMemory, 16); writer.Put(0x9000u); writer.Put(uint32_t(other.size())); writer.Put(second);
    CHECK(writer.blob_bytes() == texture.size() + other.size());
    writer.Finish(0);
  }
  Reader reader(path);
  std::vector<Record> order;
  Record type; const uint8_t* payload; uint64_t size;
  while (reader.Next(type, payload, size)) {
    order.push_back(type);
    if (type == Record::kMemory) {
      const auto memory = DecodeMemory(reader, payload, size);
      CHECK(memory.bytes.size() == memory.length);
      CHECK(memory.bytes[0] == (memory.address == 0x1000 ? 0x5A : 0x11));
    }
  }
  CHECK(order == std::vector<Record>{Record::kMemory, Record::kResolve, Record::kMemory});
  std::filesystem::remove(path);
}
TEST_CASE("a complete capture validates and its present expectations round-trip", "[native-capture]") {
  Reader reader(MakeCapture());
  CHECK(ValidateFile(reader, kRegisters) == 1);
  Record type; const uint8_t* payload; uint64_t size;
  while (reader.Next(type, payload, size)) {
    if (type == Record::kDraw) {
      const auto draw = DecodeDraw(payload, size);
      CHECK(draw.indexed);
      CHECK(draw.index_base == 0x9000);
    }
    if (type != Record::kPresent) continue;
    const auto parsed = DecodePresent(payload, size);
    const auto expected = MakePresent();
    CHECK(std::equal(std::begin(parsed.texture), std::end(parsed.texture), std::begin(expected.texture)));
    CHECK(parsed.lut == expected.lut);
    CHECK(parsed.frontbuffer == expected.frontbuffer);
    REQUIRE(parsed.written.size() == 1);
    CHECK(parsed.written[0].hash == expected.written[0].hash);
    CHECK(parsed.output == expected.output);
    CHECK(parsed.output_width == 32);
    CHECK(parsed.aspect_x == 16);
    CHECK(parsed.aspect_y == 9);
  }
}
TEST_CASE("malformed record payloads are rejected before use", "[native-capture]") {
  auto draw = [](std::initializer_list<uint32_t> words) { auto b = Words(words); return Throws([&] { DecodeDraw(b.data(), b.size()); }); };
  CHECK_FALSE(draw({0, kNoShader, 4, 3, 0, 0, 0, 0, 0, 0, 0}));
  CHECK(draw({0, kNoShader, 4, 3, 0, 0, 0, 0, 0, 0}));                       // Short.
  CHECK(draw({0, kNoShader, 4, 3, 0, 0, 0, 0, 0, 0, 0, 0}));                 // Long.
  CHECK(draw({kNoShader, kNoShader, 4, 3, 0, 0, 0, 0, 0, 0, 0}));            // No vertex shader.
  CHECK(draw({0, kNoShader, 0x17, 3, 0, 0, 0, 0, 0, 0, 0}));                 // Primitive type.
  CHECK(draw({0, kNoShader, 4, 3, 4, 0, 0, 0, 0, 0, 0}));                    // Purpose.
  CHECK(draw({0, kNoShader, 4, 3, 0, 2, 0, 0, 0, 0, 0}));                    // Index flag.
  CHECK(draw({0, kNoShader, 4, 3, 0, 1, 2, 0, 3, 0, 6}));                    // Index format.
  CHECK(draw({0, kNoShader, 4, 3, 0, 1, 0, 4, 3, 0, 6}));                    // Index endian.
  CHECK(draw({0, kNoShader, 4, 3, 0, 1, 0, 0, 3, 0x1FFFFFFF, 6}));           // Index range past memory.
  CHECK(draw({0, kNoShader, 4, 3, 0, 1, 0, 0, 3, 16, 0xFFFFFFF0}));          // Wrapping index range.

  auto registers = [](std::initializer_list<uint32_t> words) {
    auto b = Words(words); return Throws([&] { DecodeRegisters(b.data(), b.size(), kRegisters); });
  };
  CHECK_FALSE(registers({1, 0x2000, 7}));
  CHECK(registers({1, kRegisters, 7}));         // Index outside the file.
  CHECK(registers({2, 0x2000, 7}));             // Count larger than the payload.
  CHECK(registers({0x40000000, 0x2000, 7}));    // Count overflowing the size.
  CHECK(registers({}));                          // No count.

  auto shader = [](std::vector<uint32_t> words) {
    std::vector<uint8_t> b(words.size() * 4); std::memcpy(b.data(), words.data(), b.size());
    return Throws([&] { DecodeShader(b.data(), b.size()); });
  };
  CHECK_FALSE(shader({0, 1, 1, 9}));
  CHECK(shader({0, 2, 1, 9}));                   // Type.
  CHECK(shader({0, 1, 0}));                      // Empty.
  CHECK(shader({0, 1, 2, 9}));                   // Count past the payload.
  CHECK(shader({0, 1, kMaxShaderDwords + 1, 9})); // Too large.
  CHECK(shader({1u << 24, 1, 1, 9}));            // Id.

  auto sync = [](std::initializer_list<uint32_t> words) { auto b = Words(words); return Throws([&] { DecodeCompleteGuestWrites(b.data(), b.size()); }); };
  CHECK_FALSE(sync({4, 0}));
  CHECK(sync({5, 0}));
  CHECK(sync({1}));
  auto read = [](std::initializer_list<uint32_t> words) { auto b = Words(words); return Throws([&] { DecodeSyncGuestRead(b.data(), b.size()); }); };
  CHECK_FALSE(read({0x1FFFFFF0, 16, 0}));
  CHECK(read({0x1FFFFFF0, 17, 0}));
  CHECK(read({16, 0xFFFFFFFF, 0}));
  auto samples = [](std::initializer_list<uint32_t> words) { auto b = Words(words); return Throws([&] { DecodeReadSampleCounts(b.data(), b.size()); }); };
  CHECK_FALSE(samples({1, 0, 0, 0, 0, 0, 0, 0, 0}));
  CHECK(samples({2, 0, 0, 0, 0, 0, 0, 0, 0}));
  CHECK(samples({1, 0, 0, 0, 0, 0, 0, 0}));
  CHECK(Throws([] { DecodeEmpty(Record::kResolve, 4); }));
}
TEST_CASE("present records with inconsistent fields are rejected", "[native-capture]") {
  auto encoded = [](const PresentRecord& present) {
    const auto path = Temporary("present.x360nfc");
    { Writer writer(path, MakeHeader()); writer.Present(present); writer.Finish(1); }
    auto bytes = Read(path);
    std::filesystem::remove(path);
    return std::vector<uint8_t>(bytes.begin() + sizeof(Header) + 12, bytes.end());
  };
  auto rejects = [&](const PresentRecord& present) {
    const auto bytes = encoded(present);
    return Throws([&] { DecodePresent(bytes.data(), bytes.size()); });
  };
  CHECK_FALSE(rejects(MakePresent()));
  auto p = MakePresent(); p.texture[0] = 0x1FFFF800; CHECK(rejects(p));        // Scanout past memory.
  p = MakePresent(); p.texture[7] = 0; CHECK(rejects(p));                      // Empty scanout.
  p = MakePresent(); p.texture[1] = 9000; CHECK(rejects(p));                   // Width.
  p = MakePresent(); p.lut.assign(128 * 3, 0); CHECK(rejects(p));              // LUT size for RGBA8.
  p = MakePresent(); p.texture[8] = 1; CHECK(rejects(p));                      // RGB10 needs 384 entries.
  p = MakePresent(); p.post_effect = 3; CHECK(rejects(p));
  p = MakePresent(); p.has_overlay = 1; CHECK(rejects(p));                     // Overlay without a surface.
  p = MakePresent(); p.written.push_back({0x1FFFFF00, 0x200, {}}); CHECK(rejects(p));
  p = MakePresent(); p.output_height = 0; CHECK(rejects(p));                   // Half an output size.
  p = MakePresent(); p.aspect_y = 0; CHECK(rejects(p));
  auto bytes = encoded(MakePresent());
  bytes.pop_back();
  CHECK(Throws([&] { DecodePresent(bytes.data(), bytes.size()); }));
}
TEST_CASE("files with inconsistent structure are rejected", "[native-capture]") {
  const auto valid = MakeCapture();
  auto validate = [](std::vector<uint8_t> bytes) {
    return Throws([&] { Reader reader(std::move(bytes)); ValidateFile(reader, kRegisters); });
  };
  CHECK_FALSE(validate(valid));
  auto old = valid; reinterpret_cast<Header*>(old.data())->version = 1; CHECK(validate(old));
  auto magic = valid; magic[0] = 'Y'; CHECK(validate(magic));
  auto frames = valid; reinterpret_cast<Header*>(frames.data())->frames = 2; CHECK(validate(frames));
  auto scale = valid; reinterpret_cast<Header*>(scale.data())->scale_x = 0; CHECK(validate(scale));
  auto backend = valid; std::memset(reinterpret_cast<Header*>(backend.data())->backend, 'x', 16); CHECK(validate(backend));
  CHECK(Throws([&] { Reader reader(valid); ValidateFile(reader, kRegisters + 1); }));  // Other register file.
  // A draw whose shader record never came: write a capture without it.
  const auto path = Temporary("noshader.x360nfc");
  { Writer writer(path, MakeHeader()); writer.Simple(Record::kDraw, {0, kNoShader, 4, 3, 0, 0, 0, 0, 0, 0, 0}); writer.Finish(0); }
  CHECK(validate(Read(path)));
  // Memory that references a blob not yet stored.
  { Writer writer(path, MakeHeader()); writer.Begin(Record::kMemory, 16); writer.Put(0u); writer.Put(4u); writer.Put(uint64_t(0)); writer.Finish(0); }
  CHECK(validate(Read(path)));
  std::filesystem::remove(path);
}
TEST_CASE("every truncation and corrupted byte of a capture is rejected or decoded safely", "[native-capture]") {
  const auto valid = MakeCapture();
  for (size_t length = 0; length < valid.size(); ++length) {
    std::vector<uint8_t> truncated(valid.begin(), valid.begin() + length);
    const bool rejected = Throws([&] { Reader reader(std::move(truncated)); ValidateFile(reader, kRegisters); });
    INFO("truncated to " << length);
    // Only cuts at a record boundary after the present can still be complete.
    if (!rejected) CHECK(length > sizeof(Header));
  }
  std::mt19937 random(0x360);
  for (size_t i = 0; i < valid.size(); ++i) {
    auto corrupted = valid;
    corrupted[i] ^= uint8_t(1 + random() % 255);
    // Either rejected or decoded within its validated ranges; never a crash.
    Throws([&] { Reader reader(std::move(corrupted)); ValidateFile(reader, kRegisters); });
  }
  CHECK(true);
}
