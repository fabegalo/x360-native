#include "system_ui.h"
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/thread_registry.h>
#include <rex/kernel/thread_state.h>
#include <rex/kernel/function_dispatcher.h>
#include <rex/kernel/object/module_object.h>
#include <rex/kernel/object/object_table.h>
#include <rex/kernel/object/notification_broadcaster.h>
#include <rex/kernel/types/io.h>
#include <rex/kernel/xboxkrnl.h>
#include <rex/xam/title_lifecycle.h>
#include <rex/xam/system_ui.h>
#include <rex/xam/user_profile.h>
#include <rex/xam/loader_state.h>
#include <rex/xam/offline_achievement_store.h>
#include <rex/xam/task_context.h>
#include <rex/ppc/recompiled_code.h>
#include <rex/logging/log_category.h>
#include <rex/string/text.h>
#include "boot_intro.h"
#include "system_ui_request.h"
#include "storage_selector_diagnostic.h"
#include "message_box_diagnostic.h"
#include "keyboard_diagnostic.h"
#include "system_ui_saved_state.h"
#include "system_ui_transition.h"
#include "system_ui_compositor.h"
#include "../../common/system_ui_context.h"
#include "../../common/system_ui_execution.h"
#include "../../common/guide_button.h"
#include <array>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <vector>
#include <chrono>
#include <atomic>
#include <mutex>

extern "C" REX_FUNC(__imp__XamShowGuideUI);


namespace dash17559 {
using rex::kernel::X_HRESULT;
using rex::kernel::X_RESULT;
using namespace rex::kernel;
namespace {

// Borrow only the canonical executable's services; the registry owns them until title teardown.
rex::kernel::GuestAddressSpace* Memory(rex::kernel::TitleRuntime& runtime) {
  if (auto* thread = rex::kernel::ThreadState::Get(); thread && thread->GetRuntime() == &runtime)
    return thread->GetAddressSpace();
  const auto handle = runtime.GetModuleRegistry().GetExecutable();
  REX_CHECK(handle, "17559 system UI: executable is unavailable");
  auto module = runtime.GetObjectTable().Get<rex::kernel::ModuleObject>(*handle);
  REX_CHECK(module && module->processor_module, "17559 system UI: executable image is unavailable");
  return module->processor_module->GetAddressSpace();
}
rex::kernel::FunctionDispatcher* Functions(rex::kernel::TitleRuntime& runtime) {
  const auto handle = runtime.GetModuleRegistry().GetExecutable();
  REX_CHECK(handle, "17559 system UI: executable is unavailable");
  auto module = runtime.GetObjectTable().Get<rex::kernel::ModuleObject>(*handle);
  REX_CHECK(module && module->processor_module, "17559 system UI: executable image is unavailable");
  return module->processor_module->GetFunctionDispatcher();
}
rex::xam::IXAMTitleServices& TitleServices() {
  auto* services = REX_INTERFACE(rex::xam::IXAMTitleServices);
  REX_CHECK(services, "17559 system UI: XAM title services are unavailable");
  return *services;
}
rex::xam::SystemUIState& SystemUI() { return TitleServices().GetSystemUI(); }
struct UiModule { rex::kernel::X_HANDLE handle; uint32_t hmodule, entry; };
std::optional<UiModule> UiModuleInfo(rex::kernel::TitleRuntime& runtime, rex::kernel::X_HANDLE handle) {
  auto module = runtime.GetObjectTable().Get<rex::kernel::ModuleObject>(handle);
  if (!module) return std::nullopt;
  return UiModule{handle, module->hmodule_ptr, module->user.entry_point};
}
std::optional<UiModule> FindUiModule(rex::kernel::TitleRuntime& runtime, const std::string& path) {
  const auto handle = runtime.GetModuleRegistry().GetModule(path);
  return handle ? UiModuleInfo(runtime, *handle) : std::nullopt;
}
std::optional<UiModule> LoadUiModule(rex::kernel::TitleRuntime& runtime, const std::string& path, bool entry) {
  const auto loaded = runtime.GetModuleRegistry().LoadUserModule(path, entry);
  return loaded ? UiModuleInfo(runtime, *loaded) : std::nullopt;
}
uint32_t KernelVariable(rex::kernel::TitleRuntime& runtime, uint16_t ordinal) {
  const auto handle = runtime.GetModuleRegistry().FindKernelModule("xboxkrnl.exe");
  if (!handle) return 0;
  auto module = runtime.GetObjectTable().Get<rex::kernel::ModuleObject>(*handle);
  return module ? module->GetVariable(ordinal) : 0;
}

std::function<bool()> draw_ui;
std::function<void()> request_signin;
std::function<void()> pending_ui;
std::function<void()> poll_guide;
struct AchievementNotice {
  uint32_t user, title, achievement;
  uint64_t xuid;
};
std::mutex achievement_notices_mutex;
std::vector<AchievementNotice> achievement_notices;
std::atomic<bool> achievement_notices_pending{false};
uint64_t achievement_notice_generation = 0;  // guarded by achievement_notices_mutex
bool notification_host_ready = false;
const SystemUiApplication* active_application = nullptr;
bool system_ui_visible = false;
// XAM stacks a system app that launches another one (Guide -> Profile): the
// caller is serialized with 80000009 and restored with flags=2 when its
// successor closes, as in the existing resume path.
struct SuspendedApp {
  uint32_t message;
  std::vector<uint8_t> request;
  std::vector<uint8_t> state;
  uint32_t user;
};
std::vector<SuspendedApp> suspended_apps;
std::vector<uint8_t> restore_state;  // consumed by the next initialization
// Controller of an app started by XamAppLoad without a request buffer.
std::optional<uint32_t> load_user;  // consumed by the next initialization
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
constexpr uint32_t kNotificationHost = 0x81AA1B54;  // 816CEA9C
constexpr uint32_t kNotificationVisible = 0x81AA2154; // host +116
// The title's latest VdSwap front buffer; the system frame inherits it.
rex::kernel::xboxkrnl::SystemSwapFrame title_frame{};
void SystemSwap(const rex::kernel::xboxkrnl::SystemSwapFrame& frame) {
#ifndef X360N_SYSTEM_UI_PLUGIN
  // Same lifecycle gate as the 1888 host. Game session plugins have no
  // console boot animation; the dashboard bootstrap owns this state.
  if (BootIntroRunning()) return;
#endif
  title_frame = frame;
  auto* thread = rex::kernel::KernelThread::GetCallingThread();
  if (!thread || !thread->GetThreadState()) return;
  DrawSystemUiFrame(*thread->GetThreadState()->GetContext());
}
uint32_t Read32(rex::kernel::GuestAddressSpace& memory, uint32_t address) {
  return rex::memory::load_and_swap<uint32_t>(memory.TranslateVirtual(address));
}
uint32_t storage_observe_object = 0, storage_observe_vtable = 0, storage_observe_frames = 0;
std::array<uint32_t, 6> storage_observe_last{};
void ObserveStorageController(rex::kernel::GuestAddressSpace& memory) {
  if (!storage_observe_frames) return;
  --storage_observe_frames;
  const auto object = storage_observe_object;
  if (!object || !memory.IsGuestRangeAccessible(object, 144, rex::kernel::kMemoryProtectRead) ||
      Read32(memory, object) != storage_observe_vtable) {
    storage_observe_frames = 0;
    return;
  }
  const std::array<uint32_t, 6> fields = {Read32(memory, object + 92), Read32(memory, object + 96),
      Read32(memory, object + 104), Read32(memory, object + 112),
      Read32(memory, object + 116), Read32(memory, object + 140)};
  if (fields != storage_observe_last) {
    REXLOG_INFO("17559 storage controller {:08X}: operation={} state={} answer={} device={:08X} result={:08X} extended={:08X}",
                object, fields[0], fields[1], fields[2], fields[3], fields[4], fields[5]);
    storage_observe_last = fields;
  }
}
void Write32(rex::kernel::GuestAddressSpace& memory, uint32_t address, uint32_t value) {
  rex::memory::store_and_swap<uint32_t>(memory.TranslateVirtual(address), value);
}
// A nested modal request is accepted before the previous app is suspended.
// If loading its successor fails at the frame boundary, complete that accepted
// I/O instead of leaving the game or CreateProfile waiting indefinitely.
void FailQueuedModal(rex::kernel::TitleRuntime& runtime, uint32_t message,
                        const std::vector<uint8_t>& request, X_HRESULT status) {
  if (static_cast<int32_t>(status) >= 0) return;
  const bool message_box = message == 0x2100D && request.size() == 1148;
  const bool keyboard = (message == 0x21005 && request.size() == 32) ||
      (message == 0x21010 && request.size() == 36);
  if (!message_box && !keyboard && (message != 0x2100B || request.size() != 40)) return;
  const auto address = rex::memory::load_and_swap<uint32_t>(request.data() + (message_box ? 1144 : 0x1C));
  if (!address) return;
  const auto* io = Memory(runtime)->TranslateVirtual<rex::kernel::XOVERLAPPED*>(address);
  if (io->result != 997) return; // The original app may already have completed.
  const uint32_t error = (message_box || keyboard) ? (uint32_t(status) == 0x8007000Eu ? 14 : 1627)
      : (status & 0x7FFF0000) == 0x00070000 ? status & 0xFFFF : 1627;
  REX_INTERFACE(rex::kernel::IOverlappedDispatcher)->Complete(address, {.result = error, .extended_error = status, .length = 0});
}
// Static host state written by the original host 816CE8B0 before and after
// its compositor setup: dispatcher headers of its events (type byte, signal
// state, empty wait list) and the compositor's pending-work list heads.
void InitializeHostState(rex::kernel::GuestAddressSpace& memory) {
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
void HideSystemFrame(rex::kernel::TitleRuntime& runtime, rex::kernel::ThreadState* thread) {
  auto* memory = Memory(runtime);
  Write32(*memory, kHudVisible, 0);
  Functions(runtime)->ExecuteTrap(thread, 0x816C9A20, nullptr, 0);
}
// The kernel delivers display (type 3, 88 bytes) and title scaler (type 1,
// 64 bytes) information to XAM's routine 816CBFD8, registered by 816CE8B0.
// VdRegisterXamGraphicsNotification is not run here; deliver the same data.
bool NotifyDisplay(rex::kernel::TitleRuntime& runtime, rex::kernel::ThreadState* thread) {
  auto* memory = Memory(runtime);
  auto* dispatcher = Functions(runtime);
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
void ReleaseSystemUiHost(rex::kernel::TitleRuntime& runtime) {
  rex::kernel::xboxkrnl::SetSystemSwapCallback(runtime, nullptr);
  // The main thread may have exited while a different presenting thread is
  // still terminating the title. Do not destroy its callback or the runtime.
  auto* xam = &SystemUI();
  using Exit = rex::xam::XAMLoaderState::ExitRequest;
  const auto exit = TitleServices().GetLoaderState().exit_request;
  if (exit == Exit::Dashboard || exit == Exit::Terminate || exit == Exit::Firmware)
    ui_execution.WaitForCallerExit();
  draw_ui = {};
  request_signin = {};
  pending_ui = {};
  poll_guide = {};
  active_application = nullptr;
  system_ui_visible = false;
  bound_xam_path.clear();
  for (const auto& app : suspended_apps)
    ReleaseDiscardedSystemUiState(*Memory(runtime), app.message, app.state);
  suspended_apps.clear();
  restore_state.clear();
  load_user.reset();
  xam->SetAchievementUnlockedCallback({});
  {
    std::lock_guard lock(achievement_notices_mutex);
    ++achievement_notice_generation;
    achievement_notices.clear();
    achievement_notices_pending.store(false, std::memory_order_release);
  }
  notification_host_ready = false;
}
void InstallSystemUi(rex::kernel::TitleRuntime& runtime, std::string drive, std::string native_directory) {
  system_drive = std::move(drive);
  bound_xam_path = UiPath("game:\\system\\xam_ui.xex");
  // The hosted system is 2.0.17559.0. XAM compares XboxKrnlVersion (816CECD8:
  // major, minor, build, byte 7) with system.manifest +332 (20449700); the
  // runtime default 2.65535.65535 made the original registry discard it.
  // Byte 6 keeps the runtime's value: its console meaning is not known here.
  if (const auto version = KernelVariable(runtime, 0x158); version) {
    auto* bytes = Memory(runtime)->TranslateVirtual<uint8_t*>(version);
    const uint8_t kernel[] = {0x00, 0x02, 0x00, 0x00, 0x44, 0x97};
    std::memcpy(bytes, kernel, sizeof(kernel));
    bytes[7] = 0;
  } else {
    REXLOG_ERROR("17559 system UI: XboxKrnlVersion is not exported");
  }
  // XAM normally handles the physical Guide button outside the foreground
  // title. Poll its edge here because the original XAM scheduler is not run.
  x360n::ConfigureGuideButton();
  if (const auto key = REX_SETTINGS().Find("hid.keybind_guide"); key && (*key)->GetText().empty())
    REX_CHECK((*key)->SetText(rex::SettingLayer::kRuntime, "G"), "17559: Guide binding unavailable");
  poll_guide = [&runtime]() {
    for (uint32_t user = 0; user < 4; ++user) {
      const bool edge = rex::xam::PollGuideButton(user);
      if (!edge || pending_ui) continue;
      if (draw_ui) {
        if (active_application == &kGuide) {
          auto* xam = &SystemUI();
          xam->RequestSystemUiClose();
        }
        continue;
      }
      auto* ctx = rex::kernel::KernelThread::GetCallingThread()->GetThreadState()->GetContext();
      ctx->r3.u64 = user;
      __imp__XamShowGuideUI(*ctx, Memory(runtime)->GetVirtualMembase());
    }
  };
  // Opt-in service-level validation without desktop input or UI automation.
  // No clock reads or wrapper in ordinary sessions.
  const auto probe = GetSystemUiSettings().guide_probe_seconds.Get();
  if (probe > 0 && probe <= 120) {
    poll_guide = [&runtime, poll = std::move(poll_guide), probe,
                  start = std::chrono::steady_clock::now(), submitted = false]() mutable {
      poll();
      if (submitted || std::chrono::steady_clock::now() - start < std::chrono::seconds(probe)) return;
      submitted = true;
      if (draw_ui || pending_ui) return;
      auto* ctx = rex::kernel::KernelThread::GetCallingThread()->GetThreadState()->GetContext();
      ctx->r3.u64 = 0;
      __imp__XamShowGuideUI(*ctx, Memory(runtime)->GetVirtualMembase());
      REXLOG_INFO("17559 diagnostic: original Guide service requested, result={:08X}, package registry "
                  "scanned={} manifest={:08X}",
                  ctx->r3.u32, Read32(*Memory(runtime), 0x81AAC110 + 4), Read32(*Memory(runtime), 0x81AAC110 + 164));
    };
  }
  poll_guide = WithStorageSelectorDiagnostic(runtime, std::move(poll_guide));
  poll_guide = WithMessageBoxDiagnostic(runtime, std::move(poll_guide));
  poll_guide = WithKeyboardDiagnostic(runtime, std::move(poll_guide));
  // Every title presents through VdSwap: run system frames there, not from
  // title-specific render hooks.
  rex::kernel::xboxkrnl::SetSystemSwapCallback(runtime, &SystemSwap);
  auto bind = [&](const char* pe, const char* path, const char* name) {
    const auto library = native_directory.empty() ? std::string(name)
        : native_directory + "/" + name;
    runtime.GetModuleRegistry().RegisterRecompiledModule(pe, UiPath(path).c_str(), library.c_str());
  };
  bind("hud.xex", "game:\\system\\hud.xex", "dash_17559_hud");
  bind("vk.xex", "game:\\vk.xex", "dash_17559_vk");
  bind("signin.xex", "game:\\system\\signin.xex", "dash_17559_signin");
  bind("createprofile.xex", "game:\\system\\createprofile.xex", "dash_17559_createprofile");
  bind("deviceselector.xex", "game:\\system\\deviceselector.xex", "dash_17559_deviceselector");
  bind("Guide.AvatarMiniCreator.xex", "game:\\system\\Guide.AvatarMiniCreator.xex",
       "dash_17559_avatarminicreator");
  if (!native_directory.empty()) {
    bind("xam.xex", "game:\\system\\xam_ui.xex", "dash_17559_xam_ui");
    bind("gamerprofile.xex", "game:\\system\\gamerprofile.xex", "dash_17559_gamerprofile");
  }
  if (GetSystemUiSettings().signin_ui.Get()) {
    request_signin = [&runtime] {
      auto* thread = rex::kernel::KernelThread::GetCallingThread()->GetThreadState();
      uint64_t args[] = {1, 0};
      // Original dashboard import thunk -> XamShowSigninUI -> FE/21001.
      const auto result = Functions(runtime)->ExecuteTrap(thread, 0x92192AE8, args, 2);
      REXLOG_INFO("17559 system UI: manual original Sign-in request -> {}", result);
    };
  }
  auto* xam = &SystemUI();
  uint64_t notice_generation;
  {
    std::lock_guard lock(achievement_notices_mutex);
    notice_generation = ++achievement_notice_generation;
  }
  xam->SetAchievementUnlockedCallback([notice_generation](uint64_t xuid, uint32_t user, uint32_t title, uint32_t achievement) {
    // A worker may already have captured the callback when the host closes.
    // It never touches Runtime/guest state, and the generation rejects any
    // old session's presentation after teardown/reinstall.
    std::lock_guard lock(achievement_notices_mutex);
    if (notice_generation != achievement_notice_generation) return;
    achievement_notices.push_back({user, title, achievement, xuid});
    achievement_notices_pending.store(true, std::memory_order_release);
  });
  // Hypothesis (816BCE74 not fully traced): without a buffer, XAM initializes
  // the app with a null request; 90107850 handles that case. A buffer would
  // need its unverified layout and is refused rather than reinterpreted.
  xam->system_app_load = [](const std::string& module, uint32_t user, uint32_t buffer,
                                    uint32_t size) -> rex::kernel::X_HRESULT {
    const auto message = SystemUiMessageForLoad(module);
    if (!message) return X_E_NOTFOUND;
    if (buffer || size) return static_cast<X_HRESULT>(0x80004001u);
    auto* xam = &SystemUI();
    load_user = user;
    const auto result = xam->system_ui_request(message, 0, 0);
    load_user.reset();
    return result;
  };
  xam->system_ui_request = [&runtime, ui_ready = false, ui_attempted = false](uint32_t message, uint32_t request, uint32_t size) mutable -> rex::kernel::X_HRESULT {
    const bool notification = message == 0x21013;
    const auto* application = SystemUiApplicationFor(message);
    if (!notification && !application) return X_E_NOTFOUND;
    // XamAppLoad without a buffer: no request, controller from load_user.
    const bool unrequested = !notification && !request && !size && load_user;
    if (!unrequested && (!request || size != (notification ? 40u : application->request_size))) return X_E_INVALIDARG;
    if (!rex::kernel::KernelThread::IsInThread()) return X_E_INVALIDARG;
    REXLOG_INFO("17559 system UI: original application request {:08X} size={}{}", message, size,
                unrequested ? " (XamAppLoad)" : "");
    x360n::SystemUiContext system_process(*Memory(runtime));
    auto* xam = &SystemUI();
    if (!notification && (draw_ui || xam->GetSystemUiSession().module)) {
      if (pending_ui) return X_E_FAIL;
      // System apps occupy the same guest image. Copy the request now, then
      // unload the current app at the frame boundary before loading its successor.
      std::vector<uint8_t> bytes(size);
      if (size) std::memcpy(bytes.data(), Memory(runtime)->TranslateVirtual(request), size);
      pending_ui = [&runtime, message, bytes = std::move(bytes), queued_user = load_user] {
        auto* xam = &SystemUI();
        const auto buffer = bytes.empty() ? 0 : Memory(runtime)->SystemHeapAlloc(uint32_t(bytes.size()));
        if (!bytes.empty() && !buffer) {
          FailQueuedModal(runtime, message, bytes, static_cast<X_HRESULT>(0x8007000Eu));
          REXLOG_ERROR("17559 system UI: queued request allocation failed"); return;
        }
        if (buffer) std::memcpy(Memory(runtime)->TranslateVirtual(buffer), bytes.data(), bytes.size());
        load_user = queued_user;
        const auto result = xam->system_ui_request(message, buffer, uint32_t(bytes.size()));
        load_user.reset();
        if (buffer) Memory(runtime)->SystemHeapFree(buffer);
        FailQueuedModal(runtime, message, bytes, result);
        REXLOG_INFO("17559 system UI: queued request {:08X} -> {:08X}", message, uint32_t(result));
      };
      xam->RequestSystemUiClose();
      return 0;
    }
    if (!notification && xam->FindSystemApp(0xFF)) return X_E_FAIL;
    // Execute on the requesting guest thread, preserving its register context.
    auto* thread = rex::kernel::KernelThread::GetCallingThread()->GetThreadState();
    auto* dispatcher = Functions(runtime);
    // One original XAM image per process, as on the console: a title's avatar
    // service may already have loaded it (SystemXamModulePath). Its UI-only
    // initialization still runs here exactly once; a failed one is not retried.
    auto ui = FindUiModule(runtime, UiPath("game:\\system\\xam_ui.xex"));
    if (!ui_ready && ui_attempted) return X_E_FAIL;
    if (!ui_ready) {
      ui_attempted = true;
      if (!ui) ui = LoadUiModule(runtime, UiPath("game:\\system\\xam_ui.xex"), false);
      if (!ui) return X_E_NOTFOUND;
      REX_CHECK(GetSystemUiSettings().ui_self.SetText(rex::SettingLayer::kRuntime, std::to_string(ui->hmodule)), "17559: original XAM module identity could not be recorded");
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
      auto* memory = Memory(runtime);
      const auto hardware = Read32(*memory, 0x815F0480);
      // 816C9EA0 creates the device only when XboxHardwareInfo (xboxkrnl
      // 0x156, import slot 815F0480) has flag 0x200; no other XAM 17559 code
      // tests that bit, and this runtime does not set it. This host provides
      // the system graphics device, so expose the flag to that call only:
      // titles keep the runtime's existing XboxHardwareInfo value.
      const auto hardware_export = KernelVariable(runtime, 0x156);
      if (!hardware_export || hardware_export != hardware) return X_E_FAIL;
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
      const auto boot_result = static_cast<rex::kernel::X_HRESULT>(dispatcher->ExecuteTrap(thread, 0x816C8328, bootstrap, 0));
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
      uint64_t packages[] = {0x81AAC110};
      dispatcher->ExecuteTrap(thread, 0x816EA660, packages, 1);
      // The original storage manager (8167ACF8) registers the mounted hard
      // drive (device 1, kind 1) through 816E9A58; kind 1 never reads the device
      // object. XAM boot (81696558) then scans system.manifest via 816EBDE0.
      // The SDK owns storage devices, so this host makes those two original
      // calls once the drive's system extension partition is mounted.
      uint64_t hard_drive[] = {1, 1, 0};
      dispatcher->ExecuteTrap(thread, 0x816E9A58, hard_drive, 3);
      // The scan schedules its tasks, then waits for KEVENT 81A9E0F4 (816EB9F0)
      // and rescans. Original XAM init 816968A8 creates it as a notification
      // event, and its boot thread 816964A8 sets it after
      // UsbdBootEnumerationDoneEvent and the \Device\BuiltInMuSfc IOCTL. This
      // host enumerates storage itself (the drive above), so it publishes the
      // event already set, in the same layout (wait list pointing to itself).
      // shortcut: no USB/MU boot enumeration; signal from real device arrival once the SDK has it.
      constexpr uint32_t kBootDevicesEnumerated = 0x81A9E0F4;
      std::memset(memory->TranslateVirtual(kBootDevicesEnumerated), 0, 16);
      Write32(*memory, kBootDevicesEnumerated + 4, 1);
      Write32(*memory, kBootDevicesEnumerated + 8, kBootDevicesEnumerated + 8);
      Write32(*memory, kBootDevicesEnumerated + 12, kBootDevicesEnumerated + 8);
      dispatcher->ExecuteTrap(thread, 0x816EBDE0, nullptr, 0);
      REXLOG_INFO("17559 system UI: original package registry manifest={:08X}",
                  Read32(*memory, 0x81AAC110 + 164));
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
      auto skin = LoadUiModule(runtime, UiPath("game:\\system\\huduiskin.xex"), false);
      if (!skin) {
        REXLOG_ERROR("17559 system UI: could not load original game:\\system\\huduiskin.xex as resource module");
        return X_E_NOTFOUND;
      }
      const auto locator = memory->SystemHeapAlloc(256);
      if (!locator) return static_cast<X_HRESULT>(0x8007000Eu);
      uint64_t locate_skin[] = {skin->hmodule, 0x815FA684, 0x815FA6C0, locator, 128};
      dispatcher->ExecuteTrap(thread, 0x816C8A48, locate_skin, 5);
      uint64_t load_skin[] = {locator, 0};
      auto skin_result = static_cast<X_HRESULT>(dispatcher->ExecuteTrap(thread, 0x817C62C0, load_skin, 2));
      memory->SystemHeapFree(locator);
      REXLOG_INFO("17559 system UI: original visual skin load -> {:08X}", uint32_t(skin_result));
      if (static_cast<int32_t>(skin_result) < 0) return skin_result;
      uint64_t skin_path[] = {0x815FA690, 0};
      skin_result = static_cast<X_HRESULT>(dispatcher->ExecuteTrap(thread, 0x817C5EE0, skin_path, 2));
      if (static_cast<int32_t>(skin_result) < 0) return skin_result;
      // Original 816CEA68..816CEA90 loads the image tables after its skin,
      // before starting the global cache. The cache constructor reads table
      // zero through 816C8E70 for its original default PNG (not a host image).
      uint64_t image_resources[] = {0x81A82714, 3};
      dispatcher->ExecuteTrap(thread, 0x816C8D68, image_resources, 2);
      uint64_t image_variants[] = {0x81A82748, 8};
      dispatcher->ExecuteTrap(thread, 0x816C8D68, image_variants, 2);
      if (!Read32(*memory, 0x81A8271C)) {
        REXLOG_ERROR("17559 system UI: original default image resource unavailable");
        return X_E_NOTFOUND;
      }
      const auto image_cache = static_cast<X_HRESULT>(
          dispatcher->ExecuteTrap(thread, 0x81691ED0, nullptr, 0));
      REXLOG_INFO("17559 system UI: original image cache -> {:08X}", uint32_t(image_cache));
      if (static_cast<int32_t>(image_cache) < 0) return image_cache;
      // Original host 816CE8B0 calls 816C8450 before constructing hudbkgnd.
      // Let that original routine select the language, strings, font path and
      // fallback descriptor instead of synthesizing only part of its typeface.
      uint64_t localization[] = {skin->hmodule};
      const auto localization_result = static_cast<X_HRESULT>(
          dispatcher->ExecuteTrap(thread, 0x816C8450, localization, 1));
      REXLOG_INFO("17559 system UI: original localization/typeface -> {:08X}", uint32_t(localization_result));
      if (static_cast<int32_t>(localization_result) < 0) return localization_result;
      const auto strings = rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(0x81AA3EC4));
      if (!strings) return X_E_NOTFOUND;
      // hudbkgnd.xur is built by the original host before app scene properties
      // can reference its background controller at 81A9DE68.
      uint64_t background[] = {hud, skin->hmodule};
      const auto background_result = static_cast<X_HRESULT>(dispatcher->ExecuteTrap(thread, 0x816929B8, background, 2));
      REXLOG_INFO("17559 system UI: original HUD background -> {:08X}", uint32_t(background_result));
      if (static_cast<int32_t>(background_result) < 0) return background_result;
      // Complete the original host startup (816CEBA8-816CEBB8). The popup
      // compositor 816CE060 requires both fields before either its standalone
      // render path or its HUD render path. Without them notify.xur still
      // advances and plays its sound, but returns before drawing anything.
      Write32(*memory, 0x81AA2190, 1);  // host +176: graphics host ready
      Write32(*memory, 0x81AA2194, 1);  // host +180: popup composition enabled
      ui_ready = true;
    }
    if (notification) {
      if (!notification_host_ready) {
        // 816CEA98-816CEAB4 constructs the original notification presenter and
        // binds the system DC. The original compositor already updates it.
        const auto presenter = static_cast<uint32_t>(dispatcher->ExecuteTrap(thread, 0x816AB9C0, nullptr, 0));
        if (!presenter) return static_cast<X_HRESULT>(0x8007000Eu);
        uint64_t initialize[] = {presenter, Read32(*Memory(runtime), 0x81AA2110)};
        if (!dispatcher->ExecuteTrap(thread, 0x81692978, initialize, 2)) return X_E_FAIL;
        Write32(*Memory(runtime), kNotificationHost, presenter);
        notification_host_ready = true;
      }
      uint64_t args[] = {request};
      const auto queued = static_cast<X_HRESULT>(rex::xam::RunSystemTask([&] {
        return uint32_t(dispatcher->ExecuteTrap(thread, 0x816AC0A8, args, 1));
      }));
      if (static_cast<int32_t>(queued) >= 0 && queued != 1) {
        // 816C88E8 acquires one outstanding notification reference on the
        // inactive -> active edge. Its worker wake is replaced by VdSwap here.
        if (!Read32(*Memory(runtime), kNotificationVisible))
          Write32(*Memory(runtime), 0x81AA2104, Read32(*Memory(runtime), 0x81AA2104) + 1);
        Write32(*Memory(runtime), kNotificationVisible, 1);
        REXLOG_INFO("17559 system UI: original notification queued");
      }
      return queued;
    }
    auto module = LoadUiModule(runtime, UiPath(application->path), false);
    if (!module) return X_E_NOTFOUND;
    uint64_t attach[] = {module->hmodule, 1, 0};
    const auto attached = dispatcher->ExecuteTrap(thread, module->entry, attach, 3);
    auto unload = [&runtime, dispatcher, module] {
      auto* thread = rex::kernel::KernelThread::GetCallingThread()->GetThreadState();
      uint64_t detach[] = {module->hmodule, 0, 0};
      dispatcher->ExecuteTrap(thread, module->entry, detach, 3);
      runtime.GetModuleRegistry().UnloadUserModule(module->handle, false);
    };
    auto registration = xam->FindSystemApp(0xFF);
    if (!attached || !registration || registration->module != module->hmodule ||
        registration->resource_id != application->resource_id) {
      REXLOG_ERROR("17559 system UI: original DLL did not register its callback");
      if (attached) unload();
      else runtime.GetModuleRegistry().UnloadUserModule(module->handle, false);
      return X_E_FAIL;
    }
    const auto owned_request = size ? Memory(runtime)->SystemHeapAlloc(size) : 0;
    if (size && !owned_request) {
      unload();
      return static_cast<X_HRESULT>(0x8007000Eu);
    }
    uint64_t resource_registration[] = {registration->resource_id, module->hmodule};
    dispatcher->ExecuteTrap(thread, 0x817DCBB8, resource_registration, 2);
    if (size)
      std::memcpy(Memory(runtime)->TranslateVirtual(owned_request), Memory(runtime)->TranslateVirtual(request), size);
    const auto user = unrequested ? *load_user
        : rex::memory::load_and_swap<uint32_t>(Memory(runtime)->TranslateVirtual(owned_request + application->user_offset));
    auto release_module = [&runtime, dispatcher, registration, owned_request, xam, unload] {
      auto* thread = rex::kernel::KernelThread::GetCallingThread()->GetThreadState();
      uint64_t unregister_resource[] = {registration->resource_id};
      dispatcher->ExecuteTrap(thread, 0x817DCBD8, unregister_resource, 1);
      unload();
      if (owned_request) Memory(runtime)->SystemHeapFree(owned_request);
      xam->EndSystemUi(registration->module);
      active_application = nullptr;
    };
    if (!xam->BeginSystemUi(module->hmodule, registration->slot, user)) {
      release_module();
      return X_E_FAIL;
    }
    const uint32_t context = Memory(runtime)->SystemHeapAlloc(20);
    if (!context) { release_module(); return static_cast<rex::kernel::X_HRESULT>(0x8007000Eu); }
    auto put = [&](uint32_t offset, uint32_t value) {
      rex::memory::store_and_swap<uint32_t>(Memory(runtime)->TranslateVirtual(context + offset), value);
    };
    // Original XAM 17559 816BCE74-816BCED8: first-load flags=1, copied
    // request, hmodule, user; arg2 points to the module's output-size word.
    // A suspended app is restored with flags=2 and its serialized state.
    const auto restored = restore_state.empty() ? 0
        : Memory(runtime)->SystemHeapAlloc(uint32_t(restore_state.size()));
    if (restored)
      std::memcpy(Memory(runtime)->TranslateVirtual(restored), restore_state.data(), restore_state.size());
    // Keep the state alive until scene creation and layout have completed.
    struct GuestAllocation {
      rex::kernel::GuestAddressSpace* memory; uint32_t address;
      ~GuestAllocation() { if (address) memory->SystemHeapFree(address); }
    } state_lifetime{Memory(runtime), restored};
    put(0, restored ? 2 : 1); put(4, restored ? restored : owned_request); put(8, module->hmodule);
    put(12, user);
    put(16, 0);
    uint64_t initialize[] = {0x80000004, context, context + 16};
    const auto result = static_cast<rex::kernel::X_HRESULT>(dispatcher->ExecuteTrap(thread, registration->callback, initialize, 3));
    REXLOG_INFO("17559 system UI: {} original {} -> {:08X}", application->name,
                restored ? "restore" : "initialize", uint32_t(result));
    const auto saved_size = rex::memory::load_and_swap<uint32_t>(Memory(runtime)->TranslateVirtual(context + 16));
    Memory(runtime)->SystemHeapFree(context);
    // Sign-in 901050FC-90105124 and vk 917DE4C0-917DE4E8 delete and
    // clear their own instance when initialization fails.
    const bool cleans_failed_initialization = application == &kSignin ||
        application == &kKeyboard || application == &kBrandedKeyboard;
    if (static_cast<int32_t>(result) < 0) {
      if (!cleans_failed_initialization) {
        uint64_t destroy[] = {0x80000005, 0, 0};
        dispatcher->ExecuteTrap(thread, registration->callback, destroy, 3);
      }
      release_module();
      return result;
    }
    const auto output = Memory(runtime)->SystemHeapAlloc(4);
    if (!output) {
      uint64_t destroy[] = {0x80000005, 0, 0};
      dispatcher->ExecuteTrap(thread, registration->callback, destroy, 3);
      release_module();
      return static_cast<rex::kernel::X_HRESULT>(0x8007000Eu);
    }
    const auto dc = rex::memory::load_and_swap<uint32_t>(Memory(runtime)->TranslateVirtual(0x81AA2110));
    rex::memory::store_and_swap<uint32_t>(Memory(runtime)->TranslateVirtual(output), 0);
    // Original host 816CE5EC-816CE628 supplies its DC and receives the app's root.
    uint64_t prepare_scene[] = {0x8000000B, 0, 0};
    dispatcher->ExecuteTrap(thread, registration->callback, prepare_scene, 3);
    uint64_t create_scene[] = {0x80000008, dc, output};
    const auto scene_result = dispatcher->ExecuteTrap(thread, registration->callback, create_scene, 3);
    const auto scene = rex::memory::load_and_swap<uint32_t>(Memory(runtime)->TranslateVirtual(output));
    REXLOG_INFO("17559 system UI: original create scene -> {:08X}, root={:08X}, dc={:08X}", uint32_t(scene_result), scene, dc);
    // Original host 816CE5EC-816CE65C attaches the returned scene to its
    // container. Exercise attachment/layout before enabling the compositor:
    // scene construction alone never produces an on-screen frame.
    if (static_cast<int32_t>(scene_result) >= 0 && scene) {
      const auto hud = rex::memory::load_and_swap<uint32_t>(Memory(runtime)->TranslateVirtual(0x81AA2160));
      uint64_t attach_scene[] = {hud, output};
      const auto linked = dispatcher->ExecuteTrap(thread, 0x81694FF8, attach_scene, 2);
      REXLOG_INFO("17559 system UI: original attach scene -> {:08X}", uint32_t(linked));
      if (static_cast<int32_t>(linked) >= 0) {
        auto active_scene = std::make_shared<uint32_t>(scene);
        auto destroy_scene = [&runtime, dispatcher, hud, active_scene, registration, release_module](
                                 bool destroy_app = true, bool release = true) {
          auto* thread = rex::kernel::KernelThread::GetCallingThread()->GetThreadState();
          // App owns the scene. Unlink and clear the host's reference before
          // its destroy callback and original DLL detach release the app.
          if (*active_scene) {
            uint64_t unlink[] = {*active_scene};
            dispatcher->ExecuteTrap(thread, 0x817BCBE8, unlink, 1);
            *active_scene = 0;
          }
          rex::memory::store_and_swap<uint32_t>(Memory(runtime)->TranslateVirtual(hud + 0x3C), 0);
          const auto container = rex::memory::load_and_swap<uint32_t>(Memory(runtime)->TranslateVirtual(hud + 0x40));
          uint64_t hide[] = {container, 0};
          dispatcher->ExecuteTrap(thread, 0x817B4D00, hide, 2);
          if (destroy_app) {
            uint64_t destroy[] = {0x80000005, 0, 0};
            dispatcher->ExecuteTrap(thread, registration->callback, destroy, 3);
          }
          if (release) release_module();
        };
        const auto root = rex::memory::load_and_swap<uint32_t>(Memory(runtime)->TranslateVirtual(hud + 8));
        uint64_t layout[] = {root};
        const auto laid_out = dispatcher->ExecuteTrap(thread, 0x817BCAA0, layout, 1);
        REXLOG_INFO("17559 system UI: original layout -> {:08X}, canvas={:08X}", uint32_t(laid_out), root);
        if (static_cast<int32_t>(laid_out) < 0) {
          destroy_scene();
          Memory(runtime)->SystemHeapFree(output);
          return static_cast<X_HRESULT>(laid_out);
        }
        if (application == &kGuide && !system_ui_visible) {
          const auto opening = BeginGuideBackground(runtime, thread, hud);
          if (static_cast<int32_t>(opening) < 0 && opening != 0x8000000Au) {
            REXLOG_ERROR("17559 system UI: original Guide background failed {:08X}", opening);
            destroy_scene();
            Memory(runtime)->SystemHeapFree(output);
            return static_cast<X_HRESULT>(opening);
          }
        }
        // The original dashboard resolves/presents this target after its XUI
        // present call. Composite the original HUD here, with no extra swap,
        // image copy, window, blocking loop, or per-frame resource creation.
        std::vector<uint8_t> request_bytes(size);
        if (size) std::memcpy(request_bytes.data(), Memory(runtime)->TranslateVirtual(owned_request), size);
        draw_ui = [&runtime, dispatcher, hud, registration, xam, destroy_scene, release_module, active_scene, saved_size, user,
                   message, cleans_failed_initialization, request_bytes = std::move(request_bytes), frame = 0u,
                   close_transition = SystemUiCloseTransition{}]() mutable -> bool {
          auto* thread = rex::kernel::KernelThread::GetCallingThread()->GetThreadState();
          auto close_frame = [&] {
            if (close_transition.Advance(runtime, thread, hud)) return true;
            release_module();
            REXLOG_INFO("17559 system UI: original closing transition ended");
            return false;
          };
          // The app has already been destroyed, as in 816CE1B4..816CE204.
          // Only the original HUD background runs until 81695880 completes.
          if (close_transition.started()) return close_frame();
          if (xam->ConsumeSystemUiResume(registration->module)) {
            // Match the original XAM stack lifecycle: serialize (80000009),
            // destroy the application, then restore a new instance with flags=2.
            const auto resume = saved_size && saved_size <= 1024 * 1024
                ? Memory(runtime)->SystemHeapAlloc(24 + saved_size) : 0;
            if (!resume) {
              REXLOG_ERROR("17559 system UI: invalid save size or allocation failure ({})", saved_size);
              destroy_scene();
              return false;
            }
            uint64_t save[] = {0x80000009, resume + 24, 0};
            const auto saved = dispatcher->ExecuteTrap(thread, registration->callback, save, 3);
            // S_FALSE leaves the original sign-in state buffer untouched.
            if (saved != 0) {
              Memory(runtime)->SystemHeapFree(resume);
              destroy_scene();
              return false;
            }
            uint64_t unlink[] = {*active_scene};
            dispatcher->ExecuteTrap(thread, 0x817BCBE8, unlink, 1);
            *active_scene = 0;
            rex::memory::store_and_swap<uint32_t>(Memory(runtime)->TranslateVirtual(hud + 0x3C), 0);
            uint64_t destroy[] = {0x80000005, 0, 0};
            dispatcher->ExecuteTrap(thread, registration->callback, destroy, 3);
            auto* words = Memory(runtime)->TranslateVirtual<rex::be_u32*>(resume);
            words[0] = 2; words[1] = resume + 24; words[2] = registration->module;
            words[3] = user; words[4] = 0; words[5] = 0;
            uint64_t args[] = {0x80000004, resume, resume + 16};
            const auto result = dispatcher->ExecuteTrap(thread, registration->callback, args, 3);
            auto failed_restore = [&](const char* stage, uint32_t error, bool initialized = true) {
              REXLOG_ERROR("17559 system UI: original restore {} failed {:08X}", stage, error);
              Memory(runtime)->SystemHeapFree(resume);
              destroy_scene(initialized || !cleans_failed_initialization);
              return false;
            };
            if (static_cast<int32_t>(result) < 0)
              return failed_restore("init", uint32_t(result), false);
            uint64_t prepare[] = {0x8000000B, 0, 0};
            const auto prepared = dispatcher->ExecuteTrap(thread, registration->callback, prepare, 3);
            if (static_cast<int32_t>(prepared) < 0)
              return failed_restore("prepare", uint32_t(prepared));
            const auto dc = rex::memory::load_and_swap<uint32_t>(Memory(runtime)->TranslateVirtual(0x81AA2110));
            uint64_t create[] = {0x80000008, dc, resume + 20};
            const auto created = dispatcher->ExecuteTrap(thread, registration->callback, create, 3);
            *active_scene = words[5];
            if (static_cast<int32_t>(created) < 0 || !*active_scene)
              return failed_restore("scene", uint32_t(created));
            uint64_t attach[] = {hud, resume + 20};
            const auto attached = dispatcher->ExecuteTrap(thread, 0x81694FF8, attach, 2);
            if (static_cast<int32_t>(attached) < 0)
              return failed_restore("attach", uint32_t(attached));
            const auto root = rex::memory::load_and_swap<uint32_t>(Memory(runtime)->TranslateVirtual(hud + 8));
            uint64_t layout[] = {root};
            const auto laid_out = dispatcher->ExecuteTrap(thread, 0x817BCAA0, layout, 1);
            if (static_cast<int32_t>(laid_out) < 0)
              return failed_restore("layout", uint32_t(laid_out));
            Memory(runtime)->SystemHeapFree(resume);
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
          const auto closing = xam->GetSystemUiSession();
          if (closing.close_requested) {
            if (GetSystemUiSettings().storage_io_diagnostics.Get() && message == 0x2100B && request_bytes.size() == 40) {
              auto* memory = Memory(runtime);
              const auto device = rex::memory::load_and_swap<uint32_t>(request_bytes.data() + 24);
              const auto io = rex::memory::load_and_swap<uint32_t>(request_bytes.data() + 28);
              if (device && io && memory->IsGuestRangeAccessible(device, 4, rex::kernel::kMemoryProtectRead) &&
                  memory->IsGuestRangeAccessible(io, sizeof(rex::kernel::XOVERLAPPED),
                                                         rex::kernel::kMemoryProtectRead)) {
                const auto* result = memory->TranslateVirtual<rex::kernel::XOVERLAPPED*>(io);
                REXLOG_INFO("17559 storage selector: device={:08X} io={:08X} result={:08X} extended={:08X}",
                            Read32(*memory, device), io, uint32_t(result->result), uint32_t(result->extended_error));
                  // Native 92306CF8 uses object+112/+116 for destination/I/O.
                  // Observe only a committed original controller, for at most
                  // 120 frames; stop as soon as its vtable changes on teardown.
                  if (device > 112 && io == device + 4 &&
                      memory->IsGuestRangeAccessible(device - 112, 144,
                                                              rex::kernel::kMemoryProtectRead)) {
                    const auto vtable = Read32(*memory, device - 112);
                    if (vtable >= 0x92000000 && vtable < 0x93028000) {
                      storage_observe_object = device - 112;
                      storage_observe_vtable = vtable;
                      storage_observe_frames = 120;
                      storage_observe_last.fill(~0u);
                    }
                  }
              }
            }
            // CreateProfile's completion calls UnloadStack, not UnloadSelf.
            // Its completed parents must not be restored. A successor queued
            // by the same callback still runs after this frame's destruction.
            if (closing.close_stack_requested) {
              for (const auto& app : suspended_apps)
                ReleaseDiscardedSystemUiState(*Memory(runtime), app.message, app.state);
              suspended_apps.clear();
            }
            // Closing because this app requested another one: keep it below
            // the successor, serialized by its own 80000009 handler.
            if (!closing.close_stack_requested && pending_ui && saved_size && saved_size <= 1024 * 1024) {
              const auto buffer = Memory(runtime)->SystemHeapAlloc(saved_size);
              if (buffer) {
                uint64_t save[] = {0x80000009, buffer, 0};
                const auto saved = dispatcher->ExecuteTrap(thread, registration->callback, save, 3);
                if (saved == 0) {
                  const auto* bytes = Memory(runtime)->TranslateVirtual<uint8_t*>(buffer);
                  suspended_apps.push_back({message, request_bytes, {bytes, bytes + saved_size}, user});
                  REXLOG_INFO("17559 system UI: suspended {:08X} below its successor ({} bytes)", message, saved_size);
                } else if (static_cast<int32_t>(saved) < 0) {
                  REXLOG_ERROR("17559 system UI: original save failed {:08X}", uint32_t(saved));
                } else {
                  REXLOG_INFO("17559 system UI: original app declined suspension {:08X}", uint32_t(saved));
                }
                Memory(runtime)->SystemHeapFree(buffer);
              }
            }
            const bool final_close = !pending_ui && suspended_apps.empty();
            // App-to-app handoffs retain the compositor without closing its
            // background. Only the final return to the title animates out.
            // Keep the module/session until then so title input stays captured.
            destroy_scene(true, !final_close);
            REXLOG_INFO("17559 system UI: closed original app after {} frames", frame);
            if (final_close) return close_frame();
            return false;
          }
          // Original HUD frame 816CD6E8: when the kernel accepts another system
          // command buffer, 816CCF00 times, lays out and renders XUI, draws the
          // title's front buffer behind it, and publishes the recorded frame.
          if (dispatcher->ExecuteTrap(thread, 0x81842110, nullptr, 0)) {
            uint64_t compose[] = {Read32(*Memory(runtime), kSystemTarget)};
            dispatcher->ExecuteTrap(thread, 0x816CCF00, compose, 1);
            if (frame == 0 || frame % 600 == 0)
              REXLOG_INFO("17559 system UI: composed original system frame {}", frame);
            ++frame;
          }
          return true;
        };
        active_application = application;
        // 816CE4B8-816CE4D0: a shown app fades the title frame in the system pass.
        Write32(*Memory(runtime), kFadeTarget, Read32(*Memory(runtime), kFadeShown));
        Write32(*Memory(runtime), kDisplayChanged, 1);
        Write32(*Memory(runtime), kHudVisible, 1);
        xam->SetSystemUiVisible(module->hmodule);
        if (!system_ui_visible) {
          runtime.GetNotificationBroadcaster().BroadcastNotification(0x9, true);
          system_ui_visible = true;
        }
        Memory(runtime)->SystemHeapFree(output);
        return 0;
      }
    }
    Memory(runtime)->SystemHeapFree(output);
    uint64_t destroy[] = {0x80000005, 0, 0};
    dispatcher->ExecuteTrap(thread, registration->callback, destroy, 3);
    release_module();
    return static_cast<int32_t>(scene_result) < 0 ? static_cast<X_HRESULT>(scene_result) : X_E_FAIL;
  };
}
}

void dash17559::DrawSystemUiFrame(PPCContext& ctx) {
  auto* frame_runtime = REX_RUNTIME();
  if (!frame_runtime) return;
  x360n::SystemUiExecution::Scope execution(dash17559::ui_execution);
  if (!execution) return;
  if (GetSystemUiSettings().storage_io_diagnostics.Get())
    dash17559::ObserveStorageController(*Memory(*frame_runtime));
  // The original renderer also reaches this context through ThreadState. Keep
  // the frame bridge free of REX_FUNC's restrict qualifier and restore even
  // when guest execution unwinds (for example an Xbox Home request).
  x360n::SystemUiContext frame(*Memory(*frame_runtime));
  if (frame.thread_state()->GetContext() != &ctx) {
    REXLOG_ERROR("17559 system UI: frame context does not belong to the current guest thread");
    return;
  }
  if (dash17559::achievement_notices_pending.load(std::memory_order_acquire)) {
    std::vector<dash17559::AchievementNotice> notices;
    {
      std::lock_guard lock(dash17559::achievement_notices_mutex);
      notices.swap(dash17559::achievement_notices);
      dash17559::achievement_notices_pending.store(false, std::memory_order_release);
    }
    auto& runtime = *REX_RUNTIME();
    auto* memory = Memory(runtime);
    auto* xam = &SystemUI();
    for (const auto& notice : notices) {
      const auto identity = TitleServices().GetUserProfile().identity();
      if (notice.user || !identity.signin_state || identity.xuid != notice.xuid) continue;
      std::vector<rex::xam::OfflineAchievementRecord> records;
      const auto metadata = TitleServices().GetOfflineAchievements().List(notice.xuid, notice.title, records);
      if (metadata != rex::xam::OfflineAchievementResult::kSuccess) {
        REXLOG_ERROR("17559 achievement notification: committed metadata unavailable for {:08X}/{}", notice.title, notice.achievement);
        continue;
      }
      std::u16string notification_text;
      for (const auto& record : records) {
        if (record.info.id != notice.achievement || !record.unlock_filetime) continue;
        // 8190FBE8 uses the original UTF16 format at 8162F678: "%d|%s".
        // The scene supplies its localized heading, icon, sound and animation.
        notification_text = rex::string::ToUTF16(std::to_string(record.info.gamerscore) + "|" + record.info.label);
        if (notification_text.size() > 254) {
          notification_text.resize(254);
          if (notification_text.back() >= 0xD800 && notification_text.back() <= 0xDBFF)
            notification_text.pop_back();
        }
        break;
      }
      if (notification_text.empty()) continue;
      const uint32_t bytes = 40 + uint32_t((notification_text.size() + 1) * 2);
      const auto request = memory->SystemHeapAlloc(bytes);
      if (!request) {
        REXLOG_ERROR("17559 achievement notification: out of guest memory");
        continue;
      }
      auto* words = memory->TranslateVirtual<rex::be_u32*>(request);
      std::memset(words, 0, bytes);
      words[0] = 27; words[1] = notice.user; words[2] = 2; words[3] = request + 40;
      words[9] = notice.title;
      auto* text = memory->TranslateVirtual<rex::be_u16*>(request + 40);
      for (size_t i = 0; i < notification_text.size(); ++i) text[i] = notification_text[i];
      const auto result = xam->system_ui_request(0x21013, request, 40);
      memory->SystemHeapFree(request);
      if (static_cast<int32_t>(result) < 0)
        REXLOG_ERROR("17559 original achievement notification failed {:08X}: {:08X}/{}", uint32_t(result), notice.title, notice.achievement);
    }
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
  }
  if (!dash17559::draw_ui && !dash17559::pending_ui && !dash17559::suspended_apps.empty()) {
    // Also resume the parent when its queued successor failed to load.
    dash17559::pending_ui = [app = std::move(dash17559::suspended_apps.back())] {
      auto& runtime = *REX_RUNTIME();
      auto* xam = &SystemUI();
      const auto buffer = app.request.empty() ? 0 : Memory(runtime)->SystemHeapAlloc(uint32_t(app.request.size()));
      if (!app.request.empty() && !buffer) { REXLOG_ERROR("17559 system UI: restore request allocation failed"); return; }
      if (buffer) std::memcpy(Memory(runtime)->TranslateVirtual(buffer), app.request.data(), app.request.size());
      dash17559::restore_state = app.state;
      if (app.request.empty()) dash17559::load_user = app.user;
      const auto result = xam->system_ui_request(app.message, buffer, uint32_t(app.request.size()));
      dash17559::restore_state.clear();
      dash17559::load_user.reset();
      if (buffer) Memory(runtime)->SystemHeapFree(buffer);
      REXLOG_INFO("17559 system UI: restored suspended {:08X} -> {:08X}", app.message, uint32_t(result));
    };
    dash17559::suspended_apps.pop_back();
  }
  // Original 816CE22C..816CE348 switches to the pending/suspended app before
  // its final hide path (816CE390..816CE478). A module change is not a return
  // to the title: retain the system target, dimming and UI notification while
  // the next original app is loaded. Do not publish an empty system frame.
  if (!dash17559::draw_ui && dash17559::pending_ui) {
    auto request = std::move(dash17559::pending_ui);
    request();
  }
  if (!dash17559::draw_ui && !dash17559::pending_ui &&
      dash17559::suspended_apps.empty() && dash17559::system_ui_visible) {
    dash17559::HideSystemFrame(*REX_RUNTIME(), frame.thread_state());
    frame_runtime->GetNotificationBroadcaster().BroadcastNotification(0x9, false);
    dash17559::system_ui_visible = false;
  }
  // A notification is a non-modal system frame: the original presenter owns
  // animation and expiry; it never captures game controls or opens the Guide.
  // With no pending notification this path has no guest call or allocation.
  if (!dash17559::draw_ui && dash17559::notification_host_ready) {
    auto& runtime = *REX_RUNTIME();
    auto* memory = Memory(runtime);
    if (dash17559::Read32(*memory, dash17559::kNotificationVisible) &&
        Functions(runtime)->ExecuteTrap(frame.thread_state(), 0x81842110, nullptr, 0)) {
      dash17559::SystemCompositorContext compositor(*memory, frame.thread_state());
      uint64_t compose[] = {dash17559::Read32(*memory, dash17559::kSystemTarget)};
      Functions(runtime)->ExecuteTrap(frame.thread_state(), 0x816CCF00, compose, 1);
      // 816CD678-816CD6BC publishes the empty descriptor when the last
      // notification expires with no HUD. Do not drain the buffers twice.
    }
  }
}
