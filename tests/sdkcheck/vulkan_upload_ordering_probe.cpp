#include <catch2/catch_test_macros.hpp>
#include <rex/types.h>
#include "gpu/xenos/vulkan/upload_hazard_tracker.h"
#include "gpu/xenos/host/deferred_stream.h"

#include <cstdint>
#include <iostream>
#include <limits>

TEST_CASE("Upload ordering rejects every overlapping read range", "[sdk][gpu][shader-fetch][upload-ordering]") {
  rex::gpu::vulkan::UploadHazardTracker tracker;
  uint32_t failures = 0;
  uint32_t checks = 0;
  auto check = [&](bool value, const char* what) {
    ++checks;
    INFO(what);
    CHECK(value);
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
  REQUIRE(failures == 0);
}

TEST_CASE("Deferred stream truncation and transfer retain header and buffer ownership", "[sdk][gpu][shader-fetch][upload-ordering]") {
  enum class Command : uint32_t { kFirst, kSecond };
  rex::gpu::DeferredStream<Command> stream;
  auto* first = static_cast<uint64_t*>(stream.Write(Command::kFirst, sizeof(uint64_t)));
  *first = 0x123456789ABCDEF0;
  const auto boundary = stream.Size();
  *static_cast<uint64_t*>(stream.Write(Command::kSecond, sizeof(uint64_t))) = 7;
  REQUIRE(stream.Size() > boundary);
  stream.Truncate(boundary);
  REQUIRE(stream.Size() == boundary);
  std::vector<uintmax_t> submitted;
  stream.Swap(submitted);
  REQUIRE(stream.Size() == 0);
  REQUIRE(submitted.size() == boundary);
  using Header = rex::gpu::DeferredStream<Command>::Header;
  const auto& header = *reinterpret_cast<const Header*>(submitted.data());
  REQUIRE(header.command == Command::kFirst);
  REQUIRE(header.arguments_size_elements == 1);
  REQUIRE(submitted[rex::gpu::DeferredStream<Command>::kHeaderSizeElements] == 0x123456789ABCDEF0);
  *static_cast<uint64_t*>(stream.Write(Command::kSecond, sizeof(uint64_t))) = 9;
  REQUIRE(submitted[rex::gpu::DeferredStream<Command>::kHeaderSizeElements] == 0x123456789ABCDEF0);
}
