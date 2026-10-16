#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/filesystem/vfs.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/xfile.h>
#include <rex/system/xthread.h>

extern "C" REX_FUNC(__imp__NtQueryInformationFile);

TEST_CASE("XCTD query identifies the encoded stream without consuming file data",
          "[sdk][xctd-query]") {
  using namespace rex;
  using namespace rex::system;
  const auto root = std::filesystem::temp_directory_path() /
      ("x360n-xctd-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directory(root);
  struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() { std::error_code ec; std::filesystem::remove_all(root, ec); }
  } cleanup{root};
  const std::vector<std::string> contents = {
      "plain data", std::string("\x0f\xf5\x12\xedpayload", 11),
      std::string("\xed\x12\xf5\x0f", 4), "", std::string("\x0f\xf5\x12", 3)};
  for (size_t i = 0; i < contents.size(); ++i) {
    std::ofstream file(root / std::to_string(i), std::ios::binary);
    file.write(contents[i].data(), contents[i].size());
  }
  Runtime rt(root, root);
  RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  auto* memory = rt.memory();
  const auto address = memory->SystemHeapAlloc(64);
  REQUIRE(address);
  auto* words = memory->TranslateVirtual<be<uint32_t>*>(address);
  auto call = [&](uint32_t handle, uint32_t length) {
    uint32_t result = ~0u;
    auto thread = object_ref<XHostThread>(new XHostThread(rt.kernel_state(), 128 * 1024, 0, [&] {
      auto& ctx = *XThread::GetCurrentThread()->thread_state()->context();
      ctx.r3.u64 = handle;
      ctx.r4.u64 = address;
      ctx.r5.u64 = address + 16;
      ctx.r6.u64 = length;
      ctx.r7.u64 = 27;
      __imp__NtQueryInformationFile(ctx, memory->virtual_membase());
      result = ctx.r3.u32;
      return 0;
    }));
    REQUIRE(thread->Create() == X_STATUS_SUCCESS);
    uint64_t timeout = uint64_t(-50'000'000ll);
    REQUIRE(thread->Wait(0, 0, 0, &timeout) == X_STATUS_SUCCESS);
    return result;
  };
  for (size_t i = 0; i < contents.size(); ++i) {
    auto* entry = rt.file_system()->ResolvePath("game:\\" + std::to_string(i));
    REQUIRE(entry);
    filesystem::File* host = nullptr;
    REQUIRE(entry->Open(filesystem::FileAccess::kFileReadData, &host) == X_STATUS_SUCCESS);
    auto file = object_ref<XFile>(new XFile(rt.kernel_state(), host, true));
    file->set_position(17);
    std::memset(words, 0xA5, 64);
    CHECK(call(file->handle(), 4) == X_STATUS_SUCCESS);
    CHECK(uint32_t(words[0]) == X_STATUS_SUCCESS);
    CHECK(uint32_t(words[1]) == 4);
    CHECK(uint32_t(words[4]) == (i == 1 ? 1u : 0u));
    CHECK(uint32_t(words[5]) == 0xA5A5A5A5);
    CHECK(file->position() == 17);
    CHECK(call(file->handle(), 3) == X_STATUS_INFO_LENGTH_MISMATCH);
    CHECK(file->position() == 17);
    file->ReleaseHandle();
  }
  CHECK(call(0xFFFFFFFF, 4) == X_STATUS_INVALID_HANDLE);
  auto* entry = rt.file_system()->ResolvePath("game:\\0");
  filesystem::File* host = nullptr;
  REQUIRE(entry->Open(0, &host) == X_STATUS_SUCCESS);  // No read-data access.
  auto file = object_ref<XFile>(new XFile(rt.kernel_state(), host, true));
  CHECK(call(file->handle(), 4) == X_STATUS_ACCESS_DENIED);
  CHECK(uint32_t(words[0]) == X_STATUS_ACCESS_DENIED);
  CHECK(uint32_t(words[1]) == 0);
  file->ReleaseHandle();
  memory->SystemHeapFree(address);
}
