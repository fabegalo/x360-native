#pragma once
#include "system_ui.h"
#include <rex/logging/log_category.h>
#include <rex/ppc/recompiled_code.h>
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/heap.h>
#include <rex/kernel/memory_view.h>
#include <rex/kernel/object/kernel_thread.h>
#include <rex/kernel/types/io.h>
#include <rex/string/text.h>
#include <chrono>
#include <cstring>
#include <functional>
#include <memory>

extern "C" REX_FUNC(__imp__XamShowKeyboardUI);
extern "C" REX_FUNC(__imp__XamShowBrandedKeyboardUI);

namespace dash17559 {
// Explicit service validation in original dashboard/game frames. No allocation,
// clock read, automatic selection or wrapper is added to ordinary sessions.
inline std::function<void()> WithKeyboardDiagnostic(rex::kernel::TitleRuntime& runtime, std::function<void()> poll) {
  const auto seconds=GetSystemUiSettings().keyboard_probe_seconds.Get();
  if (!seconds || seconds>120) return poll;
  struct Request {
    rex::kernel::TitleRuntime& runtime; uint32_t address=0;
    ~Request(){if(address)runtime.GetGuestHeap().SystemHeapFree(address);}
  };
  auto request=std::make_shared<Request>(runtime);
  return [&runtime,poll=std::move(poll),seconds,request=std::move(request),submitted=false,
          start=std::chrono::steady_clock::now()]() mutable {
    poll();auto memory=runtime.GetGuestMemory().GetView();auto* base=memory.GetVirtualMembase();
    if(submitted) {
      if(!request->address)return;
      const auto* io=memory.TranslateVirtual<rex::kernel::XOVERLAPPED*>(request->address);
      if(io->result==997)return;
      std::u16string text;
      for(uint32_t i=0;i<16;++i){const auto unit=rex::memory::load_and_swap<uint16_t>(base+request->address+64+2*i);
        if(!unit)break;text.push_back(char16_t(unit));}
      REXLOG_INFO("17559 keyboard diagnostic: completed result={:08X} extended={:08X} text='{}' requester={:08X}",
          uint32_t(io->result),uint32_t(io->extended_error),rex::string::ToUTF8(text),uint32_t(io->context));
      runtime.GetGuestHeap().SystemHeapFree(request->address);request->address=0;return;
    }
    if(std::chrono::steady_clock::now()-start<std::chrono::seconds(seconds))return;
    submitted=true;request->address=runtime.GetGuestHeap().SystemHeapAlloc(1024);
    if(!request->address){REXLOG_ERROR("17559 keyboard diagnostic: allocation failed");return;}
    const auto address=request->address;std::memset(base+address,0,1024);
    const auto text=[&](uint32_t offset,std::u16string_view value){
      for(size_t i=0;i<value.size();++i)rex::memory::store_and_swap<uint16_t>(base+address+offset+uint32_t(i*2),value[i]);};
    text(128,u"Inicial");text(256,u"Teclado do sistema");
    text(384,u"Edite o texto e confirme, ou cancele para voltar ao jogo.");
    auto* ctx=rex::kernel::KernelThread::GetCallingThread()->GetThreadState()->GetContext();
    ctx->r3.u64=0;ctx->r4.u64=0;
    if(GetSystemUiSettings().keyboard_probe_branded.Get()) {
      const auto stack=ctx->r1.u32+0x54;const auto old=rex::memory::load_and_swap<uint32_t>(base+stack);
      rex::memory::store_and_swap<uint32_t>(base+stack,address);
      ctx->r5.u64=address+48;ctx->r6.u64=address+128;ctx->r7.u64=address+256;
      ctx->r8.u64=address+384;ctx->r9.u64=address+64;ctx->r10.u64=16;
      __imp__XamShowBrandedKeyboardUI(*ctx,base);
      rex::memory::store_and_swap<uint32_t>(base+stack,old);
    } else {
      ctx->r5.u64=address+128;ctx->r6.u64=address+256;ctx->r7.u64=address+384;
      ctx->r8.u64=address+64;ctx->r9.u64=16;ctx->r10.u64=address;
      __imp__XamShowKeyboardUI(*ctx,base);
    }
    REXLOG_INFO("17559 keyboard diagnostic: submitted original request result={:08X}",ctx->r3.u32);
    if(ctx->r3.u32!=997){runtime.GetGuestHeap().SystemHeapFree(address);request->address=0;}
  };
}
}
