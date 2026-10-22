#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <vector>
#include <array>
#include <rex/system/function_dispatcher.h>
#include <rex/kernel/init.h>
#include <rex/kernel/xam/module.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/xam/content_manager.h>
#include <rex/system/xenumerator.h>
#include <rex/system/xthread.h>
#include <rex/system/user_module.h>
#include <rex/filesystem/devices/stfs_xbox.h>
#include "synthetic_xex.h"

extern "C" REX_FUNC(__imp__XamContentAggregateCreateEnumerator);
extern "C" REX_FUNC(__imp__XamContentGetAttributesInternal);
extern "C" REX_FUNC(__imp__XamContentGetMetaDataInternal);
extern "C" REX_FUNC(__imp__XamContentCreateInternal);
extern "C" REX_FUNC(__imp__XamContentLaunchImageInternalEx);
extern "C" REX_FUNC(__imp__XamLoaderRegisterLaunchRequestCallback);
using rex::X_STATUS;
using rex::X_RESULT;

namespace {
std::array<uint32_t, 4> launch_preparation{};
unsigned preparation_calls = 0;
bool preparation_before_shutdown = false;
void PrepareLaunch(PPCContext& ctx, uint8_t* base) {
  auto* fields = reinterpret_cast<rex::be<uint32_t>*>(base + ctx.r3.u32);
  for (unsigned i = 0; i < 4; ++i) launch_preparation[i] = fields[i];
  ++preparation_calls;
  preparation_before_shutdown = !rex::Runtime::instance()->kernel_state()->is_terminating_title();
  fields[3] = 1;
}
}

TEST_CASE("Extracted executable metadata preserves disc and arcade identities", "[sdk][aggregate-content]") {
  const auto root = std::filesystem::temp_directory_path() /
      ("x360n-metadata-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{root};
  std::filesystem::create_directories(root);
  rex::Runtime rt(root, root);
  rex::RuntimeConfig config{};
  config.tool_mode = true; config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  const auto output = rt.memory()->SystemHeapAlloc(0xA000);
  REQUIRE(output);
  auto* bytes = rt.memory()->TranslateVirtual<uint8_t*>(output);
  auto* data = reinterpret_cast<rex::system::xam::XCONTENT_AGGREGATE_DATA*>(bytes);
  *data = {}; data->device_id = 1; data->title_id = 0x12345678;
  data->set_file_name("package"); data->set_display_name(u"Disc test");
  auto query = [&](bool async) {
    uint32_t result = ~0u;
    auto thread = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
        rt.kernel_state(), 128 * 1024, 0, [&]() {
      auto* ctx = rex::system::XThread::GetCurrentThread()->thread_state()->context();
      ctx->r3.u64 = output; ctx->r4.u64 = output + 0x400;
      ctx->r5.u64 = async ? output + 0x200 : 0;
      __imp__XamContentGetMetaDataInternal(*ctx, rt.memory()->virtual_membase());
      result = ctx->r3.u32; return 0;
    }));
    REQUIRE(thread->Create() == X_STATUS_SUCCESS);
    uint64_t timeout = uint64_t(-50'000'000ll);
    REQUIRE(thread->Wait(0, 0, 0, &timeout) == X_STATUS_SUCCESS);
    return result;
  };
  for (const auto type : {0x4000u, 0xD0000u}) {
    data->content_type = rex::system::XContentType(type);
    auto path = root / "0000000000000000/12345678" / (type == 0x4000 ? "00004000" : "000D0000") / "package";
    std::filesystem::create_directories(path);
    auto xex = x360n::test::BuildXex({}, nullptr, 0, x360n::test::Entry::kReturnZero);
    auto put = [&](size_t offset, uint32_t value) { rex::memory::store_and_swap<uint32_t>(xex.data() + offset, value); };
    put(0x14, 4); put(0x30, 0x40006); put(0x34, 0x700);
    put(0x700, 0x23456789); put(0x704, 10); put(0x708, 9); put(0x70C, 0x12345678);
    xex[0x710] = 2; xex[0x712] = 1; xex[0x713] = 2;
    auto save = [&] { std::ofstream(path / "default.xex", std::ios::binary | std::ios::trunc).write(reinterpret_cast<const char*>(xex.data()), xex.size()); };
    save();
    std::ofstream(path / "payload", std::ios::binary).write("abc", 3);
    std::memset(bytes + 0x400, 0xA5, 0x93D7);
    CHECK(query(false) == 0);
    const auto* metadata = reinterpret_cast<const rex::filesystem::XContentMetadata*>(bytes + 0x400);
    CHECK(uint32_t(metadata->content_type.get()) == type);
    CHECK(metadata->execution_info.title_id == 0x12345678);
    CHECK(metadata->execution_info.media_id == 0x23456789);
    CHECK(metadata->execution_info.version_value == 10);
    CHECK(metadata->execution_info.base_version_value == 9);
    CHECK(metadata->execution_info.disc_count == 2);
    CHECK(metadata->content_size == xex.size() + 3);
    CHECK(metadata->display_name(rex::system::XLanguage(1)) == u"Disc test");
    CHECK(bytes[0x400 + 0x93D6] == 0xA5);
    std::memset(bytes + 0x200, 0, sizeof(rex::system::XAM_OVERLAPPED));
    CHECK(query(true) == X_ERROR_IO_PENDING);
    CHECK(rex::memory::load_and_swap<uint32_t>(bytes + 0x200) == 0);
    put(0x70C, 0x87654321); save();
    std::memset(bytes + 0x400, 0xA5, 0x93D7);
    CHECK(query(false) == 13);
    CHECK(bytes[0x400] == 0xA5);
    put(0x34, 0xFFFFFFF0); save();
    CHECK(query(false) == 13);
    data->set_file_name("../escape"); CHECK(query(false) == X_ERROR_INVALID_PARAMETER);
    data->set_file_name("missing"); CHECK(query(false) == X_ERROR_FILE_NOT_FOUND);
    data->set_file_name("package");
  }
}

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
  std::set<uint64_t> owners;
  std::vector<uint64_t> sizes;
  size_t record_size = 0;
  auto query = [&](uint32_t title, uint64_t xuid = 0) {
    owners.clear(); sizes.clear();
    uint32_t result = ~0u;
    auto thread = rex::system::object_ref<rex::system::XHostThread>(
        new rex::system::XHostThread(rt.kernel_state(), 128 * 1024, 0, [&]() {
      auto* ctx = rex::system::XThread::GetCurrentThread()->thread_state()->context();
      ctx->r3.u64 = xuid; ctx->r4.u64 = 1; ctx->r5.u64 = 0xFFFFFFFF;
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
    record_size = e->item_size();
    std::set<std::pair<uint32_t, uint32_t>> found;
    for (unsigned i = 0; i < 8; ++i) {
      uint32_t count = 0;
      const auto status = e->WriteItems(output + 16, rt.memory()->TranslateVirtual(output + 16), &count);
      if (!count) break;
      REQUIRE(status == 0);
      auto* data = rt.memory()->TranslateVirtual<rex::system::xam::XCONTENT_AGGREGATE_DATA*>(output + 16);
      owners.insert(uint64_t(data->xuid));
      sizes.push_back(rex::memory::load_and_swap<uint64_t>(reinterpret_cast<uint8_t*>(data) + 0x148));
      found.emplace(data->title_id, uint32_t(data->content_type.get()));
    }
    return found;
  };
  // Original filter 816876B4: XUID 0 accepts every owner (the Storage query),
  // -1 only shared content, any other only that owner.
  CHECK(query(0) == std::set<std::pair<uint32_t, uint32_t>>{{0x584108B7, 0xD0000}, {0x12345678, 0x80000}});
  CHECK(owners == std::set<uint64_t>{0, 1});
  CHECK(query(0, ~0ull) == std::set<std::pair<uint32_t, uint32_t>>{{0x584108B7, 0xD0000}, {0x12345678, 0x80000}});
  CHECK(owners == std::set<uint64_t>{0});
  CHECK(query(0, 1) == std::set<std::pair<uint32_t, uint32_t>>{{0x584108B7, 0xD0000}});
  CHECK(owners == std::set<uint64_t>{1});
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
  // 584-byte original records (816EEDF0) carry the size at +0x148 (81684BC0).
  CHECK(query(0x584108B7, ~0ull).size() == 1);
  CHECK(record_size == 584);
  CHECK(sizes == std::vector<uint64_t>{15});
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
  REQUIRE(rt.function_dispatcher()->InitializeFunctionTable(0x82010000, 0x1000, 0x82000000, 0x20000));
  REQUIRE(rt.function_dispatcher()->SetFunction(0x82010000, PrepareLaunch));
  PPCContext register_context{};
  register_context.r3.u64 = 0x82010000;
  __imp__XamLoaderRegisterLaunchRequestCallback(register_context, rt.memory()->virtual_membase());
  CHECK(xam->launch_request_callback.load() == 0x82010000);
  register_context.r3.u64 = 0;
  __imp__XamLoaderRegisterLaunchRequestCallback(register_context, rt.memory()->virtual_membase());
  CHECK(xam->launch_request_callback.load() == 0);
  register_context.r3.u64 = 0x82010000;
  __imp__XamLoaderRegisterLaunchRequestCallback(register_context, rt.memory()->virtual_membase());
  preparation_calls = 0;
  CHECK(launch(0) == X_RESULT_FROM_WIN32(193)); // missing execution info
  CHECK(preparation_calls == 0);
  CHECK(xam->loader_data().launch_path.empty());
  auto target = image;
  auto write32 = [&](size_t offset, uint32_t value) {
    rex::memory::store_and_swap<uint32_t>(target.data() + offset, value);
  };
  // Add a fourth optional header to this synthetic fixture's unused space.
  write32(0x14, 4); write32(0x30, 0x00040006); write32(0x34, 0x700);
  write32(0x708, 0x10203040); write32(0x70C, 0x12345678);
  std::ofstream(package / "default.xex", std::ios::binary | std::ios::trunc)
      .write(reinterpret_cast<const char*>(target.data()), target.size());
  unsigned transition_calls = 0;
  xam->launch_display_transition = [&] {
    ++transition_calls;
    CHECK(xam->launch_request_internal_flags.load() == 0x200);
    CHECK(xam->loader_data().launch_path.empty());
    CHECK_FALSE(rt.kernel_state()->is_terminating_title());
    return X_RESULT_FROM_WIN32(1460);
  };
  CHECK(launch(0) == X_RESULT_FROM_WIN32(1460));
  CHECK(transition_calls == 1);
  CHECK(xam->loader_data().launch_path.empty());
  CHECK(rt.kernel_state()->content_manager()->GetOpenPackagePath("XSYSLAUNCH").empty());
  xam->launch_display_transition = [&] { ++transition_calls; return X_ERROR_SUCCESS; };
  launch(0);  // only a completed transition may terminate the calling guest thread
  CHECK(transition_calls == 2);
  CHECK(preparation_calls == 2);
  CHECK(launch_preparation == std::array<uint32_t, 4>{0x12345678, 0x10203040, 1, 0});
  CHECK(preparation_before_shutdown);
  CHECK(xam->launch_request_internal_flags.load() == 0x200);
  CHECK(xam->loader_data().launch_flags == 0); // internal 0x200 is not a caller flag
  CHECK(xam->loader_data().launch_path == "XSYSLAUNCH:\\default.xex");
  CHECK(rt.kernel_state()->content_manager()->GetOpenPackagePath("XSYSLAUNCH") == package);

}
