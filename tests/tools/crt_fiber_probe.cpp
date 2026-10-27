// Explicit bounded CPU contract probe of the existing native CRT hooks.
#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/xthread.h>
#include <rex/ppc/func.h>
#include <cstdio>
#include <filesystem>
#include <cstring>
#include <windows.h>

extern "C" REX_FUNC(rexcrt_ConvertThreadToFiber);
extern "C" REX_FUNC(rexcrt_ConvertFiberToThread);
extern "C" REX_FUNC(rexcrt_CreateFiber);
extern "C" REX_FUNC(rexcrt_DeleteFiber);
extern "C" REX_FUNC(rexcrt_SwitchToFiber);

namespace {
uint32_t Load(uint8_t* base,uint32_t address) {
  return rex::memory::load_and_swap<uint32_t>(base+address);
}
void Store(uint8_t* base,uint32_t address,uint32_t value) {
  rex::memory::store_and_swap<uint32_t>(base+address,value);
}
void Fill(PPCContext& ctx, uint8_t value) {
  std::memset(&ctx.r14,value,18*sizeof(PPCRegister));
  std::memset(&ctx.f14,value,18*sizeof(PPCRegister));
  std::memset(&ctx.v14,value,18*sizeof(PPCVRegister));
  std::memset(&ctx.v64,value,64*sizeof(PPCVRegister));
}
bool Matches(const PPCContext& ctx, uint8_t value) {
  PPCContext expected{}; Fill(expected,value);
  return std::memcmp(&ctx.r14,&expected.r14,18*sizeof(PPCRegister))==0 &&
      std::memcmp(&ctx.f14,&expected.f14,18*sizeof(PPCRegister))==0 &&
      std::memcmp(&ctx.v14,&expected.v14,18*sizeof(PPCVRegister))==0 &&
      std::memcmp(&ctx.v64,&expected.v64,64*sizeof(PPCVRegister))==0;
}
void FiberBody(PPCContext& ctx,uint8_t* base) {
  const uint32_t data=ctx.r3.u32, stack=ctx.r1.u32;
  const auto main=Load(base,data);
  Fill(ctx,0x35);
  for(uint32_t i=1;i<=4;++i) {
    Store(base,data+4,i);
    ctx.r3.u64=main;
    rexcrt_SwitchToFiber(ctx,base);
    if(!Matches(ctx,0x35) || ctx.r1.u32!=stack) Store(base,data+8,1);
  }
  // The caller deletes this suspended fiber after four returns.
  std::abort();
}
}
int main(int argc,char** argv) {
  if(argc!=2 || !std::filesystem::create_directory(argv[1])) return 2;
  rex::Runtime runtime(argv[1],argv[1]);
  rex::RuntimeConfig config{};config.tool_mode=true;config.kernel_init=rex::kernel::InitializeKernel;
  const PPCFuncMapping functions[]{{0x82010000,FiberBody},{0,nullptr}};
  const rex::PPCImageInfo image{0x82010000,0x1000,0x82000000,0x20000,functions};
  if(runtime.Setup(image,std::move(config),true)) return 3;
  auto* state=runtime.kernel_state();
  auto* memory=runtime.memory();auto* base=memory->virtual_membase();
  const auto data=memory->SystemHeapAlloc(64);if(!data) return 4;
  std::memset(base+data,0,64);
  bool passed=true;unsigned switches=0;
  auto thread=rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
      state,128*1024,0,[&] {
    auto& ctx=*rex::system::XThread::GetCurrentThread()->thread_state()->context();
    const auto stack=ctx.r1.u32;
    ctx.r3.u64=0;rexcrt_ConvertThreadToFiber(ctx,base);
    const auto main=ctx.r3.u32;if(!main) {passed=false;return 5;}
    Store(base,data,main);
    ctx.r3.u64=64*1024;ctx.r4.u64=0x82010000;ctx.r5.u64=data;
    rexcrt_CreateFiber(ctx,base);
    const auto child=ctx.r3.u32;if(!child) {passed=false;return 6;}
    Fill(ctx,0x6A);
    for(uint32_t i=1;i<=4;++i) {
      ctx.r3.u64=child;rexcrt_SwitchToFiber(ctx,base);
      passed &= Load(base,data+4)==i && Load(base,data+8)==0 &&
          Matches(ctx,0x6A) && ctx.r1.u32==stack;
      ++switches;
    }
    ctx.r3.u64=child;rexcrt_DeleteFiber(ctx,base);
    passed &= !state->LookupFiber(child);
    rexcrt_ConvertFiberToThread(ctx,base);
    passed &= ctx.r3.u32==1 && !state->LookupFiber(main);
    return 0;
  }));
  if(thread->Create()) return 7;
  uint64_t timeout=uint64_t(-100'000'000ll);
  if(thread->Wait(0,0,0,&timeout)) ExitProcess(8);
  std::printf("%s switches=%u independent continuations, GPR/FPR/VMX and stack preservation, lifecycle cleanup\n",passed?"PASS":"FAIL",switches);
  return passed?0:9;
}
