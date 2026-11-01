#include "frame_replay.h"
#include "../memory/physical_write.h"
#include <rex/chrono/frame_layout.h>
#include <rex/graphics/register_file.h>
#include <rex/system/xmemory.h>
#include <rex/ui/presenter.h>
#include <fmt/format.h>
#include <xxhash.h>
#include <chrono>
namespace x360n::native {
using namespace rex::graphics;
using capture::Record;
namespace {
using Clock = std::chrono::steady_clock;
double Ms(Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }
static_assert(unsigned(kGuestSyncSources) == 5, "capture_format.cpp validates GuestSync against 5 sources");
ScanoutTexture Texture(const uint32_t* f) { return {f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8] != 0}; }
}
void EnsurePhysicalAllocated(rex::memory::Memory& memory, uint32_t address, uint32_t length) {
  if (!length) return;
  if (uint64_t(address) + length > capture::kPhysicalSize) throw std::runtime_error("Native replay: memory outside guest physical range");
  auto* heap = memory.LookupHeap(0xA0000000);
  for (uint64_t page = address & ~uint64_t(0xFFFF); page < uint64_t(address) + length; page += 0x10000) {
    uint32_t protect = 0;
    if (heap->QueryProtect(0xA0000000u + uint32_t(page), &protect) && protect) continue;
    // A 4 KiB allocation of another view may already back part of this page.
    bool other = false;
    for (uint32_t base : {0xE0000000u, 0xC0000000u}) {
      auto* alias = memory.LookupHeap(base);
      const uint32_t origin = memory.GetPhysicalAddress(base);
      if (page >= origin && page < uint64_t(origin) + alias->heap_size() &&
          alias->QueryProtect(base + uint32_t(page - origin), &protect) && protect) other = true;
    }
    if (other) continue;
    if (!heap->AllocFixed(0xA0000000u + uint32_t(page), 0x10000, 0x10000,
            rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit,
            rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite))
      throw std::runtime_error(fmt::format("Native replay: cannot allocate guest physical page {:08X}", page));
  }
}
ReplayResult ReplayCapture(capture::Reader& reader, ScanoutBackend& backend, rex::memory::Memory& memory,
                           const ReplayOptions& options) {
  // Every record is decoded and range-checked before the renderer sees one.
  capture::ValidateFile(reader, RegisterFile::kRegisterCount);
  ReplayResult result;
  result.visual_checked = options.presenter != nullptr;
  auto regs = std::make_unique<RegisterFile>();
  std::vector<Shader*> shaders;
  auto mismatch = [&](std::string text) { if (result.mismatches.size() < 32) result.mismatches.push_back(std::move(text)); };
  for (uint32_t pass = 0; pass < std::max(1u, options.repeat); ++pass) {
    reader.Rewind();
    std::memset(regs->values, 0, sizeof(regs->values));
    XXH3_state_t* digest = XXH3_createState();
    XXH3_128bits_reset(digest);
    FrameStats frame;
    BackendCounters counters = backend.Counters();
    auto frame_start = Clock::now();
    uint32_t frame_index = 0;
    int64_t memory_index = 0;
    auto timed = [&](double& total, auto&& call) {
      const auto start = Clock::now();
      call();
      const double ms = Ms(Clock::now() - start);
      total += ms;
      frame.backend_ms += ms;
    };
    auto shader = [&](uint32_t id) -> Shader* {
      if (id == capture::kNoShader) return nullptr;
      if (id >= shaders.size() || !shaders[id]) throw std::runtime_error("Native replay: draw before its shader");
      return shaders[id];
    };
    Record type;
    const uint8_t* p;
    uint64_t size;
    while (reader.Next(type, p, size)) {
      switch (type) {
        case Record::kEdram: {
          const auto record = capture::DecodeEdram(reader, p, size);
          if (!options.skip_edram)
            backend.WriteEdram({std::vector<uint8_t>(record.bytes.begin(), record.bytes.end()), record.sample_counting});
          break;
        }
        case Record::kMemory: {
          const auto record = capture::DecodeMemory(reader, p, size);
          EnsurePhysicalAllocated(memory, record.address, record.length);
          ++result.memory_records;
          const bool tamper = memory_index++ == options.tamper_memory_record;
          for (uint32_t offset = 0; offset < record.length;) {
            const uint32_t end = std::min<uint32_t>(record.length, ((record.address + offset) | 4095u) + 1 - record.address);
            uint8_t* target = memory.TranslatePhysical(record.address + offset);
            std::vector<uint8_t> bytes(record.bytes.begin() + offset, record.bytes.begin() + end);
            if (tamper && offset == 0) for (size_t i = 0; i < std::min<size_t>(16, bytes.size()); ++i) bytes[i] ^= 0x5A;
            if (std::memcmp(target, bytes.data(), bytes.size())) {
              NotifyPhysicalWrite(memory, record.address + offset, uint32_t(bytes.size()));
              std::memcpy(target, bytes.data(), bytes.size());
              ++result.memory_pages_written;
            }
            offset = end;
          }
          break;
        }
        case Record::kRegisters:
          for (const auto& write : capture::DecodeRegisters(p, size, RegisterFile::kRegisterCount))
            regs->values[write.index] = write.value;
          break;
        case Record::kShader: {
          auto record = capture::DecodeShader(p, size);
          if (record.id >= shaders.size()) shaders.resize(record.id + 1, nullptr);
          if (!shaders[record.id]) {
            timed(frame.shader_ms, [&] {
              shaders[record.id] = backend.LoadShader(xenos::ShaderType(record.type), record.words.data(), uint32_t(record.words.size()));
            });
            ++frame.shaders;
          }
          break;
        }
        case Record::kDraw: {
          const auto d = capture::DecodeDraw(p, size);
          DrawIndices indices{xenos::IndexFormat(d.index_format), xenos::Endian(d.index_endian), d.index_count, d.index_base, d.index_length};
          if (d.indexed) EnsurePhysicalAllocated(memory, d.index_base, d.index_length);
          timed(frame.draw_ms, [&] {
            backend.Draw(*regs, memory, shader(d.vs), shader(d.ps), xenos::PrimitiveType(d.primitive), d.count,
                         d.indexed ? &indices : nullptr, DrawPurpose(d.purpose));
          });
          ++frame.draws;
          break;
        }
        case Record::kResolve:
          capture::DecodeEmpty(type, size);
          timed(frame.resolve_ms, [&] { backend.Resolve(*regs, memory); });
          ++frame.resolves;
          break;
        case Record::kCompleteGuestWrites: {
          const auto record = capture::DecodeCompleteGuestWrites(p, size);
          timed(frame.sync_ms, [&] { backend.CompleteGuestWrites(GuestSync(record.source), record.opcode); });
          ++frame.guest_syncs;
          break;
        }
        case Record::kSyncGuestRead: {
          const auto record = capture::DecodeSyncGuestRead(p, size);
          timed(frame.sync_ms, [&] { backend.SyncGuestRead(record.address, record.length, record.opcode); });
          ++frame.packet_reads;
          break;
        }
        case Record::kDrain:
          capture::DecodeEmpty(type, size);
          timed(frame.sync_ms, [&] { backend.Drain(); });
          break;
        case Record::kReadSampleCounts: {
          const auto record = capture::DecodeReadSampleCounts(p, size);
          SampleCounts actual;
          timed(frame.sample_ms, [&] { actual = backend.ReadSampleCounts(record.flag); });
          ++frame.sample_reads;
          ++result.sample_checks;
          XXH3_128bits_update(digest, actual.data(), sizeof(actual));
          if (actual != record.counts) {
            ++result.sample_mismatches;
            mismatch(fmt::format("pass {} frame {}: sample counts differ", pass, frame_index));
          }
          break;
        }
        case Record::kSetSampleCounts: {
          const auto record = capture::DecodeSetSampleCounts(p, size);
          timed(frame.sample_ms, [&] { backend.SetSampleCounts(record.counts, record.flag); });
          break;
        }
        case Record::kPresent: {
          const auto record = capture::DecodePresent(p, size);
          const auto texture = Texture(record.texture);
          EnsurePhysicalAllocated(memory, texture.address, texture.bytes);
          if (options.presenter) {
            ScanoutOverlay overlay{};
            const void* overlay_pixels = nullptr;
            if (record.has_overlay) {
              const auto* o = record.overlay;
              overlay = {Texture(o), o[9], o[10], o[11], o[12], o[13], o[14], o[15], o[16]};
              EnsurePhysicalAllocated(memory, overlay.surface.address, overlay.surface.bytes);
              overlay_pixels = memory.TranslatePhysical(overlay.surface.address);
            }
            rex::chrono::FrameLayout::SetDisplayAspect(record.aspect_x, record.aspect_y);
            timed(frame.present_ms, [&] {
              backend.Present(*options.presenter, texture, memory.TranslatePhysical(texture.address), record.lut.data(),
                              record.has_overlay ? &overlay : nullptr, overlay_pixels, ScanoutPostEffect(record.post_effect));
            });
          } else {
            timed(frame.present_ms, [&] { backend.CompleteFrame(texture, memory.TranslatePhysical(texture.address)); });
          }
          // The frame ends with its present; the checks below are the replay's
          // own work (hashes, presenter readback) and are not frame time.
          const auto frame_end = Clock::now();
          const auto latest = backend.Counters();
          const auto frontbuffer = capture::HashBytes(memory.TranslatePhysical(texture.address), texture.bytes);
          XXH3_128bits_update(digest, frontbuffer.data(), sizeof(frontbuffer));
          if (capture::Recorded(record.frontbuffer)) {
            ++result.frontbuffer_checks;
            if (frontbuffer != record.frontbuffer) {
              ++result.frontbuffer_mismatches;
              mismatch(fmt::format("pass {} frame {}: scanout {:08X}+{} differs", pass, frame_index, texture.address, texture.bytes));
            }
          }
          for (const auto& range : record.written) {
            ++result.written_checks;
            EnsurePhysicalAllocated(memory, range.address, range.length);
            const auto hash = capture::HashBytes(memory.TranslatePhysical(range.address), range.length);
            XXH3_128bits_update(digest, hash.data(), sizeof(hash));
            if (hash != range.hash) {
              ++result.written_mismatches;
              mismatch(fmt::format("pass {} frame {}: GPU-written {:08X}+{} differs", pass, frame_index, range.address, range.length));
            }
          }
          if (options.presenter) {
            // Every frame of a valid capture has its composed image: a replay
            // presenter that yields none (or an invalid one) is a difference.
            ++result.output_checks;
            rex::ui::RawImage image;
            capture::Hash output{};
            const bool captured = !options.drop_output && options.presenter->CaptureGuestOutput(image);
            const bool valid = captured && HashImage(image, output);
            XXH3_128bits_update(digest, output.data(), sizeof(output));
            if (!valid) {
              ++result.output_mismatches;
              mismatch(captured ? fmt::format("pass {} frame {}: presenter returned an invalid {}x{} image", pass,
                                              frame_index, image.width, image.height)
                                : fmt::format("pass {} frame {}: presenter returned no composed image", pass, frame_index));
            } else if (image.width != record.output_width || image.height != record.output_height || output != record.output) {
              ++result.output_mismatches;
              mismatch(fmt::format("pass {} frame {}: presenter output {}x{} differs from the captured {}x{}", pass,
                                   frame_index, image.width, image.height, record.output_width, record.output_height));
            }
          }
          frame.frame = frame_index++;
          frame.frame_ms = Ms(frame_end - frame_start);
          frame.counters = latest - counters;
          if (options.on_frame) options.on_frame(pass, frame);
          ++result.frames;
          frame = {};
          counters = latest;
          frame_start = Clock::now();
          break;
        }
        case Record::kBlob:
          throw std::logic_error("Native replay: blob record reached the replay");
      }
    }
    const auto value = XXH3_128bits_digest(digest);
    XXH3_freeState(digest);
    result.pass_digests.push_back({value.low64, value.high64});
    ++result.passes;
  }
  return result;
}
}
