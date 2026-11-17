#pragma once
#include <rex/hid/input_driver.h>
#include <rex/driver_module.h>
#include <rex/driver_registration.h>
#include <chrono>
#include <array>
#include "system_ui.h"

namespace dash1888 {
// Explicit diagnostic source; the SDK generates keystrokes from these pad transitions.
// It opens no host device and is selected only by input_probe.
class GuideDiagnostic final : public rex::hid::IInputDriver {
 public:
  explicit GuideDiagnostic(bool close_with_b) : close_with_b_(close_with_b) {}
  std::string_view GetName() const override { return "dash1888-probe"; }
  rex::Result<void> Initialize(rex::IDriverContext&) override {
    start_ = std::chrono::steady_clock::now();
    return rex::Ok();
  }
  void Shutdown() override {}
  rex::u64 GetDeviceRevision() const override { return 1; }
  void EnumerateDevices(std::vector<rex::hid::PadDevice>& out) override {
    rex::hid::PadDevice device;
    device.id = {0x1888000047554944ull};
    device.name = "1888 diagnostic only";
    device.identity.xinput_slot = 0;
    out = {device};
  }
  rex::Result<rex::hid::PadState> GetDeviceState(rex::hid::PadDeviceID) override {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_).count() - 3000;
    return StateAt(elapsed, close_with_b_);
  }
  static rex::hid::PadState StateAt(int64_t elapsed, bool close_with_b) {
    rex::hid::PadState state;
    if (elapsed >= 0 && elapsed < 18000) {
      const auto phase = elapsed % 6000;
      if (phase < 222 || (!close_with_b && phase >= 3000 && phase < 3222))
        state.buttons = rex::hid::kPadGuide | rex::hid::kPadBack | rex::hid::kPadStart;
      else if (close_with_b && phase >= 3000 && phase < 3222)
        state.buttons = rex::hid::kPadB;
    }
    return state;
  }
  rex::Result<rex::hid::PadCapabilities> GetDeviceCapabilities(rex::hid::PadDeviceID) override {
    rex::hid::PadCapabilities caps;
    caps.supported.buttons = 0xFFFF;
    return caps;
  }
  rex::Result<void> SetDeviceVibration(rex::hid::PadDeviceID, const rex::hid::PadVibration&) override {
    return rex::Ok();
  }
 private:
  bool close_with_b_;
  std::chrono::steady_clock::time_point start_;
};
class GuideDiagnosticProvider final : public rex::IModule, public rex::IDriverModule {
 public:
  std::string_view GetName() const override { return "dash1888-diagnostic"; }
  rex::Result<void> Connect(rex::IInterfaceFactory&) override { return rex::Ok(); }
  void Disconnect() override {}
  rex::Result<void> Init() override { return rex::Ok(); }
  void Shutdown() override {}
  rex::IInterface* QueryInterface(std::string_view name, rex::u32 version) override {
    return rex::FindModuleInterface<rex::IDriverModule>(*this, name, version);
  }
  std::span<const rex::DriverRegistration> GetDrivers() const override { return drivers_; }
 private:
  static std::unique_ptr<rex::IDriver> Create() {
    return std::make_unique<GuideDiagnostic>(GetSystemUiSettings().input_probe.Get() == 2);
  }
  static constexpr std::array drivers_{
      rex::DriverRegistration{rex::DriverType::kInput, "dash1888-probe", {}, &Create}};
};
}
