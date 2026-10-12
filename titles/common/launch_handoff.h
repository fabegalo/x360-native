#pragma once
namespace rex { class Runtime; }
namespace x360n {
void ImportLaunchData(rex::Runtime& runtime);
bool IsDashboardReturn();
void ExportLaunchRequest(rex::Runtime& runtime);
void ExportWindowClose();
}
