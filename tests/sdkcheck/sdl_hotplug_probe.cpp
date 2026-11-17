// Headless SDL event-queue lifetime regression. Window is never opened.
#include "hid/sdl3/sdl3_input_driver.h"
#include "codegen/tool_runtime.h"
#include <rex/logging/logger.h>
#include <rex/settings_registry.h>
#include <SDL3/SDL.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
class HeadlessContext final : public rex::IDriverContext {
 public:
  rex::ISettingsRegistry& GetSettingsRegistry() override {return REX_SETTINGS();}
  rex::ILogger& GetLogger() override {return REX_LOGGER();}
  rex::Result<void> RunOnUIThread(rex::MoveOnlyFunction<void()> fn) override {fn();return rex::Ok();}
  void PostToUIThread(rex::MoveOnlyFunction<void()> fn) override {posted.push_back(std::move(fn));}
  void RunPosted() {auto run=std::move(posted);posted.clear();for(auto& fn:run)fn();}
 private:
  std::vector<rex::MoveOnlyFunction<void()>> posted;
};
int main() {
  for (const char* hint : {SDL_HINT_JOYSTICK_HIDAPI, SDL_HINT_JOYSTICK_RAWINPUT,
                          SDL_HINT_JOYSTICK_DIRECTINPUT, SDL_HINT_XINPUT_ENABLED,
                          SDL_HINT_JOYSTICK_WGI}) SDL_SetHint(hint, "0");
  const auto scratch=std::filesystem::temp_directory_path()/
      ("x360n-sdl-hotplug-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(scratch);
  struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove_all(path,ec);}} cleanup{scratch};
  rex::codegen::ToolApp app("sdl_hotplug_probe",scratch);if(!app.Start())return 1;
  HeadlessContext context;
  auto driver=std::make_unique<rex::hid::sdl3::SDL3InputDriver>();
  const auto initialized=driver->Initialize(context);
  if(!initialized){std::cerr<<initialized.error().message<<'\n';return 1;}
  rex::ScopeGuard shutdown([&] {if(driver)driver->Shutdown();context.RunPosted();});
  SDL_VirtualJoystickDesc desc{};
  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  desc.nbuttons = 16;
  desc.button_mask = 0xFFFF;
  desc.name = "x360n hotplug fixture";
  for (unsigned cycle = 0; cycle < 20; ++cycle) {
    auto id = SDL_AttachVirtualJoystick(&desc);
    if (!id) return 2;
    SDL_UpdateJoysticks();
    // Leave ADDED pending, then remove the device before the guest polls.
    if (!SDL_DetachVirtualJoystick(id)) return 3;
    SDL_UpdateJoysticks();
    std::vector<rex::hid::PadDevice> devices;
    std::cout << "draining stale ADDED/REMOVED cycle=" << cycle << '\n' << std::flush;
    driver->EnumerateDevices(devices);
    if (!devices.empty()) return 4;
    id = SDL_AttachVirtualJoystick(&desc);
    if (!id) return 5;
    SDL_UpdateJoysticks();
    driver->EnumerateDevices(devices);
    if (devices.size() != 1) return 6;
    const auto old = devices.front().id;
    if (!SDL_DetachVirtualJoystick(id)) return 7;
    SDL_UpdateJoysticks();
    devices.clear(); driver->EnumerateDevices(devices);
    if (!devices.empty()) return 8;
    const auto state=driver->GetDeviceState(old);
    if(state || state.error().category!=rex::ErrorCategory::kNotFound)return 9;
  }
  const auto pending_id = SDL_AttachVirtualJoystick(&desc);
  if (!pending_id) return 11;
  SDL_UpdateJoysticks();
  std::vector<rex::hid::PadDevice> pending_devices;
  driver->EnumerateDevices(pending_devices);
  if (pending_devices.size() != 1) return 12;
  // Schedule SDL_PumpEvents from a guest thread, then retire the driver before
  // executing that UI callback. The queued work must not retain `this`.
  std::thread guest([&] {
    (void)driver->GetDeviceState(pending_devices.front().id);
  });
  guest.join();
  driver->Shutdown();driver.reset();
  // The canonical owner calls Shutdown before destroying the driver. Pending
  // UI work must retain no live driver, watch or SDL subsystem afterward.
  context.RunPosted();
  if (SDL_WasInit(SDL_INIT_GAMEPAD)) {
    std::cout << "FAIL: destroyed input driver retained SDL gamepad subsystem\n";
    return 10;
  }
  std::cout << "Completed stale SDL hotplug events\n";
  return 0;
}
