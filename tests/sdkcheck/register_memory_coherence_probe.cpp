#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <vector>
#include <rex/graphics/command_processor.h>
#include <rex/graphics/graphics_system.h>
#include <rex/graphics/shared_memory.h>
#include <rex/system/xmemory.h>

using namespace rex::graphics;
namespace {
int checks = 0, failures = 0;
void Check(bool ok, const char* what) {
  ++checks;
  if (!ok) { ++failures; std::cerr << "FAIL " << what << '\n'; }
}
class CpuGraphics final : public GraphicsSystem {
 public:
  explicit CpuGraphics(rex::memory::Memory& memory) { memory_ = &memory; }
  std::string name() const override { return "register-memory-cpu-check"; }
 private:
  void CreateProvider(bool) override {}
  std::unique_ptr<CommandProcessor> CreateCommandProcessor() override { return {}; }
};
class CpuProcessor final : public CommandProcessor {
 public:
  explicit CpuProcessor(GraphicsSystem* graphics) : CommandProcessor(graphics, nullptr) {}
  using CommandProcessor::ExecutePacketType0;
  using CommandProcessor::WriteRegister;
  using CommandProcessor::gamma_ramp_256_entry_table;
  uint32_t ranges = 0, gamma_writes = 0;
  void IssueSwap(uint32_t, uint32_t, uint32_t) override {}
 private:
  bool SetupContext() override { return true; }
  void ShutdownContext() override {}
  Shader* LoadShader(xenos::ShaderType, uint32_t, const uint32_t*, uint32_t) override { return nullptr; }
  bool IssueDraw(xenos::PrimitiveType, uint32_t, IndexBufferInfo*, bool) override { return false; }
  bool IssueCopy() override { return false; }
  void OnGammaRamp256EntryTableValueWritten() override { ++gamma_writes; }
  void WriteRegistersFromMem(uint32_t index, uint32_t* data, uint32_t count) override {
    ++ranges;
    CommandProcessor::WriteRegistersFromMem(index, data, count);
  }
};

// These adapters let the same executable compile against the pre-patch SDK,
// where the assertions below fail at runtime rather than failing to compile.
template <typename Registers>
auto WriteFetch(Registers& regs, uint32_t start, const uint32_t* data, uint32_t count) {
  if constexpr (requires { regs.WriteFetchConstantsFromMem(start, data, count); }) {
    return regs.WriteFetchConstantsFromMem(start, data, count);
  } else {
    struct Changed {
      uint32_t first, last;
      explicit operator bool() const { return first != UINT32_MAX; }
    };
    rex::memory::copy_and_swap(regs.values + start, data, count);
    return Changed{count ? start : UINT32_MAX, start + count - 1};
  }
}
template <typename Memory>
uint64_t Generation(const Memory& memory) {
  if constexpr (requires { memory.residency_generation(); }) return memory.residency_generation();
  else return 0;
}
class CpuSharedMemory final : public SharedMemory {
 public:
  explicit CpuSharedMemory(rex::memory::Memory& memory) : SharedMemory(memory) { InitializeCommon(); }
  uint32_t uploads = 0;
 private:
  bool UploadRanges(const std::vector<std::pair<uint32_t, uint32_t>>& ranges) override {
    for (auto [page, count] : ranges) {
      ++uploads;
      MakeRangeValid(page << page_size_log2(), count << page_size_log2(), false);
    }
    return true;
  }
};
void Packet(CpuProcessor& cp, uint32_t reg, std::initializer_list<uint32_t> words,
            bool one = false, bool wrap = false) {
  std::array<uint32_t, 32> data{};
  rex::memory::RingBuffer ring(reinterpret_cast<uint8_t*>(data.data()), sizeof(data));
  const size_t begin = wrap ? sizeof(data) - 4 : 0;
  size_t cursor = begin;
  for (uint32_t word : words) {
    data[cursor / 4] = rex::byte_swap(word);
    cursor = (cursor + 4) % sizeof(data);
  }
  ring.set_read_offset(begin);
  ring.set_write_offset(cursor);
  const uint32_t packet = xenos::MakePacketType0(reg, static_cast<uint32_t>(words.size())) |
                          (one ? 0x8000 : 0);
  Check(cp.ExecutePacketType0(&ring, packet), "Type0 accepted");
  Check(ring.empty(), "Type0 consumed exactly its payload");
}
}  // namespace

int main() {
  rex::memory::Memory memory;
  if (!memory.Initialize()) return 2;
  constexpr uint32_t physical = 0x01000000;
  constexpr uint32_t virtual_address = 0xA1000000;
  if (!memory.LookupHeap(virtual_address)->AllocFixed(virtual_address, 16384, 4096,
        rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit,
        rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite)) return 3;
  CpuGraphics graphics(memory);
  CpuProcessor cp(&graphics);
  auto& regs = *graphics.register_file();
  std::memset(regs.values, 0, sizeof(regs.values));
  constexpr uint32_t fetch = XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0;
  Packet(cp, fetch, {0x12345678, 0xABCDEF01, 0x10203040});
  Check(cp.ranges == 1, "one consecutive packet uses one backend range");
  Check(regs[fetch] == 0x12345678 && regs[fetch + 2] == 0x10203040, "Type0 endian and values");
  cp.ranges = 0;
  Packet(cp, fetch, {3, 5, 7}, false, true);
  Check(cp.ranges == 2, "wrapped packet uses two backend ranges");
  Check(regs[fetch] == 3 && regs[fetch + 1] == 5 && regs[fetch + 2] == 7, "wrapped registers remain consecutive");
  cp.ranges = 0;
  Packet(cp, XE_GPU_REG_DC_LUT_RW_INDEX, {0});
  Packet(cp, XE_GPU_REG_DC_LUT_WRITE_EN_MASK, {7});
  cp.ranges = 0;
  Packet(cp, XE_GPU_REG_DC_LUT_SEQ_COLOR, {64, 128, 192}, true);
  Check(cp.ranges == 0, "write-one packets preserve per-write side effects");
  Check(cp.gamma_writes == 3, "every gamma component was written");
  Check(regs[XE_GPU_REG_DC_LUT_RW_INDEX] == 1, "gamma index advanced once");
  const auto gamma = cp.gamma_ramp_256_entry_table()[0];
  Check(gamma.color_10_red == 1 && gamma.color_10_green == 2 && gamma.color_10_blue == 3,
        "gamma write order unchanged");
  regs[XE_GPU_REG_SCRATCH_ADDR] = physical;
  regs[XE_GPU_REG_SCRATCH_UMSK] = 3;
  Packet(cp, XE_GPU_REG_SCRATCH_REG0, {0xAB123456, 0xCD987654});
  Check(rex::memory::load_and_swap<uint32_t>(memory.TranslatePhysical(physical)) == 0xAB123456,
        "scratch zero writes guest memory");
  Check(rex::memory::load_and_swap<uint32_t>(memory.TranslatePhysical(physical + 4)) == 0xCD987654,
        "scratch one writes guest memory");
  Packet(cp, XE_GPU_REG_COHER_STATUS_HOST, {0x123});
  Check(regs[XE_GPU_REG_COHER_STATUS_HOST] == 0x80000123, "COHER dirty side effect retained");

  std::array<uint32_t, 192> descriptors{};
  for (uint32_t i = 0; i < descriptors.size(); ++i) descriptors[i] = rex::byte_swap(0x10203040u + i);
  for (uint32_t start = 0; start < descriptors.size(); ++start) {
    const uint32_t count = static_cast<uint32_t>(descriptors.size()) - start;
    std::fill(regs.values + fetch, regs.values + fetch + descriptors.size(), 0u);
    auto changed = WriteFetch(regs, fetch + start, descriptors.data() + start, count);
    Check(bool(changed) && changed.first == fetch + start && changed.last == fetch + 191,
          "fetch change bounds include every changed dword");
    changed = WriteFetch(regs, fetch + start, descriptors.data() + start, count);
    Check(!changed, "identical fetch range preserves bindings");
    regs[fetch + start] ^= 1;
    changed = WriteFetch(regs, fetch + start, descriptors.data() + start, count);
    Check(bool(changed) && changed.first == fetch + start && changed.last == fetch + start,
          "partial fetch update invalidates only changed span");
    Check(regs[fetch + start] == 0x10203040u + start, "fetch endian conversion");
  }
  Check(!WriteFetch(regs, fetch + 192, descriptors.data(), 0), "empty fetch range has no effect");

  CpuSharedMemory shared(memory);
  Check(shared.RequestRange(physical, 4096), "initial residency upload");
  uint32_t uploads = shared.uploads;
  uint64_t generation = Generation(shared);
  Check(shared.RequestRange(physical, 4096) && shared.uploads == uploads, "unchanged pages avoid reupload");
  shared.MemoryInvalidationCallback(physical + 16, 4, true);
  Check(Generation(shared) != generation, "CPU write invalidates residency despite identical descriptor");
  Check(shared.RequestRange(physical, 4096) && shared.uploads == ++uploads, "CPU-written page reuploads");
  generation = Generation(shared);
  shared.RangeWrittenByGpu(physical, 4096);
  Check(Generation(shared) != generation, "GPU write changes residency generation");
  Check(shared.RequestRange(physical, 4096) && shared.uploads == uploads, "GPU-written page is never overwritten by CPU copy");
  generation = Generation(shared);
  shared.SetSystemPageBlocksValidWithGpuDataWritten();
  Check(Generation(shared) != generation, "frame validity reset invalidates residency cache");
  Check(shared.RequestRange(physical, 4096) && shared.uploads == uploads, "frame reset retains GPU-written data");
  Check(shared.RequestRange(physical + 4096, 4096), "neighbor CPU page uploads");
  uploads = shared.uploads;
  shared.SetSystemPageBlocksValidWithGpuDataWritten();
  Check(shared.RequestRange(physical + 4096, 4096) && shared.uploads == uploads + 1,
        "frame reset refreshes CPU-only page");
  generation = Generation(shared);
  shared.InvalidateAllPages();
  Check(Generation(shared) != generation, "explicit GPU memory invalidation reaches residency cache");
  uploads = shared.uploads;
  Check(shared.RequestRange(physical, 4096) && shared.uploads == uploads + 1, "explicit invalidation reuploads");
  // Reusing RequestRanges scratch must not retain a prior request after an
  // empty batch or a rejected batch with an already-collected prefix.
  shared.InvalidateAllPages();
  const std::pair<uint32_t, uint32_t> rejected[] = {
      {physical, 4096}, {SharedMemory::kBufferSize - 1, 2}};
  uploads = shared.uploads;
  Check(!shared.RequestRanges(rejected, 2) && shared.uploads == uploads,
        "invalid batch does not partially upload its valid prefix");
  Check(shared.RequestRanges(nullptr, 0) && shared.uploads == uploads,
        "empty batch leaves residency untouched");
  const std::pair<uint32_t, uint32_t> neighbor[] = {
      {physical + 4096, 4096}, {physical + 4096, 0}};
  Check(shared.RequestRanges(neighbor, 2) && shared.uploads == uploads + 1,
        "next batch uploads only its own nonempty range");
  Check(shared.RequestRange(physical, 4096) && shared.uploads == uploads + 2,
        "rejected batch prefix remains invalid for the next request");
  uploads = shared.uploads;
  const std::pair<uint32_t, uint32_t> overlapping[] = {
      {physical + 4096, 4096}, {physical, 4096}, {physical + 16, 4096}};
  Check(shared.RequestRanges(overlapping, 3) && shared.uploads == uploads,
        "unsorted overlapping batch preserves already resident pages");
  std::cout << "register-memory-coherence checks=" << checks << " failures=" << failures << '\n';
  return failures ? 1 : 0;
}
