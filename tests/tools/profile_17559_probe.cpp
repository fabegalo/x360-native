// Explicit headless original-code contract check. No UI entrypoint, renderer,
// input, games or existing user storage. Assets are checked before loading.
#include <cstdio>
#include "../supervisor/scratch.h"
#include "../../titles/dash_17559/src/original_assets.h"
#include "../../titles/common/system_ui_context.h"
#include "../../titles/dash_17559/src/system_ui_saved_state.h"
#include "../sdkcheck/offline_achievement_fixture.h"
#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/kernel/xam/module.h>
#include <rex/kernel/xam/offline_achievement_helpers.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/user_module.h>
#include <rex/system/xthread.h>
#include <rex/system/offline_achievement_store.h>
#include <chrono>
#include <thread>
#include <cstring>
#include <iostream>
#include <windows.h>

extern "C" REX_FUNC(__imp__XamParseGamerTileKey);

int main(int argc, char** argv) {
  // Diagnostic: <originals> <binaries> --achievements-real-copy <user_root> <xuid> <title>.
  // Copies the profile package, session and metadata into the temporary root; the
  // source is only read. Runs the same original Games/Achievements operations.
  const bool real_copy = argc == 7 && std::string(argv[3]) == "--achievements-real-copy";
  if (argc != 3 && argc != 4 && !real_copy) return 2;
  const bool preferences_only = argc == 4 && std::string(argv[3]) == "--signin-preferences-only";
  const bool achievements_only = real_copy || (argc == 4 && std::string(argv[3]) == "--achievement-contracts-only");
  const bool tile_only = argc == 4 && std::string(argv[3]) == "--profile-tile-contracts-only";
  const bool close_only = argc == 4 && std::string(argv[3]) == "--profile-close-contracts-only";
  const bool saved_only = argc == 4 && std::string(argv[3]) == "--signin-saved-state-contracts-only";
  const bool collation_only = argc == 4 && std::string(argv[3]) == "--xam-collation-contracts-only";
  if (argc == 4 && !preferences_only && !achievements_only && !tile_only && !close_only && !saved_only && !collation_only) return 2;
  uint64_t real_xuid = 0;
  uint32_t real_title = 0;
  if (real_copy) {
    real_xuid = std::stoull(argv[5], nullptr, 16);
    real_title = uint32_t(std::stoul(argv[6], nullptr, 16));
  }
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  const auto originals = std::filesystem::absolute(argv[1]);
  const auto binaries = std::filesystem::absolute(argv[2]);
  for (const auto& asset : dash17559::kOriginalAssets) {
    if (asset.path != "system/xam_ui.xex" && asset.path !=
        (achievements_only ? "system/gamerprofile.xex" : saved_only ? "system/signin.xex" : "system/createprofile.xex")) continue;
    const auto error = x360n::VerifyDashboardAssets(originals, "17559", {&asset, 1});
    if (!error.empty()) { std::cerr << error << '\n'; return 3; }
  }
  const auto work = UniqueTempPath("profile17559");
  if (!std::filesystem::create_directory(work)) return 4;
  struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
  } cleanup{work};
  if (real_copy) {
    const auto source = std::filesystem::absolute(argv[4]);
    std::error_code ec;
    // The profile package (account, unlocks, settings) and the session.
    char owner[17];
    std::snprintf(owner, sizeof(owner), "%016llX", static_cast<unsigned long long>(real_xuid));
    const auto profile_title = std::filesystem::path(owner) / "FFFE07D1";
    std::filesystem::create_directories(work / owner, ec);
    for (const auto& dir : {std::filesystem::path("profiles"), std::filesystem::path("offline-achievements-v1"), profile_title}) {
      std::filesystem::copy(source / dir, work / dir, std::filesystem::copy_options::recursive, ec);
      if (ec) { std::cerr << "copy " << dir.string() << ": " << ec.message() << '\n'; return 4; }
    }
  }
  rex::Runtime rt(originals, work, {}, work / "cache", work / "metadata");
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  if (rt.Setup(std::move(config))) return 5;
  auto* state = rt.kernel_state();
  for (const char* name : {"xam_ui", achievements_only ? "gamerprofile" : saved_only ? "signin" : "createprofile"}) {
    const auto dll = (binaries / (std::string("dash_17559_") + name + ".dll")).string();
    const auto path = std::string("game:\\system\\") + name + ".xex";
    const auto file = std::string(name) + ".xex";
    state->RegisterRecompiledModule(file.c_str(), path.c_str(), dll.c_str());
  }
  auto xam = state->LoadUserModule("game:\\system\\xam_ui.xex", false);
  auto create = state->LoadUserModule(achievements_only ? "game:\\system\\gamerprofile.xex" :
      saved_only ? "game:\\system\\signin.xex" : "game:\\system\\createprofile.xex", false);
  if (!xam || !create) return 6;
  // The headless operation probe needs a process owner to start the normal
  // deferred-completion worker. No module entrypoint is executed.
  if (achievements_only) state->SetExecutableModule(create);
  for (auto ordinal : {0x233, 0x236, 0x2D0, 0x3BC, 0x3C7})
    std::cout << "original-export " << std::hex << ordinal << "="
              << xam->GetProcAddressByOrdinal(uint16_t(ordinal)) << std::endl;
  bool passed = true;
  auto caller = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
      state, 512 * 1024, 0, [&] {
    x360n::SystemUiContext context(*rt.memory());
    auto* dispatcher = rt.function_dispatcher();
    auto* memory = rt.memory();
    auto* profile = state->user_profile();
    const auto object = memory->SystemHeapAlloc(4096);
    const auto login = memory->SystemHeapAlloc(128);
    if (!object || !login) { passed = false; return 7; }
    auto put = [&](uint32_t address, uint32_t value) {
      rex::memory::store_and_swap<uint32_t>(memory->TranslateVirtual(address), value);
    };
    auto get = [&](uint32_t address) {
      return rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(address));
    };
    if (collation_only) {
      // Original 17559 XamUniSortCmpString (816E35A8): (str1, str2, count, flags).
      const auto a = memory->SystemHeapAlloc(64), b = memory->SystemHeapAlloc(64);
      auto set = [&](uint32_t at, std::u16string_view text) {
        for (size_t i = 0; i <= text.size(); ++i)
          rex::memory::store_and_swap<uint16_t>(memory->TranslateVirtual(at + uint32_t(i * 2)), i < text.size() ? text[i] : 0);
      };
      auto compare = [&](std::u16string_view x, std::u16string_view y) {
        set(a, x); set(b, y);
        uint64_t args[]{a, b, 128, 1};  // dash 922B37C4: display names, 128, flags 1
        return int32_t(dispatcher->ExecuteTrap(context.thread_state(), 0x816E35A8, args, 4));
      };
      const int equal_case = compare(u"Forza", u"FORZA"), less = compare(u"Alpha", u"Bravo");
      const int greater = compare(u"bravo", u"Alpha"), empty = compare(u"", u"");
      std::cout << "original-collation equal_case=" << equal_case << " less=" << less
                << " greater=" << greater << " empty=" << empty << std::endl;
      passed = equal_case == 0 && less < 0 && greater > 0 && empty == 0;
      memory->SystemHeapFree(a); memory->SystemHeapFree(b);
      memory->SystemHeapFree(login); memory->SystemHeapFree(object);
      return passed ? 0 : 16;
    }
    if (saved_only) {
      std::memset(memory->TranslateVirtual(object), 0, 4096);
      std::array<uint32_t, 4> originals{}, copies{};
      for (uint32_t i = 0; i < 4; ++i) {
        originals[i] = memory->SystemHeapAlloc(34);
        if (!originals[i]) { passed = false; return 15; }
        std::memset(memory->TranslateVirtual(originals[i]), int(0x31 + i), 34);
        put(object + 336 + 232 * i, originals[i]);
      }
      const auto saved = object + 2048;
      uint64_t args[]{object, saved};
      const auto result = dispatcher->ExecuteTrap(context.thread_state(), 0x90106EC0, args, 2);
      passed &= uint32_t(result) == 0;
      std::vector<uint8_t> state_bytes(956);
      std::memcpy(state_bytes.data(), memory->TranslateVirtual(saved), state_bytes.size());
      for (uint32_t i = 0; i < 4; ++i) {
        copies[i] = get(saved + 252 + 232 * i);
        if (!copies[i] || copies[i] == originals[i] ||
            !rex::kernel::xam::AchievementGuestSpan(memory, copies[i], 34, rex::memory::kMemoryProtectRead)) {
          passed = false; return 15;
        }
        passed &= std::memcmp(memory->TranslateVirtual(originals[i]), memory->TranslateVirtual(copies[i]), 34) == 0;
      }
      dash17559::ReleaseDiscardedSystemUiState(*memory, 0x21001, state_bytes);
      for (uint32_t i = 0; i < 4; ++i) {
        rex::memory::HeapAllocationInfo info{};
        auto* heap = memory->LookupHeap(copies[i]);
        passed &= heap && heap->QueryRegionInfo(copies[i], &info) &&
            (info.state & rex::memory::kMemoryAllocationCommit) == 0;
        passed &= memory->TranslateVirtual<uint8_t*>(originals[i])[33] == 0x31 + i;
        memory->SystemHeapFree(originals[i]);
      }
      std::cout << "original-signin-saved-state result=" << std::hex << uint32_t(result)
                << " copies-released=4 originals-preserved=" << passed << std::endl;
      // Original save explicitly declines suspension for flag 0x100000.
      // S_FALSE does not initialize a serialized state and must not be resumed.
      std::memset(memory->TranslateVirtual(object), 0, 4096);
      put(object + 96, 0x100000);
      std::memset(memory->TranslateVirtual(saved), 0xA5, 956);
      const auto declined = dispatcher->ExecuteTrap(context.thread_state(), 0x90106EC0, args, 2);
      const auto* untouched = memory->TranslateVirtual<uint8_t*>(saved);
      passed &= uint32_t(declined) == 1 && untouched[0] == 0xA5 && untouched[955] == 0xA5;
      std::cout << "original-signin-declines-save result=" << uint32_t(declined)
                << " state-untouched=" << (untouched[0] == 0xA5 && untouched[955] == 0xA5) << std::endl;
      memory->SystemHeapFree(login); memory->SystemHeapFree(object);
      return passed ? 0 : 15;
    }
    if (close_only) {
      auto kernel_xam = state->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
      const auto owner = create->hmodule_ptr();
      passed &= kernel_xam->RegisterSystemApp({owner, 0xFF, 0x90105C98, 14}) == 0;
      for (const uint32_t completion : {1u, 2u}) {
        passed &= kernel_xam->BeginSystemUi(owner, 0xFF, 0);
        std::memset(memory->TranslateVirtual(object), 0, 4096);
        put(object + 84, completion);
        uint64_t args[]{object};
        // Original CreateProfile update's completed-state branch: no scene,
        // input polling, account creation, app entrypoint or renderer.
        const auto result = dispatcher->ExecuteTrap(context.thread_state(), 0x90106398, args, 1);
        const auto session = kernel_xam->GetSystemUiSession();
        passed &= uint32_t(result) == 0 && session.close_requested &&
            session.close_stack_requested == (completion == 2);
        std::cout << "original-profile-close completion=" << completion
                  << " result=" << std::hex << uint32_t(result)
                  << " close=" << session.close_requested
                  << " stack=" << session.close_stack_requested << std::endl;
        passed &= kernel_xam->EndSystemUi(owner);
      }
      memory->SystemHeapFree(login); memory->SystemHeapFree(object);
      return passed ? 0 : 14;
    }
    if (tile_only) {
      for (const std::u16string text : {u"584108B70000000200000003", u"ffffffff0123ABCDfedcba98",
          u"FFFE08540000000200000003", u"0001ABCD0000000200000003", u"000000000000000000000000", u""}) {
        std::memset(memory->TranslateVirtual(object), 0, 4096);
        const auto string = text.empty() ? 0 : object + 512;
        put(object + 12, string); put(object + 128 + 12, string);
        for (size_t i = 0; i < text.size(); ++i)
          rex::memory::store_and_swap<uint16_t>(memory->TranslateVirtual(string + uint32_t(i * 2)), text[i]);
        uint64_t args[]{object, object + 64, object + 68, object + 72};
        const auto original = dispatcher->ExecuteTrap(context.thread_state(), 0x81670A50, args, 4);
        auto& ctx = *context.thread_state()->context();
        ctx.r3.u64 = object + 128; ctx.r4.u64 = object + 192;
        ctx.r5.u64 = object + 196; ctx.r6.u64 = object + 200;
        __imp__XamParseGamerTileKey(ctx, memory->virtual_membase());
        passed &= uint32_t(original) == ctx.r3.u32 && get(object + 8) == get(object + 136);
        for (uint32_t i = 0; i < 3; ++i) passed &= get(object + 64 + i * 4) == get(object + 192 + i * 4);
        std::cout << "original-tile-key length=" << std::hex << get(object + 8)
                  << " components=" << get(object + 64) << '/' << get(object + 68) << '/' << get(object + 72)
                  << " hle=" << get(object + 192) << '/' << get(object + 196) << '/' << get(object + 200) << std::endl;
      }
      memory->SystemHeapFree(login); memory->SystemHeapFree(object);
      return passed ? 0 : 13;
    }
    if (achievements_only) {
      // Test resources only, imported exclusively into this new temporary root.
      // Exercise the actual GamerProfile operation methods, without its entry
      // point, scene creation, UI, games or the user's existing achievement 155.
      constexpr uint32_t title_a = 0x1234C901, title_b = 0x1234C902;
      using Result = rex::system::OfflineAchievementResult;
      auto& ledger = state->offline_achievements();
      passed &= ledger.ImportTitle(title_a, offline_achievement_fixture::Metadata()) == Result::kSuccess;
      passed &= ledger.ImportTitle(title_b, offline_achievement_fixture::Metadata(40)) == Result::kSuccess;
      uint64_t alpha = 0, bravo = 0;
      passed &= profile->CreateLocal("Alpha", &alpha) == 0;
      passed &= profile->CreateLocal("Bravo", &bravo) == 0;
      std::vector<rex::system::OfflineAchievementRecord> unlocked;
      const std::array<uint32_t, 1> ids{1};
      passed &= ledger.UnlockBatch(alpha, title_a, ids, unlocked) == Result::kSuccess;
      auto text = [&](uint32_t address) {
        std::u16string value;
        if (!address) return value;
        for (size_t i = 0; i < 256; ++i) {
          const uint64_t next = uint64_t(address) + i * 2;
          if (next > UINT32_MAX || !rex::kernel::xam::AchievementGuestSpan(
                  memory, uint32_t(next), 2, rex::memory::kMemoryProtectRead))
            return std::u16string{};
          const auto c = rex::memory::load_and_swap<uint16_t>(memory->TranslateVirtual(uint32_t(next)));
          if (!c) break;
          value.push_back(c);
        }
        return value;
      };
      auto wait = [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (get(object + 8) == 997 && std::chrono::steady_clock::now() < deadline)
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return get(object + 8) == 0 && get(object + 12) == 2;
      };
      auto release = [&](uint32_t handle_offset, uint32_t buffer_offset) {
        state->object_table()->RemoveHandle(get(object + handle_offset));
        uint64_t args[]{get(object + buffer_offset)};
        dispatcher->ExecuteTrap(context.thread_state(), 0x9011C7A0, args, 1);
      };
      if (real_copy) {
        auto settle = [&] {
          const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
          while (get(object + 8) == 997 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
          return get(object + 8);
        };
        const auto session = profile->SetLocalSession(real_xuid);
        std::cout << "real-session xuid=" << std::hex << real_xuid << " result=" << session << std::endl;
        std::memset(memory->TranslateVirtual(object), 0, 4096);
        put(object + 44, 0); put(object + 56, 32);
        rex::memory::store_and_swap<uint64_t>(memory->TranslateVirtual(object + 48), real_xuid);
        uint64_t args[]{object};
        auto result = dispatcher->ExecuteTrap(context.thread_state(), 0x90120CD0, args, 1);
        auto overlap = settle();
        std::cout << "real-games-operation result=" << uint32_t(result) << " overlap=" << overlap
                  << " count=" << get(object + 12) << std::endl;
        uint32_t achievement_count = 0;
        const auto titles = get(object + 68);
        for (uint32_t i = 0; titles && overlap == 0 && i < get(object + 12); ++i) {
          const auto t = titles + i * 168;
          std::cout << "real-title id=" << get(t) << " achievements=" << get(t + 4)
                    << " unlocked=" << get(t + 8) << " total=" << get(t + 12) << " earned=" << get(t + 16) << std::endl;
          if (get(t) == real_title) achievement_count = get(t + 4);
        }
        if (titles) release(64, 68);
        for (const uint32_t count : {150u, achievement_count}) {
          if (!count) continue;
          std::memset(memory->TranslateVirtual(object), 0, 4096);
          put(object + 44, real_title); put(object + 48, 0);
          rex::memory::store_and_swap<uint64_t>(memory->TranslateVirtual(object + 56), real_xuid);
          put(object + 64, 0x27); put(object + 68, count);  // LoadAchievementsScene 90117710
          result = dispatcher->ExecuteTrap(context.thread_state(), 0x901200D8, args, 1);
          overlap = settle();
          std::cout << "real-achievements-operation requested=" << std::dec << count << std::hex
                    << " result=" << uint32_t(result) << " overlap=" << overlap
                    << " extended=" << get(object + 8 + 24) << " count=" << get(object + 12) << std::endl;
          const auto list = get(object + 76);
          for (uint32_t i = 0; list && overlap == 0 && i < get(object + 12) && i < 3; ++i) {
            const auto a = list + i * 36;
            std::cout << "real-achievement id=" << std::dec << get(a) << " score=" << get(a + 20)
                      << " flags=" << std::hex << get(a + 32) << std::endl;
          }
          if (get(object + 72)) release(72, 76);
        }
        memory->SystemHeapFree(login); memory->SystemHeapFree(object);
        return 0;
      }
      for (const auto xuid : {alpha, bravo, alpha}) {
        passed &= profile->SetLocalSession(xuid) == 0;
        passed &= profile->RecordTitlePlayed(title_a) && profile->RecordTitlePlayed(title_b);
        std::memset(memory->TranslateVirtual(object), 0, 4096);
        put(object + 44, 0); put(object + 56, 2);
        rex::memory::store_and_swap<uint64_t>(memory->TranslateVirtual(object + 48), xuid);
        uint64_t args[]{object};
        // Original operation creates the enumerator, allocates its requested
        // byte count and issues the original overlapped XamEnumerate call.
        auto result = dispatcher->ExecuteTrap(context.thread_state(), 0x90120CD0, args, 1);
        const bool games_ready = uint32_t(result) == 0x8000000A && wait();
        std::cout << "original-games-operation result=" << std::hex << uint32_t(result)
                  << " overlap=" << get(object + 8) << " count=" << get(object + 12)
                  << " handle=" << get(object + 64) << " size=" << get(object + 72) << std::endl;
        if (!games_ready) { passed = false; break; }
        const auto titles = get(object + 68);
        if (!titles) { passed = false; break; }
        passed &= get(titles) == title_a && get(titles + 168) == title_b;
        passed &= get(titles + 4) == 2 && get(titles + 12) == 35;
        passed &= get(titles + 8) == (xuid == alpha ? 1u : 0u);
        passed &= get(titles + 16) == (xuid == alpha ? 10u : 0u);
        passed &= text(titles + 40) == u"Test resource title";
        std::cout << "original-games xuid=" << std::hex << xuid << " count=" << get(object + 12)
                  << " earned=" << get(titles + 16) << std::endl;
        release(64, 68);
        std::memset(memory->TranslateVirtual(object), 0, 4096);
        put(object + 44, title_a); put(object + 48, 0);
        rex::memory::store_and_swap<uint64_t>(memory->TranslateVirtual(object + 56), xuid);
        put(object + 64, 7); put(object + 68, 2);
        result = dispatcher->ExecuteTrap(context.thread_state(), 0x901200D8, args, 1);
        const bool achievements_ready = uint32_t(result) == 0x8000000A && wait();
        std::cout << "original-achievements-operation result=" << std::hex << uint32_t(result)
                  << " overlap=" << get(object + 8) << " count=" << get(object + 12) << std::endl;
        if (!achievements_ready) { passed = false; break; }
        const auto achievements = get(object + 76);
        if (!achievements) { passed = false; break; }
        passed &= get(achievements) == 1 && get(achievements + 36) == 2;
        passed &= get(achievements + 20) == 10;
        passed &= (get(achievements + 32) & 0x30000) == (xuid == alpha ? 0x20000u : 0u);
        passed &= text(get(achievements + 4)) == u"First";
        passed &= text(get(achievements + 8)) == u"Completed first";
        std::cout << "original-achievements xuid=" << std::hex << xuid
                  << " count=" << get(object + 12) << " flags=" << get(achievements + 32) << std::endl;
        release(72, 76);
      }
      memory->SystemHeapFree(login); memory->SystemHeapFree(object);
      return passed ? 0 : 12;
    }
    if (preferences_only) {
      // The original scene dispatches this callback indirectly (LR 817B3928).
      // An unrelated message exercises its original no-op branch, without
      // constructing a scene or invoking profile/settings writes.
      std::memset(memory->TranslateVirtual(login), 0, 128);
      put(login + 4, 0xFFFFFFFF);
      put(login + 8, 0x13572468);
      uint64_t args[]{0x000806A4, login, object};
      const auto result = dispatcher->ExecuteTrap(context.thread_state(), 0x817ACC88, args, 3);
      const auto handled = rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(login + 8));
      passed = uint32_t(result) == 0 && handled == 0x13572468;
      std::cout << "original-signin-preferences-callback result=" << std::hex
                << uint32_t(result) << " handled=" << handled << std::endl;
      memory->SystemHeapFree(login); memory->SystemHeapFree(object);
      return passed ? 0 : 11;
    }
    uint64_t previous = 0;
    for (const std::u16string name : {u"OriginalOne", u"OriginalTwo"}) {
      std::memset(memory->TranslateVirtual(object), 0, 4096);
      put(object + 44, 1); put(object + 48, 1);
      for (size_t i = 0; i < name.size(); ++i)
        rex::memory::store_and_swap<uint16_t>(memory->TranslateVirtual(object + 60 + uint32_t(i * 2)), name[i]);
      uint64_t args[]{object};
      // Original operation method: constructs the eight-register Create ABI
      // and obtains the result using its own GetCreationStatus call.
      const auto result = dispatcher->ExecuteTrap(context.thread_state(), 0x9010B0B0, args, 1);
      const auto xuid = rex::memory::load_and_swap<uint64_t>(memory->TranslateVirtual(object + 2736));
      std::cout << "original-create result=" << std::hex << uint32_t(result)
                << " xuid=" << xuid << std::endl;
      if (uint32_t(result) || !xuid || xuid == previous) { passed = false; break; }
      std::memset(memory->TranslateVirtual(login), 0, 128);
      put(login + 8, 997); put(login + 80, 0x24);
      rex::memory::store_and_swap<uint64_t>(memory->TranslateVirtual(login + 48), xuid);
      args[0] = login;
      // Original local-logon operation: HRESULT pending in r3, completed
      // XOVERLAPPED at +8; do not substitute a direct host session change.
      const auto pending = dispatcher->ExecuteTrap(context.thread_state(), 0x90103528, args, 1);
      const auto completion = rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(login + 8));
      std::cout << "original-logon result=" << std::hex << uint32_t(pending)
                << " completion=" << completion << " active=" << profile->xuid() << std::endl;
      passed &= uint32_t(pending) == 0x8000000A && completion == 0 && profile->xuid() == xuid;
      previous = xuid;
    }
    memory->SystemHeapFree(login); memory->SystemHeapFree(object);
    return 0;
  }));
  if (caller->Create()) return 8;
  uint64_t timeout = uint64_t(-100'000'000ll);
  if (caller->Wait(0, 0, 0, &timeout)) ExitProcess(9);
  std::cout << "original-profile-contracts=" << (passed ? "pass" : "FAIL") << std::endl;
  return passed ? 0 : 10;
}
