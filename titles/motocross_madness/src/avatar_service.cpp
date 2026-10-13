// Title-local bridge to the original 17559 avatar algorithms. Console services
// (files, memory, threads and completion events/APCs) remain owned by the SDK.
// No XAM entry point, app manager, worker thread or global heap is initialized.
#include <rex/cvar.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/user_module.h>
#include <rex/system/xex_module.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>

#include "../../common/avatar/avatar_aot_catalog.h"

REXCVAR_DEFINE_BOOL(motocross_avatar_service, false, "Motocross Madness",
                    "Use the original 17559 avatar catalog and AOT algorithms");
REXCVAR_DECLARE(std::string, x360n_xam17559_root);
REXCVAR_DECLARE(std::string, x360n_avatar_catalog_17559);
REXCVAR_DECLARE(bool, motocross_xam_aot_probe);

namespace {
using namespace x360n::avatar;
std::mutex service_mutex;
auto* Runtime() { return rex::Runtime::instance(); }
auto* Memory() { return Runtime()->memory(); }
uint8_t* Host(uint32_t p) { return Memory()->TranslateVirtual<uint8_t*>(p); }
uint32_t Read(uint32_t p) {
  return rex::memory::load_and_swap<uint32_t>(Host(p));
}
void Put(uint32_t p, uint32_t v) {
  rex::memory::store_and_swap<uint32_t>(Host(p), v);
}
uint32_t Call(uint32_t fn, std::initializer_list<uint64_t> args) {
  std::vector<uint64_t> a(args);
  return uint32_t(Runtime()->function_dispatcher()->Execute(
      rex::runtime::ThreadState::Get(), fn, a.data(), a.size()));
}
struct Allocation {
  uint32_t p;
  explicit Allocation(uint32_t n) : p(Memory()->SystemHeapAlloc(n, 4096)) {}
  ~Allocation() {
    if (p) Memory()->SystemHeapFree(p);
  }
};
struct Profile {
  uint32_t protocol = Read(0x81B4B238), coordinates = Read(0x81A975E0);
  std::array<uint8_t, 20> callbacks;
  explicit Profile(uint32_t table) {
    std::memcpy(callbacks.data(), Host(0x81A975CC), 20);
    Put(0x81B4B238, 4);
    Put(0x81A975E0, 1);
    std::memcpy(Host(0x81A975CC), Host(table), 20);
  }
  ~Profile() {
    Put(0x81B4B238, protocol);
    Put(0x81A975E0, coordinates);
    std::memcpy(Host(0x81A975CC), callbacks.data(), 20);
  }
};
struct Service {
  AvatarToc toc;
  AotAvatarCatalog catalog{*Memory(), Call};
  Allocation callbacks{4096};
};
// Explicit Shutdown destroys guest resources while Runtime is still alive.
// A process exit without Shutdown leaves cleanup to the owning Runtime.
Service* service = nullptr;
bool mounted = false;
uint32_t Complete(uint32_t overlap, uint32_t hr) {
  if (!overlap) return hr;
  // XAM message APIs return an HRESULT for submission (0 when accepted).
  // XOVERLAPPED.InternalLow is Win32; ExtendedError is the operation HRESULT.
  Runtime()->kernel_state()->CompleteOverlappedImmediateEx(
      overlap, hr ? 1627 : 0, hr, 0);
  return 0;
}
template <class F>
void Invoke(PPCContext& ctx, const char* name, F fn) {
  std::lock_guard lock(service_mutex);
  PPCContext saved = ctx;
  const auto hr = fn();
  ctx = saved;
  ctx.fpscr.restoreGuestBits(saved.fpscr.csr);
  ctx.r3.u64 = hr;
  REXLOG_INFO("avatar-service: {} -> {:08X}", name, hr);
}
uint32_t Initialize(uint32_t version, uint32_t coord, uint32_t processor,
                    uint32_t callbacks, uint32_t buffer, uint32_t size) {
  if (!REXCVAR_GET(motocross_avatar_service)) return kAssetsFailure;
  if (service || REXCVAR_GET(motocross_xam_aot_probe) || version != 4 ||
      coord != 1 || processor > 5 || !callbacks || !buffer || (buffer & 15) ||
      size < 0x300000)
    return kAssetsInvalid;
  const auto root = REXCVAR_GET(x360n_xam17559_root);
  const auto path = REXCVAR_GET(x360n_avatar_catalog_17559);
  if (root.empty() || path.empty()) return 0x80070002;
  auto* fs = Runtime()->kernel_state()->file_system();
  if (!mounted) {
    auto image = std::make_unique<rex::filesystem::HostPathDevice>(
        "\\Device\\X360nAvatarSystem", root, true);
    auto assets = std::make_unique<rex::filesystem::HostPathDevice>(
        "\\Device\\X360nAvatarCatalog",
        std::filesystem::path(path).parent_path(), true);
    if (!image->Initialize() || !assets->Initialize() ||
        !fs->RegisterDevice(std::move(image)) ||
        !fs->RegisterDevice(std::move(assets)) ||
        !fs->RegisterSymbolicLink("x360nsys:", "\\Device\\X360nAvatarSystem"))
      return kAssetsFailure;
    mounted = true;
  }
  auto module =
      Runtime()->kernel_state()->LoadUserModule("x360nsys:\\xam.xex", false);
  if (!module || module->xex_module()->base_address() != 0x815F0000 ||
      module->xex_module()->image_size() != 0x610000)
    return kAssetsFailure;
  for (uint32_t i = 0; i < 5; ++i)
    if (!Runtime()->function_dispatcher()->GetFunction(Read(callbacks + 4 * i)))
      return kAssetsInvalid;
  std::ifstream in(path, std::ios::binary);
  std::string error;
  auto toc = AvatarToc::Parse({std::istreambuf_iterator<char>(in), {}}, &error);
  if (!toc) {
    REXLOG_ERROR("avatar-service: catalog: {}", error);
    return kAssetsFailure;
  }
  auto candidate = std::make_unique<Service>();
  candidate->toc = std::move(*toc);
  if (!candidate->callbacks.p) return kAssetsNoMemory;
  std::memcpy(Host(candidate->callbacks.p), Host(callbacks), 20);
  Profile profile(candidate->callbacks.p);
  auto hr =
      candidate->catalog.Open("\\Device\\X360nAvatarCatalog\\" +
                              std::filesystem::path(path).filename().string());
  if (hr) return hr;
  // Readiness is demonstrated by producing a complete original avatar, not by
  // changing the return value of the SDK's failed Initialize.
  std::vector<std::array<uint8_t, kMetadataSize>> metadata;
  hr = candidate->catalog.Generate(1, 1, metadata);
  if (hr) return hr;
  AotAssetsStats stats;
  AssetsRequest request(
      MakeAotMetadataAssetsBackend(*Memory(), Call, candidate->toc, metadata[0],
                                   0xFFF, candidate->callbacks.p, stats));
  while (request.status() == kAssetsPending) request.Step();
  hr = request.status();
  request.Release();
  if (hr || !stats.guards || stats.allocations != stats.frees)
    return hr ? hr : kAssetsFailure;
  service = candidate.release();
  REXLOG_INFO(
      "avatar-service: READY: original catalog, callbacks and full asset "
      "assembly available");
  return 0;
}
uint32_t MipMaps(uint32_t result, uint32_t size, uint32_t output) {
  if (!result || !output || (output & 4095) || Read(result + 4) ||
      Read(result + 8) > 13)
    return kAssetsInvalid;
  std::vector<uint32_t> textures;
  for (uint32_t i = Read(result + 8); i-- > 0;) {
    auto desc = Read(result + 16) + i * 52;
    if (Read(desc + 24) > 234) return kAssetsInvalid;
    for (uint32_t j = Read(desc + 24); j-- > 0;)
      if (!Call(0x81971060, {desc, j}))
        textures.push_back(Read(desc + 48) + j * 44);
  }
  if (textures.size() > 234) return kAssetsInvalid;
  std::stable_sort(textures.begin(), textures.end(),
                   [](uint32_t a, uint32_t b) {
                     return uint64_t(Read(a + 4)) * Read(a + 8) >
                            uint64_t(Read(b + 4)) * Read(b + 8);
                   });
  Allocation scratch(0xA0000);
  if (!scratch.p) return kAssetsNoMemory;
  uint32_t used = 0;
  for (auto texture : textures) {
    if (!Call(0x81978C90,
              {texture, 0, 0xA0000, scratch.p, size - used, output + used}))
      return kAssetsFailure;
    const auto bytes = Read(texture + 16);
    if (bytes > size - used) return kAssetsFailure;
    used += bytes;
  }
  REXLOG_INFO("avatar-service: mipmaps textures={} bytes={}", textures.size(),
              used);
  return 0;
}
uint32_t Animation(uint32_t idptr, uint32_t flags, uint32_t output) {
  if (!idptr || !output || flags) return kAssetsInvalid;
  auto id = AssetId::FromBytes(std::span<const uint8_t, 16>(Host(idptr), 16));
  std::string error;
  auto index = ResolveAsset(service->toc, id, &error);
  if (!index) {
    REXLOG_ERROR("avatar-service: animation ID: {}", error);
    return 0x80070490;
  }
  auto blob = service->toc.Blob(*index);
  if (blob.size() < 32 || std::memcmp(blob.data(), "STRB", 4) || blob[4] != 1 ||
      std::memcmp(blob.data() + 6, Host(0x81632F64), 16))
    return kAssetsInvalid;
  for (size_t p = 32; p < blob.size();) {
    if (blob.size() - p < 12) return kAssetsInvalid;
    const auto size = rex::memory::load_and_swap<uint32_t>(blob.data() + p + 1);
    const auto padded = (uint64_t(size) + 3) & ~uint64_t(3);
    if (padded > blob.size() - p - 12) return kAssetsInvalid;
    if (blob[p] == 1) {
      Allocation stream(size + 0x100);
      if (!stream.p) return kAssetsNoMemory;
      std::memcpy(Host(stream.p + 0x100), blob.data() + p + 12, size);
      Call(0x8197AC30, {stream.p, stream.p + 0x100, size, 0});
      auto ok = Call(0x81977310, {output, stream.p});
      REXLOG_INFO("avatar-service: animation record={} decoded={} payload={}",
                  *index, ok, Read(output + 22920));
      return ok ? 0 : kAssetsFailure;
    }
    p += 12 + size_t(padded);
  }
  return kAssetsInvalid;
}
}  // namespace

extern "C" REX_FUNC(__imp__XamAvatarInitialize) {
  const uint32_t version = ctx.r3.u32, coord = ctx.r4.u32,
                 processor = ctx.r5.u32, callbacks = ctx.r6.u32,
                 buffer = ctx.r7.u32, size = ctx.r8.u32;
  Invoke(ctx, "Initialize", [&] {
    return Initialize(version, coord, processor, callbacks, buffer, size);
  });
}
extern "C" REX_FUNC(__imp__XamAvatarShutdown) {
  Invoke(ctx, "Shutdown", [&] {
    delete service;
    service = nullptr;
    return 0u;
  });
}
extern "C" REX_FUNC(__imp__XamAvatarGetMetadataRandom) {
  const auto body = ctx.r3.u32, count = ctx.r4.u32, output = ctx.r5.u32,
             overlap = ctx.r6.u32;
  Invoke(ctx, "GetMetadataRandom", [&] {
    if (!service || !output) return kAssetsInvalid;
    Profile profile(service->callbacks.p);
    std::vector<std::array<uint8_t, kMetadataSize>> metadata;
    auto hr = service->catalog.Generate(body, count, metadata);
    if (!hr)
      for (size_t i = 0; i < metadata.size(); ++i)
        std::memcpy(Host(output) + i * kMetadataSize, metadata[i].data(),
                    kMetadataSize);
    return Complete(overlap, hr);
  });
}
extern "C" REX_FUNC(__imp__XamAvatarGetManifestLocalUser) {
  const auto overlap = ctx.r5.u32;
  // SDK profiles do not yet provide original avatar metadata. The game's own
  // fallback requests random metadata; never manufacture a profile avatar.
  Invoke(ctx, "GetManifestLocalUser",
         [&] { return Complete(overlap, 0x80070490); });
}
extern "C" REX_FUNC(__imp__XamAvatarManifestGetBodyType) {
  const auto metadata = ctx.r3.u32;
  Invoke(ctx, "ManifestGetBodyType", [&] {
    return metadata ? BodyType(std::span<const uint8_t, kMetadataSize>(
                          Host(metadata), kMetadataSize))
                    : 0u;
  });
}
extern "C" REX_FUNC(__imp__XamAvatarGetAssetsResultSize) {
  const auto mask = ctx.r3.u32, result = ctx.r4.u32, gpu = ctx.r5.u32;
  Invoke(ctx, "GetAssetsResultSize", [&] {
    if (!mask || (mask & ~0x1FFFu) || !result || !gpu) return kAssetsInvalid;
    auto size = ResultSizeFor(uint16_t(mask));
    Put(result, size.result);
    Put(gpu, size.gpu_resources);
    return 0u;
  });
}
extern "C" REX_FUNC(__imp__XamAvatarGetAssets) {
  const auto metadata = ctx.r3.u32, mask = ctx.r4.u32, flags = ctx.r5.u32,
             result = ctx.r6.u32, gpu = ctx.r7.u32, overlap = ctx.r8.u32;
  Invoke(ctx, "GetAssets", [&] {
    if (!service || !metadata || !mask || (mask & ~0x1FFFu) || flags > 1 ||
        !result || !gpu)
      return kAssetsInvalid;
    Profile profile(service->callbacks.p);
    auto size = ResultSizeFor(uint16_t(mask));
    AotAssetsStats stats;
    AssetsRequest request(MakeAotMetadataAssetsBackend(
        *Memory(), Call, service->toc,
        std::span<const uint8_t, kMetadataSize>(Host(metadata), kMetadataSize),
        uint16_t(mask), service->callbacks.p, stats,
        {result, size.result, gpu, size.gpu_resources}));
    while (request.status() == kAssetsPending) request.Step();
    auto hr = request.status();
    request.Release();
    REXLOG_INFO(
        "avatar-service: assets mask={:X} flags={} decoded={} applied={} "
        "finalized={} guards={}",
        mask, flags, stats.decoded, stats.applied, stats.finalized,
        stats.guards);
    return Complete(overlap, hr);
  });
}
extern "C" REX_FUNC(__imp__XamAvatarGenerateMipMaps) {
  const auto result = ctx.r3.u32, size = ctx.r5.u32, output = ctx.r6.u32,
             overlap = ctx.r7.u32;
  Invoke(ctx, "GenerateMipMaps", [&] {
    if (!service) return kAssetsFailure;
    Profile profile(service->callbacks.p);
    return Complete(overlap, MipMaps(result, size, output));
  });
}
extern "C" REX_FUNC(__imp__XamAvatarLoadAnimation) {
  const auto id = ctx.r3.u32, flags = ctx.r4.u32, output = ctx.r5.u32,
             overlap = ctx.r6.u32;
  Invoke(ctx, "LoadAnimation", [&] {
    if (!service) return kAssetsFailure;
    Profile profile(service->callbacks.p);
    return Complete(overlap, Animation(id, flags, output));
  });
}
extern "C" REX_FUNC(__imp__XamAvatarSetCustomAsset) {
  Invoke(ctx, "SetCustomAsset", [] { return 0x80004001u; });
}
