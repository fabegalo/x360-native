#include "forza_horizon_init.h"
#include <rex/rex_app.h>
#include <rex/ui/window.h>
#include <rex/logging.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/runtime.h>
#include "../../../src/supervisor/sha256.h"
#include "launch_handoff.h"
#include "guest_capture.h"
#include "enhanced_timing.h"

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
    x360n::forza::ConfigureEnhancedTiming();
    if (window())
      x360n::forza::ResizeEnhancedLayout(window()->GetActualPhysicalWidth(), window()->GetActualPhysicalHeight());
    utility_root_ = (paths.cache_root.empty() ? paths.user_data_root / "cache" : paths.cache_root) / "utility";
    return rex::ReXApp::ConstructRuntime(paths);
  }
  // The title records replay_stream and ghost_stream_N files on its utility
  // drive (cache:). On the console XMountUtilityDrive formats Harddisk0\Cache1
  // and links \??\cache:. The runtime only fakes that partition (NullDevice,
  // ~1 MB geometry): the 32 KB-cluster format fails with ERROR_UNRECOGNIZED_VOLUME,
  // no cache: link exists, and the replay constructor (sub_82C7E4F0) calls
  // through the empty stream handle. Back cache: with a persistent host folder,
  // as the Xenia reference does for \CACHE; the guest formatting itself remains
  // unimplemented. Upstream deliberately leaves cache: unmounted for titles
  // that mishandle a present device, so this stays specific to this title.
  bool MountUtilityDrive() {
    std::error_code error;
    std::filesystem::create_directories(utility_root_, error);
    auto device = std::make_unique<rex::filesystem::HostPathDevice>("\\CACHE", utility_root_, false);
    auto* fs = runtime()->file_system();
    if (error || !device->Initialize() || !fs->RegisterDevice(std::move(device)) ||
        !fs->RegisterSymbolicLink("cache:", "\\CACHE")) {
      REXLOG_ERROR("Forza 0.0.0.10: could not provide the utility drive cache: at {}", utility_root_.string());
      return false;
    }
    REXLOG_INFO("Forza 0.0.0.10: utility drive cache: -> {}", utility_root_.string());
    return true;
  }
  void OnPreLaunchModule() override {
    MountUtilityDrive();
    x360n::ImportLaunchData(*runtime());
    guest_capture_.Start(*runtime(), REXCVAR_GET(x360n_guest_capture), REXCVAR_GET(x360n_guest_capture_seconds));
  }
  void OnShutdown() override { guest_capture_.Stop(); }
  void OnWindowPixelSizeChanged(uint32_t width, uint32_t height) override {
    x360n::forza::ResizeEnhancedLayout(width, height);
  }
  void OnGuestThreadExit(rex::system::XThread*) override { x360n::ExportLaunchRequest(*runtime()); }
  bool OnWindowCloseRequested() override { x360n::ExportWindowClose(); return true; }
  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<ImportedGameApp>(new ImportedGameApp(ctx, "forza_horizon", PPCImageConfig));
  }
 private:
  x360n::GuestCapture guest_capture_;
  std::filesystem::path utility_root_;
};
REX_DEFINE_APP(forza_horizon, ImportedGameApp::Create)
