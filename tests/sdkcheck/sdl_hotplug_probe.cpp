// Headless SDL event-queue lifetime regression. Window is never opened.
#include <rex/input/sdl/sdl_input_driver.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <iostream>
#include <thread>

class HeadlessContext final : public rex::ui::WindowedAppContext {
  void NotifyUILoopOfPendingFunctions() override {}
  void PlatformQuitFromUIThread() override {}
};
int main() {
  for (const char* hint : {SDL_HINT_JOYSTICK_HIDAPI, SDL_HINT_JOYSTICK_RAWINPUT,
                          SDL_HINT_JOYSTICK_DIRECTINPUT, SDL_HINT_XINPUT_ENABLED,
                          SDL_HINT_JOYSTICK_WGI}) SDL_SetHint(hint, "0");
  rex::cvar::SetFlagByName("hid_mappings_file", "");
  HeadlessContext context;
  auto window = rex::ui::Window::Create(context, "headless fixture", 1, 1);
  auto driver = std::make_unique<rex::input::sdl::SDLInputDriver>(nullptr, 0);
  if (driver->Setup()) return 1;
  driver->OnWindowAvailable(window.get());
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
    std::vector<rex::input::DeviceInfo> devices;
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
    rex::input::X_INPUT_STATE state{};
    if (driver->GetDeviceState(old, &state) != 1167) return 9;
  }
  const auto pending_id = SDL_AttachVirtualJoystick(&desc);
  if (!pending_id) return 11;
  SDL_UpdateJoysticks();
  std::vector<rex::input::DeviceInfo> pending_devices;
  driver->EnumerateDevices(pending_devices);
  if (pending_devices.size() != 1) return 12;
  // Schedule SDL_PumpEvents from a guest thread, then retire the driver before
  // executing that UI callback. The queued work must not retain `this`.
  std::thread guest([&] {
    rex::input::X_INPUT_STATE state{};
    driver->GetDeviceState(pending_devices.front().id, &state);
  });
  guest.join();
  driver.reset();
  // The driver must detach even when the Window never sends OnClosing, as in
  // Runtime::Shutdown. No watch, pending lambda or SDL reference may survive.
  context.ExecutePendingFunctionsFromUIThread();
  if (SDL_WasInit(SDL_INIT_GAMEPAD)) {
    std::cout << "FAIL: destroyed input driver retained SDL gamepad subsystem\n";
    return 10;
  }
  std::cout << "Completed stale SDL hotplug events\n";
  return 0;
}
