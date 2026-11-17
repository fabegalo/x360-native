#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <vector>
#include <array>
#include "kernel/kernel_fixture.h"
#include "kernel/test_access.h"
#include "kernel/title_state.h"
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/function_dispatcher.h>
#include <rex/kernel/object/object_table.h>
#include <rex/kernel/types/content.h>
#include <rex/kernel/types/io.h>
#include <rex/xam/title_lifecycle.h>
#include <rex/xam/content_manager.h>
#include <rex/xam/system_ui.h>
#include <rex/xam/loader_state.h>
#include "xam/enumerator_object.h"
#include "xam/xam_state.h"
#include <rex/filesystem/file_system.h>
#include <rex/platform/file.h>
#include <rex/filesystem/stfs.h>
#include "synthetic_xex.h"

extern "C" REX_FUNC(__imp__XamContentAggregateCreateEnumerator);
extern "C" REX_FUNC(__imp__XamContentGetAttributesInternal);
extern "C" REX_FUNC(__imp__XamContentGetMetaDataInternal);
extern "C" REX_FUNC(__imp__XamContentCreateInternal);
extern "C" REX_FUNC(__imp__XamContentLaunchImageInternalEx);
extern "C" REX_FUNC(__imp__XamLoaderRegisterLaunchRequestCallback);
using namespace rex::kernel;

namespace {
std::array<uint32_t, 4> launch_preparation{};
unsigned preparation_calls = 0;
bool preparation_before_shutdown = false;
void PrepareLaunch(PPCContext& ctx, uint8_t* base) {
  auto* fields = reinterpret_cast<rex::be_u32*>(base + ctx.r3.u32);
  for (unsigned i = 0; i < 4; ++i) launch_preparation[i] = fields[i];
  ++preparation_calls;
  preparation_before_shutdown = !rex::kernel::StateOf(*REX_RUNTIME()).IsTerminatingTitle();
  fields[3] = 1;
}
}

TEST_CASE("Extracted executable metadata preserves disc and arcade identities", "[sdk][aggregate-content]") {
  const auto root = std::filesystem::temp_directory_path() /
      ("x360n-metadata-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{root};
  std::filesystem::create_directories(root);
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, root);
  auto& rt = fixture.GetRuntime();
  const auto output = rt.GetGuestHeap().SystemHeapAlloc(0xA000);
  REQUIRE(output);
  auto* bytes = reinterpret_cast<uint8_t*>(rt.GetMembase().GetBase() + (output));
  auto* data = reinterpret_cast<rex::kernel::XCONTENT_AGGREGATE_DATA*>(bytes);
  *data = {}; data->device_id = 1; data->title_id = 0x12345678;
  data->SetFileName("package"); data->SetDisplayName(u"Disc test");
  auto query = [&](bool async) {
    uint32_t result = ~0u;
    fixture.RunUntilGuestThreadEnds([&](rex::test::Caller& caller) {
      auto* ctx = &caller.GetContext();
      ctx->r3.u64 = output; ctx->r4.u64 = output + 0x400;
      ctx->r5.u64 = async ? output + 0x200 : 0;
      __imp__XamContentGetMetaDataInternal(*ctx, rt.GetMembase().GetBase());
      result = ctx->r3.u32;
    });
    return result;
  };
  for (const auto type : {0x4000u, 0xD0000u}) {
    data->content_type = rex::kernel::XContentType(type);
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
    CHECK(uint32_t(metadata->content_type.Load(std::memory_order_relaxed)) == type);
    CHECK(metadata->execution_info.title_id == 0x12345678);
    CHECK(metadata->execution_info.media_id == 0x23456789);
    CHECK(metadata->execution_info.version_value == 10);
    CHECK(metadata->execution_info.base_version_value == 9);
    CHECK(metadata->execution_info.disc_count == 2);
    CHECK(metadata->content_size == xex.size() + 3);
    CHECK(metadata->GetDisplayName(rex::kernel::XLanguage(1)) == u"Disc test");
    CHECK(bytes[0x400 + 0x93D6] == 0xA5);
    std::memset(bytes + 0x200, 0, sizeof(rex::kernel::XOVERLAPPED));
    CHECK(query(true) == X_ERROR_IO_PENDING);
    CHECK(rex::memory::load_and_swap<uint32_t>(bytes + 0x200) == 0);
    put(0x70C, 0x87654321); save();
    std::memset(bytes + 0x400, 0xA5, 0x93D7);
    CHECK(query(false) == 13);
    CHECK(bytes[0x400] == 0xA5);
    put(0x34, 0xFFFFFFF0); save();
    CHECK(query(false) == 13);
    data->SetFileName("../escape"); CHECK(query(false) == X_ERROR_INVALID_PARAMETER);
    data->SetFileName("missing"); CHECK(query(false) == X_ERROR_FILE_NOT_FOUND);
    data->SetFileName("package");
  }
}

TEST_CASE("Local save and profile metadata comes from persisted content",
          "[sdk][aggregate-content][local-content-metadata]") {
  const auto root = std::filesystem::temp_directory_path() /
      ("x360n-local-metadata-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{root};
  std::filesystem::create_directories(root);
  std::ofstream(root / "storage.toml") << "version=1\n[[device]]\nid=7\ntype=\"mu\"\n";
  std::filesystem::create_directories(root / "storage/00000007");
  constexpr uint64_t owner = 0xE003000000000123;
  for (unsigned restart = 0; restart != 2; ++restart) {
    rex::test::KernelFixture fixture({}, {}, {}, {}, {}, root);
  auto& rt = fixture.GetRuntime();
    const auto output = rt.GetGuestHeap().SystemHeapAlloc(0xA000);
    REQUIRE(output);
    auto* bytes = reinterpret_cast<uint8_t*>(rt.GetMembase().GetBase() + (output));
    auto* data = reinterpret_cast<rex::kernel::XCONTENT_AGGREGATE_DATA*>(bytes);
    auto* manager = (&rex::xam::CurrentXAM().GetContentManager());
    auto query = [&](bool async) {
      uint32_t result = ~0u;
      fixture.RunUntilGuestThreadEnds([&](rex::test::Caller& caller) {
        auto* ctx = &caller.GetContext();
        ctx->r3.u64 = output; ctx->r4.u64 = output + 0x400;
        ctx->r5.u64 = async ? output + 0x200 : 0;
        __imp__XamContentGetMetaDataInternal(*ctx, rt.GetMembase().GetBase());
        result = ctx->r3.u32;
      });
      return result;
    };
    for (const auto type : {1u, 0x10000u}) {
      *data = {}; data->device_id = 1; data->xuid = owner;
      data->title_id = type == 1 ? 0x12345678 : 0xFFFE07D1;
      data->content_type = rex::kernel::XContentType(type);
      data->SetFileName("local"); data->SetDisplayName(u"Persisted name");
      const auto path = root / "E003000000000123" /
          (type == 1 ? "12345678/00000001/local" : "FFFE07D1/00010000/local");
      if (!restart) {
        std::filesystem::create_directories(path);
        std::ofstream(path / "payload", std::ios::binary).write("actual-data", 11);
        REQUIRE(manager->WriteContentHeaderFile(owner, *data));
        REQUIRE(manager->SetContentThumbnail(owner, *data, {'p','i','c'}));
      }
      // The request routes the query; its display name is not persisted truth.
      data->SetDisplayName(u"Untrusted request name");
      for (const auto async : {false, true}) {
        std::memset(bytes + 0x400, 0xA5, 0x93D7);
        std::memset(bytes + 0x200, 0, sizeof(rex::kernel::XOVERLAPPED));
        REQUIRE(query(async) == (async ? X_ERROR_IO_PENDING : 0));
        if (async) CHECK(rex::memory::load_and_swap<uint32_t>(bytes + 0x200) == 0);
        const auto* metadata = reinterpret_cast<const rex::filesystem::XContentMetadata*>(bytes + 0x400);
        CHECK(metadata->content_type == data->content_type);
        CHECK(metadata->execution_info.title_id == data->title_id);
        CHECK(metadata->profile_id == owner);
        CHECK(metadata->content_size == 14);
        CHECK(metadata->GetDisplayName(rex::kernel::XLanguage(1)) == u"Persisted name");
        CHECK(metadata->GetDisplayName(rex::kernel::XLanguage(12)) == u"Persisted name");
        CHECK(metadata->thumbnail_size == 3);
        CHECK(std::memcmp(metadata->thumbnail, "pic", 3) == 0);
        // The original PPC UI tests raw bit 0x40, not host C++ bitfield order.
        CHECK(metadata->flags.as_byte == 0x40);
        CHECK(bytes[0x400 + 0x93D6] == 0xA5);
      }
      if (!restart) {
        data->SetDisplayName(u"Persisted name");
        REQUIRE(manager->TransferContent(owner, *data, 7, owner, false, {}) == 0);
      } else {
        const auto copied = root / "storage/00000007/E003000000000123" /
            (type == 1 ? "12345678/00000001/local" : "FFFE07D1/00010000/local");
        std::ifstream payload(copied / "payload", std::ios::binary);
        CHECK(std::string(std::istreambuf_iterator<char>(payload), {}) == "actual-data");
        data->device_id = 7;
        REQUIRE(query(false) == 0);
        const auto* metadata = reinterpret_cast<const rex::filesystem::XContentMetadata*>(bytes + 0x400);
        CHECK(metadata->flags.as_byte == 0x40);
        CHECK(metadata->profile_id == owner);
        CHECK(metadata->content_size == 14);
        data->device_id = 1;
      }
      if (restart) {
        auto header = path.parent_path().parent_path() / "Headers" /
            (type == 1 ? "00000001/local.header" : "00010000/local.header");
        std::ofstream(header, std::ios::binary | std::ios::trunc).write("short", 5);
        std::memset(bytes + 0x400, 0xA5, 0x93D7);
        CHECK(query(false) != 0);
        CHECK(bytes[0x400] == 0xA5);
        data->SetFileName("missing"); CHECK(query(false) == X_ERROR_FILE_NOT_FOUND);
      }
    }
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
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, root);
  auto& rt = fixture.GetRuntime();
  const auto image = x360n::test::BuildXex({}, nullptr, 0, x360n::test::Entry::kReturnZero);
  const auto output = rt.GetGuestHeap().SystemHeapAlloc(4096);
  REQUIRE(output);
  std::set<uint64_t> owners;
  std::vector<uint64_t> sizes;
  size_t record_size = 0;
  auto query = [&](uint32_t title, uint64_t xuid = 0) {
    owners.clear(); sizes.clear();
    uint32_t result = ~0u;
    fixture.RunUntilGuestThreadEnds([&](rex::test::Caller& caller) {
      auto* ctx = &caller.GetContext();
      ctx->r3.u64 = xuid; ctx->r4.u64 = 1; ctx->r5.u64 = 0xFFFFFFFF;
      ctx->r6.u64 = title; ctx->r7.u64 = output;
      __imp__XamContentAggregateCreateEnumerator(*ctx, rt.GetMembase().GetBase());
      result = ctx->r3.u32;

    });
    REQUIRE(result == 0);
    auto handle = *reinterpret_cast<rex::be_u32*>(rt.GetMembase().GetBase()+output);
    auto e = rex::kernel::Pinned<rex::xam::EnumeratorObject>(rt.GetObjectTable().Get(handle));
    REQUIRE(e);
    record_size = e->item_size;
    std::set<std::pair<uint32_t, uint32_t>> found;
    for (unsigned i = 0; i < 8; ++i) {
      uint32_t count = 0;
      const auto status = e->write_items(output + 16, (rt.GetMembase().GetBase() + output + 16), &count);
      if (!count) break;
      REQUIRE(status == 0);
      auto* data = reinterpret_cast<rex::kernel::XCONTENT_AGGREGATE_DATA*>(rt.GetMembase().GetBase() + (output + 16));
      owners.insert(uint64_t(data->xuid));
      sizes.push_back(rex::memory::load_and_swap<uint64_t>(reinterpret_cast<uint8_t*>(data) + 0x148));
      found.emplace(data->title_id, uint32_t(data->content_type.Load(std::memory_order_relaxed)));
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
  auto* data = reinterpret_cast<rex::kernel::XCONTENT_AGGREGATE_DATA*>(rt.GetMembase().GetBase() + (output + 1024));
  *data = {};
  data->device_id = 1; data->content_type = rex::kernel::XContentType(0xD0000);
  data->title_id = 0x584108B7; data->SetFileName("package-a");
  auto attributes = [&](bool async) {
    uint32_t result = ~0u;
    fixture.RunUntilGuestThreadEnds([&](rex::test::Caller& caller) {
      auto* ctx = &caller.GetContext();
      ctx->r3.u64 = output + 1024; ctx->r4.u64 = output + 2048;
      ctx->r5.u64 = async ? output + 3072 : 0;
      __imp__XamContentGetAttributesInternal(*ctx, rt.GetMembase().GetBase());
      result = ctx->r3.u32;
    });
    return result;
  };
  auto* bytes = reinterpret_cast<uint8_t*>(rt.GetMembase().GetBase() + (output + 2048));
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
  std::memcpy(rt.GetMembase().GetBase() + output + 2560, "shared-test", sizeof("shared-test"));
  uint32_t open_result = ~0u;
  fixture.RunUntilGuestThreadEnds([&](rex::test::Caller& caller) {
    auto* ctx = &caller.GetContext();
    ctx->r3.u64 = output + 2560; ctx->r4.u64 = output + 1024; ctx->r5.u64 = 3;
    ctx->r6.u64 = 0; ctx->r7.u64 = 0; ctx->r8.u64 = 0; ctx->r9.u64 = 0; ctx->r10.u64 = 0;
    __imp__XamContentCreateInternal(*ctx, rt.GetMembase().GetBase());
    open_result = ctx->r3.u32;
  });
  CHECK(open_result == 0);
  CHECK(rt.GetFileSystem().ResolveHostPath("shared-test:\\").value_or(std::filesystem::path{}) == rex::platform::HostPath(package));
  REQUIRE(rex::xam::CurrentXAM().GetContentManager().CloseContent("shared-test"));
  data->SetFileName("missing");
  CHECK(attributes(false) == X_ERROR_FILE_NOT_FOUND);
  std::memset((rt.GetMembase().GetBase() + output + 3072), 0, sizeof(rex::kernel::XOVERLAPPED));
  CHECK(attributes(true) == X_ERROR_IO_PENDING);
  CHECK(*reinterpret_cast<rex::be_u32*>(rt.GetMembase().GetBase()+output+3072) == X_ERROR_FILE_NOT_FOUND);

  auto* xam = &REX_INTERFACE(rex::xam::IXAMTitleServices)->GetSystemUI();
  auto launch = [&](uint32_t flags) {
    uint32_t result = ~0u;
    fixture.RunUntilGuestThreadEnds([&](rex::test::Caller& caller) {
      auto* ctx = &caller.GetContext();
      ctx->r3.u64 = output + 1024; ctx->r4.u64 = 0; ctx->r5.u64 = flags;
      __imp__XamContentLaunchImageInternalEx(*ctx, rt.GetMembase().GetBase());
      result = ctx->r3.u32;
    });
    return result;
  };
  CHECK(launch(0) != 0);  // missing package must not request a transition
  CHECK(REX_INTERFACE(rex::xam::IXAMTitleServices)->GetLoaderState().launch_path.empty());
  data->SetFileName("package-a");
  CHECK(launch(0) == X_ERROR_FILE_NOT_FOUND);  // exists, but has no executable
  CHECK(REX_INTERFACE(rex::xam::IXAMTitleServices)->GetLoaderState().launch_path.empty());
  CHECK(rt.GetFileSystem().ResolveHostPath("XSYSLAUNCH:\\").value_or(std::filesystem::path{}).empty());
  CHECK(launch(1) == X_RESULT(50));  // unsupported loader semantics
  std::ofstream exe(package / "default.xex", std::ios::binary);
  exe.write(reinterpret_cast<const char*>(image.data()), image.size()); exe.close();
  REQUIRE((&rex::kernel::GetFunctionDispatcher(rt))->SetFunction(0x8F000100, PrepareLaunch));
  PPCContext register_context{};
  register_context.r3.u64 = 0x8F000100;
  __imp__XamLoaderRegisterLaunchRequestCallback(register_context, rt.GetMembase().GetBase());
  CHECK(xam->launch_request_callback.load() == 0x8F000100);
  register_context.r3.u64 = 0;
  __imp__XamLoaderRegisterLaunchRequestCallback(register_context, rt.GetMembase().GetBase());
  CHECK(xam->launch_request_callback.load() == 0);
  register_context.r3.u64 = 0x8F000100;
  __imp__XamLoaderRegisterLaunchRequestCallback(register_context, rt.GetMembase().GetBase());
  preparation_calls = 0;
  CHECK(launch(0) == X_RESULT(193)); // missing execution info
  CHECK(preparation_calls == 0);
  CHECK(REX_INTERFACE(rex::xam::IXAMTitleServices)->GetLoaderState().launch_path.empty());
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
    CHECK(REX_INTERFACE(rex::xam::IXAMTitleServices)->GetLoaderState().launch_path.empty());
    CHECK_FALSE(rex::kernel::StateOf(rt).IsTerminatingTitle());
    return X_RESULT(1460);
  };
  CHECK(launch(0) == X_RESULT(1460));
  CHECK(transition_calls == 1);
  CHECK(REX_INTERFACE(rex::xam::IXAMTitleServices)->GetLoaderState().launch_path.empty());
  CHECK(rt.GetFileSystem().ResolveHostPath("XSYSLAUNCH:\\").value_or(std::filesystem::path{}).empty());
  xam->launch_display_transition = [&] { ++transition_calls; return X_ERROR_SUCCESS; };
  launch(0);  // only a completed transition may terminate the calling guest thread
  CHECK(transition_calls == 2);
  CHECK(preparation_calls == 2);
  CHECK(launch_preparation == std::array<uint32_t, 4>{0x12345678, 0x10203040, 1, 0});
  CHECK(preparation_before_shutdown);
  CHECK(xam->launch_request_internal_flags.load() == 0x200);
  CHECK(REX_INTERFACE(rex::xam::IXAMTitleServices)->GetLoaderState().launch_flags == 0); // internal 0x200 is not a caller flag
  CHECK(REX_INTERFACE(rex::xam::IXAMTitleServices)->GetLoaderState().launch_path == "XSYSLAUNCH:\\default.xex");
  CHECK(rt.GetFileSystem().ResolveHostPath("XSYSLAUNCH:\\").value_or(std::filesystem::path{}) == rex::platform::HostPath(package));

}
