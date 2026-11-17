#include <format>
#include <catch2/catch_test_macros.hpp>
#include "kernel/kernel_fixture.h"
#include "kernel/test_access.h"
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/heap.h>
#include <rex/kernel/types/io.h>
#include <rex/ppc/recompiled_code.h>
#include <rex/logging/logger.h>
#include <rex/xam/user_profile.h>
#include "xam/xam_state.h"
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <atomic>
#include <sstream>

extern "C" REX_FUNC(__imp__XamUserWriteProfileSettings);
extern "C" REX_FUNC(__imp__XamUserReadProfileSettings);
using Setting = rex::xam::X_USER_PROFILE_SETTING;
using Type = rex::xam::UserProfile::Setting::Type;
using namespace rex::kernel;

namespace {
struct SettingsFixture {
  std::filesystem::path root = std::filesystem::current_path() /
      ("x360n-profile-settings-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::unique_ptr<rex::test::KernelFixture> fixture;
  rex::kernel::TitleRuntime* runtime = nullptr;
  uint32_t block = 0;
  SettingsFixture() { std::filesystem::create_directory(root); Open(); }
  ~SettingsFixture() { fixture.reset(); std::error_code ec; std::filesystem::remove_all(root, ec); }
  void Open() {
    fixture.reset();
    fixture = std::make_unique<rex::test::KernelFixture>(nullptr, std::vector<rex::IModule*>{}, "", "", "", root);
    runtime = &fixture->GetRuntime();
    block = runtime->GetGuestHeap().SystemHeapAlloc(16384); REQUIRE(block);
    std::memset(base() + block, 0, 16384);
  }
  uint8_t* base() { return runtime->GetMembase().GetBase(); }
  auto* profile() { return &rex::xam::CurrentXAM().GetUserProfile(); }
  uint32_t Write(const Setting* settings, size_t count, uint32_t overlapped = 0) {
    std::memcpy(base() + block, settings, count * sizeof(Setting));
    uint32_t result = ~0u;
    fixture->RunOnGuestThread([&](rex::test::Caller& caller) {
    auto& ctx = caller.GetContext(); ctx.r3.u64 = 0; ctx.r4.u64 = 0; ctx.r5.u64 = count; ctx.r6.u64 = block; ctx.r7.u64 = overlapped;
    __imp__XamUserWriteProfileSettings(ctx, base()); result=ctx.r3.u32;
    }); return result;
  }
  uint32_t Read(const std::vector<uint32_t>& ids) {
    for (size_t i = 0; i < ids.size(); ++i)
      rex::memory::store_and_swap<uint32_t>(base() + block + 4096 + i * 4, ids[i]);
    rex::memory::store_and_swap<uint32_t>(base() + block + 4300, 4096);
    uint32_t result = ~0u;
    fixture->RunOnGuestThread([&](rex::test::Caller& caller) {
    auto& ctx = caller.GetContext();
    ctx.r3.u64=ctx.r4.u64=ctx.r5.u64=ctx.r6.u64=0; ctx.r1.u64 = block + 12000;
    ctx.r7.u64 = ids.size(); ctx.r8.u64 = block + 4096;
    ctx.r9.u64 = block + 4300; ctx.r10.u64 = block + 4400;
    rex::memory::store_and_swap<uint32_t>(base() + ctx.r1.u32 + 0x54, 0);
    __imp__XamUserReadProfileSettings(ctx, base()); result=ctx.r3.u32;
    }); return result;
  }
  Setting* Row(size_t i) { return reinterpret_cast<Setting*>(base() + block + 4408 + i * sizeof(Setting)); }
  std::u16string Text(size_t i) {
    std::u16string result;
    const uint32_t address = Row(i)->data.unicode.ptr, size = Row(i)->data.unicode.size;
    for (uint32_t j = 0; address && j + 2 < size; j += 2)
      result.push_back(rex::memory::load_and_swap<uint16_t>(base() + address + j));
    return result;
  }
  std::filesystem::path ValuePath(uint64_t xuid, uint32_t id) {
    // Profile settings live in the profile package on the hard drive.
    const auto name = std::format("{:016X}", xuid);
    return root / name / "FFFE07D1" / "00010000" / name / "settings-v1" / std::format("{:08X}.dat", id);
  }
};
Setting Scalar(uint32_t id, Type type) { Setting s{}; s.setting_id = id; s.data.type = uint8_t(type); return s; }
}

TEST_CASE("Profile setting types persist through the real write and read ABI", "[sdk][profile-settings][profile-settings-state]") {
  SettingsFixture f;
  REQUIRE(f.profile()->CreateLocal("Settings") == 0);
  REQUIRE(f.profile()->SetLocalSession(f.profile()->GetXUID()) == 0);
  const std::vector<uint32_t> ids{0x10040002, 0x20080051, 0x30080052, 0x402C0011, 0x50040055, 0x60800054, 0x70080053};
  std::array<Setting, 7> values{};
  for (size_t i = 0; i < values.size(); ++i) values[i] = Scalar(ids[i], Type(ids[i] >> 28));
  values[0].data.s32 = 1; values[1].data.s64 = 0x123456789ABCDEFLL;
  values[2].data.f64 = 12.5; values[4].data.f32 = 0.75f;
  values[6].data.filetime = 0x01DA000012345678ULL;
  const std::u16string text = u"Ol\u00E1";
  for (size_t i = 0; i <= text.size(); ++i)
    rex::memory::store_and_swap<uint16_t>(f.base() + f.block + 1024 + i * 2, i == text.size() ? 0 : text[i]);
  values[3].data.unicode.ptr = f.block + 1024; values[3].data.unicode.size = uint32_t((text.size() + 1) * 2);
  std::memcpy(f.base() + f.block + 1100, "real", 4);
  values[5].data.binary.ptr = f.block + 1100; values[5].data.binary.size = 4;
  REQUIRE(f.Write(values.data(), values.size()) == 0);
  auto verify = [&] {
    REQUIRE(f.Read(ids) == 0);
    CHECK(f.Row(0)->data.s32 == 1); CHECK(f.Row(1)->data.s64 == 0x123456789ABCDEFLL);
    CHECK(f.Row(2)->data.f64 == 12.5); CHECK(f.Text(3) == text);
    CHECK(f.Row(4)->data.f32 == 0.75f); CHECK(f.Row(6)->data.filetime == 0x01DA000012345678ULL);
    CHECK(f.Row(5)->data.binary.size == 4);
    CHECK(std::memcmp(f.base() + uint32_t(f.Row(5)->data.binary.ptr), "real", 4) == 0);
  };
  verify();
  const auto xuid = f.profile()->GetXUID();
  REQUIRE(f.profile()->RenameLocal(xuid, "Renamed") == 0);
  f.Open(); CHECK(f.profile()->GetXUID() == xuid); CHECK(f.profile()->GetName() == "Renamed");
  verify();
}

TEST_CASE("Profile settings sizing and async completion preserve a large mixed batch header",
          "[sdk][profile-settings][profile-settings-large-batch]") {
  SettingsFixture f;
  REQUIRE(f.profile()->CreateLocal("Batch") == 0);
  REQUIRE(f.profile()->SetLocalSession(f.profile()->GetXUID()) == 0);
  const std::array<uint32_t, 29> ids{
      0x10040013, 0x10040006, 0x402C0011, 0x10040005, 0x5004000B, 0x4064000F,
      0x10040012, 0x10040004, 0x1004000C, 0x1004000D, 0x1004000E, 0x10040018,
      0x10040015, 0x1004001D, 0x1004001E, 0x10040022, 0x10040023, 0x10040024,
      0x10040026, 0x10040027, 0x10040028, 0x10040029, 0x10040039, 0x10040038,
      0x10040003, 0x63E83FFF, 0x63E83FFE, 0x63E83FFD, 0x10040002};
  for (size_t i = 0; i < ids.size(); ++i)
    rex::memory::store_and_swap<uint32_t>(f.base() + f.block + i * 4, ids[i]);
  const auto size = f.block + 256, output = f.block + 512, io = f.block + 6000;
  auto read = [&](uint32_t buffer, bool async) {
    uint32_t result = ~0u;
    f.fixture->RunOnGuestThread([&](rex::test::Caller& caller) {
          auto& ctx=caller.GetContext();
    ctx.r3.u64=ctx.r4.u64=ctx.r5.u64=ctx.r6.u64=0;
          ctx.r1.u64 = f.block + 12000; ctx.r3.u64 = 0x12345678;
          ctx.r7.u64 = ids.size(); ctx.r8.u64 = f.block;
          ctx.r9.u64 = size; ctx.r10.u64 = buffer;
          rex::memory::store_and_swap<uint32_t>(f.base() + ctx.r1.u32 + 0x54, async ? io : 0);
          __imp__XamUserReadProfileSettings(ctx, f.base());
          result = ctx.r3.u32;
        });
    return result;
  };
  for (const bool async : {false, true}) {
    rex::memory::store_and_swap<uint32_t>(f.base() + size, 0);
    REQUIRE(read(0, async) == X_ERROR_INSUFFICIENT_BUFFER);
    const auto needed = rex::memory::load_and_swap<uint32_t>(f.base() + size);
    REQUIRE(needed == 4312);
    std::memset(f.base() + output, 0xA5, needed + 16);
    std::memset(f.base() + io, 0, sizeof(rex::kernel::XOVERLAPPED));
    REQUIRE(read(output, async) == (async ? X_ERROR_IO_PENDING : X_ERROR_SUCCESS));
    if (async) CHECK(rex::memory::load_and_swap<uint32_t>(f.base() + io) == X_ERROR_SUCCESS);
    CHECK(rex::memory::load_and_swap<uint32_t>(f.base() + output) == ids.size());
    CHECK(rex::memory::load_and_swap<uint32_t>(f.base() + output + 4) == output + 8);
    for (size_t i = 0; i < ids.size(); ++i) {
      const auto* row = reinterpret_cast<const Setting*>(f.base() + output + 8 + i * sizeof(Setting));
      CHECK(row->setting_id == ids[i]);
    }
    for (size_t i = 0; i < 16; ++i) CHECK(f.base()[output + needed + i] == 0xA5);
  }
}

TEST_CASE("Profile read debug diagnostics identify completed writes without changing results",
          "[sdk][profile-settings][profile-read-trace]") {
  SettingsFixture f;
  REQUIRE(f.profile()->CreateLocal("Trace") == 0);
  REQUIRE(f.profile()->SetLocalSession(f.profile()->GetXUID()) == 0);
  auto found = REX_SETTINGS().Find("log.categories.xam"); REQUIRE(found);
  auto* category = *found;
  const auto previous = category->GetText(); const auto layer = category->GetLayer();
  rex::ScopeGuard restore([&] {
    CHECK(category->Reset());
    if (layer != rex::SettingLayer::kDefault) CHECK(category->SetText(layer, previous));
  });
  rex::log::LogCaptureSink captured;
  auto sink = rex::GetLogger().AddSink(captured);
  auto after = captured.GetLatestSequence();
  const auto captured_text = [&] {
    std::vector<rex::log::CapturedMessage> lines; captured.CopySince(after, lines);
    std::string text; for (const auto& line : lines) text += line.text + "\n";
    return text;
  };
  REQUIRE(category->SetText(rex::SettingLayer::kRuntime, "debug"));
  REQUIRE(f.Read({0x10040002}) == 0);
  CHECK(f.Row(0)->data.s32 == 0);
  const auto text = captured_text();
  CHECK(text.find("ProfileSettingsRead wrote") != std::string::npos);
  CHECK(text.find("count=1") != std::string::npos);
  CHECK(text.find(std::format("settings={:08X}", f.block + 4408)) != std::string::npos);
  CHECK(text.find("ProfileSettingsRead api=base") != std::string::npos);
  CHECK(text.find("result=00000000") != std::string::npos);
  after=captured.GetLatestSequence();
  f.fixture->RunOnGuestThread([&](rex::test::Caller& caller) {
  auto& ctx=caller.GetContext();
    ctx.r3.u64=ctx.r4.u64=ctx.r5.u64=ctx.r6.u64=0;
  ctx.r1.u64 = f.block + 12000;
  ctx.r7.u64 = 1; ctx.r8.u64 = f.block + 4096; ctx.r9.u64 = 5; ctx.r10.u64 = 5;
  rex::memory::store_and_swap<uint32_t>(f.base() + ctx.r1.u32 + 0x54, 0);
  __imp__XamUserReadProfileSettings(ctx, f.base());
  CHECK(ctx.r3.u32 == X_ERROR_INVALID_PARAMETER);
  });
  CHECK(captured_text().find("result=00000057") != std::string::npos);
  CHECK(captured_text().find("ProfileSettingsRead wrote") == std::string::npos);
  after=captured.GetLatestSequence();
  REQUIRE(category->SetText(rex::SettingLayer::kRuntime, "info"));
  REQUIRE(f.Read({0x10040002}) == 0);
  CHECK(f.Row(0)->data.s32 == 0);
  CHECK(captured_text().find("ProfileSettingsRead") == std::string::npos);
}

TEST_CASE("Profile setting caches follow XUID when switching accounts", "[sdk][profile-settings][profile-settings-state]") {
  SettingsFixture f;
  uint64_t alpha = 0, bravo = 0;
  REQUIRE(f.profile()->CreateLocal("Alpha", &alpha) == 0);
  REQUIRE(f.profile()->CreateLocal("Bravo", &bravo) == 0);
  REQUIRE(f.profile()->SetLocalSession(alpha) == 0);
  auto value = Scalar(0x10040002, Type::kInt32); value.data.s32 = 1;
  REQUIRE(f.Write(&value, 1) == 0);
  REQUIRE(f.Read({0x10040002}) == 0); CHECK(f.Row(0)->data.s32 == 1);
  REQUIRE(f.profile()->SetLocalSession(bravo) == 0);
  REQUIRE(f.Read({0x10040002}) == 0); CHECK(f.Row(0)->data.s32 == 0);
  value.data.s32 = 2; REQUIRE(f.Write(&value, 1) == 0);
  REQUIRE(f.profile()->SetLocalSession(alpha) == 0);
  REQUIRE(f.Read({0x10040002}) == 0); CHECK(f.Row(0)->data.s32 == 1);
  REQUIRE(f.profile()->SetLocalSession(bravo) == 0);
  f.Open(); REQUIRE(f.Read({0x10040002}) == 0); CHECK(f.Row(0)->data.s32 == 2);
}

TEST_CASE("Profile setting failures preserve committed values and validate nested buffers", "[sdk][profile-settings][profile-settings-guard]") {
  SettingsFixture f;
  REQUIRE(f.profile()->CreateLocal("Validation") == 0);
  const auto xuid = f.profile()->GetXUID(); REQUIRE(f.profile()->SetLocalSession(xuid) == 0);
  auto value = Scalar(0x10040002, Type::kInt32); value.data.s32 = 1;
  REQUIRE(f.Write(&value, 1) == 0);
  SECTION("failed atomic write reports overlapped error and keeps cached value") {
    auto staging = f.ValuePath(xuid, 0x10040002); staging += ".tmp";
    std::filesystem::create_directory(staging);
    value.data.s32 = 2;
    CHECK(f.Write(&value, 1) == X_ERROR_FUNCTION_FAILED);
    REQUIRE(f.Read({0x10040002}) == 0); CHECK(f.Row(0)->data.s32 == 1);
    const auto overlap = f.block + 10000;
    uint32_t pending = ~0u;
    pending = f.Write(&value, 1, overlap);
    CHECK(pending == X_ERROR_IO_PENDING);
    const auto* ov = reinterpret_cast<rex::kernel::XOVERLAPPED*>(f.base() + overlap);
    CHECK(ov->result == X_ERROR_FUNCTION_FAILED);
    CHECK(ov->extended_error == uint32_t(XHRESULTFromWin32(X_ERROR_FUNCTION_FAILED)));
    f.Open(); REQUIRE(f.Read({0x10040002}) == 0); CHECK(f.Row(0)->data.s32 == 1);
  }
  SECTION("corrupt stored scalar is an error rather than a fabricated default") {
    { std::ofstream corrupt(f.ValuePath(xuid, 0x10040002), std::ios::binary); corrupt << "bad"; }
    f.Open(); CHECK(f.Read({0x10040002}) == X_ERROR_FUNCTION_FAILED);
  }
  SECTION("invalid second entry cannot partially commit a validated first entry") {
    std::array<Setting, 2> values{value, Scalar(0x60800054, Type::kBinary)};
    values[0].data.s32 = 2; values[1].data.binary.ptr = 5; values[1].data.binary.size = 8;
    CHECK(f.Write(values.data(), 2) == X_ERROR_INVALID_PARAMETER);
    REQUIRE(f.Read({0x10040002}) == 0); CHECK(f.Row(0)->data.s32 == 1);
    values[1].data.binary.ptr = f.block + 1024; values[1].data.binary.size = 129;
    CHECK(f.Write(values.data(), 2) == X_ERROR_INVALID_PARAMETER);
  }
  SECTION("ledger counters cannot be overwritten through editable preferences") {
    for (uint32_t id : {0x10040006u, 0x10040012u, 0x10040013u, 0x10040038u, 0x10040039u}) {
      value.setting_id = id;
      CHECK(f.Write(&value, 1) == X_ERROR_ACCESS_DENIED);
      CHECK_FALSE(std::filesystem::exists(f.ValuePath(xuid, id)));
    }
  }
}

TEST_CASE("Concurrent profile setting reads never borrow a replaced setting object", "[sdk][profile-settings][profile-settings-guard]") {
  SettingsFixture f;
  REQUIRE(f.profile()->CreateLocal("Concurrent") == 0);
  REQUIRE(f.profile()->SetLocalSession(f.profile()->GetXUID()) == 0);
  auto value = Scalar(0x10040002, Type::kInt32);
  std::atomic<bool> failed{false};
  std::jthread writer([&] {
    for (int i = 0; i < 12; ++i) {
      value.data.s32 = i % 2;
      if (f.Write(&value, 1)) failed = true;
    }
  });
  for (int i = 0; i < 32; ++i) {
    if (f.Read({0x10040002}) || (f.Row(0)->data.s32 != 0 && f.Row(0)->data.s32 != 1)) failed = true;
  }
  writer.join(); CHECK_FALSE(failed.load());
}

TEST_CASE("Original dashboard optional profile fields remain unset until written", "[sdk][profile-settings][profile-settings-optional]") {
  SettingsFixture f;
  REQUIRE(f.profile()->CreateLocal("Optional") == 0);
  REQUIRE(f.profile()->SetLocalSession(f.profile()->GetXUID()) == 0);
  const std::vector<uint32_t> ids{0x10040002, 0x63E80044, 0x7008004F, 0x61180050,
      0x10040057, 0x40520041, 0x41040040, 0x43E80043};
  REQUIRE(f.Read(ids) == 0);
  CHECK(f.Row(0)->from == 1);
  for (size_t i = 1; i < ids.size(); ++i) {
    CHECK(f.Row(i)->setting_id == ids[i]);
    CHECK(f.Row(i)->from == 0);
    CHECK(f.Row(i)->data.type == uint8_t(Type::kUnset));
    CHECK_FALSE(std::filesystem::exists(f.ValuePath(f.profile()->GetXUID(), ids[i])));
  }
  auto value = Scalar(0x10040057, Type::kInt32); value.data.s32 = 7;
  REQUIRE(f.Write(&value, 1) == 0);
  f.Open(); REQUIRE(f.Read(ids) == 0);
  CHECK(f.Row(4)->from == 1); CHECK(f.Row(4)->data.s32 == 7);
  CHECK(f.Row(1)->from == 0);
  for (uint32_t id : {0x10080057u, 0x20040051u, 0x40030040u, 0x80040001u})
    CHECK(f.Read({id}) == X_ERROR_INVALID_PARAMETER);
}
