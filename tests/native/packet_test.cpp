#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <rex/memory/ring_buffer.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/register_file.h>
#include <stdexcept>
#include <vector>

namespace x360n::native {
[[noreturn]] void Unsupported(const char* message) { throw std::runtime_error(message); }
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
  CHECK_THROWS(Validate(PM4_DRAW_INDX, {0x100, draw.value}));
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
