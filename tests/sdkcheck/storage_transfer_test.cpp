#include <catch2/catch_test_macros.hpp>
#include <array>
#include <algorithm>
#include <fstream>
#include <format>
#include <map>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <chrono>
#include <thread>
#if defined(_WIN32)
#include <windows.h>
#endif
#include <rex/filesystem/file_system.h>
#include <rex/filesystem/file.h>
#include <rex/filesystem/stfs.h>
#include <rex/platform/file.h>
#include <rex/kernel/types/status.h>
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/function_dispatcher.h>
#include <rex/kernel/types/io.h>
#include <rex/memory/endian.h>
#include <rex/platform/process.h>
#include <rex/string/text.h>
#include "kernel/kernel_fixture.h"
#include "kernel/test_access.h"
#include "temp_folder.h"
#include "xam/xam_state.h"
#include "xam/content_manager.h"
#include "offline_achievement_fixture.h"

namespace {
using Manager = rex::xam::ContentManager;
using Data = rex::kernel::XCONTENT_AGGREGATE_DATA;
using Stage = Manager::TransferStage;
constexpr uint32_t kTitle = 0x584108B7;

std::atomic<bool> progress_entered{false}, release_progress{false};
std::atomic<unsigned> progress_calls{0}, change_calls{0};
std::atomic<uint32_t> progress_context{0}, change_reason{99}, change_device{0};
std::atomic<bool> progress_abi_valid{true};
std::atomic<uint32_t> progress_mode{0};

void Progress(PPCContext& ctx, uint8_t* base) {
  const auto context = rex::memory::load_and_swap<uint64_t>(base + ctx.r1.u32 + 0x50);
  progress_context = uint32_t(context);
  ++progress_calls;
  if (ctx.r3.u64 != ctx.r5.u64 || ctx.r4.u64 != ctx.r6.u64 || ctx.r4.u64 > ctx.r3.u64 ||
      ctx.r7.u64 != 1 || ctx.r9.u64 || ctx.r10.u64) progress_abi_valid = false;
  if (progress_mode == 42) {
    progress_entered = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!release_progress && std::chrono::steady_clock::now() < deadline)
      rex::xam::CurrentTitle().threads.SleepCurrentThread(std::chrono::milliseconds(1));
    ctx.r3.u64 = 0;
  } else ctx.r3.u64 = progress_mode.load();
}

void Changed(PPCContext& ctx, uint8_t* base) {
  ++change_calls;
  change_reason = ctx.r3.u32;
  const auto* item = reinterpret_cast<const Data*>(base + ctx.r4.u32);
  change_device = item->device_id;
}

void Configure(const std::filesystem::path& root) {
  std::filesystem::create_directories(root / "storage" / "00000007");
  std::ofstream(root / "storage.toml") << "version=1\n[[device]]\nid=7\ntype='mu'\ncapacity_bytes=1048576\n";
}

Data Content(uint32_t device = 1) {
  Data data{};
  data.device_id = device; data.title_id = kTitle;
  data.content_type = rex::kernel::XContentType::kMarketplaceContent;
  data.SetFileName("transfer_source");
  return data;
}

uint32_t GuestContent(rex::test::Caller& c) {
  const auto item = c.Alloc(512);
  const auto data = Content();
  std::memcpy(c.GetBase() + item, &data, sizeof(data));
  return item;
}

std::filesystem::path Payload(Manager& manager, uint32_t device) {
  return manager.DeviceRoot(device) / "0000000000000000" / "584108B7" /
         "00000002" / "transfer_source" / "payload";
}

std::array<uint8_t, 8192> Bytes() {
  std::array<uint8_t, 8192> bytes{};
  for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = uint8_t(i % 251);
  return bytes;
}

void Prepare(rex::test::KernelFixture& fixture, const Data& data = Content()) {
  auto& manager = rex::xam::CurrentXAM().GetContentManager();
  REQUIRE(manager.CreateContent("transfer", 0, data));
  auto opened = fixture.GetRuntime().GetFileSystem().Open("transfer:\\payload",
      {.disposition = rex::filesystem::Disposition::kCreate, .access = rex::filesystem::Access::kWriteAccess});
  REQUIRE(opened);
  REQUIRE(opened->file->Write(Bytes(), 0));
  opened->file.reset();
  REQUIRE(manager.CloseContent("transfer"));
}

void CheckPayload(Manager& manager, uint32_t device) {
  std::array<uint8_t, 8193> bytes{};
  std::ifstream input(Payload(manager, device), std::ios::binary);
  input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
  REQUIRE(input.gcount() == 8192);
  const auto expected = Bytes();
  CHECK(std::equal(expected.begin(), expected.end(), bytes.begin()));
}

void Interrupt() {
  throw std::filesystem::filesystem_error("injected storage interruption", std::make_error_code(std::errc::io_error));
}

std::pair<std::filesystem::path, unsigned> CrashInput() {
  const auto root_text = rex::platform::GetEnvironmentValue("X360N_TEST_STORAGE_ROOT");
  const auto stage_text = rex::platform::GetEnvironmentValue("X360N_TEST_STORAGE_STAGE");
  REQUIRE(root_text); REQUIRE(stage_text);
  const auto root = rex::string::to_path(*root_text);
  REQUIRE(root.filename() == "storage_crash_current");
  REQUIRE(std::filesystem::equivalent(root.parent_path(), std::filesystem::temp_directory_path()));
  std::ifstream marker(root / "fixture-owner.txt");
  std::string owner; std::getline(marker, owner);
  REQUIRE(owner == "x360-native storage crash fixture v1");
  REQUIRE(stage_text->size() == 1);
  REQUIRE((*stage_text)[0] >= '0'); REQUIRE((*stage_text)[0] <= '7');
  return {root, static_cast<unsigned>((*stage_text)[0] - '0')};
}

bool DeleteCrash() {
  const auto operation = rex::platform::GetEnvironmentValue("X360N_TEST_STORAGE_OPERATION");
  REQUIRE(operation); REQUIRE((*operation == "move" || *operation == "delete"));
  return *operation == "delete";
}
}

TEST_CASE("Content copy commits a complete independent destination without overwriting existing bytes", "[sdk][storage-transfer]") {
  rex::test::KernelFixture fixture(Configure);
  Prepare(fixture);
  auto& manager = rex::xam::CurrentXAM().GetContentManager();
  bool committed = false, removed = true;
  unsigned copy_progress_calls = 0;
  REQUIRE(manager.TransferContent(0, Content(), 7, 0, false,
      [&](uint64_t done, uint64_t total, bool) { CHECK(done <= total); ++copy_progress_calls; return true; },
      &committed, {}, &removed) == 0);
  CHECK(committed); CHECK_FALSE(removed); CHECK(copy_progress_calls > 1);
  CheckPayload(manager, 1); CheckPayload(manager, 7);
  const auto header = manager.ReadContentHeaderFile("transfer_source", 0, kTitle, Content().content_type, 7);
  REQUIRE(header); CHECK(header->device_id == 7);
  CHECK(manager.TransferContent(0, Content(), 7, 0, false) == rex::kernel::X_ERROR_ALREADY_EXISTS);
  CheckPayload(manager, 1); CheckPayload(manager, 7);
}

TEST_CASE("Guest content exports preserve progress ABI, cancellation, quiet mode and cache callbacks", "[sdk][xam][storage-transfer]") {
  for (const auto mode : {0u, 1u, 3u}) {
    CAPTURE(mode);
    rex::test::KernelFixture fixture(Configure);
    Prepare(fixture);
    progress_calls = 0; change_calls = 0; progress_abi_valid = true; progress_mode = mode;
    fixture.RunOnGuestThread([&](rex::test::Caller& c) {
      const auto item = GuestContent(c), device = c.Alloc(4);
      c.Store32(device, 7);
      const auto progress = rex::kernel::GetFunctionDispatcher(c.GetRuntime()).AllocateThunk(&Progress, 0);
      const auto changed = rex::kernel::GetFunctionDispatcher(c.GetRuntime()).AllocateThunk(&Changed, 0);
      REQUIRE(progress); REQUIRE(changed);
      c.XAM("XamContentRegisterChangeCallback", {changed});
      c.GetContext().r31.u64 = 0x123456789ABCDEF0;
      const auto result = c.XAM("XamContentCopyInternal", {0, device, item, progress, 0x87654321, 0, 0});
      CHECK(result == (mode == 1 ? rex::kernel::X_ERROR_CANCELLED : 0));
      CHECK(c.GetContext().r31.u64 == 0x123456789ABCDEF0);
      CHECK(progress_abi_valid.load()); CHECK(progress_context.load() == 0x87654321);
      if (mode != 0) CHECK(progress_calls.load() == 1);
      else CHECK(progress_calls.load() > 1);
      CHECK(change_calls.load() == (mode == 1 ? 0 : 1));
      if (mode != 1) {
        CHECK(change_reason.load() == 0); CHECK(change_device.load() == 7);
        auto destination = Content(7);
        std::memcpy(c.GetBase() + item, &destination, sizeof(destination));
        REQUIRE(c.XAM("XamContentDeleteInternal", {item, 0}) == 0);
        CHECK(change_calls.load() == 2); CHECK(change_reason.load() == 1); CHECK(change_device.load() == 7);
      }
      c.XAM("XamContentRegisterChangeCallback", {0});
    });
    CheckPayload(rex::xam::CurrentXAM().GetContentManager(), 1);
    CHECK_FALSE(rex::xam::CurrentXAM().GetContentManager().HasContent(0, Content(7)));
  }
}

TEST_CASE("Guest transfer service retains pending ownership until cancellation and completion", "[sdk][xam][storage-transfer]") {
  rex::test::KernelFixture fixture(Configure);
  Prepare(fixture);
  progress_entered = false; release_progress = false; progress_mode = 42;
  progress_calls = 0; progress_abi_valid = true;
  fixture.RunOnGuestThread([&](rex::test::Caller& c) {
    const auto packet = c.Alloc(552), overlap = c.Alloc(sizeof(rex::kernel::XOVERLAPPED));
    const auto item = Content();
    std::memcpy(c.GetBase() + packet + 16, &item, sizeof(item));
    c.Store32(packet + 8, 7); c.Store32(packet + 528, overlap); c.Store32(packet + 532, 2);
    const auto progress = rex::kernel::GetFunctionDispatcher(c.GetRuntime()).AllocateThunk(&Progress, 0);
    REQUIRE(progress);
    c.Store32(packet + 536, progress); c.Store32(packet + 540, 0xFEDCBA98);
    const auto requester = rex::xam::CurrentTitle().threads.GetCallingThread()->GetHandle();
    rex::ScopeGuard release([&] { release_progress = true; });
    REQUIRE(c.XAM("XMsgStartIORequestEx", {0xFE, 0x2000C, overlap, packet, 552, 0}) == 0);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!progress_entered && std::chrono::steady_clock::now() < deadline)
      rex::xam::CurrentTitle().threads.SleepCurrentThread(std::chrono::milliseconds(1));
    REQUIRE(progress_entered.load());
    CHECK(c.XAM("XGetOverlappedResult", {overlap, 0, 0}) == rex::kernel::X_ERROR_IO_INCOMPLETE);
    CHECK(c.Load32(overlap + 8) == requester);
    CHECK(c.XAM("XMsgStartIORequestEx", {0xFE, 0x2000C, overlap, packet, 552, 0}) == 0);
    CHECK(progress_calls.load() == 1);
    REQUIRE(c.XAM("XMsgCancelIORequest", {overlap, 0}) == 0);
    release_progress = true;
    REQUIRE(c.XAM("XMsgCancelIORequest", {overlap, 1}) == 0);
    CHECK(c.XAM("XGetOverlappedResult", {overlap, 0, 0}) == rex::kernel::X_ERROR_CANCELLED);
    CHECK(c.XAM("XGetOverlappedExtendedError", {overlap}) == 0x800704C7);
    CHECK(c.Load32(overlap + 8) == requester);
    CHECK(progress_abi_valid.load()); CHECK(progress_context.load() == 0xFEDCBA98);
    // Reuse is legal only after the previous worker has published completion.
    progress_mode = 3;
    change_calls = 0;
    const auto changed = rex::kernel::GetFunctionDispatcher(c.GetRuntime()).AllocateThunk(&Changed, 0);
    REQUIRE(changed);
    c.XAM("XamContentRegisterChangeCallback", {changed});
    const auto device = c.Alloc(4); c.Store32(device, 7);
    REQUIRE(c.XAM("XamContentMoveInternal", {0, device, packet + 16, progress, 1, 0, overlap}) == rex::kernel::X_ERROR_IO_PENDING);
    const auto copied = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (c.XAM("XGetOverlappedResult", {overlap, 0, 0}) == rex::kernel::X_ERROR_IO_INCOMPLETE &&
           std::chrono::steady_clock::now() < copied)
      rex::xam::CurrentTitle().threads.SleepCurrentThread(std::chrono::milliseconds(1));
    CHECK(c.XAM("XGetOverlappedResult", {overlap, 0, 0}) == 0);
    CHECK(change_calls.load() == 2); CHECK(change_reason.load() == 1); CHECK(change_device.load() == 1);
    c.XAM("XamContentRegisterChangeCallback", {0});
  });
  CHECK_FALSE(rex::xam::CurrentXAM().GetContentManager().HasContent(0, Content(1)));
  CheckPayload(rex::xam::CurrentXAM().GetContentManager(), 7);
}

TEST_CASE("Guest transfer rejects invalid packets and unsupported cancellation without touching data", "[sdk][xam][storage-transfer]") {
  rex::test::KernelFixture fixture(Configure);
  Prepare(fixture);
  fixture.RunOnGuestThread([&](rex::test::Caller& c) {
    const auto item = GuestContent(c), device = c.Alloc(4), overlap = c.Alloc(sizeof(rex::kernel::XOVERLAPPED));
    c.Store32(device, 7); c.Store32(overlap, rex::kernel::X_ERROR_IO_PENDING);
    CHECK(c.XAM("XamContentCopyInternal", {0, 0, item, 0, 0, 0, 0}) == rex::kernel::X_ERROR_INVALID_PARAMETER);
    CHECK(c.XAM("XamContentMoveInternal", {0, device, 0xFFFFFFF0, 0, 0, 0, 0}) == rex::kernel::X_ERROR_INVALID_PARAMETER);
    CHECK(c.XAM("XMsgCancelIORequest", {overlap, 0}) == 50);
    CHECK(c.Load32(overlap) == rex::kernel::X_ERROR_IO_PENDING);
    CHECK(c.XAM("XMsgCancelIORequest", {0xFFFFFFF0, 0}) == rex::kernel::X_ERROR_INVALID_PARAMETER);
    const auto packet = c.Alloc(552);
    std::memcpy(c.GetBase() + packet + 16, c.GetBase() + item, 512);
    c.Store32(packet + 8, 7); c.Store32(packet + 532, 2);
    CHECK(c.XAM("XMsgStartIORequestEx", {0xFE, 0x2000C, overlap, packet, 552, 0}) == rex::kernel::X_E_INVALIDARG);
    c.Store32(packet + 528, overlap); c.Store32(packet + 544, 1);
    CHECK(c.XAM("XMsgStartIORequestEx", {0xFE, 0x2000C, overlap, packet, 552, 0}) == 0x80070032);
    c.Store32(packet + 544, 0); c.Store32(packet + 532, 8);
    CHECK(c.XAM("XMsgStartIORequestEx", {0xFE, 0x2000C, overlap, packet, 552, 0}) == rex::kernel::X_E_INVALIDARG);
    CHECK(c.Load32(overlap) == rex::kernel::X_ERROR_IO_PENDING);
    CHECK(c.XAM("XamContentDeleteInternal", {0xFFFFFFF0, 0}) == rex::kernel::X_ERROR_INVALID_PARAMETER);
  });
  CheckPayload(rex::xam::CurrentXAM().GetContentManager(), 1);
  CHECK_FALSE(rex::xam::CurrentXAM().GetContentManager().HasContent(0, Content(7)));
}

TEST_CASE("Local save and profile metadata describe persisted bytes and reject mismatched identity", "[sdk][xam][storage-transfer]") {
  for (const auto type : {rex::kernel::XContentType::kProfile, rex::kernel::XContentType::kSavedGame}) {
    rex::test::KernelFixture fixture(Configure);
    auto& manager = rex::xam::CurrentXAM().GetContentManager();
    auto data = Content(7); data.content_type = type; data.SetDisplayName(u"Local \u00e7\u00e3o");
    REQUIRE(manager.CreateContent("metadata", 0, data));
    auto opened = fixture.GetRuntime().GetFileSystem().Open("metadata:\\payload",
        {.disposition = rex::filesystem::Disposition::kCreate, .access = rex::filesystem::Access::kWriteAccess});
    REQUIRE(opened); REQUIRE(opened->file->Write(Bytes(), 0)); opened->file.reset();
    REQUIRE(manager.CloseContent("metadata"));
    const std::vector<rex::u8> thumbnail{0x89, 'P', 'N', 'G'};
    REQUIRE(manager.SetContentThumbnail(0, data, thumbnail));
    std::vector<rex::u8> output(sizeof(rex::filesystem::XContentMetadata), 0xA5);
    REQUIRE(manager.GetContentMetadata(data, output.data()) == 0);
    const auto& metadata = *reinterpret_cast<const rex::filesystem::XContentMetadata*>(output.data());
    CHECK(metadata.content_type == type); CHECK(metadata.metadata_version == 2);
    CHECK(metadata.content_size == Bytes().size() + thumbnail.size());
    CHECK(metadata.execution_info.title_id == kTitle); CHECK(metadata.execution_info.version_value == 0);
    CHECK(metadata.profile_id == 0); CHECK(metadata.flags.as_byte == 0x40);
    CHECK(metadata.GetDisplayName(rex::kernel::XLanguage::kEnglish) == u"Local \u00e7\u00e3o");
    CHECK(metadata.thumbnail_size == thumbnail.size());
    CHECK(std::equal(thumbnail.begin(), thumbnail.end(), metadata.thumbnail));
    fixture.RunOnGuestThread([&](rex::test::Caller& c) {
      const auto item = c.Alloc(sizeof(data)), result = c.Alloc(static_cast<uint32_t>(output.size()));
      std::memcpy(c.GetBase() + item, &data, sizeof(data));
      REQUIRE(c.XAM("XamContentGetMetaDataInternal", {item, result, 0}) == 0);
      CHECK(std::equal(output.begin(), output.end(), c.GetBase() + result));
    });
    const auto header_path = manager.DeviceRoot(7) / "0000000000000000" / "584108B7" /
        "Headers" / std::format("{:08X}", static_cast<uint32_t>(type)) / "transfer_source.header";
    auto wrong = data; wrong.title_id = kTitle ^ 1;
    REQUIRE(rex::platform::WriteHostFile(header_path,
        std::span<const rex::u8>(reinterpret_cast<const rex::u8*>(&wrong), sizeof(wrong)), false));
    std::fill(output.begin(), output.end(), 0xA5);
    CHECK(manager.GetContentMetadata(data, output.data()) == 13);
    CHECK(std::all_of(output.begin(), output.end(), [](uint8_t b) { return b == 0xA5; }));
    REQUIRE(manager.WriteContentHeaderFile(0, data));
    REQUIRE(manager.SetContentThumbnail(0, data, std::vector<rex::u8>(rex::filesystem::XContentMetadata::kThumbLengthV2 + 1)));
    const auto oversized_result = manager.GetContentMetadata(data, output.data());
    if (oversized_result != 13) {
      CAPTURE(static_cast<uint32_t>(type));
      const auto volume = manager.GetDevice(7);
      INFO("volume accessible=" << bool(volume));
      const auto stored = manager.ReadContentHeaderFile(data.GetFileName(), 0, kTitle, type, 7);
      INFO("header readable=" << bool(stored));
      std::array<rex::u8, 36> attrs{};
      INFO("attributes status=" << manager.GetContentAttributes(data, attrs.data()));
      const auto picture = manager.GetContentThumbnail(0, data);
      INFO("thumbnail readable=" << bool(picture) << "; bytes=" << (picture ? picture->size() : 0));
      CHECK(oversized_result == 13);
    }
    CHECK(oversized_result == 13);
    CHECK(std::all_of(output.begin(), output.end(), [](uint8_t b) { return b == 0xA5; }));
    #if defined(_WIN32)
    const auto thumbnail_path = manager.DeviceRoot(7) / "0000000000000000" / "584108B7" /
        std::format("{:08X}", static_cast<uint32_t>(type)) / "transfer_source" / "__thumbnail.png";
    const auto exclusive = CreateFileW(thumbnail_path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(exclusive != INVALID_HANDLE_VALUE);
    rex::ScopeGuard close_exclusive([&] { CloseHandle(exclusive); });
    // Known oversized metadata is rejected before opening/allocating its payload.
    CHECK(manager.GetContentMetadata(data, output.data()) == 13);
    CHECK(std::all_of(output.begin(), output.end(), [](uint8_t b) { return b == 0xA5; }));
    #endif
  }
}

TEST_CASE("Content cancellation after copying bytes publishes no incomplete destination", "[sdk][storage-transfer]") {
  rex::test::KernelFixture fixture(Configure);
  Prepare(fixture);
  auto& manager = rex::xam::CurrentXAM().GetContentManager();
  bool committed = true, removed = true, copied = false;
  const auto result = manager.TransferContent(0, Content(), 7, 0, true,
      [&](uint64_t done, uint64_t, bool initial) { if (!initial && done) copied = true; return initial || !done; },
      &committed, {}, &removed);
  CHECK(result == rex::kernel::X_ERROR_CANCELLED);
  CHECK(copied); CHECK_FALSE(committed); CHECK_FALSE(removed);
  CHECK_FALSE(manager.HasContent(0, Content(7)));
  CheckPayload(manager, 1);
}

TEST_CASE("Content transfer respects quota consumed by another mounted writer after its initial snapshot", "[sdk][storage-transfer]") {
  rex::test::KernelFixture fixture(Configure);
  Prepare(fixture);
  auto& manager = rex::xam::CurrentXAM().GetContentManager();
  auto other = Content(7); other.SetFileName("other_writer");
  const std::vector<rex::u8> filler(1048576 - sizeof(Data) - 4096, 0x6B);
  bool competing_write = false, committed = true;
  const auto result = manager.TransferContent(0, Content(), 7, 0, false,
      [&](uint64_t, uint64_t, bool initial) {
        if (!initial) return true;
        REQUIRE(manager.CreateContent("competitor", 0, other));
        auto file = fixture.GetRuntime().GetFileSystem().Open("competitor:\\payload",
            {.disposition = rex::filesystem::Disposition::kCreate, .access = rex::filesystem::Access::kWriteAccess});
        REQUIRE(file); REQUIRE(file->file->Write(filler, 0)); file->file.reset();
        REQUIRE(manager.CloseContent("competitor"));
        competing_write = true;
        return true;
      }, &committed);
  CHECK(competing_write); CHECK(result == 112); CHECK_FALSE(committed);
  CHECK_FALSE(manager.HasContent(0, Content(7))); CHECK(manager.HasContent(0, other));
  const auto path = manager.DeviceRoot(7) / "0000000000000000" / "584108B7" / "00000002" / "other_writer" / "payload";
  const auto bytes = rex::platform::ReadHostFile(path);
  REQUIRE(bytes); CHECK(*bytes == filler);
  const auto volume = manager.GetDevice(7);
  REQUIRE(volume); CHECK(volume->free_bytes == 4096);
  CheckPayload(manager, 1);
}

TEST_CASE("Retiring a closed save releases revoked file ownership before moving its directory", "[sdk][storage-transfer]") {
  rex::test::KernelFixture fixture(Configure);
  auto& manager = rex::xam::CurrentXAM().GetContentManager();
  auto data = Content(); data.content_type = rex::kernel::XContentType::kSavedGame;
  REQUIRE(manager.CreateContent("retirement", 0, data));
  auto opened = fixture.GetRuntime().GetFileSystem().Open("retirement:\\payload",
      {.disposition = rex::filesystem::Disposition::kCreate, .access = rex::filesystem::Access::kWriteAccess});
  REQUIRE(opened); REQUIRE(opened->file->Write(Bytes(), 0));
  REQUIRE(manager.CloseContent("retirement"));
  CHECK_FALSE(opened->file->IsAccessible());
  bool removed = false;
  int last_stage = -1;
  const auto result = manager.DeleteContent(0, data, &removed, [&](Stage stage) { last_stage = static_cast<int>(stage); });
  INFO("last retirement stage=" << last_stage << "; removed=" << removed);
  CHECK(result == 0); CHECK(removed); CHECK_FALSE(manager.HasContent(0, data));
  CHECK_FALSE(opened->file->Write(Bytes(), 0));
}

TEST_CASE("Revoking storage I/O preserves bytes already borrowed through a mapped view", "[sdk][storage-transfer]") {
  rex::test::KernelFixture fixture(Configure);
  auto data = Content(); data.content_type = rex::kernel::XContentType::kSavedGame;
  Prepare(fixture, data);
  auto& manager = rex::xam::CurrentXAM().GetContentManager();
  REQUIRE(manager.OpenContent("borrowed", 0, data));
  auto opened = fixture.GetRuntime().GetFileSystem().Open("borrowed:\\payload",
      {.disposition = rex::filesystem::Disposition::kOpen, .access = rex::filesystem::Access::kReadAccess});
  REQUIRE(opened);
  const auto view = opened->file->View();
  REQUIRE(view); REQUIRE(view->size() == Bytes().size());
  REQUIRE(manager.CloseContent("borrowed"));
  CHECK_FALSE(opened->file->IsAccessible());
  CHECK_FALSE(opened->file->View());
  const auto expected = Bytes();
  CHECK(std::equal(expected.begin(), expected.end(), view->begin()));
}

TEST_CASE("Profile packages preserve settings, history and unlocks through copy, committed move failure and restart", "[sdk][xam][storage-transfer][profile-packages]") {
  rex::test::TempFolder persistent("rex_profile_package_restart_");
  rex::u64 xuid = 0;
  std::map<std::string, std::vector<rex::u8>> original;
  {
    rex::test::KernelFixture fixture(Configure, {}, {}, {}, {}, persistent.path);
    auto& profile = rex::xam::CurrentXAM().GetUserProfile();
    auto& manager = rex::xam::CurrentXAM().GetContentManager();
    REQUIRE(profile.CreateLocal("Owner", &xuid) == 0);
    REQUIRE(profile.SetLocalSession(xuid) == 0);
    REQUIRE(profile.AddSetting(std::make_unique<rex::xam::UserProfile::Int32Setting>(0x10040015, 2)));
    REQUIRE(profile.RecordTitlePlayed(kTitle));
    auto& achievements = rex::xam::CurrentXAM().GetOfflineAchievements();
    const auto metadata = offline_achievement_fixture::Metadata();
    REQUIRE(achievements.ImportTitle(kTitle, metadata) == rex::xam::OfflineAchievementResult::kSuccess);
    std::vector<rex::xam::OfflineAchievementRecord> unlocked;
    const std::array<rex::u32, 1> ids{1};
    REQUIRE(achievements.UnlockBatch(xuid, kTitle, ids, unlocked) == rex::xam::OfflineAchievementResult::kSuccess);
    REQUIRE(unlocked.size() == 1);
    for (const auto* name : {"account.dat", "settings-v1/10040015.dat", "titles-played.dat", "achievements.dat"}) {
      CAPTURE(name);
      const auto bytes = rex::platform::ReadHostFile(profile.PackagePath(xuid) / std::filesystem::path(name).make_preferred());
      REQUIRE(bytes); original.emplace(name, *bytes);
    }
    Data data{}; data.device_id = 1; data.xuid = xuid; data.title_id = 0xFFFE07D1;
    data.content_type = rex::kernel::XContentType::kProfile;
    data.SetFileName(std::format("{:016X}", xuid)); data.SetDisplayName(u"Owner");
    bool committed = true, removed = true;
    CHECK(manager.TransferContent(xuid, data, 7, xuid, true, {}, &committed, {}, &removed) == rex::kernel::X_ERROR_ACCESS_DENIED);
    CHECK_FALSE(committed); CHECK_FALSE(removed);
    REQUIRE(manager.TransferContent(xuid, data, 7, xuid, false) == 0);
    const auto destination = manager.DeviceRoot(7) / std::format("{:016X}", xuid) / "FFFE07D1" / "00010000" / std::format("{:016X}", xuid);
    for (const auto& [name, before] : original) {
      const auto bytes = rex::platform::ReadHostFile(destination / std::filesystem::path(name).make_preferred());
      REQUIRE(bytes); CHECK(*bytes == before);
    }
    CHECK(profile.identity().device_id == 1); CHECK(profile.GetSigninState() == 1);
    auto copy = data; copy.device_id = 7;
    REQUIRE(manager.DeleteContent(xuid, copy));
    REQUIRE(profile.SetLocalSession(0) == 0);
    const auto result = manager.TransferContent(xuid, data, 7, xuid, true, {}, &committed,
        [](Stage stage) { if (stage == Stage::SourceRetired) Interrupt(); }, &removed);
    CHECK(result == rex::kernel::X_ERROR_ACCESS_DENIED); CHECK(committed); CHECK(removed);
    CHECK(profile.FindLocal(xuid).device_id == 7);
    CHECK(profile.identity().device_id == 7); CHECK(profile.GetSigninState() == 0);
    for (const auto& [name, before] : original) {
      const auto bytes = rex::platform::ReadHostFile(profile.PackagePath(xuid) / std::filesystem::path(name).make_preferred());
      REQUIRE(bytes); CHECK(*bytes == before);
    }
    REQUIRE(profile.SetLocalSession(xuid) == 0);
    const auto* setting = profile.GetSetting(0x10040015);
    REQUIRE(setting); CHECK(static_cast<const rex::xam::UserProfile::Int32Setting*>(setting)->value == 2);
    rex::xam::OfflineAchievementTotals totals{};
    REQUIRE(achievements.Totals(xuid, totals) == rex::xam::OfflineAchievementResult::kSuccess);
    CHECK(totals.gamerscore == 10); CHECK(totals.unlocked_count == 1);
    CHECK(profile.TitlesPlayed(xuid) == std::vector<rex::u32>{kTitle});
  }
  {
    rex::test::KernelFixture reopened({}, {}, {}, {}, {}, persistent.path);
    auto& profile = rex::xam::CurrentXAM().GetUserProfile();
    REQUIRE(profile.GetXUID() == xuid); CHECK(profile.GetSigninState() == 1);
    CHECK(profile.identity().device_id == 7); CHECK(profile.GetName() == "Owner");
    for (const auto& [name, before] : original) {
      const auto bytes = rex::platform::ReadHostFile(profile.PackagePath(xuid) / std::filesystem::path(name).make_preferred());
      REQUIRE(bytes); CHECK(*bytes == before);
    }
    CHECK(profile.TitlesPlayed(xuid) == std::vector<rex::u32>{kTitle});
    const auto* setting = profile.GetSetting(0x10040015);
    REQUIRE(setting); CHECK(static_cast<const rex::xam::UserProfile::Int32Setting*>(setting)->value == 2);
    rex::xam::OfflineAchievementTotals totals{};
    REQUIRE(rex::xam::CurrentXAM().GetOfflineAchievements().Totals(xuid, totals) == rex::xam::OfflineAchievementResult::kSuccess);
    CHECK(totals.gamerscore == 10); CHECK(totals.unlocked_count == 1);
  }
}

TEST_CASE("Move failure after destination publication preserves both commit ownership and valid data", "[sdk][storage-transfer]") {
  rex::test::KernelFixture fixture(Configure);
  Prepare(fixture);
  auto& manager = rex::xam::CurrentXAM().GetContentManager();
  bool committed = false, removed = true;
  CHECK(manager.TransferContent(0, Content(), 7, 0, true, {}, &committed,
      [](Stage stage) { if (stage == Stage::HeaderPublished) Interrupt(); }, &removed) == rex::kernel::X_ERROR_ACCESS_DENIED);
  CHECK(committed); CHECK_FALSE(removed);
  CheckPayload(manager, 1); CheckPayload(manager, 7);
  CHECK(manager.TransferContent(0, Content(), 7, 0, true) == rex::kernel::X_ERROR_ALREADY_EXISTS);
  CheckPayload(manager, 1); CheckPayload(manager, 7);
}

TEST_CASE("A fresh runtime recovers a published package and its staged complete header", "[sdk][storage-transfer]") {
  rex::test::TempFolder persistent("storage_restart_");
  {
    rex::test::KernelFixture fixture(Configure, {}, {}, {}, {}, persistent.path);
    Prepare(fixture);
    auto& manager = rex::xam::CurrentXAM().GetContentManager();
    bool committed = true;
    CHECK(manager.TransferContent(0, Content(), 7, 0, false, {}, &committed,
        [](Stage stage) { if (stage == Stage::PackagePublished) Interrupt(); }) == rex::kernel::X_ERROR_ACCESS_DENIED);
    CHECK_FALSE(committed);
  }
  {
    rex::test::KernelFixture fixture({}, {}, {}, {}, {}, persistent.path);
    auto& manager = rex::xam::CurrentXAM().GetContentManager();
    REQUIRE(manager.GetDevice(7));
    CheckPayload(manager, 1); CheckPayload(manager, 7);
    const auto header = manager.ReadContentHeaderFile("transfer_source", 0, kTitle, Content().content_type, 7);
    REQUIRE(header); CHECK(header->device_id == 7);
  }
}

TEST_CASE("Source retirement restores uncommitted names and cleans only committed private data", "[sdk][storage-transfer]") {
  for (const auto interruption : {Stage::SourcePrepared, Stage::SourcePackageRetired, Stage::SourceHeaderRetired, Stage::SourceRetired}) {
    INFO(static_cast<unsigned>(interruption));
    rex::test::TempFolder persistent("storage_retirement_");
    {
      rex::test::KernelFixture fixture(Configure, {}, {}, {}, {}, persistent.path);
      Prepare(fixture);
      auto& manager = rex::xam::CurrentXAM().GetContentManager();
      bool committed = false, removed = false;
      CHECK(manager.TransferContent(0, Content(), 7, 0, true, {}, &committed,
          [&](Stage stage) { if (stage == interruption) Interrupt(); }, &removed) == rex::kernel::X_ERROR_ACCESS_DENIED);
      CHECK(committed); CHECK(removed == (interruption == Stage::SourceRetired));
    }
    {
      rex::test::KernelFixture fixture({}, {}, {}, {}, {}, persistent.path);
      auto& manager = rex::xam::CurrentXAM().GetContentManager();
      REQUIRE(manager.GetDevice(1)); REQUIRE(manager.GetDevice(7));
      CHECK(manager.HasContent(0, Content()) == (interruption != Stage::SourceRetired));
      if (interruption != Stage::SourceRetired) CheckPayload(manager, 1);
      CheckPayload(manager, 7);
    }
  }
}

TEST_CASE("Storage process exits abruptly at the selected commit boundary", "[.][storage-crash-child]") {
  const auto [root, interruption] = CrashInput();
  rex::test::KernelFixture fixture(Configure, {}, {}, {}, {}, root);
  Prepare(fixture);
  auto& manager = rex::xam::CurrentXAM().GetContentManager();
  if (DeleteCrash()) {
    (void)manager.DeleteContent(0, Content(), nullptr,
        [&](Stage stage) { if (static_cast<unsigned>(stage) == interruption) std::_Exit(71); });
    FAIL("the selected deletion boundary was never reached");
  }
  (void)manager.TransferContent(0, Content(), 7, 0, true,
      [&](uint64_t done, uint64_t, bool initial) { if (interruption == 7 && !initial && done) std::_Exit(71); return true; }, nullptr,
      [&](Stage stage) { if (static_cast<unsigned>(stage) == interruption) std::_Exit(71); });
  FAIL("the selected commit boundary was never reached");
}

TEST_CASE("Storage reopens and verifies the data left by an abruptly terminated process", "[.][storage-crash-verify]") {
  const auto [root, interruption] = CrashInput();
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, root);
  auto& manager = rex::xam::CurrentXAM().GetContentManager();
  const bool deletion = DeleteCrash();
  REQUIRE(manager.GetDevice(1)); REQUIRE(manager.GetDevice(7));
  CHECK(manager.HasContent(0, Content()) == (interruption != static_cast<unsigned>(Stage::SourceRetired)));
  if (interruption != static_cast<unsigned>(Stage::SourceRetired)) CheckPayload(manager, 1);
  const auto source_header = manager.ReadContentHeaderFile("transfer_source", 0, kTitle, Content().content_type, 1);
  CHECK(bool(source_header) == (interruption != static_cast<unsigned>(Stage::SourceRetired)));
  CHECK(manager.HasContent(0, Content(7)) == (!deletion && interruption != 7));
  if (!deletion && interruption != 7) {
    CheckPayload(manager, 7);
    const auto header = manager.ReadContentHeaderFile("transfer_source", 0, kTitle, Content().content_type, 7);
    REQUIRE(header); CHECK(header->device_id == 7);
  }
}

TEST_CASE("Direct deletion shares recoverable retirement and protects open content", "[sdk][storage-transfer]") {
  rex::test::KernelFixture fixture(Configure);
  Prepare(fixture);
  auto& manager = rex::xam::CurrentXAM().GetContentManager();
  REQUIRE(manager.OpenContent("held", 0, Content()));
  bool removed = true;
  CHECK(manager.DeleteContent(0, Content(), &removed) == rex::kernel::X_ERROR_ACCESS_DENIED);
  CHECK_FALSE(removed); CheckPayload(manager, 1);
  REQUIRE(manager.CloseContent("held"));
  CHECK(manager.DeleteContent(0, Content(), &removed) == 0);
  CHECK(removed); CHECK_FALSE(manager.HasContent(0, Content()));
  CHECK_FALSE(manager.ReadContentHeaderFile("transfer_source", 0, kTitle, Content().content_type, 1));
  removed = true;
  CHECK(manager.DeleteContent(0, Content(), &removed) == rex::kernel::X_ERROR_FILE_NOT_FOUND);
  CHECK_FALSE(removed);
}

TEST_CASE("Recovery rejects a corrupt or conflicting intent without deleting either side", "[.][storage-crash-reject]") {
  const auto [root, interruption] = CrashInput();
  REQUIRE(interruption == 0);
  rex::test::KernelFixture fixture({}, {}, {}, {}, {}, root);
  auto& manager = rex::xam::CurrentXAM().GetContentManager();
  REQUIRE(manager.GetDevice(1));
  CHECK_FALSE(manager.GetDevice(7));
  CHECK_FALSE(manager.GetDevice(7));
  CheckPayload(manager, 1);
}
