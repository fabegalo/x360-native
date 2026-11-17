#include "kernel/kernel_fixture.h"
#include "kernel/test_access.h"
#include "../../titles/common/launch_handoff.h"
#include "../../titles/common/guest_fault_context.h"
#include <fstream>
#if defined(_WIN32)
#include <windows.h>
#endif
#include "../../titles/common/launch_request.h"
#include <rex/xam/title_lifecycle.h>
#include <rex/xam/app_manager.h>
#include <rex/xam/system_ui.h>
#include <rex/xam/loader_state.h>
#include <rex/xam/user_profile.h>
#include <rex/xam/offline_achievement_store.h>
#include "xam/xam_state.h"
#include "offline_achievement_fixture.h"
#include "temp_folder.h"
#include <rex/platform/file.h>
#include <rex/filesystem/file_system_error.h>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <array>
#include <rex/filesystem/file_system.h>
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/module.h>
#include <rex/kernel/function_dispatcher.h>
#include <rex/kernel/thread_state.h>
#include <rex/kernel/object/module_object.h>
#include <rex/kernel/object/object_table.h>
#include <rex/kernel/types/io.h>
#include <rex/kernel/types/status.h>

namespace {
void GuestRegisterOutputs(PPCContext& ctx, uint8_t* base) {
  // Exercise the actual 64-bit stack argument layout and several return banks.
  const auto ninth = rex::memory::load_and_swap<uint64_t>(base + ctx.r1.u32 + 0x50);
  ctx.r3.u64 += ninth;
  ctx.f1.f64 = 0.625;
  ctx.v1.u32[0] = 0x12345678;
  ctx.v1.u32[3] = 0xABCDEF01;
}
}
TEST_CASE("Canonical raw calls preserve original vector outputs and the caller frame",
          "[sdk][service-contracts][guest-register-outputs]") {
  rex::test::KernelFixture fixture;
  auto& runtime = fixture.GetRuntime();
  auto* dispatcher = &rex::kernel::GetFunctionDispatcher(runtime);
  const auto address = dispatcher->AllocateThunk(&GuestRegisterOutputs, 0);
  REQUIRE(address);
  fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
    auto* thread = rex::kernel::ThreadState::Get();
    auto& ctx = caller.GetContext();
    const auto saved = ctx;
    const std::array<uint64_t, 9> args{2, 3, 4, 5, 6, 7, 8, 9, 0x100000001ULL};
    auto scalar_args = args;
    const auto scalar = dispatcher->Execute(thread, address, scalar_args.data(), scalar_args.size());
    CHECK(scalar == 0x100000003ULL);
    CHECK(ctx.v1.u32[0] == saved.v1.u32[0]);
    REQUIRE(dispatcher->Run(thread, address, args));
    CHECK(ctx.r3.u64 == scalar);
    CHECK(ctx.v1.u32[0] == 0x12345678);
    CHECK(ctx.v1.u32[3] == 0xABCDEF01);
    CHECK(ctx.f1.f64 == 0.625);
    CHECK(ctx.r1.u64 == saved.r1.u64);
    CHECK(ctx.lr == saved.lr);
    CHECK(rex::kernel::ThreadState::Get() == thread);
    ctx = saved;
  });
}

TEST_CASE("Public original XAM operations share canonical guest dispatch and completion state",
          "[sdk][service-contracts][original-xam-operations]") {
  rex::test::KernelFixture fixture;
  fixture.RunOnGuestThread([&](rex::test::Caller& caller) {
    auto* services = REX_INTERFACE(rex::xam::IXAMTitleServices);
    REQUIRE(services);
    auto& ui = services->GetSystemUI();
    const auto previous_callback = ui.content_change_callback.load();
    rex::ScopeGuard restore([&] { rex::xam::XamContentRegisterChangeCallback_entry(previous_callback); });
    rex::xam::XamContentRegisterChangeCallback_entry(0x81234560);
    CHECK(ui.content_change_callback.load() == 0x81234560);
    caller.XAM("XamContentRegisterChangeCallback", {0});
    CHECK(ui.content_change_callback.load() == 0);
    // Unknown messages must preserve the canonical failure, never acknowledge missing services.
    CHECK(rex::xam::XMsgInProcessCall_entry(0xABCD, 0x1234, 0, 0) ==
          caller.XAM("XMsgInProcessCall", {0xABCD, 0x1234, 0, 0}));
    using rex::kernel::XOVERLAPPED;
    const auto address = caller.Alloc(sizeof(XOVERLAPPED));
    auto* io = reinterpret_cast<XOVERLAPPED*>(caller.GetBase() + address);
    const rex::memory::MappedPtr<XOVERLAPPED> mapped(io, address);
    CHECK(rex::xam::XMsgCancelIORequest_entry(nullptr, 0) == rex::kernel::X_ERROR_INVALID_PARAMETER);
    for (const auto result : {rex::kernel::X_ERROR_SUCCESS, rex::kernel::X_ERROR_IO_PENDING}) {
      rex::kernel::XOverlappedSetResult(io, result);
      const auto expected = result == rex::kernel::X_ERROR_IO_PENDING ? 50u : 0u;
      CHECK(rex::xam::XMsgCancelIORequest_entry(mapped, 0) == expected);
      CHECK(caller.XAM("XMsgCancelIORequest", {address, 0}) == expected);
      CHECK(rex::kernel::XOverlappedGetResult(io) == result);
    }
  });
}

TEST_CASE("Public XAM services expose the committed original achievement ledger",
          "[sdk][service-contracts][achievement-writes]") {
  rex::test::KernelFixture fixture;
  auto* services = REX_INTERFACE(rex::xam::IXAMTitleServices);
  REQUIRE(services);
  auto& store = services->GetOfflineAchievements();
  CHECK(&store == &rex::xam::CurrentXAM().GetOfflineAchievements());
  rex::u64 xuid = 0;
  REQUIRE(services->GetUserProfile().CreateLocal("Ledger fixture", &xuid) == 0);
  constexpr rex::u32 title = 0x12345678;
  using Result = rex::xam::OfflineAchievementResult;
  REQUIRE(store.ImportTitle(title, offline_achievement_fixture::Metadata()) == Result::kSuccess);
  std::vector<rex::xam::OfflineAchievementRecord> events;
  REQUIRE(store.UnlockBatch(xuid, title, std::array{1u}, events) == Result::kSuccess);
  REQUIRE(events.size() == 1);
  std::vector<rex::xam::OfflineAchievementRecord> records;
  REQUIRE(rex::xam::CurrentXAM().GetOfflineAchievements().List(xuid, title, records) == Result::kSuccess);
  REQUIRE(records.size() == 2);
  CHECK(records[0].info.id == events[0].info.id);
  CHECK(records[0].unlock_filetime == events[0].unlock_filetime);
  CHECK(records[0].unlock_filetime != 0);
  REQUIRE(store.UnlockBatch(xuid, title, std::array{1u}, events) == Result::kSuccess);
  CHECK(events.empty());
}

TEST_CASE("Kernel package names follow the shared counted symbolic-link resolver", "[sdk][service-contracts][package-mount]") {
  rex::test::KernelFixture fixture;
  auto& files = fixture.GetRuntime().GetFileSystem();
  REQUIRE(files.Link("PKG:", "CHAIN:\\base"));
  REQUIRE(files.Link("CHAIN:", "\\Device\\Harddisk0\\Partition1"));
  REQUIRE(files.Link("LOOP1:", "LOOP2:"));
  REQUIRE(files.Link("LOOP2:", "LOOP1:"));
  fixture.RunOnGuestThread([&](rex::test::Caller& c) {
    const auto attributes = c.AllocAttributes("\\??\\PKG:\\leaf");
    c.Store32(attributes, 0xFFFFFFFD);
    const auto descriptor = c.Alloc(sizeof(rex::kernel::X_ANSI_STRING)), output = c.Alloc(256);
    auto* target = reinterpret_cast<rex::kernel::X_ANSI_STRING*>(c.GetBase() + descriptor);
    target->pointer = output; target->maximum_length = 256; target->length = 0x1234;
    std::memset(c.GetBase() + output, 0xA5, 256);
    REQUIRE(c.Kernel("ObTranslateSymbolicLink", {attributes, descriptor}) == 0);
    const std::string expected = "\\Device\\Harddisk0\\Partition1\\base\\leaf";
    CHECK(target->length == expected.size());
    CHECK(std::memcmp(c.GetBase() + output, expected.data(), expected.size()) == 0);
    CHECK(c.Load8(output + static_cast<uint32_t>(expected.size())) == 0xA5);
    const auto shared = files.ResolveLinks("PKG:\\leaf");
    REQUIRE(shared); CHECK(*shared == expected);
    target->maximum_length = 4; target->length = 0x1234;
    std::memset(c.GetBase() + output, 0xA5, 256);
    CHECK(c.Kernel("ObTranslateSymbolicLink", {attributes, descriptor}) == rex::kernel::X_STATUS_BUFFER_TOO_SMALL);
    CHECK(target->length == 0x1234);
    CHECK(c.Load32(output) == 0xA5A5A5A5);
    target->maximum_length = 256;
    for (const auto* name : {"UNKNOWN:\\leaf", "PKG2:\\leaf"}) {
      const auto missing = c.AllocAttributes(name);
      CHECK(c.Kernel("ObTranslateSymbolicLink", {missing, descriptor}) == rex::kernel::X_STATUS_OBJECT_NAME_NOT_FOUND);
      CHECK(target->length == 0x1234); CHECK(c.Load32(output) == 0xA5A5A5A5);
    }
    const auto cycle = c.AllocAttributes("LOOP1:\\leaf");
    CHECK(c.Kernel("ObTranslateSymbolicLink", {cycle, descriptor}) == rex::kernel::X_STATUS_OBJECT_NAME_INVALID);
    CHECK_FALSE(files.ResolveLinks("LOOP1:\\leaf"));
    CHECK_FALSE(files.Stat("LOOP1:\\leaf"));
    CHECK(target->length == 0x1234); CHECK(c.Load32(output) == 0xA5A5A5A5);
    CHECK(c.Kernel("ObTranslateSymbolicLink", {0xFFFFFFF0, descriptor}) == rex::kernel::X_STATUS_INVALID_PARAMETER);
    target->pointer = 0xFFFFFFF0;
    CHECK(c.Kernel("ObTranslateSymbolicLink", {attributes, descriptor}) == rex::kernel::X_STATUS_INVALID_PARAMETER);
    CHECK(target->length == 0x1234);
  });
}

TEST_CASE("Official package MD5 hashes concatenated inputs and bounds the digest output", "[sdk][service-contracts][package-mount]") {
  rex::test::KernelFixture fixture;
  fixture.RunOnGuestThread([](rex::test::Caller& c) {
    const auto a = c.AllocString("a"), b = c.AllocString("b"), third = c.AllocString("c");
    const auto output = c.Alloc(24);
    const std::array<uint8_t, 16> expected{0x90,0x01,0x50,0x98,0x3C,0xD2,0x4F,0xB0,0xD6,0x96,0x3F,0x7D,0x28,0xE1,0x7F,0x72};
    for (const auto size : {7u,16u,24u}) {
      std::memset(c.GetBase() + output, 0xA5, 24);
      c.Kernel("XeCryptMd5", {a,1,b,1,third,1,output,size});
      const auto written = std::min<size_t>(size, expected.size());
      CHECK(std::equal(expected.begin(), expected.begin() + written, c.GetBase() + output));
      for (auto i = written; i < 24; ++i) CHECK(c.Load8(output + static_cast<uint32_t>(i)) == 0xA5);
    }
  });
}

TEST_CASE("Absent NUI status respects the retail caller buffer and readiness Boolean",
          "[sdk][service-contracts]") {
  rex::test::KernelFixture fixture;
  fixture.RunOnGuestThread([&](rex::test::Caller &caller) {
    const auto address = caller.Alloc(32);
    std::memset(caller.GetBase() + address, 0xCD, 32);
    caller.XAM("XamNuiGetDeviceStatus", {address});
    for (uint32_t i = 0; i < 32; ++i)
      CHECK(caller.Load8(address + i) == (i < 16 ? 0 : 0xCD));
    caller.GetContext().r3.u64 = 0xDEADBEEF;
    CHECK(caller.XAM("XamNuiIsDeviceReady") == 0);
  });
}

TEST_CASE("Resource locators decode big endian guest names and bound output",
          "[sdk][service-contracts]") {
  rex::test::KernelFixture fixture;
  fixture.RunOnGuestThread([&](rex::test::Caller &caller) {
    const auto wide = [&](std::u16string_view value) {
      const auto address = caller.Alloc(static_cast<uint32_t>((value.size() + 1) * 2));
      for (size_t i = 0; i < value.size(); ++i) {
        caller.GetBase()[address + 2 * i] = static_cast<uint8_t>(value[i] >> 8);
        caller.GetBase()[address + 2 * i + 1] = static_cast<uint8_t>(value[i]);
      }
      return address;
    };
    const auto container = wide(u"menus");
    const auto resource = wide(u"\u00E1vatar/\u65E5");
    const auto output = caller.Alloc(256);
    const auto check = [&](std::u16string_view expected, uint32_t count) {
      const size_t length = std::min(expected.size(), static_cast<size_t>(count - 1));
      for (size_t i = 0; i <= length; ++i) {
        const auto value = static_cast<char16_t>((caller.Load8(output + static_cast<uint32_t>(2 * i)) << 8) |
            caller.Load8(output + static_cast<uint32_t>(2 * i + 1)));
        CHECK(value == (i == length ? u'\0' : expected[i]));
      }
      CHECK(caller.Load8(output + count * 2) == 0xCD);
    };
    for (uint32_t count : {8u, 120u}) {
      std::memset(caller.GetBase() + output, 0xCD, 256);
      CHECK(caller.XAM("XamBuildResourceLocator", {0, container, resource, output, count}) == 0);
      check(u"file://media:/menus.xzp#\u00E1vatar/\u65E5", count);
    }
    for (const auto& [name, prefix] : {
        std::pair{"XamBuildGamercardResourceLocator", u"file://media:/gamercrd.xzp#"},
        std::pair{"XamBuildSharedSystemResourceLocator", u"file://media:/shrdres.xzp#"},
        std::pair{"XamBuildXamResourceLocator", u"file://media:/xam.xzp#"}}) {
      std::memset(caller.GetBase() + output, 0xCD, 256);
      CHECK(caller.XAM(name, {resource, output, 120}) == 0);
      check(std::u16string(prefix) + u"\u00E1vatar/\u65E5", 120);
    }
    std::memset(caller.GetBase() + output, 0xCD, 256);
    CHECK(caller.XAM("XamBuildResourceLocator", {0x12340000, 0, 0, output, 120}) == 0);
    check(u"section://12340000,#", 120);
  });
}

TEST_CASE("Unavailable camera creation leaves caller storage untouched",
          "[sdk][service-contracts]") {
  rex::test::KernelFixture fixture;
  fixture.RunOnGuestThread([&](rex::test::Caller &caller) {
    const auto address = caller.Alloc(64);
    std::memset(caller.GetBase() + address, 0xCD, 64);
    CHECK(caller.Kernel("XUsbcamCreate", {address, 64, address + 32}) == 0xC0000002u);
    CHECK(caller.Kernel("XUsbcamCreate", {0, 0, 0}) == 0xC0000002u);
    CHECK(caller.Kernel("XUsbcamGetState", {}) == 0);
    for (uint32_t i = 0; i < 64; ++i)
      CHECK(caller.Load8(address + i) == 0xCD);
  });
}

TEST_CASE("Unavailable XAM pointer and device exports preserve caller storage",
          "[sdk][service-contracts]") {
  rex::test::KernelFixture fixture;
  fixture.RunOnGuestThread([&](rex::test::Caller &caller) {
    const auto address = caller.Alloc(64);
    std::memset(caller.GetBase() + address, 0xCD, 64);
    for (uint32_t id : {8u, 11u, 272u, 275u, 0xFFFFFFFFu})
      CHECK(caller.XAM("XamLookupCommonStringByIndex", {id}) == 0);
    CHECK(caller.XAM("XCustomGetBannerImage", {address}) == 0x80004001u);
    CHECK(caller.XAM("XCustomGetBannerImage", {0}) == 0x80070057u);
    for (uint32_t user : {0u, 1u, 3u, 4u, 0xFFFFFFFFu}) {
      CHECK(caller.XAM("XamInputGetDeviceStats", {user, address}) ==
            (user < 4 ? 50u : 87u));
      CHECK(caller.XAM("XamInputGetDeviceStats", {user, 0}) == 87u);
    }
    for (uint32_t i = 0; i < 64; ++i)
      CHECK(caller.Load8(address + i) == 0xCD);
  });
}

TEST_CASE("RtlCompareMemoryUlong returns matching prefix bytes",
          "[sdk][service-contracts]") {
  rex::test::KernelFixture fixture;
  fixture.RunOnGuestThread([&](rex::test::Caller &caller) {
    const auto address = caller.Alloc(16);
    for (uint32_t i = 0; i < 4; ++i)
      caller.Store32(address + 4 * i, 0x12345678);
    CHECK(caller.Kernel("RtlCompareMemoryUlong", {address, 16, 0x12345678}) ==
          16);
    for (uint32_t i = 0; i < 4; ++i) {
      caller.Store32(address + 4 * i, 1);
      CHECK(caller.Kernel("RtlCompareMemoryUlong", {address, 16, 0x12345678}) ==
            i * 4);
      caller.Store32(address + 4 * i, 0x12345678);
    }
    CHECK(caller.Kernel("RtlCompareMemoryUlong",
                        {address + 1, 4, 0x12345678}) == 0);
    CHECK(caller.Kernel("RtlCompareMemoryUlong", {address, 5, 0x12345678}) ==
          0);
    CHECK(caller.Kernel("RtlCompareMemoryUlong", {address, 0, 0x12345678}) ==
          0);
  });
}

TEST_CASE("Guest host resolution uses canonical mounts links case and access", "[sdk][service-contracts][host-path-resolution]") {
  rex::test::KernelFixture fixture;
  auto& files = fixture.GetRuntime().GetFileSystem();
  rex::test::TempFolder originals("rex_host_resolution_");
  const auto original = originals.path / "Original.xex";
  REQUIRE(rex::platform::WriteHostFile(original, std::span<const rex::u8>{}));
  using namespace rex::filesystem;
  bool connected = true;
  MountEntry mount{"resolve:", ProviderType::kHostDirectory, originals.path, MountFlags::kNone, {}};
  mount.is_accessible = [&] { return connected; };
  REQUIRE(files.Mount(mount));
  REQUIRE(files.Link("first:", "resolve:"));
  REQUIRE(files.Link("second:", "first:"));
  const auto result = files.ResolveHostPath("SECOND:\\original.XEX");
  REQUIRE(result);
  CHECK(std::filesystem::equivalent(*result, original));
  CHECK_FALSE(files.ResolveHostPath("second:\\absent.xex"));
  CHECK_FALSE(files.ResolveHostPath("unknown:\\Original.xex"));
  REQUIRE(files.Link("cycle1:", "cycle2:"));
  REQUIRE(files.Link("cycle2:", "cycle1:"));
  CHECK_FALSE(files.ResolveHostPath("cycle1:\\Original.xex"));
  connected = false;
  auto disconnected = files.ResolveHostPath("second:\\Original.xex");
  REQUIRE_FALSE(disconnected);
  CHECK(CodeOf(disconnected.error()) == FileSystemError::kAccessDenied);
  connected = true;
  REQUIRE(files.Mount({"synthetic:", ProviderType::kNull, {}, MountFlags::kNone, {"\\placeholder"}}));
  REQUIRE(files.Stat("synthetic:\\placeholder"));
  auto synthetic = files.ResolveHostPath("synthetic:\\placeholder");
  REQUIRE_FALSE(synthetic);
  CHECK(CodeOf(synthetic.error()) == FileSystemError::kUnsupported);
  REQUIRE(files.Unmount("resolve:"));
  CHECK_FALSE(files.ResolveHostPath("second:\\Original.xex"));
  CHECK(std::filesystem::file_size(original) == 0);
}

TEST_CASE("Session handoff preserves launch data exit reasons and first-event ownership", "[sdk][service-contracts][title-handoff]") {
  rex::test::KernelFixture fixture;
  rex::test::TempFolder files("rex_handoff_");
  auto& registry = rex::GetSettingsRegistry();
  auto& settings = x360n::GetLaunchSettings();
  REQUIRE(x360n::RegisterLaunchSettings(registry));
  rex::ScopeGuard reset([&] {
    (void)settings.handoff_in.SetText(rex::SettingLayer::kRuntime, "");
    (void)settings.handoff_out.SetText(rex::SettingLayer::kRuntime, "");
    registry.Unregister(settings);
  });
  CHECK(registry.IsAlias("x360n_handoff_in"));
  const auto input = files.path / "input.bin", output = files.path / "output.bin";
  x360n::LaunchRequest incoming;
  incoming.kind = x360n::LaunchRequest::Dashboard;
  incoming.flags = 0x10203040;
  incoming.data_present = true;
  incoming.data = {0,1,0xFE,0xFF};
  REQUIRE(x360n::WriteLaunchRequest(input, incoming));
  REQUIRE(registry.SetText(rex::SettingLayer::kRuntime, "x360n_handoff_in", input.string()));
  CHECK(settings.handoff_in.Get() == input.string());
  REQUIRE(settings.handoff_out.SetText(rex::SettingLayer::kRuntime, output.string()));
  x360n::ImportLaunchData(fixture.GetRuntime());
  CHECK(x360n::IsDashboardReturn());
  auto* services = REX_INTERFACE(rex::xam::IXAMTitleServices); REQUIRE(services);
  auto& state = services->GetLoaderState();
  CHECK(state.launch_flags == incoming.flags);
  CHECK(state.launch_data_present);
  CHECK(state.launch_data == incoming.data);
  state.exit_request = rex::xam::XAMLoaderState::ExitRequest::Terminate;
  SECTION("guest termination") {
    x360n::ExportLaunchRequest(fixture.GetRuntime());
    x360n::ExportWindowClose();
    x360n::LaunchRequest result; REQUIRE(x360n::ReadLaunchRequest(output, result));
    CHECK(result.kind == x360n::LaunchRequest::TitleTerminate);
    CHECK(result.flags == incoming.flags); CHECK(result.data_present); CHECK(result.data == incoming.data);
  }
  SECTION("runtime exit callback exports before unload") {
    bool exported_while_loaded = false;
    auto& runtime = fixture.GetRuntime();
    auto exit = runtime.OnTitleExit([&](rex::kernel::TitleExitReason) {
      exported_while_loaded = runtime.IsLoaded();
      x360n::ExportLaunchRequest(runtime);
    });
    runtime.GetTitleExit().ExitTitle(rex::kernel::TitleExitReason::kNormalExit);
    CHECK(exported_while_loaded);
    runtime.Unload();
    CHECK_FALSE(runtime.IsLoaded());
    x360n::LaunchRequest result; REQUIRE(x360n::ReadLaunchRequest(output, result));
    CHECK(result.kind == x360n::LaunchRequest::TitleTerminate);
    CHECK(result.flags == incoming.flags); CHECK(result.data == incoming.data);
  }
  SECTION("host window closes first") {
    x360n::ExportWindowClose();
    x360n::ExportLaunchRequest(fixture.GetRuntime());
    x360n::LaunchRequest result; REQUIRE(x360n::ReadLaunchRequest(output, result));
    CHECK(result.kind == x360n::LaunchRequest::WindowClose);
  }
}

#if defined(_WIN32)
TEST_CASE("Guest fault observer preserves PPC state and bounds resumed diagnostics", "[sdk][service-contracts][guest-fault-observer]") {
  rex::test::KernelFixture fixture;
  rex::test::TempFolder files("rex_fault_observer_");
  const auto path = files.path / "fault.txt";
  x360n::GuestFaultContext observer;
  CHECK_FALSE(observer.Start(fixture.GetRuntime(), files.path / "missing" / "fault.txt"));
  REQUIRE(observer.Start(fixture.GetRuntime(), path));
  CHECK_FALSE(observer.Start(fixture.GetRuntime(), files.path / "other.txt"));
  unsigned handled = 0;
  const auto recover = +[](rex::platform::Exception* exception, void* context) {
    if (exception->GetCode() != rex::platform::Exception::Code::kAccessViolation) return false;
    ++*static_cast<unsigned*>(context);
    return true;
  };
  rex::platform::ExceptionHandler::Install(recover, &handled);
  rex::ScopeGuard uninstall([&] { rex::platform::ExceptionHandler::Uninstall(recover, &handled); });
  fixture.RunOnGuestThread([&](rex::test::Caller& c) {
    auto& ctx = c.GetContext();
    ctx.r0.u64 = 0x0011223344556677ull;
    ctx.r3.u64 = 0x123456789ABCDEF0ull;
    ctx.lr = 0x90105000;
    ctx.SetCR(0xA0000005);
    const ULONG_PTR arguments[]{0, reinterpret_cast<ULONG_PTR>(c.GetBase()) + 5};
    for (unsigned i = 0; i < 20; ++i) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 2, arguments);
    CHECK(ctx.r0.u64 == 0x0011223344556677ull);
    CHECK(ctx.r3.u64 == 0x123456789ABCDEF0ull);
    CHECK(ctx.lr == 0x90105000); CHECK(ctx.GetCR() == 0xA0000005);
  });
  observer.Stop(); observer.Stop();
  CHECK(handled == 20);
  std::ifstream input(path, std::ios::binary);
  const std::string text{std::istreambuf_iterator<char>(input), {}};
  size_t count = 0, offset = 0;
  while ((offset = text.find("runtime-declined guest access", offset)) != std::string::npos) {
    ++count; ++offset;
  }
  CHECK(count == 16);
  CHECK(text.find("r00=0011223344556677") != std::string::npos);
  CHECK(text.find("operation=read guest_address=00000005") != std::string::npos);
  CHECK(text.find("cr=A0000005") != std::string::npos);
  CHECK_FALSE(observer.Start(fixture.GetRuntime(), path));
  CHECK(std::filesystem::file_size(path) == text.size());
}
#endif
