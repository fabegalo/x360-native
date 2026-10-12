// x360n_shutdown_probe: a minimal ReXApp for the teardown checks of
// patches/rexglue-sdk/0004 (shutdown after a successful setup) and 0002 (a failed
// setup), run by sdkcheck/app_shutdown_test.cpp.
//
// The test writes the synthetic XEX of sdkcheck/synthetic_xex.h (Entry::kReturnZero:
// `li r3,0; blr` at 0x82010000, nothing else) as default.xex into --game_data_root.
// The function below is that entry point as the codegen emits it, and the only
// mapping. The guest main thread returns at once, ReXApp quits by itself
// ("Execution complete") and OnDestroy tears down the window, input, graphics and
// runtime: the path on which dash_17559 crashed (docs/dashboard.md §0.7 B).
//
// --x360n_probe_fail_setup=true moves the image so that the function table crosses
// the end of the 0x90000000 XEX heap, as in sdkcheck/setup_failure_test.cpp:
// Runtime::Setup fails after the graphics system was handed over (patch 0002).
#include <memory>
#include <windows.h>
#include <thread>
#include <chrono>
#include <rex/logging.h>
#include <rex/ui/window.h>

#include <rex/cvar.h>
#include <rex/image_info.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/rex_app.h>

REXCVAR_DEFINE_BOOL(x360n_probe_fail_setup, false, "x360n",
                    "Shutdown probe: make Runtime::Setup fail after the graphics system was handed over");
REXCVAR_DEFINE_BOOL(x360n_probe_child_focus, false, "x360n",
                    "Check native focus against SDL on the probe's own child window");

// 0x82010000: li r3,0; blr
extern "C" REX_FUNC(x360n_probe_entry) {
  if (REXCVAR_GET(x360n_probe_child_focus))
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
  // li r3,0
  ctx.r3.s64 = 0;
  // blr
}

namespace {

PPCFuncMapping g_mappings[] = {{0x82010000, x360n_probe_entry}, {0, nullptr}};

rex::PPCImageInfo ImageInfo() {
  rex::PPCImageInfo info{};
  info.code_base = 0x82010000;
  info.code_size = 0x200;
  info.image_base = 0x82000000;
  info.image_size = 0x20000;
  info.func_mappings = g_mappings;
  if (REXCVAR_GET(x360n_probe_fail_setup)) {
    info.code_base = 0x9FFF0000;
    info.code_size = 0x1000;
    info.image_base = 0x9FFF0000;
    info.image_size = 0x8000;
  }
  return info;
}

class ProbeApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  void OnPreLaunchModule() override {
    if (!REXCVAR_GET(x360n_probe_child_focus)) return;
    auto child = static_cast<HWND>(window()->GetNativeWindowHandle());
    auto parent = GetAncestor(child, GA_ROOT);
    // Exercise only the synthetic probe's windows. No keyboard input is injected.
    SetForegroundWindow(parent);
    REXLOG_INFO("child-focus foreground root={}", GetForegroundWindow() == parent);
    REXLOG_INFO("child-focus handles: child={} root={} parent={} foreground={} style={:08X}",
        uintptr_t(child), uintptr_t(parent), uintptr_t(GetParent(child)),
        uintptr_t(GetForegroundWindow()), uint32_t(GetWindowLongPtrW(child, GWL_STYLE)));
    phase_ = 1;
    SetFocus(nullptr);
    SetFocus(child);
  }
  void OnWindowFocusChanged(bool focused) override {
    if (!phase_) return;
    auto child = static_cast<HWND>(window()->GetNativeWindowHandle());
    if (phase_ == 1 && focused) {
      REXLOG_INFO("child-focus gain: native={}, sdk={}", GetFocus() == child, window()->HasFocus());
      phase_ = 2;
      SetFocus(nullptr);
    } else if (phase_ == 2 && !focused) {
      REXLOG_INFO("child-focus loss: native={}, sdk={}", GetFocus() == nullptr, !window()->HasFocus());
      phase_ = 3;
      SetFocus(child);
    } else if (phase_ == 3 && focused) {
      REXLOG_INFO("child-focus regain: native={}, sdk={}", GetFocus() == child, window()->HasFocus());
      phase_ = 4;
    }
  }
  unsigned phase_ = 0;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<ProbeApp>(new ProbeApp(ctx, "x360n_shutdown_probe", ImageInfo()));
  }
};

}  // namespace

REX_DEFINE_APP(x360n_shutdown_probe, ProbeApp::Create)
