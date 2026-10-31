#include <catch2/catch_test_macros.hpp>
#include <rex/ui/overlay/achievement_toast.h>
#include <rex/ui/windowed_app_context.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <atomic>
#include <barrier>
#include <functional>
#include <set>
#include <thread>

namespace {
using namespace rex::ui;
using Clock = std::chrono::steady_clock;
class Context final : public WindowedAppContext {
 public:
  std::atomic<unsigned> notifications{0};
 private:
  void NotifyUILoopOfPendingFunctions() override { ++notifications; }
  void PlatformQuitFromUIThread() override {}
};
class TestWindow final : public Window {
 public:
  explicit TestWindow(Context& context) : Window(context, "toast contract", 1280, 720) {}
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
// Real ImGui frames/dialog lists, with a recording CPU sink instead of an OS
// window or GPU. No original dashboard content or game is needed by this test.
class DrawSink final : public ImmediateDrawer {
 public:
  unsigned begins = 0, draws = 0;
  std::unique_ptr<ImmediateTexture> CreateTexture(uint32_t w, uint32_t h,
      ImmediateTextureFilter, bool, const uint8_t*) override { return std::make_unique<Texture>(w, h); }
  void Begin(UIDrawContext& context, float w, float h) override {
    ImmediateDrawer::Begin(context, w, h); ++begins;
  }
  void BeginDrawBatch(const ImmediateDrawBatch&) override {}
  void Draw(const ImmediateDraw& draw) override { if (draw.count) ++draws; }
  void EndDrawBatch() override {}
};
struct Fixture {
  Context context;
  TestWindow window{context};
  DrawSink sink;
  ImGuiDrawer drawer{&window, 64};
  Fixture() { REQUIRE(window.Open()); drawer.SetImmediateDrawer(&sink); }
  ~Fixture() { drawer.SetImmediateDrawer(nullptr); window.RequestClose(); }
  void Frame() { AppUIDrawContext frame(1280, 720); drawer.Draw(frame); }
};
class Toast final : public AchievementToastDialog {
 public:
  explicit Toast(ImGuiDrawer& drawer) : AchievementToastDialog(&drawer, nullptr, nullptr) {}
  Clock::time_point now{std::chrono::seconds(100)};
  std::string text;
  std::function<void()> after_draw;
 private:
  void OnDraw(ImGuiIO& io) override {
    ImGui::LogToBuffer();
#if defined(X360N_TOAST_BASELINE)
    AchievementToastDialog::OnDraw(io);
#else
    DrawAt(io, now);
#endif
    // Read the vector directly: ImGuiTextBuffer::c_str() references a static
    // that the runtime DLL does not export, and ImGui must stay in that DLL.
    const auto& log = ImGui::GetCurrentContext()->LogBuffer.Buf;
    text = log.Size ? std::string(log.Data) : std::string();
    ImGui::LogFinish();
    if (after_draw) after_draw();
  }
};
class OrdinaryDialog final : public ImGuiDialog {
 public:
  explicit OrdinaryDialog(ImGuiDrawer& drawer) : ImGuiDialog(&drawer) {}
};
rex::system::AchievementEvent Event(unsigned id) {
  rex::system::AchievementEvent event;
  event.achievement.id = id;
  event.achievement.label = "event[" + std::to_string(id) + "]";
  event.achievement.gamerscore = 10;
  return event;
}
}

TEST_CASE("Idle achievement toast does not keep the ImGui repaint loop alive", "[sdk][achievement-toast]") {
  Fixture f;
  Toast toast(f.drawer);
  REQUIRE_FALSE(f.drawer.HasDialogs());
  for (unsigned i = 0; i < 120; ++i) f.Frame();
  CHECK(f.sink.begins == 0);
  CHECK(f.context.notifications == 0);
  {
    OrdinaryDialog dialog(f.drawer);
    CHECK(f.drawer.HasDialogs());
    f.Frame();
    CHECK(f.sink.begins == 1);
  }
  CHECK_FALSE(f.drawer.HasDialogs());
}

#if !defined(X360N_TOAST_BASELINE)
TEST_CASE("Concurrent achievement bursts wake once and every event gets its full display time", "[sdk][achievement-toast]") {
  Fixture f;
  Toast toast(f.drawer);
  constexpr unsigned threads = 4, per_thread = 8, count = threads * per_thread;
  std::barrier start(threads);
  std::vector<std::jthread> producers;
  for (unsigned t = 0; t < threads; ++t) producers.emplace_back([&, t] {
    start.arrive_and_wait();
    for (unsigned i = 0; i < per_thread; ++i) toast.Push(Event(t * per_thread + i));
  });
  producers.clear();
  CHECK_FALSE(f.drawer.HasDialogs());
  CHECK(f.context.notifications == 1);
  f.context.ExecutePendingFunctionsFromUIThread();
  REQUIRE(f.drawer.HasDialogs());
  std::set<unsigned> displayed;
  for (unsigned i = 0; i < count; ++i) {
    REQUIRE(f.drawer.HasDialogs());
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
    CHECK(f.drawer.HasDialogs());
    CHECK(toast.text.find("event[" + std::to_string(id) + "]") != std::string::npos);
    toast.now += std::chrono::milliseconds(1);
    f.Frame();
  }
  CHECK(displayed.size() == count);
  CHECK_FALSE(f.drawer.HasDialogs());
  CHECK(f.sink.draws > 0);
  const auto old_draws = f.sink.begins;
  for (unsigned i = 0; i < 120; ++i) f.Frame();
  CHECK(f.sink.begins == old_draws);
  CHECK(f.context.notifications == 1);
}

TEST_CASE("Toast shutdown cancels queued wakeups before the drawer is destroyed", "[sdk][achievement-toast]") {
  Fixture f;
  auto toast = std::make_unique<Toast>(f.drawer);
  std::jthread producer([&] { toast->Push(Event(1)); });
  producer.join();
  CHECK(f.context.notifications == 1);
  toast.reset();
  CHECK_FALSE(f.drawer.HasDialogs());
  f.context.ExecutePendingFunctionsFromUIThread();
  CHECK_FALSE(f.drawer.HasDialogs());
  f.Frame();
  CHECK(f.sink.begins == 0);
}

TEST_CASE("An achievement arriving during toast detachment is not lost and other dialogs stay active", "[sdk][achievement-toast]") {
  Fixture f;
  Toast toast(f.drawer);
  auto other = std::make_unique<OrdinaryDialog>(f.drawer);
  toast.Push(Event(1));
  f.context.ExecutePendingFunctionsFromUIThread();
  f.Frame();
  toast.now += std::chrono::milliseconds(4500);
  bool pushed = false;
  toast.after_draw = [&] {
    if (pushed) return;
    pushed = true;
    std::jthread producer([&] { toast.Push(Event(2)); });
  };
  f.Frame();
  CHECK(f.drawer.HasDialogs()); // The ordinary dialog remains attached.
  other.reset();
  CHECK_FALSE(f.drawer.HasDialogs());
  f.context.ExecutePendingFunctionsFromUIThread();
  REQUIRE(f.drawer.HasDialogs());
  f.Frame();
  toast.now += std::chrono::milliseconds(300);
  f.Frame();
  CHECK(toast.text.find("event[2]") != std::string::npos);
  toast.now += std::chrono::milliseconds(4200);
  f.Frame();
  CHECK_FALSE(f.drawer.HasDialogs());
  CHECK(f.context.notifications == 2);
}

TEST_CASE("Achievement callback ownership and destruction remain on the UI thread", "[sdk][achievement-toast]") {
  Fixture f;
  struct Notification final : AchievementNotificationDialog {
    bool& pushed; bool& destroyed; bool& wrong_thread;
    WindowedAppContext& context;
    Notification(Fixture& f, bool& p, bool& d, bool& w)
        : AchievementNotificationDialog(&f.drawer), pushed(p), destroyed(d), wrong_thread(w), context(f.context) {}
    ~Notification() override { destroyed = true; wrong_thread |= !context.IsInUIThread(); }
    void Push(const rex::system::AchievementEvent&) override { pushed = true; wrong_thread |= !context.IsInUIThread(); }
  };
  bool pushed = false, destroyed = false, wrong_thread = false;
  auto dialog = std::make_shared<Notification>(f, pushed, destroyed, wrong_thread);
  const auto callback = AchievementNotificationDialog::MakeUICallback(f.context, dialog);
  std::jthread producer([&] { callback(Event(1)); });
  producer.join();
  CHECK_FALSE(pushed);
  f.context.ExecutePendingFunctionsFromUIThread();
  CHECK(pushed);
  pushed = false;
  std::jthread late_producer([&] { callback(Event(2)); });
  late_producer.join();
  dialog.reset();
  CHECK(destroyed);
  f.context.ExecutePendingFunctionsFromUIThread();
  CHECK_FALSE(pushed);
  CHECK_FALSE(wrong_thread);
  CHECK_FALSE(f.drawer.HasDialogs());
}
#endif
