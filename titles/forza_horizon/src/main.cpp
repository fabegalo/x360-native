#include "forza_horizon_init.h"
#include <rex/rex_app.h>
#include <rex/logging.h>
#include "../../../src/supervisor/sha256.h"
#include "launch_handoff.h"
#include "guest_capture.h"

REXCVAR_DEFINE_STRING(x360n_guest_capture, "", "Diagnostics", "Capture actual GPU output every two seconds (thirty seconds by default)");
REXCVAR_DEFINE_UINT32(x360n_guest_capture_seconds, 30, "Diagnostics", "GPU capture duration, limited to 300 seconds");

class ImportedGameApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;
  bool ConstructRuntime(const rex::PathConfig& paths) override {
    const std::pair<const char*, const char*> originals[] = {
      {"default.xex", "db40df605ade49a612b35a7a24c38f6004bcb17a88ed6b48288de16df9e3987c"},
      {"XMediaFacade_default.xex", "ed285beefeafcdc90cf64d6085d19c874e3f1e15072c12cdf9ba2237c88e2ce5"},
      {"SpeechFacade_default.xex", "d54f6aae6ae2fd6da9b8c37d48f0a6bb295caa18ad82d0d9df66026141a41594"},
    };
    if (!paths.update_data_root.empty()) {
      REXLOG_ERROR("Forza 0.0.0.10: update overlays require their own verified AOT profile");
      return false;
    }
    for (const auto& [name, hash] : originals) {
      const auto digest = x360n::DigestFile(paths.game_data_root / name);
      if (!digest || digest->sha256 != hash) {
        REXLOG_ERROR("Forza 4D5309C9/2DC7007B/0.0.0.10: missing or mismatched {} (expected {})", name, hash);
        return false;
      }
    }
    return rex::ReXApp::ConstructRuntime(paths);
  }
  void OnPreLaunchModule() override {
    x360n::ImportLaunchData(*runtime());
    guest_capture_.Start(*runtime(), REXCVAR_GET(x360n_guest_capture), REXCVAR_GET(x360n_guest_capture_seconds));
  }
  void OnShutdown() override { guest_capture_.Stop(); }
  void OnGuestThreadExit(rex::system::XThread*) override { x360n::ExportLaunchRequest(*runtime()); }
  bool OnWindowCloseRequested() override { x360n::ExportWindowClose(); return true; }
  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<ImportedGameApp>(new ImportedGameApp(ctx, "forza_horizon", PPCImageConfig));
  }
 private:
  x360n::GuestCapture guest_capture_;
};
REX_DEFINE_APP(forza_horizon, ImportedGameApp::Create)
