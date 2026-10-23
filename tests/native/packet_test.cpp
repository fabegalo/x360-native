#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <rex/memory/ring_buffer.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/register_file.h>
#include <stdexcept>
#include <vector>
#include "../../src/native/commands/scanout_backend.h"

namespace x360n::native {
[[noreturn]] void Unsupported(const char* message) { throw std::runtime_error(message); }
unsigned guest_write_drains = 0, guest_range_reads = 0;
uint32_t last_read_address = 0, last_read_length = 0;
void CompleteGuestWritesBeforePacket(GuestSync, uint32_t) { ++guest_write_drains; }
void WaitForIdleBeforePacket() { ++guest_write_drains; }
void SyncGuestReadBeforePacket(uint32_t address, uint32_t length, uint32_t) {
  ++guest_range_reads; last_read_address = address; last_read_length = length;
}
}
namespace rex::graphics {
void ValidatePacketForBackend(uint32_t, const rex::memory::RingBuffer&);
}
namespace {
using namespace rex::graphics;
using namespace rex::graphics::xenos;
void Validate(unsigned opcode, std::initializer_list<uint32_t> words, unsigned declared = 0) {
  std::vector<uint32_t> data;
  for (auto word : words) data.push_back(rex::byte_swap(word));
  data.push_back(0); // distinguish full from empty in the ring cursor
  rex::memory::RingBuffer ring(reinterpret_cast<uint8_t*>(data.data()), data.size() * 4);
  ring.set_write_offset(words.size() * 4);
  ValidatePacketForBackend(0xc0000000 | (((declared ? declared : unsigned(words.size())) - 1) << 16) | (opcode << 8), ring);
  REQUIRE(ring.read_offset() == 0);
}
}
TEST_CASE("Native validates shader payload before the shared decoder", "[native-packet]") {
  CHECK_NOTHROW(Validate(PM4_IM_LOAD_IMMEDIATE, {0, 3, 0, 0, 0}));
  CHECK_THROWS_WITH(Validate(PM4_IM_LOAD_IMMEDIATE, {0, 6, 0, 0, 0}), "inline shader exceeds contiguous packet extent");
  CHECK_THROWS(Validate(PM4_IM_LOAD_IMMEDIATE, {2, 3, 0, 0, 0}));
  CHECK_THROWS(Validate(PM4_IM_LOAD_IMMEDIATE, {0, 0x10003, 0, 0, 0}));
  CHECK_THROWS(Validate(PM4_IM_LOAD_IMMEDIATE, {0, 3, 0, 0, 0}, 8));
  CHECK_THROWS(Validate(PM4_IM_LOAD, {0x1ffffffc, 3}));
  CHECK_NOTHROW(Validate(PM4_IM_LOAD, {0x1001, 3}));
}
TEST_CASE("Native rejects dropped and malformed draws", "[native-packet]") {
  reg::VGT_DRAW_INITIATOR draw{};
  draw.source_select = SourceSelect::kAutoIndex; draw.prim_type = PrimitiveType::kTriangleList; draw.num_indices = 3;
  CHECK_NOTHROW(Validate(PM4_DRAW_INDX, {0, draw.value}));
  CHECK_NOTHROW(Validate(PM4_DRAW_INDX_2, {draw.value}));
  CHECK_NOTHROW(Validate(PM4_DRAW_INDX, {0x100, draw.value}));
  draw.source_select = SourceSelect::kImmediate;
  CHECK_THROWS_WITH(Validate(PM4_DRAW_INDX_2, {draw.value}), "immediate index source not implemented");
  draw.source_select = SourceSelect::kDMA;
  CHECK_THROWS(Validate(PM4_DRAW_INDX_2, {draw.value, 0x1000}));
  CHECK_THROWS(Validate(PM4_DRAW_INDX_2, {draw.value, 0x1000, 2}));
  CHECK_NOTHROW(Validate(PM4_DRAW_INDX_2, {draw.value, 0x1000, 3}));
}
TEST_CASE("Native checks constants and preserves valid bin state", "[native-packet]") {
  CHECK_THROWS(Validate(PM4_SET_CONSTANT, {5u << 16, 0}));
  CHECK_THROWS(Validate(PM4_SET_CONSTANT2, {RegisterFile::kRegisterCount, 0}));
  CHECK_NOTHROW(Validate(PM4_SET_CONSTANT, {1u << 16, 0}));
  CHECK_NOTHROW(Validate(PM4_SET_BIN_MASK, {0xffffffff, 0xffffffff}));
  CHECK_NOTHROW(Validate(PM4_SET_BIN_SELECT_LO, {1}));
  CHECK_THROWS(Validate(PM4_SET_BIN_SELECT_LO, {1, 2}));
  CHECK_THROWS(Validate(PM4_EVENT_WRITE_EXT, {0, 0}));
}
TEST_CASE("Native accepts only representable screen extent writes", "[native-packet]") {
  CHECK_NOTHROW(Validate(PM4_EVENT_WRITE_EXT, {0x0A, 0x1000 | 1}));
  CHECK_THROWS_WITH(Validate(PM4_EVENT_WRITE_EXT, {0x0A, 0x1000 | 2}), "screen extent endianness not implemented");
  CHECK_THROWS_WITH(Validate(PM4_EVENT_WRITE_EXT, {0x0A, 0x1ffffff8 | 1}), "screen extent outside guest physical memory");
  CHECK_NOTHROW(Validate(PM4_EVENT_WRITE_EXT, {0x0A, 0x1ffffff4 | 1}));
  CHECK_THROWS_WITH(Validate(PM4_EVENT_WRITE_EXT, {0x0A}), "invalid PM4 payload size");
  CHECK_THROWS_WITH(Validate(PM4_EVENT_WRITE_EXT, {0x0A, 0x1001, 0}), "invalid PM4 payload size");
}
TEST_CASE("Native publishes incomplete resolves before guest memory packets", "[native-packet]") {
  auto& drains = x360n::native::guest_write_drains;
  Validate(PM4_SET_CONSTANT, {1u << 16, 0}); // Consume a sync left by another case.
  drains = 0;
  Validate(PM4_MEM_WRITE, {0x1000 | 2, 1});
  CHECK(drains == 1);
  // The packet after a guest sync point starts another epoch, then drains itself.
  Validate(PM4_WAIT_REG_MEM, {0x13, 0x1000, 0, 0xffffffff, 0x100});
  CHECK(drains == 3);
  Validate(PM4_INTERRUPT, {1});
  CHECK(drains == 5);
  // Memory reads into the command processor only synchronize their own range
  // and let the guest observe nothing (no following epoch drain).
  auto& reads = x360n::native::guest_range_reads;
  reads = 0;
  Validate(PM4_LOAD_ALU_CONSTANT, {0x2000, 0, 3});
  CHECK(drains == 6);
  CHECK(reads == 1);
  CHECK(x360n::native::last_read_address == 0x2000);
  CHECK(x360n::native::last_read_length == 12);
  Validate(PM4_IM_LOAD, {0x3001, 5});
  CHECK(drains == 6);
  CHECK(reads == 2);
  CHECK(x360n::native::last_read_address == 0x3000);
  CHECK(x360n::native::last_read_length == 20);
  reg::VGT_DRAW_INITIATOR draw{};
  draw.source_select = SourceSelect::kAutoIndex; draw.prim_type = PrimitiveType::kTriangleList; draw.num_indices = 3;
  Validate(PM4_DRAW_INDX_2, {draw.value});
  CHECK(drains == 6);
  // A plain event (cache flush) publishes nothing; fence and extent writes do.
  Validate(PM4_EVENT_WRITE, {0x16});
  CHECK(drains == 6);
  Validate(PM4_EVENT_WRITE_SHD, {0x14, 0x1000 | 2, 7});
  CHECK(drains == 7);
  Validate(PM4_SET_CONSTANT, {1u << 16, 0});
  CHECK(drains == 8);
  Validate(PM4_EVENT_WRITE_EXT, {0x0A, 0x1000 | 1});
  CHECK(drains == 9);
}
#include "../../src/native/memory/guest_writeback.h"
TEST_CASE("Native resolve writeback tracks overlap and submission order") {
  x360n::native::GuestWriteback w;
  CHECK(w.empty());
  w.Add(0x1000, 0x100, 0);
  w.Add(0x2000, 0x10, 256);
  CHECK(w.Overlaps(0x10FF, 1));
  CHECK_FALSE(w.Overlaps(0x1100, 0x100));
  CHECK_FALSE(w.Overlaps(0xF00, 0x100));
  CHECK(w.Overlaps(0xF00, 0x101));
  CHECK(w.Overlaps(0, 0x20000000));
  CHECK_FALSE(w.Overlaps(0x2010, 4));
  std::vector<uint64_t> order;
  w.Complete([&](const x360n::native::GuestWriteback::Write& write) { order.push_back(write.offset); });
  CHECK(order == std::vector<uint64_t>{0, 256});
  CHECK(w.empty());
  CHECK_FALSE(w.Overlaps(0x1000, 0x100));
}
