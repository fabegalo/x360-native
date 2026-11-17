#include <catch2/catch_test_macros.hpp>
#include "codegen/ppc/instruction.h"
#include "codegen/ppc/disasm.h"
#include <string_view>

TEST_CASE("MD rotations decode split operands and register contracts", "[sdk][ppc-md-decode]") {
  using namespace rex::codegen::detail;
  unsigned cases=0, bad_opcode=0, bad_reads=0, bad_writes=0, bad_rc=0, bad_control=0, bad_fields=0;
  for(unsigned xo=0;xo<4;++xo) for(unsigned shift=0;shift<64;++shift)
    for(unsigned mask=0;mask<64;++mask) for(unsigned rc=0;rc<2;++rc) {
      const unsigned source=(shift+mask)&31, dest=(shift^mask)&31;
      const uint32_t word=30u<<26 | source<<21 | dest<<16 | (shift&31)<<11 |
          (mask&31)<<6 | (mask>>5)<<5 | xo<<2 | (shift>>5)<<1 | rc;
      const auto decoded=DecodeInstruction(0x82010000,word);
      const auto effects=GetRegisterEffects(decoded);
      const uint32_t expected_reads=(1u<<source) | (xo==3 ? 1u<<dest : 0);
      ppc_insn native{};
      Disassemble(&decoded.code,4,decoded.address,native);
      bad_opcode += !native.opcode || decoded.opcode==Opcode::kUnknown;
      if(native.opcode) {
        bad_fields += native.operands[0]!=dest || native.operands[1]!=source;
        bad_rc += std::string_view(native.opcode->name).ends_with('.')!=bool(rc);
      }
      bad_reads += effects.gpr_use!=expected_reads;
      bad_writes += effects.gpr_def!=(1u<<dest);
      bad_control += decoded.IsBranch() || decoded.IsCall() || decoded.IsReturn() ||
          effects.fpr_use || effects.fpr_def || decoded.branch_target.has_value();
      ++cases;
    }
  CHECK(cases==32768);
  CHECK(bad_opcode==0);CHECK(bad_reads==0);
  CHECK(bad_writes==0);CHECK(bad_rc==0);CHECK(bad_control==0);
  CHECK(bad_fields==0);
  // The official decoder also supports MDS rotations formerly treated as unsupported.
  for(unsigned xo : {8u,9u}) {
    const uint32_t word=30u<<26 | 3u<<21 | 5u<<16 | 4u<<11 | 17u<<6 | xo<<1;
    const auto decoded=DecodeInstruction(0x82010000,word);
    CHECK(decoded.opcode!=Opcode::kUnknown);
    const auto effects=GetRegisterEffects(decoded);
    CHECK(effects.gpr_use==((1u<<3)|(1u<<4)));
    CHECK(effects.gpr_def==(1u<<5));
  }
  for(unsigned xo=5;xo<8;++xo)
    CHECK(DecodeInstruction(0x82010000,30u<<26|xo<<2).opcode==Opcode::kUnknown);
}
