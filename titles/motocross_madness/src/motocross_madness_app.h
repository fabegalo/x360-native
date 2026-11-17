#pragma once
#include "../../common/session_title_app.h"

namespace motocross {
rex::Result<void> RegisterXamAotProbeSettings(rex::SettingsRegistry& registry);
void UnregisterXamAotProbeSettings(rex::SettingsRegistry& registry);
}

class MotocrossMadnessApp : public x360n::SessionTitleApp {
 public:
  MotocrossMadnessApp() : SessionTitleApp("motocross_madness", kPPCImageConfig) {}
  ~MotocrossMadnessApp() override { Shutdown(); }

 protected:
  rex::Result<void> OnConfigure(rex::SettingsRegistry& settings) override {
    REX_TRY_VOID(SessionTitleApp::OnConfigure(settings));
    REX_TRY_VOID(motocross::RegisterXamAotProbeSettings(settings));
    probe_settings_ = rex::ScopeGuard([&settings] { motocross::UnregisterXamAotProbeSettings(settings); });
    return rex::Ok();
  }
  void OnRelease() override {
    SessionTitleApp::OnRelease();
    probe_settings_.Reset();
  }

 private:
  rex::ScopeGuard probe_settings_;
};
