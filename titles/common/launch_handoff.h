#pragma once
#include <string>
struct PPCContext;
namespace rex { class Runtime; }
namespace x360n {
void ImportLaunchData(rex::Runtime& runtime, std::string bound_xam_path = {});
bool IsDashboardReturn();
// Guest path of the session dashboard's original XAM image, bound by its system
// UI host (one image per process, shared by the Guide and the avatar service).
// The dashboard supplies its own binding to ImportLaunchData.
const std::string& SystemXamModulePath();
void ExportLaunchRequest(rex::Runtime& runtime);
void ExportWindowClose();
}
