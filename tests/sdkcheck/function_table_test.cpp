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

#include <rex/system/function_dispatcher.h>
#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include "synthetic_xex.h"

TEST_CASE("user exports preserve identity without exhausting caller thunks",
          "[sdk][memory][user-exports]") {
  using rex::X_STATUS;
  bool host = true;
  SECTION("host dispatch returns the original export address") {}
  SECTION("legacy cross-module dispatch reuses a caller-local thunk") { host = false; }
  x360n::test::Workdir work("x360n_user_exports", host ? "host" : "legacy");
  auto bytes = x360n::test::BuildXex({}, nullptr, 0, x360n::test::Entry::kReturnZero);
  auto be32 = [&](size_t at, uint32_t value) {
    for (unsigned n = 0; n < 4; ++n) bytes.at(at + n) = uint8_t(value >> (24 - 8 * n));
  };
  constexpr uint32_t table = 0x82002000;
  be32(0x800 + 0x160, table);
  const size_t file_table = 0x1000 + table - x360n::test::kXexBase;
  be32(file_table + 0x20, 0x8200);
  be32(file_table + 0x24, 2);
  be32(file_table + 0x28, 6);
  be32(file_table + 0x2C, 0x10000);
  be32(file_table + 0x30, 0x2004);  // A data export must remain a data address.
  work.write("default.xex", bytes);
  rex::Runtime runtime(work.dir);
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(runtime.Setup(std::move(config)) == X_STATUS_SUCCESS);
  auto module = rex::system::object_ref<rex::system::UserModule>(
      new rex::system::UserModule(runtime.kernel_state()));
  REQUIRE(module->LoadFromFile("game:\\default.xex") == X_STATUS_SUCCESS);
  auto& dispatcher = *runtime.function_dispatcher();
  REQUIRE(dispatcher.InitializeFunctionTable(kGame.code_base, kGame.code_size,
      kGame.image_base, kGame.image_size, true, true));
  dispatcher.RegisterModule("exporter", kGame.code_base, [](auto* d) {
    REQUIRE(d->SetFunction(0x82010000, &FnA));
  });
  REQUIRE(dispatcher.InitializeFunctionTable(0x88010000, 0x10000,
      0x88000000, 0x30000, false, host));
  dispatcher.RegisterModule("caller", 0x88010000, [](auto*) {});
  const auto address = module->GetProcAddressByOrdinal(6, 0x88010000);
  REQUIRE(address == (host ? 0x82010000 : 0x88020000));
  bool stable = true;
  for (unsigned i = 0; i < 20000; ++i) {
    if (module->GetProcAddressByOrdinal(6, 0x88010000) != address) { stable = false; break; }
  }
  CHECK(stable);
  CHECK(dispatcher.GetFunction(address) == &FnA);
  CHECK(module->GetProcAddressByOrdinal(7, 0x88010000) == 0x82002004);
  CHECK(module->GetProcAddressByOrdinal(6, 0) == 0x82010000);
  CHECK(module->GetProcAddressByOrdinal(6, kGame.code_base) == 0x82010000);
  REQUIRE(dispatcher.InitializeFunctionTable(0x89010000, 0x10000,
      0x89000000, 0x30000, false, host));
  dispatcher.RegisterModule("other-caller", 0x89010000, [](auto*) {});
  const auto other = module->GetProcAddressByOrdinal(6, 0x89010000);
  CHECK(other == (host ? 0x82010000 : 0x89020000));
  CHECK(dispatcher.GetFunction(other) == &FnA);
  REQUIRE(dispatcher.UnregisterModule("exporter").has_value());
  CHECK(dispatcher.GetFunction(address) == nullptr);
  CHECK(dispatcher.GetFunction(other) == nullptr);
  REQUIRE(dispatcher.InitializeFunctionTable(kGame.code_base, kGame.code_size,
      kGame.image_base, kGame.image_size, true, true));
  dispatcher.RegisterModule("exporter", kGame.code_base, [](auto* d) {
    REQUIRE(d->SetFunction(0x82010000, &FnB));
  });
  const auto reloaded = module->GetProcAddressByOrdinal(6, 0x88010000);
  CHECK(reloaded == address);  // Unload/reload recycles the dedicated slot.
  CHECK(dispatcher.GetFunction(reloaded) == &FnB);
  CHECK(module->GetProcAddressByOrdinal(6, 0x88010000) == reloaded);
  const auto other_reloaded = module->GetProcAddressByOrdinal(6, 0x89010000);
  CHECK(other_reloaded == other);
  CHECK(dispatcher.GetFunction(other_reloaded) == &FnB);
  REQUIRE(dispatcher.UnregisterModule("caller").has_value());
  CHECK_FALSE(dispatcher.GetFunction(host ? 0x88020000 : reloaded));
  CHECK(dispatcher.GetFunction(other_reloaded) == &FnB);
}

TEST_CASE("host-only dispatch preserves aliased guest images", "[sdk][memory][host-dispatch]") {
  rex::memory::Memory memory;
  REQUIRE(memory.Initialize());
  auto* heap = memory.LookupHeap(0x92000000);
  REQUIRE(heap->AllocFixed(0x92000000, 0x10000, 0x1000,
      rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit,
      rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite));
  auto* sentinel = memory.TranslateVirtual<uint32_t*>(0x92000F3C);
  *sentinel = 0x13579BDF;
  rex::runtime::FunctionDispatcher dispatcher(&memory, nullptr);
  constexpr bool host_only = true;
  REQUIRE(dispatcher.InitializeFunctionTable(0x81670000, 0x406A88,
      0x815F0000, 0x610000, false, host_only));
  REQUIRE(*sentinel == 0x13579BDF);
  dispatcher.RegisterModule("synthetic", 0x81670000, [](auto* d) {
    REQUIRE(d->SetFunction(0x81670000, &FnA));
    REQUIRE(d->SetFunction(0x81A76A84, &FnB));
  });
  CHECK(dispatcher.GetFunction(0x81670000) == &FnA);
  CHECK(dispatcher.GetFunction(0x81A76A84) == &FnB);
  auto thunk = dispatcher.AllocateThunk(&FnB, 0x81670000);
  REQUIRE(thunk == 0x81A76A88);
  CHECK(dispatcher.GetFunction(thunk) == &FnB);
  CHECK_FALSE(dispatcher.SetFunction(0x92000000, &FnA));
  CHECK(*sentinel == 0x13579BDF);
  CHECK(dispatcher.UnregisterModule("synthetic").has_value());
  CHECK(dispatcher.GetFunction(0x81670000) == nullptr);
  CHECK(dispatcher.GetFunction(thunk) == nullptr);
  CHECK(*sentinel == 0x13579BDF);
  CHECK(dispatcher.InitializeFunctionTable(0x81670000, 0x406A88,
      0x815F0000, 0x610000, false, host_only));
  CHECK(*sentinel == 0x13579BDF);
}
