#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <future>
#include <rex/kernel/function_dispatcher.h>
#include <rex/kernel/object/kernel_thread.h>
#include <rex/kernel/types/thread.h>
#include "kernel/apu.h"
#include "kernel/kernel_fixture.h"
#include "kernel/test_access.h"
#include "kernel/title_state.h"

namespace {
struct Observation {
  std::promise<void> entered;
  std::atomic<unsigned> calls{0};
  unsigned cpu = ~0u;
  uint32_t argument = 0, thread_object = 0;
  bool host_fp_policy = false;
};
Observation* observation;

struct BlockingObservation {
  std::promise<void> entered, release;
  std::shared_future<void> released = release.get_future().share();
  std::atomic<unsigned> calls{0};
};
BlockingObservation* blocking;
rex::kernel::APU* callback_owner;

void QuiesceCallback(PPCContext&, uint8_t*) {
  if (observation->calls.fetch_add(1) != 0) return;
  callback_owner->QuiesceCallbacks();
  observation->entered.set_value();
}

void BlockCallback(PPCContext&, uint8_t*) {
  if (blocking->calls.fetch_add(1) == 0) blocking->entered.set_value();
  blocking->released.wait();
}

void ObserveCallback(PPCContext& ctx, uint8_t* base) {
  if (observation->calls.fetch_add(1) != 0) return;
  const auto* pcr = reinterpret_cast<const rex::kernel::X_KPCR*>(base + ctx.r13.u32);
  observation->cpu = pcr->prcb_data.current_cpu;
  observation->argument = rex::memory::load_and_swap<uint32_t>(base + ctx.r3.u32);
  observation->thread_object = rex::kernel::KernelThread::GetCallingThread()->GetGuestObject();
  auto expected = ctx.fpscr.csr;
  rex::ppc::FPSCRPlatform::InitHostExceptions(expected);
  observation->host_fp_policy = ctx.fpscr.csr == expected;
  observation->entered.set_value();
}
}

TEST_CASE("Audio callbacks retain their original processor and canonical guest context",
          "[sdk][audio-callback-context]") {
  Observation captured;
  observation = &captured;
  rex::ScopeGuard clear([] { observation = nullptr; });
  rex::test::KernelFixture fixture;
  auto& runtime = fixture.GetRuntime();
  const auto callback = rex::kernel::GetFunctionDispatcher(runtime).AllocateThunk(ObserveCallback, 0);
  REQUIRE(callback != 0);
  rex::kernel::APU audio(runtime, &fixture.GetAudio());
  REQUIRE(audio.Setup());
  REQUIRE(audio.RegisterClient(callback, 0x12345678));
  const auto ready = captured.entered.get_future().wait_for(std::chrono::seconds(2));
  audio.Shutdown();
  REQUIRE(ready == std::future_status::ready);
  CHECK(captured.cpu == 4);
  CHECK(captured.argument == 0x12345678);
  CHECK(captured.thread_object != 0);
  CHECK(captured.host_fp_policy);
}

TEST_CASE("Title termination quiesces audio before retiring guest threads",
          "[sdk][audio-callback-context][audio-quiescence]") {
  BlockingObservation captured;
  blocking = &captured;
  rex::ScopeGuard clear([] { blocking = nullptr; });
  rex::test::KernelFixture fixture;
  auto& state = rex::kernel::StateOf(fixture.GetRuntime());
  REQUIRE(state.AttachSystems(nullptr, nullptr, nullptr, nullptr, &fixture.GetAudio(), {}));
  auto* audio = state.GetAPU();
  REQUIRE(audio);
  const auto callback = state.GetFunctionDispatcher()->AllocateThunk(BlockCallback, 0);
  REQUIRE(callback);
  REQUIRE(audio->RegisterClient(callback, 0));
  const auto started = captured.entered.get_future().wait_for(std::chrono::seconds(2));
  if (started != std::future_status::ready) {
    captured.release.set_value();
    audio->StopWorker();
    FAIL("Audio callback did not start");
  }
  auto terminating = std::async(std::launch::async, [&] { state.TerminateTitle(); });
  const auto pending = terminating.wait_for(std::chrono::milliseconds(100));
  const bool retired = state.IsTerminatingTitle();
  captured.release.set_value();
  REQUIRE(terminating.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
  terminating.get();
  audio->StopWorker();
  CHECK(pending == std::future_status::timeout);
  CHECK_FALSE(retired);
  CHECK(captured.calls == 1);
}

TEST_CASE("An audio callback can quiesce its own dispatch without joining itself",
          "[sdk][audio-callback-context][audio-quiescence]") {
  Observation captured;
  observation = &captured;
  rex::ScopeGuard clear([] { observation = nullptr; callback_owner = nullptr; });
  rex::test::KernelFixture fixture;
  auto& runtime = fixture.GetRuntime();
  const auto callback = rex::kernel::GetFunctionDispatcher(runtime).AllocateThunk(QuiesceCallback, 0);
  REQUIRE(callback);
  rex::kernel::APU audio(runtime, &fixture.GetAudio());
  callback_owner = &audio;
  REQUIRE(audio.Setup());
  REQUIRE(audio.RegisterClient(callback, 0));
  const auto ready = captured.entered.get_future().wait_for(std::chrono::seconds(2));
  audio.Shutdown();
  CHECK(ready == std::future_status::ready);
  CHECK(captured.calls == 1);
}
