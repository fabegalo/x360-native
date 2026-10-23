#pragma once
#include "../commands/scanout_backend.h"
namespace x360n::native {
std::unique_ptr<ScanoutBackend> CreateVulkanScanout(rex::ui::GraphicsProvider&);
}
