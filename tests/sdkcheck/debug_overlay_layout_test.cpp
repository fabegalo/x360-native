// The caller disables instrumentation while the SDK UI constructor uses its own build flags.
#undef REXGLUE_ENABLE_PERF_COUNTERS
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <new>
#include <rex/logging/logger.h>
#include <rex/settings_registry.h>
#include "ui/ui_harness.h"
#include "ui/overlay/debug_overlay.h"
#include "ui/overlay/overlay_context.h"
#include "ui/overlay/console/model.h"
#include "ui/overlay/console_host.h"

TEST_CASE("The debug overlay stays within the storage a title allocates for it",
          "[sdk][debug-overlay]") {
  rex::test_ui::UIApp app;
  REQUIRE(app.Start());
  rex::log::LogCaptureSink capture;
  rex::ui::ConsoleModel console(REX_SETTINGS(), capture, {});
  rex::ui::ConsoleDockHost host(app.GetUI());
  rex::IAchievementRegistry* achievements = nullptr;
  rex::ui::FrameStatsProvider stats = [] { return rex::gpu::FrameStats{16.6, 60.0, 1}; };
  rex::ui::ModuleOverlayContext context{{app.GetUI(), REX_SETTINGS()}, console, host, capture,
                                       achievements, stats};
  using rex::ui::DebugOverlayDialog;
  constexpr size_t kSize = sizeof(DebugOverlayDialog), kCanary = 1024;
  alignas(DebugOverlayDialog) std::array<std::byte, kSize + kCanary> storage;
  for (int toggle = 0; toggle < 3; ++toggle) {
    storage.fill(std::byte{0xCD});
    auto* overlay = new (storage.data()) DebugOverlayDialog(context);
    {
      auto registration = app.GetUI().AddDialog(*overlay);
      for (int frame = 0; frame < 3; ++frame) REQUIRE(app.Pump());
    }
    overlay->~DebugOverlayDialog();
    CHECK(std::all_of(storage.begin() + kSize, storage.end(), [](std::byte b) {
      return b == std::byte{0xCD};
    }));
  }
  REQUIRE(app.Shutdown() == 0);
}
