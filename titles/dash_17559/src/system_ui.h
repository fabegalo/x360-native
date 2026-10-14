#pragma once
#include <string>
struct PPCContext;
namespace rex { class Runtime; }
namespace dash17559 {
void InstallSystemUi(rex::Runtime& runtime, std::string drive = "game:", std::string native_directory = "");
void DrawSystemUiFrame(PPCContext& ctx);
void ReleaseSystemUiHost();
}
