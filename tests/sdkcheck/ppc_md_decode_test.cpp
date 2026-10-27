#include <catch2/catch_test_macros.hpp>
#include "../../external/rexglue-sdk/src/codegen/ppc/instruction.h"
#include <string_view>
#include <vector>

TEST_CASE("MD rotations decode split operands and register contracts", "[sdk][ppc-md-decode]") {
  using namespace rex::codegen::ppc;
  const char* names[]{"rldicl","rldicr","rldic","rldimi"};
  unsigned cases=0, bad_opcode=0, bad_format=0, bad_reads=0, bad_writes=0, bad_rc=0, bad_control=0, bad_fields=0;
  for(unsigned xo=0;xo<4;++xo) for(unsigned shift=0;shift<64;++shift)
    for(unsigned mask=0;mask<64;++mask) for(unsigned rc=0;rc<2;++rc) {
      const unsigned source=(shift+mask)&31, dest=(shift^mask)&31;
      const uint32_t word=30u<<26 | source<<21 | dest<<16 | (shift&31)<<11 |
          (mask&31)<<6 | (mask>>5)<<5 | xo<<2 | (shift>>5)<<1 | rc;
      const auto decoded=decode_instruction(0x82010000,word);
      const auto semantics=decoded.get_semantics();
      std::vector<uint8_t> expected_reads{uint8_t(source)};
      if(xo==3) expected_reads.push_back(uint8_t(dest));
      bad_opcode += std::string_view(get_opcode_info(decoded.opcode).name)!=names[xo];
      bad_format += decoded.format!=InstrFormat::kMD;
      bad_fields += decoded.MD.shift()!=shift || decoded.MD.mask()!=mask ||
          decoded.MD.RS!=source || decoded.MD.RA!=dest || decoded.MD.XO!=xo;
      bad_reads += semantics.reads_gpr!=expected_reads;
      bad_writes += semantics.writes_gpr!=std::vector<uint8_t>{uint8_t(dest)};
      bad_rc += decoded.is_record_form()!=bool(rc) || semantics.writes_cr!=bool(rc);
      bad_control += decoded.is_branch() || decoded.is_call() || decoded.is_return() ||
          semantics.reads_memory || semantics.writes_memory || decoded.branch_target.has_value();
      ++cases;
    }
  CHECK(cases==32768);
  CHECK(bad_opcode==0);CHECK(bad_format==0);CHECK(bad_reads==0);
  CHECK(bad_writes==0);CHECK(bad_rc==0);CHECK(bad_control==0);
  CHECK(bad_fields==0);
  // Unimplemented/reserved primary-30 encodings retain the conservative barrier.
  for(unsigned xo=4;xo<8;++xo)
    CHECK(decode_instruction(0x82010000,30u<<26|xo<<2).opcode==Opcode::kUnknown);
}
