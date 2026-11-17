#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <rex/filesystem/file_system.h>
#include <rex/kernel/object/file_object.h>
#include <rex/kernel/object/object_table.h>
#include <rex/kernel/title_runtime.h>
#include "kernel/kernel_fixture.h"

TEST_CASE("XCTD query identifies the encoded stream without consuming file data",
          "[sdk][xctd-query]") {
  namespace fs = rex::filesystem;
  using namespace rex::kernel;
  const std::vector<std::string> contents = {
      "plain data", std::string("\x0f\xf5\x12\xedpayload", 11),
      std::string("\xed\x12\xf5\x0f", 4), "", std::string("\x0f\xf5\x12", 3)};
  rex::test::KernelFixture fixture([&](const std::filesystem::path& root) {
    for (size_t i = 0; i < contents.size(); ++i) {
      std::ofstream file(root / std::to_string(i), std::ios::binary);
      file.write(contents[i].data(), contents[i].size());
    }
  });
  fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
    auto& runtime = caller.GetRuntime();
    const auto address = caller.Alloc(64);
    const auto query = [&](uint32_t handle, uint32_t length) {
      return caller.Kernel("NtQueryInformationFile", {handle, address, address + 16, length, 27});
    };
    const auto open = [&](size_t i, fs::Access access) {
      auto opened = runtime.GetFileSystem().Open("game:\\" + std::to_string(i), {.access = access});
      REQUIRE(opened);
      auto handle = runtime.GetObjectTable().AddFile(std::move(opened->file), true);
      REQUIRE(handle);
      return *handle;
    };
    for (size_t i = 0; i < contents.size(); ++i) {
      CAPTURE(i);
      const auto handle = open(i, fs::Access::kReadData);
      {
        auto file = runtime.GetObjectTable().Get<FileObject>(handle);
        REQUIRE(file);
        file->SetPosition(17);
        std::memset(caller.GetBase() + address, 0xA5, 64);
        CHECK(query(handle, 4) == X_STATUS_SUCCESS);
        CHECK(caller.Load32(address) == X_STATUS_SUCCESS);
        CHECK(caller.Load32(address + 4) == 4);
        CHECK(caller.Load32(address + 16) == (i == 1 ? 1u : 0u));
        CHECK(caller.Load32(address + 20) == 0xA5A5A5A5);
        CHECK(file->GetPosition() == 17);
        CHECK(query(handle, 3) == X_STATUS_INFO_LENGTH_MISMATCH);
        CHECK(file->GetPosition() == 17);
      }
      REQUIRE(caller.Kernel("NtClose", {handle}) == X_STATUS_SUCCESS);
    }
    CHECK(query(0xFFFFFFFF, 4) == X_STATUS_INVALID_HANDLE);
    const auto handle = open(0, fs::Access::kNone);
    CHECK(query(handle, 4) == X_STATUS_ACCESS_DENIED);
    CHECK(caller.Load32(address) == X_STATUS_ACCESS_DENIED);
    CHECK(caller.Load32(address + 4) == 0);
    REQUIRE(caller.Kernel("NtClose", {handle}) == X_STATUS_SUCCESS);
  });
}
