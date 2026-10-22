// sdkcheck.Vmsum: vmsum3fp128/vmsum4fp128 executed through recompiled PPC
// (asm/vmsum.s -> rexglue recompile-tests -> clang++).
//
// Reference semantics: Xenia DOT_PRODUCT_3/4 + EmitDotProductResult. When a
// float32 dot product of finite inputs overflows, the result is QNaN
// 0x7FC00000 in every lane; the products are exact in double and summed as
// (x+z)+y or (x+z)+(y+w). Infinity/NaN coming from an input passes through.
// Finite results keep the host float32 dot product (dpps) bit for bit.
#include <rex/ppc/context.h>
#include <simde/x86/sse4.1.h>
#include "ppc_test_decls.h"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>

namespace {

using Guest = std::array<uint32_t, 4>;  // guest element order x, y, z, w
using Fn = decltype(&vmsum_82010000);

constexpr uint32_t kQNaN = 0x7FC00000;
constexpr uint32_t kFltMax = 0x7F7FFFFF, kNegFltMax = 0xFF7FFFFF;
constexpr uint32_t kInf = 0x7F800000, kNegInf = 0xFF800000;
constexpr uint32_t kOne = 0x3F800000;

// Guest element i lives in host lane 3 - i (vector element reversal).
void put(PPCVRegister& v, const Guest& g) {
  for (unsigned i = 0; i < 4; ++i) v.u32[3 - i] = g[i];
}
Guest get(const PPCVRegister& v) {
  return {v.u32[3], v.u32[2], v.u32[1], v.u32[0]};
}
float f(uint32_t bits) { return std::bit_cast<float>(bits); }
bool is_nan(uint32_t bits) { return (bits & 0x7FFFFFFF) > 0x7F800000; }
bool finite(uint32_t bits) { return (bits & 0x7F800000) != 0x7F800000; }

// Pre-fix result: the host dpps the codegen emitted before, under the same
// flush-to-zero/denormals-are-zero state as generated VMX code.
uint32_t host_dpps(const Guest& a, const Guest& b, bool four) {
  PPCVRegister va{}, vb{};
  put(va, a);
  put(vb, b);
  simde__m128 r = four ? simde_mm_dp_ps(simde_mm_load_ps(va.f32), simde_mm_load_ps(vb.f32), 0xFF)
                       : simde_mm_dp_ps(simde_mm_load_ps(va.f32), simde_mm_load_ps(vb.f32), 0xEF);
  return std::bit_cast<uint32_t>(simde_mm_cvtss_f32(r));
}

// Independent scalar model of the reference semantics.
uint32_t model(const Guest& a, const Guest& b, bool four) {
  const uint32_t fast = host_dpps(a, b, four);
  if (finite(fast)) return fast;
  double p[4];
  for (unsigned i = 0; i < 4; ++i) p[i] = double(f(a[i])) * double(f(b[i]));
  const double sum = four ? (p[0] + p[2]) + (p[1] + p[3]) : (p[0] + p[2]) + p[1];
  if (!std::isfinite(sum)) return fast;  // inf/NaN came from an input
  const float r = float(sum);
  return std::isinf(r) ? kQNaN : std::bit_cast<uint32_t>(r);
}

struct Runner {
  PPCContext c{};
  uint8_t memory[16]{};
  Fn fns[2][4] = {{vmsum_82010000, vmsum_82010008, vmsum_82010010, vmsum_82010018},
                  {vmsum_82010020, vmsum_82010028, vmsum_82010030, vmsum_82010038}};
  unsigned checks = 0, failures = 0;

  // variant 0: vD=v5,vA=v3,vB=v4; 1: vD=vA=v3; 2: vD=vB=v4; 3: v3,v3,v3.
  Guest run(bool four, unsigned variant, const Guest& a, const Guest& b) {
    put(c.v3, a);
    put(c.v4, variant == 3 ? a : b);
    put(c.v5, {0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF});
    fns[four][variant](c, memory);
    if (variant == 0 && (get(c.v3) != a || get(c.v4) != b)) {
      std::printf("FAIL %s variant 0 changed a source register\n", four ? "vmsum4" : "vmsum3");
      ++failures;
    }
    return get(variant == 0 ? c.v5 : (variant == 2 ? c.v4 : c.v3));
  }

  void expect(const char* name, bool four, const Guest& a, const Guest& b, uint32_t want) {
    for (unsigned variant = 0; variant < 4; ++variant) {
      const Guest& bb = variant == 3 ? a : b;
      const Guest got = run(four, variant, a, bb);
      for (unsigned i = 0; i < 4; ++i) {
        ++checks;
        if (got[i] != want) {
          std::printf("FAIL %s %s variant %u lane %u: got %08X want %08X\n", name,
                      four ? "vmsum4fp128" : "vmsum3fp128", variant, i, got[i], want);
          ++failures;
          break;
        }
      }
      if (variant == 2) break;  // variant 3 squares a; only used when a == b
    }
  }
  void expect_same(const char* name, bool four, const Guest& a, uint32_t want) {
    const Guest got = run(four, 3, a, a);
    for (unsigned i = 0; i < 4; ++i, ++checks)
      if (got[i] != want) {
        std::printf("FAIL %s same-register lane %u: got %08X want %08X\n", name, i, got[i], want);
        ++failures;
        break;
      }
  }
};

uint32_t random_lane(std::mt19937& rng) {
  switch (rng() % 10) {
    case 0: return 0;
    case 1: return rng() & 0x807FFFFF;                         // denormal
    case 2: return (rng() & 0x80000000) | kFltMax;
    case 3: return (rng() & 0x80000000) | kInf;
    case 4: return (rng() & 0x80000000) | 0x7F800000 | (rng() & 0x7FFFFF) | 1;  // NaN
    case 5:
    case 6: return (rng() & 0x807FFFFF) | ((0xE0 + rng() % 0x1F) << 23);  // 2^97..2^127
    default: return (rng() & 0x807FFFFF) | ((0x6B + rng() % 0x28) << 23);  // 2^-20..2^19
  }
}

int bench(Runner& r) {
  const Guest a{0x3f800000, 0x3fc00000, 0x3f8ccccd, 0x3ff33333};
  const Guest b{0x40000000, 0x40700000, 0x4013d70a, 0x40b051eb};
  constexpr unsigned kIters = 50'000'000;
  for (bool four : {false, true}) {
    put(r.c.v3, a);
    put(r.c.v4, b);
    auto fn = r.fns[four][0];
    double best = 1e30;
    for (unsigned rep = 0; rep < 5; ++rep) {
      const auto t0 = std::chrono::steady_clock::now();
      for (unsigned n = 0; n < kIters; ++n) fn(r.c, r.memory);
      const auto t1 = std::chrono::steady_clock::now();
      best = std::min(best, std::chrono::duration<double, std::nano>(t1 - t0).count() / kIters);
    }
    std::printf("BENCH %s finite path: best of 5 x %u calls = %.3f ns/call (v5 lane %08X)\n",
                four ? "vmsum4fp128" : "vmsum3fp128", kIters, best, r.c.v5.u32[0]);
  }
  // test_vmsum_bench: dependent chain of 3 vmsum3fp128 + 3 vmsum4fp128, all finite.
  put(r.c.v3, a);
  put(r.c.v4, b);
  double best = 1e30;
  for (unsigned rep = 0; rep < 5; ++rep) {
    const auto t0 = std::chrono::steady_clock::now();
    for (unsigned n = 0; n < kIters / 8; ++n) vmsum_82010040(r.c, r.memory);
    const auto t1 = std::chrono::steady_clock::now();
    best = std::min(best, std::chrono::duration<double, std::nano>(t1 - t0).count() / (kIters / 8) / 6);
  }
  std::printf("BENCH 6-instruction function: best of 5 x %u calls = %.3f ns/instruction "
              "(v2 %08X, v7 %08X)\n",
              kIters / 8, best, r.c.v2.u32[0], r.c.v7.u32[0]);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  Runner r;
  r.c.fpscr.loadFromHost();
  r.c.fpscr.enableFlushModeUnconditional();  // the state generated VMX code selects
  if (argc > 1 && std::strcmp(argv[1], "--bench") == 0) return bench(r);

  for (bool four : {false, true}) {
    const uint32_t w = four ? 0 : kFltMax;  // vmsum3 ignores w
    // Finite results are unchanged (SDK tests/ppc instr_vmsum*fp128_1 values).
    r.expect("finite", four, {0x3f800000, 0x3fc00000, 0x3f8ccccd, four ? 0x3ff33333u : 0x01020304u},
             {0x40000000, 0x40700000, 0x4013d70a, four ? 0x40b051ebu : 0x01020304u},
             four ? 0x41A5147B : 0x4122A7F0);
    // Finite inputs that overflow, either sign: QNaN.
    r.expect("positive overflow", four, {kFltMax, kFltMax, kFltMax, w}, {kOne, kOne, kOne, kOne}, kQNaN);
    r.expect("negative overflow", four, {kNegFltMax, kNegFltMax, 0, w}, {kOne, kOne, kOne, kOne}, kQNaN);
    r.expect("mixed-sign overflow", four, {kNegFltMax, kFltMax, kNegFltMax, w},
             {kFltMax, kNegFltMax, kFltMax, kOne}, kQNaN);
    r.expect_same("squared overflow", four, {kNegFltMax, kNegFltMax, kNegFltMax, four ? kNegFltMax : 0}, kQNaN);
    // Infinity / NaN from an input is not converted.
    r.expect("infinite input", four, {kInf, kOne, kOne, 0}, {kOne, kOne, kOne, 0}, kInf);
    r.expect("negative infinite input", four, {kOne, kNegInf, kOne, 0}, {kOne, kOne, kOne, 0}, kNegInf);
    r.expect("infinity plus overflow", four, {kInf, kFltMax, 0, 0}, {kOne, kFltMax, 0, 0}, kInf);
    {
      const Guest a{0x7FC12345, kOne, kOne, 0}, b{kOne, kOne, kOne, 0};
      const uint32_t want = host_dpps(a, b, four);
      if (!is_nan(want)) {
        std::puts("FAIL host dpps did not propagate NaN");
        ++r.failures;
      }
      r.expect("NaN input", four, a, b, want);
      const Guest ia{kInf, kOne, kOne, 0}, ib{0, kOne, kOne, 0};
      r.expect("infinity times zero", four, ia, ib, host_dpps(ia, ib, four));
    }
    // Products that overflow float32 but cancel in the exact sum.
    // x*x' = 2^128, z*z' = -2^128, y*y' = 6.
    r.expect("cancelling products", four, {0x7E800000, 0x40000000, 0xFE800000, w},
             {0x40800000, 0x40400000, 0x40800000, 0}, 0x40C00000);
    // 2^129 - 3*2^127 = 2^127: finite although both products overflow.
    r.expect("finite after overflowing products", four, {0x7F000000, 0, 0xFF000000, w},
             {0x40800000, 0, 0x40400000, 0}, 0x7F000000);
    // Near FLT_MAX: a quarter ulp rounds down, half an ulp (a tie) rounds to
    // even, which is 2^128, so it overflows.
    r.expect("FLT_MAX", four, {kFltMax, 0, 0, w}, {kOne, kOne, kOne, 0}, kFltMax);
    r.expect("FLT_MAX + quarter ulp", four, {kFltMax, 0x72800000, 0, w}, {kOne, kOne, 0, 0}, kFltMax);
    r.expect("-FLT_MAX - quarter ulp", four, {kNegFltMax, 0xF2800000, 0, w}, {kOne, kOne, 0, 0},
             kNegFltMax);
    r.expect("FLT_MAX + half ulp", four, {kFltMax, 0x73000000, 0, w}, {kOne, kOne, 0, 0}, kQNaN);
  }
  // Lane selection: vmsum3 ignores w, vmsum4 includes it.
  r.expect("vmsum3 w infinite", false, {kOne, 0x40000000, 0x40400000, kInf}, {kOne, kOne, kOne, kOne},
           0x40C00000);
  r.expect("vmsum3 w NaN", false, {kOne, 0x40000000, 0x40400000, 0x7FC00001}, {kOne, kOne, kOne, kOne},
           0x40C00000);
  r.expect("vmsum4 w overflow", true, {0, 0, 0, kFltMax}, {0, 0, 0, 0x40000000}, kQNaN);
  r.expect("vmsum4 w infinite", true, {kOne, kOne, kOne, kInf}, {kOne, kOne, kOne, kOne}, kInf);

  // Randomized lanes across classes, checked against the scalar model.
  std::mt19937 rng(0x4D5309C9);
  for (unsigned n = 0; n < 20000; ++n) {
    Guest a, b;
    for (unsigned i = 0; i < 4; ++i) {
      a[i] = random_lane(rng);
      b[i] = random_lane(rng);
    }
    for (bool four : {false, true}) {
      const uint32_t want = model(a, b, four);
      for (unsigned variant = 0; variant < 4; ++variant) {
        const Guest& bb = variant == 3 ? a : b;
        const uint32_t w = variant == 3 ? model(a, a, four) : want;
        const Guest got = r.run(four, variant, a, bb);
        ++r.checks;
        if (got != Guest{w, w, w, w}) {
          std::printf("FAIL random %u %s variant %u: a=%08X,%08X,%08X,%08X b=%08X,%08X,%08X,%08X "
                      "got %08X want %08X\n",
                      n, four ? "vmsum4fp128" : "vmsum3fp128", variant, a[0], a[1], a[2], a[3],
                      bb[0], bb[1], bb[2], bb[3], got[0], w);
          if (++r.failures > 20) return 1;
        }
      }
    }
  }
  if (r.failures) {
    std::printf("FAIL sdkcheck.Vmsum: %u failures in %u checks\n", r.failures, r.checks);
    return 1;
  }
  std::printf("PASS sdkcheck.Vmsum: %u checks through generated PPC\n", r.checks);
  return 0;
}
