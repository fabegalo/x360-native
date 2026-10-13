#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <rex/ppc/context.h>

// Same typed, overlapping guest-memory accesses used by the recompiler.
// Separate pointers prevent the caller's known storage from hiding TBAA bugs.
__declspec(noinline) uint32_t alias_word_half(uint32_t* word, uint16_t* half) {
    *word = 0xffffffffu;
    *half = 0;
    return *word;
}

// Constants are intentional: without strict FP the optimizer folds this using
// round-to-nearest even after the guest FPSCR has selected upward rounding.
__declspec(noinline) double rounded_sum() { return 1.0 + 0x1p-53; }
__declspec(noinline) double cancellation(double a, double b) { return a - b; }
__declspec(noinline) double divide(double a, double b) { return a / b; }

int main() {
    unsigned failures = 0;
    auto check = [&](bool ok, const char* name) {
        std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
        failures += !ok;
    };
    alignas(16) uint32_t word;
    check(alias_word_half(&word, reinterpret_cast<uint16_t*>(&word)) == 0xffff0000u,
          "overlapping word/halfword stores");
    PPCFPSCRRegister fpscr{};
    const auto saved = fpscr.getcsr();
    fpscr.loadFromHost();
    fpscr.disableFlushModeUnconditional();
    fpscr.storeFromGuest(rex::ppc::kRoundNearest);
    check(rounded_sum() == 1.0, "nearest halfway");
    fpscr.storeFromGuest(rex::ppc::kRoundUp);
    check(rounded_sum() == std::bit_cast<double>(uint64_t{0x3ff0000000000001}),
          "FPSCR upward halfway");
    fpscr.storeFromGuest(rex::ppc::kRoundDown);
    check(std::signbit(cancellation(1.0, 1.0)), "FPSCR downward signed zero");
    fpscr.storeFromGuest(rex::ppc::kRoundNearest);
    check(std::isinf(divide(1.0, 0.0)), "division infinity");
    check(std::isnan(divide(0.0, 0.0)), "division NaN");
    check(divide(0x1p-1022, 2.0) == 0x1p-1023, "gradual underflow");
    fpscr.setcsr(saved);
    return failures ? 1 : 0;
}
