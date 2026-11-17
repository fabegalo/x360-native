#include <catch2/catch_test_macros.hpp>
#include <array>
#include <atomic>
#include <cstring>
#include <fstream>
#include <rex/filesystem/file_system.h>
#include <rex/kernel/object/kernel_thread.h>
#include <rex/kernel/thread_registry.h>
#include <rex/kernel/object/object_table.h>
#include <rex/kernel/types/content.h>
#include <rex/platform/file.h>
#include "kernel/kernel_fixture.h"
#include "kernel/test_access.h"
#include <rex/kernel/function_dispatcher.h>
#include "xam/xam_state.h"

namespace {
constexpr uint32_t kTitle = 0x584108B7, kVersion = 0x10203040;
std::atomic<unsigned> callback_count{0};
void PrepareDisplay(PPCContext& ctx, uint8_t* base) {
  auto* fields = reinterpret_cast<rex::be_u32*>(base + ctx.r3.u32);
  CHECK(fields[0] == kTitle); CHECK(fields[1] == kVersion); CHECK(fields[2] == 1);
  CHECK(fields[3] == 0);
  fields[3] = 1;
  ctx.r31.u64 = 0xDEADBEEF;
  ++callback_count;
}
void TransferReply(PPCContext& ctx, uint8_t*) {
  ctx.r3.u64 = 0xABCDEF1200000003ull;
  ctx.r31.u64 = 0xDEADBEEF;
}
std::vector<uint8_t> LaunchHeader() {
  std::vector<uint8_t> bytes(128);
  auto put = [&](size_t offset, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = uint8_t(value >> (24 - i * 8));
  };
  std::memcpy(bytes.data(), "XEX2", 4);
  put(8, 128); put(20, 1); put(24, 0x00040006); put(28, 64);
  put(64 + 8, kVersion); put(64 + 12, kTitle);
  return bytes;
}

TEST_CASE("Canonical host workers return guest callback results and release their creation handle", "[sdk][xam][storage-transfer]") {
  rex::test::KernelFixture fixture;
  fixture.RunOnGuestThread([&](rex::test::Caller& c) {
    const auto thunk = rex::kernel::GetFunctionDispatcher(c.GetRuntime()).AllocateThunk(&TransferReply, 0);
    REQUIRE(thunk);
    auto& title = rex::xam::CurrentTitle();
    auto* caller = title.threads.GetCallingThread();
    REQUIRE(caller);
    const auto caller_handle = caller->GetHandle();
    std::array<rex::u64, 9> args{};
    rex::u64 result = 0x123456789ABCDEF0;
    CHECK_FALSE(caller->CallGuestFunction(0x7F100000, args, &result));
    CHECK(result == 0x123456789ABCDEF0);
    c.GetContext().r31.u64 = 0x123456789ABCDEF0;
    REQUIRE(caller->CallGuestFunction(thunk, args, &result));
    CHECK(result == 0xABCDEF1200000003ull);
    CHECK(c.GetContext().r31.u64 == 0x123456789ABCDEF0);
    std::atomic<rex::u64> worker_result{0};
    std::atomic<rex::u32> worker_handle{0};
    auto launched = title.threads.LaunchHostThread("storage callback regression", 256 * 1024, [&] {
      auto* worker = title.threads.GetCallingThread();
      if (worker) {
        worker_handle = worker->GetHandle();
        std::array<rex::u64, 9> worker_args{};
        rex::u64 reply = 0;
        if (worker->CallGuestFunction(thunk, worker_args, &reply)) worker_result = reply;
      }
      return 0;
    });
    REQUIRE(launched);
    const auto handle = *launched;
    rex::ScopeGuard close([&] { rex::kernel::CloseOwned(title.objects, handle); });
    rex::u64 timeout = static_cast<rex::u64>(-5ll * 10'000'000);
    REQUIRE(title.threads.GetObjectWaiter().WaitGuestObject(handle, 0, 0, 0, &timeout) == rex::kernel::X_STATUS_SUCCESS);
    CHECK(worker_handle.load() == handle);
    CHECK(worker_handle.load() != caller_handle);
    CHECK(worker_result.load() == 0xABCDEF1200000003ull);
  });
}
std::filesystem::path PackagePath(const std::filesystem::path& root) {
  return root / "0000000000000000" / "584108B7" / "000D0000" / "package" / "default.xex";
}
uint32_t Content(rex::test::Caller& c) {
  const auto address = c.Alloc(sizeof(rex::kernel::XCONTENT_AGGREGATE_DATA));
  auto* data = reinterpret_cast<rex::kernel::XCONTENT_AGGREGATE_DATA*>(c.GetBase() + address);
  *data = {};
  data->device_id = 1; data->title_id = kTitle;
  data->content_type = static_cast<rex::kernel::XContentType>(0xD0000);
  data->SetFileName("package");
  return address;
}
}

TEST_CASE("Launch preparation rejects unsupported callers and invalid selected-image headers before handoff", "[sdk][xam][launch-request]") {
  rex::test::KernelFixture fixture([](const auto& root) { REQUIRE(rex::platform::WriteHostFile(PackagePath(root), LaunchHeader(), true)); });
  callback_count = 0;
  fixture.RunOnGuestThread([&](rex::test::Caller& c) {
    const auto content = Content(c);
    auto& ui = rex::xam::CurrentXAM().GetSystemUI();
    auto* thread = rex::xam::CurrentTitle().threads.GetCallingThread();
    REQUIRE(thread);
    const auto current_type = c.Kernel("KeGetCurrentProcessType");
    CHECK(thread->GetTitleProcessType() == current_type);
    CHECK_FALSE(thread->CanCallGuestFunction(0x7F100000));
    std::array<rex::u64, 1> args{0};
    CHECK_FALSE(thread->CallGuestFunction(0x7F100000, args));
    c.XAM("XamLoaderRegisterLaunchRequestCallback", {0x7F100000});
    ui.launch_request_internal_flags.store(0x200);
    CHECK(c.XAM("XamContentLaunchImageInternalEx", {content, 0, 0}) == 50);
    CHECK(ui.launch_request_internal_flags.load() == 0);
    CHECK_FALSE(rex::xam::CurrentTitle().file_system.Stat("XSYSLAUNCH:\\default.xex"));
    const auto thunk = rex::kernel::GetFunctionDispatcher(c.GetRuntime()).AllocateThunk(&PrepareDisplay, 0);
    REQUIRE(thunk);
    c.XAM("XamLoaderRegisterLaunchRequestCallback", {thunk});
    c.Kernel("KeSetCurrentProcessType", {2});
    CHECK(thread->GetTitleProcessType() == 2);
    CHECK(c.Kernel("KeGetCurrentProcessType") == current_type);
    CHECK(c.XAM("XamContentLaunchImageInternalEx", {content, 0, 0}) == 50);
    c.Kernel("KeSetCurrentProcessType", {1});
    CHECK(thread->GetTitleProcessType() == 1);
    CHECK_FALSE(rex::xam::CurrentTitle().file_system.Stat("XSYSLAUNCH:\\default.xex"));
    const auto valid = LaunchHeader();
    for (const auto& invalid : {std::vector<uint8_t>{'X', 'E', 'X', '2'},
        [&] { auto b = valid; b[0] = 'B'; return b; }(),
        [&] { auto b = valid; b[20] = 0xFF; return b; }(),
        [&] { auto b = valid; b[27] = 7; return b; }(),
        [&] { auto b = valid; b[31] = 120; return b; }()}) {
      REQUIRE(rex::platform::WriteHostFile(PackagePath(fixture.GetRoot()), invalid, true));
      CHECK(c.XAM("XamContentLaunchImageInternalEx", {content, 0, 0}) == 193);
      CHECK(callback_count.load() == 0);
      CHECK_FALSE(rex::xam::CurrentTitle().file_system.Stat("XSYSLAUNCH:\\default.xex"));
      CHECK(rex::xam::CurrentXAM().GetLoaderState().launch_path.empty());
    }
    REQUIRE(rex::platform::WriteHostFile(PackagePath(fixture.GetRoot()), valid, true));
    unsigned transitions = 0;
    ui.launch_display_transition = [&] { ++transitions; return 1460u; };
    CHECK(c.XAM("XamContentLaunchImageInternalEx", {content, 0, 0}) == 1460);
    CHECK(transitions == 1); CHECK(callback_count.load() == 1);
    CHECK_FALSE(rex::xam::CurrentTitle().file_system.Stat("XSYSLAUNCH:\\default.xex"));
    CHECK(rex::xam::CurrentXAM().GetLoaderState().launch_path.empty());
    ui.launch_display_transition = {};
  });
}

TEST_CASE("Launch preparation executes the registered callback on the canonical guest thread", "[sdk][xam][launch-request]") {
  rex::test::KernelFixture fixture([](const auto& root) { REQUIRE(rex::platform::WriteHostFile(PackagePath(root), LaunchHeader(), true)); });
  callback_count = 0;
  bool returned = false;
  unsigned transitions = 0;
  rex::xam::CurrentXAM().GetSystemUI().launch_display_transition = [&] { ++transitions; return 0u; };
  fixture.RunUntilGuestThreadEnds([&](rex::test::Caller& c) {
    const auto content = Content(c);
    const auto thunk = rex::kernel::GetFunctionDispatcher(c.GetRuntime()).AllocateThunk(&PrepareDisplay, 0);
    REQUIRE(thunk);
    auto* thread = rex::xam::CurrentTitle().threads.GetCallingThread();
    REQUIRE(thread); CHECK(thread->CanCallGuestFunction(thunk));
    const auto request = c.Alloc(16);
    c.Store32(request, kTitle); c.Store32(request + 4, kVersion); c.Store32(request + 8, 1);
    std::array<rex::u64, 1> args{request};
    c.GetContext().r31.u64 = 0x123456789ABCDEF0;
    REQUIRE(thread->CallGuestFunction(thunk, args));
    CHECK(c.GetContext().r31.u64 == 0x123456789ABCDEF0);
    CHECK(c.Load32(request + 12) == 1);
    c.XAM("XamLoaderRegisterLaunchRequestCallback", {thunk});
    c.XAM("XamContentLaunchImageInternalEx", {content, 0, 0});
    returned = true;
  });
  CHECK_FALSE(returned); CHECK(callback_count.load() == 2); CHECK(transitions == 1);
  CHECK(rex::xam::CurrentXAM().GetSystemUI().launch_request_internal_flags.load() == 0x200);
  CHECK(rex::xam::CurrentXAM().GetLoaderState().launch_path == "XSYSLAUNCH:\\default.xex");
  CHECK(rex::xam::CurrentXAM().GetLoaderState().launch_flags == 0);
}
