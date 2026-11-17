#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cstdint>
#include "../../external/rexglue-sdk/thirdparty/excrypt/src/fp61.hpp"

TEST_CASE("wide crypto multiplication preserves carries and full-width products", "[content-crypto]") {
  struct Product { uint64_t a, b, high, low; };
  constexpr std::array cases{
      Product{0,0,0,0},
      Product{0,0xFFFFFFFFFFFFFFFFull,0,0},
      Product{1,0xFFFFFFFFFFFFFFFFull,0,0xFFFFFFFFFFFFFFFFull},
      Product{0xFFFFFFFFFFFFFFFFull,0xFFFFFFFFFFFFFFFFull,0xFFFFFFFFFFFFFFFEull,1},
      Product{0x8000000000000000ull,0x8000000000000000ull,0x4000000000000000ull,0},
      Product{0x100000000ull,0x100000000ull,1,0},
      Product{0xFFFFFFFFull,0xFFFFFFFFull,0,0xFFFFFFFE00000001ull},
      Product{0xFFFFFFFF00000001ull,0xFFFFFFFF00000001ull,0xFFFFFFFE00000002ull,0xFFFFFFFE00000001ull},
      Product{0x0123456789ABCDEFull,0xFEDCBA9876543210ull,0x0121FA00AD77D742ull,0x2236D88FE5618CF0ull}};
  for (const auto& expected : cases) {
    uint64_t high=0,low=0;
    CAT_MUL128(high,low,expected.a,expected.b);
    CHECK(high==expected.high);
    CHECK(low==expected.low);
    low=Emulate64x64to128(high,expected.a,expected.b);
    CHECK(high==expected.high);
    CHECK(low==expected.low);
  }
}

#if defined(__SIZEOF_INT128__)
TEST_CASE("wide crypto multiplication agrees with exact integer arithmetic", "[content-crypto]") {
  uint64_t seed=0x1972F5B47AC03D61ull;
  auto next=[&] {seed=seed*6364136223846793005ull+1;return seed;};
  for (unsigned i=0;i<4096;++i) {
    const auto a=next(),b=next();
    const unsigned __int128 expected=static_cast<unsigned __int128>(a)*b;
    uint64_t high=0,low=0;
    CAT_MUL128(high,low,a,b);
    CHECK(high==uint64_t(expected>>64));
    CHECK(low==uint64_t(expected));
    low=Emulate64x64to128(high,a,b);
    CHECK(high==uint64_t(expected>>64));
    CHECK(low==uint64_t(expected));
  }
}
#endif
