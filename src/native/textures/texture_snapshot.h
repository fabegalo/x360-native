#pragma once
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>
namespace x360n::native {
// Byte comparison, not a hash-only identity: CPU writes and resolve readbacks
// invalidate reuse even when the same guest address and fetch words recur.
struct TextureSnapshot {
  std::vector<uint8_t> base, mips;
  bool Matches(std::span<const uint8_t> b, std::span<const uint8_t> m) const {
    return base.size() == b.size() && mips.size() == m.size() &&
        (b.empty() || std::memcmp(base.data(), b.data(), b.size()) == 0) &&
        (m.empty() || std::memcmp(mips.data(), m.data(), m.size()) == 0);
  }
  void Assign(std::span<const uint8_t> b, std::span<const uint8_t> m) {
    base.assign(b.begin(), b.end()); mips.assign(m.begin(), m.end());
  }
};
}
