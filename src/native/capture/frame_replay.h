#pragma once
// Deterministic replay of a Native frame capture against any ScanoutBackend:
// the renderer receives exactly the recorded calls, registers and guest
// memory, without the title, its CPU threads or the PM4 command processor.
//
// Memory records are applied only where the replay's guest memory differs, as
// a CPU write (NotifyPhysicalWrite). Bytes the replayed GPU work produced and
// has not published yet therefore stay the GPU's, exactly as during capture.
// Each frame is checked against the capture: scanout image, the guest ranges
// resolves and memory exports wrote (after publication), sample counters and,
// with a presenter, the composed output (gamma LUT, overlay, post effect).
// The whole file is validated (capture::ValidateFile) before any call.
#include "capture_backend.h"
#include <functional>
#include <string>
#include <vector>
namespace rex::ui { class Presenter; struct RawImage; }
namespace x360n::native {
struct ReplayOptions {
  uint32_t repeat = 1;  // Passes over the whole capture (later passes are warm).
  // Real presentation (offscreen presenter) and comparison of its output.
  // Without one, frames end with CompleteFrame and the output is not checked.
  rex::ui::Presenter* presenter = nullptr;
  // Negative controls only: skip restoring EDRAM, or alter the first 16 bytes
  // of the Nth memory record when it is applied (0 = first).
  bool skip_edram = false;
  // Negative control: behave as if the presenter produced no composed image.
  bool drop_output = false;
  int64_t tamper_memory_record = -1;
  std::function<void(uint32_t pass, const FrameStats&)> on_frame;
};
struct ReplayResult {
  uint32_t passes = 0, frames = 0;
  uint64_t frontbuffer_checks = 0, frontbuffer_mismatches = 0;
  uint64_t written_checks = 0, written_mismatches = 0;
  uint64_t sample_checks = 0, sample_mismatches = 0;
  uint64_t output_checks = 0, output_mismatches = 0;
  uint64_t memory_records = 0, memory_pages_written = 0;
  // Per pass, the hash of every frame's checked results in order: identical
  // passes produced identical output (repeatability, not only equivalence).
  std::vector<capture::Hash> pass_digests;
  std::vector<std::string> mismatches;  // First few, for the report.
  // True only if every check passed; with a presenter, every frame's composed
  // image was compared (visual_checked), never skipped.
  bool visual_checked = false;
  bool equivalent() const {
    return frames && !frontbuffer_mismatches && !written_mismatches && !sample_mismatches && !output_mismatches &&
           (!visual_checked || output_checks == frames);
  }
  bool repeatable() const {
    for (const auto& digest : pass_digests) if (digest != pass_digests.front()) return false;
    return true;
  }
};
ReplayResult ReplayCapture(capture::Reader&, ScanoutBackend&, rex::memory::Memory&, const ReplayOptions& = {});
// Allocates the guest physical pages a capture touches (replay hosts only).
void EnsurePhysicalAllocated(rex::memory::Memory&, uint32_t address, uint32_t length);
}
