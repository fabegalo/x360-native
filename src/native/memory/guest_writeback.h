#pragma once
#include <cstdint>
#include <vector>
namespace x360n::native {
// Resolve results recorded in the current submission, not yet copied to guest
// memory. They become guest-visible only after their fence completes, and
// every guest-observable point (events, memory packets, waits, buffer end,
// present) and every Native read of an overlapping range completes them first.
class GuestWriteback {
 public:
  struct Write { uint32_t address, length; uint64_t offset; };
  bool empty() const { return writes_.empty(); }
  bool Overlaps(uint32_t address, uint32_t length) const {
    const uint64_t end = uint64_t(address) + length;
    for (const auto& w : writes_)
      if (address < uint64_t(w.address) + w.length && w.address < end) return true;
    return false;
  }
  void Add(uint32_t address, uint32_t length, uint64_t offset) { writes_.push_back({address, length, offset}); }
  // Submission order: a later resolve to the same bytes wins, as on the GPU.
  template<class Copy> void Complete(Copy copy) {
    for (const auto& w : writes_) copy(w);
    writes_.clear();
  }
 private:
  std::vector<Write> writes_;
};
}
