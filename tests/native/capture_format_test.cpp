// Native frame capture file contracts, without a GPU: record order, blob
// deduplication, present expectations and rejection of damaged files.
#include "../../src/native/capture/capture_format.h"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <vector>
using namespace x360n::native::capture;
namespace {
std::filesystem::path Temporary(const char* name) {
  return std::filesystem::temp_directory_path() / (std::string("x360n-capture-") + name);
}
Header MakeHeader() {
  Header header{};
  std::memcpy(header.magic, kMagic, sizeof(kMagic));
  header.version = kVersion;
  header.register_count = 0x5003;
  header.scale_x = header.scale_y = 1;
  return header;
}
}
TEST_CASE("capture records keep their order and identical memory is stored once", "[native-capture]") {
  const auto path = Temporary("order.x360nfc");
  const std::vector<uint8_t> texture(8192, 0x5A), other(4096, 0x11);
  {
    Writer writer(path, MakeHeader());
    const auto first = writer.Blob(texture.data(), texture.size());
    writer.Begin(Record::kMemory, 16); writer.Put(0x1000u); writer.Put(uint32_t(texture.size())); writer.Put(first);
    writer.Simple(Record::kDraw, {1, kNoShader, 4, 3, 0, 0, 0, 0, 0, 0, 0});
    const auto again = writer.Blob(texture.data(), texture.size());
    CHECK(again == first);
    const auto second = writer.Blob(other.data(), other.size());
    CHECK(second != first);
    writer.Begin(Record::kMemory, 16); writer.Put(0x9000u); writer.Put(uint32_t(other.size())); writer.Put(second);
    writer.Simple(Record::kResolve, {});
    CHECK(writer.blob_bytes() == texture.size() + other.size());
    writer.Finish(1);
  }
  Reader reader(path);
  CHECK(reader.header().frames == 1);
  std::vector<Record> order;
  Record type; const uint8_t* payload; uint64_t size;
  while (reader.Next(type, payload, size)) {
    order.push_back(type);
    if (type == Record::kMemory) {
      uint64_t blob; std::memcpy(&blob, payload + 8, 8);
      CHECK(reader.blob(blob).size() == Word(payload, size, 1));
    }
  }
  CHECK(order == std::vector<Record>{Record::kMemory, Record::kDraw, Record::kMemory, Record::kResolve});
  reader.Rewind();
  REQUIRE(reader.Next(type, payload, size));
  CHECK(type == Record::kMemory);
  std::filesystem::remove(path);
}
TEST_CASE("present records carry the scanout and written-memory expectations", "[native-capture]") {
  const auto path = Temporary("present.x360nfc");
  PresentRecord present{};
  for (unsigned i = 0; i < 9; ++i) present.texture[i] = 100 + i;
  present.post_effect = 2;
  present.has_overlay = 1;
  for (unsigned i = 0; i < 17; ++i) present.overlay[i] = 200 + i;
  present.lut.assign(256, 7);
  present.frontbuffer = HashBytes("scanout", 7);
  present.written = {{0x2000, 64, HashBytes("a", 1)}, {0x8000, 4096, HashBytes("b", 1)}};
  {
    Writer writer(path, MakeHeader());
    writer.Present(present);
    writer.Finish(1);
  }
  Reader reader(path);
  Record type; const uint8_t* payload; uint64_t size;
  REQUIRE(reader.Next(type, payload, size));
  REQUIRE(type == Record::kPresent);
  const auto parsed = Reader::ParsePresent(payload, size);
  CHECK(std::equal(std::begin(parsed.texture), std::end(parsed.texture), std::begin(present.texture)));
  CHECK(parsed.post_effect == 2);
  CHECK(std::equal(std::begin(parsed.overlay), std::end(parsed.overlay), std::begin(present.overlay)));
  CHECK(parsed.lut == present.lut);
  CHECK(parsed.frontbuffer == present.frontbuffer);
  REQUIRE(parsed.written.size() == 2);
  CHECK(parsed.written[1].address == 0x8000);
  CHECK(parsed.written[1].hash == present.written[1].hash);
  CHECK_FALSE(reader.Next(type, payload, size));
  std::filesystem::remove(path);
}
TEST_CASE("damaged captures are rejected, not partially replayed", "[native-capture]") {
  const auto path = Temporary("damaged.x360nfc");
  {
    Writer writer(path, MakeHeader());
    const std::vector<uint8_t> bytes(1024, 3);
    writer.Blob(bytes.data(), bytes.size());
    writer.Simple(Record::kDraw, {1, 2, 3});
    writer.Finish(0);
  }
  const auto full = std::filesystem::file_size(path);
  std::filesystem::resize_file(path, full - 2);
  Reader truncated(path);
  Record type; const uint8_t* payload; uint64_t size;
  bool threw = false;
  try { while (truncated.Next(type, payload, size)) {} } catch (const std::runtime_error&) { threw = true; }
  CHECK(threw);
  {
    std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
    file.seekp(0); file.write("NOTACAPT", 8);
  }
  threw = false;
  try { Reader wrong(path); } catch (const std::runtime_error&) { threw = true; }
  CHECK(threw);
  std::filesystem::remove(path);
}
