#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <rex/kernel/init.h>
#include <rex/kernel/xam/module.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/xam/content_manager.h>
#include <rex/system/xenumerator.h>
#include <rex/system/xthread.h>
#include <rex/system/user_module.h>
#include "synthetic_xex.h"

extern "C" REX_FUNC(__imp__XamContentAggregateCreateEnumerator);
extern "C" REX_FUNC(__imp__XamContentGetAttributesInternal);
extern "C" REX_FUNC(__imp__XamContentCreateInternal);
extern "C" REX_FUNC(__imp__XamContentLaunchImageInternalEx);
using rex::X_STATUS;
using rex::X_RESULT;

TEST_CASE("Aggregate all-types query enumerates installed titles with their real types",
          "[sdk][aggregate-content]") {
  const auto root = std::filesystem::temp_directory_path() /
      ("x360n-aggregate-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{root};
  for (const auto* path : {"0000000000000000/584108B7/000D0000/package-a",
                           "0000000000000000/12345678/00080000/package-b",
                           "0000000000000000/invalid/000D0000/not-a-title",
                           "0000000000000000/584108B7/Headers/000D0000",
                           "0000000000000001/584108B7/000D0000/another-user"}) {
    std::filesystem::create_directories(root / path);
  }
  rex::Runtime rt(root, root);
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  const auto image = x360n::test::BuildXex({}, nullptr, 0, x360n::test::Entry::kReturnZero);
  auto module = rex::system::object_ref<rex::system::UserModule>(
      new rex::system::UserModule(rt.kernel_state()));
  {
    std::ofstream file(root / "default.xex", std::ios::binary);
    file.write(reinterpret_cast<const char*>(image.data()), image.size());
  }
  // Load the existing synthetic fixture, without launching guest code.
  REQUIRE(module->LoadFromFile("game:\\default.xex") == X_STATUS_SUCCESS);
  rt.kernel_state()->SetExecutableModule(module);
  const auto output = rt.memory()->SystemHeapAlloc(4096);
  REQUIRE(output);
  auto query = [&](uint32_t title) {
    uint32_t result = ~0u;
    auto thread = rex::system::object_ref<rex::system::XHostThread>(
        new rex::system::XHostThread(rt.kernel_state(), 128 * 1024, 0, [&]() {
      auto* ctx = rex::system::XThread::GetCurrentThread()->thread_state()->context();
      ctx->r3.u64 = 0; ctx->r4.u64 = 1; ctx->r5.u64 = 0xFFFFFFFF;
      ctx->r6.u64 = title; ctx->r7.u64 = output;
      __imp__XamContentAggregateCreateEnumerator(*ctx, rt.memory()->virtual_membase());
      result = ctx->r3.u32;
      return 0;
    }));
    REQUIRE(thread->Create() == X_STATUS_SUCCESS);
    uint64_t timeout = uint64_t(-50'000'000ll);
    REQUIRE(thread->Wait(0, 0, 0, &timeout) == X_STATUS_SUCCESS);
    REQUIRE(result == 0);
    auto handle = *rt.memory()->TranslateVirtual<rex::be<uint32_t>*>(output);
    auto e = rt.kernel_state()->object_table()->LookupObject<rex::system::XEnumerator>(handle);
    REQUIRE(e);
    std::set<std::pair<uint32_t, uint32_t>> found;
    for (unsigned i = 0; i < 8; ++i) {
      uint32_t count = 0;
      const auto status = e->WriteItems(output + 16, rt.memory()->TranslateVirtual(output + 16), &count);
      if (!count) break;
      REQUIRE(status == 0);
      auto* data = rt.memory()->TranslateVirtual<rex::system::xam::XCONTENT_AGGREGATE_DATA*>(output + 16);
      CHECK(uint64_t(data->xuid) == 0);
      found.emplace(data->title_id, uint32_t(data->content_type.get()));
    }
    return found;
  };
  CHECK(query(0) == std::set<std::pair<uint32_t, uint32_t>>{{0x584108B7, 0xD0000}, {0x12345678, 0x80000}});
  CHECK(query(0x584108B7) == std::set<std::pair<uint32_t, uint32_t>>{{0x584108B7, 0xD0000}});
  // Exercise the export, including packed 36-byte output and failure completion.
  const auto package = root / "0000000000000000/584108B7/000D0000/package-a";
  std::ofstream(package / "payload", std::ios::binary).write("actual bytes", 12);
  std::filesystem::create_directories(package / "sub");
  std::ofstream(package / "sub/more", std::ios::binary).write("abc", 3);
  auto* data = rt.memory()->TranslateVirtual<rex::system::xam::XCONTENT_AGGREGATE_DATA*>(output + 1024);
  *data = {};
  data->device_id = 1; data->content_type = rex::system::XContentType(0xD0000);
  data->title_id = 0x584108B7; data->set_file_name("package-a");
  auto attributes = [&](bool async) {
    uint32_t result = ~0u;
    auto thread = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
        rt.kernel_state(), 128 * 1024, 0, [&]() {
      auto* ctx = rex::system::XThread::GetCurrentThread()->thread_state()->context();
      ctx->r3.u64 = output + 1024; ctx->r4.u64 = output + 2048;
      ctx->r5.u64 = async ? output + 3072 : 0;
      __imp__XamContentGetAttributesInternal(*ctx, rt.memory()->virtual_membase());
      result = ctx->r3.u32; return 0;
    }));
    REQUIRE(thread->Create() == X_STATUS_SUCCESS);
    uint64_t timeout = uint64_t(-50'000'000ll);
    REQUIRE(thread->Wait(0, 0, 0, &timeout) == X_STATUS_SUCCESS);
    return result;
  };
  auto* bytes = rt.memory()->TranslateVirtual<uint8_t*>(output + 2048);
  std::memset(bytes, 0xA5, 40);
  CHECK(attributes(false) == 0);
  CHECK(rex::memory::load_and_swap<uint32_t>(bytes) == 0x10);
  CHECK(rex::memory::load_and_swap<uint64_t>(bytes + 28) == 15);
  CHECK(bytes[36] == 0xA5);
  // Internal aggregate records carry the owner, including XUID=0 (shared).
  std::strcpy(reinterpret_cast<char*>(rt.memory()->TranslateVirtual(output + 2560)), "shared-test");
  uint32_t open_result = ~0u;
  auto open_thread = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
      rt.kernel_state(), 128 * 1024, 0, [&]() {
    auto* ctx = rex::system::XThread::GetCurrentThread()->thread_state()->context();
    ctx->r3.u64 = output + 2560; ctx->r4.u64 = output + 1024; ctx->r5.u64 = 3;
    ctx->r6.u64 = 0; ctx->r7.u64 = 0; ctx->r8.u64 = 0; ctx->r9.u64 = 0; ctx->r10.u64 = 0;
    __imp__XamContentCreateInternal(*ctx, rt.memory()->virtual_membase());
    open_result = ctx->r3.u32; return 0;
  }));
  REQUIRE(open_thread->Create() == X_STATUS_SUCCESS);
  uint64_t open_timeout = uint64_t(-50'000'000ll);
  REQUIRE(open_thread->Wait(0, 0, 0, &open_timeout) == X_STATUS_SUCCESS);
  CHECK(open_result == 0);
  CHECK(rt.kernel_state()->content_manager()->GetOpenPackagePath("shared-test") == package);
  rt.kernel_state()->content_manager()->CloseContent("shared-test");
  data->set_file_name("missing");
  CHECK(attributes(false) == X_ERROR_FILE_NOT_FOUND);
  std::memset(rt.memory()->TranslateVirtual(output + 3072), 0, sizeof(rex::system::XAM_OVERLAPPED));
  CHECK(attributes(true) == X_ERROR_IO_PENDING);
  CHECK(*rt.memory()->TranslateVirtual<rex::be<uint32_t>*>(output + 3072) == X_ERROR_FILE_NOT_FOUND);

  auto xam = rt.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  auto launch = [&](uint32_t flags) {
    uint32_t result = ~0u;
    auto thread = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
        rt.kernel_state(), 128 * 1024, 0, [&]() {
      auto* ctx = rex::system::XThread::GetCurrentThread()->thread_state()->context();
      ctx->r3.u64 = output + 1024; ctx->r4.u64 = 0; ctx->r5.u64 = flags;
      __imp__XamContentLaunchImageInternalEx(*ctx, rt.memory()->virtual_membase());
      result = ctx->r3.u32; return 0;
    }));
    REQUIRE(thread->Create() == X_STATUS_SUCCESS);
    uint64_t timeout = uint64_t(-50'000'000ll);
    REQUIRE(thread->Wait(0, 0, 0, &timeout) == X_STATUS_SUCCESS);
    return result;
  };
  CHECK(launch(0) != 0);  // missing package must not request a transition
  CHECK(xam->loader_data().launch_path.empty());
  data->set_file_name("package-a");
  CHECK(launch(0) == X_ERROR_FILE_NOT_FOUND);  // exists, but has no executable
  CHECK(xam->loader_data().launch_path.empty());
  CHECK(rt.kernel_state()->content_manager()->GetOpenPackagePath("XSYSLAUNCH").empty());
  CHECK(launch(1) == X_RESULT_FROM_WIN32(50));  // unsupported loader semantics
  std::ofstream exe(package / "default.xex", std::ios::binary);
  exe.write(reinterpret_cast<const char*>(image.data()), image.size()); exe.close();
  launch(0);  // successful launch terminates the calling guest thread
  CHECK(xam->loader_data().launch_path == "XSYSLAUNCH:\\default.xex");
  CHECK(rt.kernel_state()->content_manager()->GetOpenPackagePath("XSYSLAUNCH") == package);

}
