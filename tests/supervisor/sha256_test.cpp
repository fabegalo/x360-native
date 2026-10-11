#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "scratch.h"
#include "sha256.h"

using namespace x360n;

namespace {
std::span<const uint8_t> Bytes(const std::string& s) {
  return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}
}  // namespace

// Vectors from FIPS 180-4 examples / NIST CAVP.
TEST_CASE("sha256 known vectors", "[sha256]") {
  CHECK(Sha256Hex(Bytes("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(Sha256Hex(Bytes("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(Sha256Hex(Bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("sha256 one million 'a' fed in uneven chunks", "[sha256]") {
  std::string chunk(997, 'a');
  Sha256 h;
  size_t left = 1000000;
  while (left) {
    size_t n = std::min(left, chunk.size());
    h.Update({reinterpret_cast<const uint8_t*>(chunk.data()), n});
    left -= n;
  }
  auto d = h.Finish();
  CHECK(ToHex(d) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("sha256 of a file matches the in-memory digest", "[sha256]") {
  auto path = UniqueTempPath("sha256_test.bin");
  std::string data(3 * 1024 * 1024 + 17, '\0');
  for (size_t i = 0; i < data.size(); ++i) data[i] = char(i * 31 + 7);
  { std::ofstream(path, std::ios::binary).write(data.data(), std::streamsize(data.size())); }
  auto d = DigestFile(path);
  REQUIRE(d);
  CHECK(d->size == data.size());
  CHECK(d->sha256 == Sha256Hex(Bytes(data)));
  std::filesystem::remove(path);
  CHECK_FALSE(DigestFile(path));
}
