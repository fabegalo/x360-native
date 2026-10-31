// CPU contract probe of the verified original 17559 PNG encoder. No UI,
// entrypoint, GPU, user storage or achievements; all buffers are disposable.
#include "../supervisor/scratch.h"
#include "../../titles/common/system_ui_context.h"
#include "../../titles/common/guest_fault_context.h"
#include "../../titles/dash_17559/src/original_assets.h"
#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/kernel/xam/task_context.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/xam/user_profile.h>
#include <rex/ui/image_decode.h>
#include <array>
#include <cstring>
#include <iostream>
#include <windows.h>

extern "C" REX_FUNC(__imp__XamPngEncode);
extern "C" REX_FUNC(__imp__XamPngEncodeEx);
extern "C" REX_FUNC(__imp__XamWriteGamerTileEx);

int main(int argc, char** argv) {
  const bool public_api = argc == 4 && std::string(argv[3]) == "--public-api";
  if (argc != 3 && !public_api) return 2;
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  for (const auto& asset : dash17559::kOriginalAssets) {
    if (asset.path != "system/xam_ui.xex") continue;
    const auto error = x360n::VerifyDashboardAssets(argv[1], "17559", {&asset, 1});
    if (!error.empty()) { std::cerr << error << '\n'; return 8; }
  }
  const auto work = UniqueTempPath("png17559");
  if (!std::filesystem::create_directory(work)) return 9;
  struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() {
      std::error_code ec;
      const auto resolved = std::filesystem::weakly_canonical(path, ec);
      if (!ec && resolved.parent_path() == std::filesystem::weakly_canonical(std::filesystem::temp_directory_path()) &&
          resolved.filename().string().starts_with("x360n_png17559_")) std::filesystem::remove_all(resolved, ec);
    }
  } cleanup{work};
  rex::Runtime rt(std::filesystem::absolute(argv[1]), work, {}, work / "cache", work / "metadata");
  rex::RuntimeConfig config{}; config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  if (rt.Setup(std::move(config))) return 3;
  const auto dll = std::filesystem::absolute(argv[2]).string();
  rt.kernel_state()->RegisterRecompiledModule("xam_ui.xex", "game:\\system\\xam_ui.xex", dll.c_str());
  auto module = rt.kernel_state()->LoadUserModule("game:\\system\\xam_ui.xex", false);
  if (!module) return 4;
  x360n::GuestFaultContext fault;
  if (!fault.Start(*rt.memory(), work / "fault.ppc.txt")) return 10;
  std::cout << "diagnostic=" << work.string() << std::endl;
  bool passed = true;
  auto caller = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
      rt.kernel_state(), 512 * 1024, 0, [&] {
    x360n::SystemUiContext context(*rt.memory());
    const auto block = rt.memory()->SystemHeapAlloc(65536);
    if (!block) { passed = false; return 0; }
    auto* base = rt.memory()->virtual_membase();
    auto put = [&](uint32_t offset, uint32_t value) {
      rex::memory::store_and_swap<uint32_t>(base + block + offset, value);
    };
    auto get = [&](uint32_t offset) { return rex::memory::load_and_swap<uint32_t>(base + block + offset); };
    for (unsigned test = 0; test < 3; ++test) {
      const uint32_t flags = test == 1 ? 1 : 0;
      const uint32_t capacity = test == 2 ? 16 : 32768;
      std::memset(base + block, 0xCD, 65536);
      // Original API consumes a pitch, not necessarily tightly packed rows.
      for (uint32_t y = 0; y < 8; ++y)
        for (uint32_t x = 0; x < 8; ++x)
          put(256 + y * 48 + x * 4, 0x11223344);
      put(0, block + 256); put(4, 8); put(8, 8); put(12, 48);
      put(16, block + 4096); put(20, block + 64); put(24, flags); put(28, 0);
      put(64, capacity); put(68, 0);
      uint64_t args[]{block, block + 68};
      uint32_t result;
      if (public_api) {
        auto* ctx = context.thread_state()->context();
        ctx->r3.u64 = block + 256; ctx->r4.u64 = 8; ctx->r5.u64 = 8; ctx->r6.u64 = 48;
        ctx->r7.u64 = block + 4096; ctx->r8.u64 = block + 64; ctx->r9.u64 = flags; ctx->r10.u64 = 0;
        if (flags) __imp__XamPngEncodeEx(*ctx, base); else __imp__XamPngEncode(*ctx, base);
        result = ctx->r3.u32;
        put(68, get(64)); // Public API exposes its byte count through size*.
      } else {
        result = rex::kernel::xam::RunSystemTask([&] {
          return uint32_t(rt.function_dispatcher()->ExecuteTrap(context.thread_state(), 0x816B0BA8, args, 2));
        });
      }
      int width = 0, height = 0;
      const auto size = get(64);
      std::vector<uint8_t> pixels;
      if (size <= 32768) pixels = rex::ui::DecodeImageRGBA(base + block + 4096, size, width, height);
      bool valid = size == get(68) && size <= capacity && base[block + 4095] == 0xCD &&
          base[block + 4096 + capacity] == 0xCD;
      if (test == 2) valid &= int32_t(result) < 0;
      else {
        valid &= result == 0 && width == 8 && height == 8 && pixels.size() == 256;
        const std::array<uint8_t, 4> expected = flags
            ? std::array<uint8_t, 4>{17,34,51,68} : std::array<uint8_t, 4>{34,51,68,255};
        for (size_t i = 0; i < pixels.size(); ++i) valid &= pixels[i] == expected[i % 4];
      }
      passed &= valid;
      std::cout << "flags=" << flags << " capacity=" << capacity << " result=" << std::hex << result << std::dec
                << " bytes=" << size << " size=" << width << 'x' << height;
      if (pixels.size() >= 4) std::cout << " RGBA=" << unsigned(pixels[0]) << ',' << unsigned(pixels[1])
                                     << ',' << unsigned(pixels[2]) << ',' << unsigned(pixels[3]);
      std::cout << " valid=" << valid << std::endl;
    }
    rt.memory()->SystemHeapFree(block);
    if (public_api) {
      // Exercise the actual original encoder -> native profile-write boundary
      // with both sizes supplied by the original capture path. Private runtime
      // profile only; no dashboard entrypoint or achievement award is invoked.
      auto* profiles = rt.kernel_state()->user_profile();
      uint64_t xuid = 0;
      if (profiles->CreateLocal("PngProbe", &xuid) || profiles->SetLocalSession(xuid)) {
        passed = false; return 0;
      }
      const auto image = rt.memory()->SystemHeapAlloc(131072);
      if (!image) { passed = false; return 0; }
      std::array<std::vector<uint8_t>, 2> pngs;
      for (unsigned i = 0; i < 2; ++i) {
        const uint32_t side = i ? 32 : 64, pitch = side * 4 + 16;
        for (uint32_t y = 0; y < side; ++y) for (uint32_t x = 0; x < side; ++x)
          rex::memory::store_and_swap<uint32_t>(base + image + 256 + y * pitch + x * 4, 0x11223344);
        rex::memory::store_and_swap<uint32_t>(base + image + 64, 32768);
        auto* ctx = context.thread_state()->context();
        ctx->r3.u64 = image + 256; ctx->r4.u64 = side; ctx->r5.u64 = side; ctx->r6.u64 = pitch;
        ctx->r7.u64 = image + 32768; ctx->r8.u64 = image + 64; ctx->r9.u64 = 1; ctx->r10.u64 = 0;
        __imp__XamPngEncodeEx(*ctx, base);
        const auto size = rex::memory::load_and_swap<uint32_t>(base + image + 64);
        if (ctx->r3.u32 || !size || size > 32768) { passed = false; break; }
        pngs[i].assign(base + image + 32768, base + image + 32768 + size);
      }
      if (!pngs[0].empty() && !pngs[1].empty()) {
        std::memcpy(base + image + 32768, pngs[0].data(), pngs[0].size());
        std::memcpy(base + image + 65536, pngs[1].data(), pngs[1].size());
        PPCContext writer{}; writer.r1.u64 = image + 98304;
        writer.r3.u64 = 0; writer.r4.u64 = 0x32; writer.r5.u64 = 0xFFFE07D1;
        writer.r6.u64 = 1; writer.r7.u64 = 2;
        writer.r9.u64 = image + 32768; writer.r10.u64 = image + 65536;
        rex::memory::store_and_swap<uint32_t>(base + writer.r1.u32 + 0x54, uint32_t(pngs[0].size()));
        rex::memory::store_and_swap<uint32_t>(base + writer.r1.u32 + 0x5C, uint32_t(pngs[1].size()));
        rex::memory::store_and_swap<uint32_t>(base + writer.r1.u32 + 0x64, 0);
        __imp__XamWriteGamerTileEx(writer, base);
        bool valid = writer.r3.u32 == 0;
        for (bool personal : {false, true}) for (unsigned i = 0; i < 2; ++i) {
          std::vector<uint8_t> stored;
          valid &= profiles->ReadPicture(xuid, personal, i != 0, stored) == 0 && stored == pngs[i];
          int width = 0, height = 0;
          const auto pixels = rex::ui::DecodeImageRGBA(stored.data(), stored.size(), width, height);
          const int side = i ? 32 : 64;
          valid &= width == side && height == side && pixels.size() == size_t(side * side * 4);
          constexpr std::array<uint8_t, 4> expected{17,34,51,68};
          for (size_t byte = 0; byte < pixels.size(); ++byte) valid &= pixels[byte] == expected[byte % 4];
        }
        passed &= valid;
        std::cout << "original_encode_profile_write_64_32 result=" << writer.r3.u32 << " valid=" << valid << std::endl;
      }
      rt.memory()->SystemHeapFree(image);
    }
    return 0;
  }));
  if (caller->Create()) return 5;
  uint64_t timeout = uint64_t(-100'000'000ll);
  if (caller->Wait(0, 0, 0, &timeout)) ExitProcess(6);
  return passed ? 0 : 7;
}
