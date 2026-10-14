#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/audio/sdl/sdl_audio_driver.h>
#include <SDL3/SDL.h>
#include <rex/audio/audio_system.h>
#include <rex/audio/audio_driver.h>
#include <future>
#include <thread>
using rex::X_STATUS;

namespace {
class DrainProbe : public rex::audio::sdl::SDLAudioDriver {
 public:
  using SDLAudioDriver::SDLAudioDriver;
  SDL_AudioDeviceID device() const { return SDL_GetAudioStreamDevice(sdl_stream_); }
};
}

namespace {
class SilentDriver final : public rex::audio::AudioDriver {
 public:
  using AudioDriver::AudioDriver;
  void SubmitFrame(uint32_t) override {}
};
class CallbackProbe final : public rex::audio::AudioSystem {
 public:
  explicit CallbackProbe(rex::runtime::FunctionDispatcher* dispatcher) : AudioSystem(dispatcher) {}
  rex::X_STATUS CreateDriver(size_t, rex::thread::Semaphore*, rex::audio::AudioDriver** out) override {
    *out = new SilentDriver(memory());
    return X_STATUS_SUCCESS;
  }
  void DestroyDriver(rex::audio::AudioDriver* driver) override { delete driver; }
};
std::promise<void>* audio_entered;
std::shared_future<void> audio_release;
std::atomic<unsigned> audio_calls;
std::atomic<unsigned> audio_cpu;
std::atomic<bool> audio_fp_policy;
void BlockingAudioCallback(PPCContext& ctx, uint8_t* base) {
  audio_cpu = base[ctx.r13.u32 + 0x10C];
  auto expected = ctx.fpscr.csr;
  PPCFPSCRRegister::Platform::InitHostExceptions(expected);
  audio_fp_policy = ctx.fpscr.csr == expected;
  if (++audio_calls == 1) audio_entered->set_value();
  audio_release.wait();
}
void QuiescingAudioCallback(PPCContext&, uint8_t*) {
  ++audio_calls;
  rex::Runtime::instance()->audio_system()->QuiesceTitleCallbacks();
  audio_entered->set_value();
}
}

TEST_CASE("audio callback can quiesce its own dispatch without joining itself", "[sdk][audio-drain][title-return]") {
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  config.audio_factory = [](rex::runtime::FunctionDispatcher* dispatcher) {
    return std::unique_ptr<rex::system::IAudioSystem>(new CallbackProbe(dispatcher));
  };
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  REQUIRE(rt.function_dispatcher()->InitializeFunctionTable(0x82010000, 0x1000, 0x82000000, 0x20000));
  REQUIRE(rt.function_dispatcher()->SetFunction(0x82010000, QuiescingAudioCallback));
  std::promise<void> entered;
  audio_entered = &entered;
  audio_calls = 0;
  size_t client = 0;
  auto* audio = static_cast<CallbackProbe*>(rt.audio_system());
  REQUIRE(audio->RegisterClient(0x82010000, 0, &client) == X_STATUS_SUCCESS);
  CHECK(entered.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready);
  audio->Shutdown();
  CHECK(audio_calls == 1);
}

TEST_CASE("title termination joins audio callbacks before retiring guest threads", "[sdk][audio-drain][title-return]") {
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  config.audio_factory = [](rex::runtime::FunctionDispatcher* dispatcher) {
    return std::unique_ptr<rex::system::IAudioSystem>(new CallbackProbe(dispatcher));
  };
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  REQUIRE(rt.function_dispatcher()->InitializeFunctionTable(0x82010000, 0x1000, 0x82000000, 0x20000));
  REQUIRE(rt.function_dispatcher()->SetFunction(0x82010000, BlockingAudioCallback));
  std::promise<void> entered, release;
  audio_entered = &entered;
  audio_release = release.get_future().share();
  audio_calls = 0;
  audio_cpu = ~0u;
  size_t client = 0;
  auto* audio = static_cast<CallbackProbe*>(rt.audio_system());
  REQUIRE(audio->RegisterClient(0x82010000, 0, &client) == X_STATUS_SUCCESS);
  const auto started = entered.get_future().wait_for(std::chrono::seconds(2));
  if (started != std::future_status::ready) {
    release.set_value();
    FAIL("Audio callback did not start");
  }
  auto terminating = std::async(std::launch::async, [&] { rt.kernel_state()->TerminateTitle(); });
  CHECK(terminating.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout);
  CHECK_FALSE(rt.kernel_state()->is_terminating_title());
  CHECK(audio_cpu == 4);
  CHECK(audio_fp_policy);
  release.set_value();
  CHECK(terminating.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
  terminating.get();
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  CHECK(audio_calls == 1); // queued callbacks must not run after the join
  audio->UnregisterClient(client);
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
