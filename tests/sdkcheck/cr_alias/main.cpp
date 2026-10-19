#include <rex/ppc/context.h>
#include "ppc_test_decls.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <random>

int main() {
  PPCContext ctx{}; uint8_t memory[16]{};
  std::array<PPCCRRegister*, 8> cr{&ctx.cr0, &ctx.cr1, &ctx.cr2, &ctx.cr3,
                                  &ctx.cr4, &ctx.cr5, &ctx.cr6, &ctx.cr7};
  std::mt19937 random(0x1888);
  uint32_t checks = 0;
  for (unsigned n = 0; n < 4096; ++n) {
    const uint32_t input = n < 32 ? 1u << n : random();
    for (unsigned variant = 0; variant < 4; ++variant) {
      for (unsigned i = 0; i < 8; ++i) cr[i]->set_raw(input >> (28 - 4 * i));
      const unsigned dest[] = {7, 2, 28, 31}, source[] = {31, 2, 0, 7};
      const uint32_t bit = ((input >> (31 - source[variant])) & 1) ^ (variant & 1);
      const uint32_t expected = (input & ~(1u << (31 - dest[variant]))) |
                                 (bit << (31 - dest[variant]));
      switch (variant) {
        case 0: cr_alias_82010000(ctx, memory); break;
        case 1: cr_alias_82010008(ctx, memory); break;
        case 2: cr_alias_82010010(ctx, memory); break;
        case 3: cr_alias_82010018(ctx, memory); break;
      }
      uint32_t got = 0;
      for (unsigned i = 0; i < 8; ++i) got |= cr[i]->raw() << (28 - 4 * i);
      if (got != expected) {
        std::printf("FAIL alias=%u input=%08X got=%08X expected=%08X\n", variant, input, got, expected);
        return 1;
      }
      ++checks;
    }
  }
  std::printf("PASS sdkcheck.CrAliases: %u generated PPC checks\n", checks);
}
