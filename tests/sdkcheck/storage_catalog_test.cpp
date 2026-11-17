#include <catch2/catch_test_macros.hpp>
#include <array>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <rex/utility.h>
#if defined(_WIN32)
#include <windows.h>
#include <winioctl.h>
#endif
#include <rex/kernel/title_runtime.h>
#include <rex/filesystem/file_system.h>
#include <rex/filesystem/file.h>
#include <rex/filesystem/file_open.h>
#include <rex/kernel/types/content.h>
#include <rex/kernel/types/io.h>
#include <rex/kernel/types/status.h>
#include <rex/xam/content_manager.h>
#include <rex/xam/device.h>
#include "kernel/kernel_fixture.h"
#include "xam/xam_state.h"
#include "temp_folder.h"
#include <rex/platform/file.h>

TEST_CASE("Storage catalog reports actual HDD capacity and no absent optical device", "[sdk][storage-catalog]") {
  rex::test::KernelFixture fixture;
  const auto volume = fixture.GetRuntime().GetContentManager().GetDevice(1);
  REQUIRE(volume);
  CHECK(volume->name == u"Hard Drive");
  CHECK(volume->total_bytes == std::filesystem::space(volume->root).capacity);
  CHECK(volume->free_bytes <= std::filesystem::space(volume->root).available);
  fixture.RunOnGuestThread([](rex::test::Caller& c) {
    const auto out = c.Alloc(0x50);
    CHECK(c.XAM("XamContentGetDeviceData", {1, out}) == 0);
    CHECK(c.Load32(out) == 1);
    CHECK(c.XAM("XamContentGetDeviceState", {2, 0}) == rex::kernel::X_ERROR_DEVICE_NOT_CONNECTED);
    CHECK(c.XAM("XamContentGetDeviceName", {1, 0, 32}) == rex::kernel::X_ERROR_INVALID_PARAMETER);
    CHECK(c.XAM("XamContentGetDeviceData", {1, 0xFFFFFFF0}) == rex::kernel::X_ERROR_INVALID_PARAMETER);
    CHECK(c.XAM("XamContentCreateDeviceEnumerator", {0, 0, 0, 0, out}) == rex::kernel::X_ERROR_INVALID_PARAMETER);
  });
}

TEST_CASE("Configured memory units retain independent quota accounting and disconnect state", "[sdk][storage-catalog]") {
  rex::test::KernelFixture fixture([](const auto& directory) {
    std::filesystem::create_directories(directory / "storage" / "00000007");
    std::ofstream(directory / "storage.toml") << "version=1\n[[device]]\nid=7\ntype='mu'\ncapacity_bytes=4096\nreserve_bytes=128\n";
    std::ofstream payload(directory / "storage" / "00000007" / "payload", std::ios::binary);
    const std::array<char, 512> bytes{};
    payload.write(bytes.data(), bytes.size());
  });
  auto& manager = fixture.GetRuntime().GetContentManager();
  const auto volume = manager.GetDevice(7);
  REQUIRE(volume);
  CHECK(volume->device_type == rex::xam::DeviceType::kMU);
  CHECK(volume->total_bytes == 4096);
  CHECK(volume->free_bytes == 4096 - 512 - 128);
  CHECK(manager.GetDevices().size() == 2);
  const auto hidden = volume->root.parent_path() / "disconnected";
  std::filesystem::rename(volume->root, hidden);
  CHECK_FALSE(manager.GetDevice(7));
  std::filesystem::rename(hidden, volume->root);
  CHECK(manager.GetDevice(7));
}

TEST_CASE("Malformed storage configuration never silently supplies a different volume", "[sdk][storage-catalog]") {
  for (const auto* text : {"version=2\n", "version=1\n[hdd]\ncapacity_bytes=-1\n",
                          "version=1\n[[device]]\nid=7\ntype='mu'\n[[device]]\nid=7\ntype='usb'\n"}) {
    rex::test::KernelFixture fixture([&](const auto& directory) {
      std::ofstream(directory / "storage.toml") << text;
    });
    CHECK(fixture.GetRuntime().GetContentManager().GetDevices().empty());
  }
}

TEST_CASE("Storage path validation accepts ordinary and extended host paths", "[sdk][storage-paths]") {
  rex::test::TempFolder folder("storage_path_");
  CHECK(rex::xam::IsUnlinkedStoragePath(folder.path));
  const auto extended = rex::platform::HostPath(folder.path);
  CHECK(rex::xam::IsUnlinkedStoragePath(extended));
}

#if defined(_WIN32)
namespace {
void CreateStorageJunction(const std::filesystem::path& link, const std::filesystem::path& target_path) {
  REQUIRE(CreateDirectoryW(link.c_str(), nullptr));
  const auto target = target_path.wstring();
  const auto substitute = L"\\??\\" + target;
  struct JunctionData {
    DWORD tag;
    WORD size, reserved, substitute_offset, substitute_length, print_offset, print_length;
    wchar_t path[2048];
  } data{};
  data.tag = IO_REPARSE_TAG_MOUNT_POINT;
  data.substitute_length = WORD(substitute.size() * sizeof(wchar_t));
  data.print_offset = data.substitute_length + sizeof(wchar_t);
  data.print_length = WORD(target.size() * sizeof(wchar_t));
  data.size = WORD(8 + data.print_offset + data.print_length + sizeof(wchar_t));
  REQUIRE(substitute.size() + target.size() + 2 < std::size(data.path));
  std::memcpy(data.path, substitute.c_str(), data.substitute_length);
  std::memcpy(reinterpret_cast<uint8_t*>(data.path) + data.print_offset,
              target.c_str(), data.print_length);
  const auto handle = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                 FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  REQUIRE(handle != INVALID_HANDLE_VALUE);
  DWORD returned = 0;
  const auto created = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, &data,
                                      8 + data.size, nullptr, 0, &returned, nullptr);
  CloseHandle(handle);
  REQUIRE(created);
}
}

TEST_CASE("Storage rejects a real junction and every descendant without changing its target", "[sdk][storage-paths]") {
  rex::test::TempFolder folder("storage_link_"), target("storage_target_");
  const auto link = folder.path / "linked";
  rex::ScopeGuard detach([&] { RemoveDirectoryW(link.c_str()); });
  CreateStorageJunction(link, target.path);
  const auto payload = target.path / "preserved.txt";
  std::ofstream(payload) << "preserved";
  CHECK_FALSE(rex::xam::IsUnlinkedStoragePath(link));
  CHECK_FALSE(rex::xam::IsUnlinkedStoragePath(rex::platform::HostPath(link)));
  CHECK_FALSE(rex::xam::IsUnlinkedStoragePath(link / "preserved.txt"));
  CHECK_FALSE(rex::xam::IsUnlinkedStoragePath(link / "not-created"));
  CHECK(std::filesystem::file_size(payload) == 9);
}
#endif

TEST_CASE("Mounted content shares quota with XAM and rejects growth without losing valid bytes", "[sdk][storage-catalog]") {
  rex::test::KernelFixture fixture([](const auto& directory) {
    std::filesystem::create_directories(directory / "storage" / "00000007");
    std::ofstream(directory / "storage.toml") << "version=1\n[[device]]\nid=7\ntype='mu'\ncapacity_bytes=65536\nreserve_bytes=128\n";
  });
  auto& runtime = fixture.GetRuntime();
  auto& manager = runtime.GetContentManager();
  rex::kernel::XCONTENT_AGGREGATE_DATA data{};
  data.device_id = 7; data.title_id = 0x584108B7;
  data.content_type = rex::kernel::XContentType::kMarketplaceContent;
  data.SetFileName("quota_package");
  REQUIRE(manager.CreateContent("quota", 0, data));
  auto opened = runtime.GetFileSystem().Open("quota:\\payload", {.disposition = rex::filesystem::Disposition::kCreate, .access = rex::filesystem::Access::kWriteAccess});
  REQUIRE(opened);
  std::array<uint8_t, 512> payload{};
  payload.fill(0x5A);
  REQUIRE(opened->file->Write(payload, 0));
  const auto snapshot = manager.GetDevice(7);
  REQUIRE(snapshot);
  const auto volume = opened->file->QueryVolume();
  REQUIRE(volume);
  CHECK(volume->total_allocation_units * volume->bytes_per_sector == snapshot->total_bytes);
  CHECK(volume->available_allocation_units * volume->bytes_per_sector <= snapshot->free_bytes);
  const auto old_size = std::filesystem::file_size(snapshot->root / "0000000000000000" / "584108B7" / "00000002" / "quota_package" / "payload");
  const auto too_large = opened->file->SetLength(65536);
  REQUIRE_FALSE(too_large);
  CHECK(rex::kernel::ToXStatus(too_large.error()) == 0xC000007F);
  const auto bad_write = opened->file->Write(payload, 65536);
  REQUIRE_FALSE(bad_write);
  CHECK(rex::kernel::ToXStatus(bad_write.error()) == 0xC000007F);
  opened->file->Refresh();
  CHECK(opened->file->GetInfo().size == old_size);
  std::array<char, 512> intact{};
  std::ifstream previous(snapshot->root / "0000000000000000" / "584108B7" / "00000002" / "quota_package" / "payload", std::ios::binary);
  previous.read(intact.data(), intact.size());
  CHECK(previous.gcount() == 512);
  CHECK(std::all_of(intact.begin(), intact.end(), [](char byte) { return byte == 0x5A; }));
  previous.close();
  REQUIRE(opened->file->SetLength(128));
  opened->file->Refresh(); CHECK(opened->file->GetInfo().size == 128);
  CHECK(manager.ListContent(7, 0, data.content_type, data.title_id).size() == 1);
  CHECK(manager.ListContent(1, 0, data.content_type, data.title_id).empty());

  const auto current_volume = manager.GetDevice(7);
  REQUIRE(current_volume);
  fixture.RunOnGuestThread([&](rex::test::Caller& c) {
    const auto handle = c.Alloc(4), status = c.Alloc(8), info = c.Alloc(0x50);
    CHECK(c.XAM("XamContentGetDefaultDevice", {info}) == 0);
    CHECK(c.Load32(info) == 1);
    CHECK(c.XAM("XamContentGetDefaultDevice", {0xFFFFFFF0}) == rex::kernel::X_ERROR_INVALID_PARAMETER);
    CHECK(c.XAM("XamContentGetLocalizedDeviceData", {7, info}) == 0);
    CHECK(c.Load32(info) == 7); CHECK(c.Load32(info + 4) == 2);
    REQUIRE(c.Kernel("NtCreateFile", {handle, 0xC0100000, c.AllocAttributes("quota:\\payload"), status, 0, 0, 3, 1, 0x20}) == 0);
    REQUIRE(c.Kernel("NtQueryVolumeInformationFile", {c.Load32(handle), status, info, 24, rex::kernel::XFileFsSizeInformation}) == 0);
    CHECK(c.Load32(info) == 0); CHECK(c.Load32(info + 4) == 128);
    CHECK(c.Load32(info + 8) == 0);
    CHECK(c.Load32(info + 12) == current_volume->free_bytes / 512);
    CHECK(c.Load32(info + 16) == 1); CHECK(c.Load32(info + 20) == 512);
    CHECK(c.Kernel("NtClose", {c.Load32(handle)}) == 0);
    const auto count = c.Alloc(4), aggregate = c.Alloc(584);
    for (const uint32_t device : {0u, 7u}) {
      REQUIRE(c.XAM("XamContentAggregateCreateEnumerator", {0, device, 0xFFFFFFFF, 0x584108B7, handle}) == 0);
      REQUIRE(c.XAM("XamEnumerate", {c.Load32(handle), 0, aggregate, 584, count, 0}) == 0);
      CHECK(c.Load32(count) == 1); CHECK(c.Load32(aggregate) == 7);
      CHECK(c.XAM("XamEnumerate", {c.Load32(handle), 0, aggregate, 584, count, 0}) == rex::kernel::X_ERROR_NO_MORE_FILES);
      CHECK(c.Kernel("NtClose", {c.Load32(handle)}) == 0);
    }
  });
  REQUIRE(manager.CloseContent("quota"));
}

TEST_CASE("Optical storage requires an actual read-only mount and reports original file bytes", "[sdk][storage-catalog]") {
  rex::test::KernelFixture fixture([](const auto& directory) {
    std::filesystem::create_directories(directory / "optical");
    std::ofstream(directory / "optical" / "original.bin", std::ios::binary).write("original", 8);
  });
  REQUIRE(fixture.GetRuntime().GetFileSystem().Mount({.path = "\\Device\\Cdrom0", .type = rex::filesystem::ProviderType::kHostDirectory, .source = fixture.GetRoot() / "optical", .flags = rex::filesystem::MountFlags::kNone, .null_paths = {}}));
  fixture.RunOnGuestThread([](rex::test::Caller& c) {
    const auto volume = rex::xam::GetContentDeviceInfo(2);
    REQUIRE(volume);
    CHECK(volume->device_type == rex::xam::DeviceType::kODD);
    CHECK(volume->total_bytes == 8); CHECK(volume->free_bytes == 0);
    const auto out = c.Alloc(0x50);
    CHECK(c.XAM("XamContentGetDeviceData", {2, out}) == 0);
    CHECK(c.Load32(out) == 2);
    CHECK(c.Load32(out + 4) == 4);
  });
}

TEST_CASE("Original storage selector retains the full PPC request and system-app completion ownership", "[sdk][xam][storage-selector]") {
  rex::test::KernelFixture fixture;
  fixture.RunOnGuestThread([](rex::test::Caller& c) {
    auto& ui = rex::xam::CurrentXAM().GetSystemUI();
    rex::ScopeGuard reset([&] { ui.system_ui_request = {}; });
    const auto output = c.Alloc(4), overlap = c.Alloc(sizeof(rex::kernel::XOVERLAPPED));
    unsigned calls = 0;
    ui.system_ui_request = [&](uint32_t message, uint32_t request, uint32_t size) {
      ++calls;
      CHECK(message == 0x2100B); REQUIRE(size == 40);
      CHECK(c.Load32(request) == 0); CHECK(c.Load32(request + 4) == 0xFF);
      CHECK(c.Load32(request + 8) == 1); CHECK(c.Load32(request + 12) == 0x10200);
      CHECK(c.Load32(request + 16) == 1); CHECK(c.Load32(request + 20) == 1);
      CHECK(c.Load32(request + 24) == output); CHECK(c.Load32(request + 28) == overlap);
      CHECK(c.Load32(request + 32) == 0); CHECK(c.Load32(request + 36) == 0);
      return uint32_t(0);
    };
    c.Store32(output, 0xDEADBEEF);
    REQUIRE(c.XAM("XamShowDeviceSelectorUI", {0xFF, 1, 0x10000, 0x100000001ull, output, overlap}) == rex::kernel::X_ERROR_IO_PENDING);
    CHECK(calls == 1); CHECK(c.Load32(output) == 0xDEADBEEF);
    CHECK(c.XAM("XGetOverlappedResult", {overlap, 0, 0}) == rex::kernel::X_ERROR_IO_INCOMPLETE);
    REQUIRE(c.XAM("XMsgCompleteIORequest", {overlap, rex::kernel::X_ERROR_FUNCTION_FAILED, 0x800704C7, 0}) == 0);
    CHECK(c.XAM("XGetOverlappedResult", {overlap, 0, 0}) == rex::kernel::X_ERROR_FUNCTION_FAILED);
    CHECK(c.XAM("XGetOverlappedExtendedError", {overlap}) == 0x800704C7);
    CHECK(c.Load32(output) == 0xDEADBEEF);
    CHECK(c.XAM("XamShowDeviceSelectorUI", {0, 1, 0, 0, output, 0}) == rex::kernel::X_ERROR_INVALID_PARAMETER);
    CHECK(c.XAM("XamShowDeviceSelectorUI", {4, 1, 0, 0, output, overlap}) == rex::kernel::X_ERROR_INVALID_PARAMETER);
    CHECK(c.XAM("XamShowDeviceSelectorUI", {0, 1, 8, 0, output, overlap}) == rex::kernel::X_ERROR_INVALID_PARAMETER);
    CHECK(c.XAM("XamShowDeviceSelectorUI", {0, 1, 0, 0, 0xFFFFFFF0, overlap}) == rex::kernel::X_ERROR_INVALID_PARAMETER);
    CHECK(calls == 1);
    ui.system_ui_request = {};
    CHECK(c.XAM("XamShowDeviceSelectorUI", {0, 1, 0, 0, output, overlap}) == rex::kernel::X_ERROR_NOT_FOUND);
    CHECK(c.Load32(output) == 0xDEADBEEF);
    CHECK(c.XAM("XGetOverlappedExtendedError", {overlap}) == 0x80070490);
  });
}
