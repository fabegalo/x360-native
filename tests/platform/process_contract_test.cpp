#include <catch2/catch_test_macros.hpp>
#include "child_process.h"
#include "scratch.h"
#include "session_notification.h"
#include <filesystem>
#include <fstream>
#include <thread>
#include <algorithm>
#include <csignal>
#include <stdexcept>
#include <future>
#include <cstdlib>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;
using namespace x360n;
using namespace std::chrono_literals;
namespace {
struct Fixture {
  fs::path root;
  explicit Fixture(const std::string& name="platform") : root(UniqueTempPath(name) / fs::path(u8"João 日本 🎮")) { fs::create_directories(root); }
  ~Fixture() { std::error_code ec; fs::remove_all(root.parent_path(), ec); }
  ProcessSpec Spec(std::vector<std::string> args, std::chrono::milliseconds timeout = 5s) {
    return {fs::path(X360N_TEST_CHILD), std::move(args), root, root / "out.log", root / "err.log", timeout};
  }
  std::string Read(const fs::path& name = "out.log") {
    std::ifstream file(root / name, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
  }
};
}

TEST_CASE("native process arguments retain empty values quotes whitespace and shell metacharacters", "[process]") {
  Fixture f;
  const std::vector<std::string> args{"", "space value", "quote\"value", "trailing\\", "$HOME;`echo injected`", "line\nbreak", "tab\tvalue"};
  auto command = args; command.insert(command.begin(), "args");
  const auto result = RunProcess(f.Spec(command));
  INFO(result.launch_error);
  REQUIRE(result.launched);
  REQUIRE(result.exit_code == 0);
  std::string expected;
  for (const auto& arg : args) expected += std::to_string(arg.size()) + ':' + arg + '\n';
  auto actual = f.Read();
#ifdef _WIN32
  // The fixture uses text-mode stdout on Windows.
  std::erase(actual, '\r');
#endif
  CHECK(actual == expected);
}

TEST_CASE("prelaunch cancellation does not start or create logs", "[process]") {
  Fixture f;
  auto spec = f.Spec({"exit", "0"});
  spec.should_cancel = [] { return true; };
  const auto result = RunProcess(spec);
  CHECK_FALSE(result.launched);
  CHECK(result.cancelled);
  CHECK_FALSE(fs::exists(spec.stdout_file));
}

TEST_CASE("child environment overrides are isolated between concurrent launches", "[process]") {
  Fixture first("platform-env-first"), second("platform-env-second");
  auto a=first.Spec({"environment"}), b=second.Spec({"environment"});
  a.environment={{"X360N_TEST_OVERRIDE","first child"}};
  b.environment={{"X360N_TEST_OVERRIDE","second child"}};
  const auto* inherited=std::getenv("X360N_TEST_OVERRIDE");
  const std::string before=inherited ? inherited : "";
  auto future=std::async(std::launch::async,[&] { return RunProcess(a); });
  const auto result=RunProcess(b), other=future.get();
  REQUIRE(result.launched); REQUIRE(other.launched);
  CHECK(result.exit_code==0); CHECK(other.exit_code==0);
  CHECK(first.Read()=="first child"); CHECK(second.Read()=="second child");
  const auto* after=std::getenv("X360N_TEST_OVERRIDE");
  CHECK(std::string(after ? after : "")==before);
}

TEST_CASE("process cancellation leaves an unrelated session running", "[process-isolation]") {
  Fixture f;
  auto unrelated = f.Spec({"sleep", "500"});
  unrelated.stdout_file = f.root / "other.out";
  unrelated.stderr_file = f.root / "other.err";
  auto other = std::async(std::launch::async, [&] { return RunProcess(unrelated); });
  bool started = false;
  auto cancelled = f.Spec({"sleep", "20000"});
  cancelled.on_started = [&] { started = true; };
  cancelled.should_cancel = [&] { return started; };
  const auto result = RunProcess(cancelled);
  CHECK(result.launched);
  CHECK(result.cancelled);
  const auto unaffected = other.get();
  CHECK(unaffected.launched);
  CHECK_FALSE(unaffected.cancelled);
  CHECK_FALSE(unaffected.timed_out);
  CHECK(unaffected.exit_code == 0);
}

TEST_CASE("invalid working directory or log destination reports launch failure", "[process]") {
  for (const bool log : {false, true}) {
    Fixture f;
    auto spec = f.Spec({"exit", "0"});
    if (log) spec.stdout_file = f.root / "missing/out.log";
    else spec.working_dir = f.root / "missing";
    const auto result = RunProcess(spec);
    CHECK_FALSE(result.launched);
    CHECK_FALSE(result.launch_error.empty());
  }
}

TEST_CASE("named session notifications cross process boundaries and remain private", "[process]") {
  Fixture f;
  std::string name;
  {
    SessionNotification notification({}, true);
    SessionNotification independent({}, true);
    name = notification.name();
    REQUIRE(name != independent.name());
    unsigned count = 0;
    auto spec = f.Spec({"notify", name});
    spec.notification_event = notification.handle();
    spec.on_notification = [&] { ++count; };
    const auto result = RunProcess(spec);
    INFO(result.launch_error);
    REQUIRE(result.launched);
    CHECK(result.exit_code == 0);
    CHECK(count == 1);
#ifndef _WIN32
    pollfd unused{int(independent.handle() - 1), POLLIN, 0};
    CHECK(poll(&unused, 1, 0) == 0);
#else
    CHECK(WaitForSingleObject(reinterpret_cast<HANDLE>(independent.handle()), 0) == WAIT_TIMEOUT);
#endif
  }
#ifndef _WIN32
  CHECK_FALSE(fs::exists(name));
  CHECK_FALSE(fs::exists(fs::path(name).parent_path()));
#endif
  CHECK_THROWS(SessionNotification(name));
  CHECK_THROWS(SessionNotification("/tmp/../etc/passwd"));
}

#ifndef _WIN32
TEST_CASE("POSIX supervisor death kills nested process groups without cleanup callbacks", "[process-posix]") {
  Fixture f;
  const auto marker=f.root/"escaped";
  auto spec=f.Spec({"parent-tree",marker.string()});
  spec.should_cancel=[&] { return f.Read("nested.out").find("ready")!=std::string::npos; };
  const auto result=RunProcess(spec);
  REQUIRE(result.launched); REQUIRE(result.cancelled);
  std::this_thread::sleep_for(1100ms);
  CHECK_FALSE(fs::exists(marker));
}

TEST_CASE("POSIX log FIFOs and hardlinks are refused without waiting or modifying their targets", "[process-posix]") {
  Fixture f;
  auto spec = f.Spec({"exit", "0"});
  REQUIRE(mkfifo(spec.stdout_file.c_str(), 0600) == 0);
  const auto start = std::chrono::steady_clock::now();
  const auto fifo = RunProcess(spec);
  CHECK_FALSE(fifo.launched);
  CHECK_FALSE(fifo.launch_error.empty());
  CHECK(std::chrono::steady_clock::now() - start < 1s);
  fs::remove(spec.stdout_file);
  const auto protected_file = f.root / "protected";
  std::ofstream(protected_file) << "preserved";
  fs::create_hard_link(protected_file, spec.stdout_file);
  const auto hardlink = RunProcess(spec);
  CHECK_FALSE(hardlink.launched);
  CHECK_FALSE(hardlink.launch_error.empty());
  CHECK(f.Read("protected") == "preserved");
}

TEST_CASE("POSIX spawning closes non CLOEXEC descriptors in the child", "[process-posix]") {
  Fixture f;
  const int descriptor = open((f.root / "private").c_str(), O_CREAT | O_RDWR, 0600);
  REQUIRE(descriptor >= 0);
  const auto result = RunProcess(f.Spec({"fd-state", std::to_string(descriptor)}));
  close(descriptor);
  REQUIRE(result.launched);
  CHECK(result.exit_code == 0);
}

TEST_CASE("POSIX native wait handles cancel promptly and consume notifications", "[process-posix]") {
  Fixture f;
  struct Pipe {
    int fds[2]{-1, -1};
    Pipe() {
      if (pipe(fds) < 0) throw std::runtime_error("pipe failed");
      if (fcntl(fds[0], F_SETFL, O_NONBLOCK) < 0) throw std::runtime_error("nonblocking pipe failed");
    }
    ~Pipe() { close(fds[0]); close(fds[1]); }
    uintptr_t handle() const { return uintptr_t(fds[0] + 1); }
    void Signal() { REQUIRE(write(fds[1], "x", 1) == 1); }
  } cancel, notification;
  auto spec = f.Spec({"sleep", "20000"});
  spec.cancel_event = cancel.handle();
  spec.notification_event = notification.handle();
  unsigned notifications = 0;
  spec.on_started = [&] { notification.Signal(); };
  spec.on_notification = [&] { ++notifications; cancel.Signal(); };
  const auto result = RunProcess(spec);
  INFO(result.launch_error);
  REQUIRE(result.launched);
  CHECK(result.cancelled);
  CHECK_FALSE(result.timed_out);
  CHECK(notifications == 1);
  CHECK(result.duration < 2s);
  const auto repeat = RunProcess(spec);
  CHECK_FALSE(repeat.launched);
  CHECK(repeat.cancelled);
}

TEST_CASE("POSIX preserves UTF8 argv and working directory", "[process-posix]") {
  Fixture f;
  const auto name = std::string("João 日本 🎮");
  auto result = RunProcess(f.Spec({"args", name}));
  REQUIRE(result.launched);
  CHECK(result.exit_code == 0);
  CHECK(f.Read() == std::to_string(name.size()) + ':' + name + '\n');
  result = RunProcess(f.Spec({"cwd"}));
  REQUIRE(result.launched);
  CHECK(f.Read() == "cwd " + fs::canonical(f.root).string() + '\n');
}

TEST_CASE("POSIX does not confuse signal termination with explicit exit status", "[process-posix]") {
  Fixture f;
  const auto result = RunProcess(f.Spec({"exit", "139"}));
  REQUIRE(result.launched);
  CHECK(result.exit_code == 139);
  CHECK(result.termination_signal == 0);
}

TEST_CASE("POSIX cancellation allows a bounded clean termination and forces unresponsive children", "[process-posix]") {
  for (const std::string mode : {"term-clean", "ignore-term"}) {
    Fixture f;
    auto spec = f.Spec({mode});
    spec.should_cancel = [&] { return f.Read().find("ready") != std::string::npos; };
    const auto result = RunProcess(spec);
    INFO(result.launch_error);
    REQUIRE(result.launched);
    CHECK(result.cancelled);
    CHECK_FALSE(result.timed_out);
    CHECK(result.duration < 3s);
    if (mode == "term-clean") {
      CHECK(result.exit_code == 23);
      CHECK(f.Read().find("clean termination") != std::string::npos);
    } else {
      CHECK(result.termination_signal == SIGKILL);
    }
  }
}

TEST_CASE("POSIX descendants cannot outlive timeout cancellation or normal leader exit", "[process-posix]") {
  for (const std::string mode : {"exit", "timeout", "cancel"}) {
    Fixture f;
    const auto marker = f.root / "escaped";
    auto spec = f.Spec({"tree", marker.string(), mode}, mode == "timeout" ? 200ms : 5s);
    if (mode == "cancel") spec.should_cancel = [&] { return f.Read().find("ready") != std::string::npos; };
    const auto result = RunProcess(spec);
    INFO(result.launch_error);
    REQUIRE(result.launched);
    CHECK(result.timed_out == (mode == "timeout"));
    CHECK(result.cancelled == (mode == "cancel"));
    if (mode == "exit") CHECK(result.exit_code == 0);
    std::this_thread::sleep_for(1100ms);
    CHECK_FALSE(fs::exists(marker));
  }
}

TEST_CASE("POSIX unsupported diagnostics fail explicitly before spawning", "[process-posix]") {
  for (int option : {0, 1, 2}) {
    Fixture f;
    auto spec = f.Spec({"exit", "0"});
    spec.capture_crash = option == 0;
    spec.collect_metrics = option == 1;
    spec.crash_guest_memory_base = option == 2 ? 4096 : 0;
    const auto result = RunProcess(spec);
    CHECK_FALSE(result.launched);
    CHECK_FALSE(result.launch_error.empty());
    CHECK_FALSE(fs::exists(spec.stdout_file));
  }
}

TEST_CASE("POSIX callback exceptions kill and reap the child", "[process-posix]") {
  Fixture f;
  auto spec = f.Spec({"sleep", "20000"});
  spec.on_started = [] { throw std::runtime_error("callback failed"); };
  const auto start = std::chrono::steady_clock::now();
  const auto result = RunProcess(spec);
  CHECK(result.launched);
  CHECK(result.exit_code != 0);
  CHECK(result.launch_error == "callback failed");
  CHECK(std::chrono::steady_clock::now() - start < 3s);
}

TEST_CASE("POSIX rejects embedded null arguments and symlink log destinations", "[process-posix]") {
  Fixture f;
  auto spec = f.Spec({"args", std::string("a\0b", 3)});
  CHECK_FALSE(RunProcess(spec).launched);
  const auto original = f.root / "protected";
  std::ofstream(original) << "preserved";
  fs::create_symlink(original, spec.stdout_file);
  spec.args = {"exit", "0"};
  CHECK_FALSE(RunProcess(spec).launched);
  CHECK(f.Read("protected") == "preserved");
}
#endif
