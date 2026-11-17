#pragma once
#include "codegen/tool_runtime.h"
#include "kernel/test_access.h"
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/title_paths.h>
#include <rex/kernel/function_dispatcher.h>
#include <rex/kernel/module.h>
#include <rex/kernel/heap.h>
#include <rex/kernel/thread_registry.h>
#include <rex/kernel/object/kernel_thread.h>
#include <rex/kernel/object/object_table.h>
#include <rex/path_settings.h>
#include <rex/ppc/recompiled_code.h>
#include <rex/memory.h>
#include <rex/kernel/object/module_object.h>
#include <rex/xam/title_lifecycle.h>
#include <rex/xam/system_ui.h>
#include <rex/xam/user_profile.h>
#include <rex/xam/offline_achievement_store.h>
#include <rex/kernel/types/io.h>
#include <rex/ppc/image.h>

namespace x360n {
// Keep diagnostic writes in the supplied app root before startup settings freeze.
class DiagnosticToolApp : public rex::codegen::ToolApp {
 public:
  using ToolApp::ToolApp;
 protected:
  rex::Result<void> OnConfigure(rex::SettingsRegistry& settings) override {
    REX_TRY_VOID(ToolApp::OnConfigure(settings));
    return REX_PATHS().user_root.SetText(rex::SettingLayer::kTitle,root.Get().string());
  }
};
}
