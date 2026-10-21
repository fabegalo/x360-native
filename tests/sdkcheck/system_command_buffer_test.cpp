#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <filesystem>
#include <vector>
#include <rex/runtime.h>
#include <rex/graphics/xenos.h>
#include <rex/graphics/register_file.h>
#include <rex/kernel/init.h>
#include <rex/kernel/xboxkrnl/video.h>
#include <rex/ppc/func.h>

extern "C" REX_FUNC(__imp__VdSwap);
extern "C" REX_FUNC(__imp__VdSetSystemCommandBuffer);
extern "C" REX_FUNC(__imp__VdQuerySystemCommandBuffer);

namespace {
int swap_callbacks = 0;
rex::kernel::xboxkrnl::SystemSwapFrame observed{};
void ObserveSwap(const rex::kernel::xboxkrnl::SystemSwapFrame& frame) {
  ++swap_callbacks;
  observed = frame;
}
}  // namespace

TEST_CASE("VdSwap runs the published system command buffer and presents its frame",
          "[sdk][system-command-buffer]") {
  swap_callbacks = 0;
  observed = {};
  namespace xenos = rex::graphics::xenos;
  using rex::X_STATUS;
  rex::Runtime rt(std::filesystem::temp_directory_path());
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(rt.Setup(std::move(config)) == X_STATUS_SUCCESS);
  auto* memory = rt.memory();
  auto* base = memory->virtual_membase();
  const auto scratch = memory->SystemHeapAlloc(4096);
  const auto stack = memory->SystemHeapAlloc(4096);
  const auto title = memory->SystemHeapAlloc(64 * 32 * 4, 4096, rex::memory::kSystemHeapPhysical);
  const auto system = memory->SystemHeapAlloc(64 * 32 * 4, 4096, rex::memory::kSystemHeapPhysical);
  REQUIRE(scratch);
  REQUIRE(stack);
  REQUIRE(title);
  REQUIRE(system);
  auto* bytes = memory->TranslateVirtual<uint8_t*>(scratch);
  std::memset(bytes, 0, 4096);
  auto put = [&](uint32_t offset, uint32_t value) {
    rex::memory::store_and_swap<uint32_t>(bytes + offset, value);
  };
  auto get = [&](uint32_t offset) { return rex::memory::load_and_swap<uint32_t>(bytes + offset); };
  // Title Direct3D front buffer header fetch: tiled 64x32 k_8_8_8_8.
  xenos::xe_gpu_texture_fetch_t fetch{};
  fetch.type = xenos::FetchConstantType::kTexture;
  fetch.tiled = 1;
  fetch.pitch = 64 >> 5;
  fetch.format = xenos::TextureFormat::k_8_8_8_8;
  fetch.endianness = xenos::Endian::k8in32;
  fetch.base_address = title >> 12;
  fetch.size_2d.width = 63;
  fetch.size_2d.height = 31;
  const uint32_t* words = reinterpret_cast<const uint32_t*>(&fetch);
  for (uint32_t i = 0; i < 6; ++i) put(0x400 + i * 4, words[i]);
  put(0x500, title);  // front buffer address
  put(0x504, uint32_t(xenos::TextureFormat::k_8_8_8_8));
  put(0x508, 0);      // RGB
  put(0x50C, 64);
  put(0x510, 32);
  PPCContext ctx{};
  auto swap = [&] {
    ctx.r1.u64 = stack + 1024;
    rex::memory::store_and_swap<uint32_t>(memory->TranslateVirtual(stack + 1024 + 0x54), scratch + 0x50C);
    rex::memory::store_and_swap<uint32_t>(memory->TranslateVirtual(stack + 1024 + 0x5C), scratch + 0x510);
    ctx.r3.u64 = scratch;           // 64 ring dwords
    ctx.r4.u64 = scratch + 0x400;
    ctx.r5.u64 = 0;
    ctx.r6.u64 = 0;
    ctx.r7.u64 = 0;
    ctx.r8.u64 = scratch + 0x500;
    ctx.r9.u64 = scratch + 0x504;
    ctx.r10.u64 = scratch + 0x508;
    __imp__VdSwap(ctx, base);
  };
  auto query = [&] {
    ctx.r3.u64 = 0;
    __imp__VdQuerySystemCommandBuffer(ctx, base);
    return ctx.r3.u32;
  };
  const uint32_t title_physical = memory->GetPhysicalAddress(title);
  const uint32_t system_physical = memory->GetPhysicalAddress(system);

  rex::kernel::xboxkrnl::SetSystemSwapCallback(&ObserveSwap);
  swap();
  CHECK(swap_callbacks == 1);
  CHECK(observed.frontbuffer_physical == title_physical);
  CHECK(observed.width == 64);
  CHECK(observed.height == 32);
  // Without a published descriptor the title's own frame is presented.
  CHECK(get(0) == xenos::MakePacketType0(rex::graphics::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0, 6));
  CHECK(get(7 * 4) == xenos::MakePacketType3(xenos::PM4_XE_SWAP, 4));
  CHECK(get(9 * 4) == title_physical);
  CHECK(query() == 1);

  // Descriptor layout of XAM 17559 81842B08.
  put(0x600, 0xA0123000);
  put(0x604, 0x2000123);  // 24-bit dword count
  put(0x608, system | 0x86);
  put(0x60C, 6);
  put(0x614, 48);
  put(0x618, 24);
  ctx.r3.u64 = scratch + 0x600;
  __imp__VdSetSystemCommandBuffer(ctx, base);
  swap();
  CHECK(get(7 * 4) == xenos::MakePacketType3(xenos::PM4_XE_SYSTEM_COMMANDS, 10));
  CHECK(get(8 * 4) == 0xA0123000);
  CHECK(get(9 * 4) == 0x123);
  const uint32_t fence = get(10 * 4);
  CHECK(fence != 0);
  CHECK(get(11 * 4) == 1);
  // The command buffer inherits the title frame; the swap uses the system one.
  for (uint32_t i = 0; i < 6; ++i) CHECK((i == 1 || get((12 + i) * 4) == words[i]));
  CHECK(get(13 * 4) == ((words[1] & 0xFFF) | (title_physical & 0xFFFFF000)));
  CHECK(get(1 * 4) == words[0]);
  CHECK(get(18 * 4) == xenos::MakePacketType0(rex::graphics::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0, 6));
  xenos::xe_gpu_texture_fetch_t presented{};
  auto* presented_words = reinterpret_cast<uint32_t*>(&presented);
  for (uint32_t i = 0; i < 6; ++i) presented_words[i] = get((19 + i) * 4);
  CHECK(presented.base_address == system_physical >> 12);
  CHECK(presented.size_2d.width == 47);
  CHECK(presented.size_2d.height == 23);
  CHECK(presented.pitch == 2);
  CHECK(get(25 * 4) == xenos::MakePacketType3(xenos::PM4_XE_SWAP, 4));
  CHECK(get(27 * 4) == system_physical);
  CHECK(get(28 * 4) == 48);
  CHECK(get(29 * 4) == 24);
  CHECK(query() == 1);  // one buffer may still be pending

  swap();
  CHECK(get(11 * 4) == 2);
  CHECK(query() == 0);  // two pending: XAM must not overwrite a rotating buffer
  rex::memory::store_and_swap<uint32_t>(memory->TranslatePhysical(fence), 2);
  CHECK(query() == 1);

  SECTION("Null descriptor ends system scanout") {
    ctx.r3.u64 = 0;
  }
  SECTION("Original empty descriptor ends system scanout") {
    put(0x600, 0);
    put(0x604, 0);
    ctx.r3.u64 = scratch + 0x600;
  }
  __imp__VdSetSystemCommandBuffer(ctx, base);
  swap();
  CHECK(get(7 * 4) == xenos::MakePacketType0(rex::graphics::XE_GPU_REG_D1OVL_ENABLE, 1));
  CHECK(get(8 * 4) == 0);
  CHECK(get(9 * 4) == xenos::MakePacketType3(xenos::PM4_XE_SWAP, 4));
  CHECK(get(11 * 4) == title_physical);
  rex::kernel::xboxkrnl::SetSystemSwapCallback(nullptr);
  swap();
  CHECK(swap_callbacks == 4);
  CHECK(get(7 * 4) == xenos::MakePacketType3(xenos::PM4_XE_SWAP, 4));
  memory->SystemHeapFree(system);
  memory->SystemHeapFree(title);
  memory->SystemHeapFree(stack);
  memory->SystemHeapFree(scratch);
}
