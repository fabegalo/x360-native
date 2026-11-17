#pragma once
// Existing GPU diagnostic host, shared by preflight and Native replay.
#include <rex/app/app.h>
#include <rex/apu/audio_system.h>
#include <rex/cli.h>
#include <rex/driver_context.h>
#include <rex/driver_module.h>
#include <rex/gpu/graphics_driver.h>
#include <rex/gpu/graphics_system.h>
#include <rex/hid/input_system.h>
#include <rex/ui/ui_system.h>
#include <rex/utility.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
namespace x360n {
class ProbeServices final : public rex::IModule {
 public:
  std::string_view GetName() const override { return "x360n_gpu_probe"; }
  rex::Result<void> Connect(rex::IInterfaceFactory& factory) override { factory_ = &factory; return rex::Ok(); }
  void Disconnect() override { if (release_) release_(); factory_ = nullptr; }
  rex::Result<void> Init() override { return rex::Ok(); }
  void Shutdown() override {}
  rex::IInterface* QueryInterface(std::string_view, rex::u32) override { return nullptr; }
  rex::IInterfaceFactory* factory_ = nullptr;
  rex::MoveOnlyFunction<void()> release_;
};
class GPUProbeApp final : public rex::app::App {
 public:
  GPUProbeApp(std::filesystem::path scratch, std::string module, std::string selected, rex::IModule* linked_provider = nullptr)
      : App("x360n-gpu-preflight"), scratch_(std::move(scratch)), module_(std::move(module)), selected_(std::move(selected)), linked_provider_(linked_provider) {
    services_.release_ = [this] { ReleaseProbeDriver(); };
  }
  ~GPUProbeApp() override { Shutdown(); }
  rex::IDriver& GetProbeDriver() { return *driver_; }
  void ReleaseProbeDriver() {
    if (driver_) { driver_->Shutdown(); driver_.reset(); }
    if (linked_connected_) {
      linked_provider_->Shutdown();
      linked_provider_->Disconnect();
      linked_connected_ = false;
    }
  }
 protected:
  void OnRegisterModules(std::vector<rex::IModule*>& modules) override {
    App::OnRegisterModules(modules);
    modules.push_back(&services_);
  }
  void OnModules(std::vector<std::string>& modules) override {
    App::OnModules(modules);
    if (!linked_provider_) modules.push_back(module_);
  }
  rex::Result<void> OnConfigure(rex::SettingsRegistry& settings) override {
    REX_TRY_VOID(App::OnConfigure(settings));
    REX_TRY_VOID(root.Set(scratch_));
    REX_TRY_VOID(GetUI().GetSettings().driver.SetText(rex::SettingLayer::kTitle, "none"));
    REX_TRY_VOID(GetInput().GetSettings().drivers.SetText(rex::SettingLayer::kTitle, "none"));
    REX_TRY_VOID(GetAudio().GetSettings().driver.SetText(rex::SettingLayer::kTitle, "none"));
    REX_TRY_VOID(GetGraphics().GetSettings().driver.SetText(rex::SettingLayer::kTitle, "none"));
    // Register driver-owned startup settings before CLI/environment layers freeze.
    // Linked modules connect after the SDK libraries they require, just as loaded plugins do.
    if (linked_provider_) {
      REX_TRY_VOID(linked_provider_->Connect(*services_.factory_));
      linked_connected_ = true;
      REX_TRY_VOID(linked_provider_->Init());
    }
    auto* provider = linked_provider_
        ? static_cast<rex::IDriverModule*>(linked_provider_->QueryInterface(
              rex::IDriverModule::kInterface.name, rex::IDriverModule::kInterface.version))
        : services_.factory_->Get<rex::IDriverModule>();
    auto* context = services_.factory_->Get<rex::IDriverContext>();
    if (!provider || !context) throw std::runtime_error("GPU driver module or context unavailable");
    const auto drivers = provider->GetDrivers();
    const auto registration = std::find_if(drivers.begin(), drivers.end(), [&](const auto& driver) {
      return driver.type == rex::DriverType::kGraphics && driver.family == rex::GraphicsFamily::kXenos &&
          driver.name == selected_;
    });
    if (registration == drivers.end()) throw std::runtime_error("Requested GPU driver not provided by module");
    driver_ = registration->create();
    if (!driver_) throw std::runtime_error("GPU driver factory returned no driver");
    return driver_->Initialize(*context);
  }
  void OnRelease() override { ReleaseProbeDriver(); App::OnRelease(); }
 private:
  std::filesystem::path scratch_;
  std::string module_;
  std::string selected_;
  std::unique_ptr<rex::IDriver> driver_;
  ProbeServices services_;
  rex::IModule* linked_provider_;
  bool linked_connected_ = false;
};
}
