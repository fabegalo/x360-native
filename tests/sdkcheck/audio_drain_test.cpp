#include <catch2/catch_test_macros.hpp>
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <string_view>
#include <rex/apu/audio_driver.h>
#include <rex/kernel/guest_memory.h>
#include <rex/kernel/title_runtime.h>
#include <rex/logging/logger.h>
#include <rex/platform/process.h>
#include <rex/settings_registry.h>
#include "apu/audio_system.h"
#include "base/driver_context.h"
#include "base/module_group.h"
#include "base/module_registration.h"
#include "kernel/apu.h"
#include "kernel/kernel_fixture.h"

// Callback processor/FP and before-retirement/self-quiescence checks use the
// canonical runtime in audio_callback_context_test.cpp.
namespace {
class DrainProbe final : public rex::apu::IAudioDriver {
 public:
  explicit DrainProbe(std::unique_ptr<rex::apu::IAudioDriver> driver) : driver_(std::move(driver)) {}
  std::string_view GetName() const override { return driver_->GetName(); }
  rex::Result<void> Initialize(rex::IDriverContext& context) override { return driver_->Initialize(context); }
  void Shutdown() override { driver_->Shutdown(); }
  rex::Result<rex::u32> OpenDevice(rex::u32 channels, rex::apu::AudioFill fill) override {
    auto result = driver_->OpenDevice(channels, std::move(fill));
    opened = result.has_value();
    if (!result) open_failed = true;
    return result;
  }
  void CloseDevice() override { driver_->CloseDevice(); opened = false; }
  bool IsDeviceLost() const override {
    const bool result = driver_->IsDeviceLost();
    if (result) lost = true;
    return result;
  }
  std::atomic<bool> opened{false}, open_failed{false};
  mutable std::atomic<bool> lost{false};
 private:
  std::unique_ptr<rex::apu::IAudioDriver> driver_;
};
}

TEST_CASE("SDL audio shutdown consumes submitted guest frames before destroying the stream", "[sdk][audio-drain]") {
  const auto previous = rex::platform::GetEnvironmentValue("SDL_AUDIO_DRIVER");
  rex::ScopeGuard restore_environment([&] {
    if (previous) rex::platform::SetEnvironmentValue("SDL_AUDIO_DRIVER", *previous);
    else rex::platform::UnsetEnvironmentValue("SDL_AUDIO_DRIVER");
  });
  if (previous) REQUIRE(rex::platform::UnsetEnvironmentValue("SDL_AUDIO_DRIVER"));
  rex::test::KernelFixture fixture;
  rex::ModuleGroup modules;
  REQUIRE(modules.Load("rexapu-sdl3"));
  REQUIRE(modules.Connect());
  std::unique_ptr<rex::apu::IAudioDriver> actual;
  for (const auto* registration : rex::test::GetRegistrations(modules, rex::DriverType::kAudio)) {
    if (registration->name == "sdl3") {
      auto driver = registration->create();
      actual.reset(static_cast<rex::apu::IAudioDriver*>(driver.release()));
      break;
    }
  }
  REQUIRE(actual);
  auto& settings = REX_SETTINGS();
  rex::AppDriverContext context(settings, REX_LOGGER());
  DrainProbe driver(std::move(actual));
  REQUIRE(driver.Initialize(context));
  rex::ScopeGuard shutdown_driver([&] { driver.Shutdown(); });
  auto& mixer = static_cast<rex::apu::AudioSystem&>(fixture.GetAudio());
  mixer.Stop();
  rex::ScopeGuard stop([&] { mixer.Stop(); });
  REQUIRE(mixer.GetSettings().max_queued_frames.Set(64));
  REQUIRE(mixer.Start(&driver));
  const auto* backend = SDL_GetCurrentAudioDriver();
  if (!driver.opened || driver.open_failed || !backend ||
      std::string_view(backend) == "dummy" || std::string_view(backend) == "disk") {
    SKIP("Physical SDL audio endpoint unavailable; timer/dummy playback is not hardware evidence");
  }
  INFO("Physical SDL backend: " << backend);
  rex::kernel::APU audio(fixture.GetRuntime(), &mixer);
  const auto client = audio.RegisterClient(0, 0);
  REQUIRE(client);
  for (unsigned i = 0; i < 64; ++i) REQUIRE(audio.TakeCredit(*client));
  auto& memory = fixture.GetRuntime().GetGuestMemory();
  constexpr auto bytes = rex::apu::kAudioFrameSamples * sizeof(float);
  const auto samples = memory.Allocate(bytes, 16, rex::kernel::GuestProtect::kReadWrite,
                                       rex::kernel::GuestHeap::kSystem);
  REQUIRE(samples);
  const auto span = memory.GetWritableSpan(*samples, bytes);
  REQUIRE(span);
  std::fill(span->begin(), span->end(), std::byte{0});
  for (unsigned i = 0; i < 64; ++i) audio.SubmitFrame(*client, *samples);
  audio.Shutdown();
  CHECK(audio.CountCredits(*client) == 64);
  CHECK(driver.opened);
  CHECK_FALSE(driver.open_failed);
  CHECK_FALSE(driver.lost);
  REQUIRE(memory.Free(*samples));
}
