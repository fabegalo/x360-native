#include "tool_sdk.h"
// Explicit headless check of the original notification constructor/queue or
// compositor rearming, no entrypoint, GPU, input, profiles or gameplay.
// Caller supplies verified local assets; no achievement is unlocked here.
#include "../supervisor/scratch.h"
#include "../../titles/common/system_ui_context.h"
#include "../../titles/dash_17559/src/original_assets.h"
#include "../../titles/dash_17559/src/system_ui_compositor.h"
#include <rex/xam/task_context.h>
#include <iostream>
#include <windows.h>

int main(int argc, char** argv) {
  const bool rearm = argc == 4 && std::string(argv[3]) == "--compositor-rearm";
  if (argc != 3 && !rearm) return 2;
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  for (const auto& asset : dash17559::kOriginalAssets) {
    if (asset.path != "system/xam_ui.xex") continue;
    const auto error = x360n::VerifyDashboardAssets(argv[1], "17559", {&asset, 1});
    if (!error.empty()) { std::cerr << error << std::endl; return 8; }
  }
  const auto work = UniqueTempPath("notification17559");
  if (!std::filesystem::create_directory(work)) return 9;
  struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
  } cleanup{work};
  x360n::DiagnosticToolApp app("notification_17559_probe",work);
  if(!app.Start())return 5;
  auto tool=app.Open(std::filesystem::absolute(argv[1]));if(!tool)return 5;
  auto& rt=tool->GetRuntime();
  const auto dll = std::filesystem::absolute(argv[2]).replace_extension().string();
  rt.GetModuleRegistry().RegisterRecompiledModule("xam_ui.xex", "game:\\system\\xam_ui.xex", dll.c_str());
  auto module = rt.GetModuleRegistry().LoadUserModule("game:\\system\\xam_ui.xex", false);
  if (!module) return 4;
  uint32_t presenter = 0;
  auto caller = rt.GetThreadRegistry().LaunchHostThread("notification_17559 probe",512*1024,[&] {
    x360n::SystemUiContext context(*(&rex::kernel::GetAddressSpace(rt)));
    auto* dispatcher = (&rex::kernel::GetFunctionDispatcher(rt));
    if (rearm) {
      auto read = [&](uint32_t address) {
        return rex::memory::load_and_swap<uint32_t>((&rex::kernel::GetAddressSpace(rt))->TranslateVirtual(address));
      };
      auto put = [&](uint32_t address, uint32_t value) {
        rex::memory::store_and_swap<uint32_t>((&rex::kernel::GetAddressSpace(rt))->TranslateVirtual(address), value);
      };
      const auto device = (&rex::kernel::GetAddressSpace(rt))->SystemHeapAlloc(128);
      const auto commands = (&rex::kernel::GetAddressSpace(rt))->SystemHeapAlloc(4096);
      if (!device || !commands) return 0;
      put(0x81AA1B14, device);
      put(device + 48, commands);
      put(device + 56, commands + 4096);
      put(0x81AA1868, 0); // compositor's programmed enable shadow
      put(0x81AA186C, 0); // requested enable
      const auto prior_worker = read(0x81AA09F0);
      bool correct = true;
      for (const uint32_t enabled : {1u, 0u, 1u, 0u, 1u}) {
        const auto before = read(device + 48);
        {
          dash17559::SystemCompositorContext compositor(*(&rex::kernel::GetAddressSpace(rt)), context.thread_state());
          uint64_t enable[]{enabled};
          dispatcher->ExecuteTrap(context.thread_state(), 0x816C7D00, enable, 1);
        }
        const auto after = read(device + 48);
        const bool emitted = after == before + 40 && read(before + 28) == 6240 &&
            read(before + 32) == enabled && read(0x81AA1868) == enabled;
        correct &= emitted && read(0x81AA09F0) == prior_worker;
        std::cout << "overlay_enable=" << enabled << " emitted=" << emitted
                  << " command_bytes=" << (after - before) << std::endl;
      }
      put(0x81AA1B14, 0);
      (&rex::kernel::GetAddressSpace(rt))->SystemHeapFree(commands);
      (&rex::kernel::GetAddressSpace(rt))->SystemHeapFree(device);
      presenter = correct ? 1 : 0;
      return 0;
    }
    uint64_t args[]{240};
    const auto allocation = dispatcher->ExecuteTrap(context.thread_state(), 0x816E6B18, args, 1);
    std::cout << "allocation=" << std::hex << allocation << std::endl;
    if (allocation) (&rex::kernel::GetAddressSpace(rt))->SystemHeapFree(uint32_t(allocation));
    presenter = uint32_t(dispatcher->ExecuteTrap(context.thread_state(), 0x816AB9C0, nullptr, 0));
    std::cout << "presenter=" << std::hex << presenter << " singleton="
              << rex::memory::load_and_swap<uint32_t>((&rex::kernel::GetAddressSpace(rt))->TranslateVirtual(0x81AA07A8)) << std::endl;
    if (!presenter) return 0;
    const auto request = (&rex::kernel::GetAddressSpace(rt))->SystemHeapAlloc(128);
    if (!request) { presenter = 0; return 0; }
    auto put = [&](uint32_t address, uint32_t value) {
      rex::memory::store_and_swap<uint32_t>((&rex::kernel::GetAddressSpace(rt))->TranslateVirtual(address), value);
    };
    put(request, 27); put(request + 8, 2); put(request + 12, request + 40);
    rex::memory::store_and_swap<uint16_t>((&rex::kernel::GetAddressSpace(rt))->TranslateVirtual(request + 40), '1');
    const auto task = dispatcher->ExecuteTrap(context.thread_state(), 0x816B8098, nullptr, 0);
    std::cout << "current_task=" << std::hex << task << std::endl;
    uint64_t queue_args[]{presenter, 27, 0, 2, request + 40, 0, 0, 0};
    const auto queued = dispatcher->ExecuteTrap(context.thread_state(), 0x816ABFC0, queue_args, 8);
    std::cout << "task=" << std::hex << task << " queue=" << uint32_t(queued) << std::endl;
    const auto hosted = rex::xam::RunSystemTask([&] {
      return uint32_t(dispatcher->ExecuteTrap(context.thread_state(), 0x816ABFC0, queue_args, 8));
    });
    std::cout << "hosted_queue=" << std::hex << hosted << std::endl;
    auto read = [&](uint32_t address) {
      return rex::memory::load_and_swap<uint32_t>((&rex::kernel::GetAddressSpace(rt))->TranslateVirtual(address));
    };
    const auto node = read(presenter + 72);
    const auto count = read(presenter + 88);
    bool copied = false;
    if (node && node != presenter + 72 && count == 1) {
      const auto text = read(node + 20);
      copied = read(node + 8) == 27 && text && text != request + 40 &&
          rex::memory::load_and_swap<uint16_t>((&rex::kernel::GetAddressSpace(rt))->TranslateVirtual(text)) == '1';
    }
    std::cout << "queued_items=" << count << " owns_text=" << copied << std::endl;
    if (uint32_t(queued) != 0x8007000E || hosted != 0 || !copied) presenter = 0;
    (&rex::kernel::GetAddressSpace(rt))->SystemHeapFree(request);
    return 0;
  });
  if (!caller) return 5;
  uint64_t timeout = uint64_t(-100'000'000ll);
  if (rt.GetThreadRegistry().GetObjectWaiter().WaitGuestObject(*caller, 0, 0, 0, &timeout)) ExitProcess(6);
    rex::kernel::CloseOwned(rt.GetObjectTable(),*caller);
  return presenter ? 0 : 7;
}
