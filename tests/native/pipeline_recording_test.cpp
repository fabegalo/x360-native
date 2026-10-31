// Platform-neutral contracts of Native's asynchronous pipeline creation:
// PipelineWorkers (creation off the recording thread) and DeferredCommands
// (command order preserved while a bound pipeline is still being created).
#include "../../src/native/pipeline/deferred_commands.h"
#include "../../src/native/pipeline/pipeline_workers.h"
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
using namespace x360n::native;
using namespace std::chrono_literals;
namespace {
struct FakeList {
  std::vector<std::string> calls;
};
using Commands = DeferredCommands<FakeList>;
void Call(Commands& commands, std::string name) {
  commands.Record([name](FakeList* list) { list->calls.push_back(name); });
}
void Bind(Commands& commands, const std::shared_future<std::string>& pipeline) {
  commands.BindPending(pipeline, [](FakeList* list, const std::string& p) { list->calls.push_back("pipeline:" + p); });
}
template <class F>
std::string ErrorOf(F&& f) {
  try { f(); } catch (const std::exception& error) { return error.what(); }
  return {};
}
std::shared_future<std::string> Ready(std::string value) {
  std::promise<std::string> promise;
  promise.set_value(std::move(value));
  return promise.get_future().share();
}
}

TEST_CASE("pipeline workers create concurrently", "[native-pipeline]") {
  constexpr unsigned kJobs = 4;
  PipelineWorkers workers(kJobs);
  std::mutex mutex;
  std::condition_variable all;
  unsigned arrived = 0;
  std::vector<std::shared_future<bool>> results;
  for (unsigned i = 0; i < kJobs; ++i)
    results.push_back(workers.Submit(std::function<bool()>([&] {
      std::unique_lock lock(mutex);
      if (++arrived == kJobs) all.notify_all();
      // Only true if every job is running at the same time.
      return all.wait_for(lock, 10s, [&] { return arrived == kJobs; });
    })));
  for (auto& result : results) CHECK(result.get());
}

TEST_CASE("pipeline workers without threads create inline", "[native-pipeline]") {
  PipelineWorkers workers(0);
  const auto caller = std::this_thread::get_id();
  auto result = workers.Submit(std::function<std::thread::id()>([] { return std::this_thread::get_id(); }));
  REQUIRE(result.wait_for(0s) == std::future_status::ready);
  CHECK(result.get() == caller);
  auto failed = workers.Submit(std::function<int()>([]() -> int { throw std::runtime_error("driver rejected"); }));
  REQUIRE(failed.wait_for(0s) == std::future_status::ready);
  CHECK_THROWS_AS(failed.get(), std::runtime_error);
}

TEST_CASE("pipeline workers report creation errors through the result", "[native-pipeline]") {
  PipelineWorkers workers(2);
  auto failed = workers.Submit(std::function<int()>([]() -> int { throw std::runtime_error("driver rejected"); }));
  CHECK(ErrorOf([&] { failed.get(); }) == "driver rejected");
  // A failure does not stop the worker.
  CHECK(workers.Submit(std::function<int()>([] { return 7; })).get() == 7);
}

TEST_CASE("pipeline workers drop queued jobs at teardown", "[native-pipeline]") {
  std::shared_future<int> queued;
  std::atomic<bool> release = false, started = false;
  std::thread releaser;
  {
    PipelineWorkers workers(1);
    auto running = workers.Submit(std::function<int()>([&] {
      started = true;
      while (!release) std::this_thread::sleep_for(1ms);
      return 1;
    }));
    queued = workers.Submit(std::function<int()>([] { return 2; }));
    while (!started) std::this_thread::sleep_for(1ms);
    releaser = std::thread([&] { std::this_thread::sleep_for(200ms); release = true; });
  }  // Joins the running job; the queued one never starts.
  releaser.join();
  REQUIRE(queued.valid());
  CHECK_THROWS_AS(queued.get(), std::future_error);
}

TEST_CASE("commands record directly while no pipeline is pending", "[native-pipeline]") {
  FakeList list;
  Commands commands;
  commands.Attach(&list);
  Call(commands, "a");
  Bind(commands, Ready("p0"));
  Call(commands, "b");
  CHECK_FALSE(commands.deferred());
  CHECK(list.calls == std::vector<std::string>{"a", "pipeline:p0", "b"});
  commands.Replay();
  CHECK(list.calls.size() == 3);
}

TEST_CASE("commands after a pending pipeline replay in order", "[native-pipeline]") {
  FakeList list;
  Commands commands;
  commands.Attach(&list);
  std::promise<std::string> creation;
  Call(commands, "barrier");
  Bind(commands, creation.get_future().share());
  REQUIRE(commands.deferred());
  Call(commands, "draw0");
  Bind(commands, Ready("p1"));  // Ready, but must stay behind the pending bind.
  Call(commands, "draw1");
  CHECK(list.calls == std::vector<std::string>{"barrier"});
  CHECK(commands.deferred_count() == 4);
  creation.set_value("p0");
  commands.Replay();
  CHECK_FALSE(commands.deferred());
  CHECK(list.calls == std::vector<std::string>{"barrier", "pipeline:p0", "draw0", "pipeline:p1", "draw1"});
  Call(commands, "next");
  CHECK(list.calls.back() == "next");
}

TEST_CASE("replay waits for a pipeline created on a worker", "[native-pipeline]") {
  PipelineWorkers workers(1);
  FakeList list;
  Commands commands;
  commands.Attach(&list);
  auto pipeline = workers.Submit(std::function<std::string()>([] {
    std::this_thread::sleep_for(50ms);
    return std::string("slow");
  }));
  Bind(commands, pipeline);
  Call(commands, "draw");
  commands.Replay();
  CHECK(list.calls == std::vector<std::string>{"pipeline:slow", "draw"});
}

TEST_CASE("deferred commands own their arguments", "[native-pipeline]") {
  FakeList list;
  Commands commands;
  commands.Attach(&list);
  std::promise<std::string> creation;
  Bind(commands, creation.get_future().share());
  {
    std::string transient = "viewport";
    Call(commands, transient);
    transient = "changed";
  }
  creation.set_value("p");
  commands.Replay();
  CHECK(list.calls == std::vector<std::string>{"pipeline:p", "viewport"});
}

TEST_CASE("a failed pipeline throws at replay", "[native-pipeline]") {
  FakeList list;
  Commands commands;
  commands.Attach(&list);
  std::promise<std::string> creation;
  Bind(commands, creation.get_future().share());
  Call(commands, "draw");
  creation.set_exception(std::make_exception_ptr(std::runtime_error("pipeline creation failed")));
  CHECK(ErrorOf([&] { commands.Replay(); }) == "pipeline creation failed");
  CHECK_FALSE(commands.deferred());
}
