// Portions adapted from ReXGlue/Xenia, under BSD-3-Clause:
// Copyright (c) 2021 Ben Vanik; Copyright (c) 2026 Tom Clay.
// See licenses/rexglue-sdk.txt and THIRD_PARTY_NOTICES.md.
// x360n-content apply-patch. Same steps as the SDK's XexModule::ReadImageCompressed
// and XexModule::ApplyPatch (external/rexglue-sdk/src/system/xex_module.cpp), on
// files instead of guest memory, with two differences that only add checks:
//  - the AES master key is not assumed. Each key of the local key file (XEX2/XEX1
//    retail/devkit, the set Xenia Canary 2372a54105d9 src/xenia/cpu/xex_module.cpp
//    lists; this tool reads keys only from that local file) is tried, and a key is
//    accepted only by the format's own tests: the SHA-1 of
//    the first compressed block (base), and the patch's image_key_source, which must
//    decrypt to the base session key (patch). Base files of 2.0.1888.0 such as
//    xam.xex use the XEX1 retail key (also supported by local SDK patch 0095).
//  - the base image and the result are checked against the page digest chain of
//    their headers: section_digest covers page run 0 plus descriptor 0, descriptor
//    i-1 covers page run i plus descriptor i. That chain was confirmed on images
//    the SDK loaded (docs/dashboard.md).
// Nothing is written to the input files. Inputs are untrusted: every offset and
// every delta record is bounds-checked before use.
#include "xex_patch.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include <fmt/format.h>
#include <rex/system/lzx.h>
#include <rex/system/util/xex2_info.h>

#include "sha256.h"

namespace fs = std::filesystem;

namespace x360n::content {
namespace {

using Bytes = std::vector<uint8_t>;
using Key = std::array<uint8_t, 16>;
using Digest = std::array<uint8_t, 20>;

struct MasterKey {
  std::string name;
  Key key;
};

struct Error {
  std::string what;
};

constexpr const char* kKeyNames[] = {"xex2-retail", "xex2-devkit", "xex1-retail", "xex1-devkit"};

// This tool's master keys come from a local file, one
// per line as "<name> <32 hex digits>", '#' starting a comment. Anything else in it is
// an error, so that a typo cannot silently drop a key.
std::vector<MasterKey> LoadKeys(const fs::path& path) {
  std::ifstream in(path);
  if (!in)
    throw Error{fmt::format(
        "key file '{}' not found. It is local and ignored by Git: create it with one line "
        "per key, '<name> <32 hex digits>' (names xex2-retail, xex2-devkit, xex1-retail, "
        "xex1-devkit), or pass --keys <file> or set X360N_XEX_KEYS",
        path.string())};
  std::vector<MasterKey> keys;
  std::string line;
  for (int n = 1; std::getline(in, line); ++n) {
    if (const auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
    std::istringstream fields(line);
    std::string name, hex, extra;
    if (!(fields >> name)) continue;
    if (!(fields >> hex) || (fields >> extra))
      throw Error{fmt::format("key file '{}' line {}: expected '<name> <32 hex digits>'",
                              path.string(), n)};
    if (std::find_if(std::begin(kKeyNames), std::end(kKeyNames),
                     [&](const char* k) { return name == k; }) == std::end(kKeyNames))
      throw Error{fmt::format("key file '{}' line {}: unknown key name '{}'", path.string(), n, name)};
    for (const auto& k : keys)
      if (k.name == name)
        throw Error{fmt::format("key file '{}' line {}: '{}' given twice", path.string(), n, name)};
    MasterKey k{name, {}};
    auto nibble = [](char c) {
      return c >= '0' && c <= '9' ? c - '0'
             : c >= 'a' && c <= 'f' ? c - 'a' + 10
             : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                    : -1;
    };
    bool ok = hex.size() == 32;
    for (size_t i = 0; ok && i < 16; ++i) {
      const int hi = nibble(hex[2 * i]), lo = nibble(hex[2 * i + 1]);
      ok = hi >= 0 && lo >= 0;
      if (ok) k.key[i] = uint8_t(hi << 4 | lo);
    }
    if (!ok)
      throw Error{fmt::format("key file '{}' line {}: '{}' is not 32 hex digits", path.string(), n,
                              name)};
    keys.push_back(std::move(k));
  }
  if (keys.empty()) throw Error{fmt::format("key file '{}' holds no key", path.string())};
  return keys;
}

uint32_t Be32(const Bytes& b, size_t o) {
  if (o > b.size() || b.size() - o < 4) throw Error{fmt::format("read past end at 0x{:X}", o)};
  return (uint32_t(b[o]) << 24) | (uint32_t(b[o + 1]) << 16) | (uint32_t(b[o + 2]) << 8) | b[o + 3];
}
uint16_t Be16(const Bytes& b, size_t o) {
  if (o > b.size() || b.size() - o < 2) throw Error{fmt::format("read past end at 0x{:X}", o)};
  return uint16_t((b[o] << 8) | b[o + 1]);
}
const uint8_t* At(const Bytes& b, size_t o, size_t n) {
  if (o > b.size() || n > b.size() - o) throw Error{fmt::format("range 0x{:X}+0x{:X} past end", o, n)};
  return b.data() + o;
}
std::string Hex(const uint8_t* p, size_t n) {
  std::string s;
  for (size_t i = 0; i < n; ++i) s += fmt::format("{:02x}", p[i]);
  return s;
}
std::string Version(uint32_t v) {
  return fmt::format("{}.{}.{}.{}", v >> 28, (v >> 24) & 0xF, (v >> 8) & 0xFFFF, v & 0xFF);
}

Bytes ReadFile(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) throw Error{"cannot read " + p.string()};
  return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// AES-128-CBC with a zero IV and no padding. An incomplete block is invalid.
Bytes AesCbcDecrypt(const Key& key, const uint8_t* data, size_t len) {
  if (len % 16 || len > std::numeric_limits<ULONG>::max())
    throw Error{"AES ciphertext size is not supported or is not block aligned"};
  Bytes out(data, data + len);
  const ULONG whole = ULONG(len);
  if (!whole) return out;
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_KEY_HANDLE k = nullptr;
  bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) == 0 &&
            BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC,
                              sizeof(BCRYPT_CHAIN_MODE_CBC), 0) == 0 &&
            BCryptGenerateSymmetricKey(alg, &k, nullptr, 0, (PUCHAR)key.data(), 16, 0) == 0;
  uint8_t iv[16] = {};
  ULONG got = 0;
  ok = ok && BCryptDecrypt(k, (PUCHAR)data, whole, nullptr, iv, 16, out.data(), whole, &got, 0) == 0 &&
       got == whole;
  if (k) BCryptDestroyKey(k);
  if (alg) BCryptCloseAlgorithmProvider(alg, 0);
  if (!ok) throw Error{"AES decryption failed (CNG)"};
  return out;
}
Key DecryptKey(const Key& master, const uint8_t* block16) {
  Bytes b = AesCbcDecrypt(master, block16, 16);
  Key k;
  std::memcpy(k.data(), b.data(), 16);
  return k;
}

Digest Sha1(const uint8_t* p, size_t n) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  Digest d{};
  bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) == 0 &&
            BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0;
  while (ok && n) {
    const auto chunk = ULONG(std::min(n, size_t(std::numeric_limits<ULONG>::max())));
    ok = BCryptHashData(hash, const_cast<PUCHAR>(p), chunk, 0) == 0;
    p += chunk;
    n -= chunk;
  }
  ok = ok && BCryptFinishHash(hash, d.data(), ULONG(d.size()), 0) == 0;
  if (hash) BCryptDestroyHash(hash);
  if (alg) BCryptCloseAlgorithmProvider(alg, 0);
  if (!ok) throw Error{"SHA-1 failed (CNG)"};
  return d;
}

struct Header {
  uint32_t header_size, sec, flags, page_size, page_count = 0;
};
void HeaderRange(const Header& h, size_t offset, size_t length) {
  if (offset > h.header_size || length > h.header_size - offset)
    throw Error{fmt::format("header: range 0x{:X}+0x{:X} outside header", offset, length)};
}
Header ParseHeader(const Bytes& h) {
  if (Be32(h, 0) != 0x58455832) throw Error{"not an XEX2 header"};
  Header r{};
  r.header_size = Be32(h, 0x8);
  if (r.header_size < 0x18 || r.header_size > h.size())
    throw Error{"header: invalid header size"};
  HeaderRange(r, 0x18, uint64_t(Be32(h, 0x14)) * 8);
  r.sec = Be32(h, 0x10);
  HeaderRange(r, r.sec, 0x184);
  r.flags = Be32(h, size_t(r.sec) + 0x10C);
  r.page_size = (r.flags & 0x10000000) ? 0x1000 : 0x10000;  // XEX_IMAGE_PAGE_SIZE_4KB
  const uint32_t n = Be32(h, size_t(r.sec) + 0x180);
  HeaderRange(r, size_t(r.sec) + 0x184, uint64_t(n) * 24);
  uint64_t pages = 0;
  for (uint32_t i = 0; i < n; ++i)
    pages += Be32(h, size_t(r.sec) + 0x184 + size_t(24) * i) >> 4;
  if (pages > std::numeric_limits<uint32_t>::max() / r.page_size)
    throw Error{"header: page descriptors exceed the XEX image address space"};
  r.page_count = uint32_t(pages);
  return r;
}
std::optional<uint32_t> OptHeader(const Bytes& h, const Header& hd, uint32_t key) {
  const uint32_t n = Be32(h, 0x14);
  HeaderRange(hd, 0x18, uint64_t(n) * 8);
  for (uint32_t i = 0; i < n; ++i) {
    const size_t offset = 0x18 + size_t(8) * i;
    if (Be32(h, offset) == key) return Be32(h, offset + 4);
  }
  return std::nullopt;
}
void CheckWindow(uint32_t window) {
  // This pinned libmspack path uses regular LZX (window bits 15..21).
  if (window < 0x8000 || window > 0x200000 || (window & (window - 1)))
    throw Error{"unsupported LZX window size"};
}
size_t ImageSize(const Header& hd) {
  const uint64_t size = uint64_t(hd.page_count) * hd.page_size;
  // mspack_memory_open uses a signed int and asserts on larger buffers.
  if (!size || size >= std::numeric_limits<int>::max())
    throw Error{"header: empty image or image too large for the LZX decoder"};
  return size_t(size);
}

// Page digest chain; returns "<ok>/<total> links" and whether all held.
bool VerifyPages(const Bytes& h, const Bytes& image, std::string* report) {
  const Header hd = ParseHeader(h);
  const uint32_t n = Be32(h, hd.sec + 0x180);
  if (!n || !hd.page_count) throw Error{"header: no image page descriptors"};
  if (uint64_t(hd.page_count) * hd.page_size > image.size()) {
    *report = "image smaller than its page descriptors";
    return false;
  }
  std::vector<uint64_t> offset(size_t(n) + 1, 0);
  for (uint32_t i = 0; i < n; ++i)
    offset[i + 1] = offset[i] + uint64_t(Be32(h, hd.sec + 0x184 + 24 * i) >> 4) * hd.page_size;
  auto link = [&](uint32_t i) {  // SHA-1(page run i + descriptor i)
    Bytes buf(image.begin() + offset[i], image.begin() + offset[i + 1]);
    const uint8_t* d = At(h, hd.sec + 0x184 + 24 * i, 24);
    buf.insert(buf.end(), d, d + 24);
    return Sha1(buf.data(), buf.size());
  };
  uint32_t ok = 0;
  const Digest s0 = link(0);
  ok += std::memcmp(s0.data(), At(h, hd.sec + 0x114, 20), 20) == 0;
  for (uint32_t i = 1; i < n; ++i) {
    const Digest di = link(i);
    ok += std::memcmp(di.data(), At(h, hd.sec + 0x184 + 24 * (i - 1) + 4, 20), 20) == 0;
  }
  *report = fmt::format("{}/{} page runs verified ({} pages of 0x{:X})", ok, n, hd.page_count,
                        hd.page_size);
  return ok == n;
}

struct BaseImage {
  Bytes image;
  Key session;
  std::string key_name;
  uint32_t window_size;
};

// Decrypt (trying each master key), de-block with SHA-1 per block, LZX-decompress.
BaseImage DecodeBase(const Bytes& x, const std::vector<MasterKey>& keys) {
  const Header hd = ParseHeader(x);
  const size_t image_size = ImageSize(hd);
  const auto ffi = OptHeader(x, hd, 0x000003FF);
  if (!ffi) throw Error{"base: no file format info"};
  HeaderRange(hd, *ffi, 0x24);
  const size_t format = *ffi;
  const uint16_t enc = Be16(x, format + 4), comp = Be16(x, format + 6);
  if (enc != 1 || comp != 2)
    throw Error{fmt::format("base: encryption {} compression {} not handled (needs 1, 2)", enc, comp)};
  BaseImage r{};
  r.window_size = Be32(x, format + 8);
  CheckWindow(r.window_size);
  const uint32_t first_size = Be32(x, format + 0xC);
  if (first_size < 24) throw Error{"base: invalid first block size"};
  const uint8_t* first_hash = At(x, format + 0x10, 20);
  const size_t body_len = x.size() - hd.header_size;
  Bytes body;
  for (const auto& mk : keys) {
    const Key session = DecryptKey(mk.key, At(x, hd.sec + 0x150, 16));
    Bytes b = AesCbcDecrypt(session, At(x, hd.header_size, body_len), body_len);
    if (first_size <= b.size() && std::memcmp(Sha1(b.data(), first_size).data(), first_hash, 20) == 0) {
      r.session = session;
      r.key_name = mk.name;
      body = std::move(b);
      break;
    }
  }
  if (r.key_name.empty()) throw Error{"base: no key of the key file decrypts the first block"};
  Bytes compressed;
  size_t p = 0;
  uint32_t size = first_size;
  Digest want;
  std::memcpy(want.data(), first_hash, 20);
  while (size) {
    if (p > body.size() || size > body.size() - p || size < 24)
      throw Error{"base: block past end"};
    if (Sha1(body.data() + p, size) != want) throw Error{fmt::format("base: block hash mismatch at 0x{:X}", p)};
    const uint32_t next = Be32(body, p);
    std::memcpy(want.data(), At(body, p + 4, 20), 20);
    size_t q = p + 24;
    const size_t end = p + size;
    for (;;) {
      if (end - q < 2) throw Error{"base: chunk length past block end"};
      const uint16_t chunk = Be16(body, q);
      q += 2;
      if (!chunk) break;
      if (chunk > end - q) throw Error{"base: chunk data past block end"};
      const uint8_t* c = At(body, q, chunk);
      compressed.insert(compressed.end(), c, c + chunk);
      q += chunk;
    }
    p += size;
    size = next;
  }
  if (compressed.size() >= std::numeric_limits<int>::max())
    throw Error{"base: compressed image too large for the LZX decoder"};
  r.image.resize(image_size);
  if (lzx_decompress(compressed.data(), compressed.size(), r.image.data(), r.image.size(),
                     r.window_size, nullptr, 0) != 0)
    throw Error{"base: LZX decompression failed"};
  return r;
}

// Walks delta records as lzxdelta_apply_patch does and checks every range first.
void CheckDelta(const uint8_t* p, size_t len, size_t dest_size, uint32_t window,
                const char* what) {
  size_t o = 0;
  while (o < len) {
    if (len - o < 12) throw Error{fmt::format("{}: truncated delta record", what)};
    const uint32_t old_addr = (uint32_t(p[o]) << 24) | (uint32_t(p[o + 1]) << 16) |
                              (uint32_t(p[o + 2]) << 8) | p[o + 3];
    const uint32_t new_addr = (uint32_t(p[o + 4]) << 24) | (uint32_t(p[o + 5]) << 16) |
                              (uint32_t(p[o + 6]) << 8) | p[o + 7];
    const uint16_t ulen = uint16_t((p[o + 8] << 8) | p[o + 9]);
    const uint16_t clen = uint16_t((p[o + 10] << 8) | p[o + 11]);
    if (!old_addr && !new_addr && !ulen && !clen) return;
    if (uint64_t(new_addr) + ulen > dest_size || (clen != 0 && uint64_t(old_addr) + ulen > dest_size))
      throw Error{fmt::format("{}: delta record outside the buffer (old 0x{:X} new 0x{:X} len 0x{:X})",
                              what, old_addr, new_addr, ulen)};
    const size_t step = 12 + (clen > 1 ? clen : 0);
    if (step > len - o) throw Error{fmt::format("{}: delta data past end", what)};
    if (clen > 1 && ulen > window)
      throw Error{fmt::format("{}: delta reference exceeds LZX window", what)};
    o += step;
  }
}

void WriteNew(const fs::path& p, const uint8_t* data, size_t n) {
  // CREATE_NEW keeps the no-overwrite contract atomic, including concurrent runs.
  HANDLE file = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    throw Error{"cannot create new output (refusing to overwrite) " + p.string()};
  bool ok = true;
  while (n && ok) {
    const auto chunk = DWORD(std::min(n, size_t(std::numeric_limits<DWORD>::max())));
    DWORD written = 0;
    ok = WriteFile(file, data, chunk, &written, nullptr) && written == chunk;
    data += written;
    n -= written;
  }
  if (!CloseHandle(file)) ok = false;
  if (!ok) throw Error{"cannot write " + p.string()};
}

}  // namespace

fs::path DefaultXexKeyFile() {
  if (const char* env = std::getenv("X360N_XEX_KEYS"); env && *env) return fs::path(env);
  return fs::path(".local") / "keys" / "xex-keys.txt";
}

int ApplyXexPatch(const fs::path& base_path, const fs::path& patch_path, const fs::path& out_dir,
                  const fs::path& key_file) {
  std::string log;
  auto say = [&](const std::string& s) {
    fmt::print("{}\n", s);
    log += s + "\n";
  };
  try {
    const std::vector<MasterKey> keys = LoadKeys(key_file);
    const Bytes base = ReadFile(base_path);
    const Bytes patch = ReadFile(patch_path);
    say(fmt::format("base  {} {} bytes sha256 {}", base_path.string(), base.size(),
                    x360n::Sha256Hex({base.data(), base.size()})));
    say(fmt::format("patch {} {} bytes sha256 {}", patch_path.string(), patch.size(),
                    x360n::Sha256Hex({patch.data(), patch.size()})));

    // Base: decode and check against its own header.
    BaseImage b = DecodeBase(base, keys);
    const Header bh = ParseHeader(base);
    Bytes base_header(base.begin(), base.begin() + bh.header_size);
    std::string rep;
    const bool base_ok = VerifyPages(base_header, b.image, &rep);
    say(fmt::format("base: key {}, image 0x{:X} bytes, {}", b.key_name, b.image.size(), rep));
    if (!base_ok) throw Error{"base image does not match its page digests"};

    // Patch descriptor and correspondence.
    const Header ph = ParseHeader(patch);
    if (!(Be32(patch, 4) & 0x40)) throw Error{"patch: not a delta patch (module flags)"};
    const auto d = OptHeader(patch, ph, 0x000005FF);
    if (!d) throw Error{"patch: no delta patch descriptor"};
    const size_t desc = *d;
    HeaderRange(ph, desc, 0x58);
    const uint32_t descriptor_size = Be32(patch, desc);
    if (descriptor_size < 0x58) throw Error{"patch: delta descriptor too small"};
    HeaderRange(ph, desc, descriptor_size);
    const uint32_t target_ver = Be32(patch, desc + 4), source_ver = Be32(patch, desc + 8);
    const Digest sig = Sha1(At(base, bh.sec + 8, 0x100), 0x100);
    const bool sig_ok = std::memcmp(sig.data(), At(patch, desc + 0xC, 20), 20) == 0;
    say(fmt::format("patch: {} -> {}; digest_source {} base sha1(rsa_signature) {}", Version(source_ver),
                    Version(target_ver), sig_ok ? "matches" : "DOES NOT match", Hex(sig.data(), 20)));
    if (!sig_ok) throw Error{"patch is not for this base"};
    const auto pffi = OptHeader(patch, ph, 0x000003FF);
    if (!pffi) throw Error{"patch: no file format info"};
    const size_t format = *pffi;
    HeaderRange(ph, format, 0x24);
    const uint16_t penc = Be16(patch, format + 4), pcomp = Be16(patch, format + 6);
    if (penc != 0 && penc != 1)
      throw Error{fmt::format("patch: encryption {} not supported", penc)};
    if (pcomp != 3) throw Error{fmt::format("patch: compression {} (expected 3, delta)", pcomp)};
    const uint32_t window = Be32(patch, format + 8);
    CheckWindow(window);

    // Target header.
    const uint32_t size_of_target_headers = Be32(patch, desc + 0x30);
    const uint32_t hdr_src_off = Be32(patch, desc + 0x34), hdr_src_size = Be32(patch, desc + 0x38);
    const uint32_t hdr_dst_off = Be32(patch, desc + 0x3C);
    const uint32_t img_src_off = Be32(patch, desc + 0x40), img_src_size = Be32(patch, desc + 0x44);
    const uint32_t img_dst_off = Be32(patch, desc + 0x48);
    const uint64_t target_header_size = size_of_target_headers ? size_of_target_headers :
        uint64_t(hdr_dst_off) + hdr_src_size;
    if (!target_header_size || target_header_size >= std::numeric_limits<int>::max())
      throw Error{"patch: invalid target header size"};
    if (uint64_t(hdr_src_off) + hdr_src_size > base_header.size() ||
        uint64_t(hdr_dst_off) + hdr_src_size > target_header_size)
      throw Error{"patch: header move outside the header"};
    Bytes header = base_header;
    if (target_header_size > header.size()) header.resize(target_header_size, 0);
    if (hdr_src_off) {
      std::memmove(header.data() + hdr_dst_off, header.data() + hdr_src_off, hdr_src_size);
    }
    if (target_header_size < header.size())
      std::memset(header.data() + target_header_size, 0, header.size() - target_header_size);
    const uint16_t hdr_clen = Be16(patch, desc + 0x4C + 10);
    const size_t hdr_patch_size = 0xC + (hdr_clen > 1 ? hdr_clen : 0);
    if (hdr_patch_size > descriptor_size - 0x4C)
      throw Error{"patch: header delta data past descriptor end"};
    const uint8_t* hp = At(patch, desc + 0x4C, hdr_patch_size);
    Bytes hdr_patch(hp, hp + hdr_patch_size);
    CheckDelta(hdr_patch.data(), hdr_patch.size(), header.size(), window, "header patch");
    if (lzxdelta_apply_patch(reinterpret_cast<rex::xex2_delta_patch*>(hdr_patch.data()), hdr_patch.size(),
                             window, header.data()) != 0)
      throw Error{"header patch failed"};
    header.resize(target_header_size);
    const Header th = ParseHeader(header);

    // Keys: the master key is accepted only if image_key_source decrypts to the base session key.
    std::string key_name;
    Key patch_session{};
    for (const auto& mk : keys) {
      const Key s = DecryptKey(mk.key, At(header, th.sec + 0x150, 16));
      if (DecryptKey(s, At(patch, desc + 0x20, 16)) == b.session) {
        key_name = mk.name;
        patch_session = DecryptKey(s, At(patch, ph.sec + 0x150, 16));
        break;
      }
    }
    if (key_name.empty())
      throw Error{"patch: no key of the key file relates the patched header to the base session key"};
    say(fmt::format("keys: patched header key {}; image_key_source checks", key_name));

    // Image.
    const size_t target_image_size = ImageSize(th);
    Bytes image = b.image;
    const size_t work = std::max(image.size(), target_image_size);
    image.resize(work, 0);
    if (uint64_t(img_src_off) + img_src_size > b.image.size() ||
        uint64_t(img_dst_off) + img_src_size > work)
      throw Error{"patch: image move outside the image"};
    if (img_src_off) {
      std::memmove(image.data() + img_dst_off, image.data() + img_src_off, img_src_size);
    }
    const uint64_t image_target_size = uint64_t(img_dst_off) + img_src_size;
    if (image_target_size < b.image.size())
      std::memset(image.data() + image_target_size, 0, b.image.size() - size_t(image_target_size));
    const size_t pbody_len = patch.size() - ph.header_size;
    const uint8_t* pb = At(patch, ph.header_size, pbody_len);
    Bytes pbody = penc == 1 ? AesCbcDecrypt(patch_session, pb, pbody_len) : Bytes(pb, pb + pbody_len);
    uint32_t size = Be32(patch, format + 0xC);
    Digest want;
    std::memcpy(want.data(), At(patch, format + 0x10, 20), 20);
    size_t p = 0, blocks = 0;
    while (size) {
      if (p + size > pbody.size() || size < 24) throw Error{"patch: block past end"};
      if (Sha1(pbody.data() + p, size) != want)
        throw Error{fmt::format("patch: block {} hash mismatch", blocks)};
      const uint32_t next = Be32(pbody, p);
      std::memcpy(want.data(), At(pbody, p + 4, 20), 20);
      CheckDelta(pbody.data() + p + 24, size - 24, work, window, "image patch");
      if (lzxdelta_apply_patch(reinterpret_cast<rex::xex2_delta_patch*>(pbody.data() + p + 24), size - 24,
                               window, image.data()) != 0)
        throw Error{fmt::format("patch: delta block {} failed", blocks)};
      p += size;
      size = next;
      ++blocks;
    }
    image.resize(target_image_size);
    const bool target_ok = VerifyPages(header, image, &rep);
    say(fmt::format("result: {} patch blocks applied; image 0x{:X} bytes; {}", blocks, image.size(), rep));
    say(fmt::format("result: header sha256 {}; image sha256 {}",
                    x360n::Sha256Hex({header.data(), header.size()}),
                    x360n::Sha256Hex({image.data(), image.size()})));
    if (!target_ok) throw Error{"result does not match the page digests of the patched header"};

    fs::create_directories(out_dir);
    const std::string stem = base_path.stem().string() + "." + Version(target_ver);
    WriteNew(out_dir / (stem + ".header.bin"), header.data(), header.size());
    WriteNew(out_dir / (stem + ".image.bin"), image.data(), image.size());
    say("written: " + (out_dir / (stem + ".{header,image}.bin")).string());
    WriteNew(out_dir / (stem + ".report.txt"), reinterpret_cast<const uint8_t*>(log.data()), log.size());
    return 0;
  } catch (const Error& e) {
    fmt::print(stderr, "apply-patch: {}\n", e.what);
    return 1;
  } catch (const std::exception& e) {
    fmt::print(stderr, "apply-patch: {}\n", e.what());
    return 1;
  }
}

}  // namespace x360n::content
