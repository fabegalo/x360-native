#pragma once
#include "../textures/texture.h"
#include "draw_types.h"
#include "occlusion_query.h"
#include <memory>
#include <rex/graphics/command_processor.h>
namespace rex::ui { class GraphicsProvider; class Presenter; }
namespace x360n::native {
// Guest-observable point that requires recorded resolve results in guest memory.
enum GuestSync : unsigned { kGuestSyncEvent, kGuestSyncPacket, kGuestSyncWait, kGuestSyncBufferEnd, kGuestSyncPresent, kGuestSyncSources };
// D1OVL plane (Guide, notifications) composed over the title before gamma, as
// the pinned Xenos path does: rectangle and offset in display coordinates.
struct ScanoutOverlay {
  ScanoutTexture surface;
  uint32_t x, y, right, bottom, offset_x, offset_y, display_width, display_height;
};
// Commands and guest formats do not depend on a host API. A Vulkan backend
// implements this boundary with its own queues, pipelines and resources.
class ScanoutBackend {
 public:
  virtual ~ScanoutBackend() = default;
  // lut: 256 packed RGB10 entries for RGBA8; 128 RGB base/delta triples for RGB10A2.
  // overlay/overlay_pixels: enabled D1OVL plane and its guest surface, or null.
  virtual void Present(rex::ui::Presenter&, const ScanoutTexture&, const void* pixels,
                       const uint32_t* lut, const ScanoutOverlay* overlay,
                       const void* overlay_pixels) = 0;
  virtual void Drain() = 0;
  virtual void InitializeShaderStorage(const std::filesystem::path&, uint32_t title_id) = 0;
  // Guest-observable point (memory packet, wait, buffer end): publish every
  // recorded resolve result to guest memory before the guest can see it.
  virtual void CompleteGuestWrites(GuestSync, uint32_t opcode) = 0;
  // The command processor reads guest memory: only overlapping resolve
  // results must be in guest memory first.
  virtual void SyncGuestRead(uint32_t address, uint32_t length, uint32_t opcode) = 0;
  virtual rex::graphics::Shader* LoadShader(rex::graphics::xenos::ShaderType, const uint32_t*, uint32_t) = 0;
  virtual void Draw(const rex::graphics::RegisterFile&, rex::memory::Memory&, rex::graphics::Shader*, rex::graphics::Shader*,
      rex::graphics::xenos::PrimitiveType, uint32_t, const DrawIndices*) = 0;
  virtual void Resolve(const rex::graphics::RegisterFile&, rex::memory::Memory&) = 0;
  // Snapshot cumulative guest sample counters, waiting for counted work only.
  // No shader instrumentation is used when no query remains active.
  virtual SampleCounts ReadSampleCounts(bool counting_after) = 0;
};
using ScanoutFactory = std::unique_ptr<ScanoutBackend> (*)(rex::ui::GraphicsProvider&);
std::unique_ptr<ScanoutBackend> CreateD3D12Scanout(rex::ui::GraphicsProvider& provider);
}
