#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#endif
#include <rex/cvar.h>
#include <rex/kernel/init.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/xam/content_manager.h>
#include <rex/kernel/xam/module.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xthread.h>
#include <rex/system/thread_state.h>
#include <rex/system/xtypes.h>

#define STORAGE_IMPORT(name) extern "C" void __imp__##name(PPCContext&, uint8_t*)
STORAGE_IMPORT(XamContentCreateDeviceEnumerator);
STORAGE_IMPORT(XamContentGetDeviceName);
STORAGE_IMPORT(XamContentGetDeviceData);
STORAGE_IMPORT(XamContentGetLocalizedDeviceData);
STORAGE_IMPORT(XamContentGetDeviceState);
STORAGE_IMPORT(XamEnumerate);
STORAGE_IMPORT(NtClose);
STORAGE_IMPORT(XamShowDeviceSelectorUI);
STORAGE_IMPORT(XMsgCompleteIORequest);
STORAGE_IMPORT(XGetOverlappedResult);
STORAGE_IMPORT(XGetOverlappedExtendedError);
STORAGE_IMPORT(XamContentGetDefaultDevice);
STORAGE_IMPORT(XamNetworkStorageIsEnabledInLiveHive);
STORAGE_IMPORT(XamNetworkStorageHasUserEnabledStorage);
STORAGE_IMPORT(XamNetworkStorageIsSupportedContentType);
STORAGE_IMPORT(XamNetworkStorageIsUserInSync);
STORAGE_IMPORT(XamNetworkStorageUserHasPrivilege);
STORAGE_IMPORT(XamNetworkStorageShouldHideFromTitle);
STORAGE_IMPORT(XamNetworkStorageDeviceHasCacheFile);
using namespace rex;

namespace {
struct StorageRoot {
  std::filesystem::path path = std::filesystem::current_path() /
      ("storage-contract-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  StorageRoot() { std::filesystem::create_directories(path); }
  ~StorageRoot() { std::error_code ec; std::filesystem::remove_all(path, ec); }
  void config(std::string_view text) { std::ofstream(path / "storage.toml") << text; }
};
struct DeviceGuest {
  rex::Runtime runtime;
  uint32_t address;
  uint8_t* bytes;
  explicit DeviceGuest(const std::filesystem::path& root) : runtime(root, root) {
    rex::RuntimeConfig config{};
    config.tool_mode = true;
    config.kernel_init = rex::kernel::InitializeKernel;
    REQUIRE(runtime.Setup(std::move(config)) == 0);
    address = runtime.memory()->SystemHeapAlloc(8192);
    REQUIRE(address);
    bytes = runtime.memory()->virtual_membase();
    std::memset(bytes + address, 0, 8192);
  }
  uint32_t call(void (*fn)(PPCContext&, uint8_t*), std::initializer_list<uint64_t> args) {
    uint32_t result = ~0u;
    auto thread = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
        runtime.kernel_state(), 128 * 1024, 0, [&] {
      auto& ctx = *rex::system::XThread::GetCurrentThread()->thread_state()->context();
      PPCRegister* registers[] = {&ctx.r3, &ctx.r4, &ctx.r5, &ctx.r6, &ctx.r7, &ctx.r8, &ctx.r9, &ctx.r10};
      size_t i = 0;
      for (auto value : args) registers[i++]->u64 = value;
      fn(ctx, bytes); result = ctx.r3.u32; return 0;
    }));
    REQUIRE(thread->Create() == 0);
    uint64_t timeout = uint64_t(-50'000'000ll);
    REQUIRE(thread->Wait(0, 0, 0, &timeout) == 0);
    return result;
  }
  uint32_t word(size_t offset) { return rex::memory::load_and_swap<uint32_t>(bytes + address + offset); }
  uint64_t wide(size_t offset) { return rex::memory::load_and_swap<uint64_t>(bytes + address + offset); }
};
}

TEST_CASE("Storage devices report real backing space and omit absent optical media", "[sdk][storage-device]") {
  StorageRoot root;
  DeviceGuest guest(root.path);
  CHECK(guest.call(__imp__XamContentGetDeviceData, {1, guest.address}) == 0);
  CHECK(guest.word(0) == 1);
  CHECK(guest.word(4) == 1);
  REQUIRE(guest.wide(8) == std::filesystem::space(root.path).capacity);
  CHECK(guest.wide(16) > 0);
  CHECK(guest.wide(16) <= std::filesystem::space(root.path).capacity);
  CHECK(guest.call(__imp__XamContentGetDeviceState, {2, 0}) == X_ERROR_DEVICE_NOT_CONNECTED);
  CHECK(guest.call(__imp__XamContentGetDeviceData, {2, guest.address}) == X_ERROR_DEVICE_NOT_CONNECTED);
  CHECK(guest.call(__imp__XamContentCreateDeviceEnumerator,
      {1, 0, 4, guest.address + 256, guest.address + 260}) == 0);
  CHECK(guest.word(256) == 4 * 0x50);
  const auto handle = guest.word(260);
  CHECK(guest.call(__imp__XamEnumerate, {handle, 0, guest.address + 512, 4 * 0x50, guest.address + 264, 0}) == 0);
  CHECK(guest.word(264) == 1);
  CHECK(guest.word(512) == 1);
  CHECK(guest.call(__imp__NtClose, {handle}) == 0);
  CHECK(guest.call(__imp__XamContentGetDeviceName, {1, 0, 32}) == X_ERROR_INVALID_PARAMETER);
  CHECK(guest.call(__imp__XamContentGetDeviceName, {1, guest.address, 1}) == X_ERROR_INSUFFICIENT_BUFFER);
  CHECK(guest.call(__imp__XamContentGetDeviceData, {1, 0}) == X_ERROR_INVALID_PARAMETER);
  CHECK(guest.call(__imp__XamContentCreateDeviceEnumerator, {1, 0, 0, 0, guest.address}) == X_ERROR_INVALID_PARAMETER);
  CHECK(guest.call(__imp__XamContentCreateDeviceEnumerator, {1, 0, 0x40000000, 0, guest.address}) == X_ERROR_INVALID_PARAMETER);
}

TEST_CASE("Storage volumes retain IDs and isolate capacity across restart and removal", "[sdk][storage-device]") {
  StorageRoot root;
  root.config("version=1\n[hdd]\ncapacity_bytes=8388608\nreserve_bytes=4096\n"
      "[[device]]\nid=7\ntype='mu'\nname='My MU'\ncapacity_bytes=4194304\n"
      "[[device]]\nid=12\ntype='usb'\ncapacity_bytes=2097152\n");
  const auto mu = root.path / "storage/00000007";
  std::filesystem::create_directories(mu);
  std::ofstream(mu / "payload") << std::string(1024, 'x');
  for (int restart = 0; restart != 2; ++restart) {
    DeviceGuest guest(root.path);
    REQUIRE(guest.call(__imp__XamContentGetDeviceData, {7, guest.address}) == 0);
    CHECK(guest.word(0) == 7);
    CHECK(guest.word(4) == 2);
    CHECK(guest.wide(8) == 4194304);
    CHECK(guest.wide(16) == 4194304 - 1024);
    CHECK(guest.call(__imp__XamContentGetLocalizedDeviceData, {7, guest.address + 128}) == 0);
    CHECK(std::memcmp(guest.bytes + guest.address, guest.bytes + guest.address + 128, 0x50) == 0);
    CHECK(guest.call(__imp__XamContentGetDeviceData, {1, guest.address}) == 0);
    CHECK(guest.wide(8) == 8388608);
    CHECK(guest.wide(16) < 8388608);
    CHECK(guest.wide(16) <= 8388608 - 4096);
    CHECK(guest.wide(16) > 8388608 - 4096 - 1024); // MU bytes are excluded from HDD accounting.
    CHECK(guest.call(__imp__XamContentGetDeviceState, {12, 0}) == X_ERROR_DEVICE_NOT_CONNECTED);
  }
  // Simulate unplugging a configured device without deleting its payload.
  std::filesystem::rename(mu, root.path / "detached-mu");
  DeviceGuest guest(root.path);
  CHECK(guest.call(__imp__XamContentGetDeviceState, {7, 0}) == X_ERROR_DEVICE_NOT_CONNECTED);
  CHECK_FALSE(std::filesystem::exists(mu));
  std::memset(guest.bytes + guest.address + 256, 0, 0x20);
  CHECK(guest.call(__imp__XamContentGetDeviceState, {7, guest.address + 256}) == X_ERROR_IO_PENDING);
  CHECK(guest.word(256) == X_ERROR_FUNCTION_FAILED);
  CHECK(guest.word(256 + 24) == X_ERROR_DEVICE_NOT_CONNECTED);
}

TEST_CASE("Storage rejects corrupt configuration without silently inventing devices", "[sdk][storage-device]") {
  StorageRoot root;
  for (const auto text : {"version=2\n", "version=1\n[[device]]\nid=1\ntype='usb'\n",
                         "version=1\n[hdd]\ncapacity_bytes=-1\n", "invalid = ["}) {
    root.config(text);
    DeviceGuest guest(root.path);
    CHECK(guest.call(__imp__XamContentGetDeviceState, {1, 0}) == X_ERROR_DEVICE_NOT_CONNECTED);
  }
}

TEST_CASE("Storage selector preserves original request and defers selection to the system app", "[sdk][storage-selector]") {
  StorageRoot root;
  DeviceGuest guest(root.path);
  const auto xam = guest.runtime.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  unsigned calls = 0;
  xam->system_ui_request = [&](uint32_t message, uint32_t request, uint32_t size) {
    ++calls;
    CHECK(message == 0x2100B);
    REQUIRE(size == 40);
    const auto* words = guest.runtime.memory()->TranslateVirtual<rex::be<uint32_t>*>(request);
    CHECK(uint32_t(words[0]) == 0);
    CHECK(uint32_t(words[1]) == 0xFF);
    CHECK(uint32_t(words[2]) == 1);
    CHECK(uint32_t(words[3]) == 0x10200);
    CHECK(rex::memory::load_and_swap<uint64_t>(words + 4) == 0x100000001ull);
    CHECK(uint32_t(words[6]) == guest.address);
    CHECK(uint32_t(words[7]) == guest.address + 256);
    CHECK(uint32_t(words[8]) == 0);
    CHECK(uint32_t(words[9]) == 0);
    return X_E_SUCCESS;
  };
  rex::memory::store_and_swap<uint32_t>(guest.bytes + guest.address, 0xDEADBEEF);
  REQUIRE(guest.call(__imp__XamShowDeviceSelectorUI,
      {0xFF, 1, 0x10000, 0x100000001ull, guest.address, guest.address + 256}) == X_ERROR_IO_PENDING);
  CHECK(calls == 1);
  CHECK(guest.word(0) == 0xDEADBEEF); // No automatic HDD selection.
  CHECK(guest.call(__imp__XGetOverlappedResult, {guest.address + 256, 0, 0}) == X_ERROR_IO_INCOMPLETE);
  // The original app reports cancellation through the extended HRESULT.
  REQUIRE(guest.call(__imp__XMsgCompleteIORequest,
      {guest.address + 256, X_ERROR_FUNCTION_FAILED, 0x800704C7, 0}) == 0);
  CHECK(guest.call(__imp__XGetOverlappedResult, {guest.address + 256, 0, 0}) == X_ERROR_FUNCTION_FAILED);
  CHECK(guest.call(__imp__XGetOverlappedExtendedError, {guest.address + 256}) == 0x800704C7);
  CHECK(guest.word(0) == 0xDEADBEEF);
  CHECK(guest.call(__imp__XamShowDeviceSelectorUI, {0, 1, 0, 0, guest.address, 0}) == X_ERROR_INVALID_PARAMETER);
  CHECK(guest.call(__imp__XamShowDeviceSelectorUI, {4, 1, 0, 0, guest.address, guest.address + 256}) == X_ERROR_INVALID_PARAMETER);
  CHECK(calls == 1);
  xam->system_ui_request = {};
  CHECK(guest.call(__imp__XamShowDeviceSelectorUI, {0, 1, 0, 0, guest.address, guest.address + 256}) == X_ERROR_NOT_FOUND);
  CHECK(guest.word(0) == 0xDEADBEEF);
  CHECK(guest.call(__imp__XamContentGetDefaultDevice, {guest.address + 512}) == 0);
  CHECK(guest.word(512) == 1); // Default query is separate from a user choice.
}

TEST_CASE("Storage selector cannot infer cloud support from stale PPC arguments", "[sdk][storage-selector]") {
  StorageRoot root;
  DeviceGuest guest(root.path);
  for (const auto argument : {0u, 1u, 3u, 0xFFu, 0xFFFE07D1u}) {
    for (const auto function : {__imp__XamNetworkStorageIsEnabledInLiveHive,
        __imp__XamNetworkStorageHasUserEnabledStorage, __imp__XamNetworkStorageIsSupportedContentType,
        __imp__XamNetworkStorageIsUserInSync, __imp__XamNetworkStorageUserHasPrivilege})
      CHECK(guest.call(function, {argument}) == 0);
    CHECK(guest.call(__imp__XamNetworkStorageShouldHideFromTitle, {argument}) == 1);
  }
}

TEST_CASE("Offline devices do not advertise a nonexistent network storage cache", "[sdk][storage-offline-cache]") {
  StorageRoot root;
  DeviceGuest guest(root.path);
  // Original dashboard 922FA610 branches on this BOOL. Leaving the input ID
  // in r3 creates its synthetic 512 MiB cache item (922F9A60), even on an
  // empty local HDD. There is no network-storage cache in this backend.
  for (const auto device : {0u, 1u, 2u, 7u, 0xFFFFFFFFu}) {
    CHECK(guest.call(__imp__XamNetworkStorageDeviceHasCacheFile, {device}) == 0);
  }
}

STORAGE_IMPORT(XamContentCopyInternal);
STORAGE_IMPORT(XamContentMoveInternal);
STORAGE_IMPORT(XMsgCancelIORequest);
STORAGE_IMPORT(XMsgSystemProcessCall);
STORAGE_IMPORT(XMsgStartIORequest);
namespace {
// Stand-in for the dashboard's CopyProgressRoutine-style callback (922FDB00):
// records each call and cancels on the call selected by the test.
struct ProgressLog {
  std::vector<uint64_t> transferred, totals;
  std::vector<uint32_t> reasons, contexts;
  int cancel_at = -1;
} progress_log;
void ProgressCallback(PPCContext& ctx, uint8_t* base) {
  progress_log.totals.push_back(ctx.r3.u64);
  progress_log.transferred.push_back(ctx.r4.u64);
  progress_log.reasons.push_back(ctx.r8.u32);
  // Ninth argument: 84(r1), as the original callback reads it.
  progress_log.contexts.push_back(rex::memory::load_and_swap<uint32_t>(base + ctx.r1.u32 + 0x54));
  ctx.r3.u64 = int(progress_log.reasons.size()) - 1 == progress_log.cancel_at ? 1 : 0;
}
constexpr uint64_t kOwner = 0xE000000012345678ull;
constexpr uint32_t kTitle = 0x1234ABCD;
// Writes one saved-game package on `device` and the dashboard's 512-byte item.
rex::system::xam::XCONTENT_AGGREGATE_DATA MakeItem(DeviceGuest& guest, uint32_t device,
                                                    const std::filesystem::path& root, size_t size) {
  rex::system::xam::XCONTENT_AGGREGATE_DATA data{};
  data.device_id = device;
  data.content_type = rex::system::XContentType::kSavedGame;
  data.set_display_name(u"Transfer save");
  data.set_file_name("SAVE0001");
  data.xuid = kOwner;
  data.title_id = kTitle;
  const auto package = root / "E000000012345678" / "1234ABCD" / "00000001" / "SAVE0001";
  std::filesystem::create_directories(package / "dir");
  std::ofstream(package / "dir" / "slot.dat", std::ios::binary) << std::string(size, 's');
  std::ofstream(package / "small.bin", std::ios::binary) << "abc";
  REQUIRE(guest.runtime.kernel_state()->content_manager()->WriteContentHeaderFile(kOwner, data) == 0);
  std::memset(guest.bytes + guest.address + 1024, 0, 512);
  std::memcpy(guest.bytes + guest.address + 1024, &data, sizeof(data));
  rex::memory::store_and_swap<uint64_t>(guest.bytes + guest.address + 1024 + 328, size + 3);
  return data;
}
std::filesystem::path PackageOn(const std::filesystem::path& root) {
  return root / "E000000012345678" / "1234ABCD" / "00000001" / "SAVE0001";
}
std::filesystem::path HeaderOn(const std::filesystem::path& root) {
  return root / "E000000012345678" / "1234ABCD" / "Headers" / "00000001" / "SAVE0001.header";
}
}  // namespace

namespace {
uint32_t cancel_io = 0;
std::atomic<uint32_t> cancel_calls{0};
void CancelTransferCallback(PPCContext& ctx, uint8_t* base) {
  // Cancel on the worker itself without waiting. QUIET must suppress guest
  // callbacks, never the host's cancellation checks.
  ctx.r3.u32 = cancel_io;
  ctx.r4.u32 = 0;
  __imp__XMsgCancelIORequest(ctx, base);
  cancel_calls.fetch_add(1);
  ctx.r3.u64 = 3;
}
}

TEST_CASE("XMsg cancellation stops storage I/O without an event and preserves the source",
          "[sdk][storage-transfer][storage-cancel]") {
  StorageRoot root;
  root.config("version=1\n[[device]]\nid=7\ntype='mu'\ncapacity_bytes=4194304\n");
  const auto mu = root.path / "storage/00000007";
  std::filesystem::create_directories(mu);
  DeviceGuest guest(root.path);
  auto* dispatcher = guest.runtime.function_dispatcher();
  REQUIRE(dispatcher->InitializeFunctionTable(0x82000000, 0x1000, 0x82000000, 0x10000, true, true));
  REQUIRE(dispatcher->SetFunction(0x82000100, &CancelTransferCallback));
  const auto device = guest.address + 2048;
  cancel_io = guest.address + 2304;
  rex::memory::store_and_swap<uint32_t>(guest.bytes + device, 7);
  for (auto operation : {__imp__XamContentCopyInternal, __imp__XamContentMoveInternal}) {
    MakeItem(guest, 1, root.path, 300000);
    std::memset(guest.bytes + cancel_io, 0, 28);
    cancel_calls = 0;
    REQUIRE(guest.call(operation, {0, device, guest.address + 1024, 0x82000100, 0, 0, cancel_io}) == 997);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (guest.word(cancel_io - guest.address) == 997 && std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(guest.word(cancel_io - guest.address) != 997);
    CHECK(cancel_calls.load() == 1);
    CHECK(guest.word(cancel_io - guest.address) == 1223);
    CHECK(guest.word(cancel_io - guest.address + 24) == 0x800704C7);
    CHECK(std::filesystem::file_size(PackageOn(root.path) / "dir/slot.dat") == 300000);
    CHECK(std::filesystem::exists(HeaderOn(root.path)));
    CHECK_FALSE(std::filesystem::exists(PackageOn(mu)));
    CHECK_FALSE(std::filesystem::exists(HeaderOn(mu)));
    CHECK_FALSE(std::filesystem::exists(mu / ".x360n-transfer"));
    CHECK(guest.call(__imp__XMsgCancelIORequest, {cancel_io, 1}) == 0);
    // A regression must not contaminate the second independent operation.
    if (std::filesystem::exists(PackageOn(mu))) break;
  }
  CHECK(guest.call(__imp__XMsgCancelIORequest, {0, 0}) == X_ERROR_INVALID_PARAMETER);
  CHECK(guest.call(__imp__XMsgCancelIORequest, {0x1234, 0}) == X_ERROR_INVALID_PARAMETER);
}

TEST_CASE("Storage FE transfer requests use the original 552-byte packet and complete once",
          "[sdk][storage-transfer][storage-message]") {
  StorageRoot root;
  root.config("version=1\n[[device]]\nid=7\ntype='mu'\ncapacity_bytes=4194304\n"
              "[[device]]\nid=12\ntype='usb'\ncapacity_bytes=4194304\n");
  const auto mu = root.path / "storage/00000007";
  const auto usb = root.path / "storage/0000000C";
  std::filesystem::create_directories(mu);
  std::filesystem::create_directories(usb);
  DeviceGuest guest(root.path);
  const auto packet = guest.address + 4096;
  const auto io = guest.address + 2304;
  auto put = [&](uint32_t offset, uint32_t value) {
    rex::memory::store_and_swap<uint32_t>(guest.bytes + packet + offset, value);
  };
  MakeItem(guest, 1, root.path, 300000);
  std::memcpy(guest.bytes + packet + 16, guest.bytes + guest.address + 1024, 512);
  put(8, 7); put(532, 2); // device supplied, copy
  auto dispatch = [&] {
    return guest.call(__imp__XMsgSystemProcessCall, {0xFE, 0x2000C, packet, 552});
  };
  REQUIRE(dispatch() == 0);
  CHECK(std::filesystem::file_size(PackageOn(mu) / "dir/slot.dat") == 300000);
  CHECK(std::filesystem::exists(HeaderOn(mu)));
  CHECK(std::filesystem::exists(PackageOn(root.path)));
  CHECK(dispatch() == 0x800700B7); // duplicate, no overwrite

  // The original move wrapper sets bit 0. Packet +528 is its OVERLAPPED.
  put(16, 7); put(8, 12); put(532, 3); put(528, io);
  REQUIRE(guest.call(__imp__XMsgStartIORequest, {0xFE, 0x2000C, io, packet, 552}) == 0);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (guest.word(io - guest.address) == 997 && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  REQUIRE(guest.word(io - guest.address) == 0);
  CHECK(guest.word(io - guest.address + 24) == 0);
  CHECK(std::filesystem::exists(PackageOn(usb)));
  CHECK_FALSE(std::filesystem::exists(PackageOn(mu)));
  CHECK_FALSE(std::filesystem::exists(HeaderOn(mu)));
  CHECK(std::filesystem::exists(PackageOn(root.path)));

  // The message acknowledgement must not complete the worker's OVERLAPPED.
  auto* dispatcher = guest.runtime.function_dispatcher();
  REQUIRE(dispatcher->InitializeFunctionTable(0x82000000, 0x1000, 0x82000000, 0x10000, true, true));
  REQUIRE(dispatcher->SetFunction(0x82000100, &CancelTransferCallback));
  cancel_io = io; cancel_calls = 0;
  put(16, 12); put(8, 7); put(536, 0x82000100);
  REQUIRE(guest.call(__imp__XMsgStartIORequest, {0xFE, 0x2000C, io, packet, 552}) == 0);
  const auto cancel_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (guest.word(io - guest.address) == 997 && std::chrono::steady_clock::now() < cancel_deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  REQUIRE(guest.word(io - guest.address) == 1223);
  CHECK(guest.word(io - guest.address + 24) == 0x800704C7);
  CHECK(cancel_calls.load() == 1);
  CHECK(std::filesystem::exists(PackageOn(usb)));
  CHECK_FALSE(std::filesystem::exists(PackageOn(mu)));

  // Invalid descriptors and unsupported optional data must not start I/O.
  put(528, 0); put(532, 0);
  CHECK(dispatch() == 0x80070057);
  put(532, 0xA);
  CHECK(dispatch() == 0x80070057);
  put(532, 2); put(544, 1);
  CHECK(dispatch() == 0x80070032);
  CHECK(guest.call(__imp__XMsgSystemProcessCall, {0xFE, 0x2000C, packet, 551}) == 0x80070057);
  CHECK(guest.call(__imp__XMsgSystemProcessCall, {0xFE, 0x2000C, 0x1234, 552}) == 0x80070057);
  CHECK(guest.call(__imp__XMsgStartIORequest, {0xFE, 0x2000C, io, packet, 552}) == 0x80070057);
}

TEST_CASE("XMsg cancellation waits for worker cleanup even without a guest event",
          "[sdk][storage-transfer][storage-cancel]") {
  StorageRoot root;
  DeviceGuest guest(root.path);
  const auto io = guest.address + 2304;
  auto* content = guest.runtime.kernel_state()->content_manager();
  auto request = content->BeginTransfer(io);
  REQUIRE(request);
  CHECK_FALSE(content->BeginTransfer(io));
  rex::memory::store_and_swap<uint32_t>(guest.bytes + io, 997);
  std::atomic<uint32_t> returned{~0u};
  auto waiter = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
      guest.runtime.kernel_state(), 128 * 1024, 0, [&] {
    auto& ctx = *rex::system::XThread::GetCurrentThread()->thread_state()->context();
    ctx.r3.u64 = io; ctx.r4.u64 = 1;
    __imp__XMsgCancelIORequest(ctx, guest.bytes);
    returned.store(ctx.r3.u32);
    return 0;
  }));
  REQUIRE(waiter->Create() == 0);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!request->cancelled.load() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  CHECK(request->cancelled.load());
  CHECK(returned.load() == ~0u);
  content->FinishTransfer(io, request, [&] {
    guest.runtime.kernel_state()->CompleteOverlappedEx(io, 1223, 0x800704C7, 0);
  });
  uint64_t timeout = uint64_t(-50'000'000ll);
  REQUIRE(waiter->Wait(0, 0, 0, &timeout) == 0);
  CHECK(returned.load() == 0);
  CHECK(guest.word(io - guest.address) == 1223);
  CHECK(guest.word(io - guest.address + 24) == 0x800704C7);
  CHECK(guest.call(__imp__XMsgCancelIORequest, {io, 1}) == 0);
  // Reuse is allowed after completion, with a fresh cancellation state.
  const auto reused = content->BeginTransfer(io);
  REQUIRE(reused);
  CHECK_FALSE(reused->cancelled.load());
  content->FinishTransfer(io, reused, [] {});
  rex::memory::store_and_swap<uint32_t>(guest.bytes + io, 997);
  CHECK(guest.call(__imp__XMsgCancelIORequest, {io, 0}) == 50);
  CHECK(guest.word(io - guest.address) == 997);
}

TEST_CASE("Storage copies and moves packages between volumes without partial results",
          "[sdk][storage-transfer]") {
  StorageRoot root;
  root.config("version=1\n[[device]]\nid=7\ntype='mu'\ncapacity_bytes=4194304\n"
              "[[device]]\nid=12\ntype='usb'\ncapacity_bytes=8388608\n");
  const auto mu = root.path / "storage/00000007";
  const auto usb = root.path / "storage/0000000C";
  std::filesystem::create_directories(mu);
  std::filesystem::create_directories(usb);
  // A staging directory abandoned by an interrupted process.
  std::filesystem::create_directories(mu / ".x360n-transfer" / "stale");
  DeviceGuest guest(root.path);
  auto* dispatcher = guest.runtime.function_dispatcher();
  REQUIRE(dispatcher->InitializeFunctionTable(0x82000000, 0x1000, 0x82000000, 0x10000, true, true));
  REQUIRE(dispatcher->SetFunction(0x82000100, &ProgressCallback));
  const auto item = guest.address + 1024;
  const auto device = guest.address + 2048;
  const auto io = guest.address + 2304;
  auto put = [&](uint32_t address, uint32_t value) {
    rex::memory::store_and_swap<uint32_t>(guest.bytes + address, value);
  };
  MakeItem(guest, 1, root.path, 300000);

  // Neither owner nor destination: rejected before any work (816EEAB4).
  CHECK(guest.call(__imp__XamContentCopyInternal, {0, 0, item, 0, 0, 0, 0}) == 87);
  // HDD -> MU, synchronous, with progress. Context arrives as the ninth argument.
  put(device, 7);
  progress_log = {};
  CHECK(guest.call(__imp__XamContentCopyInternal, {0, device, item, 0x82000100, 0xC0FFEE, 0, 0}) == 0);
  REQUIRE(progress_log.reasons.size() >= 3);
  CHECK(progress_log.reasons.front() == 1);
  CHECK(progress_log.transferred.front() == 0);
  for (size_t i = 1; i < progress_log.reasons.size(); ++i) {
    CHECK(progress_log.reasons[i] == 0);
    CHECK(progress_log.transferred[i] >= progress_log.transferred[i - 1]);
  }
  CHECK(progress_log.transferred.back() == progress_log.totals.back());
  for (auto context : progress_log.contexts) CHECK(context == 0xC0FFEE);
  CHECK(std::filesystem::file_size(PackageOn(mu) / "dir" / "slot.dat") == 300000);
  CHECK(std::filesystem::file_size(PackageOn(mu) / "small.bin") == 3);
  REQUIRE(std::filesystem::exists(HeaderOn(mu)));
  rex::system::xam::XCONTENT_AGGREGATE_DATA header{};
  std::ifstream(HeaderOn(mu), std::ios::binary).read(reinterpret_cast<char*>(&header), sizeof(header));
  CHECK(uint32_t(header.device_id) == 7);
  CHECK(uint64_t(header.xuid) == kOwner);
  CHECK(std::filesystem::exists(PackageOn(root.path)));  // copy keeps the source
  CHECK_FALSE(std::filesystem::exists(mu / ".x360n-transfer"));  // stale staging purged
  // The same item already on the destination is never overwritten.
  CHECK(guest.call(__imp__XamContentCopyInternal, {0, device, item, 0, 0, 0, 0}) == 183);

  // MU -> USB move through OVERLAPPED; the source item is the MU copy.
  put(item, 7);
  put(device, 12);
  std::memset(guest.bytes + io, 0, 28);
  CHECK(guest.call(__imp__XamContentMoveInternal, {0, device, item, 0, 0, 0, io}) == 997);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (guest.word(io - guest.address) == 997 && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  CHECK(guest.word(io - guest.address) == 0);
  CHECK(guest.word(io - guest.address + 24) == 0);
  CHECK(std::filesystem::file_size(PackageOn(usb) / "dir" / "slot.dat") == 300000);
  CHECK(std::filesystem::exists(HeaderOn(usb)));
  CHECK_FALSE(std::filesystem::exists(PackageOn(mu)));
  CHECK_FALSE(std::filesystem::exists(HeaderOn(mu)));

  // Cancelling from the callback leaves no destination and no staging files.
  put(item, 1);
  put(device, 7);
  progress_log = {};
  progress_log.cancel_at = 1;
  CHECK(guest.call(__imp__XamContentCopyInternal, {0, device, item, 0x82000100, 1, 0, 0}) == 1223);
  CHECK(progress_log.reasons.size() == 2);
  CHECK_FALSE(std::filesystem::exists(PackageOn(mu)));
  CHECK_FALSE(std::filesystem::exists(HeaderOn(mu)));
  CHECK_FALSE(std::filesystem::exists(mu / ".x360n-transfer"));
  CHECK(std::filesystem::exists(PackageOn(root.path)));

  // Insufficient space is refused before copying anything.
  std::filesystem::remove_all(root.path / "E000000012345678");
  MakeItem(guest, 1, root.path, 5 * 1024 * 1024);
  CHECK(guest.call(__imp__XamContentMoveInternal, {0, device, item, 0, 0, 0, 0}) == 112);
  CHECK_FALSE(std::filesystem::exists(PackageOn(mu)));
  CHECK(std::filesystem::exists(PackageOn(root.path)));  // a refused move keeps the source
}

TEST_CASE("Interrupted storage staging is reclaimed before checking the destination quota",
          "[sdk][storage-transfer][storage-transfer-recovery][storage-stale-quota]") {
  StorageRoot root;
  root.config("version=1\n[[device]]\nid=7\ntype='mu'\ncapacity_bytes=1048576\n");
  const auto mu = root.path / "storage/00000007";
  const auto stale = mu / ".x360n-transfer/interrupted/package";
  std::filesystem::create_directories(stale);
  std::ofstream(stale / "partial.bin", std::ios::binary) << std::string(900000, 'x');
  {
    DeviceGuest guest(root.path);
    MakeItem(guest, 1, root.path, 300000);
    rex::memory::store_and_swap<uint32_t>(guest.bytes + guest.address + 2048, 7);
    REQUIRE(guest.call(__imp__XamContentCopyInternal,
        {0, guest.address + 2048, guest.address + 1024, 0, 0, 0, 0}) == 0);
    CHECK_FALSE(std::filesystem::exists(mu / ".x360n-transfer"));
    CHECK(std::filesystem::exists(PackageOn(root.path)));
  }
  DeviceGuest restarted(root.path);
  const auto items = restarted.runtime.kernel_state()->content_manager()->ListContent(
      7, kOwner, rex::system::XContentType::kSavedGame, kTitle);
  REQUIRE(items.size() == 1);
  CHECK(items.front().file_name() == "SAVE0001");
  std::ifstream input(PackageOn(mu) / "dir/slot.dat", std::ios::binary);
  const std::string bytes(std::istreambuf_iterator<char>{input}, {});
  CHECK(bytes == std::string(300000, 's'));
}

TEST_CASE("An active transfer preserves its staging while another volume recovers",
          "[sdk][storage-transfer][storage-transfer-recovery][storage-stale-quota]") {
  StorageRoot root;
  root.config("version=1\n[[device]]\nid=7\ntype='mu'\ncapacity_bytes=1048576\n"
              "[[device]]\nid=12\ntype='usb'\ncapacity_bytes=1048576\n");
  const auto mu=root.path/"storage/00000007", usb=root.path/"storage/0000000C";
  std::filesystem::create_directories(mu);
  std::filesystem::create_directories(usb/".x360n-transfer/interrupted");
  std::ofstream(usb/".x360n-transfer/interrupted/partial.bin",std::ios::binary)
      << std::string(900000,'x');
  DeviceGuest guest(root.path);
  const auto data=MakeItem(guest,1,root.path,300000);
  auto* content=guest.runtime.kernel_state()->content_manager();
  uint32_t other_result=~0u;
  REQUIRE(content->TransferContent(kOwner,data,7,0,false,[&](uint64_t,uint64_t,bool first) {
    if(first) other_result=content->TransferContent(kOwner,data,12,0,false,{});
    return true;
  })==0);
  CHECK(other_result==0);
  CHECK(std::filesystem::file_size(PackageOn(mu)/"dir/slot.dat")==300000);
  CHECK(std::filesystem::file_size(PackageOn(usb)/"dir/slot.dat")==300000);
  CHECK(std::filesystem::exists(HeaderOn(mu)));
  CHECK(std::filesystem::exists(HeaderOn(usb)));
  CHECK_FALSE(std::filesystem::exists(mu/".x360n-transfer"));
  CHECK_FALSE(std::filesystem::exists(usb/".x360n-transfer"));
}

#if defined(_WIN32)
TEST_CASE("A failed move cleanup preserves the complete committed destination",
          "[sdk][storage-transfer][storage-transfer-recovery]") {
  StorageRoot root;
  root.config("version=1\n[[device]]\nid=7\ntype='mu'\ncapacity_bytes=4194304\n");
  const auto mu=root.path/"storage/00000007";
  std::filesystem::create_directories(mu);
  {
    DeviceGuest guest(root.path);
    MakeItem(guest,1,root.path,300000);
    // The copy can read the header, but Windows refuses to delete it. The
    // source package itself is removed before DeleteContent reaches this file.
    struct LockedHeader {
      HANDLE handle;
      ~LockedHeader(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
    } locked{CreateFileW(HeaderOn(root.path).c_str(),GENERIC_READ,
        FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
    REQUIRE(locked.handle!=INVALID_HANDLE_VALUE);
    rex::memory::store_and_swap<uint32_t>(guest.bytes+guest.address+2048,7);
    REQUIRE(guest.call(__imp__XamContentMoveInternal,
        {0,guest.address+2048,guest.address+1024,0,0,0,0})==X_ERROR_ACCESS_DENIED);
    CHECK_FALSE(std::filesystem::exists(PackageOn(root.path)));
    REQUIRE(std::filesystem::exists(PackageOn(mu)/"dir/slot.dat"));
    CHECK(std::filesystem::file_size(PackageOn(mu)/"dir/slot.dat")==300000);
    CHECK(std::filesystem::file_size(PackageOn(mu)/"small.bin")==3);
    CHECK(std::filesystem::exists(HeaderOn(mu)));
  }
  // Recovery must remain discoverable after the original runtime is gone.
  DeviceGuest restarted(root.path);
  const auto data=restarted.runtime.kernel_state()->content_manager()->ListContent(
      7,kOwner,rex::system::XContentType::kSavedGame,kTitle);
  REQUIRE(data.size()==1);
  CHECK(data.front().file_name()=="SAVE0001");
  std::ifstream input(PackageOn(mu)/"dir/slot.dat",std::ios::binary);
  const std::string bytes(std::istreambuf_iterator<char>{input},{});
  CHECK(bytes==std::string(300000,'s'));
}
#endif

TEST_CASE("A session's titles share the console hard drive, not their own user root",
          "[sdk][storage-device][storage-hdd-root]") {
  StorageRoot title_root, hard_drive;
  struct RestoreFlag {
    std::string value = rex::cvar::GetFlagByName("content_data_root");
    ~RestoreFlag() { rex::cvar::SetFlagByName("content_data_root", value); }
  } restore;
  REQUIRE(rex::cvar::SetFlagByName("content_data_root", hard_drive.path.string()));
  DeviceGuest guest(title_root.path);
  auto* content = guest.runtime.kernel_state()->content_manager();
  CHECK(std::filesystem::equivalent(content->DeviceRoot(1), hard_drive.path));
  // Saves created by the title land on the hard drive; its own root stays clean.
  rex::system::xam::XCONTENT_AGGREGATE_DATA data{};
  data.device_id = 1;
  data.content_type = rex::system::XContentType::kSavedGame;
  data.set_file_name("SAVE0001");
  data.xuid = 0xE000000012345678ull;
  data.title_id = 0x1234ABCD;
  REQUIRE(content->WriteContentHeaderFile(0xE000000012345678ull, data) == 0);
  CHECK(std::filesystem::exists(hard_drive.path / "E000000012345678" / "1234ABCD" / "Headers"));
  CHECK_FALSE(std::filesystem::exists(title_root.path / "E000000012345678"));
}


STORAGE_IMPORT(XamAlloc);
STORAGE_IMPORT(XamFree);
STORAGE_IMPORT(XamContentRegisterChangeCallback);
STORAGE_IMPORT(XamContentDeleteInternal);
namespace {
struct ContentEvent { uint32_t reason, device; uint64_t owner, size; bool present; };
std::vector<ContentEvent> content_events;
void ContentChangeCallback(PPCContext& ctx, uint8_t* base) {
  const auto reason = ctx.r3.u32, source = ctx.r4.u32;
  auto* memory = rex::Runtime::instance()->memory();
  const auto output = memory->SystemHeapAlloc(4);
  if (!output) return;
  // Exercise the original queue's allocator too (92263398), rather than
  // accepting an event that the original consumer cannot enqueue.
  ctx.r3.u64 = 0x08000000; ctx.r4.u64 = 528; ctx.r5.u64 = output;
  __imp__XamAlloc(ctx, base);
  const auto result = ctx.r3.u32;
  const auto message = rex::memory::load_and_swap<uint32_t>(base + output);
  memory->SystemHeapFree(output);
  if (result) return;
  rex::memory::store_and_swap<uint32_t>(base + message, reason);
  std::memcpy(base + message + 8, base + source, 512);
  ctx.r3.u64 = reason; ctx.r4.u64 = message + 8;
  auto* data = reinterpret_cast<rex::system::xam::XCONTENT_AGGREGATE_DATA*>(base + ctx.r4.u32);
  content_events.push_back({ctx.r3.u32, data->device_id, data->xuid,
      rex::memory::load_and_swap<uint64_t>(base + ctx.r4.u32 + 328),
      rex::Runtime::instance()->kernel_state()->content_manager()->ContentExists(data->xuid, *data)});
  ctx.r3.u64 = message; __imp__XamFree(ctx, base);
  // A callback cannot replace the export's return value or caller registers.
  ctx.r3.u64 = 0xBADBAD;
}
}
TEST_CASE("Storage mutations notify the registered original content cache", "[sdk][storage-transfer][storage-change]") {
  StorageRoot root;
  root.config("version=1\n[[device]]\nid=7\ntype='mu'\n[[device]]\nid=12\ntype='usb'\n");
  const auto mu = root.path / "storage/00000007", usb = root.path / "storage/0000000C";
  std::filesystem::create_directories(mu); std::filesystem::create_directories(usb);
  DeviceGuest guest(root.path);
  auto* dispatcher = guest.runtime.function_dispatcher();
  REQUIRE(dispatcher->InitializeFunctionTable(0x82000000, 0x1000, 0x82000000, 0x10000, true, true));
  REQUIRE(dispatcher->SetFunction(0x82000200, &ContentChangeCallback));
  MakeItem(guest, 1, root.path, 1234);
  const auto item = guest.address + 1024, device = guest.address + 2048, io = guest.address + 2304;
  guest.call(__imp__XamContentRegisterChangeCallback, {0x82000200});
  content_events.clear();
  rex::memory::store_and_swap<uint32_t>(guest.bytes + device, 7);
  REQUIRE(guest.call(__imp__XamContentCopyInternal, {0, device, item, 0, 0, 0, 0}) == 0);
  CHECK(content_events.size() == 1);
  if (!content_events.empty()) {
    CHECK(content_events[0].reason == 0); CHECK(content_events[0].device == 7);
    CHECK(content_events[0].owner == kOwner); CHECK(content_events[0].size == 1237);
    CHECK(content_events[0].present);
  }
  const auto copied_count = content_events.size();
  CHECK(guest.call(__imp__XamContentCopyInternal, {0, device, item, 0, 0, 0, 0}) == 183);
  CHECK(content_events.size() == copied_count);
  REQUIRE(dispatcher->SetFunction(0x82000100, &ProgressCallback));
  progress_log = {}; progress_log.cancel_at = 0;
  rex::memory::store_and_swap<uint32_t>(guest.bytes + device, 12);
  CHECK(guest.call(__imp__XamContentCopyInternal, {0, device, item, 0x82000100, 0, 0, 0}) == 1223);
  CHECK(content_events.size() == copied_count);
  rex::memory::store_and_swap<uint32_t>(guest.bytes + item, 7);
  rex::memory::store_and_swap<uint32_t>(guest.bytes + device, 12);
  REQUIRE(guest.call(__imp__XamContentMoveInternal, {0, device, item, 0, 0, 0, io}) == 997);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (guest.word(io - guest.address) == 997 && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  REQUIRE(guest.word(io - guest.address) == 0);
  CHECK(content_events.size() == 3);
  if (content_events.size() == 3) {
    CHECK(content_events[1].reason == 0); CHECK(content_events[1].device == 12);
    CHECK(content_events[1].present);
    CHECK(content_events[2].reason == 1); CHECK(content_events[2].device == 7);
    CHECK_FALSE(content_events[2].present);
  }
  rex::memory::store_and_swap<uint32_t>(guest.bytes + item, 12);
  REQUIRE(guest.call(__imp__XamContentDeleteInternal, {item, 0}) == 0);
  CHECK(content_events.size() == 4);
  if (content_events.size() == 4) {
    CHECK(content_events[3].reason == 1); CHECK(content_events[3].device == 12);
    CHECK_FALSE(content_events[3].present);
  }
  const auto deleted_count = content_events.size();
  CHECK(guest.call(__imp__XamContentDeleteInternal, {item, 0}) == 2);
  CHECK(content_events.size() == deleted_count);
  guest.call(__imp__XamContentRegisterChangeCallback, {0});
  rex::memory::store_and_swap<uint32_t>(guest.bytes + item, 1);
  CHECK(guest.call(__imp__XamContentDeleteInternal, {item, 0}) == 0);
  CHECK(content_events.size() == deleted_count);
#if defined(_WIN32)
  // Move commits the destination even if Windows locks the source header.
  MakeItem(guest, 1, root.path, 1234);
  content_events.clear();
  guest.call(__imp__XamContentRegisterChangeCallback, {0x82000200});
  struct LockedHeader {
    HANDLE handle;
    ~LockedHeader() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
  } locked{CreateFileW(HeaderOn(root.path).c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
  REQUIRE(locked.handle != INVALID_HANDLE_VALUE);
  rex::memory::store_and_swap<uint32_t>(guest.bytes + device, 7);
  CHECK(guest.call(__imp__XamContentMoveInternal, {0, device, item, 0, 0, 0, 0}) == 5);
  CHECK(content_events.size() == 1);
  if (content_events.size() == 1) {
    CHECK(content_events[0].reason == 0); CHECK(content_events[0].device == 7);
    CHECK(content_events[0].present); CHECK(content_events[0].size == 1237);
  }
  guest.call(__imp__XamContentRegisterChangeCallback, {0});
#endif
}
