#include <catch2/catch_test_macros.hpp>
#include <rex/runtime.h>
#include <rex/cvar.h>
#include <rex/kernel/init.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/user_module.h>
#include "synthetic_xex.h"
#include "../../titles/dash_17559/src/system_ui.h"

extern "C" REX_FUNC(__imp__XamUniSortCmpString);
extern "C" REX_FUNC(__imp__XamContentGetLocalizedString);

namespace {
struct SystemUiScope {
  struct Setting {const char* name;std::string value;rex::cvar::Source source;};
  std::vector<Setting> settings;
  bool installed=true;
  explicit SystemUiScope(rex::Runtime& rt) {
    for(const auto* name:{"guide_button","guide_chord_ms","guide_chord_users","keybind_guide"})
      settings.push_back({name,rex::cvar::GetFlagByName(name),rex::cvar::GetFlagSource(name)});
    dash17559::InstallSystemUi(rt);
  }
  void Close() {
    if(!installed)return;
    installed=false;dash17559::ReleaseSystemUiHost();
    for(const auto& setting:settings) {
      if(setting.source==rex::cvar::Source::kDefault)rex::cvar::ResetToDefault(setting.name);
      else rex::cvar::SetFlagByName(setting.name,setting.value);
    }
  }
  ~SystemUiScope(){Close();}
};
bool service_called=false, image_mapped=false;
void ServiceProbe(PPCContext& ctx,uint8_t*) {
  service_called=true;
  image_mapped=bool(rex::Runtime::instance()->kernel_state()->GetModule(dash17559::SystemXamPath()));
  CHECK(ctx.r3.u32==0x1234);CHECK(ctx.r4.u32==0x5678);
  CHECK(ctx.r5.u32==15);CHECK(ctx.r6.u32==7);
  ctx.r3.u64=77;
}
void RegisterServiceProbe(rex::Runtime& rt) {
  auto* dispatcher=rt.function_dispatcher();
  REQUIRE(dispatcher->InitializeFunctionTable(0x816E0000,0x10000,0x816E0000,0x10000,true,true));
  REQUIRE(dispatcher->SetFunction(0x816E35A8,ServiceProbe));
  REQUIRE(dispatcher->SetFunction(0x816EE090,ServiceProbe));
}
PPCContext Request() {
  PPCContext ctx{};ctx.r3.u64=0x1234;ctx.r4.u64=0x5678;ctx.r5.u64=15;ctx.r6.u64=7;return ctx;
}
}

TEST_CASE("Original storage services refuse dispatch when their image cannot load","[sdk][original-xam-service]") {
  x360n::test::Workdir dir("x360n-original-service","missing-image");
  dir.write("anchor.xex",x360n::test::BuildXex({},nullptr,0,x360n::test::Entry::kReturnZero));
  rex::Runtime rt(dir.dir,dir.dir);rex::RuntimeConfig config{};
  config.tool_mode=true;config.kernel_init=rex::kernel::InitializeKernel;REQUIRE(rt.Setup(std::move(config))==0);
  RegisterServiceProbe(rt);
  auto anchor=rt.kernel_state()->LoadUserModule("game:\\anchor.xex",false);REQUIRE(anchor);
  rt.kernel_state()->SetExecutableModule(anchor);
  SystemUiScope host(rt);
  for(auto* call : {__imp__XamUniSortCmpString,__imp__XamContentGetLocalizedString}) {
    service_called=false;auto ctx=Request();call(ctx,rt.memory()->virtual_membase());
    CHECK_FALSE(service_called);CHECK(ctx.r3.u32==0x80004001);
    CHECK(ctx.r4.u32==0x5678);CHECK(ctx.r5.u32==15);CHECK(ctx.r6.u32==7);
  }
}

TEST_CASE("Original storage services load the selected XAM before invoking its collation and localization",
          "[sdk][original-xam-service]") {
  x360n::test::Workdir dir("x360n-original-service","selected-image");
  std::filesystem::create_directories(dir.dir/"system");
  dir.write("system/xam_ui.xex",x360n::test::BuildXex({},nullptr,0,x360n::test::Entry::kReturnZero));
  rex::Runtime rt(dir.dir,dir.dir);rex::RuntimeConfig config{};
  config.tool_mode=true;config.kernel_init=rex::kernel::InitializeKernel;REQUIRE(rt.Setup(std::move(config))==0);
  RegisterServiceProbe(rt);SystemUiScope host(rt);
  REQUIRE(dash17559::SystemXamPath()=="game:\\system\\xam_ui.xex");
  for(auto* call : {__imp__XamUniSortCmpString,__imp__XamContentGetLocalizedString}) {
    service_called=false;image_mapped=false;auto ctx=Request();call(ctx,rt.memory()->virtual_membase());
    CHECK(service_called);CHECK(image_mapped);CHECK(ctx.r3.u32==77);
  }
  REQUIRE(rt.kernel_state()->GetModule(dash17559::SystemXamPath()));
  host.Close();CHECK(dash17559::SystemXamPath().empty());
}
