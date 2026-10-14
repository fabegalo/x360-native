// Exhaustive metadata equivalence and a bounded lookup microbenchmark.
// This synthetic benchmark is not a substitute for real-game measurements.
#include <rex/graphics/register_file.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>

int main() {
  using rex::graphics::RegisterFile;
  using rex::graphics::RegisterInfo;
  std::vector<RegisterInfo> expected(65536);
#define XE_GPU_REGISTER(index, type, name) \
  expected[index] = {RegisterInfo::Type::type, #name};
#include <rex/graphics/register_table.inc>
#undef XE_GPU_REGISTER
  unsigned known = 0;
  for (uint32_t i = 0; i < expected.size(); ++i) {
    const auto* actual = RegisterFile::GetRegisterInfo(i);
    if (expected[i].name) {
      ++known;
      if (!actual || actual->type != expected[i].type ||
          std::strcmp(actual->name, expected[i].name) != 0 ||
          actual != RegisterFile::GetRegisterInfo(i)) return 1;
    } else if (actual) return 2;
  }
  for (uint32_t i : {0x10000u, 0x7fffffffu, 0x80000000u, 0xffffffffu})
    if (RegisterFile::GetRegisterInfo(i)) return 3;
  // Fixed mixed stream: known constants, sparse registers and unknown indices.
  std::array<uint32_t, 4096> stream{};
  uint32_t state = 0x360;
  for (auto& index : stream) {
    state = state * 1664525 + 1013904223;
    index = (state >> 8) % RegisterFile::kRegisterCount;
  }
  uintptr_t checksum = 0;
  std::printf("{\"schema\":1,\"verified_indices\":65540,\"known_registers\":%u,\"lookup_ns\":[", known);
  for (int sample = 0; sample < 7; ++sample) {
    const auto start = std::chrono::steady_clock::now();
    for (unsigned j = 0; j < 1000000; ++j) {
      const auto* info = RegisterFile::GetRegisterInfo(stream[j % stream.size()]);
      checksum += info ? static_cast<unsigned char>(info->name[0]) : 0;
    }
    const auto elapsed = std::chrono::duration<double, std::nano>(
        std::chrono::steady_clock::now() - start).count() / 1000000;
    std::printf("%s%.6f", sample ? "," : "", elapsed);
  }
  std::printf("],\"checksum\":%llu}\n", static_cast<unsigned long long>(checksum));
}
