#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
namespace x360n::native {
using SampleCounts = std::array<uint32_t, 8>;
inline bool VisibilityDrawEnabled(uint32_t condition, uint32_t low, uint32_t high) {
  if (!(condition & 0x100)) return true;
  const unsigned id = condition & 63;
  return ((id < 32 ? low : high) & (uint32_t(1) << (id & 31))) != 0;
}
// D3D marks END reports by writing the big-endian pending token into either
// ZPass or ZFail. The GPU report itself is little endian. This is the same
// begin/end boundary identified by the pinned SDK, without fake sample values.
// Begin and end reports may have different addresses; overlapping queries
// share cumulative counters and each guest result subtracts its own snapshot.
class OcclusionQueries {
 public:
  static bool IsEnd(const SampleCounts& report) {
    constexpr uint32_t pending_le = 0xEDFEFFFF;
    return report[2] == pending_le || report[3] == pending_le ||
           report[4] == pending_le || report[5] == pending_le;
  }
  bool CountingAfter(bool end) const {
    if (end && !active_) throw std::runtime_error("Native ZPD: END without a matching BEGIN");
    if (!end && active_ == UINT32_MAX) throw std::runtime_error("Native ZPD: query nesting overflow");
    return end ? active_ > 1 : true;
  }
  void Commit(bool end) { if (end) --active_; else ++active_; }
  uint32_t active() const { return active_; }
 private:
  uint32_t active_ = 0;
};
// The scan converter accumulates an internal result between BEGIN and END;
// PA_SC_VIZ_QUERY_STATUS is published at END and remains independently writable.
class VisibilityQueries {
 public:
  void Begin(unsigned id) {
    const auto bit = uint64_t(1) << id;
    active_ |= bit;
    visible_ &= ~bit;
  }
  void Accumulate(unsigned id, bool visible) {
    if (visible) visible_ |= uint64_t(1) << id;
  }
  bool End(unsigned id) {
    const auto bit = uint64_t(1) << id;
    if (!(active_ & bit)) throw std::runtime_error("Native visibility query: END without BEGIN");
    active_ &= ~bit;
    return (visible_ & bit) != 0;
  }
 private:
  uint64_t active_ = 0, visible_ = 0;
};
}
