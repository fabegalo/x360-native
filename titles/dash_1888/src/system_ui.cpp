#include "system_ui.h"
#include "system_ui_request.h"
#include "system_ui_statics.h"
#include "boot_intro.h"
#include "guide_diagnostic.h"
#include "../../common/system_ui_context.h"
#include "../../common/system_ui_execution.h"
#include "../../common/guide_button.h"
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/heap.h>
#include <rex/kernel/module.h>
#include <rex/kernel/object/module_object.h>
#include <rex/kernel/object/object_table.h>
#include <rex/kernel/object/kernel_thread.h>
#include <rex/kernel/object/notification_broadcaster.h>
#include <rex/kernel/xboxkrnl.h>
#include <rex/xam/title_lifecycle.h>
#include <rex/xam/system_ui.h>
#include <rex/xam/loader_state.h>
#include <rex/xam/input.h>
#include <rex/logging/log_category.h>
#include <rex/chrono.h>
#include <rex/hid/input_system.h>
#include <rex/memory.h>
#include <cstring>
#include <array>
#include <atomic>
#include <chrono>
#include <optional>
#include <mutex>
#include <vector>
#ifdef X360N_1888_UI_DIAGNOSTIC
#include <thread>
#endif

namespace dash1888 {
namespace {
struct UiModule { rex::kernel::X_HANDLE handle; uint32_t hmodule, entry; };
std::optional<UiModule> LoadUiModule(rex::kernel::TitleRuntime& runtime, const std::string& path) {
  const auto loaded = runtime.GetModuleRegistry().LoadUserModule(path, false);
  if (!loaded) return std::nullopt;
  auto module = runtime.GetObjectTable().Get<rex::kernel::ModuleObject>(*loaded);
  if (!module) return std::nullopt;
  return UiModule{*loaded, module->hmodule_ptr, module->user.entry_point};
}
rex::kernel::GuestAddressSpace* Memory(rex::kernel::TitleRuntime& runtime) {
  if (auto* thread = rex::kernel::KernelThread::GetCallingThread(); thread && &thread->GetRuntime() == &runtime)
    return thread->GetAddressSpace();
  const auto executable = runtime.GetModuleRegistry().GetExecutable();
  REX_CHECK(executable, "1888 UI: executable is unavailable");
  auto module = runtime.GetObjectTable().Get<rex::kernel::ModuleObject>(*executable);
  REX_CHECK(module && module->processor_module, "1888 UI: executable image is unavailable");
  return module->processor_module->GetAddressSpace();
}
using Bytes = std::vector<uint8_t>;
constexpr uint32_t kHost = 0x81AAED88, kDevice = 0x81AAED38, kTarget = 0x81AAED3C;
struct Request { uint32_t message, user; Bytes bytes, saved; };
struct Active {
  Request request;
  UiModule module;
  uint32_t callback = 0, scene = 0, saved_size = 0, storage = 0;
};
std::optional<Request> pending;
std::mutex request_mutex;
std::atomic<bool> ui_active = false;
std::optional<Active> active;
std::vector<Request> suspended;
x360n::SystemUiExecution execution;
bool initialized = false, attempted = false, reserved = false;
bool HasPending() { std::lock_guard lock(request_mutex); return pending.has_value(); }
std::optional<Request> TakePending() {
  std::lock_guard lock(request_mutex);
  auto request = std::move(pending); pending.reset(); return request;
}
bool Enqueue(Request request) {
  std::lock_guard lock(request_mutex);
  if (pending) return false;
  pending = std::move(request); reserved = false; return true;
}
rex::kernel::xboxkrnl::SystemSwapFrame title_frame{};
uint32_t Read(rex::kernel::TitleRuntime& rt, uint32_t address) {
  return rex::memory::load_and_swap<uint32_t>(Memory(rt)->TranslateVirtual(address));
}
void Put(rex::kernel::TitleRuntime& rt, uint32_t address, uint32_t value) {
  rex::memory::store_and_swap<uint32_t>(Memory(rt)->TranslateVirtual(address), value);
}
uint64_t Call(rex::kernel::TitleRuntime& rt, uint32_t address, std::initializer_list<uint64_t> values = {}) {
  (void)rt;
  auto* thread = rex::kernel::KernelThread::GetCallingThread();
  REX_CHECK(thread, "1888 UI: guest callback requires a calling thread");
  std::vector<uint64_t> args(values);
  uint64_t result = 0;
  REX_CHECK(thread->CallGuestFunction(address, args, &result), "1888 UI: missing original callback {:08X}", address);
  return result;
}
bool Ok(uint64_t result) { return int32_t(uint32_t(result)) >= 0; }
auto* Services() {
  auto* services = REX_INTERFACE(rex::xam::IXAMTitleServices);
  REX_CHECK(services, "1888 UI: XAM services unavailable");
  return services;
}
auto* Xam(rex::kernel::TitleRuntime&) { return &Services()->GetSystemUI(); }

void Hide(rex::kernel::TitleRuntime& rt) {
  Put(rt, kHost + 0x80, 0);
  // 818DACB8 publishes this same empty system descriptor. Avoid its busy wait
  // here: the next title swap consumes the descriptor through the SDK.
  Call(rt, 0x818EF860, {0, 0, *Memory(rt)->TranslateVirtual<uint8_t*>(0x81AAEBAC), 0, 0});
}
bool NotifyDisplay(rex::kernel::TitleRuntime& rt) {
  const auto data = Memory(rt)->SystemHeapAlloc(152);
  if (!data) return false;
  Call(rt, 0x81A9B494, {data}); // Original VdGetCurrentDisplayInformation thunk.
  auto* bytes = Memory(rt)->TranslateVirtual<uint8_t*>(data);
  const auto width = rex::memory::load_and_swap<uint16_t>(bytes + 0x48);
  const auto height = rex::memory::load_and_swap<uint16_t>(bytes + 0x4A);
  if (title_frame.width && title_frame.height) {
    rex::memory::store_and_swap<uint16_t>(bytes, uint16_t(title_frame.width));
    rex::memory::store_and_swap<uint16_t>(bytes + 2, uint16_t(title_frame.height));
    rex::memory::store_and_swap<uint32_t>(bytes + 0x10, title_frame.width);
    rex::memory::store_and_swap<uint32_t>(bytes + 0x14, title_frame.height);
  }
  std::memcpy(bytes + 88, bytes, 4);
  rex::memory::store_and_swap<uint16_t>(bytes + 92, width);
  rex::memory::store_and_swap<uint16_t>(bytes + 94, height);
  std::memcpy(bytes + 96, bytes + 8, 56);
  Call(rt, 0x818DD7C0, {0, 3, data});
  Call(rt, 0x818DD7C0, {0, 1, data + 88});
  Memory(rt)->SystemHeapFree(data);
  return true;
}
bool Initialize(rex::kernel::TitleRuntime& rt) {
  if (initialized) return true;
  if (attempted) return false;
  attempted = true;
  const auto xam = LoadUiModule(rt, "game:\\xam.xex");
  if (!xam) return false;
  // 818E7EA0 stores this identity before any section:// resource resolution.
  // Its network/title scheduler entry point is deliberately not executed.
  Put(rt, 0x81AAF6B8, xam->hmodule);
  for (const auto& initializer : kUiStaticInitializers)
    Call(rt, initializer.function, {initializer.object});
  if (!Read(rt, 0x81AB1208) || !Read(rt, 0x81AB1228)) {
    REXLOG_ERROR("1888 system UI: original XUI static constructors failed");
    return false;
  }
  REXLOG_INFO("1888 system UI: original XUI resource and text constructors completed");
  auto event = [&](uint32_t address, uint8_t type) {
    std::memset(Memory(rt)->TranslateVirtual(address), 0, 16);
    *Memory(rt)->TranslateVirtual<uint8_t*>(address) = type;
    Put(rt, address + 8, address + 8); Put(rt, address + 12, address + 8);
  };
  // Original host initialization 818DE7A8 and worker clock setup 818DE1F0.
  event(kHost + 0x40, 0); event(kHost + 0x50, 1); event(0x81AAEB38, 0);
  const auto frequency = Call(rt, 0x81A9B2E4);
  rex::memory::store_and_swap<uint64_t>(Memory(rt)->TranslateVirtual(kHost + 0x70), frequency);
  rex::memory::store_and_swap<uint64_t>(Memory(rt)->TranslateVirtual(kHost + 0x78),
                                       rex::Clock::QueryGuestTickCount());
  auto stage = [&](const char* name, uint32_t address) {
    const auto result = Call(rt, address);
    REXLOG_INFO("1888 system UI: {} -> {:08X}", name, uint32_t(result));
    return Ok(result);
  };
  if (!stage("system device", 0x818DADC8) || !Read(rt, kDevice) || !NotifyDisplay(rt) ||
      !stage("graphics resources", 0x818DC680) || !stage("XUI", 0x818DAED0)) return false;
  Call(rt, 0x81937D38, {0x81AAECB8, 0x81AAED50});
  for (auto constructor : {0x818C0518u, 0x818CF2F8u, 0x818D3D10u, 0x818CECD8u})
    Call(rt, constructor); // The original host does not consume these void results.
  const auto hud = Memory(rt)->SystemHeapAlloc(80);
  if (!hud) return false;
  Call(rt, 0x818D0BC8, {hud});
  Put(rt, kHost + 0x68, hud);
  if (!Call(rt, 0x818D5698, {hud, Read(rt, kHost + 0x28)}) ||
      !stage("localization", 0x818DAFE0) ||
      !Ok(Call(rt, 0x818CF948, {hud, 0x81872840})) ||
      !Call(rt, 0x818D20F0, {hud}) || !Read(rt, hud + 0x40)) {
    REXLOG_ERROR("1888 system UI: original HUD initialization failed (container={:08X})", Read(rt, hud + 0x40));
    return false;
  }
  initialized = true;
  REXLOG_INFO("1888 system UI: original HUD/XUI ready, dc={:08X}, host={:08X}, root={:08X}, container={:08X}",
              Read(rt, kHost + 0x28), hud, Read(rt, hud + 8), Read(rt, hud + 0x40));
  return true;
}

void Close(rex::kernel::TitleRuntime& rt, bool keep) {
  if (!active) return;
  auto xam = Xam(rt);
  auto& app = *active;
  if (keep && app.saved_size && app.saved_size <= 1024 * 1024) {
    const auto buffer = Memory(rt)->SystemHeapAlloc(app.saved_size);
    if (buffer) {
      if (Ok(Call(rt, app.callback, {0x80000009, buffer, 0}))) {
        const auto* bytes = Memory(rt)->TranslateVirtual<uint8_t*>(buffer);
        app.request.saved.assign(bytes, bytes + app.saved_size);
        suspended.push_back(std::move(app.request));
      } else REXLOG_ERROR("1888 system UI: original application could not save its state");
      Memory(rt)->SystemHeapFree(buffer);
    }
  }
  const auto hud = Read(rt, kHost + 0x68);
  // 818D174C closes the host's stacked dialog before releasing the app scene.
  // Its completion callback must run while the original application is loaded.
  if (const auto dialog = Read(rt, hud + 0x38)) Call(rt, 0x818D0D80, {dialog});
  if (app.scene) Call(rt, 0x81913998, {app.scene}); // App owns the root scene.
  Put(rt, hud + 0x3C, 0);
  Call(rt, 0x818D5440, {Read(rt, hud + 0x40), 0});
  Call(rt, app.callback, {0x80000005, 0, 0});
  if (GetSystemUiSettings().ui_lifecycle_trace.Get()) {
    Put(rt, app.storage + 20, 0);
    const auto resolved = Call(rt, 0x81907780, {app.scene, app.storage + 20});
    REXLOG_INFO("1888 diagnostic: app destroyed message={:X}, scene={:08X}, remaining object={:08X}, host state={}",
                app.request.message, app.scene, Ok(resolved) ? Read(rt, app.storage + 20) : 0,
                Read(rt, Read(rt, hud + 0x38) + 12));
  }
  Call(rt, 0x8192C060, {ApplicationFor(app.request.message)->resource});
  Call(rt, app.module.entry, {app.module.hmodule, 0, 0});
  const auto owner = app.module.hmodule;
  rt.GetModuleRegistry().UnloadUserModule(app.module.handle, false);
  Memory(rt)->SystemHeapFree(app.storage);
  xam->EndSystemUi(owner);
  active.reset();
  ui_active.store(false, std::memory_order_release);
  Hide(rt);
  rt.GetNotificationBroadcaster().BroadcastNotification(9, false);
}

uint32_t Open(rex::kernel::TitleRuntime& rt, Request request) {
  if (!Initialize(rt)) return 0x80004005u;
  const auto* description = ApplicationFor(request.message);
  auto* state = &rt.GetModuleRegistry(); auto xam = Xam(rt);
  const auto path = std::string("game:\\") + description->file;
  auto module = LoadUiModule(rt, path);
  if (!module) return 0x80070490u;
  const auto attached = Call(rt, module->entry, {module->hmodule, 1, 0});
  const auto registration = xam->FindSystemApp(0xFF);
  if (!attached || !registration || registration->module != module->hmodule ||
      registration->resource_id != description->resource) {
    if (attached) Call(rt, module->entry, {module->hmodule, 0, 0});
    state->UnloadUserModule(module->handle, false); return 0x80004005u;
  }
  Call(rt, 0x8192C040, {description->resource, module->hmodule});
  const auto size = request.saved.empty() ? request.bytes.size() : request.saved.size();
  const auto buffer = Memory(rt)->SystemHeapAlloc(uint32_t(24 + size));
  if (!buffer) {
    Call(rt, 0x8192C060, {description->resource});
    Call(rt, module->entry, {module->hmodule, 0, 0});
    state->UnloadUserModule(module->handle, false); return 0x8007000Eu;
  }
  const auto& bytes = request.saved.empty() ? request.bytes : request.saved;
  if (!bytes.empty()) std::memcpy(Memory(rt)->TranslateVirtual(buffer + 24), bytes.data(), bytes.size());
  // Original Guide launch 818EA744 has no initial data. A pointer to a zero
  // word selects HUD's message-box application instead (913E306C).
  Put(rt, buffer, request.saved.empty() ? 1 : 2); Put(rt, buffer + 4, bytes.empty() ? 0 : buffer + 24);
  Put(rt, buffer + 8, module->hmodule); Put(rt, buffer + 12, request.user);
  Put(rt, buffer + 16, 0); Put(rt, buffer + 20, 0);
  if (!xam->BeginSystemUi(module->hmodule, registration->slot, request.user)) {
    Memory(rt)->SystemHeapFree(buffer);
    Call(rt, 0x8192C060, {description->resource});
    Call(rt, module->entry, {module->hmodule, 0, 0});
    state->UnloadUserModule(module->handle, false); return 0x80004005u;
  }
  const auto result = Call(rt, registration->callback, {0x80000004, buffer, buffer + 16});
  const auto saved_size = Read(rt, buffer + 16);
  uint32_t scene = 0;
  auto created = result;
  if (Ok(result)) {
    created = Call(rt, registration->callback, {0x8000000B, 0, 0});
    REXLOG_INFO("1888 system UI: {} prepare -> {:08X}", description->file, uint32_t(created));
    if (Ok(created)) created = Call(rt, registration->callback,
                                  {0x80000008, Read(rt, kHost + 0x28), buffer + 20});
    scene = Read(rt, buffer + 20);
    REXLOG_INFO("1888 system UI: {} scene creation -> {:08X}, scene={:08X}",
                description->file, uint32_t(created), scene);
    if (Ok(created) && scene) created = Call(rt, 0x818D1700, {Read(rt, kHost + 0x68), buffer + 20});
  }
  REXLOG_INFO("1888 system UI: {} init={:08X} scene={:08X} result={:08X}",
              description->file, uint32_t(result), scene, uint32_t(created));
  if (!Ok(created) || !scene) {
    // Signin clears its object after failed init (90102640); do not destroy null.
    if (Ok(result)) Call(rt, registration->callback, {0x80000005, 0, 0});
    Call(rt, 0x8192C060, {description->resource});
    Call(rt, module->entry, {module->hmodule, 0, 0});
    state->UnloadUserModule(module->handle, false); xam->EndSystemUi(module->hmodule);
    Memory(rt)->SystemHeapFree(buffer);
    return Ok(created) ? 0x80004005u : uint32_t(created);
  }
  // Original callbacks may retain their request/context until destruction.
  // Keep guest storage alive across frames, not just through scene creation.
  active = Active{std::move(request), *module, registration->callback, scene, saved_size, buffer};
  ui_active.store(true, std::memory_order_release);
  Put(rt, kHost + 0x80, 1);
  Put(rt, 0x81AA0808, 1); // 818DE5B0: publish fresh compositor/scaler state for each app.
  xam->SetSystemUiVisible(module->hmodule);
  rt.GetNotificationBroadcaster().BroadcastNotification(9, true);
  return 0;
}

void ProcessApplicationInput(rex::kernel::TitleRuntime& rt) {
  const auto hud = Read(rt, kHost + 0x68);
  // 818D21E8 is the host's shutdown transition (818D1AE0), used by the
  // original worker while closing an application. Calling it here changes
  // the host from open to hidden on its very first frame. hud+38 is the
  // persistent host scene, not a pending dialog that needs to be advanced.
  Call(rt, active->callback, {0x8000000A, 0, 0});
  Call(rt, 0x818D00C8, {hud}); // One original XuiProcessInput pass per frame.
}

void Swap(const rex::kernel::xboxkrnl::SystemSwapFrame& frame) {
  if (BootIntroRunning()) return;
  if (!rex::kernel::KernelThread::IsInThread()) return;
  x360n::SystemUiExecution::Scope guard(execution);
  if (!guard) return;
  title_frame = frame;
  auto& rt = *REX_RUNTIME();
  auto xam = Xam(rt);
  for (uint32_t user = 0; user < 4; ++user) {
    if (rex::xam::PollGuideButton(user)) {
      // One line per physical press: distinguishes missing input from a Guide
      // that opened but did not present.
      REXLOG_INFO("1888 system UI: Guide button user={} (active={:X}, pending={})", user,
                  active ? active->request.message : 0, HasPending());
      if (HasPending()) {
        // A queued application is presented first; this press is not retained.
      } else if (active) {
        // The closing shortcut also belongs to this UI until release. Capture
        // its held controls before returning input ownership to the dashboard.
        xam->RequestSystemUiClose();
      } else {
        Enqueue(Request{0x21015, user, {}, {}});
      }
    }
  }
  // Opt-in service-level validation without desktop input or UI automation.
  // No clock reads in ordinary sessions.
  if (const auto probe = GetSystemUiSettings().guide_probe_seconds.Get(); probe > 0 && probe <= 120) {
    static const auto start = std::chrono::steady_clock::now();
    static uint32_t phase = 0;
    const auto cycles = std::clamp(GetSystemUiSettings().guide_probe_cycles.Get(), 1u, 3u);
    const auto phases = cycles == 1 ? 1 : 2 * cycles;
    if (phase < phases && std::chrono::steady_clock::now() - start >= std::chrono::seconds(probe + 3 * phase)) {
      if (!(phase & 1)) {
        const bool queued = !active && Enqueue(Request{0x21015, 0, {}, {}});
        REXLOG_INFO("1888 diagnostic: original Guide requested, cycle={}, queued={}", phase / 2 + 1, queued);
      } else {
        const bool closing = active && xam->RequestSystemUiClose();
        REXLOG_INFO("1888 diagnostic: original Guide close requested, cycle={}, closing={}", phase / 2 + 1, closing);
      }
      ++phase;
    }
  }
  if (!active && !HasPending() && suspended.empty()) return;
  x360n::SystemUiContext context(*Memory(rt));
  if (active && xam->GetSystemUiSession().close_requested) Close(rt, HasPending());
  if (!active && !HasPending() && !suspended.empty()) {
    if (Enqueue(suspended.back())) suspended.pop_back();
  }
  if (!active) {
    if (auto request = TakePending()) {
      const auto result = Open(rt, std::move(*request));
      if (result) REXLOG_ERROR("1888 system UI: original application request failed {:08X}", result);
    }
  }
  if (!active) return;
  if (HasPending()) xam->RequestSystemUiClose();
  if (xam->ConsumeSystemUiResume(active->module.hmodule)) {
    // The local storage service completed a nested selector. The original
    // manager serializes the app (80000009), destroys it and restores flags=2.
    // Preserve its own serialized bytes rather than synthesizing a completion page.
    const bool can_save = active->saved_size && active->saved_size <= 1024 * 1024;
    if (!can_save) REXLOG_ERROR("1888 system UI: invalid original resume state size {}", active->saved_size);
    Close(rt, can_save);
    return;
  }
  ProcessApplicationInput(rt);
  if (xam->GetSystemUiSession().close_requested) { Close(rt, HasPending()); return; }
  if (Call(rt, 0x818EEB48)) Call(rt, 0x818DC1E0, {Read(rt, kTarget)});
}
}

void InstallSystemUi(rex::kernel::TitleRuntime& rt) {
  for (const auto* file : {"signin", "createprofile", "hud", "vk", "gamerprofile"}) {
    const auto image = std::string(file) + ".xex";
    const auto library = "dash_1888_" + std::string(file);
    rt.GetModuleRegistry().RegisterRecompiledModule(image.c_str(), ("game:\\" + image).c_str(), library.c_str());
    // Original private module loads use the 1888-only mounted SystemRoot.
    rt.GetModuleRegistry().RegisterRecompiledModule(image.c_str(), ("\\SystemRoot\\" + image).c_str(), library.c_str());
  }
  auto* input = REX_INTERFACE(rex::hid::IInputSystem);
  REX_CHECK(input, "1888 UI: input service unavailable");
  x360n::ConfigureGuideButton();
  if (auto binding = REX_SETTINGS().Find("hid.keybind_guide"); binding && (*binding)->GetText().empty())
    REX_CHECK((*binding)->SetText(rex::SettingLayer::kRuntime, "G"), "1888 UI: Guide binding failed");
  // Shared dashboard defaults preserve explicit SDK input configuration.
  Xam(rt)->system_ui_request = [&rt](uint32_t message, uint32_t address, uint32_t size) -> rex::kernel::X_HRESULT {
    if (message == kReleaseApp) {
      std::lock_guard lock(request_mutex); reserved = false; return 0;
    }
    if (message == kReserveApp) {
      std::lock_guard lock(request_mutex);
      if (reserved || pending) return 0;
      reserved = true; return 1;
    }
    const auto* description = ApplicationFor(message);
    if (!description) return 0x80070490u;
    if (!address || !rex::kernel::KernelThread::IsInThread() || size > 544) return 0x80070057u;
    Request request{message, 0, {}, {}};
    if (!AdaptRequest(message, {Memory(rt)->TranslateVirtual<uint8_t*>(address), size},
                      request.bytes, request.user)) return 0x80070057u;
    if (!Enqueue(std::move(request))) return 0x80004005u;
    if (ui_active.load(std::memory_order_acquire)) Xam(rt)->RequestSystemUiClose();
    // The copied request is now owned by the presentation callback. Success
    // means queued, not signed in: only the original app can complete logon.
    return 0;
  };
  rex::kernel::xboxkrnl::SetSystemSwapCallback(rt, &Swap);
}
void ReleaseSystemUi(rex::kernel::TitleRuntime& rt) {
  rex::kernel::xboxkrnl::SetSystemSwapCallback(rt, nullptr);
  using Exit = rex::xam::XAMLoaderState::ExitRequest;
  const auto exit = Services()->GetLoaderState().exit_request;
  if (exit == Exit::Dashboard || exit == Exit::Terminate || exit == Exit::Firmware)
    execution.WaitForCallerExit();
  Xam(rt)->system_ui_request = {};
  { std::lock_guard lock(request_mutex); pending.reset(); reserved = false; }
  active.reset(); suspended.clear();
  initialized = attempted = reserved = false;
  title_frame = {};
  ui_active.store(false, std::memory_order_release);
}
#ifdef X360N_1888_UI_DIAGNOSTIC
uint32_t ProbeOriginalApplication(rex::kernel::TitleRuntime& rt, uint32_t message, uint32_t* scene, bool record_draw,
                                  bool advance_animation) {
  const auto* description = ApplicationFor(message);
  if (!description || !scene) return 0x80070057u;
  *scene = 0;
  Request request{message, 0, Bytes(description->size), {}};
  if (message == 0x21001) request.bytes[7] = 1; // One local user, same 1888 layout.
  const auto result = Open(rt, std::move(request));
  if (result) return result;
  if (!active || !active->scene) return 0x80004005u;
  *scene = active->scene;
  if (advance_animation) {
    const auto hud = Read(rt, kHost + 0x68), host_scene = Read(rt, hud + 0x38);
    const auto opening_state = Read(rt, host_scene + 12);
    if (!opening_state) { Close(rt, false); return 0x80004005u; }
    const auto draw_buffer = Memory(rt)->SystemHeapAlloc(64);
    if (!draw_buffer) { Close(rt, false); return 0x8007000Eu; }
    struct FreeBuffer { rex::kernel::TitleRuntime& rt; uint32_t buffer;
      ~FreeBuffer() { Memory(rt)->SystemHeapFree(buffer); } } free_draw{rt, draw_buffer};
    // Exercise the production input pump and the original timer/animation
    // routines without a graphics device, window or physical input.
    for (unsigned frame = 0; frame < 120; ++frame) {
      // Original UI timers use guest wall time, independently of XuiAnimRun.
      // Allow their 800 ms lifecycle callbacks to run in this bounded fixture.
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      ProcessApplicationInput(rt);
      Call(rt, 0x8192A248);
      auto* context = rex::kernel::KernelThread::GetCallingThread()->GetThreadState()->GetContext();
      context->f1.f64 = 1000.0 / 60.0;
      Call(rt, 0x81906C90);
      // Draw messages also advance original scene/focus lifecycle. A scene
      // merely existing and an animation ticking cannot prove it accepts input.
      const auto dc = Read(rt, hud + 12);
      auto drawn = Call(rt, 0x819389E0, {dc, 0xFF000000});
      if (Ok(drawn)) {
        Call(rt, 0x818DBB20, {draw_buffer + 16, draw_buffer, dc, 0xFFFFFFFF, 1});
        drawn = Call(rt, 0x8190CC08, {Read(rt, hud + 8), draw_buffer + 16});
        const auto ended = Call(rt, 0x81936020, {dc});
        if (Ok(drawn)) drawn = ended;
      }
      const bool pending_app = HasPending(), closing = Xam(rt)->GetSystemUiSession().close_requested;
      if (!Ok(drawn) || pending_app || Read(rt, host_scene + 12) != opening_state || closing) {
        REXLOG_ERROR("1888 diagnostic: unexpected Guide lifecycle frame={} state={}->{} pending={} close={} draw={:08X}",
                     frame, opening_state, Read(rt, host_scene + 12), pending_app, closing, uint32_t(drawn));
        Close(rt, false); return 0x80004005u;
      }
    }
    REXLOG_INFO("1888 diagnostic: Guide host remained open across 120 original animation frames");
  }
  if (record_draw) {
    // Exercise the same virtual call observed at manual LR 819EDBB8, even
    // when an opening scene is still transparent. Use the real constructor,
    // vtable and dispatch body, with an empty gradient and guarded storage.
    const auto object = Memory(rt)->SystemHeapAlloc(64);
    if (!object) { Close(rt, false); return 0x8007000Eu; }
    auto* bytes = Memory(rt)->TranslateVirtual<uint8_t*>(object);
    std::memset(bytes, 0, 48); std::memset(bytes + 48, 0xA5, 16);
    Call(rt, 0x819E8478, {object});
    // Select the derived interface from the original .rdata (the base
    // constructor installs its own vtable). This fixture reproduces the
    // observed caller's dispatch through slot +12, not a direct thunk call.
    Put(rt, object + 16, 0x81887888);
    if (Read(rt, 0x81887894) != 0x819E8460) {
      Memory(rt)->SystemHeapFree(object); Close(rt, false); return 0x80004005u;
    }
    bytes[20] = bytes[29] = 0x5A;
    Call(rt, 0x819EDB30, {object + 16, 0, 0, 0});
    const bool cleared = !bytes[20] && !bytes[29];
    const bool guarded = std::all_of(bytes + 48, bytes + 64, [](uint8_t b) { return b == 0xA5; });
    Memory(rt)->SystemHeapFree(object);
    if (!cleared || !guarded) { Close(rt, false); return 0x80004005u; }
    REXLOG_INFO("1888 diagnostic: original virtual draw callback and memory guards passed");
    // Original 818D0110's begin/message/end sequence, stopping before its
    // XuiRenderPresent. Tool-mode has no graphics backend, window or input.
    const auto hud = Read(rt, kHost + 0x68), dc = Read(rt, hud + 12);
    const auto buffer = Memory(rt)->SystemHeapAlloc(64);
    if (!buffer) { Close(rt, false); return 0x8007000Eu; }
    auto status = Call(rt, 0x819389E0, {dc, 0xFF000000});
    if (Ok(status)) {
      Call(rt, 0x818DBB20, {buffer + 16, buffer, dc, 0xFFFFFFFF, 1});
      status = Call(rt, 0x8190CC08, {Read(rt, hud + 8), buffer + 16});
      const auto ended = Call(rt, 0x81936020, {dc});
      if (Ok(status)) status = ended;
    }
    Memory(rt)->SystemHeapFree(buffer);
    REXLOG_INFO("1888 diagnostic: original CPU draw -> {:08X}; no presentation", uint32_t(status));
    if (!Ok(status)) { Close(rt, false); return uint32_t(status); }
  }
  Close(rt, false);
  return 0;
}
#endif
}
