#include "system_ui.h"
#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/kernel/xam/module.h>
#include <rex/kernel/xboxkrnl/video.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <rex/system/xthread.h>
#include <rex/system/thread_state.h>
#include <rex/chrono/clock.h>
#include <cstring>
#include <functional>
#include <rex/ppc/func.h>

namespace dash17559 {
using rex::X_HRESULT;
using rex::X_RESULT;
namespace {
std::function<bool()> draw_ui;
thread_local bool in_dashboard_frame = false;
}
void ReleaseSystemUiHost() {
  // Called after the dashboard guest thread has exited, before runtime teardown.
  draw_ui = {};
}
void InstallSystemUi(rex::Runtime& runtime) {
  auto xam = runtime.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  xam->system_ui_request = [&runtime, ui_ready = false](uint32_t message, uint32_t request, uint32_t size) mutable -> rex::X_HRESULT {
    if (message != 0x21008) return X_E_NOTFOUND;
    if (!request || size != 544 || !rex::system::XThread::IsInThread()) return X_E_INVALIDARG;
    auto* state = runtime.kernel_state();
    auto xam = state->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
    if (draw_ui || xam->FindSystemApp(0xFF)) return X_E_FAIL;
    // Execute on the requesting guest thread, preserving its register context.
    auto* thread = rex::system::XThread::GetCurrentThread()->thread_state();
    auto* dispatcher = runtime.function_dispatcher();
    auto ui = state->GetModule("game:\\system\\xam_ui.xex");
    if (ui && !ui_ready) return X_E_FAIL;
    if (!ui) {
      ui = state->LoadUserModule("game:\\system\\xam_ui.xex", false);
      if (!ui) return X_E_NOTFOUND;
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
      // Original system-XUI bootstrap 816C8328 shares the active D3D device.
      // Supply the live device and video mode through its own host-state fields.
      const auto* device_export = runtime.export_resolver()->GetExportByOrdinal("xboxkrnl.exe", 0x1BE);
      if (!device_export || !device_export->variable_ptr) return X_E_FAIL;
      auto* memory = runtime.memory();
      const auto device = rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(device_export->variable_ptr));
      if (!device) return X_E_FAIL;
      rex::system::X_VIDEO_MODE mode{};
      rex::kernel::xboxkrnl::VdQueryVideoMode(&mode);
      rex::memory::store_and_swap<uint32_t>(memory->TranslateVirtual(0x81AA1B14), device);
      rex::memory::store_and_swap<uint32_t>(memory->TranslateVirtual(0x81A826F0), mode.display_width);
      rex::memory::store_and_swap<uint32_t>(memory->TranslateVirtual(0x81A826F4), mode.display_height);
      uint64_t bootstrap[] = {0};
      const auto boot_result = static_cast<rex::X_HRESULT>(dispatcher->ExecuteTrap(thread, 0x816C8328, bootstrap, 0));
      REXLOG_INFO("17559 system UI: original XUI host bootstrap -> {:08X}", uint32_t(boot_result));
      if (static_cast<int32_t>(boot_result) < 0) return boot_result;
      // Typeface descriptor from original host 816C8450. Register the original
      // language font in this XUI instance; dashboard registration is separate.
      const auto font = memory->SystemHeapAlloc(540);
      if (!font) return static_cast<X_HRESULT>(0x8007000Eu);
      std::memset(memory->TranslateVirtual(font), 0, 540);
      uint64_t font_path[] = {0, 260, font + 20};
      auto font_result = static_cast<X_HRESULT>(dispatcher->ExecuteTrap(thread, 0x81699810, font_path, 3));
      if (static_cast<int32_t>(font_result) >= 0) {
        rex::memory::store_and_swap<uint32_t>(memory->TranslateVirtual(font),
            rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(0x81A827D0)));
        rex::memory::store_and_swap<uint32_t>(memory->TranslateVirtual(font + 4), font + 20);
        uint64_t register_font[] = {font, 1};
        font_result = static_cast<X_HRESULT>(dispatcher->ExecuteTrap(thread, 0x817DEA18, register_font, 2));
      }
      memory->SystemHeapFree(font);
      REXLOG_INFO("17559 system UI: original default typeface -> {:08X}", uint32_t(font_result));
      if (static_cast<int32_t>(font_result) < 0) return font_result;
      // CRT 81A6F888 constructs the configuration facade used by the HUD
      // clock/play-timer query; 81A71190 constructs the queried timer object.
      // Run their constructor bodies without starting XAM or registering atexit.
      uint64_t config_facade[] = {0x81A9AFC4};
      dispatcher->ExecuteTrap(thread, 0x81672C98, config_facade, 1);
      uint64_t play_timer[] = {0x81B52130};
      dispatcher->ExecuteTrap(thread, 0x819951A0, play_timer, 1);
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
      auto skin = state->LoadUserModule("game:\\system\\huduiskin.xex", false);
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
      // hudbkgnd.xur is built by the original host before app scene properties
      // can reference its background controller at 81A9DE68.
      uint64_t background[] = {hud, skin->hmodule_ptr()};
      const auto background_result = static_cast<X_HRESULT>(dispatcher->ExecuteTrap(thread, 0x816929B8, background, 2));
      REXLOG_INFO("17559 system UI: original HUD background -> {:08X}", uint32_t(background_result));
      if (static_cast<int32_t>(background_result) < 0) return background_result;
      ui_ready = true;
    }
    auto module = state->LoadUserModule("game:\\system\\gamerprofile.xex", false);
    if (!module) return X_E_NOTFOUND;
    uint64_t attach[] = {module->hmodule_ptr(), 1, 0};
    const auto attached = dispatcher->ExecuteTrap(thread, module->entry_point(), attach, 3);
    auto unload = [state, dispatcher, thread, module] {
      uint64_t detach[] = {module->hmodule_ptr(), 0, 0};
      dispatcher->ExecuteTrap(thread, module->entry_point(), detach, 3);
      state->UnloadUserModule(module, false);
    };
    auto registration = xam->FindSystemApp(0xFF);
    if (!attached || !registration || registration->module != module->hmodule_ptr()) {
      REXLOG_ERROR("17559 system UI: original DLL did not register its callback");
      state->UnloadUserModule(module, false);
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
    const auto user = rex::memory::load_and_swap<uint32_t>(runtime.memory()->TranslateVirtual(owned_request));
    auto release_module = [&runtime, dispatcher, thread, registration, owned_request, xam, unload] {
      uint64_t unregister_resource[] = {registration->resource_id};
      dispatcher->ExecuteTrap(thread, 0x817DCBD8, unregister_resource, 1);
      unload();
      runtime.memory()->SystemHeapFree(owned_request);
      xam->EndSystemUi(registration->module);
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
    put(0, 1); put(4, owned_request); put(8, module->hmodule_ptr());
    put(12, user);
    put(16, 0);
    uint64_t initialize[] = {0x80000004, context, context + 16};
    const auto result = static_cast<rex::X_HRESULT>(dispatcher->ExecuteTrap(thread, registration->callback, initialize, 3));
    REXLOG_INFO("17559 system UI: gamerprofile original initialize -> {:08X}", uint32_t(result));
    runtime.memory()->SystemHeapFree(context);
    if (static_cast<int32_t>(result) < 0) {
      uint64_t destroy[] = {0x80000005, 0, 0};
      dispatcher->ExecuteTrap(thread, registration->callback, destroy, 3);
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
        auto destroy_scene = [&runtime, dispatcher, thread, hud, scene, registration, release_module] {
          // App owns the scene. Unlink and clear the host's reference before
          // its destroy callback and original DLL detach release the app.
          uint64_t unlink[] = {scene};
          dispatcher->ExecuteTrap(thread, 0x817BCBE8, unlink, 1);
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
        draw_ui = [&runtime, dispatcher, thread, hud, registration, xam, destroy_scene,
                   previous_frame = rex::chrono::Clock::QueryGuestUptimeMillis(), frame = 0u]() mutable -> bool {
          uint64_t update[] = {0x8000000A, 0, 0};
          dispatcher->ExecuteTrap(thread, registration->callback, update, 3);
          if (xam->GetSystemUiSession().close_requested) {
            destroy_scene();
            runtime.kernel_state()->BroadcastNotification(0x9, false);
            REXLOG_INFO("17559 system UI: closed original app after {} frames; dashboard focus released", frame);
            return false;
          }
          dispatcher->ExecuteTrap(thread, 0x817DA398, nullptr, 0);
          const auto saved_f1 = thread->context()->f1;
          const auto now = rex::chrono::Clock::QueryGuestUptimeMillis();
          // Original 816C8108 converts timebase ticks to milliseconds (x1000).
          thread->context()->f1.f64 = static_cast<uint32_t>(now - previous_frame);
          previous_frame = now;
          dispatcher->ExecuteTrap(thread, 0x817AB6D0, nullptr, 0);
          thread->context()->f1 = saved_f1;
          uint64_t render[] = {hud, 0};
          const auto rendered = dispatcher->ExecuteTrap(thread, 0x81695098, render, 2);
          if (static_cast<int32_t>(rendered) < 0) {
            REXLOG_ERROR("17559 system UI: original render failed {:08X}; releasing app", uint32_t(rendered));
            destroy_scene();
            runtime.kernel_state()->BroadcastNotification(0x9, false);
            return false;
          }
          if (frame == 0 || frame % 600 == 0)
            REXLOG_INFO("17559 system UI: composed original frame {} -> {:08X}", frame, uint32_t(rendered));
          ++frame;
          return true;
        };
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

extern "C" REX_FUNC(__imp__sub_92186D90);
extern "C" REX_FUNC(__imp__sub_9221CD90);
extern "C" REX_FUNC(sub_92186D90) {
  const auto previous = dash17559::in_dashboard_frame;
  dash17559::in_dashboard_frame = true;
  __imp__sub_92186D90(ctx, base);
  dash17559::in_dashboard_frame = previous;
}
extern "C" void sub_9221CD90(PPCContext& ctx, uint8_t* base) {
  if (dash17559::in_dashboard_frame && dash17559::draw_ui) {
    // The host accesses this context through ThreadState too, so this bridge
    // must not use REX_FUNC's restrict qualifier. Preserve the caller's context.
    const PPCContext saved = ctx;
    const bool active = dash17559::draw_ui();
    if (!active) dash17559::draw_ui = {};
    ctx = saved;
    ctx.fpscr.restoreGuestBits(saved.fpscr.csr);
    if (!active) REXLOG_INFO("17559 system UI: restored present context r3={:08X}", ctx.r3.u32);
  }
  __imp__sub_9221CD90(ctx, base);
}
