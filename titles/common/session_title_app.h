#pragma once

#include <rex/app/title_app.h>
#include <rex/ui/ui_system.h>
#include "guest_capture.h"
#include "guest_fault_context.h"
#include "launch_handoff.h"

namespace x360n {
// Shared host hooks only; TitleApp owns the single runtime and its services.
class SessionTitleApp : public rex::app::TitleApp, private rex::ui::IUIEventListener {
 public:
  using TitleApp::TitleApp;
  ~SessionTitleApp() override { Shutdown(); }

 protected:
  rex::Result<void> OnConfigure(rex::SettingsRegistry& settings) override {
    REX_TRY_VOID(TitleApp::OnConfigure(settings));
    REX_TRY_VOID(RegisterLaunchSettings(settings));
    launch_settings_ = rex::ScopeGuard([&settings] { settings.Unregister(GetLaunchSettings()); });
    return rex::Ok();
  }
  void OnModules(std::vector<std::string>& modules) override {
    TitleApp::OnModules(modules);
#if X360N_HAS_NATIVE_GPU
    modules.push_back("rexgpu-native");
#endif
  }
  rex::Result<void> OnSettingsLoaded(rex::SettingsRegistry& settings) override {
    REX_TRY_VOID(TitleApp::OnSettingsLoaded(settings));
    return ConfigureSessionUi(settings);
  }
  rex::Result<void> OnInit() override {
    REX_TRY_VOID(TitleApp::OnInit());
    window_events_ = GetUI().AddEventListener(*this);
    return rex::Ok();
  }
  rex::Result<void> OnTitleLaunch(rex::kernel::TitleRuntime& runtime) override {
    title_unload_ = runtime.OnTitleUnload([this] { OnSessionUnload(GetTitleRuntime()); });
    REX_TRY_VOID(OnSessionLaunch(runtime));
    ImportLaunchData(runtime, GetSystemXamPath());
    // Export while XAM still owns launch data, before UI shutdown can unload it.
    title_exit_ = runtime.OnTitleExit([this](rex::kernel::TitleExitReason) {
      ExportLaunchRequest(GetTitleRuntime());
      rex::app::RequestShutdown(0);
    });
    const auto& settings = GetLaunchSettings();
    const auto path = settings.guest_fault_context.Get();
    if (!path.empty() && !fault_context_.Start(runtime, path))
      REXLOG_ERROR("Could not create guest fault diagnostic at {}", path);
    guest_capture_.Start(GetGraphics(), settings.guest_capture.Get(), settings.guest_capture_seconds.Get());
    return rex::Ok();
  }
  virtual std::string GetSystemXamPath() const { return {}; }
  virtual rex::Result<void> OnSessionLaunch(rex::kernel::TitleRuntime&) { return rex::Ok(); }
  virtual void OnSessionUnload(rex::kernel::TitleRuntime& runtime) {
    ReleaseSessionUi(runtime);
    guest_capture_.Stop();
    fault_context_.Stop();
  }
  void OnRun() override {
    TitleApp::OnRun();
    auto& runtime = GetTitleRuntime();
    if (runtime.IsLoaded()) {
      runtime.GetTitleExit().TerminateTitle();
      ExportLaunchRequest(runtime);
    }
    // The graphics service still owns its driver here, before TitleApp::OnStop.
    guest_capture_.Stop();
  }
  void OnRelease() override {
    UnconfigureSessionUi(REX_SETTINGS());
    window_events_.Reset();
    title_exit_.Reset();
    title_unload_.Reset();
    guest_capture_.Stop();
    fault_context_.Stop();
    TitleApp::OnRelease();
    launch_settings_.Reset();
  }

 private:
  void OnUIEvent(const rex::ui::UIEvent& event) override {
    if (event.GetType() == rex::ui::UIEvent::Type::kClose) ExportWindowClose();
  }
  void OnInputEvent(rex::InputEvent&) override {}
  GuestCapture guest_capture_;
  GuestFaultContext fault_context_;
  rex::ScopeGuard window_events_;
  rex::ScopeGuard title_exit_;
  rex::ScopeGuard title_unload_;
  rex::ScopeGuard launch_settings_;
};
}  // namespace x360n
