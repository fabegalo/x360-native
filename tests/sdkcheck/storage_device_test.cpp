#include <catch2/catch_test_macros.hpp>
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
