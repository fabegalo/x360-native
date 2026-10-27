#pragma once

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/runtime.h>
#include <rex/system/xthread.h>
#include <rex/system/thread_state.h>
#include <rex/system/xio.h>
#include <chrono>
#include <cstring>
#include <functional>
#include <memory>
#include <string_view>

extern "C" REX_FUNC(__imp__XamShowMessageBoxUI);
REXCVAR_DEFINE_UINT32(dash17559_message_box_probe_seconds, 0, "Diagnostics",
                     "Submit one original message-box request after 1..120 seconds; zero disables the diagnostic");

namespace dash17559 {
// Explicit service validation in any title's system frames. No clock read,
// allocation, synthetic request or input is added to ordinary sessions.
inline std::function<void()> WithMessageBoxDiagnostic(rex::Runtime& runtime,
                                                      std::function<void()> poll) {
  const auto seconds=REXCVAR_GET(dash17559_message_box_probe_seconds);
  if (!seconds || seconds>120) return poll;
  struct Request {
    rex::Runtime& runtime;uint32_t address=0;
    explicit Request(rex::Runtime& value):runtime(value){}
    ~Request(){if(address)runtime.memory()->SystemHeapFree(address);}
  };
  return [&runtime,poll=std::move(poll),seconds,request=std::make_shared<Request>(runtime),
          submitted=false,start=std::chrono::steady_clock::now()]() mutable {
    poll();
    auto* memory=runtime.memory();auto* base=memory->virtual_membase();
    if(submitted){
      if(!request->address)return;
      const auto* io=memory->TranslateVirtual<rex::system::XAM_OVERLAPPED*>(request->address+8);
      if(io->result==997)return;
      REXLOG_INFO("17559 message-box diagnostic: completed result={:08X} extended={:08X} selected={} user={}",
          uint32_t(io->result),uint32_t(io->extended_error),
          rex::memory::load_and_swap<uint32_t>(base+request->address),
          rex::memory::load_and_swap<uint32_t>(base+request->address+4));
      memory->SystemHeapFree(request->address);request->address=0;return;
    }
    if(std::chrono::steady_clock::now()-start<std::chrono::seconds(seconds))return;
    submitted=true;request->address=memory->SystemHeapAlloc(1024);
    if(!request->address){REXLOG_ERROR("17559 message-box diagnostic: allocation failed");return;}
    const auto address=request->address;std::memset(base+address,0,1024);
    const auto text=[&](uint32_t offset,std::u16string_view value){
      for(size_t i=0;i<value.size();++i)
        rex::memory::store_and_swap<uint16_t>(base+address+offset+uint32_t(i*2),value[i]);
    };
    text(64,u"Confirmar");text(128,u"Cancelar");
    text(256,u"Mensagem de sistema");
    text(384,u"Caixa original do Xbox 360. Escolha um bot\u00E3o para concluir o teste.");
    rex::memory::store_and_swap<uint32_t>(base+address+48,address+64);
    rex::memory::store_and_swap<uint32_t>(base+address+52,address+128);
    auto* ctx=rex::system::XThread::GetCurrentThread()->thread_state()->context();
    // The ninth PPC argument lives in the caller's outgoing home area. Keep
    // its prior contents as well as the register context owned by the frame.
    struct StackArgument {
      uint8_t* base;uint32_t address,value;
      ~StackArgument(){rex::memory::store_and_swap<uint32_t>(base+address,value);}
    } saved{base,ctx->r1.u32+0x54,rex::memory::load_and_swap<uint32_t>(base+ctx->r1.u32+0x54)};
    rex::memory::store_and_swap<uint32_t>(base+saved.address,address+8);
    ctx->r3.u64=0xFF;ctx->r4.u64=address+256;ctx->r5.u64=address+384;
    ctx->r6.u64=2;ctx->r7.u64=address+48;ctx->r8.u64=1;ctx->r9.u64=3;ctx->r10.u64=address;
    __imp__XamShowMessageBoxUI(*ctx,base);
    REXLOG_INFO("17559 message-box diagnostic: submitted original request result={:08X}",ctx->r3.u32);
    if(ctx->r3.u32!=997){memory->SystemHeapFree(address);request->address=0;}
  };
}
}
