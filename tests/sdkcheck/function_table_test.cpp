// Function table for recompiled code (rex::memory::Memory, src/system/xmemory.cpp).
//
// The generated code finds a host function with
//   REX_LOOKUP_FUNC = base + REX_IMAGE_BASE + REX_IMAGE_SIZE + (target - REX_CODE_BASE) * 2
// (resources/templates/codegen/_indirect_call.inja), so the table must sit exactly
// at image_base + image_size. At f5337cdc it was always allocated in the
// 0x80000000 XEX heap, which rejects images in the 0x90000000 XEX heap (the
// dashboard 2.0.17559.0 at 0x92000000: "Failed to allocate function table at
// 93028000"). patches/rexglue-sdk/0001 selects the XEX heap that holds the whole
// table range. Synthetic addresses only; no guest content.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include <rex/system/xmemory.h>

namespace {

void FnA(PPCContext&, uint8_t*) {}
void FnB(PPCContext&, uint8_t*) {}

constexpr uint32_t kThunkReserve = 0x10000;  // FunctionDispatcher::kThunkReserveSize

struct Image {
  uint32_t image_base, image_size, code_base, code_size;
  uint32_t table() const { return image_base + image_size; }
};

PPCFunc* Lookup(rex::memory::Memory& m, const Image& img, uint32_t guest) {
  // Same address computation as REX_LOOKUP_FUNC.
  uint64_t slot = uint64_t(img.table()) + (uint64_t(guest) - img.code_base) * 2;
  return *m.TranslateVirtual<PPCFunc**>(uint32_t(slot));
}

void CreateRegisterLookupDestroy(rex::memory::Memory& m, const Image& img) {
  REQUIRE(m.InitializeFunctionTable(img.code_base, img.code_size, img.image_base, img.image_size));
  CHECK(m.HasAnyFunctionTable());
  uint32_t first = img.code_base, last = img.code_base + img.code_size - 4;
  uint32_t thunk = img.code_base + img.code_size;  // first slot of the thunk reserve
  CHECK(m.SetFunction(first, &FnA));
  CHECK(m.SetFunction(last, &FnB));
  CHECK(m.SetFunction(thunk, &FnA));
  CHECK(Lookup(m, img, first) == &FnA);
  CHECK(Lookup(m, img, last) == &FnB);
  CHECK(Lookup(m, img, thunk) == &FnA);
  CHECK(Lookup(m, img, first + 4) == nullptr);  // the table starts zeroed
  CHECK_FALSE(m.SetFunction(img.code_base - 4, &FnA));                // before the code
  CHECK_FALSE(m.SetFunction(thunk + kThunkReserve, &FnA));            // past the reserve
  CHECK(m.DestroyFunctionTable(img.code_base));
  CHECK_FALSE(m.SetFunction(first, &FnA));  // no table any more
  CHECK_FALSE(m.DestroyFunctionTable(img.code_base));
}

}  // namespace

// A title in the 0x80000000 XEX heap (64 KB pages), like every game so far.
const Image kGame{0x82000000, 0x00030000, 0x82010000, 0x00010000};
// The 0x90000000 XEX heap (4 KB pages): image size a multiple of 4 KB only,
// code size not page-aligned, like dash.xex 17559 (0x92000000 + 0x1028000).
const Image kSystem{0x92000000, 0x01028000, 0x9217E000, 0x007C0C8C};

TEST_CASE("function table in the 0x80000000 XEX heap: create, register, look up, destroy, recreate",
          "[sdk][memory]") {
  rex::memory::Memory m;
  REQUIRE(m.Initialize());
  CreateRegisterLookupDestroy(m, kGame);
  CreateRegisterLookupDestroy(m, kGame);  // the released range can be allocated again
}

TEST_CASE("function table in the 0x90000000 XEX heap: create, register, look up, destroy, recreate",
          "[sdk][memory]") {
  rex::memory::Memory m;
  REQUIRE(m.Initialize());
  CreateRegisterLookupDestroy(m, kSystem);
  CreateRegisterLookupDestroy(m, kSystem);
}

TEST_CASE("function tables in both XEX heaps coexist", "[sdk][memory]") {
  rex::memory::Memory m;
  REQUIRE(m.Initialize());
  REQUIRE(m.InitializeFunctionTable(kGame.code_base, kGame.code_size, kGame.image_base, kGame.image_size));
  REQUIRE(m.InitializeFunctionTable(kSystem.code_base, kSystem.code_size, kSystem.image_base,
                                    kSystem.image_size));
  CHECK(m.SetFunction(kGame.code_base, &FnA));
  CHECK(m.SetFunction(kSystem.code_base, &FnB));
  CHECK(Lookup(m, kGame, kGame.code_base) == &FnA);
  CHECK(Lookup(m, kSystem, kSystem.code_base) == &FnB);
  CHECK(m.DestroyFunctionTable(kSystem.code_base));
  CHECK(Lookup(m, kGame, kGame.code_base) == &FnA);  // the other table is untouched
  CHECK(m.DestroyFunctionTable(kGame.code_base));
  CHECK_FALSE(m.HasAnyFunctionTable());
}

TEST_CASE("invalid function table requests are rejected without side effects", "[sdk][memory]") {
  rex::memory::Memory m;
  REQUIRE(m.Initialize());
  // Table range crosses the end of the 0x90000000 heap (0xA0000000 is physical memory).
  CHECK_FALSE(m.InitializeFunctionTable(0x9FFF0000, 0x1000, 0x9FFF0000, 0x8000));
  // image_base + image_size wraps around 4 GB.
  CHECK_FALSE(m.InitializeFunctionTable(0xFFFF0000, 0x1000, 0xFFFF0000, 0x20000));
  // code_base + code_size + thunk reserve wraps around 4 GB.
  CHECK_FALSE(m.InitializeFunctionTable(0xFFFFF000, 0x2000, 0x82000000, 0x30000));
  // Table start not aligned to the 64 KB pages of the 0x80000000 heap.
  CHECK_FALSE(m.InitializeFunctionTable(0x82000000, 0x1000, 0x82000000, 0x1000));
  // Not an XEX heap (0x40000000 is guest virtual memory).
  CHECK_FALSE(m.InitializeFunctionTable(0x40000000, 0x1000, 0x40000000, 0x10000));
  // Empty code range.
  CHECK_FALSE(m.InitializeFunctionTable(0x82010000, 0, 0x82000000, 0x30000));
  CHECK_FALSE(m.HasAnyFunctionTable());

  // Overlap: a second table whose range intersects the first one is refused, and
  // the first one keeps working.
  REQUIRE(m.InitializeFunctionTable(kGame.code_base, kGame.code_size, kGame.image_base, kGame.image_size));
  CHECK_FALSE(m.InitializeFunctionTable(0x82100000, 0x1000, kGame.image_base, kGame.image_size + 0x10000));
  CHECK_FALSE(m.InitializeFunctionTable(kGame.code_base, kGame.code_size, kGame.image_base, kGame.image_size));
  CHECK(m.SetFunction(kGame.code_base, &FnA));
  CHECK(Lookup(m, kGame, kGame.code_base) == &FnA);
  CHECK(m.DestroyFunctionTable(kGame.code_base));
  CHECK_FALSE(m.DestroyFunctionTable(0x12345678));

  // Nothing above left an allocation behind: both valid tables still fit.
  CreateRegisterLookupDestroy(m, kGame);
  CreateRegisterLookupDestroy(m, kSystem);
}
