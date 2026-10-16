#pragma once
#include <string>
struct PPCContext;
namespace rex { class Runtime; }
namespace x360n {
void ImportLaunchData(rex::Runtime& runtime);
bool IsDashboardReturn();
// Guest path of the session dashboard's original XAM image, bound by its system
// UI host (one image per process, shared by the Guide and the avatar service).
// Empty without such a host.
const std::string& SystemXamModulePath();
void ExportLaunchRequest(rex::Runtime& runtime);
void ExportWindowClose();
}
