#include "frame_replay.h"
#include "../memory/physical_write.h"
#include <rex/graphics/register_file.h>
#include <rex/system/xmemory.h>
#include <fmt/format.h>
#include <chrono>
namespace x360n::native {
using namespace rex::graphics;
using capture::Record;
using capture::Word;
namespace {
using Clock = std::chrono::steady_clock;
double Ms(Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }
bool Recorded(const capture::Hash& hash) { return hash[0] || hash[1]; }
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
  ReplayResult result;
  auto regs = std::make_unique<RegisterFile>();
  std::vector<Shader*> shaders;
  auto mismatch = [&](std::string text) { if (result.mismatches.size() < 32) result.mismatches.push_back(std::move(text)); };
  for (uint32_t pass = 0; pass < std::max(1u, options.repeat); ++pass) {
    reader.Rewind();
    std::memset(regs->values, 0, sizeof(regs->values));
    FrameStats frame;
    BackendCounters counters = backend.Counters();
    auto frame_start = Clock::now();
    uint32_t frame_index = 0;
    auto timed = [&](double& total, auto&& call) {
      const auto start = Clock::now();
      call();
      const double ms = Ms(Clock::now() - start);
      total += ms;
      frame.backend_ms += ms;
    };
    Record type;
    const uint8_t* p;
    uint64_t size;
    while (reader.Next(type, p, size)) {
      switch (type) {
        case Record::kEdram: {
          uint64_t blob; std::memcpy(&blob, p, 8);
          const auto bytes = reader.blob(blob);
          EdramSnapshot snapshot{std::vector<uint8_t>(bytes.begin(), bytes.end()), Word(p, size, 2) != 0};
          if (!options.skip_edram) backend.WriteEdram(snapshot);
          break;
        }
        case Record::kMemory: {
          const uint32_t address = Word(p, size, 0), length = Word(p, size, 1);
          uint64_t blob; std::memcpy(&blob, p + 8, 8);
          const auto bytes = reader.blob(blob);
          if (bytes.size() != length) throw std::runtime_error("Native replay: memory record size mismatch");
          EnsurePhysicalAllocated(memory, address, length);
          ++result.memory_records;
          for (uint32_t offset = 0; offset < length;) {
            const uint32_t page_end = std::min<uint32_t>(length, ((address + offset) | 4095u) + 1 - address);
            uint8_t* target = memory.TranslatePhysical(address + offset);
            if (std::memcmp(target, bytes.data() + offset, page_end - offset)) {
              NotifyPhysicalWrite(memory, address + offset, page_end - offset);
              std::memcpy(target, bytes.data() + offset, page_end - offset);
              ++result.memory_pages_written;
            }
            offset = page_end;
          }
          break;
        }
        case Record::kRegisters: {
          const uint32_t count = Word(p, size, 0);
          for (uint32_t i = 0; i < count; ++i) {
            const uint32_t index = Word(p, size, 1 + i * 2);
            if (index >= RegisterFile::kRegisterCount) throw std::runtime_error("Native replay: register outside file");
            regs->values[index] = Word(p, size, 2 + i * 2);
          }
          break;
        }
        case Record::kShader: {
          const uint32_t id = Word(p, size, 0), type_value = Word(p, size, 1), count = Word(p, size, 2);
          if (size != 12 + uint64_t(count) * 4) throw std::runtime_error("Native replay: shader record size");
          if (id >= shaders.size()) shaders.resize(id + 1, nullptr);
          if (!shaders[id]) {
            std::vector<uint32_t> words(count);
            std::memcpy(words.data(), p + 12, size_t(count) * 4);
            timed(frame.shader_ms, [&] { shaders[id] = backend.LoadShader(xenos::ShaderType(type_value), words.data(), count); });
            ++frame.shaders;
          }
          break;
        }
        case Record::kDraw: {
          auto shader = [&](uint32_t id) -> Shader* {
            if (id == capture::kNoShader) return nullptr;
            if (id >= shaders.size() || !shaders[id]) throw std::runtime_error("Native replay: draw before its shader");
            return shaders[id];
          };
          DrawIndices indices{xenos::IndexFormat(Word(p, size, 6)), xenos::Endian(Word(p, size, 7)),
                              Word(p, size, 8), Word(p, size, 9), Word(p, size, 10)};
          timed(frame.draw_ms, [&] {
            backend.Draw(*regs, memory, shader(Word(p, size, 0)), shader(Word(p, size, 1)),
                         xenos::PrimitiveType(Word(p, size, 2)), Word(p, size, 3),
                         Word(p, size, 5) ? &indices : nullptr, DrawPurpose(Word(p, size, 4)));
          });
          ++frame.draws;
          break;
        }
        case Record::kResolve:
          timed(frame.resolve_ms, [&] { backend.Resolve(*regs, memory); });
          ++frame.resolves;
          break;
        case Record::kCompleteGuestWrites:
          timed(frame.sync_ms, [&] { backend.CompleteGuestWrites(GuestSync(Word(p, size, 0)), Word(p, size, 1)); });
          ++frame.guest_syncs;
          break;
        case Record::kSyncGuestRead:
          timed(frame.sync_ms, [&] { backend.SyncGuestRead(Word(p, size, 0), Word(p, size, 1), Word(p, size, 2)); });
          ++frame.packet_reads;
          break;
        case Record::kDrain:
          timed(frame.sync_ms, [&] { backend.Drain(); });
          break;
        case Record::kReadSampleCounts: {
          SampleCounts recorded, actual;
          for (unsigned i = 0; i < 8; ++i) recorded[i] = Word(p, size, 1 + i);
          timed(frame.sample_ms, [&] { actual = backend.ReadSampleCounts(Word(p, size, 0) != 0); });
          ++frame.sample_reads;
          ++result.sample_checks;
          if (actual != recorded) {
            ++result.sample_mismatches;
            mismatch(fmt::format("pass {} frame {}: sample counts differ", pass, frame_index));
          }
          break;
        }
        case Record::kSetSampleCounts: {
          SampleCounts counts;
          for (unsigned i = 0; i < 8; ++i) counts[i] = Word(p, size, i);
          timed(frame.sample_ms, [&] { backend.SetSampleCounts(counts, Word(p, size, 8) != 0); });
          break;
        }
        case Record::kPresent: {
          const auto record = capture::Reader::ParsePresent(p, size);
          const auto* f = record.texture;
          const ScanoutTexture texture{f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8] != 0};
          EnsurePhysicalAllocated(memory, texture.address, texture.bytes);
          timed(frame.present_ms, [&] { backend.CompleteFrame(texture, memory.TranslatePhysical(texture.address)); });
          if (Recorded(record.frontbuffer)) {
            ++result.frontbuffer_checks;
            if (capture::HashBytes(memory.TranslatePhysical(texture.address), texture.bytes) != record.frontbuffer) {
              ++result.frontbuffer_mismatches;
              mismatch(fmt::format("pass {} frame {}: scanout {:08X}+{} differs", pass, frame_index, texture.address, texture.bytes));
            }
          }
          for (const auto& range : record.written) {
            ++result.written_checks;
            EnsurePhysicalAllocated(memory, range.address, range.length);
            if (capture::HashBytes(memory.TranslatePhysical(range.address), range.length) != range.hash) {
              ++result.written_mismatches;
              mismatch(fmt::format("pass {} frame {}: GPU-written {:08X}+{} differs", pass, frame_index, range.address, range.length));
            }
          }
          const auto now = Clock::now();
          const auto latest = backend.Counters();
          frame.frame = frame_index++;
          frame.frame_ms = Ms(now - frame_start);
          frame.counters = latest - counters;
          if (options.on_frame) options.on_frame(pass, frame);
          ++result.frames;
          frame = {};
          counters = latest;
          frame_start = Clock::now();
          break;
        }
        default:
          throw std::runtime_error(fmt::format("Native replay: unknown record {}", uint32_t(type)));
      }
    }
    ++result.passes;
  }
  return result;
}
}
