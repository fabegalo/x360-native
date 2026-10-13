// motocross_madness: integration probe for recompiled code of the original XAM
// 2.0.17559.0 (module x360nsys:\xam.xex, [[modules]] in the manifest, dispatch table
// on the host: patch 0031). Runs once, only with --motocross_xam_aot_probe=true, on
// the game thread that is about to call XamAvatarInitialize (avatar_trace.cpp), and
// restores that thread's whole PowerPC context afterwards. XAM's entry point is never
// called and no SDK export changes; XamAvatarInitialize still fails.
//
// Checks (each logged PASS/FAIL with the values):
//   context   the registers the hook receives are the guest thread's PPCContext
//   load      the module loads at its image base 0x815F0000, size 0x610000
//   registry  0x816753C8 and 0x81670000 are registered; a mid-function address is not
//   data      XAM .rdata at 0x81632E68 holds the MaleBody ID the image was built with
//   game->XAM XamAvatarManifestGetBodyType (0x816753C8, nested XAM calls 0x8173EE60 and
//             0x8196CA80) on descriptors built from the original 17559 catalog equals
//             the C++ BodyType() for male, female and empty bodies
//   XAM->game XAM's vector iterator 0x81670000(p, 16, 4, fn) calls the game's
//             constructor 0x82450340, which stores 0x8200131C at 0(r3); the other
//             bytes of each element stay untouched
//   ABI       r1, r2, r13-r31 and f14-f31 are unchanged across each XAM call
//   errors    executing an unregistered address returns the dispatcher's failure
//             value and leaves memory unchanged
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <rex/cvar.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/ppc/context.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/user_module.h>
#include <rex/system/xex_module.h>

#include "../../common/avatar/avatar_metadata.h"
#include "../../common/avatar/avatar_toc.h"
#include "../../dash_17559/src/experimental_region.h"

REXCVAR_DEFINE_BOOL(motocross_xam_aot_probe, false, "Motocross Madness",
                    "Diagnostic: once, at the XamAvatarInitialize call site, load the original "
                    "XAM 2.0.17559.0 (AOT) and run integration checks; the game's registers are "
                    "restored afterwards");
REXCVAR_DEFINE_STRING(x360n_xam17559_root, "", "x360n",
                      "Host folder with the original xam.xex 2.0.17559.0, mounted read-only as "
                      "x360nsys:");
REXCVAR_DEFINE_STRING(x360n_avatar_catalog_17559, "", "x360n",
                      "Host path of AvatarAssetPack.toc from the 17559 storage pack FFFE07DF00000002");
REXCVAR_DEFINE_BOOL(motocross_xam_aot_probe_strb, false, "Motocross Madness",
                    "Diagnostic, with the probe: decode every STRB blob of the 17559 catalog with "
                    "XAM's original Structured Binary reader and compare with the bytes");
REXCVAR_DEFINE_BOOL(motocross_xam_aot_probe_sizes, false, "Motocross Madness",
                    "Diagnostic, with the probe: run XAM's GetAssets size routine 0x81965EC0 on "
                    "every valid component mask and compare with the C++ ResultSizeFor");

REXCVAR_DEFINE_BOOL(motocross_xam_aot_probe_colors, false, "Motocross Madness",
                    "Diagnostic: expand original STRB color palettes with XAM AOT and verify every byte");

namespace {

constexpr uint32_t kXamBase = 0x815F0000, kXamSize = 0x610000;
constexpr uint32_t kBodyTypeExport = 0x816753C8;  // XamAvatarManifestGetBodyType, ordinal 0x5F1
constexpr uint32_t kVectorIterator = 0x81670000;  // for (n) { fn(p); p += size; }
constexpr uint32_t kMidFunction = 0x816753CC;     // inside kBodyTypeExport
constexpr uint32_t kGameCtor = 0x82450340;        // lis/addi 0x8200131C; stw 0(r3); blr
constexpr uint32_t kGameCtorValue = 0x8200131C;
constexpr uint32_t kFailValue = 0xDEADBABE;       // FunctionDispatcher::Execute, not found
constexpr std::array<uint8_t, 16> kXamMaleBody = {0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01,
                                                  0xc1, 0xc8, 0xf1, 0x09, 0xa1, 0x9c, 0xb2, 0xe0};

int g_failures = 0;
uint32_t g_xam_hmodule = 0;  // LDR_DATA_TABLE_ENTRY of the loaded XAM, as DllMain receives it

void Check(bool ok, const std::string& what) {
  if (!ok) ++g_failures;
  REXLOG_INFO("xam-aot-probe: {} {}", ok ? "PASS" : "FAIL", what);
}

struct NonVolatile {
  uint64_t r[21];  // r1, r2, r13..r31
  uint64_t f[18];  // f14..f31, compared bit for bit
};

NonVolatile Snapshot(const PPCContext& c) {
  NonVolatile s{};
  const PPCRegister* gpr[21] = {&c.r1,  &c.r2,  &c.r13, &c.r14, &c.r15, &c.r16, &c.r17,
                                &c.r18, &c.r19, &c.r20, &c.r21, &c.r22, &c.r23, &c.r24,
                                &c.r25, &c.r26, &c.r27, &c.r28, &c.r29, &c.r30, &c.r31};
  const PPCRegister* fpr[18] = {&c.f14, &c.f15, &c.f16, &c.f17, &c.f18, &c.f19,
                                &c.f20, &c.f21, &c.f22, &c.f23, &c.f24, &c.f25,
                                &c.f26, &c.f27, &c.f28, &c.f29, &c.f30, &c.f31};
  for (int i = 0; i < 21; ++i) s.r[i] = gpr[i]->u64;
  for (int i = 0; i < 18; ++i) s.f[i] = fpr[i]->u64;
  return s;
}

bool SameNonVolatile(const NonVolatile& a, const NonVolatile& b) {
  return std::memcmp(a.r, b.r, sizeof(a.r)) == 0 && std::memcmp(a.f, b.f, sizeof(a.f)) == 0;
}

std::vector<uint8_t> Slurp(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

uint64_t Call(rex::runtime::FunctionDispatcher* d, rex::runtime::ThreadState* t, uint32_t fn,
              std::initializer_list<uint64_t> args, const char* what) {
  std::vector<uint64_t> a(args);
  auto* ctx = t->context();
  const auto before = Snapshot(*ctx);
  const uint64_t r = d->Execute(t, fn, a.data(), a.size());
  Check(SameNonVolatile(before, Snapshot(*ctx)),
        std::string("ABI: non-volatile registers preserved across ") + what);
  return r;
}

// Same as Call, for loops: ABI breaks are counted instead of logged one by one.
uint64_t CallQuiet(rex::runtime::FunctionDispatcher* d, rex::runtime::ThreadState* t, uint32_t fn,
                   std::initializer_list<uint64_t> args, uint32_t* abi_breaks) {
  std::vector<uint64_t> a(args);
  auto* ctx = t->context();
  const auto before = Snapshot(*ctx);
  const uint64_t r = d->Execute(t, fn, a.data(), a.size());
  if (!SameNonVolatile(before, Snapshot(*ctx))) ++*abi_breaks;
  return r;
}

// XAM's Structured Binary ("STRB") decoding, the first step of every asset in GetAssets
// (0x81966FC8): a memory stream over the blob (0x8196D568: {vtable 0x816331C4, data,
// size}, Read = memcpy at slot 1), the reader (0x8196DBE8: magic, version, 24-byte
// header, error text at +40), its checks (0x8196D5E8 valid, 0x81930508 format GUID) and
// the document parse (0x8196E610 + 0x8196EE50). They read only guest memory and XAM's
// static vtables: no XAM global, heap, thread or service. The blobs come from the C++
// catalog reader; the reference is the bytes themselves.
struct StrbResult {
  uint8_t valid, parsed;
  uint8_t guid[16];
  uint8_t version_bytes[3];  // reader +22, +23, +24
  std::string error;         // reader +40
};

StrbResult DecodeStrb(rex::runtime::FunctionDispatcher* d, rex::runtime::ThreadState* t,
                      rex::memory::Memory* memory, uint32_t scratch, std::span<const uint8_t> blob,
                      uint32_t* abi_breaks) {
  constexpr uint32_t kStream = 0x000, kReader = 0x040, kDoc = 0x240, kData = 0x400;
  auto* host = memory->TranslateVirtual<uint8_t*>(scratch);
  std::memset(host, 0, kData);
  std::memcpy(host + kData, blob.data(), blob.size());
  CallQuiet(d, t, 0x8196D568, {scratch + kStream, scratch + kData, blob.size()}, abi_breaks);
  CallQuiet(d, t, 0x8196DBE8, {scratch + kReader, scratch + kStream}, abi_breaks);
  StrbResult r{};
  r.valid = uint8_t(CallQuiet(d, t, 0x8196D5E8, {scratch + kReader}, abi_breaks));
  const uint32_t guid = uint32_t(CallQuiet(d, t, 0x81930508, {scratch + kReader}, abi_breaks));
  if (guid >= scratch && guid + 16 <= scratch + kData)
    std::memcpy(r.guid, memory->TranslateVirtual(guid), 16);
  std::memcpy(r.version_bytes, host + kReader + 22, 3);
  const char* text = reinterpret_cast<const char*>(host + kReader + 40);
  r.error.assign(text, strnlen(text, 128));
  if (r.valid) {
    CallQuiet(d, t, 0x8196E610, {scratch + kDoc, 0, 0, 0, 0, 0}, abi_breaks);
    r.parsed = uint8_t(CallQuiet(d, t, 0x8196EE50, {scratch + kDoc, scratch + kReader}, abi_breaks));
  }
  return r;
}

void RunStrbDecode(const x360n::avatar::AvatarToc& toc) {
  auto* runtime = rex::Runtime::instance();
  auto* thread = rex::runtime::ThreadState::Get();
  auto* memory = runtime->memory();
  auto* dispatcher = runtime->function_dispatcher();
  size_t largest = 0, used = 0;
  for (size_t i = 0; i < toc.size(); ++i) largest = std::max(largest, toc.Blob(i).size());
  const uint32_t scratch = memory->SystemHeapAlloc(uint32_t(0x400 + largest));
  if (!scratch) {
    Check(false, "strb: allocate the scratch buffer");
    return;
  }
  // The format GUID as XAM stores it (.rdata 0x81632EF8) and as the blobs carry it.
  uint8_t xam_guid[16];
  std::memcpy(xam_guid, memory->TranslateVirtual(0x81632EF8), 16);
  uint32_t abi = 0, ok = 0, mismatched = 0;
  std::string first_bad;
  for (size_t i = 0; i < toc.size(); ++i) {
    const auto blob = toc.Blob(i);
    if (toc.record(i).empty) continue;
    ++used;
    const auto r = DecodeStrb(dispatcher, thread, memory, scratch, blob, &abi);
    // Independent reading of the same bytes: "STRB", version 1, the format GUID at +6,
    // and the bytes the reader copies (header +16, +17 and +20, i.e. blob +22, +23, +26).
    const bool expected = blob.size() >= 30 && std::memcmp(blob.data(), "STRB", 4) == 0 &&
                          blob[4] == 1 && std::memcmp(blob.data() + 6, xam_guid, 16) == 0;
    const bool same = r.valid == 1 && r.parsed == 1 && r.error.empty() &&
                      std::memcmp(r.guid, blob.data() + 6, 16) == 0 &&
                      r.version_bytes[0] == blob[22] && r.version_bytes[1] == blob[23] &&
                      r.version_bytes[2] == blob[26];
    if (expected && same) {
      ++ok;
    } else if (++mismatched == 1) {
      first_bad = fmt::format("record {}: expected {} valid {} parsed {} error '{}'", i, expected,
                              r.valid, r.parsed, r.error);
    }
  }
  Check(used > 0 && ok == used,
        fmt::format("strb: XAM decoded {}/{} original 17559 blobs with header fields equal to "
                    "the bytes{}",
                    ok, used, first_bad.empty() ? "" : "; first difference " + first_bad));
  Check(abi == 0, fmt::format("strb: ABI preserved in all {} XAM calls ({} breaks)", used * 6, abi));

  // XAM's own error paths, on copies of the first blob with one byte changed.
  for (size_t i = 0; i < toc.size(); ++i) {
    if (toc.record(i).empty) continue;
    std::vector<uint8_t> bad(toc.Blob(i).begin(), toc.Blob(i).end());
    bad[3] = 'X';
    auto r = DecodeStrb(dispatcher, thread, memory, scratch, bad, &abi);
    Check(r.valid == 0 && r.parsed == 0 &&
              r.error == "Location not recognized as a Structured Binary",
          fmt::format("strb: magic STRX rejected by XAM: valid {} error '{}'", r.valid, r.error));
    bad[3] = 'B';
    bad[4] = 2;
    r = DecodeStrb(dispatcher, thread, memory, scratch, bad, &abi);
    Check(r.valid == 0 && r.parsed == 0 && r.error == "Structured Binary version too high",
          fmt::format("strb: version 2 rejected by XAM: valid {} error '{}'", r.valid, r.error));
    break;
  }
  memory->SystemHeapFree(scratch);
}

// GetAssets buffer sizes: XAM's 0x81965EC0(mask, &result, &gpu), which the GetAssets
// output builder (0x81966030) calls, reads only its arguments and the 13-entry component
// table at 0x816332B8 (through 0x81970970). Every valid input is checked against the C++
// ResultSizeFor: the games' wrappers accept only nonzero 13-bit masks (Motocross
// 0x82958130, Keflings 0x822F9C18), and both games pass 0x1FFF (Motocross 0x824377D4,
// Keflings 0x8223F564).
void RunSizeCheck() {
  auto* runtime = rex::Runtime::instance();
  auto* thread = rex::runtime::ThreadState::Get();
  auto* memory = runtime->memory();
  auto* dispatcher = runtime->function_dispatcher();
  const uint32_t out = memory->SystemHeapAlloc(8);
  if (!out) {
    Check(false, "sizes: allocate the output words");
    return;
  }
  auto* host = memory->TranslateVirtual<uint8_t*>(out);
  uint32_t abi = 0, equal = 0, first_mask = 0;
  x360n::avatar::AssetsResultSize first_cpp{}, first_xam{}, real{};
  for (uint32_t mask = 1; mask <= 0x1FFF; ++mask) {
    std::memset(host, 0xA5, 8);  // both words are written by XAM
    CallQuiet(dispatcher, thread, 0x81965EC0, {mask, out, out + 4}, &abi);
    const x360n::avatar::AssetsResultSize xam{rex::memory::load_and_swap<uint32_t>(host),
                                              rex::memory::load_and_swap<uint32_t>(host + 4)};
    const auto cpp = x360n::avatar::ResultSizeFor(uint16_t(mask));
    if (mask == 0x1FFF) real = xam;
    if (xam.result == cpp.result && xam.gpu_resources == cpp.gpu_resources) {
      ++equal;
    } else if (!first_mask) {
      first_mask = mask;
      first_cpp = cpp;
      first_xam = xam;
    }
  }
  Check(equal == 0x1FFF,
        fmt::format("sizes: XAM 0x81965EC0 = C++ ResultSizeFor for {}/8191 masks{}", equal,
                    first_mask ? fmt::format("; first difference mask 0x{:04X}: XAM 0x{:X}/0x{:X}, "
                                             "C++ 0x{:X}/0x{:X}",
                                             first_mask, first_xam.result, first_xam.gpu_resources,
                                             first_cpp.result, first_cpp.gpu_resources)
                               : std::string()));
  Check(abi == 0, fmt::format("sizes: ABI preserved in all 8191 XAM calls ({} breaks)", abi));
  REXLOG_INFO("xam-aot-probe: sizes: games' mask 0x1FFF -> result 0x{:X}, GPU resources 0x{:X} "
              "(XAM)",
              real.result, real.gpu_resources);
  memory->SystemHeapFree(out);
}

// Diagnostic only: GetAssets -> 0x81966450 -> 0x81976598 expands STRB field 7.
// The reference reads the original field directly, without consulting AOT output:
// seven little-endian header words, followed by LSB-first packed integer deltas.
// Output is at most 18 triples of BE u32 ARGB values, then a BE count at +216.
void RunStrbColors(const x360n::avatar::AvatarToc& toc) {
  auto* rt = rex::Runtime::instance();
  auto* memory = rt->memory();
  auto* d = rt->function_dispatcher();
  auto* t = rex::runtime::ThreadState::Get();
  constexpr uint32_t kReader = 0x40, kIter = 0x100, kFieldStream = 0x180;
  constexpr uint32_t kGuard = 0x200, kOut = kGuard + 32, kEnd = kOut + 220;
  constexpr uint32_t kData = 0x400;
  size_t largest = 0;
  for (size_t i = 0; i < toc.size(); ++i) largest = std::max(largest, toc.Blob(i).size());
  const uint32_t scratch = memory->SystemHeapAlloc(uint32_t(kData + largest + 32));
  if (!scratch) { Check(false, "colors: allocate private memory"); return; }
  auto* h = memory->TranslateVirtual<uint8_t*>(scratch);
  auto be = [](const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 |
                                        uint32_t(p[2]) << 8 | p[3]; };
  auto le = [](const uint8_t* p) { return uint32_t(p[3]) << 24 | uint32_t(p[2]) << 16 |
                                        uint32_t(p[1]) << 8 | p[0]; };
  auto put = [](uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
  };
  uint32_t abi = 0, calls = 0, tested = 0, packed = 0, triples = 0;
  auto call = [&](uint32_t fn, std::initializer_list<uint64_t> args) {
    ++calls; return CallQuiet(d, t, fn, args, &abi);
  };
  std::array<uint8_t, 20> callbacks{};
  std::memcpy(callbacks.data(), memory->TranslateVirtual(0x81A975CC), callbacks.size());
  for (size_t record = 0; record < toc.size(); ++record) {
    const auto blob = toc.Blob(record);
    if (blob.empty()) continue;
    // This diagnostic deliberately supports only the observed 17559 encoding.
    if (blob.size() < 32 || std::memcmp(blob.data(), "STRB", 4) || blob[4] != 1 ||
        blob[5] != 0 || blob[22] != 1 || blob[23] != 4 || blob[26] != 4) {
      Check(false, fmt::format("colors: unsupported STRB layout at record {}", record)); break;
    }
    size_t pos = 32;
    uint32_t ordinal = 0;
    while (pos + 12 <= blob.size() && blob[pos] != 7) {
      const uint32_t len = be(blob.data() + pos + 1);
      if (len > blob.size() - pos - 12) { pos = blob.size(); break; }
      pos += 12 + ((size_t(len) + 3) & ~size_t(3));
      ++ordinal;
    }
    if (pos + 12 > blob.size()) continue;  // no palette field in this asset
    const uint32_t len = be(blob.data() + pos + 1);
    if (len < 28 || len > blob.size() - pos - 12 || be(blob.data() + pos + 5) != 1) {
      Check(false, "colors: field 7 bounds/element size"); break;
    }
    const auto* payload = blob.data() + pos + 12;
    const uint32_t count = le(payload);
    std::array<uint32_t, 3> bias{}, bits{};
    uint32_t stride = 0;
    for (size_t c = 0; c < 3; ++c) {
      bias[c] = le(payload + 4 + c * 8);
      bits[c] = le(payload + 8 + c * 8);
      stride += bits[c];
    }
    // Bound inputs before giving them to the original trusted-data decoder.
    if (!count || count > 18 || bits[0] > 32 || bits[1] > 32 || bits[2] > 32 ||
        uint64_t(count) * stride > uint64_t(len - 28) * 8) {
      Check(false, fmt::format("colors: unsupported palette count/width at {}", record)); break;
    }
    std::array<uint8_t, 220> expected;
    expected.fill(0xA5);
    size_t bit = 0;
    for (uint32_t row = 0; row < count; ++row) {
      for (size_t c = 0; c < 3; ++c) {
        uint32_t delta = 0;
        for (uint32_t b = 0; b < bits[c]; ++b, ++bit)
          delta |= uint32_t((payload[28 + bit / 8] >> (bit % 8)) & 1) << b;
        put(expected.data() + row * 12 + c * 4, bias[c] + delta);
      }
    }
    put(expected.data() + 216, count);
    std::memset(h, 0, kData);
    std::memcpy(h + kData, blob.data(), blob.size());
    std::memset(h + kGuard, 0xA5, kEnd + 32 - kGuard);
    std::memset(h + kData + blob.size(), 0xA5, 32);
    call(0x8196D568, {scratch, scratch + kData, blob.size()});
    call(0x8196DBE8, {scratch + kReader, scratch});
    call(0x8196E0E8, {scratch + kIter, scratch + kReader});
    for (uint32_t n = 0; n < ordinal; ++n) call(0x8196F228, {scratch + kIter});
    const bool iterator = rex::memory::load_and_swap<uint64_t>(h + kIter) == pos &&
        rex::memory::load_and_swap<uint64_t>(h + kIter + 8) == 7 &&
        rex::memory::load_and_swap<uint64_t>(h + kIter + 16) == len &&
        rex::memory::load_and_swap<uint64_t>(h + kIter + 24) == 1 &&
        be(h + kIter + 32) == scratch + kReader;
    if (!iterator) { Check(false, fmt::format("colors: iterator bounds at {}", record)); break; }
    call(0x81976080, {scratch + kFieldStream, scratch + kReader, scratch + kIter, 0});
    const auto result = call(0x81976598, {scratch + kOut, scratch + kFieldStream});
    const bool equal = std::memcmp(h + kOut, expected.data(), expected.size()) == 0;
    const bool guards = std::all_of(h + kGuard, h + kOut, [](uint8_t b) { return b == 0xA5; }) &&
        std::all_of(h + kEnd, h + kEnd + 32, [](uint8_t b) { return b == 0xA5; }) &&
        std::all_of(h + kData + blob.size(), h + kData + blob.size() + 32,
                    [](uint8_t b) { return b == 0xA5; });
    const bool source = std::memcmp(h + kData, blob.data(), blob.size()) == 0;
    if (result != 1 || !equal || !guards || !source) {
      Check(false, fmt::format("colors: record {} result {} bytes {} guards {} source {} first "
                               "{:08X}/{:08X} count {}/{}", record, result, equal, guards, source,
                               be(h + kOut), be(expected.data()), be(h + kOut + 216), count));
      break;
    }
    ++tested; triples += count;
    if (stride) ++packed;
    REXLOG_INFO("xam-aot-probe: colors: record {} blob {} field +0x{:X}/{} -> {} triples; "
                "widths {}/{}/{}; first {:08X}/{:08X}/{:08X}; guest 0x{:08X}..0x{:08X}; "
                "220 bytes/reference, guards and source equal", record, blob.size(), pos, len,
                count, bits[0], bits[1], bits[2], be(h + kOut), be(h + kOut + 4),
                be(h + kOut + 8), scratch + kOut, scratch + kEnd);
    if (stride) {
      // Shorten only the field's declared length in a private copy. The packed
      // header still advertises all rows, so its final delta is unavailable.
      put(h + kData + pos + 1, len - 1);
      std::memset(h + kGuard, 0xA5, kEnd + 32 - kGuard);
      call(0x8196DBE8, {scratch + kReader, scratch});
      call(0x8196E0E8, {scratch + kIter, scratch + kReader});
      for (uint32_t n = 0; n < ordinal; ++n) call(0x8196F228, {scratch + kIter});
      call(0x81976080, {scratch + kFieldStream, scratch + kReader, scratch + kIter, 0});
      const auto short_result = call(0x81976598, {scratch + kOut, scratch + kFieldStream});
      const bool untouched = std::all_of(h + kGuard, h + kEnd + 32,
                                         [](uint8_t b) { return b == 0xA5; });
      Check(short_result == 0 && untouched,
            fmt::format("colors: truncated record {} rejected (return {}), output and guards {}",
                        record, short_result, untouched ? "untouched" : "CHANGED"));
    }

  }
  Check(tested > 0 && packed > 0,
        fmt::format("colors: 0x81976598 transformed {} real palettes ({} packed), {} triples",
                    tested, packed, triples));
  Check(abi == 0, fmt::format("colors: ABI preserved over {} calls ({} breaks)", calls, abi));
  Check(std::memcmp(callbacks.data(), memory->TranslateVirtual(0x81A975CC), callbacks.size()) == 0,
        "colors: all five title callbacks untouched; no GPU callback required by this routine");
  memory->SystemHeapFree(scratch);
}


void Run() {
  auto* runtime = rex::Runtime::instance();
  auto* thread = rex::runtime::ThreadState::Get();
  if (!runtime || !thread) {
    Check(false, "probe needs the runtime and a guest thread");
    return;
  }
  auto* ks = runtime->kernel_state();
  auto* dispatcher = runtime->function_dispatcher();
  auto* memory = runtime->memory();
  const std::string root = REXCVAR_GET(x360n_xam17559_root);
  const std::string catalog_path = REXCVAR_GET(x360n_avatar_catalog_17559);
  if (root.empty() || catalog_path.empty()) {
    Check(false, "--x360n_xam17559_root and --x360n_avatar_catalog_17559 are required");
    return;
  }

  // Mount and load. Name lookups of "xam.xex" still reach the SDK's kernel module
  // first (KernelState::GetModule searches kernel modules before user modules).
  auto device = std::make_unique<rex::filesystem::HostPathDevice>("\\Device\\X360nSystem17559",
                                                                   root, true);
  const bool mounted = device->Initialize() && ks->file_system()->RegisterDevice(std::move(device)) &&
                       ks->file_system()->RegisterSymbolicLink("x360nsys:", "\\Device\\X360nSystem17559");
  Check(mounted, "mount " + root + " as x360nsys: (read-only)");
  if (!mounted) return;
  auto module = ks->LoadUserModule("x360nsys:\\xam.xex", false);
  Check(bool(module), "load x360nsys:\\xam.xex without its entry point");
  if (!module) return;
  const uint32_t base = module->xex_module()->base_address();
  const uint32_t size = module->xex_module()->image_size();
  g_xam_hmodule = module->hmodule_ptr();
  Check(base == kXamBase && size == kXamSize,
        fmt::format("load: image 0x{:08X}+0x{:X} (expected 0x{:08X}+0x{:X})", base, size, kXamBase,
                    kXamSize));

  Check(dispatcher->GetFunction(kBodyTypeExport) != nullptr, "registry: 0x816753C8 registered");
  Check(dispatcher->GetFunction(kVectorIterator) != nullptr, "registry: 0x81670000 registered");
  Check(dispatcher->GetFunction(kMidFunction) == nullptr, "registry: mid-function 0x816753CC absent");

  Check(std::memcmp(memory->TranslateVirtual(0x81632E68), kXamMaleBody.data(), 16) == 0,
        "data: XAM .rdata 0x81632E68 holds the MaleBody asset ID");

  // game -> XAM: body type on descriptors built from the original 17559 catalog.
  std::string error;
  auto toc = x360n::avatar::AvatarToc::Parse(Slurp(catalog_path), &error);
  Check(bool(toc), "catalog " + catalog_path + (toc ? "" : " (" + error + ")"));
  if (!toc) return;
  const uint32_t desc = memory->SystemHeapAlloc(x360n::avatar::kMetadataSize);
  if (!desc) {
    Check(false, "allocate a 1000-byte descriptor");
    return;
  }
  auto* desc_host = memory->TranslateVirtual<uint8_t*>(desc);
  for (int body = 0; body <= 2; ++body) {
    std::memset(desc_host, 0, x360n::avatar::kMetadataSize);
    if (body) {
      const size_t index = size_t(body - 1);  // records 0 and 1: MaleBody, FemaleBody
      const auto& rec = toc->record(index);
      const x360n::avatar::AssetId id{rec.field_00, uint16_t(index), 0, toc->Opaque(index)[0],
                                      x360n::avatar::AssetStampFor(*toc)};
      const auto bytes = id.ToBytes();
      std::memcpy(desc_host + x360n::avatar::kBodyAssetAt, bytes.data(), bytes.size());
    }
    const uint32_t expected = x360n::avatar::BodyType(
        std::span<const uint8_t, x360n::avatar::kMetadataSize>(desc_host, x360n::avatar::kMetadataSize));
    const uint32_t got = uint32_t(Call(dispatcher, thread, kBodyTypeExport, {desc}, "0x816753C8"));
    Check(got == expected && expected == uint32_t(body),
          fmt::format("game->XAM: XamAvatarManifestGetBodyType = {} (C++ {}, expected {})", got,
                      expected, body));
  }
  memory->SystemHeapFree(desc);
  if (REXCVAR_GET(motocross_xam_aot_probe_strb)) RunStrbDecode(*toc);
  if (REXCVAR_GET(motocross_xam_aot_probe_colors)) RunStrbColors(*toc);
  if (REXCVAR_GET(motocross_xam_aot_probe_sizes)) RunSizeCheck();

  // XAM -> game: the original vector iterator calls the game's constructor.
  constexpr uint32_t kCount = 4, kStride = 16;
  const uint32_t array = memory->SystemHeapAlloc(kCount * kStride);
  if (!array) {
    Check(false, "allocate the test array");
    return;
  }
  auto* array_host = memory->TranslateVirtual<uint8_t*>(array);
  std::memset(array_host, 0xA5, kCount * kStride);
  Check(dispatcher->GetFunction(kGameCtor) != nullptr, "registry: game constructor 0x82450340");
  Call(dispatcher, thread, kVectorIterator, {array, kStride, kCount, kGameCtor}, "0x81670000");
  bool all = true;
  for (uint32_t i = 0; i < kCount; ++i) {
    const uint8_t* e = array_host + i * kStride;
    if (rex::memory::load_and_swap<uint32_t>(e) != kGameCtorValue) all = false;
    for (uint32_t k = 4; k < kStride; ++k) all = all && e[k] == 0xA5;
  }
  Check(all, "XAM->game: 4 elements hold 0x8200131C at +0, bytes +4..+15 untouched");

  // Errors: an unregistered address inside the XAM image.
  std::memset(array_host, 0x5A, kCount * kStride);
  const uint64_t r = Call(dispatcher, thread, kMidFunction, {array}, "unregistered 0x816753CC");
  bool untouched = true;
  for (uint32_t k = 0; k < kCount * kStride; ++k) untouched = untouched && array_host[k] == 0x5A;
  Check(uint32_t(r) == kFailValue && untouched,
        fmt::format("errors: unregistered address -> 0x{:08X}, memory untouched", uint32_t(r)));
  memory->SystemHeapFree(array);
}

// Experiment, not a route: XAM's own XamAvatarInitialize (ordinal 0x5DC) with the game's
// version, flags, processor and callbacks, but a private 3 MB buffer so that whatever
// XAM keeps there cannot touch the game's. XAM's global initialization (its entry point)
// has not run; this shows how far its app framework gets without it.
void RunOriginalInitialize(const PPCRegister& version, const PPCRegister& flags,
                           const PPCRegister& processor, const PPCRegister& callbacks,
                           uint32_t size) {
  auto* runtime = rex::Runtime::instance();
  auto* thread = rex::runtime::ThreadState::Get();
  auto* memory = runtime->memory();
  constexpr uint32_t kInitializeExport = 0x816755E8, kAppSlotF2 = 0x81AA1470;
  const uint32_t buffer = memory->SystemHeapAlloc(size);
  if (!buffer) {
    Check(false, "initialize: allocate the private buffer");
    return;
  }
  // XAM state the avatar app needs, built by XAM's own code in the order its DllMain and
  // boot run it. XAM's DllMain (0x81696AF0) first stores its module handle (0x816E0B58 ->
  // [0x81AA3EE0]); internal apps take it as their module (0x816BCE58: slot +0x08), and
  // the message sender (0x816BC968) skips an app whose +0x08 is zero (E_INVALIDARG).
  // Only that store runs here; DllMain's call to the whole boot 0x816968A8 does not.
  constexpr uint32_t kSetModuleHandle = 0x816E0B58;
  Call(runtime->function_dispatcher(), thread, kSetModuleHandle, {g_xam_hmodule},
       "module handle store 0x816E0B58");
  REXLOG_INFO("xam-aot-probe: initialize: XAM 0x{:08X}(hmodule 0x{:08X}); [0x81AA3EE0] = 0x{:08X}",
              kSetModuleHandle, g_xam_hmodule,
              rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(0x81AA3EE0)));
  // Then, as at XAM's boot (0x816968A8 calls it first), its system heaps 1..8 in the
  // table at 0x81AABFF8 (0x816E6618); a game thread has no XAM app in its TLS, so its
  // XAM allocations (the message task included) come from system heap 1.
  // XAM 17559 reads a flag at the fixed system page 0x8E03860A while creating them
  // (0x8168AF28: lhz, bit 2). Nothing in the SDK maps that page; the dashboard 17559
  // profile maps it zeroed as an explicit hypothesis, reused here unchanged.
  static dash17559::ExperimentalRegion region;
  const auto mapped = region.Map(*memory, true);
  REXLOG_WARN("xam-aot-probe: [compatibility-experiment] 0x8E030000 size=65536 RW zeroed ({}); "
              "região de compatibilidade experimental; conteúdo real desconhecido",
              mapped == dash17559::ExperimentalRegion::Result::Mapped ? "mapped" : "not mapped");
  if (mapped != dash17559::ExperimentalRegion::Result::Mapped) {
    Check(false, "initialize: map the experimental system page");
    return;
  }
  constexpr uint32_t kSystemHeapsInit = 0x816E6618, kTitleHeapInit = 0x816E6870;
  const uint32_t system_heaps = uint32_t(Call(runtime->function_dispatcher(), thread,
                                              kSystemHeapsInit, {}, "system heaps init 0x816E6618"));
  REXLOG_INFO("xam-aot-probe: initialize: XAM system heaps init 0x{:08X} -> 0x{:08X}",
              kSystemHeapsInit, system_heaps);
  // Next at XAM's boot that the avatar app depends on: the task scheduler (0x816BA650 ->
  // 0x816BA120), which allocates the TLS slot of the current XAM app (0x81A82570) and
  // starts XAM's worker threads; Initialize posts its message through it (0x816BD620).
  constexpr uint32_t kSchedulerInit = 0x816BA650;
  const uint32_t scheduler = uint32_t(Call(runtime->function_dispatcher(), thread, kSchedulerInit,
                                           {}, "task scheduler init 0x816BA650"));
  REXLOG_INFO("xam-aot-probe: initialize: XAM task scheduler init 0x{:08X} -> 0x{:08X}; TLS slot "
              "0x81A82570 = 0x{:08X}",
              kSchedulerInit, scheduler,
              rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(0x81A82570)));
  // XAM's title manager (boot step 0x816A17B8: 6536-byte object at [0x81A9F850], then
  // 0x816A14B8). The avatar app asks it whether the running title is the dashboard
  // (0x8196B8C8 -> 0x816E0C68 -> 0x8169CE70, which dereferences it unconditionally).
  constexpr uint32_t kTitleManagerInit = 0x816A17B8;
  const uint32_t manager = uint32_t(Call(runtime->function_dispatcher(), thread, kTitleManagerInit,
                                         {}, "title manager init 0x816A17B8"));
  const uint32_t manager_ptr =
      rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(0x81A9F850));
  REXLOG_INFO("xam-aot-probe: initialize: XAM title manager init 0x{:08X} -> 0x{:08X}; "
              "[0x81A9F850] = 0x{:08X}, state {}",
              kTitleManagerInit, manager, manager_ptr,
              manager_ptr ? rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(manager_ptr))
                          : 0u);
  // XAM's title heap (0x816E6870, from the title start path 0x816A01A8): sized from the
  // running title's XEX header field 0x40201 through XexExecutableModuleHandle.
  const uint32_t heap = uint32_t(Call(runtime->function_dispatcher(), thread, kTitleHeapInit, {},
                                      "title heap init 0x816E6870"));
  REXLOG_INFO("xam-aot-probe: initialize: XAM title heap init 0x{:08X} -> 0x{:08X}", kTitleHeapInit,
              heap);
  REXLOG_INFO("xam-aot-probe: initialize: calling XAM 0x{:08X}(0x{:X}, 0x{:X}, 0x{:X}, 0x{:08X}, "
              "0x{:08X}, 0x{:X})",
              kInitializeExport, uint32_t(version.u64), uint32_t(flags.u64),
              uint32_t(processor.u64), uint32_t(callbacks.u64), buffer, size);
  const uint32_t r = uint32_t(Call(runtime->function_dispatcher(), thread, kInitializeExport,
                                   {version.u64, flags.u64, processor.u64, callbacks.u64, buffer,
                                    size},
                                   "XamAvatarInitialize 0x816755E8"));
  const auto* slot = memory->TranslateVirtual<const uint8_t*>(kAppSlotF2);
  REXLOG_INFO("xam-aot-probe: initialize: XAM returned 0x{:08X}; app 0xF2 slot +0x08=0x{:08X} "
              "+0x10=0x{:08X} +0x84=0x{:08X} +0x88=0x{:X}",
              r, rex::memory::load_and_swap<uint32_t>(slot + 0x08),
              rex::memory::load_and_swap<uint32_t>(slot + 0x10),
              rex::memory::load_and_swap<uint32_t>(slot + 0x84),
              rex::memory::load_and_swap<uint32_t>(slot + 0x88));
  // The buffer is not freed: if XAM kept it, freeing it would hand it to someone else.
}

}  // namespace

REXCVAR_DEFINE_BOOL(motocross_xam_aot_probe_initialize, false, "Motocross Madness",
                    "Diagnostic, after the probe: call the original XAM XamAvatarInitialize with "
                    "the game's arguments and a private buffer; the game still gets the SDK's "
                    "result");

namespace motocross {

void MaybeRunXamAotProbe(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& r6,
                         PPCRegister& r7, PPCRegister& r8) {
  static bool done = false;
  if (done || !REXCVAR_GET(motocross_xam_aot_probe)) return;
  done = true;
  auto* thread = rex::runtime::ThreadState::Get();
  if (!thread) {
    Check(false, "no guest thread");
    return;
  }
  // The registers the generated code hands to the hook are the thread's context, so a
  // call made from here sees the game's stack pointer and TLS exactly as the import would.
  Check(&r3 == &thread->context()->r3 && &r8 == &thread->context()->r8,
        "context: hook registers are the guest thread's PPCContext");
  (void)r7;
  PPCContext saved = *thread->context();
  Run();
  if (REXCVAR_GET(motocross_xam_aot_probe_initialize)) {
    *thread->context() = saved;
    RunOriginalInitialize(saved.r3, saved.r4, saved.r5, saved.r6, uint32_t(saved.r8.u64));
  }
  *thread->context() = saved;
  thread->context()->fpscr.restoreGuestBits(saved.fpscr.csr);
  REXLOG_INFO("xam-aot-probe: done, {} failure(s); game context restored", g_failures);
}

}  // namespace motocross
