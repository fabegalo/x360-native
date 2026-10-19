#include "system_ui.h"
#include "system_ui_request.h"
#include "system_ui_statics.h"
#include "boot_intro.h"
#include "guide_diagnostic.h"
#include "../../common/system_ui_context.h"
#include "../../common/system_ui_execution.h"
#include <rex/runtime.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/chrono/clock.h>
#include <rex/input/input_system.h>
#include <rex/input/flags.h>
#include <rex/kernel/xam/module.h>
#include <rex/kernel/xboxkrnl/video.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <rex/system/xthread.h>
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

REXCVAR_DEFINE_UINT32(dash1888_guide_probe_seconds, 0, "Diagnostics",
                     "Request original Guide after 1..120 seconds; zero disables the diagnostic");
REXCVAR_DEFINE_UINT32(dash1888_guide_probe_cycles, 1, "Diagnostics",
                     "Diagnostic Guide open/close cycles (1..3); requires guide_probe_seconds");
REXCVAR_DEFINE_BOOL(dash1888_ui_lifecycle_trace, false, "Diagnostics",
                   "Trace original scene ownership during system UI destruction");
REXCVAR_DEFINE_UINT32(dash1888_input_probe, 0, "Diagnostics",
                     "Three scripted Guide cycles: 1 closes by Guide, 2 by B; zero disables");
namespace dash1888 {
namespace {
using rex::system::UserModule;
using rex::system::object_ref;
using Bytes = std::vector<uint8_t>;
constexpr uint32_t kHost = 0x81AAED88, kDevice = 0x81AAED38, kTarget = 0x81AAED3C;
struct Request { uint32_t message, user; Bytes bytes, saved; };
struct Active {
  Request request;
  object_ref<UserModule> module;
  uint32_t callback = 0, scene = 0, saved_size = 0, storage = 0;
};
std::optional<Request> pending;
std::mutex request_mutex;
std::atomic<bool> ui_active = false;
std::optional<Active> active;
std::vector<Request> suspended;
x360n::SystemUiExecution execution;
bool initialized = false, attempted = false, reserved = false, guide_held = false;
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
uint32_t Read(rex::Runtime& rt, uint32_t address) {
  return rex::memory::load_and_swap<uint32_t>(rt.memory()->TranslateVirtual(address));
}
void Put(rex::Runtime& rt, uint32_t address, uint32_t value) {
  rex::memory::store_and_swap<uint32_t>(rt.memory()->TranslateVirtual(address), value);
}
uint64_t Call(rex::Runtime& rt, uint32_t address, std::initializer_list<uint64_t> values = {}) {
  auto* thread = rex::system::XThread::GetCurrentThread()->thread_state();
  std::array<uint64_t, 8> args{};
  std::copy(values.begin(), values.end(), args.begin());
  return rt.function_dispatcher()->ExecuteTrap(thread, address, args.data(), values.size());
}
bool Ok(uint64_t result) { return int32_t(uint32_t(result)) >= 0; }
auto Xam(rex::Runtime& rt) {
  return rt.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
}

void Hide(rex::Runtime& rt) {
  Put(rt, kHost + 0x80, 0);
  // 818DACB8 publishes this same empty system descriptor. Avoid its busy wait
  // here: the next title swap consumes the descriptor through the SDK.
  Call(rt, 0x818EF860, {0, 0, *rt.memory()->TranslateVirtual<uint8_t*>(0x81AAEBAC), 0, 0});
}
bool NotifyDisplay(rex::Runtime& rt) {
  const auto data = rt.memory()->SystemHeapAlloc(152);
  if (!data) return false;
  Call(rt, 0x81A9B494, {data}); // Original VdGetCurrentDisplayInformation thunk.
  auto* bytes = rt.memory()->TranslateVirtual<uint8_t*>(data);
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
  rt.memory()->SystemHeapFree(data);
  return true;
}
bool Initialize(rex::Runtime& rt) {
  if (initialized) return true;
  if (attempted) return false;
  attempted = true;
  const auto xam = rt.kernel_state()->LoadUserModule("game:\\xam.xex", false);
  if (!xam) return false;
  // 818E7EA0 stores this identity before any section:// resource resolution.
  // Its network/title scheduler entry point is deliberately not executed.
  Put(rt, 0x81AAF6B8, xam->hmodule_ptr());
  for (const auto& initializer : kUiStaticInitializers)
    Call(rt, initializer.function, {initializer.object});
  if (!Read(rt, 0x81AB1208) || !Read(rt, 0x81AB1228)) {
    REXLOG_ERROR("1888 system UI: original XUI static constructors failed");
    return false;
  }
  REXLOG_INFO("1888 system UI: original XUI resource and text constructors completed");
  auto event = [&](uint32_t address, uint8_t type) {
    std::memset(rt.memory()->TranslateVirtual(address), 0, 16);
    *rt.memory()->TranslateVirtual<uint8_t*>(address) = type;
    Put(rt, address + 8, address + 8); Put(rt, address + 12, address + 8);
  };
  // Original host initialization 818DE7A8 and worker clock setup 818DE1F0.
  event(kHost + 0x40, 0); event(kHost + 0x50, 1); event(0x81AAEB38, 0);
  const auto frequency = Call(rt, 0x81A9B2E4);
  rex::memory::store_and_swap<uint64_t>(rt.memory()->TranslateVirtual(kHost + 0x70), frequency);
  rex::memory::store_and_swap<uint64_t>(rt.memory()->TranslateVirtual(kHost + 0x78),
                                       rex::chrono::Clock::QueryGuestTickCount());
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
  const auto hud = rt.memory()->SystemHeapAlloc(80);
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

void Close(rex::Runtime& rt, bool keep) {
  if (!active) return;
  auto xam = Xam(rt);
  auto& app = *active;
  if (keep && app.saved_size && app.saved_size <= 1024 * 1024) {
    const auto buffer = rt.memory()->SystemHeapAlloc(app.saved_size);
    if (buffer) {
      if (Ok(Call(rt, app.callback, {0x80000009, buffer, 0}))) {
        const auto* bytes = rt.memory()->TranslateVirtual<uint8_t*>(buffer);
        app.request.saved.assign(bytes, bytes + app.saved_size);
        suspended.push_back(std::move(app.request));
      } else REXLOG_ERROR("1888 system UI: original application could not save its state");
      rt.memory()->SystemHeapFree(buffer);
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
  if (REXCVAR_GET(dash1888_ui_lifecycle_trace)) {
    Put(rt, app.storage + 20, 0);
    const auto resolved = Call(rt, 0x81907780, {app.scene, app.storage + 20});
    REXLOG_INFO("1888 diagnostic: app destroyed message={:X}, scene={:08X}, remaining object={:08X}, host state={}",
                app.request.message, app.scene, Ok(resolved) ? Read(rt, app.storage + 20) : 0,
                Read(rt, Read(rt, hud + 0x38) + 12));
  }
  Call(rt, 0x8192C060, {ApplicationFor(app.request.message)->resource});
  Call(rt, app.module->entry_point(), {app.module->hmodule_ptr(), 0, 0});
  const auto owner = app.module->hmodule_ptr();
  rt.kernel_state()->UnloadUserModule(app.module, false);
  rt.memory()->SystemHeapFree(app.storage);
  xam->EndSystemUi(owner);
  active.reset();
  ui_active.store(false, std::memory_order_release);
  Hide(rt);
  rt.kernel_state()->BroadcastNotification(9, false);
}

uint32_t Open(rex::Runtime& rt, Request request) {
  if (!Initialize(rt)) return 0x80004005u;
  const auto* description = ApplicationFor(request.message);
  auto* state = rt.kernel_state(); auto xam = Xam(rt);
  const auto path = std::string("game:\\") + description->file;
  auto module = state->LoadUserModule(path, false);
  if (!module) return 0x80070490u;
  const auto attached = Call(rt, module->entry_point(), {module->hmodule_ptr(), 1, 0});
  const auto registration = xam->FindSystemApp(0xFF);
  if (!attached || !registration || registration->module != module->hmodule_ptr() ||
      registration->resource_id != description->resource) {
    if (attached) Call(rt, module->entry_point(), {module->hmodule_ptr(), 0, 0});
    state->UnloadUserModule(module, false); return 0x80004005u;
  }
  Call(rt, 0x8192C040, {description->resource, module->hmodule_ptr()});
  const auto size = request.saved.empty() ? request.bytes.size() : request.saved.size();
  const auto buffer = rt.memory()->SystemHeapAlloc(uint32_t(24 + size));
  if (!buffer) {
    Call(rt, 0x8192C060, {description->resource});
    Call(rt, module->entry_point(), {module->hmodule_ptr(), 0, 0});
    state->UnloadUserModule(module, false); return 0x8007000Eu;
  }
  const auto& bytes = request.saved.empty() ? request.bytes : request.saved;
  if (!bytes.empty()) std::memcpy(rt.memory()->TranslateVirtual(buffer + 24), bytes.data(), bytes.size());
  // Original Guide launch 818EA744 has no initial data. A pointer to a zero
  // word selects HUD's message-box application instead (913E306C).
  Put(rt, buffer, request.saved.empty() ? 1 : 2); Put(rt, buffer + 4, bytes.empty() ? 0 : buffer + 24);
  Put(rt, buffer + 8, module->hmodule_ptr()); Put(rt, buffer + 12, request.user);
  Put(rt, buffer + 16, 0); Put(rt, buffer + 20, 0);
  if (!xam->BeginSystemUi(module->hmodule_ptr(), registration->slot, request.user)) {
    rt.memory()->SystemHeapFree(buffer);
    Call(rt, 0x8192C060, {description->resource});
    Call(rt, module->entry_point(), {module->hmodule_ptr(), 0, 0});
    state->UnloadUserModule(module, false); return 0x80004005u;
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
    Call(rt, module->entry_point(), {module->hmodule_ptr(), 0, 0});
    state->UnloadUserModule(module, false); xam->EndSystemUi(module->hmodule_ptr());
    rt.memory()->SystemHeapFree(buffer);
    return Ok(created) ? 0x80004005u : uint32_t(created);
  }
  // Original callbacks may retain their request/context until destruction.
  // Keep guest storage alive across frames, not just through scene creation.
  active = Active{std::move(request), module, registration->callback, scene, saved_size, buffer};
  ui_active.store(true, std::memory_order_release);
  Put(rt, kHost + 0x80, 1);
  Put(rt, 0x81AA0808, 1); // 818DE5B0: publish fresh compositor/scaler state for each app.
  xam->SetSystemUiVisible(module->hmodule_ptr());
  state->BroadcastNotification(9, true);
  return 0;
}

void ProcessApplicationInput(rex::Runtime& rt) {
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
  if (!rex::system::XThread::IsInThread()) return;
  x360n::SystemUiExecution::Scope guard(execution);
  if (!guard) return;
  title_frame = frame;
  auto& rt = *rex::Runtime::instance();
  auto xam = Xam(rt);
  rex::input::X_INPUT_STATE input{};
  auto* backend = static_cast<rex::input::InputSystem*>(rt.input_system());
  const bool pressed = backend && backend->GetState(0, &input) == 0 &&
      (uint16_t(input.gamepad.buttons) & rex::input::X_INPUT_GAMEPAD_GUIDE);
  if (pressed && !guide_held) {
    // One line per physical press: distinguishes missing input from a Guide
    // that opened but did not present.
    REXLOG_INFO("1888 system UI: Guide button (active={:X}, pending={})",
                active ? active->request.message : 0, HasPending());
    if (HasPending()) {
      // A queued application is presented first; this press is not retained.
    } else if (active) {
      // The closing shortcut also belongs to this UI until release. Capture
      // its held controls before returning input ownership to the dashboard.
      rex::kernel::xam::CaptureSystemUiOpeningInput(active->request.user);
      xam->RequestSystemUiClose();
    } else if (!active) {
      Enqueue(Request{0x21015, 0, {}, {}});
    }
  }
  guide_held = pressed;
  // Opt-in service-level validation without desktop input or UI automation.
  // No clock reads in ordinary sessions.
  if (const auto probe = REXCVAR_GET(dash1888_guide_probe_seconds); probe > 0 && probe <= 120) {
    static const auto start = std::chrono::steady_clock::now();
    static uint32_t phase = 0;
    const auto cycles = std::clamp(REXCVAR_GET(dash1888_guide_probe_cycles), 1u, 3u);
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
  x360n::SystemUiContext context(*rt.memory());
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
  if (xam->ConsumeSystemUiResume(active->module->hmodule_ptr())) {
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

void InstallSystemUi(rex::Runtime& rt) {
  const auto input_probe = REXCVAR_GET(dash1888_input_probe);
  if (input_probe == 1 || input_probe == 2) {
    if (auto* backend = static_cast<rex::input::InputSystem*>(rt.input_system()))
      backend->AddDriver(std::make_unique<GuideDiagnostic>(input_probe == 2));
  }
  for (const auto* file : {"signin", "createprofile", "hud", "vk", "gamerprofile"}) {
    const auto image = std::string(file) + ".xex";
    const auto library = "dash_1888_" + std::string(file);
    rt.kernel_state()->RegisterRecompiledModule(image.c_str(), ("game:\\" + image).c_str(), library.c_str());
    // Original private module loads use the 1888-only mounted SystemRoot.
    rt.kernel_state()->RegisterRecompiledModule(image.c_str(), ("\\SystemRoot\\" + image).c_str(), library.c_str());
  }
  rex::cvar::SetFlagByName("guide_button", "true");
  if (rex::cvar::GetFlagByName("keybind_guide").empty()) rex::cvar::SetFlagByName("keybind_guide", "G");
  Xam(rt)->system_ui_request = [&rt](uint32_t message, uint32_t address, uint32_t size) -> rex::X_HRESULT {
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
    if (!address || !rex::system::XThread::IsInThread() || size > 544) return 0x80070057u;
    Request request{message, 0, {}, {}};
    if (!AdaptRequest(message, {rt.memory()->TranslateVirtual<uint8_t*>(address), size},
                      request.bytes, request.user)) return 0x80070057u;
    if (!Enqueue(std::move(request))) return 0x80004005u;
    if (ui_active.load(std::memory_order_acquire)) Xam(rt)->RequestSystemUiClose();
    // The copied request is now owned by the presentation callback. Success
    // means queued, not signed in: only the original app can complete logon.
    return 0;
  };
  rex::kernel::xboxkrnl::SetSystemSwapCallback(&Swap);
}
void ReleaseSystemUi() {
  rex::kernel::xboxkrnl::SetSystemSwapCallback(nullptr);
  auto& rt = *rex::Runtime::instance();
  using Exit = rex::kernel::xam::XamModule::LoaderData::ExitRequest;
  const auto exit = Xam(rt)->loader_data().exit_request;
  if (exit == Exit::Dashboard || exit == Exit::Terminate || exit == Exit::Firmware)
    execution.WaitForCallerExit();
  Xam(rt)->system_ui_request = {};
  { std::lock_guard lock(request_mutex); pending.reset(); reserved = false; }
  active.reset(); suspended.clear();
  ui_active.store(false, std::memory_order_release);
}
#ifdef X360N_1888_UI_DIAGNOSTIC
uint32_t ProbeOriginalApplication(rex::Runtime& rt, uint32_t message, uint32_t* scene, bool record_draw,
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
    const auto draw_buffer = rt.memory()->SystemHeapAlloc(64);
    if (!draw_buffer) { Close(rt, false); return 0x8007000Eu; }
    struct FreeBuffer { rex::Runtime& rt; uint32_t buffer;
      ~FreeBuffer() { rt.memory()->SystemHeapFree(buffer); } } free_draw{rt, draw_buffer};
    // Exercise the production input pump and the original timer/animation
    // routines without a graphics device, window or physical input.
    for (unsigned frame = 0; frame < 120; ++frame) {
      // Original UI timers use guest wall time, independently of XuiAnimRun.
      // Allow their 800 ms lifecycle callbacks to run in this bounded fixture.
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      ProcessApplicationInput(rt);
      Call(rt, 0x8192A248);
      auto* context = rex::system::XThread::GetCurrentThread()->thread_state()->context();
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
    const auto object = rt.memory()->SystemHeapAlloc(64);
    if (!object) { Close(rt, false); return 0x8007000Eu; }
    auto* bytes = rt.memory()->TranslateVirtual<uint8_t*>(object);
    std::memset(bytes, 0, 48); std::memset(bytes + 48, 0xA5, 16);
    Call(rt, 0x819E8478, {object});
    // Select the derived interface from the original .rdata (the base
    // constructor installs its own vtable). This fixture reproduces the
    // observed caller's dispatch through slot +12, not a direct thunk call.
    Put(rt, object + 16, 0x81887888);
    if (Read(rt, 0x81887894) != 0x819E8460) {
      rt.memory()->SystemHeapFree(object); Close(rt, false); return 0x80004005u;
    }
    bytes[20] = bytes[29] = 0x5A;
    Call(rt, 0x819EDB30, {object + 16, 0, 0, 0});
    const bool cleared = !bytes[20] && !bytes[29];
    const bool guarded = std::all_of(bytes + 48, bytes + 64, [](uint8_t b) { return b == 0xA5; });
    rt.memory()->SystemHeapFree(object);
    if (!cleared || !guarded) { Close(rt, false); return 0x80004005u; }
    REXLOG_INFO("1888 diagnostic: original virtual draw callback and memory guards passed");
    // Original 818D0110's begin/message/end sequence, stopping before its
    // XuiRenderPresent. Tool-mode has no graphics backend, window or input.
    const auto hud = Read(rt, kHost + 0x68), dc = Read(rt, hud + 12);
    const auto buffer = rt.memory()->SystemHeapAlloc(64);
    if (!buffer) { Close(rt, false); return 0x8007000Eu; }
    auto status = Call(rt, 0x819389E0, {dc, 0xFF000000});
    if (Ok(status)) {
      Call(rt, 0x818DBB20, {buffer + 16, buffer, dc, 0xFFFFFFFF, 1});
      status = Call(rt, 0x8190CC08, {Read(rt, hud + 8), buffer + 16});
      const auto ended = Call(rt, 0x81936020, {dc});
      if (Ok(status)) status = ended;
    }
    rt.memory()->SystemHeapFree(buffer);
    REXLOG_INFO("1888 diagnostic: original CPU draw -> {:08X}; no presentation", uint32_t(status));
    if (!Ok(status)) { Close(rt, false); return uint32_t(status); }
  }
  Close(rt, false);
  return 0;
}
#endif
}
