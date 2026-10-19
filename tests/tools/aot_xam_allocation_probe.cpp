// Explicit, headless diagnostic of the original 1888 XUI realloc body.
// Never executes XAM's module entrypoint, creates a window, starts the dashboard
// or runs a game. Optional static/core XUI checks use a null renderer.
#include "paths.h"
#include "../supervisor/scratch.h"
#include "../../titles/dash_1888/src/system_ui_statics.h"
#include "../../titles/dash_1888/src/system_ui.h"
#include "../../titles/dash_1888/src/original_assets.h"
#include "../../titles/common/system_ui_context.h"
#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/image_info.h>
#include <rex/ppc/func.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/xmemory.h>
#include <rex/system/xex_module.h>
#include <rex/system/user_module.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>
#include <rex/cvar.h>
#include <rex/audio/audio_system.h>
#include <rex/audio/audio_driver.h>
#include <rex/input/input_system.h>
#include <rex/input/input_driver.h>
#include <rex/input/device_assignment.h>
#include <windows.h>
#include <algorithm>
#include <iostream>
#include <map>
#include <fstream>
#include <vector>
#include <cstring>
#include <atomic>

using rex::X_STATUS;
// This explicit diagnostic never installs the boot-intro hook.
namespace dash1888 { bool BootIntroRunning() { return false; } }
struct AllocatorCollector final : rex::runtime::IModuleRegistrar {
  std::map<uint32_t, PPCFunc*> functions;
  bool SetFunction(uint32_t address, PPCFunc* fn) override {
    return address && fn && functions.emplace(address, fn).second;
  }
};
// Diagnostic sink: keep the real SDK's client and callback lifecycle, while
// sending no samples to an OS audio device. Production audio is unchanged.
class ProbeSilentDriver final : public rex::audio::AudioDriver {
 public:
  using AudioDriver::AudioDriver;
  void SubmitFrame(uint32_t) override {}
};
class ProbeAudio final : public rex::audio::AudioSystem {
 public:
  explicit ProbeAudio(rex::runtime::FunctionDispatcher* dispatcher) : AudioSystem(dispatcher) {}
  rex::X_STATUS CreateDriver(size_t, rex::thread::Semaphore*, rex::audio::AudioDriver** out) override {
    *out = new ProbeSilentDriver(memory());
    return X_STATUS_SUCCESS;
  }
  void DestroyDriver(rex::audio::AudioDriver* driver) override { delete driver; }
};
// A scripted controller for the BACK+START opening boundary comparison. No
// host device is opened and no event is injected into the desktop session.
class ProbeOpeningChord final : public rex::input::InputDriver {
 public:
  explicit ProbeOpeningChord(bool reversed, bool delayed = false)
      : InputDriver(nullptr, 0), reversed_(reversed), delayed_(delayed) {}
  rex::X_STATUS Setup() override { return X_STATUS_SUCCESS; }
  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override {
    out.push_back({rex::input::DeviceId(1), 0, "scripted opening chord", "", false});
  }
  rex::X_RESULT GetDeviceState(rex::input::DeviceId, rex::input::X_INPUT_STATE* out) override {
    *out = {};
    out->gamepad.buttons = rex::input::X_INPUT_GAMEPAD_GUIDE |
        rex::input::X_INPUT_GAMEPAD_BACK | rex::input::X_INPUT_GAMEPAD_START;
    return 0;
  }
  rex::X_RESULT GetDeviceCapabilities(rex::input::DeviceId, uint32_t, rex::input::X_INPUT_CAPABILITIES*) override { return 0; }
  rex::X_RESULT SetDeviceVibration(rex::input::DeviceId, rex::input::X_INPUT_VIBRATION*) override { return 0; }
  rex::X_RESULT GetDeviceKeystroke(rex::input::DeviceId, uint32_t, rex::input::X_INPUT_KEYSTROKE* out) override {
    if (next_ == 4) return 0x10D2; // X_ERROR_EMPTY.
    // Separate physical transitions across frames. Delivering the whole chord
    // during the initial transparent scene missed real UI actions on reopening.
    if (delayed_ && polls_++ < 60u + next_ * 8u) return 0x10D2;
    *out = {};
    out->virtual_key = bool(next_ % 2) != reversed_ ? 0x5814 : 0x5815; // START, BACK.
    out->flags = next_ < 2 ? rex::input::X_INPUT_KEYSTROKE_KEYDOWN : rex::input::X_INPUT_KEYSTROKE_KEYUP;
    std::cout << "scripted chord delivered key=" << std::hex << uint16_t(out->virtual_key)
              << " flags=" << uint16_t(out->flags) << " lr="
              << (rex::system::XThread::IsInThread() ?
                  rex::system::XThread::GetCurrentThread()->thread_state()->context()->lr : 0)
              << std::dec << '\n';
    ++next_;
    return 0;
  }
  unsigned delivered() const { return next_; }
 private:
  unsigned next_ = 0;
  unsigned polls_ = 0;
  bool reversed_ = false;
  bool delayed_ = false;
};
int main(int argc, char** argv) {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
  const bool device_bootstrap = argc == 4 && std::string(argv[3]) == "device-bootstrap";
  const bool service_signin = argc == 4 && std::string(argv[3]) == "service-signin";
  const bool service_guide = argc == 4 && std::string(argv[3]) == "service-guide";
  const bool service_cycle = argc == 4 && std::string(argv[3]) == "service-cycle";
  const bool draw_cycle = argc == 4 && std::string(argv[3]) == "service-draw-cycle";
  const bool opening_chord_reversed = argc == 4 && std::string(argv[3]) == "service-guide-chord-reversed";
  const bool opening_chord_delayed = argc == 4 && std::string(argv[3]) == "service-guide-chord-delayed";
  const bool opening_chord_raw = opening_chord_delayed || opening_chord_reversed || (argc == 4 && std::string(argv[3]) == "service-guide-chord-raw");
  const bool guide_animation = opening_chord_raw ||
      (argc == 4 && std::string(argv[3]) == "service-guide-animation");
  const bool service = service_signin || service_guide || service_cycle || draw_cycle || guide_animation;
  rex::LogConfig logging;
  logging.log_to_console = true;
  rex::InitLogging(logging);
  AddVectoredExceptionHandler(0, [](EXCEPTION_POINTERS* exception) -> LONG {
    if (exception->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        rex::system::XThread::IsInThread()) {
      auto* ctx = rex::system::XThread::GetCurrentThread()->thread_state()->context();
      std::cerr << "diagnostic exception guest LR=" << std::hex << ctx->lr
                << " r3=" << ctx->r3.u32 << " r4=" << ctx->r4.u32
                << " r5=" << ctx->r5.u32 << " r6=" << ctx->r6.u32 << '\n' << std::flush;
    }
    return EXCEPTION_CONTINUE_SEARCH;
  });
  if (argc < 2 || argc > 4 || (argc == 4 && !device_bootstrap && !service && std::string(argv[3]) != "omit-statics")) {
    std::cerr << "usage: xam_allocation_probe dash_1888_xam.dll [memory-image.bin [omit-statics] | original-xam.xex device-bootstrap|service-signin|service-guide|service-cycle|service-draw-cycle|service-guide-animation|service-guide-chord-raw|service-guide-chord-reversed|service-guide-chord-delayed]\n";
    return 2;
  }
  const auto dll = std::filesystem::absolute(x360n::PathFromUtf8(argv[1]));
  const auto library = LoadLibraryExW(dll.c_str(), nullptr,
      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if (!library) { std::cerr << "LoadLibrary error=" << GetLastError() << '\n'; return 3; }
  struct Unload { HMODULE library; ~Unload() { FreeLibrary(library); } } unload{library};
  const auto image_fn = reinterpret_cast<const rex::PPCImageInfo* (*)()>(GetProcAddress(library, "ReXModule_GetImageInfo"));
  const auto register_fn = reinterpret_cast<rex::runtime::FunctionDispatcher::RegisterFn>(GetProcAddress(library, "ReXModule_Register"));
  if (!image_fn || !register_fn || image_fn()->image_base != 0x81870000 ||
      image_fn()->image_size != 0x310000 || image_fn()->code_base != 0x818C0000) return 4;
  AllocatorCollector collector;
  register_fn(&collector);
  if (!collector.functions.contains(0x818E7EA0)) return 4;
  const auto found = collector.functions.find(0x81907EF0);
  if (found == collector.functions.end()) return 5;
  HMODULE owner = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(found->second), &owner) || owner != library) return 5;
  const auto work = UniqueTempPath("1888_allocator");
  std::filesystem::create_directories(work);
  struct Cleanup { std::filesystem::path p; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(p, ec); } } cleanup{work};
  const auto originals = service ? std::filesystem::absolute(x360n::PathFromUtf8(argv[2])).parent_path() : work;
  if (service) {
    const auto error = x360n::VerifyDashboardAssets(originals, "2.0.1888.0", dash1888::kOriginalAssets);
    if (!error.empty()) { std::cerr << error << '\n'; return 9; }
  }
  rex::Runtime rt(originals, work, {}, work / "cache", work / "metadata");
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  ProbeOpeningChord* opening_pad = nullptr;
  if (guide_animation) config.input_factory = [opening_chord_raw, opening_chord_reversed, opening_chord_delayed, &opening_pad](bool) {
    // No host drivers: the real input queue is empty and never samples a pad.
    auto input = std::make_unique<rex::input::InputSystem>(nullptr);
    if (opening_chord_raw) {
      auto driver = std::make_unique<ProbeOpeningChord>(opening_chord_reversed, opening_chord_delayed);
      opening_pad = driver.get();
      input->AddDriver(std::move(driver));
      input->SetDeviceAssignment(std::make_unique<rex::input::SlotAssignment>());
    }
    return std::unique_ptr<rex::system::IInputSystem>(std::move(input));
  };
  if (device_bootstrap || service) config.audio_factory = [](rex::runtime::FunctionDispatcher* dispatcher) {
    return std::unique_ptr<rex::system::IAudioSystem>(new ProbeAudio(dispatcher));
  };
  if (rt.Setup(std::move(config)) != X_STATUS_SUCCESS) return 6;
  if (opening_chord_raw)
    rex::cvar::SetFlagByName("dash1888_ui_input_trace", "true");
  if (service) rex::cvar::SetFlagByName("dash1888_ui_lifecycle_trace", "true");
  auto* memory = rt.memory();
  auto* base = memory->virtual_membase();
  if (service) {
    auto system_root = std::make_unique<rex::filesystem::HostPathDevice>("\\SystemRoot", originals, true);
    if (!system_root->Initialize() || !rt.file_system()->RegisterDevice(std::move(system_root))) return 9;
    rt.file_system()->RegisterSymbolicLink("media:", "game:");
    if (service_cycle || draw_cycle) {
      rex::cvar::SetFlagByName("user_language", "9");
      rex::cvar::SetFlagByName("user_country", "13");
    }
    const auto library_path = x360n::PathUtf8(dll);
    rt.kernel_state()->RegisterRecompiledModule("xam.xex", "game:\\xam.xex", library_path.c_str());
    // Load only the dashboard's data/header for the real title identity. Its
    // entrypoint and functions are never executed by this service diagnostic.
    auto dashboard = rt.kernel_state()->LoadUserModule("game:\\dash.xex", false);
    if (!dashboard) return 9;
    rt.kernel_state()->SetExecutableModule(dashboard);
    dash1888::InstallSystemUi(rt);
    uint32_t result = ~0u, scene = 0;
    std::cout << "diagnostic_only=true gui=false dashboard_entry_execution=false isolated_user_data=true\n"
              << "original service=" << (draw_cycle ? "draw-cycle-pt-br" : service_cycle ? "cycle-pt-br" : service_signin ? "signin" : "guide") << '\n' << std::flush;
    auto caller = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
        rt.kernel_state(), 512 * 1024, 0, [&] {
      x360n::SystemUiContext system_context(*memory);
      if (service_cycle || draw_cycle) {
        for (const auto message : {0x21001u, 0x21015u, 0x2100Eu, 0x21001u, 0x21015u}) {
          result = dash1888::ProbeOriginalApplication(rt, message, &scene, draw_cycle);
          std::cout << "lifecycle message=" << std::hex << message << " result=" << result << " scene=" << scene << '\n' << std::flush;
          if (result || !scene) break;
        }
      } else {
        // Reopen the original Guide, not just its first instance. No sign-in
        // or physical input is needed for this animation lifecycle regression.
        for (unsigned iteration = 0; iteration < (guide_animation ? 3u : 1u); ++iteration) {
          result = dash1888::ProbeOriginalApplication(rt, service_signin ? 0x21001 : 0x21015,
                                                       &scene, guide_animation, guide_animation);
          if (result || !scene) break;
        }
      }
      return 0;
    }));
    if (caller->Create() != X_STATUS_SUCCESS) return 12;
    uint64_t timeout = uint64_t(-100'000'000ll);
    if (caller->Wait(0, 0, 0, &timeout) != X_STATUS_SUCCESS) {
      std::cout << "FAIL original application lifecycle timed out\n" << std::flush;
      ExitProcess(12);
    }
    rt.audio_system()->QuiesceTitleCallbacks();
    rt.audio_system()->Shutdown();
    dash1888::ReleaseSystemUi();
    std::cout << "original application result=" << std::hex << result << " scene=" << scene << '\n';
    if (result || !scene) return 13;
    if (opening_chord_raw && (!opening_pad || opening_pad->delivered() != 4)) {
      std::cerr << "FAIL opening chord was not fully delivered to the original UI\n";
      return 14;
    }
    std::cout << "PASS original application scene created and destroyed; CPU draw=" << (draw_cycle || guide_animation)
              << "; presentation/physical input not exercised\n";
    return 0;
  }
  const auto stack = memory->SystemHeapAlloc(16384);
  if (!stack) return 6;
  PPCContext ctx{};
  ctx.r1.u64 = stack + 16384 - 256;
  auto realloc_original = [&](uint32_t pointer, uint32_t size) {
    ctx.r3.u64 = pointer; ctx.r4.u64 = size;
    found->second(ctx, base);
    return ctx.r3.u32;
  };
  std::cout << "diagnostic_only=true gui=false xam_entry_execution=false body=81907EF0\n" << std::flush;
  const auto allocation = realloc_original(0, 32);
  if (!allocation) return 7;
  for (unsigned i = 0; i < 32; ++i) base[allocation + i] = uint8_t(i + 1);
  // Cross a committed-page boundary: the old stub returned the input address
  // as a size and made the real XUI body read past its previous allocation.
  const auto large = realloc_original(allocation, 4097);
  if (!large) return 7;
  for (unsigned i = 0; i < 32; ++i) if (base[large + i] != uint8_t(i + 1)) return 8;
  // The newly committed byte must remain zero. An over-read can hit the new
  // allocation itself when adjacent and silently copy its first byte here.
  if (base[large + 4096] != 0) return 8;
  const auto shrunk = realloc_original(large, 7);
  if (!shrunk) return 7;
  for (unsigned i = 0; i < 7; ++i) if (base[shrunk + i] != uint8_t(i + 1)) return 8;
  if (realloc_original(shrunk, 0)) return 8;
  if (argc >= 3) {
    std::ifstream input(x360n::PathFromUtf8(argv[2]), std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || input.tellg() > 16 * 1024 * 1024 ||
        (!device_bootstrap && input.tellg() != 0x310000)) return 9;
    std::vector<uint8_t> image(static_cast<size_t>(input.tellg()));
    input.seekg(0); input.read(reinterpret_cast<char*>(image.data()), image.size());
    if (!input) return 9;
    std::unique_ptr<rex::runtime::XexModule> module;
    if (device_bootstrap) {
      module = std::make_unique<rex::runtime::XexModule>(rt.function_dispatcher(), rt.kernel_state());
      if (!module->Load("xam.xex", "game:\\xam.xex", image.data(), image.size()) ||
          module->base_address() != 0x81870000 || module->image_size() != 0x310000 ||
          !module->LoadContinue()) return 9;
    } else {
      if (image[0] != 'M' || image[1] != 'Z') return 9;
      auto* heap = memory->LookupHeap(0x81870000);
      if (!heap->AllocFixed(0x81870000, image.size(), heap->page_size(),
          rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit,
          rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite)) return 9;
      std::memcpy(base + 0x81870000, image.data(), image.size());
    }
    const auto* layout = image_fn();
    if (!rt.function_dispatcher()->InitializeFunctionTable(layout->code_base, layout->code_size,
        layout->image_base, layout->image_size, true, true)) return 9;
    rt.function_dispatcher()->RegisterModule("xam-static-probe", layout->code_base, register_fn);
    const auto word = [&](uint32_t address) { return rex::memory::load_and_swap<uint32_t>(base + address); };
    if (word(0x81AB1208) || word(0x81AB1228)) return 10;
    for (const auto& initializer : dash1888::kUiStaticInitializers) {
      if (argc == 4 && !device_bootstrap) break; // Explicit negative control, never a production option.
      const auto fn = collector.functions.find(initializer.function);
      if (fn == collector.functions.end()) return 10;
      ctx.r3.u64 = initializer.object;
      fn->second(ctx, base);
    }
    if (argc == 3 || device_bootstrap) {
      if (!word(0x81AB1208) || !word(0x81AB1228)) return 10;
      const auto text_vtable = word(0x81AB1228);
      if (text_vtable < 0x81870000 || uint64_t(text_vtable) + 4 > 0x818C0000 ||
          !collector.functions.contains(word(text_vtable))) return 10;
      std::cout << "PASS original XUI statics: resource backing allocated, text vtable initialized\n";
    }
    // The original XuiInit accepts a null renderer and initializes its core and
    // text services independently. Use the same callback as 818DAED0; no D3D
    // device, presentation loop or user input exists in this tool-mode runtime.
    std::cout << "original XUI core init, statics=" << (argc == 3 || device_bootstrap) << '\n' << std::flush;
    const auto initialize = collector.functions.find(0x81938E18);
    if (initialize == collector.functions.end()) return 11;
    uint32_t result = ~0u;
    std::atomic<DWORD> worker_id = 0;
    auto caller = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
        rt.kernel_state(), 128 * 1024, 0, [&] {
      worker_id = GetCurrentThreadId();
      x360n::SystemUiContext system_context(*memory);
      const auto call = [&](uint32_t address) {
        std::cout << "starting " << std::hex << address << '\n' << std::flush;
        const auto value = rt.function_dispatcher()->ExecuteTrap(system_context.thread_state(), address, nullptr, 0);
        std::cout << "completed " << std::hex << address << " result=" << uint32_t(value) << '\n' << std::flush;
        return uint32_t(value);
      };
      if (device_bootstrap) {
        if (call(0x818DADC8) || !word(0x81AAED38)) { result = 13; return 0; }
        if (call(0x818DC680)) { result = 13; return 0; }
        result = call(0x818DAED0);
        return 0;
      }
      uint64_t args[]{0, 0, 0x81939B60};
      result = uint32_t(rt.function_dispatcher()->ExecuteTrap(
          system_context.thread_state(), 0x81938E18, args, 3));
      return 0;
    }));
    if (caller->Create() != X_STATUS_SUCCESS) return 12;
    uint64_t timeout = uint64_t(-50'000'000ll);
    if (caller->Wait(0, 0, 0, &timeout) != X_STATUS_SUCCESS) {
      // Inspect only this diagnostic's own bounded worker, never another app.
      auto worker = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                               FALSE, worker_id.load());
      if (worker && SuspendThread(worker) != DWORD(-1)) {
        CONTEXT context{}; context.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(worker, &context)) {
          for (unsigned i = 0; i < 8 && context.Rip; ++i) {
            std::cout << "timeout frame=" << i << " pc=" << std::hex << context.Rip;
            uintptr_t nearest = 0; uint32_t guest = 0;
            for (const auto& [address, fn] : collector.functions) {
              const auto native = reinterpret_cast<uintptr_t>(fn);
              if (native <= context.Rip && native > nearest) { nearest = native; guest = address; }
            }
            if (nearest && context.Rip - nearest < 0x10000)
              std::cout << " nearest_registered=" << guest << "+" << context.Rip - nearest;
            std::cout << '\n';
            DWORD64 image_base = 0;
            auto* unwind = RtlLookupFunctionEntry(context.Rip, &image_base, nullptr);
            if (!unwind) break;
            void* handler_data = nullptr; DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context.Rip, unwind,
                            &context, &handler_data, &establisher, nullptr);
          }
        }
        std::cout << "guest LR=" << std::hex << caller->thread_state()->context()->lr << '\n' << std::flush;
        ResumeThread(worker);
      }
      if (worker) CloseHandle(worker);
      std::cout << "FAIL: bounded XUI diagnostic worker timed out\n" << std::flush;
      ExitProcess(12);
    }
    std::cout << "XUI core result=" << std::hex << result << '\n';
    if (result) return 11;
    std::cout << "PASS original XUI core initialized without graphics\n";
    if (device_bootstrap) {
      rt.audio_system()->QuiesceTitleCallbacks();
      rt.audio_system()->Shutdown();
    }
    rt.function_dispatcher()->UnregisterModule("xam-static-probe");
  }
  memory->SystemHeapFree(stack);
  std::cout << "PASS original XUI realloc: allocate, grow across page, shrink, free; bytes preserved\n";
}
