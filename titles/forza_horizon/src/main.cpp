#include "forza_horizon_init.h"
#include <rex/rex_app.h>
#include "launch_handoff.h"

class ImportedGameApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;
  void OnPreLaunchModule() override { x360n::ImportLaunchData(*runtime()); }
  void OnGuestThreadExit(rex::system::XThread*) override { x360n::ExportLaunchRequest(*runtime()); }
  bool OnWindowCloseRequested() override { x360n::ExportWindowClose(); return true; }
  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<ImportedGameApp>(new ImportedGameApp(ctx, "forza_horizon", PPCImageConfig));
  }
};
REX_DEFINE_APP(forza_horizon, ImportedGameApp::Create)
