#include <catch2/catch_test_macros.hpp>
#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xthread.h>
#include <rex/system/xio.h>
#include <rex/kernel/xam/task_context.h>
#include <cstring>

extern "C" REX_FUNC(__imp__XamPngEncode);
extern "C" REX_FUNC(__imp__XamPngEncodeEx);
namespace rex::kernel::xam { uint32_t XamTaskCloseHandle_entry(uint32_t); }
namespace {
uint32_t png_block, png_flags, png_overlap, png_error;
unsigned png_calls;
void PngEngine(PPCContext& ctx, uint8_t* base) {
  ++png_calls;
  auto word = [&](uint32_t offset) { return rex::memory::load_and_swap<uint32_t>(base + ctx.r3.u32 + offset); };
  CHECK(word(0) == png_block + 256); CHECK(word(4) == 2); CHECK(word(8) == 2);
  CHECK(word(12) == 12); CHECK(word(16) == png_block + 4096);
  CHECK(word(20) == png_block + 64); CHECK(word(24) == png_flags);
  CHECK(word(28) == png_overlap);
  const auto task = rex::kernel::xam::CurrentSystemTask();
  CHECK(task != 0);
  if (task) rex::kernel::xam::XamTaskCloseHandle_entry(task);
  // Only ABI/bounds are mocked here; the original-code probe separately
  // verifies the real PNG stream and independent decoded pixels.
  if (!png_error) rex::memory::store_and_swap<uint32_t>(base + word(16), 0x89504E47);
  rex::memory::store_and_swap<uint32_t>(base + word(20), png_error ? 0 : 4);
  rex::memory::store_and_swap<uint32_t>(base + ctx.r4.u32, png_error ? 0 : 4);
  ctx.r3.u64 = png_error;
}
}

TEST_CASE("17559 PNG service preserves original ABI HRESULT completion and buffer bounds",
          "[sdk][original-png-service]") {
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{}; config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == 0);
  auto* memory = rt.memory(); auto* base = memory->virtual_membase();
  png_block = memory->SystemHeapAlloc(32768); REQUIRE(png_block);
  REQUIRE(rt.function_dispatcher()->InitializeFunctionTable(0x816B0000,0x10000,0x816B0000,0x10000,true,true));
  png_calls = 0; png_error = 0;
  auto caller = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
      rt.kernel_state(), 128 * 1024, 0, [&] {
    auto* ctx = rex::system::XThread::GetCurrentThread()->thread_state()->context();
    auto prepare = [&](bool ex, bool async) {
      std::memset(base + png_block, 0xCD, 32768);
      png_flags = ex ? 1 : 0; png_overlap = async ? png_block + 8192 : 0;
      if (png_overlap) std::memset(base + png_overlap, 0, sizeof(rex::system::XAM_OVERLAPPED));
      rex::memory::store_and_swap<uint32_t>(base + png_block + 64, 4096);
      ctx->r3.u64 = png_block + 256; ctx->r4.u64 = 2; ctx->r5.u64 = 2;
      ctx->r6.u64 = 12; ctx->r7.u64 = png_block + 4096; ctx->r8.u64 = png_block + 64;
      ctx->r9.u64 = ex ? png_flags : png_overlap; ctx->r10.u64 = png_overlap;
    };
    prepare(false, false); __imp__XamPngEncode(*ctx, base);
    CHECK(ctx->r3.u32 == 0x80004001); CHECK(png_calls == 0);
    REQUIRE(rt.function_dispatcher()->SetFunction(0x816B0BA8, PngEngine));
    for (bool ex : {false, true}) for (bool async : {false, true}) {
      prepare(ex, async);
      const auto before = png_calls;
      if (ex) __imp__XamPngEncodeEx(*ctx, base); else __imp__XamPngEncode(*ctx, base);
      CHECK(ctx->r3.u32 == (async ? 0x800703E5 : 0)); CHECK(png_calls == before + 1);
      CHECK(rex::memory::load_and_swap<uint32_t>(base + png_block + 64) == 4);
      CHECK(rex::memory::load_and_swap<uint32_t>(base + png_block + 4096) == 0x89504E47);
      CHECK(base[png_block + 4095] == 0xCD); CHECK(base[png_block + 4100] == 0xCD);
      if (async) {
        auto* io = memory->TranslateVirtual<rex::system::XAM_OVERLAPPED*>(png_overlap);
        CHECK(io->result == 0); CHECK(io->extended_error == 0); CHECK(io->length == 4);
      }
    }
    png_error = 0x8007007A;
    prepare(true, true); __imp__XamPngEncodeEx(*ctx, base);
    CHECK(ctx->r3.u32 == 0x800703E5);
    auto* io = memory->TranslateVirtual<rex::system::XAM_OVERLAPPED*>(png_overlap);
    CHECK(io->result == 1627); CHECK(io->extended_error == png_error); CHECK(io->length == 0);
    png_error = 0;
    for (unsigned invalid = 0; invalid < 7; ++invalid) {
      prepare(true, false); const auto before = png_calls;
      switch (invalid) {
        case 0: ctx->r3.u64 = 0; break;
        case 1: ctx->r4.u64 = 0; break;
        case 2: ctx->r6.u64 = 7; break;
        case 3: ctx->r7.u64 = 0; break;
        case 4: ctx->r8.u64 = 0; break;
        case 5: ctx->r9.u64 = 2; break;
        case 6: ctx->r10.u64 = 5; break;
      }
      __imp__XamPngEncodeEx(*ctx, base);
      CHECK(ctx->r3.u32 == 0x80070057); CHECK(png_calls == before);
      CHECK(base[png_block + 4096] == 0xCD);
    }
    return 0;
  }));
  REQUIRE(caller->Create() == 0); uint64_t timeout = uint64_t(-50'000'000ll);
  REQUIRE(caller->Wait(0, 0, 0, &timeout) == 0);
  memory->SystemHeapFree(png_block);
}
