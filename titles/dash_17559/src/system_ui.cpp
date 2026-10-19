#include "system_ui.h"
#include "system_ui_request.h"
#include "../../common/system_ui_context.h"
#include "../../common/system_ui_execution.h"
#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/cvar.h>
#include <rex/kernel/xam/module.h>
#include <rex/kernel/xboxkrnl/video.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <rex/system/xthread.h>
#include <rex/system/thread_state.h>
#include <rex/chrono/clock.h>
#include <array>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>
#include <chrono>
#include <rex/ppc/func.h>
#include <rex/input/input_system.h>
#include <rex/input/flags.h>

extern "C" REX_FUNC(__imp__XamShowGuideUI);

REXCVAR_DEFINE_BOOL(dash17559_signin_ui, false, "Dashboard 17559",
                   "Submit the original Sign-in request once at the first dashboard frame (manual validation)");
REXCVAR_DEFINE_UINT32(dash17559_guide_probe_seconds, 0, "Diagnostics",
                     "Request original Guide once after 1..120 seconds; zero disables the diagnostic");

namespace dash17559 {
using rex::X_HRESULT;
using rex::X_RESULT;
namespace {
std::function<bool()> draw_ui;
std::function<void()> request_signin;
std::function<void()> pending_ui;
std::function<void()> poll_guide;
const SystemUiApplication* active_application = nullptr;
// XAM stacks a system app that launches another one (Guide -> Profile): the
// caller is serialized with 80000009 and restored with flags=2 when its
// successor closes, as in the existing resume path.
struct SuspendedApp {
  uint32_t message;
  std::vector<uint8_t> request;
  std::vector<uint8_t> state;
};
std::vector<SuspendedApp> suspended_apps;
std::vector<uint8_t> restore_state;  // consumed by the next initialization
x360n::SystemUiExecution ui_execution;
std::string system_drive = "game:";
std::string bound_xam_path;
std::string UiPath(const char* path) {
  return system_drive + std::string(path).substr(5);
}
// Original XAM 17559 HUD host state (816CE8B0/816CE108), base 81AA20E0.
constexpr uint32_t kHudVisible = 0x81AA2188;       // +168
constexpr uint32_t kFadeTarget = 0x81AA21B4;       // +212
constexpr uint32_t kFadeCurrent = 0x81AA21B8;      // +216
constexpr uint32_t kDisplayChanged = 0x81A82744;   // set with a visible app (816CE4C8)
constexpr uint32_t kFadeShown = 0x815FA6BC;        // f30 in 816CE108
constexpr uint32_t kFadeHidden = 0x815F86F8;       // f31 in 816CE108
constexpr uint32_t kSystemDevice = 0x81AA1B14;
constexpr uint32_t kSystemTarget = 0x81AA1B18;
// The title's latest VdSwap front buffer; the system frame inherits it.
rex::kernel::xboxkrnl::SystemSwapFrame title_frame{};
void SystemSwap(const rex::kernel::xboxkrnl::SystemSwapFrame& frame) {
  title_frame = frame;
  auto* thread = rex::system::XThread::GetCurrentThread();
  if (!thread || !thread->thread_state()) return;
  DrawSystemUiFrame(*thread->thread_state()->context());
}
uint32_t Read32(rex::memory::Memory& memory, uint32_t address) {
  return rex::memory::load_and_swap<uint32_t>(memory.TranslateVirtual(address));
}
void Write32(rex::memory::Memory& memory, uint32_t address, uint32_t value) {
  rex::memory::store_and_swap<uint32_t>(memory.TranslateVirtual(address), value);
}
// Static host state written by the original host 816CE8B0 before and after
// its compositor setup: dispatcher headers of its events (type byte, signal
// state, empty wait list) and the compositor's pending-work list heads.
void InitializeHostState(rex::memory::Memory& memory) {
  auto event = [&](uint32_t header, uint8_t type) {
    *memory.TranslateVirtual<uint8_t*>(header) = type;
    Write32(memory, header + 4, 0);
    Write32(memory, header + 8, header + 8);
    Write32(memory, header + 12, header + 8);
  };
  event(0x81AA2134, 0);  // 816CE8B0: r31+84
  event(0x81AA2144, 1);  // r31+100
  event(0x81AA1858, 0);  // 6232
  event(0x81AA1838, 1);  // 6200
  event(0x81AA1828, 0);  // 6184
  // 816CE994-816CE9B8: two empty list heads at 6152 and 6160.
  Write32(memory, 0x81AA1808, 0x81AA1808);
  Write32(memory, 0x81AA180C, 0x81AA1808);
  Write32(memory, 0x81AA1810, 0x81AA1810);
  Write32(memory, 0x81AA1814, 0x81AA1810);
}
// 816CE3F8 hides the HUD and, with no pending app, 816C9A20 publishes an
// empty descriptor: the title's own front buffer is presented again.
void HideSystemFrame(rex::Runtime& runtime, rex::runtime::ThreadState* thread) {
  auto* memory = runtime.memory();
  Write32(*memory, kHudVisible, 0);
  Write32(*memory, kFadeTarget, Read32(*memory, kFadeHidden));
  Write32(*memory, kFadeCurrent, Read32(*memory, kFadeHidden));
  runtime.function_dispatcher()->ExecuteTrap(thread, 0x816C9A20, nullptr, 0);
}
// The kernel delivers display (type 3, 88 bytes) and title scaler (type 1,
// 64 bytes) information to XAM's routine 816CBFD8, registered by 816CE8B0.
// VdRegisterXamGraphicsNotification is not run here; deliver the same data.
bool NotifyDisplay(rex::Runtime& runtime, rex::runtime::ThreadState* thread) {
  auto* memory = runtime.memory();
  auto* dispatcher = runtime.function_dispatcher();
  const auto info = memory->SystemHeapAlloc(88 + 64);
  if (!info) return false;
  uint64_t query[] = {info};
  dispatcher->ExecuteTrap(thread, 0x81A72E44, query, 1);  // VdGetCurrentDisplayInformation
  auto* bytes = memory->TranslateVirtual<uint8_t*>(info);
  const uint16_t display_width = rex::memory::load_and_swap<uint16_t>(bytes + 0x48);
  const uint16_t display_height = rex::memory::load_and_swap<uint16_t>(bytes + 0x4A);
  if (title_frame.width && title_frame.height) {
    // Front buffer and scaler source follow the title's swap chain.
    rex::memory::store_and_swap<uint16_t>(bytes + 0, uint16_t(title_frame.width));
    rex::memory::store_and_swap<uint16_t>(bytes + 2, uint16_t(title_frame.height));
    rex::memory::store_and_swap<uint32_t>(bytes + 0x10, title_frame.width);
    rex::memory::store_and_swap<uint32_t>(bytes + 0x14, title_frame.height);
  }
  auto* scaler = bytes + 88;
  std::memcpy(scaler, bytes, 4);
  rex::memory::store_and_swap<uint16_t>(scaler + 4, display_width);
  rex::memory::store_and_swap<uint16_t>(scaler + 6, display_height);
  std::memcpy(scaler + 8, bytes + 8, 56);
  uint64_t display[] = {0, 3, info};
  dispatcher->ExecuteTrap(thread, 0x816CBFD8, display, 3);
  uint64_t title[] = {0, 1, info + 88};
  dispatcher->ExecuteTrap(thread, 0x816CBFD8, title, 3);
  memory->SystemHeapFree(info);
  REXLOG_INFO("17559 system UI: display {}x{}, title front buffer {}x{}", display_width, display_height,
              title_frame.width, title_frame.height);
  return true;
}
}
const std::string& SystemXamPath() { return bound_xam_path; }
void ReleaseSystemUiHost() {
  rex::kernel::xboxkrnl::SetSystemSwapCallback(nullptr);
  // The main thread may have exited while a different presenting thread is
  // still terminating the title. Do not destroy its callback or the runtime.
  auto xam = rex::Runtime::instance()->kernel_state()
      ->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  using Exit = rex::kernel::xam::XamModule::LoaderData::ExitRequest;
  const auto exit = xam->loader_data().exit_request;
  if (exit == Exit::Dashboard || exit == Exit::Terminate || exit == Exit::Firmware)
    ui_execution.WaitForCallerExit();
  draw_ui = {};
  request_signin = {};
  pending_ui = {};
  poll_guide = {};
  active_application = nullptr;
  suspended_apps.clear();
  restore_state.clear();
}
void InstallSystemUi(rex::Runtime& runtime, std::string drive, std::string native_directory) {
  system_drive = std::move(drive);
  // XAM normally handles the physical Guide button outside the foreground
  // title. Poll its edge here because the original XAM scheduler is not run.
  rex::cvar::SetFlagByName("guide_button", "true");
  if (rex::cvar::GetFlagByName("keybind_guide").empty()) rex::cvar::SetFlagByName("keybind_guide", "G");
  // Controllers without a Guide button may report it only as BACK+START. Unless
  // configured, let the SDK keep that chord from becoming a dashboard action.
  if (rex::cvar::GetFlagSource("guide_chord_ms") == rex::cvar::Source::kDefault)
    rex::cvar::SetFlagByName("guide_chord_ms", "150");
  poll_guide = [&runtime, held = std::array<bool, 4>{}]() mutable {
    auto* system = static_cast<rex::input::InputSystem*>(runtime.input_system());
    for (uint32_t user = 0; user < 4; ++user) {
      rex::input::X_INPUT_STATE input{};
      const bool pressed = system && system->GetState(user, &input) == 0 &&
          (uint16_t(input.gamepad.buttons) & rex::input::X_INPUT_GAMEPAD_GUIDE);
      const bool edge = pressed && !held[user];
      held[user] = pressed;
      if (!edge || pending_ui) continue;
      if (draw_ui) {
        if (active_application == &kGuide) {
          auto xam = runtime.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
          xam->RequestSystemUiClose();
        }
        continue;
      }
      auto* ctx = rex::system::XThread::GetCurrentThread()->thread_state()->context();
      ctx->r3.u64 = user;
      __imp__XamShowGuideUI(*ctx, runtime.memory()->virtual_membase());
    }
  };
  // Opt-in service-level validation without desktop input or UI automation.
  // No clock reads or wrapper in ordinary sessions.
  const auto probe = REXCVAR_GET(dash17559_guide_probe_seconds);
  if (probe > 0 && probe <= 120) {
    poll_guide = [&runtime, poll = std::move(poll_guide), probe,
                  start = std::chrono::steady_clock::now(), submitted = false]() mutable {
      poll();
      if (submitted || std::chrono::steady_clock::now() - start < std::chrono::seconds(probe)) return;
      submitted = true;
      if (draw_ui || pending_ui) return;
      auto* ctx = rex::system::XThread::GetCurrentThread()->thread_state()->context();
      ctx->r3.u64 = 0;
      __imp__XamShowGuideUI(*ctx, runtime.memory()->virtual_membase());
      REXLOG_INFO("17559 diagnostic: original Guide service requested, result={:08X}", ctx->r3.u32);
    };
  }
  // Every title presents through VdSwap: run system frames there, not from
  // title-specific render hooks.
  rex::kernel::xboxkrnl::SetSystemSwapCallback(&SystemSwap);
  auto bind = [&](const char* pe, const char* path, const char* name) {
    const auto library = native_directory.empty() ? std::string(name)
        : native_directory + "/" + name;
    runtime.kernel_state()->RegisterRecompiledModule(pe, UiPath(path).c_str(), library.c_str());
  };
  bind("hud.xex", "game:\\system\\hud.xex", "dash_17559_hud");
  bind("vk.xex", "game:\\vk.xex", "dash_17559_vk");
  const auto keyboard_library = native_directory.empty() ? std::string("dash_17559_vk")
      : native_directory + "/dash_17559_vk";
  runtime.kernel_state()->RegisterRecompiledModule("vk.xex", "\\Device\\Harddisk0\\Partition1\\vk.xex", keyboard_library.c_str());
  bind("signin.xex", "game:\\system\\signin.xex", "dash_17559_signin");
  bind("createprofile.xex", "game:\\system\\createprofile.xex", "dash_17559_createprofile");
  if (!native_directory.empty()) {
    bind("xam.xex", "game:\\system\\xam_ui.xex", "dash_17559_xam_ui");
    bound_xam_path = UiPath("game:\\system\\xam_ui.xex");
    bind("gamerprofile.xex", "game:\\system\\gamerprofile.xex", "dash_17559_gamerprofile");
  }
  if (REXCVAR_GET(dash17559_signin_ui)) {
    request_signin = [&runtime] {
      auto* thread = rex::system::XThread::GetCurrentThread()->thread_state();
      uint64_t args[] = {1, 0};
      // Original dashboard import thunk -> XamShowSigninUI -> FE/21001.
      const auto result = runtime.function_dispatcher()->ExecuteTrap(thread, 0x92192AE8, args, 2);
      REXLOG_INFO("17559 system UI: manual original Sign-in request -> {}", result);
    };
  }
  auto xam = runtime.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  xam->system_ui_request = [&runtime, ui_ready = false, ui_attempted = false](uint32_t message, uint32_t request, uint32_t size) mutable -> rex::X_HRESULT {
    const auto* application = SystemUiApplicationFor(message);
    if (!application) return X_E_NOTFOUND;
    if (!request || size != application->request_size || !rex::system::XThread::IsInThread()) return X_E_INVALIDARG;
    REXLOG_INFO("17559 system UI: original application request {:08X} size={}", message, size);
    x360n::SystemUiContext system_process(*runtime.memory());
    auto* state = runtime.kernel_state();
    auto xam = state->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
    if (draw_ui) {
      if (pending_ui) return X_E_FAIL;
      // System apps occupy the same guest image. Copy the request now, then
      // unload the current app at the frame boundary before loading its successor.
      std::vector<uint8_t> bytes(size);
      std::memcpy(bytes.data(), runtime.memory()->TranslateVirtual(request), size);
      pending_ui = [&runtime, message, bytes = std::move(bytes)] {
        auto xam = runtime.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
        const auto buffer = runtime.memory()->SystemHeapAlloc(uint32_t(bytes.size()));
        if (!buffer) { REXLOG_ERROR("17559 system UI: queued request allocation failed"); return; }
        std::memcpy(runtime.memory()->TranslateVirtual(buffer), bytes.data(), bytes.size());
        const auto result = xam->system_ui_request(message, buffer, uint32_t(bytes.size()));
        runtime.memory()->SystemHeapFree(buffer);
        REXLOG_INFO("17559 system UI: queued request {:08X} -> {:08X}", message, uint32_t(result));
      };
      xam->RequestSystemUiClose();
      return 0;
    }
    if (xam->FindSystemApp(0xFF)) return X_E_FAIL;
    // Execute on the requesting guest thread, preserving its register context.
    auto* thread = rex::system::XThread::GetCurrentThread()->thread_state();
    auto* dispatcher = runtime.function_dispatcher();
    // One original XAM image per process, as on the console: a title's avatar
    // service may already have loaded it (SystemXamModulePath). Its UI-only
    // initialization still runs here exactly once; a failed one is not retried.
    auto ui = state->GetModule(UiPath("game:\\system\\xam_ui.xex"));
    if (!ui_ready && ui_attempted) return X_E_FAIL;
    if (!ui_ready) {
      ui_attempted = true;
      if (!ui) ui = state->LoadUserModule(UiPath("game:\\system\\xam_ui.xex"), false);
      if (!ui) return X_E_NOTFOUND;
      rex::cvar::SetFlagByName("dash17559_ui_self", std::to_string(ui->hmodule_ptr()));
      // XUI-specific static constructor from 81A70370. It initializes the
      // library's critical sections; starting the whole XAM module is unnecessary.
      uint64_t constructor[] = {0x81AC8324};
      dispatcher->ExecuteTrap(thread, 0x817AC690, constructor, 1);
      // Graphics statics from CRT constructors 81A701A8/81A701E0:
      // initialize the renderer lock and construct its default provider.
      // Invoke their initializer bodies without registering XAM-wide atexit hooks.
      uint64_t renderer_lock[] = {0x81AC7DF8};
      dispatcher->ExecuteTrap(thread, 0x81A723A4, renderer_lock, 1);
      uint64_t renderer_provider[] = {0x81AC7E2C};
      dispatcher->ExecuteTrap(thread, 0x8178EA98, renderer_provider, 1);
      // Text service lock initialized by the original CRT entry 81A712C0.
      uint64_t text_lock[] = {0x81A97700};
      dispatcher->ExecuteTrap(thread, 0x81A723A4, text_lock, 1);
      // Audio's CRT constructor 81A71288 initializes the render callback
      // lock at 81A976E0. Without it the worker waits on zero-filled state;
      // signalling all objects during exit only concealed that missing init.
      uint64_t audio_lock[] = {0x81A976E0};
      dispatcher->ExecuteTrap(thread, 0x81A723A4, audio_lock, 1);
      // Image-cache constructor 81A70450. The first asynchronous image timer
      // enters this lock; zero-initialized storage is not an initialized lock.
      uint64_t image_cache_lock[] = {0x81ACA3F4};
      dispatcher->ExecuteTrap(thread, 0x81A723A4, image_cache_lock, 1);
      // Object handle table CRT constructor 81A702E0: lock then zero buckets.
      uint64_t object_lock[] = {0x81AC8728};
      dispatcher->ExecuteTrap(thread, 0x81A723A4, object_lock, 1);
      uint64_t object_buckets[] = {0x81AC8328, 0, 0x400};
      dispatcher->ExecuteTrap(thread, 0x8172D4F0, object_buckets, 3);
      REXLOG_INFO("17559 system UI: original XUI critical-section constructor completed");
      // XAM owns a separate recording D3D device (816968A8 -> 816C9EA0) whose
      // command buffers the kernel runs at the title's swap. The title device
      // and its D3D state are never shared with the system UI.
      auto* memory = runtime.memory();
      const auto hardware = Read32(*memory, 0x815F0480);
      // 816C9EA0 creates the device only when XboxHardwareInfo (xboxkrnl
      // 0x156, import slot 815F0480) has flag 0x200; no other XAM 17559 code
      // tests that bit, and this runtime does not set it. This host provides
      // the system graphics device, so expose the flag to that call only:
      // titles keep the runtime's existing XboxHardwareInfo value.
      const auto* hardware_export = runtime.export_resolver()->GetExportByOrdinal("xboxkrnl.exe", 0x156);
      if (!hardware_export || hardware_export->variable_ptr != hardware) return X_E_FAIL;
      InitializeHostState(*memory);
      const auto hardware_flags = Read32(*memory, hardware);
      Write32(*memory, hardware, hardware_flags | 0x200);
      const auto device_result = static_cast<X_HRESULT>(dispatcher->ExecuteTrap(thread, 0x816C9EA0, nullptr, 0));
      Write32(*memory, hardware, hardware_flags);
      const auto system_device = Read32(*memory, kSystemDevice);
      REXLOG_INFO("17559 system UI: original system device -> {:08X}, device={:08X}, commands={:08X}",
                  uint32_t(device_result), system_device, Read32(*memory, 0x81AA1B00));
      if (static_cast<int32_t>(device_result) < 0 || !system_device) return X_E_FAIL;
      if (!NotifyDisplay(runtime, thread)) return static_cast<X_HRESULT>(0x8007000Eu);
      // 816CE8B0 creates compositor resources (system front buffer, target,
      // shaders and the 852x480 canvas) before the XUI bootstrap.
      const auto resources = static_cast<X_HRESULT>(dispatcher->ExecuteTrap(thread, 0x816CA748, nullptr, 0));
      REXLOG_INFO("17559 system UI: original compositor resources -> {:08X}, target={:08X}, canvas={}x{}",
                  uint32_t(resources), Read32(*memory, kSystemTarget), Read32(*memory, 0x81A826F0),
                  Read32(*memory, 0x81A826F4));
      if (static_cast<int32_t>(resources) < 0) return resources;
      Write32(*memory, kFadeTarget, Read32(*memory, kFadeHidden));
      Write32(*memory, kFadeCurrent, Read32(*memory, kFadeHidden));
      uint64_t bootstrap[] = {0};
      const auto boot_result = static_cast<rex::X_HRESULT>(dispatcher->ExecuteTrap(thread, 0x816C8328, bootstrap, 0));
      REXLOG_INFO("17559 system UI: original XUI host bootstrap -> {:08X}", uint32_t(boot_result));
      if (static_cast<int32_t>(boot_result) < 0) return boot_result;
      // The original host initializes localization and fonts after loading its skin.
      uint64_t common_strings_lock[] = {0x81A83050};
      dispatcher->ExecuteTrap(thread, 0x81A723A4, common_strings_lock, 1);
      // CRT 81A6F888 constructs the configuration facade used by the HUD
      // clock/play-timer query; 81A71190 constructs the queried timer object.
      // Run their constructor bodies without starting XAM or registering atexit.
      uint64_t config_facade[] = {0x81A9AFC4};
      dispatcher->ExecuteTrap(thread, 0x81672C98, config_facade, 1);
      uint64_t play_timer[] = {0x81B52130};
      dispatcher->ExecuteTrap(thread, 0x819951A0, play_timer, 1);
      // CRT 81A6FF90 constructs the package registry queried by CreateProfile.
      // This host has no installed system package; the original query reports
      // absence and the app selects its own completion flow.
      uint64_t packages[] = {0x81AAC110};
      dispatcher->ExecuteTrap(thread, 0x816EA660, packages, 1);
      // Original visual host from 816CE9BC-816CE9E4. Reuse the active DC.
      const auto hud = memory->SystemHeapAlloc(72);
      if (!hud) return static_cast<X_HRESULT>(0x8007000Eu);
      uint64_t hud_ctor[] = {hud};
      dispatcher->ExecuteTrap(thread, 0x81693D78, hud_ctor, 1);
      uint64_t hud_init[] = {hud, rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(0x81AA2110))};
      const auto hud_ready = dispatcher->ExecuteTrap(thread, 0x81692978, hud_init, 2);
      REXLOG_INFO("17559 system UI: original HUD host initialize -> {}", hud_ready);
      if (!hud_ready) return X_E_FAIL;
      rex::memory::store_and_swap<uint32_t>(memory->TranslateVirtual(0x81AA2160), hud);
      // Original host loads huduiskin as data (816CE9E8-816CEA54).
      auto skin = state->LoadUserModule(UiPath("game:\\system\\huduiskin.xex"), false);
      if (!skin) {
        REXLOG_ERROR("17559 system UI: could not load original game:\\system\\huduiskin.xex as resource module");
        return X_E_NOTFOUND;
      }
      const auto locator = memory->SystemHeapAlloc(256);
      if (!locator) return static_cast<X_HRESULT>(0x8007000Eu);
      uint64_t locate_skin[] = {skin->hmodule_ptr(), 0x815FA684, 0x815FA6C0, locator, 128};
      dispatcher->ExecuteTrap(thread, 0x816C8A48, locate_skin, 5);
      uint64_t load_skin[] = {locator, 0};
      auto skin_result = static_cast<X_HRESULT>(dispatcher->ExecuteTrap(thread, 0x817C62C0, load_skin, 2));
      memory->SystemHeapFree(locator);
      REXLOG_INFO("17559 system UI: original visual skin load -> {:08X}", uint32_t(skin_result));
      if (static_cast<int32_t>(skin_result) < 0) return skin_result;
      uint64_t skin_path[] = {0x815FA690, 0};
      skin_result = static_cast<X_HRESULT>(dispatcher->ExecuteTrap(thread, 0x817C5EE0, skin_path, 2));
      if (static_cast<int32_t>(skin_result) < 0) return skin_result;
      // Original host 816CE8B0 calls 816C8450 before constructing hudbkgnd.
      // Let that original routine select the language, strings, font path and
      // fallback descriptor instead of synthesizing only part of its typeface.
      uint64_t localization[] = {skin->hmodule_ptr()};
      const auto localization_result = static_cast<X_HRESULT>(
          dispatcher->ExecuteTrap(thread, 0x816C8450, localization, 1));
      REXLOG_INFO("17559 system UI: original localization/typeface -> {:08X}", uint32_t(localization_result));
      if (static_cast<int32_t>(localization_result) < 0) return localization_result;
      const auto strings = rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(0x81AA3EC4));
      if (!strings) return X_E_NOTFOUND;
      // hudbkgnd.xur is built by the original host before app scene properties
      // can reference its background controller at 81A9DE68.
      uint64_t background[] = {hud, skin->hmodule_ptr()};
      const auto background_result = static_cast<X_HRESULT>(dispatcher->ExecuteTrap(thread, 0x816929B8, background, 2));
      REXLOG_INFO("17559 system UI: original HUD background -> {:08X}", uint32_t(background_result));
      if (static_cast<int32_t>(background_result) < 0) return background_result;
      ui_ready = true;
    }
    auto module = state->LoadUserModule(UiPath(application->path), false);
    if (!module) return X_E_NOTFOUND;
    uint64_t attach[] = {module->hmodule_ptr(), 1, 0};
    const auto attached = dispatcher->ExecuteTrap(thread, module->entry_point(), attach, 3);
    auto unload = [state, dispatcher, module] {
      auto* thread = rex::system::XThread::GetCurrentThread()->thread_state();
      uint64_t detach[] = {module->hmodule_ptr(), 0, 0};
      dispatcher->ExecuteTrap(thread, module->entry_point(), detach, 3);
      state->UnloadUserModule(module, false);
    };
    auto registration = xam->FindSystemApp(0xFF);
    if (!attached || !registration || registration->module != module->hmodule_ptr() ||
        registration->resource_id != application->resource_id) {
      REXLOG_ERROR("17559 system UI: original DLL did not register its callback");
      if (attached) unload();
      else state->UnloadUserModule(module, false);
      return X_E_FAIL;
    }
    const auto owned_request = runtime.memory()->SystemHeapAlloc(size);
    if (!owned_request) {
      unload();
      return static_cast<X_HRESULT>(0x8007000Eu);
    }
    uint64_t resource_registration[] = {registration->resource_id, module->hmodule_ptr()};
    dispatcher->ExecuteTrap(thread, 0x817DCBB8, resource_registration, 2);
    std::memcpy(runtime.memory()->TranslateVirtual(owned_request), runtime.memory()->TranslateVirtual(request), size);
    const auto user = rex::memory::load_and_swap<uint32_t>(runtime.memory()->TranslateVirtual(owned_request + application->user_offset));
    auto release_module = [&runtime, dispatcher, registration, owned_request, xam, unload] {
      auto* thread = rex::system::XThread::GetCurrentThread()->thread_state();
      uint64_t unregister_resource[] = {registration->resource_id};
      dispatcher->ExecuteTrap(thread, 0x817DCBD8, unregister_resource, 1);
      unload();
      runtime.memory()->SystemHeapFree(owned_request);
      xam->EndSystemUi(registration->module);
      active_application = nullptr;
    };
    if (!xam->BeginSystemUi(module->hmodule_ptr(), registration->slot, user)) {
      release_module();
      return X_E_FAIL;
    }
    const uint32_t context = runtime.memory()->SystemHeapAlloc(20);
    if (!context) { release_module(); return static_cast<rex::X_HRESULT>(0x8007000Eu); }
    auto put = [&](uint32_t offset, uint32_t value) {
      rex::memory::store_and_swap<uint32_t>(runtime.memory()->TranslateVirtual(context + offset), value);
    };
    // Original XAM 17559 816BCE74-816BCED8: first-load flags=1, copied
    // request, hmodule, user; arg2 points to the module's output-size word.
    // A suspended app is restored with flags=2 and its serialized state.
    const auto restored = restore_state.empty() ? 0
        : runtime.memory()->SystemHeapAlloc(uint32_t(restore_state.size()));
    if (restored)
      std::memcpy(runtime.memory()->TranslateVirtual(restored), restore_state.data(), restore_state.size());
    // Keep the state alive until scene creation and layout have completed.
    struct GuestAllocation {
      rex::memory::Memory* memory; uint32_t address;
      ~GuestAllocation() { if (address) memory->SystemHeapFree(address); }
    } state_lifetime{runtime.memory(), restored};
    put(0, restored ? 2 : 1); put(4, restored ? restored : owned_request); put(8, module->hmodule_ptr());
    put(12, user);
    put(16, 0);
    uint64_t initialize[] = {0x80000004, context, context + 16};
    const auto result = static_cast<rex::X_HRESULT>(dispatcher->ExecuteTrap(thread, registration->callback, initialize, 3));
    REXLOG_INFO("17559 system UI: {} original {} -> {:08X}", application->name,
                restored ? "restore" : "initialize", uint32_t(result));
    const auto saved_size = rex::memory::load_and_swap<uint32_t>(runtime.memory()->TranslateVirtual(context + 16));
    runtime.memory()->SystemHeapFree(context);
    if (static_cast<int32_t>(result) < 0) {
      // Sign-in 901050FC-90105124 already deletes and clears its app on
      // initialization failure. A destroy message would dereference that null.
      if (application != &kSignin) {
        uint64_t destroy[] = {0x80000005, 0, 0};
        dispatcher->ExecuteTrap(thread, registration->callback, destroy, 3);
      }
      release_module();
      return result;
    }
    const auto output = runtime.memory()->SystemHeapAlloc(4);
    if (!output) {
      uint64_t destroy[] = {0x80000005, 0, 0};
      dispatcher->ExecuteTrap(thread, registration->callback, destroy, 3);
      release_module();
      return static_cast<rex::X_HRESULT>(0x8007000Eu);
    }
    const auto dc = rex::memory::load_and_swap<uint32_t>(runtime.memory()->TranslateVirtual(0x81AA2110));
    rex::memory::store_and_swap<uint32_t>(runtime.memory()->TranslateVirtual(output), 0);
    // Original host 816CE5EC-816CE628 supplies its DC and receives the app's root.
    uint64_t prepare_scene[] = {0x8000000B, 0, 0};
    dispatcher->ExecuteTrap(thread, registration->callback, prepare_scene, 3);
    uint64_t create_scene[] = {0x80000008, dc, output};
    const auto scene_result = dispatcher->ExecuteTrap(thread, registration->callback, create_scene, 3);
    const auto scene = rex::memory::load_and_swap<uint32_t>(runtime.memory()->TranslateVirtual(output));
    REXLOG_INFO("17559 system UI: original create scene -> {:08X}, root={:08X}, dc={:08X}", uint32_t(scene_result), scene, dc);
    // Original host 816CE5EC-816CE65C attaches the returned scene to its
    // container. Exercise attachment/layout before enabling the compositor:
    // scene construction alone never produces an on-screen frame.
    if (static_cast<int32_t>(scene_result) >= 0 && scene) {
      const auto hud = rex::memory::load_and_swap<uint32_t>(runtime.memory()->TranslateVirtual(0x81AA2160));
      uint64_t attach_scene[] = {hud, output};
      const auto linked = dispatcher->ExecuteTrap(thread, 0x81694FF8, attach_scene, 2);
      REXLOG_INFO("17559 system UI: original attach scene -> {:08X}", uint32_t(linked));
      if (static_cast<int32_t>(linked) >= 0) {
        auto active_scene = std::make_shared<uint32_t>(scene);
        auto destroy_scene = [&runtime, dispatcher, hud, active_scene, registration, release_module] {
          auto* thread = rex::system::XThread::GetCurrentThread()->thread_state();
          // App owns the scene. Unlink and clear the host's reference before
          // its destroy callback and original DLL detach release the app.
          if (*active_scene) {
            uint64_t unlink[] = {*active_scene};
            dispatcher->ExecuteTrap(thread, 0x817BCBE8, unlink, 1);
            *active_scene = 0;
          }
          rex::memory::store_and_swap<uint32_t>(runtime.memory()->TranslateVirtual(hud + 0x3C), 0);
          const auto container = rex::memory::load_and_swap<uint32_t>(runtime.memory()->TranslateVirtual(hud + 0x40));
          uint64_t hide[] = {container, 0};
          dispatcher->ExecuteTrap(thread, 0x817B4D00, hide, 2);
          uint64_t destroy[] = {0x80000005, 0, 0};
          dispatcher->ExecuteTrap(thread, registration->callback, destroy, 3);
          release_module();
        };
        const auto root = rex::memory::load_and_swap<uint32_t>(runtime.memory()->TranslateVirtual(hud + 8));
        uint64_t layout[] = {root};
        const auto laid_out = dispatcher->ExecuteTrap(thread, 0x817BCAA0, layout, 1);
        REXLOG_INFO("17559 system UI: original layout -> {:08X}, canvas={:08X}", uint32_t(laid_out), root);
        if (static_cast<int32_t>(laid_out) < 0) {
          destroy_scene();
          runtime.memory()->SystemHeapFree(output);
          return static_cast<X_HRESULT>(laid_out);
        }
        // The original dashboard resolves/presents this target after its XUI
        // present call. Composite the original HUD here, with no extra swap,
        // image copy, window, blocking loop, or per-frame resource creation.
        std::vector<uint8_t> request_bytes(size);
        std::memcpy(request_bytes.data(), runtime.memory()->TranslateVirtual(owned_request), size);
        draw_ui = [&runtime, dispatcher, hud, registration, xam, destroy_scene, active_scene, saved_size, user,
                   message, request_bytes = std::move(request_bytes), frame = 0u]() mutable -> bool {
          auto* thread = rex::system::XThread::GetCurrentThread()->thread_state();
          if (xam->ConsumeSystemUiResume(registration->module)) {
            // Match the original XAM stack lifecycle: serialize (80000009),
            // destroy the application, then restore a new instance with flags=2.
            const auto resume = saved_size && saved_size <= 1024 * 1024
                ? runtime.memory()->SystemHeapAlloc(24 + saved_size) : 0;
            if (!resume) {
              REXLOG_ERROR("17559 system UI: invalid save size or allocation failure ({})", saved_size);
              destroy_scene();
              runtime.kernel_state()->BroadcastNotification(0x9, false);
              return false;
            }
            uint64_t save[] = {0x80000009, resume + 24, 0};
            const auto saved = dispatcher->ExecuteTrap(thread, registration->callback, save, 3);
            if (static_cast<int32_t>(saved) < 0) {
              runtime.memory()->SystemHeapFree(resume);
              destroy_scene();
              runtime.kernel_state()->BroadcastNotification(0x9, false);
              return false;
            }
            uint64_t unlink[] = {*active_scene};
            dispatcher->ExecuteTrap(thread, 0x817BCBE8, unlink, 1);
            *active_scene = 0;
            rex::memory::store_and_swap<uint32_t>(runtime.memory()->TranslateVirtual(hud + 0x3C), 0);
            uint64_t destroy[] = {0x80000005, 0, 0};
            dispatcher->ExecuteTrap(thread, registration->callback, destroy, 3);
            auto* words = runtime.memory()->TranslateVirtual<rex::be<uint32_t>*>(resume);
            words[0] = 2; words[1] = resume + 24; words[2] = registration->module;
            words[3] = user; words[4] = 0; words[5] = 0;
            uint64_t args[] = {0x80000004, resume, resume + 16};
            const auto result = dispatcher->ExecuteTrap(thread, registration->callback, args, 3);
            auto failed_restore = [&](const char* stage, uint32_t error) {
              REXLOG_ERROR("17559 system UI: original restore {} failed {:08X}", stage, error);
              runtime.memory()->SystemHeapFree(resume);
              destroy_scene();
              runtime.kernel_state()->BroadcastNotification(0x9, false);
              return false;
            };
            if (static_cast<int32_t>(result) < 0)
              return failed_restore("init", uint32_t(result));
            uint64_t prepare[] = {0x8000000B, 0, 0};
            const auto prepared = dispatcher->ExecuteTrap(thread, registration->callback, prepare, 3);
            if (static_cast<int32_t>(prepared) < 0)
              return failed_restore("prepare", uint32_t(prepared));
            const auto dc = rex::memory::load_and_swap<uint32_t>(runtime.memory()->TranslateVirtual(0x81AA2110));
            uint64_t create[] = {0x80000008, dc, resume + 20};
            const auto created = dispatcher->ExecuteTrap(thread, registration->callback, create, 3);
            *active_scene = words[5];
            if (static_cast<int32_t>(created) < 0 || !*active_scene)
              return failed_restore("scene", uint32_t(created));
            uint64_t attach[] = {hud, resume + 20};
            const auto attached = dispatcher->ExecuteTrap(thread, 0x81694FF8, attach, 2);
            if (static_cast<int32_t>(attached) < 0)
              return failed_restore("attach", uint32_t(attached));
            const auto root = rex::memory::load_and_swap<uint32_t>(runtime.memory()->TranslateVirtual(hud + 8));
            uint64_t layout[] = {root};
            const auto laid_out = dispatcher->ExecuteTrap(thread, 0x817BCAA0, layout, 1);
            if (static_cast<int32_t>(laid_out) < 0)
              return failed_restore("layout", uint32_t(laid_out));
            runtime.memory()->SystemHeapFree(resume);
            REXLOG_INFO("17559 system UI: original restore save={:08X} init={:08X} scene={:08X} attach={:08X}",
                        uint32_t(saved), uint32_t(result), uint32_t(created), uint32_t(attached));
          }
          // A stacked HUD dialog owns input ahead of the app. Otherwise the
          // app's update handles its A/B commands before XUI navigation.
          uint64_t input[] = {hud};
          if (dispatcher->ExecuteTrap(thread, 0x81693E20, input, 1))
            dispatcher->ExecuteTrap(thread, 0x816927C0, input, 1);
          uint64_t update[] = {0x8000000A, 0, 0};
          dispatcher->ExecuteTrap(thread, registration->callback, update, 3);
          dispatcher->ExecuteTrap(thread, 0x816927C0, input, 1);
          if (xam->GetSystemUiSession().close_requested) {
            // Closing because this app requested another one: keep it below
            // the successor, serialized by its own 80000009 handler.
            if (pending_ui && saved_size && saved_size <= 1024 * 1024) {
              const auto buffer = runtime.memory()->SystemHeapAlloc(saved_size);
              if (buffer) {
                uint64_t save[] = {0x80000009, buffer, 0};
                const auto saved = dispatcher->ExecuteTrap(thread, registration->callback, save, 3);
                if (static_cast<int32_t>(saved) >= 0) {
                  const auto* bytes = runtime.memory()->TranslateVirtual<uint8_t*>(buffer);
                  suspended_apps.push_back({message, request_bytes, {bytes, bytes + saved_size}});
                  REXLOG_INFO("17559 system UI: suspended {:08X} below its successor ({} bytes)", message, saved_size);
                } else {
                  REXLOG_ERROR("17559 system UI: original save failed {:08X}", uint32_t(saved));
                }
                runtime.memory()->SystemHeapFree(buffer);
              }
            }
            destroy_scene();
            runtime.kernel_state()->BroadcastNotification(0x9, false);
            REXLOG_INFO("17559 system UI: closed original app after {} frames; dashboard focus released", frame);
            return false;
          }
          // Original HUD frame 816CD6E8: when the kernel accepts another system
          // command buffer, 816CCF00 times, lays out and renders XUI, draws the
          // title's front buffer behind it, and publishes the recorded frame.
          if (dispatcher->ExecuteTrap(thread, 0x81842110, nullptr, 0)) {
            uint64_t compose[] = {Read32(*runtime.memory(), kSystemTarget)};
            dispatcher->ExecuteTrap(thread, 0x816CCF00, compose, 1);
            if (frame == 0 || frame % 600 == 0)
              REXLOG_INFO("17559 system UI: composed original system frame {}", frame);
            ++frame;
          }
          return true;
        };
        active_application = application;
        // 816CE4B8-816CE4D0: a shown app fades the title frame in the system pass.
        Write32(*runtime.memory(), kFadeTarget, Read32(*runtime.memory(), kFadeShown));
        Write32(*runtime.memory(), kDisplayChanged, 1);
        Write32(*runtime.memory(), kHudVisible, 1);
        xam->SetSystemUiVisible(module->hmodule_ptr());
        state->BroadcastNotification(0x9, true);
        runtime.memory()->SystemHeapFree(output);
        return 0;
      }
    }
    runtime.memory()->SystemHeapFree(output);
    uint64_t destroy[] = {0x80000005, 0, 0};
    dispatcher->ExecuteTrap(thread, registration->callback, destroy, 3);
    release_module();
    return static_cast<int32_t>(scene_result) < 0 ? static_cast<X_HRESULT>(scene_result) : X_E_FAIL;
  };
}
}

void dash17559::DrawSystemUiFrame(PPCContext& ctx) {
  x360n::SystemUiExecution::Scope execution(dash17559::ui_execution);
  if (!execution) return;
  // The original renderer also reaches this context through ThreadState. Keep
  // the frame bridge free of REX_FUNC's restrict qualifier and restore even
  // when guest execution unwinds (for example an Xbox Home request).
  x360n::SystemUiContext frame(*rex::Runtime::instance()->memory());
  if (frame.thread_state()->context() != &ctx) {
    REXLOG_ERROR("17559 system UI: frame context does not belong to the current guest thread");
    return;
  }
  if (dash17559::poll_guide) dash17559::poll_guide();
  if (!dash17559::draw_ui && dash17559::pending_ui) {
    auto request = std::move(dash17559::pending_ui);
    request();
  }
  if (dash17559::request_signin) {
    auto request = std::move(dash17559::request_signin);
    request();
  }
  if (dash17559::draw_ui && !dash17559::draw_ui()) {
    dash17559::draw_ui = {};
    dash17559::HideSystemFrame(*rex::Runtime::instance(), frame.thread_state());
    if (!dash17559::pending_ui && !dash17559::suspended_apps.empty()) {
      // The successor closed: return to the app that launched it.
      dash17559::pending_ui = [app = std::move(dash17559::suspended_apps.back())] {
        auto& runtime = *rex::Runtime::instance();
        auto xam = runtime.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
        const auto buffer = runtime.memory()->SystemHeapAlloc(uint32_t(app.request.size()));
        if (!buffer) { REXLOG_ERROR("17559 system UI: restore request allocation failed"); return; }
        std::memcpy(runtime.memory()->TranslateVirtual(buffer), app.request.data(), app.request.size());
        dash17559::restore_state = app.state;
        const auto result = xam->system_ui_request(app.message, buffer, uint32_t(app.request.size()));
        dash17559::restore_state.clear();
        runtime.memory()->SystemHeapFree(buffer);
        REXLOG_INFO("17559 system UI: restored suspended {:08X} -> {:08X}", app.message, uint32_t(result));
      };
      dash17559::suspended_apps.pop_back();
    }
  }
}
