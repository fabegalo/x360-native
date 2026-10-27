#include <catch2/catch_test_macros.hpp>
#include "../../titles/common/guest_fault_context.h"
#include "../supervisor/scratch.h"
#include "child_process.h"
#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <fstream>

namespace {
struct Directory {
  std::filesystem::path path = UniqueTempPath("guest_fault");
  Directory() { std::filesystem::create_directory(path); }
  ~Directory() { std::error_code error; std::filesystem::remove_all(path, error); }
};
std::string Read(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), {}};
}
}

TEST_CASE("Guest fault diagnostic records PPC context without handling the real access violation",
          "[sdk][guest-fault-context]") {
  Directory directory;
  for (const auto* mode : {"enabled", "disabled", "stopped", "host", "handled", "resumed"}) {
    CAPTURE(mode);
    const auto root = directory.path / mode;
    REQUIRE(std::filesystem::create_directory(root));
    x360n::ProcessSpec spec;
    spec.exe = X360N_GUEST_FAULT_PROBE;
    spec.args = {root.string(), mode};
    spec.working_dir = spec.exe.parent_path();
    spec.stdout_file = root / "stdout.log";
    spec.stderr_file = root / "stderr.log";
    spec.timeout = std::chrono::seconds(10);
    spec.capture_crash = true;
    const auto result = x360n::RunProcess(spec);
    REQUIRE(result.launched);
    REQUIRE_FALSE(result.timed_out);
    const std::string_view selection(mode);
    if (selection == "handled" || selection == "resumed") {
      CHECK(result.exit_code == 0);
      CHECK_FALSE(result.crash.captured);
    } else {
      CHECK(result.exit_code == 0xC0000005u);
      REQUIRE(result.crash.captured);
      CHECK(result.crash.access == "read 0x0000000100000005");
      CHECK_FALSE(result.crash.frames.empty());
    }
    const auto path = root / "fault.ppc.txt";
    if (selection == "disabled") { CHECK_FALSE(std::filesystem::exists(path)); continue; }
    const auto text = Read(path);
    if (selection == "resumed") {
      size_t count = 0, offset = 0;
      while ((offset = text.find("runtime-declined guest access", offset)) != std::string::npos) {
        ++count; ++offset;
      }
      CHECK(count == 16);
      continue;
    }
    if (selection != "enabled") { CHECK(text.empty()); continue; }
    CHECK(text.find("operation=read guest_address=00000005") != std::string::npos);
    CHECK(text.find("r00=0011223344556677") != std::string::npos);
    CHECK(text.find("r03=123456789ABCDEF0") != std::string::npos);
    CHECK(text.find("r31=FFEEDDCCBBAA9988") != std::string::npos);
    CHECK(text.find("lr=0000000090105000 ctr=00000000913F847C") != std::string::npos);
    CHECK(text.find("last_indirect_target=90116C88 cr=A0000005") != std::string::npos);
    CHECK(text.find("LR is not PC") != std::string::npos);
    CHECK(text.find("guest_memory[") != std::string::npos);
    CHECK(text.find("CAFEBABE") != std::string::npos);
    CHECK(text.find("DEADC0DE") != std::string::npos);
  }
}

TEST_CASE("Guest fault diagnostic preserves previous evidence and rejects unavailable destinations",
          "[sdk][guest-fault-context]") {
  Directory directory;
  rex::Runtime runtime(directory.path, directory.path);
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(runtime.Setup(std::move(config)) == 0);
  const auto path = directory.path / "existing.txt";
  { std::ofstream file(path); file << "preserve"; }
  x360n::GuestFaultContext diagnostic;
  CHECK_FALSE(diagnostic.Start(*runtime.memory(), path));
  CHECK(Read(path) == "preserve");
  CHECK_FALSE(diagnostic.Start(*runtime.memory(), directory.path / "missing" / "fault.txt"));
  CHECK_FALSE(diagnostic.Start(*runtime.memory(), {}));
  REQUIRE(diagnostic.Start(*runtime.memory(), directory.path / "new.txt"));
  CHECK_FALSE(diagnostic.Start(*runtime.memory(), directory.path / "other.txt"));
  CHECK_FALSE(std::filesystem::exists(directory.path / "other.txt"));
  diagnostic.Stop();
  diagnostic.Stop();
  REQUIRE(diagnostic.Start(*runtime.memory(), directory.path / "after-stop.txt"));
}
