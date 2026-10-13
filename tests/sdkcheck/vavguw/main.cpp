#include <rex/ppc/context.h>
#include "ppc_test_decls.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <random>
int main() {
  PPCContext c{};
  uint8_t memory[16]{};
  const std::array<uint32_t,8> edge={0,1,2,3,0x7FFFFFFF,0x80000000,0xFFFFFFFE,0xFFFFFFFF};
  std::mt19937 rng(0x81978208);
  uint32_t checks=0;
  try {
    for(unsigned n=0;n<4160;++n) {
      std::array<uint32_t,4> a,b;
      for(unsigned i=0;i<4;++i) {
        a[i]=n<64?edge[(n/8+i)%8]:rng();
        b[i]=n<64?edge[(n+i)%8]:rng();
      }
      for(unsigned variant=0;variant<4;++variant) {
        for(unsigned i=0;i<4;++i){c.v3.u32[i]=a[i];c.v4.u32[i]=b[i];}
        switch(variant){
          case 0: vavguw_82010000(c,memory); break;
          case 1: vavguw_82010008(c,memory); break;
          case 2: vavguw_82010010(c,memory); break;
          case 3: vavguw_82010018(c,memory); break;
        }
        for(unsigned i=0;i<4;++i) {
          auto other=variant==3?a[i]:b[i];
          // Independent overflow-free identity for ceil((a+b)/2).
          auto expected=(a[i]|other)-((a[i]^other)>>1);
          auto got=variant==0?c.v5.u32[i]:(variant==2?c.v4.u32[i]:c.v3.u32[i]);
          if(got!=expected){std::printf("FAIL variant=%u lane=%u\n",variant,i);return 1;}
          if(variant==0 && (c.v3.u32[i]!=a[i]||c.v4.u32[i]!=b[i])) return 2;
          ++checks;
        }
      }
    }
  } catch (...) { std::puts("FAIL: instruction raised an exception");return 3; }
  std::printf("PASS sdkcheck.Vavguw: %u lane checks through generated PPC\n",checks);
}
