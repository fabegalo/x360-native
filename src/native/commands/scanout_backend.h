#pragma once
#include "../textures/texture.h"
#include "draw_types.h"
#include "occlusion_query.h"
#include <memory>
#include <span>
#include <vector>
#include <rex/graphics/command_processor.h>
namespace rex::ui { class GraphicsProvider; class Presenter; }
namespace x360n::native {
// Guest-observable point that requires recorded resolve results in guest memory.
enum GuestSync : unsigned { kGuestSyncEvent, kGuestSyncPacket, kGuestSyncWait, kGuestSyncBufferEnd, kGuestSyncPresent, kGuestSyncSources };
enum class ScanoutPostEffect { kNone, kFxaa, kFxaaExtreme };
// D1OVL plane (Guide, notifications) composed over the title before gamma, as
// the pinned Xenos path does: rectangle and offset in display coordinates.
struct ScanoutOverlay {
  ScanoutTexture surface;
  uint32_t x, y, right, bottom, offset_x, offset_y, display_width, display_height;
};
// Renderer-side work counters for frame profiling, capture and replay. Fields a
// backend does not measure stay zero with measured = false (never a reading).
struct BackendCounters {
  bool measured = false, gpu_timed = false;
  uint64_t submissions = 0, fence_waits = 0, fence_wait_ns = 0, gpu_busy_ns = 0;
};
// EDRAM bytes then the 32-byte sample counter tail, and whether sample
// counting (an open ZPD query) is active for the following draws.
struct EdramSnapshot {
  std::vector<uint8_t> bytes;
  bool sample_counting = false;
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
                       const void* overlay_pixels, ScanoutPostEffect = ScanoutPostEffect::kNone) = 0;
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
      rex::graphics::xenos::PrimitiveType, uint32_t, const DrawIndices*, DrawPurpose = DrawPurpose::kGuest) = 0;
  virtual void Resolve(const rex::graphics::RegisterFile&, rex::memory::Memory&) = 0;
  // Snapshot cumulative guest sample counters, waiting for counted work only.
  // No shader instrumentation is used when no query remains active.
  virtual SampleCounts ReadSampleCounts(bool counting_after) = 0;
  // Query probes use a private counter interval and restore the preceding ZPD
  // snapshot. Visibility accumulation is boolean OR, so heavy overdraw cannot
  // wrap back to "not visible". This mode is never enabled for ordinary draws.
  virtual void SetSampleCounts(const SampleCounts&, bool visibility_only) = 0;
  // The guest-visible part of Present without a presenter: publish recorded
  // results, submit, end the frame (CPU page epochs). Headless frame replay.
  virtual void CompleteFrame(const ScanoutTexture&, const void* pixels) = 0;
  // EDRAM followed by the sample counter tail, as the GPU holds it after all
  // submitted work. Frame capture records it; replay restores it.
  virtual EdramSnapshot ReadEdram() = 0;
  virtual void WriteEdram(const EdramSnapshot&) = 0;
  // Cumulative counters (native_stats / native_vulkan_perf enable timing).
  virtual BackendCounters Counters() { return {}; }
};
using ScanoutFactory = std::unique_ptr<ScanoutBackend> (*)(rex::ui::GraphicsProvider&);
std::unique_ptr<ScanoutBackend> CreateD3D12Scanout(rex::ui::GraphicsProvider& provider);
}
