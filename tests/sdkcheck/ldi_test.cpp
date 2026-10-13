// Synthetic LZX and direct import ABI checks. No title or proprietary assets.
#include <rex/memory.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

extern "C" REX_FUNC(__imp__LDICreateDecompression);
extern "C" REX_FUNC(__imp__LDIDecompress);
extern "C" REX_FUNC(__imp__LDIResetDecompression);
extern "C" REX_FUNC(__imp__LDIDestroyDecompression);

namespace {
uint32_t checks = 0;
void Require(bool ok, const char* label) {
  ++checks;
  if (!ok) throw std::runtime_error(label);
}
// LZX uncompressed block, written independently from the decoder: MSB-first
// bits in LE 16-bit words; then LE R0/R1/R2 and the literal payload.
std::vector<uint8_t> LiteralFrame(const std::vector<uint8_t>& plain) {
  std::vector<uint8_t> bits;
  auto put = [&](uint32_t value, unsigned count) {
    while (count) bits.push_back(uint8_t((value >> --count) & 1));
  };
  put(0, 1);
  put(3, 3);
  put(uint32_t(plain.size()), 24);
  while (bits.size() % 16) bits.push_back(0);
  std::vector<uint8_t> packed;
  for (size_t i = 0; i < bits.size(); i += 16) {
    uint16_t word = 0;
    for (unsigned j = 0; j < 16; ++j) word = uint16_t(word * 2 + bits[i + j]);
    packed.push_back(uint8_t(word));
    packed.push_back(uint8_t(word >> 8));
  }
  for (unsigned i = 0; i < 3; ++i) packed.insert(packed.end(), {1, 0, 0, 0});
  packed.insert(packed.end(), plain.begin(), plain.end());
  if (plain.size() & 1) packed.push_back(0);
  return packed;
}
}  // namespace

int main() {
  try {
    std::vector<uint8_t> memory(0x30000, 0xCD);
    auto c = std::make_unique<PPCContext>();
    auto put = [&](uint32_t p, uint32_t v) {
      rex::memory::store_and_swap<uint32_t>(memory.data() + p, v);
    };
    auto get = [&](uint32_t p) {
      return rex::memory::load_and_swap<uint32_t>(memory.data() + p);
    };
    auto call = [&](PPCFunc* fn, std::initializer_list<uint32_t> args) {
      std::array<PPCRegister*, 7> regs = {&c->r3, &c->r4, &c->r5, &c->r6,
                                          &c->r7, &c->r8, &c->r9};
      size_t i = 0;
      for (auto a : args) regs[i++]->u64 = a;
      c->r1.u64 = 0x27000;
      c->r2.u64 = 0xAABBCCDD;
      c->r31.u64 = 0x12345678;
      fn(*c, memory.data());
      Require(c->r1.u64 == 0x27000 && c->r2.u64 == 0xAABBCCDD &&
                  c->r31.u64 == 0x12345678,
              "import ABI nonvolatile registers");
      return uint32_t(c->r3.u64);
    };
    auto create = [&](uint32_t work, uint32_t available,
                      uint32_t callback = 0) {
      put(0x100, 0x8000);
      put(0x104, 0x8000);
      put(0x108, 0x8000);
      put(0x110, available);
      put(0x114, 0xFFFFFFFF);
      return call(__imp__LDICreateDecompression,
                  {0x100, 0x104, callback, 0, work, 0x110, 0x114});
    };
    Require(create(0x1000, 0x9800) == 0, "LDI create caller-owned workspace");
    const uint32_t handle = get(0x114);
    Require(handle == 0x1000 && get(0x110) == 0x9800,
            "workspace ownership and size contract");
    std::vector<uint8_t> plain(513);
    for (size_t i = 0; i < plain.size(); ++i) plain[i] = uint8_t(i * 37 + 11);
    const auto packed = LiteralFrame(plain);
    std::memcpy(memory.data() + 0xB000, packed.data(), packed.size());
    auto decode = [&](uint32_t h, uint32_t packed_size, uint32_t output_size) {
      put(0x118, output_size);
      return call(__imp__LDIDecompress,
                  {h, 0xB000, packed_size, 0x18000, 0x118});
    };
    Require(
        decode(handle, uint32_t(packed.size()), uint32_t(plain.size())) == 0,
        "decode synthetic LZX literal frame");
    Require(
        get(0x118) == plain.size() &&
            !std::memcmp(memory.data() + 0x18000, plain.data(), plain.size()),
        "decoded bytes and output count");
    Require(memory[0x17FFF] == 0xCD && memory[0x18000 + plain.size()] == 0xCD,
            "destination guards");
    Require(
        decode(handle, uint32_t(packed.size()), uint32_t(plain.size())) != 0,
        "continuation without reset is explicitly unsupported");
    Require(call(__imp__LDIResetDecompression, {handle}) == 0,
            "reset live context");
    Require(
        decode(handle, uint32_t(packed.size()), uint32_t(plain.size())) == 0,
        "reset permits new independent frame");
    Require(call(__imp__LDIResetDecompression, {handle}) == 0,
            "reset before bad input");
    std::fill(memory.begin() + 0x18000, memory.begin() + 0x18000 + plain.size(),
              0xCD);
    Require(decode(handle, 2, uint32_t(plain.size())) != 0 && get(0x118) == 0,
            "truncated input rejects with zero output");
    Require(std::all_of(memory.begin() + 0x18000,
                        memory.begin() + 0x18000 + plain.size(),
                        [](uint8_t b) { return b == 0xCD; }),
            "failed decode does not expose partial destination");
    Require(call(__imp__LDIResetDecompression, {handle}) == 0,
            "recover from decoder error");
    Require(decode(handle, uint32_t(packed.size()), 0x8001) != 0,
            "oversized output rejects");
    Require(decode(handle, 0x9801, 513) != 0, "oversized input rejects");
    Require(
        create(0xD000, 0x97FF) != 0 && get(0x114) == 0 && get(0x110) == 0x9800,
        "insufficient workspace reports required bytes");
    Require(create(0xD001, 0x9800) != 0 && get(0x114) == 0,
            "unaligned workspace rejects");
    Require(create(0xD000, 0x9800, 0x1234) != 0 && get(0x114) == 0,
            "allocator callback mode rejects explicitly");
    Require(create(0xD000, 0x9800) == 0, "second independent context");
    Require(call(__imp__LDIDestroyDecompression, {handle}) == 0,
            "destroy first context");
    Require(decode(handle, uint32_t(packed.size()), 513) != 0,
            "use after destroy rejects");
    Require(call(__imp__LDIResetDecompression, {handle}) != 0,
            "reset destroyed context rejects");
    Require(decode(0xD000, uint32_t(packed.size()), 513) == 0,
            "second context survives first destruction");
    Require(call(__imp__LDIDestroyDecompression, {0xD000}) == 0,
            "destroy second context");
    Require(call(__imp__LDIDestroyDecompression, {0xD000}) != 0,
            "double destroy rejects");
    Require(call(__imp__LDIResetDecompression, {0}) != 0,
            "null context rejects");
    std::printf("sdkcheck.LDI: PASS (%u checks)\n", checks);
    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "sdkcheck.LDI: FAIL after %u checks: %s\n", checks,
                 e.what());
    return 1;
  }
}
