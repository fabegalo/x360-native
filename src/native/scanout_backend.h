#pragma once
#include "texture.h"
#include <memory>
namespace rex::ui { class GraphicsProvider; class Presenter; }
namespace x360n::native {
// Commands and guest formats do not depend on a host API. A Vulkan backend
// implements this boundary with its own queues, pipelines and resources.
class ScanoutBackend {
 public:
  virtual ~ScanoutBackend() = default;
  virtual void Present(rex::ui::Presenter&, const ScanoutTexture&, const void* pixels,
                       const uint32_t* lut) = 0;
  virtual void Drain() = 0;
};
using ScanoutFactory = std::unique_ptr<ScanoutBackend> (*)(rex::ui::GraphicsProvider&);
std::unique_ptr<ScanoutBackend> CreateD3D12Scanout(rex::ui::GraphicsProvider& provider);
}
