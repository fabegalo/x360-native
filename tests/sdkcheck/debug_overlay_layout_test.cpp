// Titles compile rex_app.cpp, which allocates the debug overlay (F3), with
// their own flags and without the SDK's REXGLUE_ENABLE_PERF_COUNTERS, while
// rexruntime builds the overlay's constructor with it. Reproduce that title
// translation unit here.
#undef REXGLUE_ENABLE_PERF_COUNTERS
#include <catch2/catch_test_macros.hpp>
#include <rex/ui/overlay/debug_overlay.h>
#include <rex/ui/windowed_app_context.h>
#include <algorithm>
#include <cstddef>
#include <memory>
#include <new>

namespace {
using namespace rex::ui;
class Context final : public WindowedAppContext {
 private:
  void NotifyUILoopOfPendingFunctions() override {}
  void PlatformQuitFromUIThread() override {}
};
class TestWindow final : public Window {
 public:
  explicit TestWindow(Context& context) : Window(context, "debug overlay layout", 1280, 720) {}
  ~TestWindow() override { EnterDestructor(); }
 private:
  bool OpenImpl() override {
    WindowDestructionReceiver receiver(this);
    OnActualSizeUpdate(1280, 720, receiver);
    return true;
  }
  void RequestCloseImpl() override {
    WindowDestructionReceiver receiver(this); OnBeforeClose(receiver); OnAfterClose();
  }
  std::unique_ptr<Surface> CreateSurfaceImpl(Surface::TypeFlags) override { return {}; }
  void RequestPaintImpl() override {}
};
class Texture final : public ImmediateTexture {
 public:
  Texture(uint32_t width, uint32_t height) : ImmediateTexture(width, height) {}
};
class DrawSink final : public ImmediateDrawer {
 public:
  std::unique_ptr<ImmediateTexture> CreateTexture(uint32_t w, uint32_t h,
      ImmediateTextureFilter, bool, const uint8_t*) override { return std::make_unique<Texture>(w, h); }
  void BeginDrawBatch(const ImmediateDrawBatch&) override {}
  void Draw(const ImmediateDraw&) override {}
  void EndDrawBatch() override {}
};
}  // namespace

TEST_CASE("The debug overlay stays within the storage a title allocates for it",
          "[sdk][debug-overlay]") {
  Context context;
  TestWindow window{context};
  DrawSink sink;
  ImGuiDrawer drawer{&window, 64};
  REQUIRE(window.Open());
  drawer.SetImmediateDrawer(&sink);
  // F3 heap-corrupted the 17559 dashboard (0xC0000374 in the next driver free)
  // because the constructor zeroed members beyond the title's allocation.
  constexpr size_t kSize = sizeof(DebugOverlayDialog);
  constexpr size_t kCanary = 1024;
  alignas(std::max_align_t) static std::byte storage[kSize + kCanary];
  for (int toggle = 0; toggle < 3; ++toggle) {
    std::fill(std::begin(storage), std::end(storage), std::byte{0xCD});
    auto* overlay = new (storage) DebugOverlayDialog(&drawer, [] { return FrameStats{16.6, 60.0, 1}; });
    for (int frame = 0; frame < 3; ++frame) {
      AppUIDrawContext draw(1280, 720);
      drawer.Draw(draw);
    }
    overlay->~DebugOverlayDialog();
    CHECK(std::all_of(storage + kSize, std::end(storage), [](std::byte b) { return b == std::byte{0xCD}; }));
  }
  drawer.SetImmediateDrawer(nullptr);
  window.RequestClose();
}
