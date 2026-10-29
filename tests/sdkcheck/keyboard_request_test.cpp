#include <catch2/catch_test_macros.hpp>
#include <rex/runtime.h>
#include <rex/cvar.h>
#include <rex/kernel/init.h>
#include <rex/kernel/xam/module.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xthread.h>
#include <rex/system/xio.h>
#include <cstring>
#include <filesystem>
#include <vector>
#include "../../titles/dash_17559/src/system_ui_request.h"
#include "../../titles/dash_17559/src/system_ui_dependencies.h"

extern "C" REX_FUNC(__imp__XamShowKeyboardUI);
extern "C" REX_FUNC(__imp__XamShowBrandedKeyboardUI);
extern "C" REX_FUNC(__imp__XMsgCompleteIORequest);
REXCVAR_DECLARE(bool, headless);

TEST_CASE("Keyboard APIs route to the original 17559 virtual keyboard", "[sdk][keyboard-request]") {
  for (auto message : {0x21005u, 0x21010u}) {
    const auto* app = dash17559::SystemUiApplicationFor(message);
    REQUIRE(app);
    CHECK(std::string(app->path) == "game:\\vk.xex");
    CHECK(app->request_size == (message == 0x21005 ? 32 : 36));
    CHECK(app->user_offset == 0);
    CHECK(app->resource_id == 11);
  }
}

TEST_CASE("Keyboard requests remain pending until original UI completes on another thread",
          "[sdk][keyboard-request]") {
  const bool previous = REXCVAR_GET(headless);
  struct Restore { bool value; ~Restore(){ REXCVAR_SET(headless, value); } } restore{previous};
  REXCVAR_SET(headless, true);
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{}; config.tool_mode = true; config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == 0);
  auto* mem = rt.memory(); auto* base = mem->virtual_membase();
  const auto block = mem->SystemHeapAlloc(16384); REQUIRE(block);
  std::memset(base + block, 0, 16384);
  const auto put = [&](uint32_t at, uint32_t value) { rex::memory::store_and_swap<uint32_t>(base + at, value); };
  const auto get = [&](uint32_t at) { return rex::memory::load_and_swap<uint32_t>(base + at); };
  const auto text = [&](uint32_t at, std::u16string_view value) {
    for (size_t i=0; i<=value.size(); ++i)
      rex::memory::store_and_swap<uint16_t>(base + at + uint32_t(2*i), i<value.size()?value[i]:0);
  };
  text(block, u"A\u00e7\u03a9\U0001f600"); text(block+128, u"Nome"); text(block+256, u"Descri\u00e7\u00e3o");
  const auto output=block+512, io=block+1024, brand=block+2048;
  std::memset(base+output, 0xCC, 32);
  auto xam=rt.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  REQUIRE(xam);
  std::vector<uint8_t> request; uint32_t actual_message=0, requester=0;
  xam->system_ui_request=[&](uint32_t message,uint32_t at,uint32_t size) {
    actual_message=message; request.assign(base+at,base+at+size);
    requester=rex::system::XThread::GetCurrentThreadHandle();
    CHECK(get(io)==997); CHECK(get(io+8)==requester);
    return rex::X_HRESULT(0);
  };
  const auto invoke=[&](bool branded,uint32_t length=8,uint32_t result_address=0,uint32_t overlap=0) {
    uint32_t result=~0u;
    auto caller=rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
      rt.kernel_state(),128*1024,0,[&] {
        PPCContext ctx{}; ctx.r1.u64=block+12000;ctx.r3.u64=0;ctx.r4.u64=0;
        if(branded) {
          put(brand,0x12345678);ctx.r5.u64=brand;ctx.r6.u64=block;ctx.r7.u64=block+128;
          ctx.r8.u64=block+256;ctx.r9.u64=result_address;ctx.r10.u64=length;put(ctx.r1.u32+0x54,overlap);
          __imp__XamShowBrandedKeyboardUI(ctx,base);
        } else {
          ctx.r5.u64=block;ctx.r6.u64=block+128;ctx.r7.u64=block+256;
          ctx.r8.u64=result_address;ctx.r9.u64=length;ctx.r10.u64=overlap;
          __imp__XamShowKeyboardUI(ctx,base);
        }
        result=ctx.r3.u32;return 0;
      }));
    REQUIRE(caller->Create()==0);uint64_t timeout=uint64_t(-50'000'000ll);
    REQUIRE(caller->Wait(0,0,0,&timeout)==0);return result;
  };
  for (bool branded : {false,true}) {
    request.clear();put(io,0);put(io+8,0);
    CHECK(invoke(branded,8,output,io)==997);
    REQUIRE(request.size()==(branded?36:32));
    CHECK(actual_message==(branded?0x21010:0x21005));
    const auto word=[&](size_t offset){return rex::memory::load_and_swap<uint32_t>(request.data()+offset);};
    CHECK(word(0)==0);CHECK(word(4)==8);CHECK(word(8)==(branded?0x08000000:0));
    CHECK(word(12)==block);CHECK(word(16)==block+128);CHECK(word(20)==block+256);
    CHECK(word(24)==output);CHECK(word(28)==io);
    if(branded){CHECK(word(32)==0x12345678);CHECK(get(brand)==0);}
    CHECK(get(io)==997);CHECK(get(output)==0xCCCCCCCC);CHECK(get(io+8)==requester);
    // Original 917DEE78 maps a nonzero extended error to 1627 when closing
    // state is zero (Escape observed in the original scene: 1627/1223).
    // Submission must not accept the default or bind I/O to the renderer.
    PPCContext done{};done.r3.u64=io;done.r4.u64=1627;done.r5.u64=1223;done.r6.u64=0;
    __imp__XMsgCompleteIORequest(done,base);
    CHECK(get(io)==1627);CHECK(get(io+24)==1223);CHECK(get(io+8)==requester);
    CHECK(get(output)==0xCCCCCCCC);
  }
  request.clear();CHECK(invoke(false,0,output,io)==87);CHECK(request.empty());
  CHECK(invoke(false,8,0,io)==87);CHECK(request.empty());
  CHECK(invoke(false,8,output,0)==87);CHECK(request.empty());
  CHECK(invoke(false,0x80000000,output,io)==87);CHECK(request.empty());
  xam->system_ui_request=nullptr;
  CHECK(invoke(false,8,output,io)==1627);CHECK(get(io)==1627);
  CHECK(get(output)==0xCCCCCCCC);
  mem->SystemHeapFree(block);
}

TEST_CASE("CreateProfile resolves vk only in its verified original XAM namespace", "[sdk][keyboard-dependency]") {
  CHECK(dash17559::OriginalKeyboardDependency("sys17559:\\system\\xam_ui.xex") == "sys17559:\\vk.xex");
  CHECK(dash17559::OriginalKeyboardDependency("game:\\system\\xam_ui.xex") == "game:\\vk.xex");
  CHECK(dash17559::OriginalKeyboardDependency("\\SystemRoot\\system\\xam_ui.xex") == "sys17559:\\vk.xex");
  CHECK(dash17559::OriginalKeyboardDependency("\\Device\\Harddisk0\\Partition1\\system\\xam_ui.xex") == "game:\\vk.xex");
  for (auto path : {"", "vk.xex", "sys13604:\\system\\xam_ui.xex",
       "sys6717:\\system\\xam_ui.xex", "game:\\xam.xex", "game:\\system\\..\\xam_ui.xex"})
    CHECK(dash17559::OriginalKeyboardDependency(path).empty());
}
