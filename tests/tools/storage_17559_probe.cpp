// Explicit CPU-only checks of the original 17559 selector and web task sizing.
// --web-task-size checks the native FC/58072 method without starting XLiveBase.
// Does not construct scenes, enable input, start a title, or use user storage.
#include "../supervisor/scratch.h"
#include "../../titles/dash_17559/src/original_assets.h"
#include "../../titles/common/system_ui_context.h"
#include <rex/kernel/init.h>
#include <rex/kernel/xam/module.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/user_module.h>
#include <rex/system/xthread.h>
#include <cstring>
#include <cmath>
#include <iostream>
#include <windows.h>

int main(int argc, char** argv) {
  const bool web_size = argc == 4 && std::string(argv[3]) == "--web-task-size";
  if (argc != 3 && !web_size) return 2;
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  const auto originals = std::filesystem::absolute(argv[1]);
  const auto binaries = std::filesystem::absolute(argv[2]);
  for (const auto& asset : dash17559::kOriginalAssets) {
    if (asset.path != (web_size ? "system/xam_ui.xex" : "system/deviceselector.xex")) continue;
    const auto error = x360n::VerifyDashboardAssets(originals, "17559", {&asset, 1});
    if (!error.empty()) { std::cerr << error << '\n'; return 3; }
  }
  const auto work = UniqueTempPath("storage17559");
  if (!std::filesystem::create_directory(work)) return 4;
  struct Cleanup { std::filesystem::path path;
    ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
  } cleanup{work};
  rex::Runtime runtime(originals, work, {}, work / "cache", work / "metadata");
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  if (runtime.Setup(std::move(config))) return 5;
  auto* state = runtime.kernel_state();
  if (web_size) {
    const auto dll = (binaries / "dash_17559_xam_ui.dll").string();
    state->RegisterRecompiledModule("xam_ui.xex", "game:\\system\\xam_ui.xex", dll.c_str());
    if (!state->LoadUserModule("game:\\system\\xam_ui.xex", false)) return 6;
    bool passed = false;
    auto thread = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
        state, 512 * 1024, 0, [&] {
      x360n::SystemUiContext process(*runtime.memory());
      auto* memory = runtime.memory();
      auto* dispatcher = runtime.function_dispatcher();
      const auto block = memory->SystemHeapAlloc(1024);
      if (!block || !dispatcher->GetFunction(0x818E1828)) return 7;
      std::memset(memory->TranslateVirtual(block), 0, 1024);
      auto put = [&](uint32_t offset, uint32_t value) {
        rex::memory::store_and_swap<uint32_t>(memory->TranslateVirtual(block + offset), value);
      };
      auto get = [&](uint32_t offset) {
        return rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(block + offset));
      };
      constexpr char uri[] = "/storage/{name}";
      constexpr char schema[] = "name{t:str,offset:0,size:32,loc:url,max:32}";
      std::memcpy(memory->TranslateVirtual(block + 128), uri, sizeof(uri));
      std::memcpy(memory->TranslateVirtual(block + 256), schema, sizeof(schema));
      put(0, block + 128); put(16, block + 64);
      passed = true;
      uint32_t baseline = 0;
      for (const auto flags : {0u, 0x10u, 0u}) {
        put(12, flags); put(60, 0x13579BDF); put(64, 0xFFFFFFFF); put(68, 0x2468ACE0);
        uint64_t args[] = {0, block}; // this is unused by the original size method.
        const auto result = uint32_t(dispatcher->ExecuteTrap(process.thread_state(), 0x818E1828, args, 2));
        const auto size = get(64);
        if (!baseline) baseline = size;
        // 0x10 adds 112 bytes of request headers plus the 0x426C0 work area.
        const bool okay = !result && size == baseline + (flags ? 0x42730u : 0u) &&
            size >= 7016 && get(60) == 0x13579BDF && get(68) == 0x2468ACE0;
        passed &= okay;
        std::cout << "original-web-size flags=" << std::hex << flags << " result=" << result
                  << " size=" << std::dec << size << " checked=" << okay << std::endl;
      }
      put(4, block + 256);
      uint64_t args[] = {0, block};
      const auto result = uint32_t(dispatcher->ExecuteTrap(process.thread_state(), 0x818E1828, args, 2));
      const bool schema_okay = !result && get(64) > baseline &&
          get(60) == 0x13579BDF && get(68) == 0x2468ACE0;
      passed &= schema_okay;
      std::cout << "original-web-size schema-result=" << std::hex << result << " size="
                << std::dec << get(64) << " checked=" << schema_okay << std::endl;
      constexpr char invalid[] = "name{t:invalid,offset:0,size:32,loc:url}";
      std::memcpy(memory->TranslateVirtual(block + 256), invalid, sizeof(invalid));
      put(64, 0xFFFFFFFF);
      const auto error = uint32_t(dispatcher->ExecuteTrap(process.thread_state(), 0x818E1828, args, 2));
      const bool invalid_okay = int32_t(error) < 0 && get(64) == 0 &&
          get(60) == 0x13579BDF && get(68) == 0x2468ACE0;
      passed &= invalid_okay;
      std::cout << "original-web-size invalid-result=" << std::hex << error
                << " size=" << std::dec << get(64) << " checked=" << invalid_okay << std::endl;
      memory->SystemHeapFree(block);
      return passed ? 0 : 10;
    }));
    if (thread->Create()) return 11;
    uint64_t timeout = uint64_t(-100'000'000ll);
    if (thread->Wait(0, 0, 0, &timeout)) ExitProcess(12);
    return passed ? 0 : 13;
  }
  const auto dll = (binaries / "dash_17559_deviceselector.dll").string();
  state->RegisterRecompiledModule("deviceselector.xex", "game:\\system\\deviceselector.xex", dll.c_str());
  const auto module = state->LoadUserModule("game:\\system\\deviceselector.xex", false);
  if (!module) return 6;
  bool passed = false;
  auto thread = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
      state, 512 * 1024, 0, [&] {
    x360n::SystemUiContext process(*runtime.memory());
    auto* dispatcher = runtime.function_dispatcher();
    auto* memory = runtime.memory();
    uint64_t attach[] = {module->hmodule_ptr(), 1, 0};
    if (!dispatcher->ExecuteTrap(process.thread_state(), module->entry_point(), attach, 3)) return 7;
    const auto xam = state->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
    const auto registration = xam->FindSystemApp(0xFF);
    if (!registration || registration->resource_id != 2 || registration->callback != 0x916D6B20) return 8;
    const auto object = memory->SystemHeapAlloc(512);
    const auto overlapped = memory->SystemHeapAlloc(sizeof(rex::system::XAM_OVERLAPPED));
    if (!object || !overlapped) return 9;
    auto put = [&](uint32_t address, uint32_t value) {
      rex::memory::store_and_swap<uint32_t>(memory->TranslateVirtual(address), value);
    };
    passed = true;
    // The scene uses these original rounding helpers indirectly for capacity
    // labels. Both are outside .pdata and must be explicit AOT roots.
    for (const auto address : {0x916D7560u, 0x916D75A0u}) {
      auto* function = dispatcher->GetFunction(address);
      if (!function) {
        std::cout << "original-selector-rounding missing=" << std::hex << address << std::endl;
        passed = false;
        continue;
      }
      auto* ctx = process.thread_state()->context();
      bool okay = true;
      for (const double value : {-2.25, -0.0, 0.0, 3.0, 7.5, 8191.1}) {
        const auto saved = *ctx;
        ctx->f1.f64 = value;
        function(*ctx, memory->virtual_membase());
        const double actual = ctx->f1.f64;
        *ctx = saved;
        ctx->fpscr.restoreGuestBits(saved.fpscr.csr);
        const double expected = address == 0x916D7560u ? std::ceil(value) : std::floor(value);
        okay &= actual == expected && (actual != 0 || std::signbit(actual) == std::signbit(expected));
      }
      passed &= okay;
      std::cout << "original-selector-rounding function=" << std::hex << address
                << " cases=6 checked=" << okay << std::endl;
    }
    // Original completion handler 916D47A0, state != 1: close application,
    // then XMsgCompleteIORequest(io, status ? 1627 : 0, status, 0).
    for (const uint32_t status : {0u, 0x800704C7u}) {
      std::memset(memory->TranslateVirtual(object), 0, 512);
      std::memset(memory->TranslateVirtual(overlapped), 0, sizeof(rex::system::XAM_OVERLAPPED));
      put(object + 0x4C, 2); put(object + 0x50, status); put(object + 0x74, overlapped);
      put(overlapped, 997);
      uint64_t args[] = {object};
      const auto result = dispatcher->ExecuteTrap(process.thread_state(), 0x916D47A0, args, 1);
      const auto* io = memory->TranslateVirtual<rex::system::XAM_OVERLAPPED*>(overlapped);
      const bool okay = uint32_t(result) == 0 && io->result == (status ? 1627u : 0u) &&
          io->extended_error == status && io->length == 0;
      passed &= okay;
      std::cout << "original-selector-completion status=" << std::hex << status
                << " result=" << uint32_t(io->result) << " extended=" << uint32_t(io->extended_error)
                << " checked=" << okay << std::endl;
    }
    memory->SystemHeapFree(overlapped); memory->SystemHeapFree(object);
    uint64_t detach[] = {module->hmodule_ptr(), 0, 0};
    dispatcher->ExecuteTrap(process.thread_state(), module->entry_point(), detach, 3);
    passed &= !xam->FindSystemApp(0xFF).has_value();
    return passed ? 0 : 10;
  }));
  if (thread->Create()) return 11;
  uint64_t timeout = uint64_t(-100'000'000ll);
  if (thread->Wait(0, 0, 0, &timeout)) ExitProcess(12);
  std::cout << "original-selector-contracts=" << (passed ? "pass" : "FAIL") << std::endl;
  return passed ? 0 : 13;
}
