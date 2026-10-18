#include <catch2/catch_test_macros.hpp>
#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>
#include <rex/system/function_dispatcher.h>
#include "synthetic_xex.h"

using namespace x360n::test;
using rex::X_STATUS;
namespace {
void First(PPCContext&, uint8_t*) {}
void Second(PPCContext&, uint8_t*) {}
// Keep the regression executable on the pinned pre-patch SDK: the requested
// host mode then follows its only available path and exposes the collision.
template<class Runtime>
auto Setup(Runtime& runtime, const rex::PPCImageInfo& image, rex::RuntimeConfig config, bool host) {
  if constexpr (requires { runtime.Setup(image, std::move(config), true); }) {
    if (host) return runtime.Setup(image, std::move(config), true);
  }
  return runtime.Setup(image, std::move(config));
}
}
TEST_CASE("Entrypoint host dispatch preserves an adjacent original image and legacy defaults",
          "[sdk][entrypoint-dispatch][memory]") {
  for (const bool host : {false,true}) {
    CAPTURE(host);
    Workdir work("x360n_entry_dispatch",host ? "host" : "legacy");
    work.write("default.xex",BuildXex({},nullptr,0,Entry::kReturnZero));
    rex::Runtime runtime(work.dir);
    rex::RuntimeConfig config{};config.tool_mode=true;config.kernel_init=rex::kernel::InitializeKernel;
    const PPCFuncMapping functions[]={{kXexText,First},{0,nullptr}};
    const rex::PPCImageInfo image{kXexText,0x1000,kXexBase,kXexImageSize,functions};
    REQUIRE(Setup(runtime,image,std::move(config),host)==X_STATUS_SUCCESS);
    REQUIRE(runtime.LoadXexImage("game:\\default.xex")==X_STATUS_SUCCESS);
    auto* dispatcher=runtime.function_dispatcher();
    REQUIRE(dispatcher->GetFunction(kXexText)==First);
    const auto next=kXexBase+kXexImageSize;
    auto* heap=runtime.memory()->LookupHeap(next);
    const auto flags=rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit;
    const auto protection=rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite;
    const bool registered=dispatcher->InitializeFunctionTable(next+0x10000,0x1000,next,kXexImageSize,false,true);
    CHECK(registered==host);
    if (!registered) continue;
    REQUIRE(heap->AllocFixed(next,kXexImageSize,kXexPage,flags,protection));
    auto* marker=runtime.memory()->TranslateVirtual<uint32_t*>(next);*marker=0x12345678;
    REQUIRE(dispatcher->SetFunction(next+0x10000,Second));
    CHECK(dispatcher->GetFunction(next+0x10000)==Second);
    CHECK(dispatcher->GetFunction(kXexText)==First);
    CHECK(*marker==0x12345678);
    CHECK(heap->Release(next));
  }
}
