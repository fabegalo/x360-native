// dash_6717 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/filesystem/file_system.h>
#include <rex/kernel/title_paths.h>
#include "../../common/session_title_app.h"
#include "../../common/dashboard_paths.h"
#include "original_assets.h"

class Dash6717App : public x360n::SessionTitleApp {
 public:
  Dash6717App() : SessionTitleApp("dash_6717", kPPCImageConfig) {}
  ~Dash6717App() override { Shutdown(); }

 protected:
  rex::Result<void> OnConfigure(rex::SettingsRegistry& settings) override {
    REX_TRY_VOID(SessionTitleApp::OnConfigure(settings));
    return GetTitleRuntime().GetTitlePaths().entry.SetText(rex::SettingLayer::kTitle, "dash.xex");
  }
  rex::Result<void> OnSettingsLoaded(rex::SettingsRegistry& settings) override {
    REX_TRY_VOID(SessionTitleApp::OnSettingsLoaded(settings));
    return x360n::PrepareDashboardPaths(GetTitleRuntime().GetTitlePaths(), "6717", dash6717::kOriginalAssets);
  }

  // x360-native: this dash.xex links media: and dashmedia: to \SystemRoot itself
  // ("Registered symbolic link: media: => \SystemRoot", run
  // 20261012T054514Z-dash6717-media-off-debug) and opens media:\shrdres.xzp and
  // media:\XenonJKLatin.xtt. Without a \SystemRoot device every open failed with
  // "device not found" (run 20261012T054026Z-dash6717-cam). As in 17559, \SystemRoot
  // is this profile's package folder (--game_data_root), read-only; nothing from 17559.
  rex::Result<void> OnSessionLaunch(rex::kernel::TitleRuntime& title) override {
    REX_TRY_VOID(title.GetFileSystem().Mount({"\\SystemRoot", rex::filesystem::ProviderType::kHostDirectory,
        title.GetTitlePaths().game_root.Get(), rex::filesystem::MountFlags::kNone, {}}));
    return rex::Ok();
  }
};
