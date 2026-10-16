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
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <rex/cvar.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/ppc/context.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/lzx.h>
#include <rex/system/thread_state.h>
#include <rex/system/user_module.h>
#include <rex/system/xex_module.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <filesystem>
#include "../../common/avatar/avatar_aot_catalog.h"
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "../../common/avatar/avatar_aot_assets.h"
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
// Defined by the shared avatar service (titles/common/avatar/avatar_service_17559.cpp).
REXCVAR_DECLARE(std::string, x360n_avatar_catalog_17559);
REXCVAR_DECLARE(bool, x360n_avatar_service);
REXCVAR_DEFINE_BOOL(motocross_xam_aot_probe_strb, false, "Motocross Madness",
                    "Diagnostic, with the probe: decode every STRB blob of the 17559 catalog with "
                    "XAM's original Structured Binary reader and compare with the bytes");
REXCVAR_DEFINE_BOOL(motocross_xam_aot_probe_sizes, false, "Motocross Madness",
                    "Diagnostic, with the probe: run XAM's GetAssets size routine 0x81965EC0 on "
                    "every valid component mask and compare with the C++ ResultSizeFor");

REXCVAR_DEFINE_BOOL(motocross_xam_aot_probe_colors, false, "Motocross Madness",
                    "Diagnostic: expand original STRB color palettes with XAM AOT and verify every byte");

REXCVAR_DEFINE_BOOL(motocross_xam_aot_probe_meshes, false, "Motocross Madness",
                    "Diagnostic: only field 3 meshes/textures with real game "
                    "callbacks; skip prior suites");

REXCVAR_DEFINE_BOOL(motocross_xam_aot_probe_ldi, false, "Motocross Madness",
                    "Diagnostic: original compressed stream and per-asset "
                    "GetAssets worker only");

REXCVAR_DEFINE_BOOL(motocross_xam_aot_probe_aggregate, false, "Motocross Madness",
                    "Diagnostic: aggregate result and request lifecycle only" );

REXCVAR_DEFINE_BOOL(motocross_avatar_service_probe, false, "Motocross Madness", "Diagnostic: avatar service integration" );
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

// Field 3 transport: 12-byte LE headers (compressed size, output offset, output
// size), each followed by an independently reset LZX block. This diagnostic
// uses the SDK's existing decoder because the kernel LDI imports are still
// stubs.
uint32_t MeshLE(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
         uint32_t(p[3]) << 24;
}
uint32_t MeshBE(const uint8_t* p) {
  return rex::memory::load_and_swap<uint32_t>(p);
}
void MeshPut(uint8_t* p, uint32_t v) {
  rex::memory::store_and_swap<uint32_t>(p, v);
}

bool MeshInflate(std::span<const uint8_t> src, std::vector<uint8_t>& dst) {
  dst.clear();
  while (!src.empty()) {
    if (src.size() < 12) return false;
    const uint32_t packed = MeshLE(src.data()), offset = MeshLE(src.data() + 4),
                   size = MeshLE(src.data() + 8);
    if (!packed || packed > 0x9800 || packed > src.size() - 12 || !size ||
        size > 0x8000 || offset != dst.size() ||
        offset > 16 * 1024 * 1024 - size)
      return false;
    dst.resize(offset + size);
    if (lzx_decompress(src.data() + 12, packed, dst.data() + offset, size,
                       0x8000, nullptr, 0))
      return false;
    src = src.subspan(12 + packed);
  }
  return dst.size() >= 48;
}

// A bounded sequential memory stream, with the same five slots used by XAM's
// compressed stream. Slot 3 advances, rather than seeking absolutely. All mesh,
// index, vertex and texture interpretation remains in original XAM AOT code.
struct MeshStreamState {
  std::span<const uint8_t> bytes;
  size_t pos = 0;
  uint32_t misses = 0;
};
MeshStreamState* g_mesh_stream = nullptr;
void MeshStreamDestroy(PPCContext& c, uint8_t*) { c.r3.u64 = 0; }
void MeshStreamReset(PPCContext& c, uint8_t*) {
  g_mesh_stream->pos = 0;
  c.r3.u64 = 1;
}
void MeshStreamTell(PPCContext& c, uint8_t*) { c.r3.u64 = g_mesh_stream->pos; }
void MeshStreamSkip(PPCContext& c, uint8_t*) {
  auto& s = *g_mesh_stream;
  const uint32_t n = uint32_t(c.r4.u64);
  c.r3.u64 = n <= s.bytes.size() - s.pos;
  if (c.r3.u64)
    s.pos += n;
  else
    ++s.misses;
}
void MeshStreamRead(PPCContext& c, uint8_t* base) {
  auto& s = *g_mesh_stream;
  const uint32_t n = uint32_t(c.r5.u64);
  MeshPut(base + uint32_t(c.r6.u64), 0);
  if (n > s.bytes.size() - s.pos) {
    ++s.misses;
    c.r3.u64 = 0;
    return;
  }
  std::memcpy(base + uint32_t(c.r4.u64), s.bytes.data() + s.pos, n);
  s.pos += n;
  MeshPut(base + uint32_t(c.r6.u64), n);
  c.r3.u64 = 1;
}

std::array<PPCFunc*, 5> g_mesh_callbacks{};
std::array<uint32_t, 5> g_mesh_callback_calls{};
uint32_t g_mesh_callback_abi = 0;
struct MeshTextureChecks {
  uint32_t gpu, gpu_size, scratch, scratch_size;
  uint32_t failures = 0, tiled = 0, layouts = 0;
  uint64_t compared = 0;
};
MeshTextureChecks* g_mesh_texture_checks = nullptr;

// Independent of game AOT: Xenos 2D address bit layout, also documented by
// rex/graphics/pipeline/texture/util.cpp::GetTiledOffset2D in the pinned SDK.
uint32_t MeshTiledAddress(uint32_t x, uint32_t y, uint32_t width,
                          uint32_t log_bytes) {
  const uint32_t macro = (x / 32 + y / 32 * ((width + 31) / 32))
                         << (log_bytes + 7);
  const uint32_t micro = ((x % 8) + (y & 14) * 4) << log_bytes;
  const uint32_t combined =
      macro + (micro & ~15u) * 2 + (micro & 15) + (y % 2) * 16;
  return (combined & ~511u) * 8 + (y & 16) * 128 + (combined & 448) * 4 +
         (((y & 8) / 4 + x / 8) % 4) * 64 + (combined & 63);
}

bool MeshFormat(uint32_t format, uint32_t& block, uint32_t& log_bytes) {
  switch (format & 63) {
    case 2:
      block = 1;
      log_bytes = 0;
      return true;  // 8
    case 6:
      block = 1;
      log_bytes = 2;
      return true;  // 8_8_8_8
    case 18:
    case 58:
    case 59:
      block = 4;
      log_bytes = 3;
      return true;
    case 19:
    case 20:
      block = 4;
      log_bytes = 4;
      return true;
    default:
      return false;
  }
}
bool MeshRange(uint32_t p, uint64_t n, uint32_t start, uint32_t size) {
  return p >= start && uint64_t(p) + n <= uint64_t(start) + size;
}

template <size_t I>
void MeshObserveCallback(PPCContext& c, uint8_t* base) {
  const auto before = Snapshot(c);
  const std::array<uint32_t, 8> a = {uint32_t(c.r3.u64), uint32_t(c.r4.u64),
                                     uint32_t(c.r5.u64), uint32_t(c.r6.u64),
                                     uint32_t(c.r7.u64), uint32_t(c.r8.u64),
                                     uint32_t(c.r9.u64), uint32_t(c.r10.u64)};
  ++g_mesh_callback_calls[I];
  g_mesh_callbacks[I](
      c,
      base);  // Execute the saved real function with the unchanged guest ABI.
  if (!SameNonVolatile(before, Snapshot(c))) ++g_mesh_callback_abi;
  auto* checks = g_mesh_texture_checks;
  if (!checks) return;
  uint32_t block = 0, log_bytes = 0;
  if constexpr (I == 0 || I == 2) {
    if (!MeshFormat(a[0], block, log_bytes) || a[3] != 0) {
      ++checks->failures;
      REXLOG_ERROR(
          "xam-aot-probe: meshes: unsupported reference format {:08X} mip {}",
          a[0], a[3]);
      return;
    }
    const uint32_t columns = (a[1] + block - 1) / block,
                   rows = (a[2] + block - 1) / block;
    const uint32_t row_bytes = columns << log_bytes;
    if constexpr (I == 2) {
      ++checks->layouts;
      const bool ok = MeshBE(base + a[4]) == row_bytes &&
                      MeshBE(base + a[6]) == rows &&
                      MeshBE(base + a[5]) >= row_bytes;
      if (!ok) ++checks->failures;
    } else {
      ++checks->tiled;
      bool ok = c.r3.u64 == 1 && a[4] >= row_bytes &&
                MeshRange(a[6], uint64_t(a[4]) * rows, checks->scratch,
                          checks->scratch_size) &&
                MeshRange(a[7], a[5], checks->gpu, checks->gpu_size);
      for (uint32_t y = 0; ok && y < rows; ++y)
        for (uint32_t x = 0; ok && x < columns; ++x) {
          const uint32_t tiled = MeshTiledAddress(x, y, columns, log_bytes);
          const uint32_t n = 1u << log_bytes;
          ok = uint64_t(tiled) + n <= a[5] &&
               !std::memcmp(base + a[7] + tiled,
                            base + a[6] + y * a[4] + (x << log_bytes), n);
          if (ok) checks->compared += n;
        }
      if (!ok) {
        ++checks->failures;
        REXLOG_ERROR(
            "xam-aot-probe: meshes: tiled bytes mismatch format "
            "{:08X} {}x{} pitch {} capacity {}",
            a[0], a[1], a[2], a[4], a[5]);
      }
    }
  }
}

bool MeshValidateBuffers(const uint8_t* base, const std::array<uint32_t, 12>& h,
                         uint32_t cpu, uint32_t gpu, uint64_t& indices) {
  for (uint32_t i = 0; i < h[5]; ++i) {
    const uint8_t* mesh = base + cpu + h[9] + i * 512;
    const uint32_t triangles = MeshBE(mesh + 484),
                   vertices = MeshBE(mesh + 488), stride = MeshBE(mesh + 496),
                   vp = MeshBE(mesh + 504), ip = MeshBE(mesh + 508);
    if (!stride || !MeshRange(vp, uint64_t(vertices) * stride, gpu, h[1]) ||
        !MeshRange(ip, uint64_t(triangles) * 6, gpu, h[1]))
      return false;
    for (uint64_t j = 0; j < uint64_t(triangles) * 3; ++j) {
      if (rex::memory::load_and_swap<uint16_t>(base + ip + j * 2) >= vertices)
        return false;
      ++indices;
    }
  }
  for (uint32_t i = 0; i < h[6]; ++i) {
    const uint8_t* tex = base + cpu + h[10] + i * 44;
    if (!MeshRange(MeshBE(tex + 36), MeshBE(tex + 12), gpu, h[1]) ||
        !MeshBE(tex + 4) || !MeshBE(tex + 8))
      return false;
  }
  return true;
}

void RunMeshes(const x360n::avatar::AvatarToc& toc, uint32_t game_callbacks) {
  auto* rt = rex::Runtime::instance();
  auto* m = rt->memory();
  auto* d = rt->function_dispatcher();
  auto* t = rex::runtime::ThreadState::Get();
  constexpr std::array<uint32_t, 5> expected = {
      0x8295B630, 0x8295B720, 0x8295B3C0, 0x8295B458, 0x8295B500};
  if (!game_callbacks) {
    Check(false, "meshes: game callback table is null");
    return;
  }
  const auto* table = m->TranslateVirtual<const uint8_t*>(game_callbacks);
  for (size_t i = 0; i < 5; ++i) {
    if (MeshBE(table + i * 4) != expected[i] ||
        !(g_mesh_callbacks[i] = d->GetFunction(expected[i]))) {
      Check(false,
            "meshes: unrecognized/unregistered real game callback table");
      return;
    }
  }
  std::array<uint8_t, 20> saved_callbacks{};
  std::memcpy(saved_callbacks.data(), m->TranslateVirtual(0x81A975CC), 20);
  uint32_t abi = 0;
  CallQuiet(d, t, 0x81977C80, {game_callbacks}, &abi);
  const std::array<PPCFunc*, 5> observers = {
      MeshObserveCallback<0>, MeshObserveCallback<1>, MeshObserveCallback<2>,
      MeshObserveCallback<3>, MeshObserveCallback<4>};
  for (size_t i = 0; i < 5; ++i) d->SetFunction(expected[i], observers[i]);
  // Fixed private allocations are capped independently of the input's
  // capacities.
  constexpr uint32_t cap = 0x200000, guard = 0x1000;
  std::vector<uint32_t> allocations;
  auto alloc = [&](uint32_t n) {
    auto p = m->SystemHeapAlloc(n, 0x1000);
    if (p) allocations.push_back(p);
    return p;
  };
  const uint32_t state = alloc(0x1000), cpu_alloc = alloc(cap + 2 * guard),
                 gpu_alloc = alloc(cap + 2 * guard),
                 tmp_alloc = alloc(cap + 2 * guard);
  const uint32_t cpu = cpu_alloc + guard, gpu = gpu_alloc + guard,
                 tmp = tmp_alloc + guard;
  uint32_t tested = 0, meshes = 0, textures = 0, negatives = 0;
  uint64_t indices = 0, texture_bytes = 0;
  if (!state || !cpu_alloc || !gpu_alloc || !tmp_alloc) {
    Check(false, "meshes: private allocations");
  } else {
    auto* h = m->TranslateVirtual<uint8_t*>(state);
    const std::array<PPCFunc*, 5> methods = {MeshStreamDestroy, MeshStreamReset,
                                             MeshStreamTell, MeshStreamSkip,
                                             MeshStreamRead};
    bool registered = true;
    for (size_t i = 0; i < 5; ++i) {
      const uint32_t thunk = d->AllocateThunk(methods[i], 0x81974A90);
      MeshPut(h + 0x100 + i * 4, thunk);
      registered &= thunk != 0;
    }
    MeshPut(h + 0x80, state + 0x100);
    Check(registered, "meshes: bounded sequential stream registered");
    if (registered)
      for (size_t record = 0; record < toc.size(); ++record) {
        const auto blob = toc.Blob(record);
        if (blob.empty()) continue;
        if (blob.size() < 32 || std::memcmp(blob.data(), "STRB", 4) ||
            blob[4] != 1 || blob[5] != 0 || blob[22] != 1 || blob[23] != 4 ||
            blob[26] != 4) {
          Check(false, "meshes: unsupported STRB layout");
          break;
        }
        size_t pos = 32;
        while (pos + 12 <= blob.size() && blob[pos] != 3) {
          const auto len = MeshBE(blob.data() + pos + 1);
          if (len > blob.size() - pos - 12) {
            pos = blob.size();
            break;
          }
          pos += 12 + ((size_t(len) + 3) & ~size_t(3));
        }
        if (pos + 12 > blob.size()) continue;
        const uint32_t len = MeshBE(blob.data() + pos + 1);
        std::vector<uint8_t> expanded;
        if (len > blob.size() - pos - 12 ||
            MeshBE(blob.data() + pos + 5) != 1 ||
            !MeshInflate(blob.subspan(pos + 12, len), expanded)) {
          Check(false,
                fmt::format("meshes: LZX framing/decode record {}", record));
          break;
        }
        std::array<uint32_t, 12> header{};
        for (size_t i = 0; i < 12; ++i)
          header[i] = MeshLE(expanded.data() + i * 4);
        REXLOG_DEBUG(
            "xam-aot-probe: meshes: record {} LZX {} -> {} bytes; header {:X}",
            record, len, expanded.size(), fmt::join(header, "/"));
        const bool bounds =
            header[0] <= cap && header[1] <= cap && header[11] <= cap &&
            uint64_t(header[9]) + uint64_t(header[5]) * 512 <= header[0] &&
            uint64_t(header[10]) + uint64_t(header[6]) * 44 <= header[0] &&
            header[7] <= header[1] && header[8] <= header[1];
        if (!bounds) {
          Check(false, "meshes: decoded header capacities/offsets");
          break;
        }
        for (auto p : {cpu_alloc, gpu_alloc, tmp_alloc})
          std::memset(m->TranslateVirtual(p), 0xCD, cap + 2 * guard);
        std::memset(m->TranslateVirtual(cpu), 0, header[0]);
        std::memset(m->TranslateVirtual(gpu), 0, header[1]);
        std::memset(m->TranslateVirtual(tmp), 0, header[11]);
        std::memset(h, 0, 52);
        MeshPut(h, header[0]);
        MeshPut(h + 4, header[1]);
        MeshPut(h + 28, cpu);
        MeshPut(h + 32, gpu);
        MeshStreamState stream{expanded};
        g_mesh_stream = &stream;
        MeshTextureChecks texture_check{gpu, header[1], tmp, header[11]};
        g_mesh_texture_checks = &texture_check;
        const uint32_t result = uint32_t(CallQuiet(
            d, t, 0x81974A90, {state, state + 0x80, header[11], tmp}, &abi));
        if (result != 1 || stream.misses || stream.pos != expanded.size())
          Check(false, fmt::format("meshes: record {} original reader={} "
                                   "consumed {}/{} misses={}",
                                   record, result, stream.pos, expanded.size(),
                                   stream.misses));
        bool descriptors = true;
        for (size_t i = 0; i < 7; ++i)
          descriptors &= MeshBE(h + i * 4) == header[i];
        descriptors &= MeshBE(h + 36) == gpu + header[7] &&
                       MeshBE(h + 40) == gpu + header[8] &&
                       MeshBE(h + 44) == cpu + header[9] &&
                       MeshBE(h + 48) == cpu + header[10];
        bool guards = true;
        for (auto p : {cpu_alloc, gpu_alloc, tmp_alloc}) {
          const auto* b = m->TranslateVirtual<const uint8_t*>(p);
          for (uint32_t i = 0; i < guard; ++i)
            guards &= b[i] == 0xCD && b[guard + cap + i] == 0xCD;
        }
        for (auto pair : {std::pair{cpu, header[0]}, std::pair{gpu, header[1]},
                          std::pair{tmp, header[11]}}) {
          const auto* b =
              m->TranslateVirtual<const uint8_t*>(pair.first + pair.second);
          for (uint32_t i = 0; i < 32; ++i) guards &= b[i] == 0xCD;
        }
        descriptors &= MeshValidateBuffers(m->virtual_membase(), header, cpu,
                                           gpu, indices);
        if (!descriptors || !guards || texture_check.failures)
          Check(false, fmt::format("meshes: record {} descriptors={} guards={} "
                                   "texture failures={}",
                                   record, descriptors, guards,
                                   texture_check.failures));
        texture_bytes += texture_check.compared;
        g_mesh_texture_checks = nullptr;
        if (record == 72 && result == 1) {
          std::array<uint8_t, 52> original{};
          std::memcpy(original.data(), h, original.size());
          MeshPut(h, header[0] - 1);
          std::array<uint8_t, 52> short_desc{};
          std::memcpy(short_desc.data(), h, short_desc.size());
          const auto rejected = CallQuiet(
              d, t, 0x81974A90, {state, state + 0x80, header[11], tmp}, &abi);
          const bool capacity_ok =
              rejected == 0 &&
              !std::memcmp(h, short_desc.data(), short_desc.size());
          Check(capacity_ok,
                "meshes negative: insufficient CPU capacity "
                "rejects and restores descriptor");
          negatives += capacity_ok;
          std::memcpy(h, original.data(), original.size());
          stream =
              MeshStreamState{std::span<const uint8_t>(expanded).first(47)};
          const auto short_read = CallQuiet(
              d, t, 0x81974A90, {state, state + 0x80, header[11], tmp}, &abi);
          const bool short_ok =
              short_read == 0 && stream.misses == 1 &&
              !std::memcmp(h, original.data(), original.size());
          Check(short_ok,
                "meshes negative: truncated decoded header rejects "
                "without descriptor writes");
          negatives += short_ok;
          std::vector<uint8_t> unused;
          const bool lzx_ok =
              !MeshInflate(blob.subspan(pos + 12, len - 1), unused);
          Check(lzx_ok,
                "meshes negative: truncated LZX frame rejects before AOT");
          negatives += lzx_ok;
        }
        g_mesh_stream = nullptr;
        if (!result || !descriptors || !guards || texture_check.failures) break;
        ++tested;
        meshes += header[5];
        textures += header[6];
        if (tested % 100 == 0)
          REXLOG_INFO("xam-aot-probe: meshes: {} assets validated", tested);
      }
  }
  for (size_t i = 0; i < 5; ++i)
    d->SetFunction(expected[i], g_mesh_callbacks[i]);
  std::memcpy(m->TranslateVirtual(0x81A975CC), saved_callbacks.data(), 20);
  for (auto p : allocations) m->SystemHeapFree(p);
  Check(
      tested > 3 && meshes > 0 && textures > 0 && texture_bytes > 0 &&
          negatives == 3,
      fmt::format("meshes: coverage {} assets, {} meshes, {} textures, {} "
                  "indices, {} tiled bytes, {} negatives",
                  tested, meshes, textures, indices, texture_bytes, negatives));
  Check(abi == 0 && g_mesh_callback_abi == 0,
        "meshes: XAM and real callback non-volatile ABI");
  REXLOG_INFO(
      "xam-aot-probe: meshes: {} assets, {} meshes, {} textures; real "
      "callbacks {}/{}/{}/{}/{}",
      tested, meshes, textures, g_mesh_callback_calls[0],
      g_mesh_callback_calls[1], g_mesh_callback_calls[2],
      g_mesh_callback_calls[3], g_mesh_callback_calls[4]);
}

// New route: original STRB field stream -> original LDI stream -> the GetAssets
// per-asset worker. The previous mesh/color/size/integration suites are not
// run.
void RunLdiAssets(const x360n::avatar::AvatarToc& toc,
                  uint32_t game_callbacks) {
  auto* rt = rex::Runtime::instance();
  auto* m = rt->memory();
  auto* d = rt->function_dispatcher();
  auto* t = rex::runtime::ThreadState::Get();
  uint32_t abi = 0;
  auto call = [&](uint32_t fn, std::initializer_list<uint64_t> args) {
    return uint32_t(CallQuiet(d, t, fn, args, &abi));
  };
  constexpr uint32_t cap = 0x200000, guard = 0x1000, ldi_size = 0x1CA0F;
  std::vector<uint32_t> owned;
  auto alloc = [&](uint32_t size) {
    const auto p = m->SystemHeapAlloc(size, 0x1000);
    if (p) owned.push_back(p);
    return p;
  };
  const auto control = alloc(0x40000), cpu_alloc = alloc(cap + 2 * guard),
             gpu_alloc = alloc(cap + 2 * guard),
             temp_alloc = alloc(cap + 2 * guard),
             ldi_alloc = alloc(ldi_size + 2 * guard);
  if (!control || !cpu_alloc || !gpu_alloc || !temp_alloc || !ldi_alloc) {
    Check(false, "ldi-assets: private allocations");
    for (auto p : owned) m->SystemHeapFree(p);
    return;
  }
  auto* h = m->TranslateVirtual<uint8_t*>(control);
  const uint32_t cpu = cpu_alloc + guard, gpu = gpu_alloc + guard,
                 temp = temp_alloc + guard, ldi = ldi_alloc + guard;
  const uint32_t reader = control + 0x40, iter = control + 0x100,
                 field = control + 0x180, compressed = control + 0x200,
                 output = control + 0x300, allocator = control + 0x400,
                 selection = control + 0x500;
  constexpr std::array<uint32_t, 5> addresses = {
      0x8295B630, 0x8295B720, 0x8295B3C0, 0x8295B458, 0x8295B500};
  const std::array<PPCFunc*, 5> observers = {
      MeshObserveCallback<0>, MeshObserveCallback<1>, MeshObserveCallback<2>,
      MeshObserveCallback<3>, MeshObserveCallback<4>};
  const auto* game_table = m->TranslateVirtual<const uint8_t*>(game_callbacks);
  bool registered = game_callbacks != 0;
  for (size_t i = 0; registered && i < 5; ++i) {
    g_mesh_callbacks[i] = d->GetFunction(addresses[i]);
    registered =
        MeshBE(game_table + i * 4) == addresses[i] && g_mesh_callbacks[i];
  }
  std::array<uint8_t, 20> saved{};
  std::memcpy(saved.data(), m->TranslateVirtual(0x81A975CC), saved.size());
  if (registered) {
    call(0x81977C80, {game_callbacks});
    for (size_t i = 0; i < 5; ++i)
      registered &= d->SetFunction(addresses[i], observers[i]);
  }
  Check(registered, "ldi-assets: real game callbacks installed");
  uint32_t assets = 0, seeks = 0;
  for (size_t record : {size_t(0), size_t(1), size_t(72)}) {
    if (!registered) break;
    const auto blob = toc.Blob(record);
    if (blob.size() > 0x3F000) {
      Check(false, "ldi-assets: blob capacity");
      break;
    }
    size_t pos = 32;
    uint32_t ordinal = 0;
    while (pos + 12 <= blob.size() && blob[pos] != 3) {
      const uint32_t len = MeshBE(blob.data() + pos + 1);
      if (len > blob.size() - pos - 12) {
        pos = blob.size();
        break;
      }
      pos += 12 + ((size_t(len) + 3) & ~size_t(3));
      ++ordinal;
    }
    if (pos + 12 > blob.size()) {
      Check(false, "ldi-assets: missing field 3");
      break;
    }
    const auto len = MeshBE(blob.data() + pos + 1);
    std::vector<uint8_t> reference;
    if (len > blob.size() - pos - 12 ||
        !MeshInflate(blob.subspan(pos + 12, len), reference)) {
      Check(false, "ldi-assets: bounded reference transport");
      break;
    }
    std::array<uint32_t, 12> header{};
    for (size_t i = 0; i < 12; ++i)
      header[i] = MeshLE(reference.data() + i * 4);
    if (header[0] > cap || header[1] > cap || header[11] > cap ||
        uint64_t(header[9]) + uint64_t(header[5]) * 512 > header[0] ||
        uint64_t(header[10]) + uint64_t(header[6]) * 44 > header[0]) {
      Check(false, "ldi-assets: header capacity");
      break;
    }
    std::memset(h, 0, 0x1000);
    std::memcpy(h + 0x1000, blob.data(), blob.size());
    call(0x8196D568, {control, control + 0x1000, blob.size()});
    call(0x8196DBE8, {reader, control});
    call(0x8196E0E8, {iter, reader});
    for (uint32_t i = 0; i < ordinal; ++i) call(0x8196F228, {iter});
    // Exercise new compressed-stream position/reset behavior across a 32 KiB
    // boundary. Reference is transport-only: it does not call this stream.
    if (record == 72) {
      call(0x81976080, {field, reader, iter, 0});
      call(0x819766B0, {compressed, field, ldi_size, ldi});
      const uint32_t context = MeshBE(h + 0x20C), vtable = MeshBE(h + 0x200);
      Check(context != 0,
            "ldi-assets: original stream created a live LDI context");
      if (!context) break;
      const auto* v = m->TranslateVirtual<const uint8_t*>(vtable);
      auto read = [&](uint32_t n) {
        return call(MeshBE(v + 16),
                    {compressed, control + 0x800, n, control + 0x900});
      };
      const bool first =
          read(48) == 1 && !std::memcmp(h + 0x800, reference.data(), 48);
      const bool skipped =
          call(MeshBE(v + 12), {compressed, 0x8000 - 32 - 48}) == 1;
      const bool crossed =
          read(96) == 1 &&
          !std::memcmp(h + 0x800, reference.data() + 0x8000 - 32, 96);
      const bool reset = call(MeshBE(v + 4), {compressed}) == 1;
      const bool again =
          read(48) == 1 && !std::memcmp(h + 0x800, reference.data(), 48);
      call(0x81976648, {compressed});
      const bool destroyed =
          MeshBE(m->TranslateVirtual<const uint8_t*>(context)) == 0;
      Check(first && skipped && crossed && reset && again && destroyed,
            fmt::format("ldi-assets: original stream header={} skip={} "
                        "cross-block={} rewind={} reread={} destroy={}",
                        first, skipped, crossed, reset, again, destroyed));
      seeks += first && skipped && crossed && reset && again && destroyed;
    }
    // The original worker receives a private bump allocator and explicit
    // diagnostic material choices; these do not represent a user's avatar.
    for (auto p : {cpu_alloc, gpu_alloc, temp_alloc})
      std::memset(m->TranslateVirtual(p), 0xCD, cap + 2 * guard);
    std::memset(m->TranslateVirtual(cpu), 0, cap);
    std::memset(m->TranslateVirtual(gpu), 0, cap);
    std::memset(m->TranslateVirtual(temp), 0, cap);
    std::memset(m->TranslateVirtual(ldi_alloc), 0xCD, ldi_size + 2 * guard);
    const std::array<uint32_t, 10> pool = {
        cap, cap, header[11], ldi_size, cpu, gpu, temp, ldi, cpu, gpu};
    for (size_t i = 0; i < pool.size(); ++i)
      MeshPut(h + 0x400 + i * 4, pool[i]);
    rex::memory::store_and_swap<uint16_t>(
        h + 0x500 + 48, uint16_t(toc.record(record).field_00));
    for (uint32_t i = 0; i < 3; ++i) {
      MeshPut(h + 0x500 + 80 + i * 32, 22 + i);
      for (uint32_t j = 0; j < 4; ++j)
        rex::memory::store_and_swap<float>(h + 0x500 + 64 + i * 32 + j * 4,
                                           j == 3 ? 1.0f : 0.5f);
    }
    MeshTextureChecks texture_check{gpu, cap, temp, header[11]};
    g_mesh_texture_checks = &texture_check;
    const uint32_t result =
        call(0x81966450, {allocator, selection, 0, reader, iter, output});
    g_mesh_texture_checks = nullptr;
    bool output_ok = result == 0;
    for (size_t i = 0; i < 7; ++i)
      output_ok &= MeshBE(h + 0x300 + i * 4) == header[i];
    output_ok &= MeshBE(h + 0x31C) == cpu && MeshBE(h + 0x320) == gpu &&
                 MeshBE(h + 0x32C) == cpu + header[9] &&
                 MeshBE(h + 0x330) == cpu + header[10];
    uint64_t indices = 0;
    if (output_ok)
      output_ok =
          MeshValidateBuffers(m->virtual_membase(), header, cpu, gpu, indices);
    bool guards = true;
    for (auto p : {cpu_alloc, gpu_alloc, temp_alloc}) {
      const auto* b = m->TranslateVirtual<const uint8_t*>(p);
      for (uint32_t i = 0; i < guard; ++i)
        guards &= b[i] == 0xCD && b[guard + cap + i] == 0xCD;
    }
    const auto* l = m->TranslateVirtual<const uint8_t*>(ldi_alloc);
    for (uint32_t i = 0; i < guard; ++i)
      guards &= l[i] == 0xCD && l[guard + ldi_size + i] == 0xCD;
    const bool source = !std::memcmp(h + 0x1000, blob.data(), blob.size());
    const bool destroyed =
        MeshBE(m->TranslateVirtual<const uint8_t*>(ldi + 0x11800)) == 0;
    Check(
        output_ok && guards && source && destroyed && !texture_check.failures,
        fmt::format(
            "ldi-assets: GetAssets worker record {} HRESULT={:08X}, output={}, "
            "guards={}, source={}, destroyed={}, indices={}, tiled-bytes={}",
            record, result, output_ok, guards, source, destroyed, indices,
            texture_check.compared));
    if (!output_ok || !guards || !source || !destroyed ||
        texture_check.failures)
      break;
    ++assets;
  }
  for (size_t i = 0; i < 5; ++i)
    if (g_mesh_callbacks[i]) d->SetFunction(addresses[i], g_mesh_callbacks[i]);
  std::memcpy(m->TranslateVirtual(0x81A975CC), saved.data(), saved.size());
  for (auto p : owned) m->SystemHeapFree(p);
  Check(assets == 3 && seeks == 1 && abi == 0 && g_mesh_callback_abi == 0,
        fmt::format("ldi-assets: {} workers, {} stream seek/reset case, ABI "
                    "breaks={}/{}; callbacks restored",
                    assets, seeks, abi, g_mesh_callback_abi));
}

// Only the new aggregate/lifecycle route, with original compressed assets.
void RunAggregateAssets(const x360n::avatar::AvatarToc& toc, uint32_t callbacks) {
  using namespace x360n::avatar;
  auto* rt = rex::Runtime::instance();
  auto* m = rt->memory();
  auto* d = rt->function_dispatcher();
  auto* t = rex::runtime::ThreadState::Get();
  uint32_t abi = 0, calls = 0;
  const auto saved_protocol = MeshBE(m->TranslateVirtual<const uint8_t*>(0x81B4B238));
  std::array<uint8_t, 20> saved_callbacks{};
  std::memcpy(saved_callbacks.data(), m->TranslateVirtual(0x81A975CC), 20);
  bool corrupt_second = false;
  uint32_t decode_calls = 0;
  AvatarAotCall call = [&](uint32_t fn, std::initializer_list<uint64_t> args) {
    ++calls;
    uint8_t* corrupt = nullptr;
    uint8_t original = 0;
    if (fn == 0x81966FC8 && ++decode_calls == 2 && corrupt_second) {
      const auto selection = uint32_t(*(args.begin() + 1));
      const auto source = MeshBE(m->TranslateVirtual<const uint8_t*>(selection + 400));
      // Corrupt only the private copy's GUID, after one asset already populated
      // the aggregate. The actual AOT entry must reject it; no injected HRESULT.
      corrupt = m->TranslateVirtual<uint8_t*>(source + 8);
      original = *corrupt;
      *corrupt ^= 0x80;
    }
    auto hr = uint32_t(CallQuiet(d, t, fn, args, &abi));
    if (corrupt) *corrupt = original;
    if (fn == 0x81967628 || fn == 0x81966FC8 || fn == 0x819672F8)
      REXLOG_INFO("xam-aot-probe: aggregate: fn={:08X} HRESULT={:08X}", fn, hr);
    return hr;
  };
  std::vector<AssetSelection> selection;
  for (size_t index : {size_t(72), size_t(0)}) {
    AssetSelection s;
    s.id = {toc.record(index).field_00, uint16_t(index), 0,
            toc.Opaque(index)[0], AssetStampFor(toc)};
    for (size_t i = 0; i < 3; ++i)
      s.materials[i] = {uint32_t(22 + i), {0.5f, 0.5f, 0.5f, 1.0f}};
    selection.push_back(s);
  }
  auto make = [&](AotAssetsStats& stats, uint32_t protocol = 4) {
    decode_calls = 0;
    return MakeAotAssetsBackend(*m, call, toc, selection, protocol, 1,
                                callbacks, stats);
  };
  auto drain = [&](AssetsRequest& request) {
    bool unpublished = true;
    for (int i = 0; i < 32 && request.status() == kAssetsPending; ++i) {
      unpublished &= request.result().result == 0;
      request.Step();
    }
    Check(unpublished && request.status() != kAssetsPending,
          "aggregate: partial output stays private; bounded completion");
  };
  AotAssetsStats success;
  {
    AssetsRequest request(make(success));
    drain(request);
    auto result = request.result();
    bool aggregate = request.status() == 0 && result.result && result.gpu;
    uint32_t count = 0, bones = 0;
    if (aggregate) {
      const auto* root = m->TranslateVirtual<const uint8_t*>(result.result);
      count = MeshBE(root + 8);
      bones = MeshBE(m->TranslateVirtual<const uint8_t*>(MeshBE(root)));
      aggregate &= count == 2 && bones > 0 && success.decoded == 2 &&
                   success.finalized == 1;
      const auto refs = MeshBE(root + 12), descriptors = MeshBE(root + 16);
      uint32_t last_cpu_end = 0, last_gpu_end = 0;
      for (uint32_t i = 0; aggregate && i < 2; ++i) {
        const auto id = selection[i].id.ToBytes();
        const auto* ref = m->TranslateVirtual<const uint8_t*>(refs + i * 32);
        const auto* desc =
            m->TranslateVirtual<const uint8_t*>(descriptors + i * 52);
        const auto cpu = MeshBE(desc + 28), gpu = MeshBE(desc + 32);
        aggregate &= !std::memcmp(ref, id.data(), 16) && cpu >= last_cpu_end &&
                     gpu >= last_gpu_end;
        last_cpu_end = cpu + MeshBE(desc);
        last_gpu_end = gpu + MeshBE(desc + 4);
      }
    }
    Check(aggregate,
          fmt::format("aggregate: HRESULT={:08X}, assets={}, bones={}, "
                      "decoded={}, field4={}, finalized={}",
                      request.status(), count, bones, success.decoded,
                      success.applied, success.finalized));
    const auto before = calls;
    request.Step();
    request.Cancel();
    Check(calls == before && request.result().result == result.result,
          "aggregate: terminal polling/cancel do not rerun or release the "
          "result");
    request.Release();
    request.Release();
    Check(!request.result().result && success.allocations == success.frees &&
              success.guards,
          "aggregate: explicit release frees all buffers exactly once");
  }
  for (int steps : {0, 2, 3}) {
    AotAssetsStats stats;
    AssetsRequest request(make(stats));
    for (int i = 0; i < steps; ++i) request.Step();
    const bool pending = request.status() == kAssetsPending;
    request.Cancel();
    const auto before = calls;
    request.Cancel();
    request.Step();
    Check(pending && request.status() == kAssetsAbort &&
              !request.result().result && calls == before &&
              stats.allocations == stats.frees && stats.guards &&
              stats.finalized == 0,
          fmt::format("aggregate: cancel after {} steps discards partial "
                      "result ({} assets)",
                      steps, stats.decoded));
  }
  AotAssetsStats failed;
  {
    corrupt_second = true;
    AssetsRequest request(make(failed));
    drain(request);
    const auto before = calls;
    request.Step();
    Check(request.state() == AssetsRequest::State::Failed &&
              request.status() == kAssetsFailure && failed.decoded == 2 &&
              !request.result().result && failed.allocations == failed.frees &&
              calls == before && failed.finalized == 0 && failed.guards,
          "aggregate: real AOT error on second asset discards first asset and "
          "preserves HRESULT");
    corrupt_second = false;
  }
  AotAssetsStats unsupported;
  {
    AssetsRequest request(make(unsupported, 2));
    const auto before = calls;
    request.Step();
    Check(request.status() == kAssetsInvalid && !unsupported.allocations &&
              calls == before,
          "aggregate: unsupported protocol rejected before guest allocation or "
          "execution");
  }
  AotAssetsStats abandoned;
  {
    AssetsRequest request(make(abandoned));
    request.Step();
    request.Step();
  }
  Check(abandoned.decoded == 1 && abandoned.allocations == abandoned.frees &&
            abandoned.guards,
        "aggregate: destruction of pending request releases private output");
  Check(abi == 0 &&
            MeshBE(m->TranslateVirtual<const uint8_t*>(0x81B4B238)) ==
                saved_protocol &&
            !std::memcmp(saved_callbacks.data(),
                         m->TranslateVirtual(0x81A975CC), 20),
        fmt::format(
            "aggregate: ABI breaks={}; protocol and callback table restored",
            abi));
}

void RunAvatarServiceProbe(const x360n::avatar::AvatarToc& toc,
                           const std::string& catalog_path, uint32_t callbacks) {
  using namespace x360n::avatar;
  auto* rt = rex::Runtime::instance();
  auto* thread = rex::runtime::ThreadState::Get();
  uint32_t abi = 0;
  AvatarAotCall call = [&](uint32_t fn, std::initializer_list<uint64_t> args) {
    const auto hr = uint32_t(CallQuiet(rt->function_dispatcher(), thread, fn, args, &abi));
    REXLOG_INFO("avatar-service: {:08X} -> {:08X}", fn, hr);
    return hr;
  };
  auto device = std::make_unique<rex::filesystem::HostPathDevice>(
      "\\Device\\X360nAvatarCatalog17559", std::filesystem::path(catalog_path).parent_path(), true);
  if (!device->Initialize() || !rt->kernel_state()->file_system()->RegisterDevice(std::move(device))) {
    Check(false, "avatar-service: mount read-only catalog");
    return;
  }
  AotAvatarCatalog catalog(*rt->memory(), call);
  auto hr = catalog.Open("\\Device\\X360nAvatarCatalog17559\\AvatarAssetPack.toc");
  Check(hr == 0, fmt::format("avatar-service: open original generator HRESULT={:08X}", hr));
  if (hr) return;
  std::vector<std::array<uint8_t, kMetadataSize>> descriptions;
  hr = catalog.Generate(3, 4, descriptions);
  Check(hr == 0 && descriptions.size() == 4,
        fmt::format("avatar-service: random metadata HRESULT={:08X}, count={}", hr, descriptions.size()));
  // IDs have three layouts: modifiers, body/head refs and clothing refs.
  for (size_t i = 0; i < descriptions.size(); ++i) {
    const auto& md = descriptions[i];
    std::vector<size_t> slots{12, 28, 44, 288, 320};
    for (size_t n = 0; n < 6; ++n) slots.push_back(60 + n * 32);
    for (size_t n = 0; n < 13; ++n) slots.push_back(352 + n * 32);
    size_t resolved = 0;
    for (auto offset : slots) {
      const auto id = AssetId::FromBytes(std::span<const uint8_t, 16>(md.data() + offset, 16));
      if (id.IsZero()) continue;
      std::string error;
      auto index = ResolveAsset(toc, id, &error);
      Check(bool(index), fmt::format("avatar-service: avatar {} slot {} record {} category {:X}: {}",
                                    i, offset, id.index, id.category, error));
      resolved += bool(index);
    }
    REXLOG_INFO("avatar-service: avatar {} body {}: {} resolved assets", i, BodyType(md), resolved);
  }
  for (const auto& md : descriptions) {
    AotAssetsStats stats;
    auto backend = MakeAotMetadataAssetsBackend(*rt->memory(), call, toc, md, 0xFFF, callbacks, stats);
    AssetsRequest request(std::move(backend));
    for (int n = 0; n < 80 && request.status() == kAssetsPending; ++n) request.Step();
    auto output = request.result();
    uint32_t count = output.result ? MeshBE(rt->memory()->TranslateVirtual<const uint8_t*>(output.result + 8)) : 0;
    Check(request.status() == 0 && count >= 2 && stats.applied > 0,
          fmt::format("avatar-service: full metadata HRESULT={:08X}, meshes={}, decoded={}, modifiers={}, finalized={}",
                      request.status(), count, stats.decoded, stats.applied, stats.finalized));
  }
  Check(abi == 0, "avatar-service: original generator ABI preserved");
}


void Run(uint32_t game_callbacks) {
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
    Check(
        false,
        "--x360n_xam17559_root and --x360n_avatar_catalog_17559 are required");
    return;
  }

  // Mount and load. Name lookups of "xam.xex" still reach the SDK's kernel
  // module first (KernelState::GetModule searches kernel modules before user
  // modules).
  auto device = std::make_unique<rex::filesystem::HostPathDevice>(
      "\\Device\\X360nSystem17559", root, true);
  const bool mounted = device->Initialize() &&
                       ks->file_system()->RegisterDevice(std::move(device)) &&
                       ks->file_system()->RegisterSymbolicLink(
                           "x360nsys:", "\\Device\\X360nSystem17559");
  Check(mounted, "mount " + root + " as x360nsys: (read-only)");
  if (!mounted) return;
  auto module = ks->LoadUserModule("x360nsys:\\xam.xex", false);
  Check(bool(module), "load x360nsys:\\xam.xex without its entry point");
  if (!module) return;
  const uint32_t base = module->xex_module()->base_address();
  const uint32_t size = module->xex_module()->image_size();
  g_xam_hmodule = module->hmodule_ptr();
  Check(base == kXamBase && size == kXamSize,
        fmt::format("load: image 0x{:08X}+0x{:X} (expected 0x{:08X}+0x{:X})",
                    base, size, kXamBase, kXamSize));

  if (REXCVAR_GET(motocross_avatar_service_probe) || REXCVAR_GET(motocross_xam_aot_probe_aggregate) || REXCVAR_GET(motocross_xam_aot_probe_meshes) || REXCVAR_GET(motocross_xam_aot_probe_ldi)) {
    std::string error;
    auto toc = x360n::avatar::AvatarToc::Parse(Slurp(catalog_path), &error);
    if (!toc) { Check(false, "meshes: catalog: " + error); return; }
    if (REXCVAR_GET(motocross_avatar_service_probe)) RunAvatarServiceProbe(*toc, catalog_path, game_callbacks);
    else if (REXCVAR_GET(motocross_xam_aot_probe_aggregate)) RunAggregateAssets(*toc, game_callbacks);
    else if (REXCVAR_GET(motocross_xam_aot_probe_ldi)) RunLdiAssets(*toc, game_callbacks);
    else RunMeshes(*toc, game_callbacks);
    return;
  }
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
  // The probe mounts x360nsys: itself and owns the module; the game's later
  // XamAvatarInitialize then fails in this diagnostic mode, as before.
  REXCVAR_SET(x360n_avatar_service, false);
  REXLOG_INFO("xam-aot-probe: avatar service disabled for this diagnostic run");
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
  Run(uint32_t(saved.r6.u64));
  if (REXCVAR_GET(motocross_xam_aot_probe_initialize) &&
      !REXCVAR_GET(motocross_avatar_service_probe) && !REXCVAR_GET(motocross_xam_aot_probe_aggregate) && !REXCVAR_GET(motocross_xam_aot_probe_meshes) && !REXCVAR_GET(motocross_xam_aot_probe_ldi)) {
    *thread->context() = saved;
    RunOriginalInitialize(saved.r3, saved.r4, saved.r5, saved.r6, uint32_t(saved.r8.u64));
  }
  *thread->context() = saved;
  thread->context()->fpscr.restoreGuestBits(saved.fpscr.csr);
  REXLOG_INFO("xam-aot-probe: done, {} failure(s); game context restored", g_failures);
}

}  // namespace motocross
