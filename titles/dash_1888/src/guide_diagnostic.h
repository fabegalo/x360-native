#pragma once
#include <rex/input/input_driver.h>
#include <rex/input/flags.h>
#include <rex/logging.h>
#include <chrono>
#include <deque>
#include <mutex>

namespace dash1888 {
// Explicit integrated input fixture. It opens no host device and is installed
// only by the opt-in diagnostic cvar. Normal sessions have no driver or timer.
class GuideDiagnostic final : public rex::input::InputDriver {
 public:
  explicit GuideDiagnostic(bool close_with_b) : InputDriver(nullptr, 0), close_with_b_(close_with_b) {}
  rex::X_STATUS Setup() override { return 0; }
  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override {
    out.push_back({rex::input::DeviceId(0x1888000047554944ull), 0, "1888 diagnostic only", "", true});
  }
  rex::X_RESULT GetDeviceState(rex::input::DeviceId, rex::input::X_INPUT_STATE* out) override {
    std::lock_guard lock(mutex_);
    const auto elapsed = Millis(); Pump(elapsed);
    *out = {}; out->packet_number = uint32_t(elapsed + 1);
    if (elapsed >= 0 && elapsed < 18000) {
      const auto phase = elapsed % 6000;
      if (phase < 222 || (!close_with_b_ && phase >= 3000 && phase < 3222))
        out->gamepad.buttons = rex::input::X_INPUT_GAMEPAD_GUIDE |
            rex::input::X_INPUT_GAMEPAD_BACK | rex::input::X_INPUT_GAMEPAD_START;
      else if (close_with_b_ && phase >= 3000 && phase < 3222)
        out->gamepad.buttons = rex::input::X_INPUT_GAMEPAD_B;
    }
    return 0;
  }
  rex::X_RESULT GetDeviceCapabilities(rex::input::DeviceId, uint32_t, rex::input::X_INPUT_CAPABILITIES* out) override {
    if (!out) return 87;
    *out = {}; out->type = 1; out->sub_type = 1; out->gamepad.buttons = 0xFFFF;
    return 0;
  }
  rex::X_RESULT SetDeviceVibration(rex::input::DeviceId, rex::input::X_INPUT_VIBRATION*) override { return 0; }
  rex::X_RESULT GetDeviceKeystroke(rex::input::DeviceId, uint32_t, rex::input::X_INPUT_KEYSTROKE* out) override {
    std::lock_guard lock(mutex_);
    Pump(Millis());
    if (keys_.empty()) return 4306;
    *out = keys_.front(); keys_.pop_front();
    REXLOG_INFO("1888 diagnostic input: vk={:04X} flags={:04X}",
                uint16_t(out->virtual_key), uint16_t(out->flags));
    return 0;
  }
 private:
  int64_t Millis() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_).count() - 3000;
  }
  void Pump(int64_t elapsed) {
    constexpr int times[] = {0, 12, 210, 222, 3000, 3012, 3210, 3222};
    while (next_ < 24 && elapsed >= int64_t(next_ / 8 * 6000 + times[next_ % 8])) {
      const auto event = next_++ % 8;
      if (close_with_b_ && (event == 5 || event == 7)) continue;
      rex::input::X_INPUT_KEYSTROKE stroke{};
      stroke.virtual_key = close_with_b_ && event >= 4 ? 0x5801 : (event % 2 ? 0x5814 : 0x5815);
      stroke.flags = event % 4 < 2 ? rex::input::X_INPUT_KEYSTROKE_KEYDOWN : rex::input::X_INPUT_KEYSTROKE_KEYUP;
      keys_.push_back(stroke);
    }
  }
  bool close_with_b_;
  std::mutex mutex_;
  unsigned next_ = 0;
  std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
  std::deque<rex::input::X_INPUT_KEYSTROKE> keys_;
};
}
