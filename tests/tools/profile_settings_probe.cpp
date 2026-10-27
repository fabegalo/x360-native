// Explicit CPU diagnostic. Loads authenticated XEX data, never its entrypoint,
// and reads settings only from a new copy of the supplied console profile.
#include "sha256.h"
#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/kernel/xam/offline_achievement_helpers.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <rex/system/xthread.h>
#include <rex/ppc/func.h>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <regex>
#include <windows.h>

extern "C" REX_FUNC(__imp__XamUserReadProfileSettings);
namespace fs = std::filesystem;
int main(int argc, char** argv) {
  if (argc != 8) {
    std::cerr << "usage: profile_settings_probe game_root entry_relative sha256 console_source ids_hex count output_new\n";
    return 2;
  }
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  try {
    const auto game = fs::canonical(argv[1]);
    const auto relative = fs::path(argv[2]);
    if (relative.is_absolute()) return 2;
    for (const auto& part : relative) if (part == "..") return 2;
    const auto digest = x360n::DigestFile(game / relative);
    if (!digest || digest->sha256 != argv[3]) return 3;
    const auto source = fs::canonical(argv[4]);
    const uint32_t ids = uint32_t(std::stoul(argv[5], nullptr, 16));
    const uint32_t count = uint32_t(std::stoul(argv[6]));
    if (!count || count > 32) return 2;
    const auto output = fs::absolute(argv[7]);
    if (!fs::create_directory(output)) return 4;
    const auto console = output / "console-copy";
    fs::create_directory(console);
    const std::regex owner("E[0-9A-Fa-f]{15}");
    for (const auto& entry : fs::directory_iterator(source)) {
      const auto name = entry.path().filename().string();
      if (entry.is_directory() && (std::regex_match(name, owner) || name == "profiles" ||
          name == "console" || name == "offline-achievements-v1"))
        fs::copy(entry.path(), console / name, fs::copy_options::recursive);
    }
    rex::Runtime runtime(game, console, {}, output / "cache", output / "metadata");
    rex::RuntimeConfig config{};
    config.tool_mode = true;
    config.kernel_init = rex::kernel::InitializeKernel;
    if (runtime.Setup(std::move(config))) return 5;
    auto* state = runtime.kernel_state();
    auto module = rex::system::object_ref<rex::system::UserModule>(new rex::system::UserModule(state));
    auto guest_path = std::string("game:\\") + relative.generic_string();
    const auto loaded = module->LoadFromFile(guest_path.c_str());
    std::cout << "load=" << std::hex << loaded << std::endl;
    if (loaded) return 6;
    state->SetExecutableModule(module);
    auto* memory = runtime.memory();
    if (!rex::kernel::xam::AchievementGuestSpan(memory, ids, count * 4,
                                               rex::memory::kMemoryProtectRead)) return 7;
    auto* base = memory->virtual_membase();
    auto* profile = state->user_profile();
    std::cout << "title=" << std::hex << state->title_id() << " signed-in=" << profile->signin_state() << std::endl;
    rex::system::OfflineAchievementTotals totals{}, title_totals{};
    auto totals_result = state->offline_achievements().Totals(profile->identity().xuid, totals,
                                                             state->title_id(), &title_totals);
    std::cout << "totals-result=" << unsigned(totals_result) << " win32="
              << rex::kernel::xam::OfflineAchievementError(totals_result) << std::endl;
    for (uint32_t i = 0; i < count; ++i) {
      auto id = rex::memory::load_and_swap<uint32_t>(base + ids + i * 4);
      auto setting = profile->GetSetting(id);
      std::cout << "setting=" << std::hex << id << " exists=" << bool(setting)
                << " load-failed=" << (setting && setting->load_failed)
                << " set=" << (setting && setting->is_set) << std::endl;
    }
    const auto block = memory->SystemHeapAlloc(16384);
    if (!block) return 8;
    std::memset(base + block, 0, 16384);
    auto put = [&](uint32_t address, uint32_t value) { rex::memory::store_and_swap<uint32_t>(base + address, value); };
    auto get = [&](uint32_t address) { return rex::memory::load_and_swap<uint32_t>(base + address); };
    bool complete = false;
    auto thread = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
        state, 128 * 1024, 0, [&] {
      auto& ctx = *rex::system::XThread::GetCurrentThread()->thread_state()->context();
      ctx.r1.u64 = block + 14336;
      auto call = [&](uint32_t buffer, uint32_t overlapped) {
        ctx.r3.u64 = 0; ctx.r4.u64 = 0; ctx.r5.u64 = 0; ctx.r6.u64 = 0;
        ctx.r7.u64 = count; ctx.r8.u64 = ids; ctx.r9.u64 = block; ctx.r10.u64 = buffer;
        put(ctx.r1.u32 + 0x54, overlapped);
        __imp__XamUserReadProfileSettings(ctx, base);
        return ctx.r3.u32;
      };
      const auto sizing = call(0, 0);
      const auto size = get(block);
      std::cout << "sizing=" << std::hex << sizing << " bytes=" << std::dec << size << std::endl;
      if (!size || size > 12000) return 9;
      const auto buffer = block + 512, io = block + 64;
      std::memset(base + buffer, 0xA5, size);
      const auto sync = call(buffer, 0);
      std::cout << "sync=" << std::hex << sync << " header=" << get(buffer) << "," << get(buffer + 4) << std::endl;
      std::memset(base + buffer, 0xA5, size);
      const auto async = call(buffer, io);
      std::cout << "async=" << std::hex << async << " result=" << get(io)
                << " extended=" << get(io + 24) << " header=" << get(buffer) << "," << get(buffer + 4) << std::endl;
      complete = true;
      return 0;
    }));
    if (thread->Create()) return 10;
    uint64_t timeout = uint64_t(-100'000'000ll);
    if (thread->Wait(0, 0, 0, &timeout)) ExitProcess(11);
    memory->SystemHeapFree(block);
    return complete ? 0 : 12;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 13;
  }
}
