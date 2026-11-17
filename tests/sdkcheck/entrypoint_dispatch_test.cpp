#include <catch2/catch_test_macros.hpp>
#include <rex/kernel/heap.h>
#include <rex/kernel/function_dispatcher.h>
#include "kernel/title_fixture.h"
#include "kernel/title_state.h"
#include "synthetic_xex.h"

using namespace x360n::test;
namespace {
void First(PPCContext&, uint8_t*) {}
void Second(PPCContext&, uint8_t*) {}
}
TEST_CASE("Entrypoint host dispatch preserves an adjacent original image and legacy defaults",
          "[sdk][entrypoint-dispatch][memory]") {
  for (const bool host : {false,true}) {
    CAPTURE(host);
    Workdir work("x360n_entry_dispatch",host ? "host" : "legacy");
    work.write("default.xex",BuildXex({},nullptr,0,Entry::kReturnZero));
    rex::test::TitleBoot boot(false, [&] {
      REQUIRE(rex::kernel::GetTitlePaths().game_root.Set(work.dir));
      REQUIRE(rex::kernel::GetTitlePaths().entry.Set("default.xex"));
    });
    const PPCFuncMapping functions[]={{kXexText,First},{0,nullptr}};
    const rex::ppc::PPCImage image{kXexText,0x1000,kXexBase,kXexImageSize,functions};
    rex::kernel::TitleRuntime runtime(image, host);
    const auto loaded = runtime.Load();
    INFO((loaded ? std::string{} : loaded.error().GetDescription()));
    REQUIRE(loaded);
    auto& state = rex::kernel::StateOf(runtime);
    auto* memory = state.GetAddressSpace();
    auto* dispatcher = state.GetFunctionDispatcher();
    REQUIRE(dispatcher->GetFunction(kXexText)==First);
    const auto next=kXexBase+kXexImageSize;
    auto* heap=memory->LookupHeap(next);
    const auto flags=rex::kernel::kMemoryAllocationReserve | rex::kernel::kMemoryAllocationCommit;
    const auto protection=rex::kernel::kMemoryProtectRead | rex::kernel::kMemoryProtectWrite;
    const bool registered=dispatcher->InitializeFunctionTable(next+0x10000,0x1000,next,kXexImageSize,false,true);
    CHECK(registered==host);
    if (!registered) continue;
    REQUIRE(heap->AllocFixed(next,kXexImageSize,kXexPage,flags,protection));
    auto* marker=memory->TranslateVirtual<uint32_t*>(next);*marker=0x12345678;
    REQUIRE(dispatcher->SetFunction(next+0x10000,Second));
    CHECK(dispatcher->GetFunction(next+0x10000)==Second);
    CHECK(dispatcher->GetFunction(kXexText)==First);
    CHECK(*marker==0x12345678);
    CHECK(heap->Release(next));
  }
}
