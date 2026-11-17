#include <catch2/catch_test_macros.hpp>
#include "kernel/kernel_fixture.h"
#include "kernel/test_access.h"
#include "kernel/title_state.h"
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/function_dispatcher.h>
#include <rex/kernel/module.h>
#include <rex/ppc/recompiled_code.h>
#include <rex/settings_registry.h>
#include "synthetic_xex.h"
#include "../../titles/dash_17559/src/system_ui.h"

extern "C" REX_FUNC(__imp__XamUniSortCmpString);
extern "C" REX_FUNC(__imp__XamContentGetLocalizedString);

namespace {
struct SystemUiScope {
  struct Setting {rex::SettingBase* setting; std::string value; rex::SettingLayer layer;};
  std::vector<Setting> settings;
  bool installed=true;
  rex::kernel::TitleRuntime& runtime;
  explicit SystemUiScope(rex::kernel::TitleRuntime& rt) : runtime(rt) {
    for (const auto* name : {"hid.guide_button", "hid.guide_chord_ms", "hid.guide_chord_users", "hid.keybind_guide"}) {
      auto setting = REX_SETTINGS().Find(name); REQUIRE(setting);
      settings.push_back({*setting, (*setting)->GetText(), (*setting)->GetLayer()});
    }
    dash17559::InstallSystemUi(rt);
  }
  void Close() {
    if(!installed)return;
    installed=false;dash17559::ReleaseSystemUiHost(runtime);
    for(const auto& setting:settings) {
      CHECK(setting.setting->Reset());
      if (setting.layer != rex::SettingLayer::kDefault)
        CHECK(setting.setting->SetText(setting.layer, setting.value));
    }
  }
  ~SystemUiScope(){Close();}
};
bool service_called=false, image_mapped=false;
void ServiceProbe(PPCContext& ctx,uint8_t*) {
  service_called=true;
  image_mapped=bool(REX_RUNTIME()->GetModuleRegistry().GetModule(dash17559::SystemXamPath()));
  CHECK(ctx.r3.u32==0x1234);CHECK(ctx.r4.u32==0x5678);
  CHECK(ctx.r5.u32==15);CHECK(ctx.r6.u32==7);
  ctx.r3.u64=77;
}
void RegisterServiceProbe(rex::kernel::TitleRuntime& rt) {
  auto* dispatcher=&rex::kernel::GetFunctionDispatcher(rt);
  REQUIRE(dispatcher->InitializeFunctionTable(0x816E0000,0x10000,0x816E0000,0x10000,false,true));
  REQUIRE(dispatcher->SetFunction(0x816E35A8,ServiceProbe));
  REQUIRE(dispatcher->SetFunction(0x816EE090,ServiceProbe));
}

}

TEST_CASE("Original storage services refuse dispatch when their image cannot load","[sdk][original-xam-service]") {
  x360n::test::Workdir dir("x360n-original-service","missing-image");
  dir.write("anchor.xex",x360n::test::BuildXex({},nullptr,0,x360n::test::Entry::kReturnZero,0x83000000));
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, dir.dir);
  auto& rt = fixture.GetRuntime();
  REQUIRE(rex::kernel::StateOf(rt).LoadXEXImage("game:\\anchor.xex"));
  RegisterServiceProbe(rt);
  SystemUiScope host(rt);
  for(auto* call : {__imp__XamUniSortCmpString,__imp__XamContentGetLocalizedString}) {
    service_called=false;
    fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
    auto& ctx=caller.GetContext(); ctx.r3.u64=0x1234;ctx.r4.u64=0x5678;ctx.r5.u64=15;ctx.r6.u64=7;call(ctx,caller.GetBase());
    CHECK_FALSE(service_called);CHECK(ctx.r3.u32==0x80004001);
    CHECK(ctx.r4.u32==0x5678);CHECK(ctx.r5.u32==15);CHECK(ctx.r6.u32==7);
    });
  }
}

TEST_CASE("Original storage services load the selected XAM before invoking its collation and localization",
          "[sdk][original-xam-service]") {
  x360n::test::Workdir dir("x360n-original-service","selected-image");
  std::filesystem::create_directories(dir.dir/"system");
  dir.write("system/xam_ui.xex",x360n::test::BuildXex({},nullptr,0,x360n::test::Entry::kReturnZero));
  dir.write("anchor.xex",x360n::test::BuildXex({},nullptr,0,x360n::test::Entry::kReturnZero,0x83000000));
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, dir.dir);
  auto& rt = fixture.GetRuntime();
  REQUIRE(rex::kernel::StateOf(rt).LoadXEXImage("game:\\anchor.xex"));
  RegisterServiceProbe(rt);SystemUiScope host(rt);
  REQUIRE(dash17559::SystemXamPath()=="game:\\system\\xam_ui.xex");
  for(auto* call : {__imp__XamUniSortCmpString,__imp__XamContentGetLocalizedString}) {
    service_called=false;image_mapped=false;
    fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
    auto& ctx=caller.GetContext(); ctx.r3.u64=0x1234;ctx.r4.u64=0x5678;ctx.r5.u64=15;ctx.r6.u64=7;call(ctx,caller.GetBase());
    CHECK(service_called);CHECK(image_mapped);CHECK(ctx.r3.u32==77);
    });
  }
  REQUIRE(rt.GetModuleRegistry().GetModule(dash17559::SystemXamPath()));
  host.Close();CHECK(dash17559::SystemXamPath().empty());
}
