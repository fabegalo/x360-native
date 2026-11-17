// Explicit, headless diagnostic of the original 1888 XUI realloc body.
// Never executes XAM's module entrypoint, creates a window, starts the dashboard
// or runs a game. Optional static/core XUI checks use a null renderer.
#include "paths.h"
#include "../supervisor/scratch.h"
#include "../../titles/dash_1888/src/system_ui_statics.h"
#include "../../titles/dash_1888/src/system_ui.h"
#include "../../titles/dash_1888/src/original_assets.h"
#include "../../titles/common/system_ui_context.h"
#include "tool_sdk.h"
#include <rex/filesystem/file_system.h>
#include <rex/hid/input_system.h>
#include <rex/hid/input_driver.h>
#include <rex/driver_module.h>
#include <rex/apu/audio_system.h>
#include <rex/kernel/title_exit.h>
#include <rex/kernel/kernel_settings.h>
#include <windows.h>
#include <algorithm>
#include <iostream>
#include <map>
#include <fstream>
#include <vector>
#include <cstring>
#include <atomic>

using rex::kernel::X_STATUS;
using namespace rex::kernel;
// This explicit diagnostic never installs the boot-intro hook.
namespace dash1888 { bool BootIntroRunning() { return false; } }
struct AllocatorCollector final : rex::kernel::IFunctionRegistrar {
  std::map<uint32_t, PPCFunc*> functions;
  bool SetFunction(uint32_t address, PPCFunc* fn) override {
    return address && fn && functions.emplace(address, fn).second;
  }
};
// The SDK's no-device audio path still mixes on its timer and delivers callbacks.
// Script raw pad transitions; the canonical HID system creates the guest keystrokes.
class ProbeOpeningChord final : public rex::hid::IInputDriver {
 public:
  inline static bool reversed=false,delayed=false;
  inline static ProbeOpeningChord* active=nullptr;
  std::string_view GetName() const override {return "opening-chord";}
  rex::Result<void> Initialize(rex::IDriverContext&) override {active=this;return {};}
  void Shutdown() override {active=nullptr;}
  rex::u64 GetDeviceRevision() const override {return 1;}
  void EnumerateDevices(std::vector<rex::hid::PadDevice>& out) override {
    out.clear();rex::hid::PadDevice pad;pad.id=rex::hid::PadDeviceID{1};pad.name="scripted opening chord";out.push_back(std::move(pad));
  }
  rex::Result<rex::hid::PadState> GetDeviceState(rex::hid::PadDeviceID) override {
    if(next_<4 && (!delayed || polls_++>=60u+next_*8u)) {
      const auto button=bool(next_%2)!=reversed ? rex::hid::kPadStart : rex::hid::kPadBack;
      if(next_<2)buttons_|=button;else buttons_&=~button;
      std::cout<<"scripted chord transition="<<next_<<" button="<<std::hex<<button<<std::dec<<'\n';++next_;
    }
    rex::hid::PadState state{};state.buttons=buttons_;return state;
  }
  rex::Result<rex::hid::PadCapabilities> GetDeviceCapabilities(rex::hid::PadDeviceID) override {return rex::hid::PadCapabilities{};}
  rex::Result<void> SetDeviceVibration(rex::hid::PadDeviceID,const rex::hid::PadVibration&) override {return {};}
  unsigned delivered() const {return next_;}
 private:
  unsigned next_=0,polls_=0;uint16_t buttons_=rex::hid::kPadGuide;
};
class ProbePadModule final : public rex::IModule,public rex::IDriverModule {
 public:
  std::string_view GetName() const override {return "1888-probe-input";}
  rex::Result<void> Connect(rex::IInterfaceFactory&) override {return {};}
  void Disconnect() override {}
  rex::Result<void> Init() override {return {};}
  void Shutdown() override {}
  rex::IInterface* QueryInterface(std::string_view name,rex::u32 version) override {return rex::FindModuleInterface<rex::IDriverModule>(*this,name,version);}
  std::span<const rex::DriverRegistration> GetDrivers() const override {
    static const std::array drivers{rex::DriverRegistration{rex::DriverType::kInput,"opening-chord",{},[]{return std::unique_ptr<rex::IDriver>(new ProbeOpeningChord);}}};return drivers;
  }
};
class AllocationProbeApp final : public x360n::DiagnosticToolApp {
 public:
  AllocationProbeApp(std::filesystem::path work,bool chord,bool portuguese):DiagnosticToolApp("1888-allocation-probe",work),chord_(chord),portuguese_(portuguese) {}
  ~AllocationProbeApp() override {Shutdown();}
 protected:
  void OnRegisterModules(std::vector<rex::IModule*>& modules) override {ToolApp::OnRegisterModules(modules);modules.push_back(&pad_);}
  rex::Result<void> OnConfigure(rex::SettingsRegistry& settings) override {
    REX_TRY_VOID(DiagnosticToolApp::OnConfigure(settings));
    REX_TRY_VOID(settings.Register(dash1888::GetSystemUiSettings()));
    settings_guard_=rex::ScopeGuard([&settings]{settings.Unregister(dash1888::GetSystemUiSettings());});
    if(chord_)REX_TRY_VOID(GetInput().GetSettings().drivers.SetText(rex::SettingLayer::kTitle,"opening-chord"));
    if(portuguese_) {REX_TRY_VOID(settings.SetText(rex::SettingLayer::kTitle,"kernel.user_language","9"));REX_TRY_VOID(settings.SetText(rex::SettingLayer::kTitle,"kernel.user_country","13"));}
    return rex::Ok();
  }
 private:
  bool portuguese_;bool chord_;ProbePadModule pad_;rex::ScopeGuard settings_guard_;
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
  AddVectoredExceptionHandler(0, [](EXCEPTION_POINTERS* exception) -> LONG {
    if (exception->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        (rex::kernel::KernelThread::GetCallingThread()!=nullptr)) {
      auto* ctx = rex::kernel::KernelThread::GetCallingThread()->GetThreadState()->GetContext();
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
  const auto image_fn = reinterpret_cast<const rex::ppc::PPCImage* (*)()>(GetProcAddress(library, "ReXGetModuleImage"));
  const auto register_fn = reinterpret_cast<rex::kernel::FunctionDispatcher::RegisterFn>(GetProcAddress(library, "ReXRegisterModule"));
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
  ProbeOpeningChord::reversed=opening_chord_reversed;ProbeOpeningChord::delayed=opening_chord_delayed;
  AllocationProbeApp app(work,opening_chord_raw,service_cycle || draw_cycle);
  if(!app.Start())return 6;
  auto set=[](const char* name,const char* value){auto result=REX_SETTINGS().SetText(rex::SettingLayer::kTitle,name,value);return result;};
  if(opening_chord_raw && !set("dash1888.ui_input_trace","true"))return 6;
  if(service && !set("dash1888.ui_lifecycle_trace","true"))return 6;
  static std::string recompiled_library=x360n::PathUtf8(std::filesystem::path(dll).replace_extension());
  rex::ppc::PPCImage descriptor{};
  if(service)descriptor.register_modules=[](rex::kernel::IModuleRegistry& modules) {
    modules.RegisterRecompiledModule("xam.xex","game:\\xam.xex",recompiled_library.c_str());
  };
  rex::kernel::TitleRuntime rt(descriptor);
  if(!rt.GetTitlePaths().game_root.Set(originals) ||
      !rt.GetTitlePaths().entry.Set(service?"dash.xex":"") || !rt.Load())return 6;
  auto* memory=&rex::kernel::GetAddressSpace(rt);
  auto* base = memory->GetVirtualMembase();
  if (service) {
    if(!rt.GetFileSystem().Mount({.path="\\SystemRoot",.source=originals}) ||
       !rt.GetFileSystem().Link("media:","game:"))return 9;
    dash1888::InstallSystemUi(rt);
    uint32_t result = ~0u, scene = 0;
    std::cout << "diagnostic_only=true gui=false dashboard_entry_execution=false isolated_user_data=true\n"
              << "original service=" << (draw_cycle ? "draw-cycle-pt-br" : service_cycle ? "cycle-pt-br" : service_signin ? "signin" : "guide") << '\n' << std::flush;
    auto caller=rt.GetThreadRegistry().LaunchHostThread("1888 diagnostic",512*1024,[&] {
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
    });
    if (!caller) return 12;
    uint64_t timeout = uint64_t(-100'000'000ll);
    if (rt.GetThreadRegistry().GetObjectWaiter().WaitGuestObject(*caller,0,0,0,&timeout) != X_STATUS_SUCCESS) {
      std::cout << "FAIL original application lifecycle timed out\n" << std::flush;
      ExitProcess(12);
    }
    rt.GetTitleExit().TerminateTitle();
    dash1888::ReleaseSystemUi(rt);
    std::cout << "original application result=" << std::hex << result << " scene=" << scene << '\n';
    if (result || !scene) return 13;
    if (opening_chord_raw && (!ProbeOpeningChord::active || ProbeOpeningChord::active->delivered()!=4)) {
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
    std::unique_ptr<rex::kernel::XEXModule> module;
    if (device_bootstrap) {
      module = std::make_unique<rex::kernel::XEXModule>(&rex::kernel::GetFunctionDispatcher(rt),rt);
      if (!module->Load("xam.xex", "game:\\xam.xex", image.data(), image.size()) ||
          module->GetBaseAddress() != 0x81870000 || module->GetImageSize() != 0x310000 ||
          !module->LoadContinue()) return 9;
    } else {
      if (image[0] != 'M' || image[1] != 'Z') return 9;
      auto* heap = memory->LookupHeap(0x81870000);
      if (!heap->AllocFixed(0x81870000, image.size(), heap->GetPageSize(),
          rex::kernel::kMemoryAllocationReserve | rex::kernel::kMemoryAllocationCommit,
          rex::kernel::kMemoryProtectRead | rex::kernel::kMemoryProtectWrite)) return 9;
      std::memcpy(base + 0x81870000, image.data(), image.size());
    }
    const auto* layout = image_fn();
    if (!(&rex::kernel::GetFunctionDispatcher(rt))->InitializeFunctionTable(layout->code_base, layout->code_size,
        layout->image_base, layout->image_size, true, true)) return 9;
    (&rex::kernel::GetFunctionDispatcher(rt))->RegisterModule("xam-static-probe", layout->code_base, register_fn);
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
    auto caller=rt.GetThreadRegistry().LaunchHostThread("1888 diagnostic",128*1024,[&] {
      worker_id = GetCurrentThreadId();
      x360n::SystemUiContext system_context(*memory);
      const auto call = [&](uint32_t address) {
        std::cout << "starting " << std::hex << address << '\n' << std::flush;
        const auto value = (&rex::kernel::GetFunctionDispatcher(rt))->ExecuteTrap(system_context.thread_state(), address, nullptr, 0);
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
      result = uint32_t((&rex::kernel::GetFunctionDispatcher(rt))->ExecuteTrap(
          system_context.thread_state(), 0x81938E18, args, 3));
      return 0;
    });
    if (!caller) return 12;
    uint64_t timeout = uint64_t(-50'000'000ll);
    if (rt.GetThreadRegistry().GetObjectWaiter().WaitGuestObject(*caller,0,0,0,&timeout) != X_STATUS_SUCCESS) {
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
        std::cout << "guest LR=" << std::hex << rt.GetThreadRegistry().Find(rt.GetObjectTable().Get<rex::kernel::ThreadRef>(*caller)->thread_id)->GetThreadState()->GetContext()->lr << '\n' << std::flush;
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
      rt.GetTitleExit().TerminateTitle();
    }
    (&rex::kernel::GetFunctionDispatcher(rt))->UnregisterModule("xam-static-probe");
  }
  memory->SystemHeapFree(stack);
  std::cout << "PASS original XUI realloc: allocate, grow across page, shrink, free; bytes preserved\n";
}
