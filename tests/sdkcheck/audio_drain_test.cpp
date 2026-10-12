#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/audio/sdl/sdl_audio_driver.h>
#include <SDL3/SDL.h>

namespace {
class DrainProbe : public rex::audio::sdl::SDLAudioDriver {
 public:
  using SDLAudioDriver::SDLAudioDriver;
  SDL_AudioDeviceID device() const { return SDL_GetAudioStreamDevice(sdl_stream_); }
};
}

TEST_CASE("SDL audio shutdown consumes submitted guest frames before destroying the stream", "[sdk][audio-drain]") {
  using rex::X_STATUS;
  // The pinned SDL build has no dummy backend. Use the real configured
  // endpoint with silent samples, and report missing hardware as skipped.
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};
  config.tool_mode = true; config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  auto semaphore = rex::thread::Semaphore::Create(0, 64);
  DrainProbe driver(rt.memory(), semaphore.get());
  if (!driver.Initialize()) {
    driver.Shutdown();
    SKIP("SDL audio endpoint unavailable");
  }
  REQUIRE(SDL_PauseAudioDevice(driver.device()));
  const auto samples = rt.memory()->SystemHeapAlloc(6 * 256 * sizeof(float));
  REQUIRE(samples);
  auto* data = rt.memory()->TranslateVirtual<rex::be<float>*>(samples);
  for (unsigned i = 0; i < 6 * 256; ++i) data[i] = 0.0f;
  for (unsigned i = 0; i < 64; ++i) driver.SubmitFrame(samples);
  REQUIRE(SDL_ResumeAudioDevice(driver.device()));
  driver.Shutdown();
  unsigned consumed = 0;
  while (rex::thread::Wait(semaphore.get(), false, std::chrono::milliseconds(0)) ==
         rex::thread::WaitResult::kSuccess) ++consumed;
  CHECK(consumed == 64); // old Shutdown discards the queued frames without consuming them
  rt.memory()->SystemHeapFree(samples);
}
