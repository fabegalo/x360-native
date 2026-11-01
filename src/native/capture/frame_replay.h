#pragma once
// Deterministic replay of a Native frame capture against any ScanoutBackend:
// the renderer receives exactly the recorded calls, registers and guest
// memory, without the title, its CPU threads or the PM4 command processor.
//
// Memory records are applied only where the replay's guest memory differs, as
// a CPU write (NotifyPhysicalWrite). Bytes the replayed GPU work produced and
// has not published yet therefore stay the GPU's, exactly as during capture.
// Each frame is checked against the capture: scanout image and the guest
// ranges resolves and memory exports wrote, after publication.
#include "capture_backend.h"
#include <functional>
#include <string>
#include <vector>
namespace x360n::native {
struct ReplayOptions {
  uint32_t repeat = 1;  // Passes over the whole capture (later passes are warm).
  // Negative control only: do not restore EDRAM, so EDRAM-dependent frames must differ.
  bool skip_edram = false;
  std::function<void(uint32_t pass, const FrameStats&)> on_frame;
};
struct ReplayResult {
  uint32_t passes = 0, frames = 0;
  uint64_t frontbuffer_checks = 0, frontbuffer_mismatches = 0;
  uint64_t written_checks = 0, written_mismatches = 0;
  uint64_t sample_checks = 0, sample_mismatches = 0;
  uint64_t memory_records = 0, memory_pages_written = 0;
  std::vector<std::string> mismatches;  // First few, for the report.
  bool equivalent() const {
    return !frontbuffer_mismatches && !written_mismatches && !sample_mismatches;
  }
};
ReplayResult ReplayCapture(capture::Reader&, ScanoutBackend&, rex::memory::Memory&, const ReplayOptions& = {});
// Allocates the guest physical pages a capture touches (replay hosts only).
void EnsurePhysicalAllocated(rex::memory::Memory&, uint32_t address, uint32_t length);
}
