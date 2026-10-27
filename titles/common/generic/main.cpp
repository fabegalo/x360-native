#include "@TITLE@_init.h"
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/logging.h>
#include "launch_handoff.h"
#include "guest_fault_context.h"

REXCVAR_DEFINE_STRING(x360n_guest_fault_context, "", "Diagnostics",
                      "Optional private file for bounded, non-handling guest fault context");

class ImportedGameApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;
  bool UseHostFunctionTable() const override { return @HOST_DISPATCH@ != 0; }
  void OnPreLaunchModule() override {
    x360n::ImportLaunchData(*runtime());
    const auto path = REXCVAR_GET(x360n_guest_fault_context);
    if (!path.empty() && !fault_context_.Start(*runtime()->memory(), path))
      REXLOG_ERROR("Could not create guest fault diagnostic at {}", path);
  }
  void OnGuestThreadExit(rex::system::XThread*) override { x360n::ExportLaunchRequest(*runtime()); }
  bool OnWindowCloseRequested() override { x360n::ExportWindowClose(); return true; }
  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<ImportedGameApp>(new ImportedGameApp(ctx, "@TITLE@", PPCImageConfig));
  }
 private:
  // ReXApp::OnDestroy joins guest execution and destroys the runtime before
  // this member is destroyed. OnShutdown is too early to remove the observer.
  x360n::GuestFaultContext fault_context_;
};
REX_DEFINE_APP(@TITLE@, ImportedGameApp::Create)
