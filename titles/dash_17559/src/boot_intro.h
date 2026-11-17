#pragma once
namespace rex::kernel { class TitleRuntime; }
namespace dash17559 {
// Optional profile-specific bootstrap. Direct dashboard launch is the default.
void InstallBootIntro(rex::kernel::TitleRuntime& runtime);
bool BootIntroRunning();
}
