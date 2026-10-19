// Headless reproduction of simultaneous title/HUD/capability polling while
// devices disappear and acquire new identities. No physical input or window.
#include <rex/input/input_system.h>
#include <atomic>
#include <barrier>
#include <chrono>
#include <iostream>
#include <thread>

using namespace rex::input;
class HotplugPad final : public InputDriver {
 public:
  HotplugPad() : InputDriver(nullptr, 0) {}
  rex::X_STATUS Setup() override { return 0; }
  void EnumerateDevices(std::vector<DeviceInfo>& out) override {
    const auto n = generation.fetch_add(1);
    for (unsigned i = 0; i < (n % 5); ++i)
      out.push_back({DeviceId(1 + (n / 5) * 4 + i), 0, "hotplug fixture", "", false});
  }
  rex::X_RESULT GetDeviceState(DeviceId, X_INPUT_STATE* out) override { *out = {}; return 0; }
  rex::X_RESULT GetDeviceCapabilities(DeviceId, uint32_t, X_INPUT_CAPABILITIES* out) override { *out = {}; return 0; }
  rex::X_RESULT SetDeviceVibration(DeviceId, X_INPUT_VIBRATION*) override { return 0; }
  rex::X_RESULT GetDeviceKeystroke(DeviceId, uint32_t, X_INPUT_KEYSTROKE*) override { return 4306; }
  std::atomic<unsigned> generation{0};
};
int main() {
  InputSystem input(nullptr);
  input.AddDriver(std::make_unique<HotplugPad>());
  input.SetDeviceAssignment(std::make_unique<SlotAssignment>());
  std::barrier start(4);
  std::array<std::thread, 4> pollers;
  std::atomic<unsigned> completed{0};
  std::cout << "Four input consumers; 20000 hotplug queries each\n" << std::flush;
  for (unsigned user = 0; user < 4; ++user) pollers[user] = std::thread([&, user] {
    start.arrive_and_wait();
    for (unsigned i = 0; i < 20000; ++i) {
      X_INPUT_STATE state{}; X_INPUT_CAPABILITIES caps{};
      X_INPUT_KEYSTROKE key{}; X_INPUT_VIBRATION vibration{};
      switch (user) {
        case 0: input.GetState(user, &state); break;
        case 1: input.GetCapabilities(user, 0, &caps); break;
        case 2: input.GetKeystroke(user, 0, &key); break;
        case 3: input.SetState(user, &vibration); break;
      }
      ++completed;
    }
  });
  for (auto& poller : pollers) poller.join();
  input.Shutdown();
  std::cout << "Completed " << completed << " queries\n";
  return completed == 80000 ? 0 : 1;
}
