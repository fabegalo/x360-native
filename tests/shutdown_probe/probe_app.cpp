// Exercises real TitleApp/SDL/graphics teardown with a synthetic returning title.
#include <windows.h>
#include <chrono>
#include <thread>
#include <rex/app/title_app.h>
#include <rex/kernel/title_exit.h>
#include <rex/ppc/recompiled_code.h>
#include <rex/ppc/context.h>
#include <rex/ui/ui_system.h>
#include <rex/logging/logger.h>

namespace {
bool child_focus = false;
void Entry(PPCContext& ctx, uint8_t*) {
  if (child_focus) std::this_thread::sleep_for(std::chrono::milliseconds(1500));
  ctx.r3.u64 = 0;
}
PPCFuncMapping mappings[] = {{0x82010000, Entry}, {0, nullptr}};
rex::ppc::PPCImage Image(bool fail) {
  if (fail) return {0x9FFF0000, 0x1000, 0x9FFF0000, 0x8000, mappings};
  return {0x82010000, 0x200, 0x82000000, 0x20000, mappings};
}
class ProbeApp final : public rex::app::TitleApp, public rex::ui::IUIEventListener {
 public:
  explicit ProbeApp(bool fail, bool no_gpu) : TitleApp("x360n_shutdown_probe", Image(fail), false), no_gpu_(no_gpu) {}
 protected:
  rex::AppSysRequirements GetRequirements() const override {
    auto needs = TitleApp::GetRequirements();
    if (no_gpu_) needs.graphics_family = rex::GraphicsFamily::kNone;
    return needs;
  }
  void ReportError(std::string_view message) override { rex::app::App::ReportError(message); }
  rex::Result<void> OnTitleLaunch(rex::kernel::TitleRuntime&) override {
    if (!child_focus) return rex::Ok();
    const auto native = GetUI().GetNativeWindow();
    if (!native) return std::unexpected(native.error());
    child_ = static_cast<HWND>(native->window);
    listener_ = GetUI().AddEventListener(*this);
    auto parent = GetAncestor(child_, GA_ROOT);
    SetForegroundWindow(parent);
    REXLOG_INFO("child-focus foreground root={}", GetForegroundWindow() == parent);
    phase_ = 1;
    SetFocus(nullptr);
    SetFocus(child_);
    return rex::Ok();
  }
  void OnUIEvent(const rex::ui::UIEvent& event) override {
    if (!phase_ || event.GetType() != rex::ui::UIEvent::Type::kFocus) return;
    const bool focused = GetUI().IsFocused();
    if (phase_ == 1 && focused) {
      REXLOG_INFO("child-focus gain: native={}, sdk={}", GetFocus() == child_, focused);
      phase_ = 2; SetFocus(nullptr);
    } else if (phase_ == 2 && !focused) {
      REXLOG_INFO("child-focus loss: native={}, sdk={}", GetFocus() == nullptr, !focused);
      phase_ = 3; SetFocus(child_);
    } else if (phase_ == 3 && focused) {
      REXLOG_INFO("child-focus regain: native={}, sdk={}", GetFocus() == child_, focused);
      phase_ = 4;
    }
  }
  void OnInputEvent(rex::InputEvent&) override {}
  void OnTitleExit(rex::kernel::TitleExitReason reason) override {
    if (reason == rex::kernel::TitleExitReason::kNormalExit) {
      REXLOG_INFO("Execution complete");
      rex::app::RequestShutdown(0);
    } else TitleApp::OnTitleExit(reason);
  }
  void OnShutdown() override { listener_.Reset(); TitleApp::OnShutdown(); }
 private:
  bool no_gpu_;
  rex::ScopeGuard listener_;
  HWND child_ = nullptr;
  unsigned phase_ = 0;
};
int Run(const rex::CLIArgList& args) {
  const bool fail = args.Get("x360n_probe_fail_setup") == "true";
  child_focus = args.Get("x360n_probe_child_focus") == "true";
  std::vector<std::string> forwarded;
  for (const auto& option : args.GetOptions()) {
    if (option.name == "x360n_probe_fail_setup" || option.name == "x360n_probe_child_focus") continue;
    forwarded.push_back("--" + option.name + (option.value ? "=" + *option.value : ""));
  }
  ProbeApp app(fail, args.Get("gpu.driver") == "none");
  if (app.Init(rex::CLIArgList::Parse(forwarded))) {
    if (app.LaunchTitle()) app.Run();
    else REXLOG_ERROR("Runtime setup failed");
  }
  return app.Shutdown();
}
}
REX_DETAIL_ENTRY_POINTS(Run)
