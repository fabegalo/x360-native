#pragma once
#include <rex/graphics/pipeline/texture/util.h>
#include "host_layout.h"
#if defined(_WIN32)
#include <wrl/client.h>
#endif
#include <array>
#include <span>
#include <vector>
namespace x360n::native {
class ScaledResolveCache;
class D3D12Commands;
// Guest texture storage selected by a fetch constant, in the terms both the
// CPU conversion and the GPU load use. Base/mip are byte addresses.
struct TextureLoadInput {
  rex::graphics::xenos::TextureFormat format;
  rex::graphics::xenos::DataDimension dimension;
  rex::graphics::xenos::Endian endian;
  bool tiled, packed_mips;
  uint32_t width, height, depth_or_slices, pitch;  // pitch: fetch units (texels / 32)
  uint32_t base, mip;
  uint32_t max_level;                              // highest host level
  bool host_volume;                                // 3D host resource
  unsigned array_size;                             // 2D array slices (cube faces, stacked)
  DXGI_FORMAT host_format;
  bool is_signed, num_format;
  uint32_t scale_x = 1, scale_y = 1;
  const ScaledResolveCache* scaled = nullptr;
  rex::graphics::texture_util::TextureGuestLayout Layout() const;
};
// Pinned SDK texture_load_* compute shaders Native dispatches (indices are
// Native's own; TextureCache keeps its table protected).
enum class LoadShader { k8bpb, k16bpb, k32bpb, k64bpb, k128bpb, kDepthUnorm, kDepthFloat, kCount, kUnknown = kCount };
// The shaders' push_consts_xe layout (TextureCache::LoadConstants).
struct LoadConstants {
  uint32_t is_tiled_3d_endian_scale, guest_offset, guest_pitch_aligned, guest_z_stride_block_rows_aligned;
  uint32_t size_blocks[3], host_offset;
  uint32_t host_pitch, height_texels;
};
static_assert(sizeof(LoadConstants) == 40);
constexpr uint32_t kLoadGuestXThreadsPerGroupLog2 = 2, kLoadGuestYBlocksPerGroupLog2 = 5;
// RGBA4, signed RGB10A2 and integer 16-bit textures keep Native's CPU conversion.
bool NeedsCpuConversion(rex::graphics::xenos::TextureFormat, bool is_signed, bool num_format);
// The translator declares alternative signed and dimensional views. Null
// alternatives do not read guest bytes and must not trigger CPU synchronization.
bool TextureViewActive(const rex::graphics::xenos::xe_gpu_texture_fetch_t&,
    rex::graphics::xenos::FetchOpDimension, bool is_signed);
// Pinned SDK load shader whose output equals Native's CPU conversion for this
// binding; LoadShader::kUnknown keeps the CPU conversion.
LoadShader GpuLoadShader(const TextureLoadInput&);
// GPU load of guest bytes from the resident guest memory mirror, laid out as
// the pinned D3D12 texture cache does (LoadTextureDataFromResidentMemoryImpl):
// the load shader untiles and endian-swaps into a scratch buffer, then
// footprint copies place every level and slice, including packed mip tails.
struct TextureLoadPlan {
  LoadShader shader = LoadShader::kUnknown;
  uint32_t source_bpe_log2 = 0, dest_bpe_log2 = 0;
  struct Dispatch { LoadConstants constants; uint32_t groups[3]; };
  std::vector<Dispatch> dispatches;
  struct Copy {
    uint32_t subresource;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    bool boxed;
    D3D12_BOX box;
  };
  std::vector<Copy> copies;
  uint64_t scratch_bytes = 0;
};
TextureLoadPlan PlanGpuTextureLoad(const TextureLoadInput&, LoadShader);
// The plan's footprint copies as buffer-to-image regions (Vulkan): each copy
// reads the same scratch blocks as D3D12 CopyTextureRegion with its box, and
// stops at the level's edge. Offsets are relative to the plan's scratch base.
struct BufferImageRegion { uint64_t buffer_offset; uint32_t row_length, image_height, level, layer, extent[3]; };
std::vector<BufferImageRegion> PlanBufferImageRegions(const TextureLoadPlan&, uint32_t levels,
    uint32_t block_bytes, uint32_t block_width, std::array<uint32_t, 3> extent);
// Native's CPU conversion of guest bytes into the subresource footprints of
// the host resource (GetCopyableFootprints order: level-major per slice).
void ConvertTextureCpu(const TextureLoadInput&, std::span<const uint8_t> base,
    std::span<const uint8_t> mips, std::span<const D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints,
    uint8_t* output);
#if defined(_WIN32)
// Root signature and pipelines of the shared load shaders (constants, source
// SRV table, destination UAV table).
Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateTextureLoadRoot(ID3D12Device*);
Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateTextureLoadPipeline(ID3D12Device*, ID3D12RootSignature*, LoadShader);
DXGI_FORMAT UintPow2Format(uint32_t log2);
// Records the dispatches into scratch (UAV state, bound by the caller at
// scratch_offset), then the copies into texture (COPY_DEST state). Leaves the
// scratch buffer in COPY_SOURCE. Recorded through D3D12Commands, so the
// commands keep their order when a pipeline is still being created.
void RecordTextureLoad(D3D12Commands&, const TextureLoadPlan&, uint64_t scratch_offset,
    ID3D12Resource* scratch, ID3D12Resource* texture);
#endif
}
