#include <catch2/catch_test_macros.hpp>
#include "../../titles/common/guest_fault_context.h"
#include "../supervisor/scratch.h"
#include "child_process.h"
#include "kernel/kernel_fixture.h"
#include <fstream>
#include <charconv>
#include <format>

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
      const auto stdout_text = Read(spec.stdout_file);
      constexpr std::string_view marker = "Guest memory mapped at virtual base 0x";
      const auto start = stdout_text.find(marker);
      REQUIRE(start != std::string::npos);
      const auto offset = start + marker.size();
      REQUIRE(stdout_text.size() >= offset + 16);
      uint64_t guest_base = 0;
      const auto parsed = std::from_chars(stdout_text.data() + offset,
          stdout_text.data() + offset + 16, guest_base, 16);
      REQUIRE(parsed.ec == std::errc{});
      REQUIRE(parsed.ptr == stdout_text.data() + offset + 16);
      CHECK(result.crash.access == std::format("read 0x{:016x}", guest_base + 5));
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
  rex::test::KernelFixture fixture;
  auto& runtime = fixture.GetRuntime();
  const auto path = directory.path / "existing.txt";
  { std::ofstream file(path); file << "preserve"; }
  x360n::GuestFaultContext diagnostic;
  CHECK_FALSE(diagnostic.Start(runtime, path));
  CHECK(Read(path) == "preserve");
  CHECK_FALSE(diagnostic.Start(runtime, directory.path / "missing" / "fault.txt"));
  CHECK_FALSE(diagnostic.Start(runtime, {}));
  REQUIRE(diagnostic.Start(runtime, directory.path / "new.txt"));
  CHECK_FALSE(diagnostic.Start(runtime, directory.path / "other.txt"));
  CHECK_FALSE(std::filesystem::exists(directory.path / "other.txt"));
  diagnostic.Stop();
  diagnostic.Stop();
  REQUIRE(diagnostic.Start(runtime, directory.path / "after-stop.txt"));
}
