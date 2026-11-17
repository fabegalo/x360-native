#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>
#include "hid/input_harness.h"
#include "hid/input_system.h"
#include "ui/ui_harness.h"

TEST_CASE("Canonical input Stop serializes against an in-flight device query", "[sdk][hid][input-lifetime]") {
  using namespace std::chrono_literals;
  rex::test_input::Run("fake_a", [](rex::hid::IInputSystem& input) {
    using namespace rex::test_input;
    g_fake[0].devices = {MakePad(1, 0)};
    g_fake[0].states[1] = Buttons(rex::hid::kPadA);
    ++g_fake[0].revision;
    REQUIRE(input.GetState(0));
    std::mutex mutex;
    std::condition_variable condition;
    bool release = false;
    std::promise<void> entered, stop_started, stop_done;
    auto entered_future = entered.get_future(), started = stop_started.get_future(), done = stop_done.get_future();
    std::atomic<bool> reader_ok{false}, released_before_stop{false};
    g_fake[0].before_state = [&] {
      entered.set_value();
      std::unique_lock lock(mutex);
      condition.wait_for(lock, 2s, [&] { return release; });
      released_before_stop.store(release);
    };
    std::jthread reader([&] { reader_ok.store(input.GetState(0).has_value()); });
    const auto ready = entered_future.wait_for(2s);
    CHECK(ready == std::future_status::ready);
    std::jthread stopper([&] {
      stop_started.set_value();
      static_cast<rex::hid::InputSystem&>(input).Stop();
      stop_done.set_value();
    });
    CHECK(started.wait_for(2s) == std::future_status::ready);
    if (ready == std::future_status::ready) CHECK(done.wait_for(20ms) == std::future_status::timeout);
    { std::lock_guard lock(mutex); release = true; }
    condition.notify_all();
    reader.join(); stopper.join();
    g_fake[0].before_state = {};
    CHECK(reader_ok.load()); CHECK(released_before_stop.load());
    CHECK(done.wait_for(0ms) == std::future_status::ready);
    CHECK_FALSE(input.GetState(0));
    CHECK_FALSE(input.GetCapabilities(0));
    CHECK_FALSE(input.GetKeystroke(0, rex::hid::KeystrokeSource::kPad));
  });
}

TEST_CASE("Canonical input samples carry distinct generations across assignment and stop", "[sdk][hid][input-lifetime]") {
  rex::test_input::Run("fake_a", [](rex::hid::IInputSystem& input) {
    using namespace rex::test_input;
    g_fake[0].devices = {MakePad(1, 0)}; g_fake[0].states[1] = Buttons(0); ++g_fake[0].revision;
    const auto first = input.GetState(0); REQUIRE(first);
    const auto same = input.GetState(0); REQUIRE(same); CHECK(first->generation == same->generation);
    g_fake[0].devices = {MakePad(2, 0)}; g_fake[0].states.erase(1); g_fake[0].states[2] = Buttons(0); ++g_fake[0].revision;
    const auto replacement = input.GetState(0); REQUIRE(replacement);
    CHECK(replacement->generation != first->generation);
    CHECK(replacement->packet != first->packet);
  });
}

TEST_CASE("Focus listeners observe the current canonical UI state", "[sdk][hid][ui-focus-state]") {
  rex::test_ui::UIApp app;
  app.input_drivers = "none";
  REQUIRE(app.Start());
  struct Observer final : rex::ui::IUIEventListener {
    explicit Observer(rex::ui::IUISystem& value) : ui(value) {}
    void OnUIEvent(const rex::ui::UIEvent& event) override {
      if (event.GetType() == rex::ui::UIEvent::Type::kFocus) observed.push_back(ui.IsFocused());
    }
    void OnInputEvent(rex::InputEvent&) override {}
    rex::ui::IUISystem& ui;
    std::vector<bool> observed;
  } observer(app.GetUI());
  auto listener = app.GetUI().AddEventListener(observer);
  for (const bool focused : {false, true, false, true}) {
    const auto previous = observer.observed.size();
    rex::test_ui::DeliverFocus(focused); app.Pump();
    REQUIRE(observer.observed.size() == previous + 1);
    CHECK(observer.observed.back() == focused);
    CHECK(app.GetUI().IsFocused() == focused);
  }
  listener.Reset(); app.Shutdown();
}
