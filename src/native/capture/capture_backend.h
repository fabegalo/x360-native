#pragma once
// Opt-in decorator between Native's command processor and its renderer.
//
// Frame profile (native_frame_profile=FILE.csv): per guest frame, wall time,
// time inside the renderer per call kind, calls, and the renderer's own
// submission/fence/GPU counters. "outside_ms" is the command processor thread
// outside the renderer: PM4 decoding and waiting for the title to submit work.
//
// Frame capture (native_capture=FILE): after native_capture_skip presents,
// records native_capture_frames frames of renderer calls (frame_replay.h).
// native_capture_trigger=FILE starts it instead when that file appears.
// Neither mode is installed unless its cvar is set: no cost otherwise.
#include "capture_format.h"
#include "../commands/scanout_backend.h"
#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>
namespace rex::ui { struct RawImage; }
namespace x360n::native {
struct FrameStats {
  uint64_t frame = 0;
  double frame_ms = 0, backend_ms = 0, draw_ms = 0, resolve_ms = 0, sync_ms = 0, sample_ms = 0, present_ms = 0, shader_ms = 0;
  // Overhead of this decorator inside the frame, never charged to the title,
  // the command processor or the renderer: recording (hashing, file writes,
  // EDRAM snapshot, presenter readback) and the profile row of the last frame.
  double capture_ms = 0, profile_ms = 0;
  uint64_t draws = 0, resolves = 0, guest_syncs = 0, packet_reads = 0, sample_reads = 0, shaders = 0;
  BackendCounters counters;  // Delta over the frame.
};
// Shared by the live profile and replay: the CSV columns of one frame.
std::string FrameStatsCsvHeader();
std::string FrameStatsCsvRow(const FrameStats&);
// The presenter's composed output (RGB of each row and the size); false for an
// empty or truncated image, which no check may accept.
bool HashImage(const rex::ui::RawImage&, capture::Hash&);
BackendCounters operator-(const BackendCounters& later, const BackendCounters& earlier);

class CaptureBackend final : public ScanoutBackend {
 public:
  struct Options {
    std::filesystem::path capture, profile;
    // When set, recording starts at the first present after this file exists
    // (manual sessions: create it when the scene is on screen); skip is ignored.
    std::filesystem::path trigger;
    uint32_t skip_frames = 0, frames = 1;
    uint32_t scale_x = 1, scale_y = 1;
    std::string backend;
  };
  // Returns the backend itself when no option is set.
  static std::unique_ptr<ScanoutBackend> Wrap(std::unique_ptr<ScanoutBackend>, rex::memory::Memory&, Options);
  CaptureBackend(std::unique_ptr<ScanoutBackend>, rex::memory::Memory&, Options);
  ~CaptureBackend() override;

  void Present(rex::ui::Presenter&, const ScanoutTexture&, const void* pixels, const uint32_t* lut,
               const ScanoutOverlay*, const void* overlay_pixels, ScanoutPostEffect) override;
  void Drain() override;
  void InitializeShaderStorage(const std::filesystem::path& path, uint32_t title_id) override {
    inner_->InitializeShaderStorage(path, title_id);
  }
  void CompleteGuestWrites(GuestSync, uint32_t opcode) override;
  void SyncGuestRead(uint32_t address, uint32_t length, uint32_t opcode) override;
  rex::graphics::Shader* LoadShader(rex::graphics::xenos::ShaderType, const uint32_t*, uint32_t) override;
  void Draw(const rex::graphics::RegisterFile&, rex::memory::Memory&, rex::graphics::Shader*, rex::graphics::Shader*,
            rex::graphics::xenos::PrimitiveType, uint32_t, const DrawIndices*, DrawPurpose) override;
  void Resolve(const rex::graphics::RegisterFile&, rex::memory::Memory&) override;
  SampleCounts ReadSampleCounts(bool counting_after) override;
  void SetSampleCounts(const SampleCounts&, bool visibility_only) override;
  void CompleteFrame(const ScanoutTexture& texture, const void* pixels) override { inner_->CompleteFrame(texture, pixels); }
  EdramSnapshot ReadEdram() override { return inner_->ReadEdram(); }
  void WriteEdram(const EdramSnapshot& snapshot) override { inner_->WriteEdram(snapshot); }
  BackendCounters Counters() override { return inner_->Counters(); }

 private:
  using Clock = std::chrono::steady_clock;
  struct Scope {
    CaptureBackend& self; double& total; Clock::time_point start = Clock::now();
    ~Scope();
  };
  // Recording work: charged to capture_ms only; nested scopes count once.
  struct CaptureWork {
    explicit CaptureWork(CaptureBackend& owner) : self(owner) { ++self.capture_depth_; }
    ~CaptureWork();
    CaptureBackend& self; Clock::time_point start = Clock::now();
  };
  unsigned capture_depth_ = 0;
  struct ShaderInfo { uint32_t id, type; std::vector<uint32_t> words; bool emitted = false; };
  bool recording() const { return writer_ != nullptr; }
  void StartRecording();
  void FinishRecording();
  // Stops recording and deletes the incomplete file (never a partial capture).
  void AbortRecording(const std::string& reason);
  void EmitRegisters(const rex::graphics::RegisterFile&);
  uint32_t EmitShader(rex::graphics::Shader*);
  // Records guest bytes a call reads, except pages that hold GPU results.
  void EmitRange(uint32_t address, uint32_t length);
  void EmitRun(uint32_t address, uint32_t length);
  void MarkGpuWritten(uint32_t address, uint32_t length);
  void EmitDrawInputs(const rex::graphics::RegisterFile&, rex::graphics::Shader*, rex::graphics::Shader*,
                      const DrawIndices*, DrawPurpose);
  void EndProfileFrame();

  std::unique_ptr<ScanoutBackend> inner_;
  rex::memory::Memory& memory_;
  Options options_;
  uint64_t presents_ = 0;
  // Frame profile.
  std::FILE* profile_ = nullptr;
  FrameStats frame_;
  Clock::time_point frame_start_ = Clock::now();
  BackendCounters frame_counters_;
  // Capture.
  std::unique_ptr<capture::Writer> writer_;
  bool capture_done_ = false;
  uint32_t recorded_frames_ = 0;
  std::unordered_map<const rex::graphics::Shader*, ShaderInfo> shaders_;
  uint32_t next_shader_ = 0;
  std::unique_ptr<uint32_t[]> registers_;
  std::map<std::pair<uint32_t, uint32_t>, capture::Hash> emitted_ranges_;
  std::vector<std::pair<uint32_t, uint32_t>> written_ranges_;
  // Pages written by resolves/MEMEXPORT since recording began; hashed at each
  // frame end (after publication) to recognize later CPU writes.
  struct GpuPage { bool hashed = false; capture::Hash hash{}; };
  std::unordered_map<uint32_t, GpuPage> gpu_pages_;
  std::unordered_map<const rex::graphics::Shader*, bool> memexport_known_;
};
}
