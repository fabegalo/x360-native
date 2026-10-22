#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <fmt/format.h>
#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <rex/system/xthread.h>
#include <rex/system/thread_state.h>
#include <rex/system/xio.h>
#include "synthetic_xex.h"

#define IMPORT(name) extern "C" void __imp__##name(PPCContext&, uint8_t*)
IMPORT(XamContentCreate); IMPORT(XamContentCreateEx); IMPORT(XamContentClose);
IMPORT(XamContentFlush); IMPORT(XamContentDelete); IMPORT(XamContentGetCreator);
IMPORT(XamContentCreateEnumerator); IMPORT(XamEnumerate); IMPORT(XamUserLogonEx);
IMPORT(XamUserLogon); IMPORT(XGetOverlappedResult);
IMPORT(NtCreateFile); IMPORT(NtReadFile); IMPORT(NtWriteFile); IMPORT(NtClose);
IMPORT(NtFlushBuffersFile);
IMPORT(NtQueryVolumeInformationFile);
IMPORT(XamContentAggregateCreateEnumerator);
using namespace rex;
using namespace rex::system;
using namespace rex::system::xam;

namespace {
struct SaveGuest {
  Runtime rt;
  uint32_t mem;
  uint8_t* base;
  explicit SaveGuest(const std::filesystem::path& root, uint32_t title = 0x1234C800)
      : rt(root, root) {
    RuntimeConfig config{}; config.tool_mode = true;
    config.kernel_init = kernel::InitializeKernel;
    REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
    auto image = x360n::test::BuildXex({}, nullptr, 0, x360n::test::Entry::kReturnZero);
    // Add the real XEX execution-info header; KernelState obtains Title ID from it.
    memory::store_and_swap<uint32_t>(image.data() + 0x14, 4);
    memory::store_and_swap<uint32_t>(image.data() + 0x30, 0x00040006);
    memory::store_and_swap<uint32_t>(image.data() + 0x34, 0x60);
    memory::store_and_swap<uint32_t>(image.data() + 0x6C, title);
    { std::ofstream out(root / "default.xex", std::ios::binary);
      out.write(reinterpret_cast<char*>(image.data()), image.size()); }
    auto module = object_ref<UserModule>(new UserModule(rt.kernel_state()));
    REQUIRE(module->LoadFromFile("game:\\default.xex") == X_STATUS_SUCCESS);
    rt.kernel_state()->SetExecutableModule(module);
    REQUIRE(rt.kernel_state()->title_id() == title);
    mem = rt.memory()->SystemHeapAlloc(8192);
    REQUIRE(mem); base = rt.memory()->virtual_membase();
    std::memset(base + mem, 0, 8192);
    std::strcpy(reinterpret_cast<char*>(base + mem), "save");
    auto* data = reinterpret_cast<XCONTENT_DATA*>(base + mem + 256);
    data->device_id = 1; data->content_type = XContentType::kSavedGame;
    data->set_file_name("progress"); data->set_display_name(u"Progress");
  }
  uint32_t call(void (*fn)(PPCContext&, uint8_t*), std::initializer_list<uint64_t> args) {
    uint32_t result = ~0u;
    auto thread = object_ref<XHostThread>(new XHostThread(rt.kernel_state(), 128*1024, 0, [&] {
      auto& ctx = *XThread::GetCurrentThread()->thread_state()->context();
      ctx.r1.u64 = mem + 7168;
      PPCRegister* regs[] = {&ctx.r3,&ctx.r4,&ctx.r5,&ctx.r6,&ctx.r7,&ctx.r8,&ctx.r9,&ctx.r10};
      unsigned i = 0;
      for (auto arg : args) {
        if (i < 8) regs[i]->u64 = arg;
        else memory::store_and_swap<uint32_t>(base + ctx.r1.u32 + 0x54 + (i-8)*8, uint32_t(arg));
        ++i;
      }
      fn(ctx, base); result = ctx.r3.u32; return 0;
    }));
    REQUIRE(thread->Create() == X_STATUS_SUCCESS);
    uint64_t timeout = uint64_t(-50'000'000ll);
    REQUIRE(thread->Wait(0, 0, 0, &timeout) == X_STATUS_SUCCESS);
    return result;
  }
  uint32_t word(uint32_t offset) { return memory::load_and_swap<uint32_t>(base+mem+offset); }
  void select(uint64_t xuid) {
    std::memset(base+mem+4096, 0, 392*4);
    if (!xuid) {
      memory::store_and_swap<uint64_t>(base+mem+4096, rt.kernel_state()->user_profile()->xuid());
      REQUIRE(call(__imp__XamUserLogon, {mem+4096, 0x28, 0}) == 0);
      return;
    }
    memory::store_and_swap<uint64_t>(base+mem+4096, xuid);
    REQUIRE(call(__imp__XamUserLogonEx, {mem+4096, 0x120420, 0}) == 0);
  }
  uint32_t content(uint32_t disposition, unsigned user = 0, bool async = false) {
    if (async) return call(__imp__XamContentCreateEx,
        {user,mem,mem+256,disposition,mem+600,mem+604,0,0,mem+700});
    return call(__imp__XamContentCreate, {user,mem,mem+256,disposition,mem+600,mem+604,async?mem+700:0});
  }
  uint32_t open(uint32_t disposition = 3, const char* path = "save:\\progress.bin") {
    auto* str = reinterpret_cast<X_ANSI_STRING*>(base+mem+800);
    std::strcpy(reinterpret_cast<char*>(base+mem+832), path);
    str->length = uint16_t(std::strlen(path)); str->maximum_length = uint16_t(std::strlen(path)+1);
    str->pointer = mem+832;
    auto* attrs = reinterpret_cast<X_OBJECT_ATTRIBUTES*>(base+mem+768);
    attrs->root_directory = 0; attrs->name_ptr = mem+800; attrs->attributes = 64;
    REQUIRE(call(__imp__NtCreateFile, {mem+608,0xC0000000,mem+768,mem+640,0,0x80,3,disposition,0x20}) == 0);
    return word(608);
  }
  uint32_t io(uint32_t file, const std::string& bytes, bool write) {
    if (write) std::memcpy(base+mem+1024, bytes.data(), bytes.size());
    else std::memset(base+mem+1024, 0, bytes.size());
    memory::store_and_swap<uint64_t>(base+mem+680, 0);
    return call(write?__imp__NtWriteFile:__imp__NtReadFile,
                {file,0,0,0,mem+640,mem+1024,bytes.size(),mem+680});
  }
  void write(uint32_t file, const std::string& bytes) {
    REQUIRE(io(file, bytes, true) == 0); REQUIRE(word(644) == bytes.size());
    REQUIRE(call(__imp__NtFlushBuffersFile,{file,mem+640}) == 0);
    REQUIRE(call(__imp__XamContentFlush,{mem,0}) == 0);
  }
  void read(uint32_t file, const std::string& bytes) {
    REQUIRE(io(file,bytes,false) == 0); REQUIRE(word(644) == bytes.size());
    REQUIRE(std::string(reinterpret_cast<char*>(base+mem+1024), bytes.size()) == bytes);
  }
  uint32_t enumerator() {
    REQUIRE(call(__imp__XamContentCreateEnumerator,{0,1,1,0,8,mem+612,mem+616}) == 0);
    REQUIRE(word(612) == 8*sizeof(XCONTENT_DATA)); return word(616);
  }
  uint32_t enumerate(uint32_t handle) {
    return call(__imp__XamEnumerate,{handle,0,mem+1536,8*sizeof(XCONTENT_DATA),mem+620,0});
  }
  void close(uint32_t file) {
    REQUIRE(call(__imp__NtClose,{file}) == 0);
    REQUIRE(call(__imp__XamContentClose,{mem,0}) == 0);
  }
};
}

TEST_CASE("Save storage routes separate volumes and enforces capacity through NT", "[sdk][save-storage][storage-device]") {
  const auto root = std::filesystem::current_path() /
      ("c8-volumes-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root / "storage/00000007");
  struct Cleanup { std::filesystem::path path;
    ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
  } cleanup{root};
  std::ofstream(root / "storage.toml") << "version=1\n[[device]]\nid=7\ntype='mu'\ncapacity_bytes=4096\n";
  uint64_t owner = 0;
  for (int restart = 0; restart != 2; ++restart) {
    SaveGuest g(root);
    auto* profile = g.rt.kernel_state()->user_profile();
    if (!restart) REQUIRE(profile->CreateLocal("VolumeOwner") == 0);
    owner = profile->xuid();
    g.select(owner);
    auto* data = reinterpret_cast<XCONTENT_DATA*>(g.base + g.mem + 256);
    data->device_id = 7;
    REQUIRE(g.content(restart ? 3 : 1) == 0);
    auto file = g.open(restart ? 1 : 3);
    if (!restart) {
      g.write(file, "volume-seven");
      // A rejected growth leaves both the file and package untouched.
      REQUIRE(g.io(file, std::string(5000, 'x'), true) == 0xC000007F);
    }
    g.read(file, "volume-seven");
    REQUIRE(g.call(__imp__NtQueryVolumeInformationFile, {file, g.mem + 640, g.mem + 1536, 24, 3}) == 0);
    CHECK(memory::load_and_swap<uint64_t>(g.base + g.mem + 1536) == 4096 / 512);
    CHECK(memory::load_and_swap<uint64_t>(g.base + g.mem + 1544) < 4096 / 512);
    g.close(file);
    // Internal Storage enumeration carries the complete XUID in r3.
    // A low-32-bit owner must never hide this profile's packages.
    REQUIRE(g.call(__imp__XamContentAggregateCreateEnumerator,
        {owner, 7, 0xFFFFFFFF, 0, g.mem + 616}) == 0);
    REQUIRE(g.call(__imp__XamEnumerate,
        {g.word(616), 0, g.mem + 1536, sizeof(XCONTENT_AGGREGATE_DATA), g.mem + 620, 0}) == 0);
    CHECK(g.word(620) == 1);
    const auto* aggregate = reinterpret_cast<const XCONTENT_AGGREGATE_DATA*>(g.base + g.mem + 1536);
    CHECK(aggregate->xuid == owner);
    CHECK(aggregate->device_id == 7);
    REQUIRE(g.call(__imp__NtClose, {g.word(616)}) == 0);
    REQUIRE(g.call(__imp__XamContentCreateEnumerator, {0, 1, 1, 0, 8, g.mem + 612, g.mem + 616}) == 0);
    CHECK(g.enumerate(g.word(616)) != 0); // Nothing from device 7 leaks into HDD.
    const auto package = root / "storage/00000007" / fmt::format("{:016X}", owner) /
        "1234C800/00000001/progress/progress.bin";
    CHECK(std::filesystem::file_size(package) == std::string("volume-seven").size());
    if (!restart) {
      std::filesystem::rename(root / "storage/00000007", root / "detached-volume");
      CHECK(g.content(3) == X_ERROR_DEVICE_NOT_CONNECTED);
      std::filesystem::rename(root / "detached-volume", root / "storage/00000007");
    }
  }
}

TEST_CASE("Save storage reports real failures and completes overlapped operations", "[sdk][save-storage]") {
  const auto root = std::filesystem::current_path() /
      ("c8-errors-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  struct Cleanup { std::filesystem::path path;
    ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path,ec); }
  } cleanup{root};
  SaveGuest g(root);
  auto* profile = g.rt.kernel_state()->user_profile();
  REQUIRE(profile->CreateLocal("StorageErrors") == 0); g.select(profile->xuid());
  auto* data = reinterpret_cast<XCONTENT_DATA*>(g.base+g.mem+256);
  for (auto name : {"../escape", "..", "C:\\escape", "save:stream", "trailing.", ""}) {
    data->set_file_name(name); REQUIRE(g.content(4) == X_ERROR_INVALID_PARAMETER);
  }
  data->set_file_name("progress");
  data->device_id = 99; REQUIRE(g.content(4) == X_ERROR_DEVICE_NOT_CONNECTED); data->device_id = 1;
  REQUIRE(g.content(0) == X_ERROR_INVALID_PARAMETER);
  const auto title = root / fmt::format("{:016X}/1234C800", profile->xuid());
  std::filesystem::create_directories(title);
  { std::ofstream blocked(title / "00000001"); blocked << "existing data must survive"; }
  REQUIRE(g.content(4) == X_ERROR_ACCESS_DENIED);
  REQUIRE(g.call(__imp__XamContentCreateEnumerator,{0,1,1,0,8,g.mem+612,g.mem+616}) == X_ERROR_ACCESS_DENIED);
  REQUIRE(std::filesystem::file_size(title / "00000001") == 26);
  std::filesystem::remove(title / "00000001"); // only this test's sentinel
  // Failed metadata must not publish a mount or leave an apparent empty save.
  std::filesystem::create_directories(title/"Headers/00000001/progress.header");
  REQUIRE(g.content(1) == X_ERROR_ACCESS_DENIED);
  REQUIRE_FALSE(std::filesystem::exists(title/"00000001/progress"));
  REQUIRE(g.call(__imp__XamContentFlush,{g.mem,0}) == X_ERROR_FILE_NOT_FOUND);
  REQUIRE(std::filesystem::is_directory(title/"Headers/00000001/progress.header"));
  std::filesystem::remove(title/"Headers/00000001/progress.header");
  // Deferred completion goes through the dispatch queue and guest XOVERLAPPED.
  REQUIRE(g.content(1,0,true) == X_ERROR_IO_PENDING);
  auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(3);
  uint32_t result;
  do {
    result = g.call(__imp__XGetOverlappedResult,{g.mem+700,g.mem+624,0});
    if (result != X_ERROR_IO_INCOMPLETE) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  } while(std::chrono::steady_clock::now() < deadline);
  REQUIRE(result == 0); REQUIRE(g.word(624) == 1); REQUIRE(g.word(724) == 0);
  auto f = g.open(); g.write(f,"do not overwrite");
  REQUIRE(g.call(__imp__XamContentDelete,{0,g.mem+256,0}) == X_ERROR_ACCESS_DENIED);
  REQUIRE(g.content(2) == X_ERROR_ALREADY_EXISTS); g.read(f,"do not overwrite"); g.close(f);
  // Reusing a system alias must not delete the existing save before failing.
  std::strcpy(reinterpret_cast<char*>(g.base+g.mem),"game");
  REQUIRE(g.content(2) == X_ERROR_ALREADY_EXISTS);
  std::strcpy(reinterpret_cast<char*>(g.base+g.mem),"save");
  REQUIRE(g.content(3) == 0); f=g.open(1); g.read(f,"do not overwrite"); g.close(f);
  // Explicit truncate really replaces data, then reports missing files honestly.
  REQUIRE(g.content(5) == 0); REQUIRE(g.word(600) == 1);
  REQUIRE(std::filesystem::is_empty(title/"00000001/progress"));
  REQUIRE(g.call(__imp__XamContentClose,{g.mem,0}) == 0);
  // Keep an existing shared/legacy save untouched and invisible in the user catalog.
  std::filesystem::create_directories(root/"0000000000000000/1234C800/00000001/legacy");
  std::ofstream(root/"0000000000000000/1234C800/00000001/legacy/data") << "legacy";
  auto e=g.enumerator(); REQUIRE(g.enumerate(e)==0); REQUIRE(g.word(620)==1);
  REQUIRE(std::filesystem::file_size(root/"0000000000000000/1234C800/00000001/legacy/data")==6);
}

TEST_CASE("Save storage uses XAM and NT file contracts across profiles and restarts", "[sdk][save-storage]") {
  const char* external = std::getenv("X360N_C8_STORE");
  const char* phase = std::getenv("X360N_C8_PHASE");
  const auto root = external ? std::filesystem::path(external) : std::filesystem::current_path() /
      ("c8-save-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  struct Cleanup { std::filesystem::path path; bool remove;
    ~Cleanup() { if(remove) { std::error_code ec; std::filesystem::remove_all(path,ec); } }
  } cleanup{root,!external};
  const std::string first = "Player1 level=17 coins=4321";
  const std::string second = "Player2 level=03 coins=0099";
  uint64_t p1 = 0, p2 = 0;
  if (!phase || std::string(phase) != "read") {
    SaveGuest g(root);
    auto* profile = g.rt.kernel_state()->user_profile();
    REQUIRE(profile->CreateLocal("Player1", &p1) == 0);
    REQUIRE(profile->CreateLocal("Player2", &p2) == 0);
    REQUIRE(g.content(4) == X_ERROR_NO_SUCH_USER);
    g.select(p1);
    REQUIRE(g.content(3) == X_ERROR_PATH_NOT_FOUND);
    REQUIRE(g.content(4,1) == X_ERROR_NO_SUCH_USER);
    REQUIRE(g.content(1) == 0); REQUIRE(g.word(600) == 1);
    const auto a = g.open(); g.write(a,first); g.read(a,first);
    REQUIRE(g.content(1) == X_ERROR_ALREADY_EXISTS);
    const auto old_enum = g.enumerator();
    REQUIRE(g.enumerate(old_enum) == 0); REQUIRE(g.word(620) == 1);
    // A duplicated/live file and an already-created enumerator must not grant
    // Player2 access to Player1's bytes after switching the original XAM session.
    g.select(p2);
    REQUIRE(g.io(a,first,false) == X_STATUS_ACCESS_DENIED);
    REQUIRE(g.io(a,"overwrite",true) == X_STATUS_ACCESS_DENIED);
    REQUIRE(g.enumerate(old_enum) == X_ERROR_NO_SUCH_USER);
    auto empty = g.enumerator();
    REQUIRE(g.enumerate(empty) == X_ERROR_NO_MORE_FILES); REQUIRE(g.word(620) == 0);
    REQUIRE(g.content(3) == X_ERROR_PATH_NOT_FOUND);
    REQUIRE(g.content(4) == 0);
    auto b = g.open(); g.write(b,second); g.close(b);
    g.select(p1);
    REQUIRE(g.io(a,first,false) == X_STATUS_ACCESS_DENIED); // no revival
    REQUIRE(g.content(3) == 0); REQUIRE(g.word(600) == 2);
    auto again = g.open(1); g.read(again,first); g.close(again);
    REQUIRE(g.call(__imp__NtFlushBuffersFile,{0x12345678,g.mem+640}) == X_STATUS_INVALID_HANDLE);
    REQUIRE(g.call(__imp__XamContentFlush,{g.mem,0}) == X_ERROR_FILE_NOT_FOUND);
    REQUIRE(g.call(__imp__NtClose,{a}) == 0);
    std::cout << "C8 write: two real XUIDs, XAM mounts, NT write/read/flush, switch isolation; store=" << root << '\n';
  }
  if (!phase || std::string(phase) != "write") {
    {
      SaveGuest g(root);
      const auto profiles = g.rt.kernel_state()->user_profile()->local_profiles();
      // Profiles are found by scanning the device, so match them by gamertag.
      REQUIRE(profiles.size() == 2);
      for (const auto& account : profiles) (account.name == "Player1" ? p1 : p2) = account.xuid;
      REQUIRE(g.rt.kernel_state()->user_profile()->FindLocal(p1).name == "Player1");
      REQUIRE(g.rt.kernel_state()->user_profile()->FindLocal(p2).name == "Player2");
      for (auto [id, bytes] : {std::pair{p1,first},std::pair{p2,second}}) {
        g.select(id); REQUIRE(g.content(3) == 0);
        auto f = g.open(1); g.read(f,bytes); g.close(f);
        auto e = g.enumerator(); REQUIRE(g.enumerate(e) == 0); REQUIRE(g.word(620) == 1);
      }
      // Delete Player2 through XAM; Player1's data is still present and readable.
      REQUIRE(g.call(__imp__XamContentDelete,{0,g.mem+256,0}) == 0);
      REQUIRE(g.call(__imp__XamContentDelete,{0,g.mem+256,0}) == X_ERROR_FILE_NOT_FOUND);
      g.select(p1); REQUIRE(g.content(3) == 0);
      auto f = g.open(1); g.read(f,first); g.close(f);
      g.select(0); REQUIRE(g.content(4) == X_ERROR_NO_SUCH_USER);
      std::cout << "C8 restart: both payloads recovered through NT reads, isolated deletion confirmed\n";
    }
    {
      SaveGuest other(root,0x1234C801); other.select(p1);
      REQUIRE(other.content(3) == X_ERROR_PATH_NOT_FOUND);
      REQUIRE(other.content(4) == 0);
      auto f = other.open(); other.write(f,"other title"); other.close(f);
    }
  }
}
