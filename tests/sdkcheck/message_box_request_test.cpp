#include <catch2/catch_test_macros.hpp>
#include <rex/runtime.h>
#include <rex/cvar.h>
#include <rex/kernel/init.h>
#include <rex/kernel/xam/module.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xthread.h>
#include <rex/system/xio.h>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <thread>
#include <vector>
#include "../../titles/dash_17559/src/system_ui_request.h"

extern "C" REX_FUNC(__imp__XamShowMessageBoxUI);
extern "C" REX_FUNC(__imp__XMsgCompleteIORequest);
extern "C" REX_FUNC(__imp__XamShowDeviceSelectorUI);
REXCVAR_DECLARE(bool, headless);

TEST_CASE("Message boxes use the selected original HUD with their own request ABI",
          "[sdk][message-box-request]") {
  const auto* box=dash17559::SystemUiApplicationFor(0x2100D);
  REQUIRE(box);CHECK(std::string(box->path)=="game:\\system\\hud.xex");
  CHECK(box->resource_id==0);CHECK(box->request_size==1148);CHECK(box->user_offset==4);
  const auto* guide=dash17559::SystemUiApplicationFor(0x21015);
  REQUIRE(guide);CHECK(guide!=box);CHECK(guide->request_size==12);
}

TEST_CASE("Message boxes transport original owned requests instead of choosing a default",
          "[sdk][message-box-request]") {
  const bool previous=REXCVAR_GET(headless);
  struct Restore { bool value; ~Restore(){REXCVAR_SET(headless,value);} } restore{previous};
  REXCVAR_SET(headless,true); // No renderer, window or automatic UI acceptance.
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};config.tool_mode=true;config.kernel_init=rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config))==0);
  auto* memory=rt.memory();auto* base=memory->virtual_membase();
  const auto block=memory->SystemHeapAlloc(16384);REQUIRE(block);
  std::memset(base+block,0,16384);
  const auto put=[&](uint32_t at,uint32_t word){rex::memory::store_and_swap<uint32_t>(base+at,word);};
  const auto text=[&](uint32_t at,const std::u16string& value){
    for(size_t i=0;i<=value.size();++i)
      rex::memory::store_and_swap<uint16_t>(base+at+uint32_t(i*2),i==value.size()?0:value[i]);
  };
  text(block,u"Question");text(block+128,u"Body");
  text(block+512,u"Accept");text(block+640,u"Cancel");
  put(block+1024,block+512);put(block+1028,block+640);
  const auto output=block+2048,io=block+2112;
  put(output,0xCCCCCCCC);put(output+4,0xDDDDDDDD);
  bool delivered=false;std::vector<uint8_t> request;
  auto xam=rt.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  REQUIRE(xam);
  xam->system_ui_request=[&](uint32_t message,uint32_t at,uint32_t size){
    CHECK(message==0x2100D);CHECK(size==1148);
    delivered=true;request.assign(base+at,base+at+size);
    const auto overlap=rex::memory::load_and_swap<uint32_t>(base+at+1144);
    if(overlap) CHECK(rex::memory::load_and_swap<uint32_t>(base+overlap+8)==
                     rex::system::XThread::GetCurrentThreadHandle());
    return rex::X_HRESULT(0);
  };
  uint32_t result=~0u;
  auto caller=rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
      rt.kernel_state(),128*1024,0,[&]{
    PPCContext ctx{};ctx.r1.u64=block+12000;
    ctx.r3.u64=0xFF;ctx.r4.u64=block;ctx.r5.u64=block+128;ctx.r6.u64=2;
    ctx.r7.u64=block+1024;ctx.r8.u64=1;ctx.r9.u64=3;ctx.r10.u64=output;
    put(ctx.r1.u32+0x54,io);
    __imp__XamShowMessageBoxUI(ctx,base);result=ctx.r3.u32;
    // Let the old headless path finish before fixture teardown on the red run.
    for(unsigned i=0;!delivered && i<50 &&
        rex::memory::load_and_swap<uint32_t>(base+io)==997;++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return 0;
  }));
  REQUIRE(caller->Create()==0);
  uint64_t timeout=uint64_t(-50'000'000ll);REQUIRE(caller->Wait(0,0,0,&timeout)==0);
  REQUIRE(delivered);
  CHECK(result==997);REQUIRE(request.size()==1148);
  const auto word=[&](size_t offset){return rex::memory::load_and_swap<uint32_t>(request.data()+offset);};
  CHECK(word(0)==0);CHECK(word(4)==0xFF);CHECK(word(8)==1);
  CHECK(word(868)==2);CHECK(word(1132)==1);CHECK(word(1136)==3);
  CHECK(word(1140)==output);CHECK(word(1144)==io);
  CHECK(rex::memory::load_and_swap<uint32_t>(base+io)==997);
  CHECK(rex::memory::load_and_swap<uint32_t>(base+output)==0);
  CHECK(rex::memory::load_and_swap<uint32_t>(base+output+4)==0);
  CHECK(rex::memory::load_and_swap<uint16_t>(request.data()+12)==u'Q');
  CHECK(rex::memory::load_and_swap<uint16_t>(request.data()+98)==u'B');
  CHECK(rex::memory::load_and_swap<uint16_t>(request.data()+872)==u'A');
  CHECK(rex::memory::load_and_swap<uint16_t>(request.data()+958)==u'C');
  PPCContext standard{};standard.r3.u64=0xFF;standard.r4.u64=block;
  standard.r5.u64=block+128;standard.r6.u64=2;standard.r7.u64=block+1024;
  standard.r8.u64=1;standard.r9.u64=3;standard.r10.u64=output;
  const auto invoke=[&](PPCContext args,uint32_t overlap){
    uint32_t returned=~0u;
    auto thread=rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
        rt.kernel_state(),128*1024,0,[&]{
      args.r1.u64=block+12000;put(args.r1.u32+0x54,overlap);
      __imp__XamShowMessageBoxUI(args,base);returned=args.r3.u32;return 0;
    }));
    REQUIRE(thread->Create()==0);uint64_t limit=uint64_t(-50'000'000ll);
    REQUIRE(thread->Wait(0,0,0,&limit)==0);return returned;
  };
  // Original inline capacities include their terminator. Caller buffers can
  // disappear immediately after dispatch without changing the copied request.
  text(block,std::u16string(70,u'\u00E7'));text(block+4096,std::u16string(450,u'\u03A9'));
  text(block+512,std::u16string(70,u'Y'));
  auto args=standard;args.r5.u64=block+4096;
  REQUIRE(invoke(args,io)==997);
  CHECK(rex::memory::load_and_swap<uint16_t>(request.data()+12+41*2)==u'\u00E7');
  CHECK(rex::memory::load_and_swap<uint16_t>(request.data()+12+42*2)==0);
  CHECK(rex::memory::load_and_swap<uint16_t>(request.data()+98+382*2)==u'\u03A9');
  CHECK(rex::memory::load_and_swap<uint16_t>(request.data()+98+383*2)==0);
  CHECK(rex::memory::load_and_swap<uint16_t>(request.data()+872+41*2)==u'Y');
  CHECK(rex::memory::load_and_swap<uint16_t>(request.data()+872+42*2)==0);
  text(block,u"Changed");CHECK(rex::memory::load_and_swap<uint16_t>(request.data()+12)==u'\u00E7');
  text(block,u"Question");text(block+128,u"Body");text(block+512,u"Accept");
  text(block+768,u"Third");put(block+1032,block+768);
  args=standard;args.r6.u64=3;args.r8.u64=2;
  REQUIRE(invoke(args,io)==997);CHECK(word(868)==3);CHECK(word(1132)==2);
  CHECK(rex::memory::load_and_swap<uint16_t>(request.data()+1044)==u'T');
  args=standard;args.r4.u64=0;args.r5.u64=0;
  REQUIRE(invoke(args,io)==997);
  CHECK(rex::memory::load_and_swap<uint16_t>(request.data()+12)==0);
  CHECK(rex::memory::load_and_swap<uint16_t>(request.data()+98)==0);
  args=standard;args.r6.u64=0;args.r7.u64=0;args.r8.u64=0;args.r9.u64=0x10000;
  REQUIRE(invoke(args,io)==997);CHECK(word(868)==0);CHECK(word(1136)==0x10000);
  // Built-in special modes preserve the caller's result until the app writes it.
  put(output,0xCCCCCCCC);put(output+4,0xDDDDDDDD);
  args=standard;args.r9.u64=0x20003;
  REQUIRE(invoke(args,io)==997);
  CHECK(rex::memory::load_and_swap<uint32_t>(base+output)==0xCCCCCCCC);
  CHECK(rex::memory::load_and_swap<uint32_t>(base+output+4)==0xDDDDDDDD);
  args=standard;args.r10.u64=0;
  REQUIRE(invoke(args,0)==0);CHECK(word(1140)==0);CHECK(word(1144)==0);
  std::vector<PPCContext> invalid;
  const auto bad=[&](unsigned reg,uint32_t value){auto c=standard;
    switch(reg){case 3:c.r3.u64=value;break;case 4:c.r4.u64=value;break;
      case 5:c.r5.u64=value;break;case 6:c.r6.u64=value;break;case 7:c.r7.u64=value;break;
      case 8:c.r8.u64=value;break;case 9:c.r9.u64=value;break;case 10:c.r10.u64=value;break;}
    invalid.push_back(c);
  };
  bad(3,4);bad(6,4);bad(6,0);bad(8,2);bad(9,8);bad(9,0x300003);
  bad(9,0x100003);bad(10,0);bad(10,1);bad(4,1);bad(5,1);
  bad(4,0xFFFFFFFFu);bad(7,0);bad(7,0xFFFFFFFEu);
  for(size_t i=0;i<invalid.size();++i){
    CAPTURE(i);delivered=false;put(output,0xCCCCCCCC);put(io,0xA5A5A5A5);put(io+24,0);
    CHECK(invoke(invalid[i],io)==87);CHECK_FALSE(delivered);
    CHECK(rex::memory::load_and_swap<uint32_t>(base+output)==0xCCCCCCCC);
    CHECK(rex::memory::load_and_swap<uint32_t>(base+io)==0xA5A5A5A5);
    CHECK(rex::memory::load_and_swap<uint32_t>(base+io+24)==87);
  }
  delivered=false;CHECK(invoke(standard,1)==87);CHECK_FALSE(delivered);
  delivered=false;CHECK(invoke(standard,0)==87);CHECK_FALSE(delivered);
  put(block+1024,1);delivered=false;
  CHECK(invoke(standard,io)==87);CHECK_FALSE(delivered);put(block+1024,block+512);
  // The original app can complete before the API returns. The API must retain
  // its asynchronous return and the app's actual selection/status.
  xam->system_ui_request=[&](uint32_t,uint32_t at,uint32_t size){
    REQUIRE(size==1148);
    const auto target=rex::memory::load_and_swap<uint32_t>(base+at+1140);
    const auto overlap=rex::memory::load_and_swap<uint32_t>(base+at+1144);
    put(target,1);put(target+4,2);
    rt.kernel_state()->CompleteOverlappedImmediateEx(overlap,0,0,0);
    return rex::X_HRESULT(0);
  };
  REQUIRE(invoke(standard,io)==997);
  CHECK(rex::memory::load_and_swap<uint32_t>(base+output)==1);
  CHECK(rex::memory::load_and_swap<uint32_t>(base+output+4)==2);
  CHECK(rex::memory::load_and_swap<uint32_t>(base+io)==0);
  for(const auto [status,expected]:{std::pair<uint32_t,uint32_t>{0x8007000E,14},
      {0x80070057,1627},{0x80004005,1627}}){
    CAPTURE(status);
    xam->system_ui_request=[&](uint32_t,uint32_t,uint32_t){return rex::X_HRESULT(status);};
    CHECK(invoke(standard,io)==expected);
    CHECK(rex::memory::load_and_swap<uint32_t>(base+io)==expected);
    CHECK(rex::memory::load_and_swap<uint32_t>(base+io+24)==status);
  }
  xam->system_ui_request={};
  CHECK(invoke(standard,io)==1627); // Even headless mode may not approve a default.
  CHECK(rex::memory::load_and_swap<uint32_t>(base+io)==1627);
}

TEST_CASE("System UI completion retains the requesting thread across a service worker",
          "[sdk][message-box-io-context]") {
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};config.tool_mode=true;config.kernel_init=rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config))==0);
  const auto address=rt.memory()->SystemHeapAlloc(sizeof(rex::system::XAM_OVERLAPPED));
  REQUIRE(address);auto* io=rt.memory()->TranslateVirtual<rex::system::XAM_OVERLAPPED*>(address);
  std::memset(io,0,sizeof(*io));uint32_t requester=0,completer=0,returned=~0u;
  const auto start=[&](auto callback){
    auto thread=rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
        rt.kernel_state(),128*1024,0,callback));
    REQUIRE(thread->Create()==0);uint64_t limit=uint64_t(-50'000'000ll);
    REQUIRE(thread->Wait(0,0,0,&limit)==0);return thread;
  };
  auto first=start([&]{
    requester=rex::system::XThread::GetCurrentThreadHandle();
    io->result=997;io->context=requester;return 0;
  });
  auto second=start([&]{
    completer=rex::system::XThread::GetCurrentThreadHandle();
    PPCContext ctx{};ctx.r3.u64=address;ctx.r4.u64=0;ctx.r5.u64=0;ctx.r6.u64=7;
    __imp__XMsgCompleteIORequest(ctx,rt.memory()->virtual_membase());
    returned=ctx.r3.u32;return 0;
  });
  REQUIRE(requester);REQUIRE(completer);REQUIRE(requester!=completer);
  CHECK(returned==0);CHECK(uint32_t(io->result)==0);CHECK(uint32_t(io->length)==7);
  CHECK(uint32_t(io->context)==requester);
}

TEST_CASE("Storage UI records the initiating caller before dispatching its modal",
          "[sdk][message-box-io-context]") {
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};config.tool_mode=true;config.kernel_init=rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config))==0);
  const auto address=rt.memory()->SystemHeapAlloc(64);REQUIRE(address);
  auto* base=rt.memory()->virtual_membase();std::memset(base+address,0,64);
  auto* io=rt.memory()->TranslateVirtual<rex::system::XAM_OVERLAPPED*>(address);
  auto xam=rt.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  REQUIRE(xam);bool delivered=false;uint32_t returned=~0u;
  xam->system_ui_request=[&](uint32_t message,uint32_t,uint32_t size){
    delivered=true;CHECK(message==0x2100B);CHECK(size==40);
    CHECK(uint32_t(io->result)==997);
    CHECK(uint32_t(io->context)==rex::system::XThread::GetCurrentThreadHandle());
    return rex::X_HRESULT(0);
  };
  auto thread=rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
      rt.kernel_state(),128*1024,0,[&]{
    PPCContext ctx{};ctx.r3.u64=0xFF;ctx.r4.u64=1;ctx.r5.u64=0;
    ctx.r6.u64=1024;ctx.r7.u64=address+32;ctx.r8.u64=address;
    __imp__XamShowDeviceSelectorUI(ctx,base);returned=ctx.r3.u32;return 0;
  }));
  REQUIRE(thread->Create()==0);uint64_t limit=uint64_t(-50'000'000ll);
  REQUIRE(thread->Wait(0,0,0,&limit)==0);CHECK(delivered);CHECK(returned==997);
  xam->system_ui_request={};
}
