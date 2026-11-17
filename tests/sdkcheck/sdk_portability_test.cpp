#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cstdint>
#include "../../external/rexglue-sdk/thirdparty/excrypt/src/fp61.hpp"
#include "../../external/rexglue-sdk/thirdparty/simde/simde/x86/ssse3.h"
#if defined(_WIN32)
#include "../../external/rexglue-sdk/src/base/exception_context_win.h"

TEST_CASE("Windows fault contexts preserve native registers and sparse resume edits", "[windows-context]") {
  using namespace rex::platform;
  CONTEXT native{};
  std::memset(&native,0xA5,sizeof(native));
  const auto original=native;
  auto context=detail::CaptureWindowsContext(native);
  Exception exception;
  exception.InitializeIllegalInstruction(&context);
#if REX_ARCH_AMD64
  CHECK(exception.GetPC()==original.Rip);
  CHECK(context.eflags==original.EFlags);
  CHECK(std::memcmp(context.int_registers,&original.Rax,sizeof(context.int_registers))==0);
  CHECK(std::memcmp(context.xmm_registers,&original.Xmm0,sizeof(context.xmm_registers))==0);
  exception.ModifyIntRegister(0)=0x123456789ABCDEFull;
  exception.ModifyIntRegister(15)=0xFEDCBA9876543210ull;
  std::memset(&exception.ModifyXMMRegister(15),0x3C,sizeof(VectorRegister));
#elif REX_ARCH_ARM64
  CHECK(exception.GetPC()==original.Pc);
  CHECK(context.sp==original.Sp);
  CHECK(context.pstate==original.Cpsr);
  CHECK(context.fpsr==original.Fpsr);
  CHECK(context.fpcr==original.Fpcr);
  CHECK(std::memcmp(context.x,original.X,sizeof(context.x))==0);
  CHECK(std::memcmp(context.v,original.V,sizeof(context.v))==0);
  exception.ModifyXRegister(0)=0x123456789ABCDEFull;
  exception.ModifyXRegister(30)=0xFEDCBA9876543210ull;
  context.sp=0x1020304050607080ull;
  context.pstate=0x60000000;
  context.fpsr=0x80000001;
  context.fpcr=0x01000000;
  std::memset(&exception.ModifyVRegister(31),0x3C,sizeof(VectorRegister));
#endif
  exception.SetResumePC(0x1122334455667788ull);
  detail::ApplyWindowsContext(native,exception);
#if REX_ARCH_AMD64
  CHECK(native.Rip==0x1122334455667788ull);
  CHECK(native.Rax==0x123456789ABCDEFull);
  CHECK(native.R15==0xFEDCBA9876543210ull);
  CHECK(native.Rcx==original.Rcx);
  CHECK(native.EFlags==original.EFlags);
  CHECK(std::memcmp(&native.Xmm0,&original.Xmm0,sizeof(VectorRegister)*15)==0);
  CHECK(std::memcmp(&native.Xmm15,&context.xmm_registers[15],sizeof(VectorRegister))==0);
#elif REX_ARCH_ARM64
  CHECK(native.Pc==0x1122334455667788ull);
  CHECK(native.X[0]==0x123456789ABCDEFull);
  CHECK(native.X[30]==0xFEDCBA9876543210ull);
  CHECK(std::memcmp(native.X+1,original.X+1,sizeof(uint64_t)*29)==0);
  CHECK(native.Sp==context.sp);
  CHECK(native.Cpsr==context.pstate);
  CHECK(native.Fpsr==context.fpsr);
  CHECK(native.Fpcr==context.fpcr);
  CHECK(std::memcmp(native.V,original.V,sizeof(VectorRegister)*31)==0);
  CHECK(std::memcmp(&native.V[31],&context.v[31],sizeof(VectorRegister))==0);
#endif
}

TEST_CASE("Windows native fault dispatcher resumes a handled software exception", "[windows-context]") {
  using namespace rex::platform;
  struct Seen {bool called=false;uint64_t pc=0,address=0;};
  Seen seen;
  const auto handler=+[](Exception* exception,void* data) {
    auto& seen=*static_cast<Seen*>(data);
    seen.called=true;seen.pc=exception->GetPC();seen.address=exception->GetFaultAddress();
    return true;
  };
  ExceptionHandler::Install(handler,&seen);
  const ULONG_PTR arguments[]{1,0x12345000};
  RaiseException(EXCEPTION_ACCESS_VIOLATION,0,2,arguments);
  ExceptionHandler::Uninstall(handler,&seen);
  CHECK(seen.called);
  CHECK(seen.pc!=0);
  CHECK(seen.address==0x12345000);
}
#endif

TEST_CASE("portable byte shuffle preserves guest big-endian words", "[content-crypto]") {
  constexpr std::array<uint8_t,16> input{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
  constexpr std::array<uint8_t,16> expected{3,2,1,0,7,6,5,4,11,10,9,8,15,14,13,12};
  const auto value=simde_mm_loadu_si128(reinterpret_cast<const simde__m128i*>(input.data()));
  const auto mask=simde_mm_loadu_si128(reinterpret_cast<const simde__m128i*>(expected.data()));
  std::array<uint8_t,16> actual{};
  simde_mm_storeu_si128(reinterpret_cast<simde__m128i*>(actual.data()),simde_mm_shuffle_epi8(value,mask));
  CHECK(actual==expected);
}

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
