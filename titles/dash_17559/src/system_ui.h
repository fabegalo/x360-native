#pragma once
#include <string>
struct PPCContext;
namespace rex { class Runtime; }
namespace dash17559 {
void InstallSystemUi(rex::Runtime& runtime, std::string drive = "game:", std::string native_directory = "");
void DrawSystemUiFrame(PPCContext& ctx);
void ReleaseSystemUiHost();
// Guest path of the single original XAM image this host binds in a title
// process (its own dash_17559_xam_ui AOT); empty when nothing is bound.
const std::string& SystemXamPath();
}
