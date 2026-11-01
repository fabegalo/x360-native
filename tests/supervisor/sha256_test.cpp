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

TEST_CASE("platform SHA-256 agrees on padding and streaming boundaries", "[sha256]") {
  for (const size_t size : {0u, 3u, 55u, 56u, 63u, 64u, 65u, 1048575u, 1048576u, 1048577u, 3145745u}) {
    INFO(size);
    std::string bytes(size, '\0');
    for (size_t i = 0; i < size; ++i) bytes[i] = char(i * 31 + 7);
    CHECK(PlatformSha256Hex(Bytes(bytes)) == Sha256Hex(Bytes(bytes)));
  }
  CHECK(PlatformSha256Hex(Bytes("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
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

TEST_CASE("sha256 of a file beyond the Windows MAX_PATH limit", "[sha256]") {
  namespace fs = std::filesystem;
  const auto root = UniqueTempPath("sha256_long");
  // Built with the \\?\ form so the fixture itself is not limited to 260.
  const auto extended = [](const fs::path& p) {
#ifdef _WIN32
    return fs::path(L"\\\\?\\" + fs::absolute(p).native());
#else
    return p;
#endif
  };
  fs::remove_all(extended(root));
  auto directory = root;
  fs::create_directory(extended(directory));
  while (directory.native().size() < 300) {
    directory /= std::string(40, 'd');
    fs::create_directory(extended(directory));
  }
  const auto file = directory / "clusterfuzz-testcase";
  { std::ofstream(extended(file), std::ios::binary) << "abc"; }
  const auto d = DigestFile(file);
  REQUIRE(d);
  CHECK(d->size == 3);
  CHECK(d->sha256 == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  fs::remove_all(extended(root));
}
