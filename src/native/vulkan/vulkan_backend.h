#pragma once
#include "../commands/scanout_backend.h"
namespace x360n::native {
std::unique_ptr<ScanoutBackend> CreateVulkanScanout(rex::gpu::GraphicsProvider&, const rex::gpu::XenosSettings&, rex::IPhysicalWriteWatch&);
}
