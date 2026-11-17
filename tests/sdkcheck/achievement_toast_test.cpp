#include <catch2/catch_test_macros.hpp>
#include "ui/ui_harness.h"
#include "ui/ui_control.h"
#include "ui/overlay/achievement_toast.h"
#include "ui/overlay/console/model.h"
#include "ui/overlay/console_host.h"
#include <rex/logging/logger.h>
#include <rex/overlay_renderer.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <barrier>
#include <functional>
#include <mutex>
#include <set>
#include <thread>

namespace {
using namespace rex::ui;
using Clock = std::chrono::steady_clock;
// Real ImGui frames and dialog ownership with a recording CPU sink.
class DrawSink final : public rex::IOverlayRenderer {
 public:
  unsigned begins=0, draws=0;
  void RenderOverlays(ImDrawData& data) override {
    ++begins;
    for (auto* texture : *data.Textures) {
      if (texture->Status == ImTextureStatus_WantDestroy) texture->SetStatus(ImTextureStatus_Destroyed);
      else if (texture->Status != ImTextureStatus_OK) {
        texture->SetTexID(static_cast<ImTextureID>(1)); texture->SetStatus(ImTextureStatus_OK);
      }
    }
    if (data.TotalIdxCount) ++draws;
  }
};
struct Fixture {
  static IUISystem& Start(rex::test_ui::UIApp& app) {
    app.input_drivers="none"; REQUIRE(app.Start()); return app.GetUI();
  }
  rex::test_ui::UIApp app;
  IUISystem& ui=Start(app);
  DrawSink sink;
  rex::log::LogCaptureSink capture;
  ConsoleModel console{REX_SETTINGS(),capture,{}};
  ConsoleDockHost host{ui};
  rex::IAchievementRegistry* achievements=nullptr;
  FrameStatsProvider stats=[] {return rex::gpu::FrameStats{};};
  ModuleOverlayContext overlay{{ui,REX_SETTINGS()},console,host,capture,achievements,stats};
  int initial_wakes=rex::test_ui::g_ui.wake_calls;
  ~Fixture() {app.Shutdown();}
  void Frame() {ui.DrawOverlays(sink);}
  void Pump() {app.Pump();}
  int Wakes() const {return rex::test_ui::g_ui.wake_calls-initial_wakes;}
};
class Toast final : public AchievementToastDialog {
 public:
  explicit Toast(const ModuleOverlayContext& context) : AchievementToastDialog(context) {}
  Clock::time_point now{std::chrono::seconds(100)};
  std::string text;
  std::function<void()> after_draw;
 private:
  bool Draw(ImGuiIO& io) override {
    ImGui::LogToBuffer();
#if defined(X360N_TOAST_BASELINE)
    const bool keep=AchievementToastDialog::Draw(io);
#else
    const bool keep=DrawAt(io, now);
#endif
    // Read the vector directly: ImGuiTextBuffer::c_str() references a static
    // that the runtime DLL does not export, and ImGui must stay in that DLL.
    const auto& log = ImGui::GetCurrentContext()->LogBuffer.Buf;
    text = log.Size ? std::string(log.Data) : std::string();
    ImGui::LogFinish();
    if (after_draw) after_draw();
    return keep;
  }
};
class OrdinaryDialog final : public DialogBase {
 public:
  explicit OrdinaryDialog(IUISystem& ui) : registration_(ui.AddDialog(*this)) {}
  bool Draw(ImGuiIO&) override {
    ImGui::Begin("ordinary dialog"); ImGui::TextUnformatted("Other dialog"); ImGui::End(); return true;
  }
 private:
  rex::ScopeGuard registration_;
};
rex::Achievement Event(unsigned id) {
  rex::Achievement event{}; event.id=id; event.label="event["+std::to_string(id)+"]"; event.gamerscore=10; return event;
}

}

TEST_CASE("Idle achievement toast does not keep the ImGui repaint loop alive", "[sdk][achievement-toast]") {
  Fixture f;
  Toast toast(f.overlay);
  REQUIRE_FALSE(f.ui.HasDialogs());
  for (unsigned i = 0; i < 120; ++i) f.Frame();
  CHECK(f.sink.begins == 0);
  CHECK(f.Wakes() == 0);
  {
    OrdinaryDialog dialog(f.ui);
    CHECK(f.ui.HasDialogs());
    f.Frame();
    CHECK(f.sink.begins == 1);
  }
  CHECK_FALSE(f.ui.HasDialogs());
}

#if !defined(X360N_TOAST_BASELINE)
TEST_CASE("Concurrent achievement bursts wake once and every event gets its full display time", "[sdk][achievement-toast]") {
  Fixture f;
  Toast toast(f.overlay);
  constexpr unsigned threads = 4, per_thread = 8, count = threads * per_thread;
  std::barrier start(threads);
  std::vector<std::jthread> producers;
  for (unsigned t = 0; t < threads; ++t) producers.emplace_back([&, t] {
    start.arrive_and_wait();
    for (unsigned i = 0; i < per_thread; ++i) toast.Push(Event(t * per_thread + i));
  });
  producers.clear();
  CHECK_FALSE(f.ui.HasDialogs());
  CHECK(f.Wakes() == 1);
  f.Pump();
  REQUIRE(f.ui.HasDialogs());
  std::set<unsigned> displayed;
  for (unsigned i = 0; i < count; ++i) {
    REQUIRE(f.ui.HasDialogs());
    f.Frame();
    toast.now += std::chrono::milliseconds(300);
    f.Frame();
    INFO("iteration " << i << " visible text: " << toast.text);
    CHECK(toast.text.find("Achievement Unlocked") != std::string::npos);
    const auto begin = toast.text.find("event[");
    REQUIRE(begin != std::string::npos);
    const auto id = unsigned(std::stoul(toast.text.substr(begin + 6)));
    CHECK(displayed.insert(id).second);
    toast.now += std::chrono::milliseconds(4199);
    f.Frame();
    INFO("before expiry: " << toast.text);
    CHECK(f.ui.HasDialogs());
    CHECK(toast.text.find("event[" + std::to_string(id) + "]") != std::string::npos);
    toast.now += std::chrono::milliseconds(1);
    f.Frame();
  }
  CHECK(displayed.size() == count);
  CHECK_FALSE(f.ui.HasDialogs());
  CHECK(f.sink.draws > 0);
  const auto old_draws = f.sink.begins;
  for (unsigned i = 0; i < 120; ++i) f.Frame();
  CHECK(f.sink.begins == old_draws);
  CHECK(f.Wakes() == 1);
}

TEST_CASE("Toast shutdown cancels queued wakeups before the drawer is destroyed", "[sdk][achievement-toast]") {
  Fixture f;
  auto toast = std::make_unique<Toast>(f.overlay);
  std::jthread producer([&] { toast->Push(Event(1)); });
  producer.join();
  CHECK(f.Wakes() == 1);
  toast.reset();
  CHECK_FALSE(f.ui.HasDialogs());
  f.Pump();
  CHECK_FALSE(f.ui.HasDialogs());
  f.Frame();
  CHECK(f.sink.begins == 0);
}

TEST_CASE("An achievement arriving during toast detachment is not lost and other dialogs stay active", "[sdk][achievement-toast]") {
  Fixture f;
  Toast toast(f.overlay);
  auto other = std::make_unique<OrdinaryDialog>(f.ui);
  toast.Push(Event(1));
  f.Pump();
  f.Frame();
  toast.now += std::chrono::milliseconds(4500);
  bool pushed = false;
  toast.after_draw = [&] {
    if (pushed) return;
    pushed = true;
    std::jthread producer([&] { toast.Push(Event(2)); });
  };
  f.Frame();
  CHECK(f.ui.HasDialogs()); // The ordinary dialog remains attached.
  other.reset();
  CHECK_FALSE(f.ui.HasDialogs());
  f.Pump();
  REQUIRE(f.ui.HasDialogs());
  f.Frame();
  toast.now += std::chrono::milliseconds(300);
  f.Frame();
  CHECK(toast.text.find("event[2]") != std::string::npos);
  toast.now += std::chrono::milliseconds(4200);
  f.Frame();
  CHECK_FALSE(f.ui.HasDialogs());
  CHECK(f.Wakes() == 2);
}

TEST_CASE("Achievement callback ownership and destruction remain on the UI thread", "[sdk][achievement-toast]") {
  Fixture f;
  struct Registry final : rex::IAchievementRegistry {
    explicit Registry(IUISystem& owner) : ui(owner) {}
    std::vector<rex::Achievement> GetAchievements() override {return {};}
    rex::Result<std::vector<std::byte>> GetAchievementIcon(rex::u32) override {
      return rex::Err<std::vector<std::byte>>(rex::ErrorCategory::kNotFound,"No synthetic icon");
    }
    rex::ScopeGuard OnAchievementUnlocked(rex::MoveOnlyFunction<void(const rex::Achievement&)> fn) override {
      callback=std::move(fn);
      return rex::ScopeGuard([this] {std::lock_guard lock(mutex); removed=true; wrong_thread|=!ui.IsUIThread();callback=nullptr;});
    }
    void Emit(const rex::Achievement& event) {std::lock_guard lock(mutex);if(callback) callback(event);}
    IUISystem& ui;
    std::mutex mutex;
    rex::MoveOnlyFunction<void(const rex::Achievement&)> callback;
    bool removed=false,wrong_thread=false;
  } registry(f.ui);
  auto* control=f.app.GetUIControl(); REQUIRE(control);
  rex::ScopeGuard detach([&] {control->DetachTitle();});
  REQUIRE(control->AttachTitle({},registry));
  std::jthread producer([&] {registry.Emit(Event(1));}); producer.join();
  CHECK_FALSE(f.ui.HasDialogs()); f.Pump(); CHECK(f.ui.HasDialogs());
  std::jthread late([&] {registry.Emit(Event(2));}); late.join();
  control->DetachTitle();
  CHECK(registry.removed); CHECK_FALSE(registry.wrong_thread);
  f.Pump(); CHECK_FALSE(f.ui.HasDialogs());
  registry.Emit(Event(3)); f.Pump(); CHECK_FALSE(f.ui.HasDialogs());
}

#endif
