#include <rex/graphics/vulkan/upload_hazard_tracker.h>

#include <cstdint>
#include <iostream>
#include <limits>

int main() {
  rex::graphics::vulkan::UploadHazardTracker tracker;
  uint32_t failures = 0;
  uint32_t checks = 0;
  auto check = [&](bool value, const char* what) {
    ++checks;
    if (!value) { ++failures; std::cerr << what << '\n'; }
  };
  tracker.Reset(131072);
  auto serial = tracker.BeginPass();
  check(!tracker.Intersects(0, 131072), "initial render pass has no readers");
  tracker.Mark(63, 3);
  check(tracker.Intersects(63, 1), "last page of word protected");
  check(tracker.Intersects(64, 1), "first page of following word protected");
  check(tracker.Intersects(65, 1), "end page protected");
  check(!tracker.Intersects(0, 63), "preceding disjoint upload can move");
  check(!tracker.Intersects(66, 131072 - 66), "following disjoint upload can move");
  check(tracker.Intersects(62, 2), "partial read/upload overlap cannot move");
  check(tracker.Intersects(65, 2), "partial overlap at trailing boundary cannot move");
  check(!tracker.Intersects(63, 0), "empty upload is harmless");
  check(tracker.Intersects(131072, 1), "out-of-range upload rejected");
  check(tracker.Intersects(1, std::numeric_limits<uint32_t>::max()), "wrapped range rejected");
  check(tracker.BeginPass() != serial, "new render pass has distinct ordering identity");
  check(!tracker.Intersects(63, 3), "old readers do not block a later render pass");
  tracker.Mark(131071, 1);
  check(tracker.Intersects(131071, 1), "last physical page protected");
  tracker.Mark(0, 131072);
  check(tracker.Intersects(0, 1) && tracker.Intersects(65535, 2),
        "unknown MEMEXPORT destinations protect the full address space");
  tracker.Reset(129);
  tracker.BeginPass();
  tracker.Mark(128, std::numeric_limits<uint32_t>::max());
  check(tracker.Intersects(128, 1) && !tracker.Intersects(0, 128),
        "clamped mark protects the final partial word without wrapping");
  std::cout << "Vulkan upload ordering: " << checks << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
