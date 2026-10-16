#include <rex/graphics/d3d12/inactive_vertex_fetch.h>
#include <array>
#include <iostream>
using namespace rex::graphics;
using namespace rex::graphics::ucode;
using Words = std::array<uint32_t, 3>;
static uint64_t Exec(ControlFlowOpcode opcode, uint32_t address, uint32_t count,
                     uint32_t sequence, bool condition = false) {
  return address | (uint64_t(count) << 12) | (uint64_t(sequence) << 16) |
         (uint64_t(condition) << 42) | (uint64_t(opcode) << 44);
}
static void Pair(std::vector<uint32_t>& code, uint64_t a, uint64_t b) {
  code.push_back(uint32_t(a));
  code.push_back(uint32_t(a >> 32) | uint32_t(b << 16));
  code.push_back(uint32_t(b >> 16));
}
static Words Setp(uint32_t constant, AluScalarOpcode opcode = AluScalarOpcode::kSetpEq) {
  // Scalar src3 swizzle .x (relative swizzle for W -> X is 1 at bits 6:7).
  return {uint32_t(opcode) << 26, 0x40, constant};
}
static Words Fetch(uint32_t index, bool predicated = true, bool condition = false, bool mini = false) {
  return {(1u << 19) | ((index / 3) << 20) | ((index % 3) << 25),
          (uint32_t(predicated) << 31) | (uint32_t(mini) << 30),
          1u | (uint32_t(condition) << 31)};
}
static std::vector<uint32_t> Program(uint32_t index, Words predicate, bool fetch_predicated = true,
                                     bool cf_predicated = true) {
  std::vector<uint32_t> code;
  Pair(code, Exec(ControlFlowOpcode::kExec, 2, 1, 0),
             Exec(cf_predicated ? ControlFlowOpcode::kCondExecPred : ControlFlowOpcode::kExec, 3, 1, 1));
  Pair(code, Exec(ControlFlowOpcode::kExecEnd, 4, 0, 0), 0);
  code.insert(code.end(), predicate.begin(), predicate.end());
  auto fetch = Fetch(index, fetch_predicated);
  code.insert(code.end(), fetch.begin(), fetch.end());
  return code;
}
int main() {
  RegisterFile regs;
  std::memset(&regs, 0, sizeof(regs));
  unsigned checks=0, failures=0;
  auto check=[&](bool ok, const char* name){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<name<<'\n';}};
  auto proof=[&](const auto& code,uint32_t index){return d3d12::IsVertexFetchInactive(code,regs,index);};
  for(uint32_t index=0;index<96;++index) {
    for(uint32_t constant : {0u,17u,127u,255u}) {
      auto code=Program(index,Setp(constant));
      uint32_t& value=regs[XE_GPU_REG_SHADER_CONSTANT_000_X+4*constant];
      value=0; check(proof(code,index),"zero / inactive");
      value=0x80000000; check(proof(code,index),"negative zero / inactive");
      value=1; check(proof(code,index),"denormal / inactive");
      value=0x3F800000; check(!proof(code,index),"one / active after constants change");
      value=0x7FC00000; check(!proof(code,index),"NaN / active");
      value=0x7F800000; check(!proof(code,index),"infinity / active");
      value=0;
    }
  }
  auto code=Program(7,Setp(17));
  check(!proof(code,8),"absent target is not proof");
  check(!proof(code,96),"invalid index");
  check(!proof(std::vector<uint32_t>{},7),"empty shader");
  auto truncated=code;truncated.pop_back();check(!proof(truncated,7),"truncated instruction");
  auto temp=Setp(17);temp[2]|=1u<<29;
  check(!proof(Program(7,temp),7),"per-vertex predicate unknown");
  auto relative=Setp(17);relative[1]|=(1u<<30)|(1u<<31);
  check(!proof(Program(7,relative),7),"relative constant unknown");
  check(!proof(Program(7,Setp(17),false,false),7),"unconditional fetch active");
  check(proof(Program(7,Setp(17),true,false),7),"instruction predication alone");
  check(proof(Program(7,Setp(17),false,true),7),"uniform EXEC predication alone");
  auto ne=Program(7,Setp(17,AluScalarOpcode::kSetpNe));
  check(!proof(ne,7),"setp_ne zero active");
  regs[XE_GPU_REG_SHADER_CONSTANT_000_X+4*17]=0x3F800000;
  check(proof(ne,7),"setp_ne one inactive");
  regs[XE_GPU_REG_SHADER_CONSTANT_000_X+4*17]=0;
  regs[XE_GPU_REG_SQ_VS_CONST]=0xFF100;check(!proof(code,7),"unsupported constant base");
  regs[XE_GPU_REG_SQ_VS_CONST]=0xFF000;check(proof(code,7),"supported constant base");
  regs[XE_GPU_REG_SQ_VS_CONST]=0;
  for(auto op:{ControlFlowOpcode::kCondJmp,ControlFlowOpcode::kCondCall,ControlFlowOpcode::kLoopStart,
               ControlFlowOpcode::kReturn,ControlFlowOpcode::kCondExecEnd}) {
    auto changed=code;changed[1]=(changed[1]&0xFFFF0FFFu)|(uint32_t(op)<<12);
    check(!proof(changed,7),"unsupported control flow");
  }
  auto bad=code;bad[0]=(bad[0]&~0xFFFu)|0xFFFu;check(!proof(bad,7),"out-of-bounds EXEC");
  // A later use must not inherit a proof for an earlier inactive use.
  auto repeated=code;
  repeated[3]=uint32_t(Exec(ControlFlowOpcode::kExecEnd,4,1,1));
  auto active=Fetch(7,false);repeated.insert(repeated.end(),active.begin(),active.end());
  check(!proof(repeated,7),"second active use defeats proof");
  repeated=code;
  repeated[3]=uint32_t(Exec(ControlFlowOpcode::kExecEnd,4,1,1));
  auto mini=Fetch(0,false,false,true);repeated.insert(repeated.end(),mini.begin(),mini.end());
  check(!proof(repeated,7),"active mini after inactive full defeats proof");
  repeated.back()=1;repeated[13]|=1u<<31;
  check(proof(repeated,7),"inactive full and inactive mini");
  // Boolean constants are sampled per draw, including predicate producers.
  auto conditional=code;
  conditional[1]=(conditional[1]&0xFFFF0000u)|(uint32_t(ControlFlowOpcode::kCondExec)<<12)|(1u<<10);
  check(!proof(conditional,7),"skipped predicate producer remains unknown");
  regs[XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031]=1;
  check(proof(conditional,7),"executed predicate producer");
  regs[XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031]=0;
  std::cout<<checks<<" checks, "<<failures<<" failures\n";
  return failures ? 1 : 0;
}
