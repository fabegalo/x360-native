#include "texture_load.h"
#include "texture.h"
#include "texture_block.h"
#include <rex/graphics/pipeline/texture/info.h>
#include <algorithm>
#include <bit>
#include <stdexcept>
#include <string>
namespace x360n::native {
using namespace rex::graphics;
using namespace rex::graphics::xenos;
namespace bytecode {
#include "src/graphics/shaders/bytecode/d3d12_5_1/texture_load_8bpb_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/texture_load_16bpb_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/texture_load_32bpb_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/texture_load_64bpb_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/texture_load_128bpb_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/texture_load_depth_unorm_cs.h"
#include "src/graphics/shaders/bytecode/d3d12_5_1/texture_load_depth_float_cs.h"
}
namespace {
void Check(HRESULT hr, const char* what) {
  if (FAILED(hr)) throw std::runtime_error(std::string("Native texture load: ") + what + " HRESULT=" + std::to_string(uint32_t(hr)));
}
// Values of TextureCache::load_shader_info_ in the pinned SDK
// (src/graphics/pipeline/texture/cache.cpp) for the shaders Native uses:
// source/dest element log2, host bytes per block, guest X blocks per thread log2.
struct Info { uint32_t source_bpe_log2, dest_bpe_log2, bytes_per_host_block, x_blocks_log2; };
Info LoadInfo(LoadShader shader) {
  switch (shader) {
    case LoadShader::k8bpb: return {3, 4, 1, 4};
    case LoadShader::k16bpb: return {4, 4, 2, 4};
    case LoadShader::k32bpb: return {4, 4, 4, 3};
    case LoadShader::k64bpb: return {4, 4, 8, 2};
    case LoadShader::k128bpb: return {4, 4, 16, 1};
    case LoadShader::kDepthUnorm: case LoadShader::kDepthFloat: return {4, 4, 4, 3};
    default: throw std::runtime_error("Native texture load: shader without Native mapping");
  }
}
bool DepthFormat(TextureFormat f) { return f == TextureFormat::k_24_8 || f == TextureFormat::k_24_8_FLOAT; }
}
texture_util::TextureGuestLayout TextureLoadInput::Layout() const {
  return texture_util::GetGuestTextureLayout(dimension, pitch, width, height, depth_or_slices, tiled, format,
      packed_mips, true, max_level);
}
bool NeedsCpuConversion(TextureFormat format, bool is_signed, bool num_format) {
  // RGBA4: the pinned D3D12 shaders target B4G4R4A4, not Native's exact RGBA8.
  return format == TextureFormat::k_4_4_4_4 ||
      (GetBaseFormat(format) == TextureFormat::k_2_10_10_10 && is_signed) ||
      (num_format && format == TextureFormat::k_16);
}
LoadShader GpuLoadShader(const TextureLoadInput& t) {
  if (NeedsCpuConversion(t.format, t.is_signed, t.num_format)) return LoadShader::kUnknown;
  if (DepthFormat(t.format))
    return t.format == TextureFormat::k_24_8_FLOAT ? LoadShader::kDepthFloat : LoadShader::kDepthUnorm;
  switch (FormatInfo::Get(t.format)->bytes_per_block()) {
    case 1: return LoadShader::k8bpb;
    case 2: return LoadShader::k16bpb;
    case 4: return LoadShader::k32bpb;
    case 8: return LoadShader::k64bpb;
    case 16: return LoadShader::k128bpb;
    default: return LoadShader::kUnknown;
  }
}
TextureLoadPlan PlanGpuTextureLoad(const TextureLoadInput& t, LoadShader shader) {
  // Mirrors D3D12TextureCache::LoadTextureDataFromResidentMemoryImpl of the
  // pinned SDK (unscaled, no decompression), loading base and mips together.
  TextureLoadPlan plan;
  plan.shader = shader;
  const Info info = LoadInfo(shader);
  plan.source_bpe_log2 = info.source_bpe_log2; plan.dest_bpe_log2 = info.dest_bpe_log2;
  const auto layout = t.Layout();
  const FormatInfo* fi = FormatInfo::Get(t.format);
  const uint32_t block_width = fi->block_width, block_height = fi->block_height, bytes_per_block = fi->bytes_per_block();
  const bool is_3d = t.dimension == DataDimension::k3D;
  const uint32_t depth = t.host_volume ? t.depth_or_slices : 1;
  const uint32_t array_size = t.array_size;
  const uint32_t level_last = t.max_level;
  const uint32_t level_packed = layout.packed_level;
  const uint32_t level_stored_last = std::min(level_last, level_packed);
  uint32_t loop_level_first, loop_level_last;
  if (level_packed == 0) { loop_level_first = 0; loop_level_last = uint32_t(level_last != 0); }
  else { loop_level_first = 0; loop_level_last = level_stored_last; }
  // Native never decompresses: block-compressed guest formats stay compressed.
  const uint32_t host_block_width = block_width, host_block_height = block_height;
  const uint32_t host_x_blocks_per_thread = 1u << info.x_blocks_log2;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT slice_base{}, slice_mips[kTextureMaxMips]{};
  uint64_t size_base = 0, size_mips[kTextureMaxMips]{};
  uint64_t offset = 0;
  for (uint32_t loop_level = loop_level_first; loop_level <= loop_level_last; ++loop_level) {
    const bool is_base = loop_level == 0;
    const uint32_t level = level_packed == 0 ? 0 : loop_level;
    auto& fp = is_base ? slice_base : slice_mips[level];
    fp.Offset = offset; fp.Footprint.Format = t.host_format;
    if (level == level_packed) {
      const auto& packed = is_base ? layout.base : layout.mips[level];
      fp.Footprint.Width = packed.x_extent_blocks * block_width;
      fp.Footprint.Height = packed.y_extent_blocks * block_height;
      fp.Footprint.Depth = t.host_volume ? packed.z_extent : 1;
    } else {
      fp.Footprint.Width = std::max(t.width >> level, 1u);
      fp.Footprint.Height = std::max(t.height >> level, 1u);
      fp.Footprint.Depth = std::max(depth >> level, 1u);
    }
    fp.Footprint.Width = (fp.Footprint.Width + host_block_width - 1) / host_block_width * host_block_width;
    fp.Footprint.Height = (fp.Footprint.Height + host_block_height - 1) / host_block_height * host_block_height;
    const uint32_t row_blocks = (fp.Footprint.Width / host_block_width + host_x_blocks_per_thread - 1) /
        host_x_blocks_per_thread * host_x_blocks_per_thread;
    fp.Footprint.RowPitch = (row_blocks * info.bytes_per_host_block + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
        ~uint32_t(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
    const uint64_t slice_size = (uint64_t(fp.Footprint.RowPitch) * (fp.Footprint.Height / host_block_height) * fp.Footprint.Depth +
        D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1) & ~uint64_t(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1);
    (is_base ? size_base : size_mips[level]) = slice_size;
    offset += slice_size * array_size;
  }
  plan.scratch_bytes = offset;
  LoadConstants c{};
  c.is_tiled_3d_endian_scale = uint32_t(t.tiled) | (uint32_t(is_3d) << 1) | (uint32_t(t.endian) << 2) | (1u << 4) | (1u << 7);
  const uint32_t x_per_group_log2 = kLoadGuestXThreadsPerGroupLog2 + info.x_blocks_log2;
  for (uint32_t loop_level = loop_level_first; loop_level <= loop_level_last; ++loop_level) {
    const bool is_base = loop_level == 0;
    const uint32_t level = level_packed == 0 ? 0 : loop_level;
    c.guest_offset = is_base ? t.base : t.mip + layout.mip_offsets_bytes[level];
    const auto& gl = is_base ? layout.base : layout.mips[level];
    uint32_t pitch = gl.row_pitch_bytes;
    if (t.tiled) pitch /= bytes_per_block;
    c.guest_pitch_aligned = pitch;
    c.guest_z_stride_block_rows_aligned = gl.z_slice_stride_block_rows;
    uint32_t w, h, d;
    if (level == level_packed) { w = gl.x_extent_blocks * block_width; h = gl.y_extent_blocks * block_height; d = gl.z_extent; }
    else { w = std::max(t.width >> level, 1u); h = std::max(t.height >> level, 1u); d = std::max((is_3d ? t.depth_or_slices : 1u) >> level, 1u); }
    // A 2D view of volume storage loads its first slice only.
    if (!t.host_volume) d = 1;
    c.size_blocks[0] = (w + block_width - 1) / block_width;
    c.size_blocks[1] = (h + block_height - 1) / block_height;
    c.size_blocks[2] = d;
    c.height_texels = h;
    const auto& fp = is_base ? slice_base : slice_mips[level];
    const uint64_t slice_size = is_base ? size_base : size_mips[level];
    c.host_offset = uint32_t(fp.Offset);
    c.host_pitch = fp.Footprint.RowPitch;
    const uint32_t gx = (c.size_blocks[0] + (1u << x_per_group_log2) - 1) >> x_per_group_log2;
    const uint32_t gy = (c.size_blocks[1] + (1u << kLoadGuestYBlocksPerGroupLog2) - 1) >> kLoadGuestYBlocksPerGroupLog2;
    for (uint32_t slice = 0; slice < array_size; ++slice) {
      plan.dispatches.push_back({c, {gx, gy, c.size_blocks[2]}});
      c.guest_offset += gl.array_slice_stride_bytes;
      c.host_offset += uint32_t(slice_size);
    }
  }
  const uint32_t levels = t.max_level + 1;
  for (uint32_t level = 0; level <= level_last; ++level) {
    const uint32_t guest_level = std::min(level, level_packed);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = level ? slice_mips[guest_level] : slice_base;
    const uint64_t slice_size = level ? size_mips[guest_level] : size_base;
    TextureLoadPlan::Copy copy{};
    copy.boxed = level >= level_packed;
    if (copy.boxed) {
      uint32_t ox, oy, oz;
      texture_util::GetPackedMipOffset(t.width, t.height, t.host_volume ? t.depth_or_slices : 1, t.format, level, ox, oy, oz);
      copy.box.left = ox * block_width; copy.box.top = oy * block_height; copy.box.front = oz;
      copy.box.right = copy.box.left + (std::max(t.width >> level, 1u) + host_block_width - 1) / host_block_width * host_block_width;
      copy.box.bottom = copy.box.top + (std::max(t.height >> level, 1u) + host_block_height - 1) / host_block_height * host_block_height;
      copy.box.back = copy.box.front + std::max(depth >> level, 1u);
    }
    for (uint32_t slice = 0; slice < array_size; ++slice) {
      copy.subresource = level + slice * levels;
      copy.footprint = fp;
      plan.copies.push_back(copy);
      fp.Offset += slice_size;
    }
  }
  return plan;
}
void ConvertTextureCpu(const TextureLoadInput& t, std::span<const uint8_t> base_source, std::span<const uint8_t> mip_source,
    std::span<const D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints, uint8_t* linear) {
  const auto layout = t.Layout();
  const FormatInfo* fi = FormatInfo::Get(t.format);
  const unsigned block = fi->bytes_per_block();
  const bool volume = t.dimension == DataDimension::k3D;
  const bool depth_texture = DepthFormat(t.format);
  const bool integer16 = t.num_format && t.format == TextureFormat::k_16;
  const bool rgba4 = t.format == TextureFormat::k_4_4_4_4;
  const bool rgb10_signed = GetBaseFormat(t.format) == TextureFormat::k_2_10_10_10 && t.is_signed;
  const unsigned host_block = (rgba4 && t.is_signed) || rgb10_signed ? 16 : integer16 || rgba4 ? 4 : block;
  const bool raw = !depth_texture && !rgb10_signed && !rgba4 && !integer16;
  const unsigned block_log2 = std::countr_zero(block);
  const unsigned levels = t.max_level + 1;
  const Endian endian = TextureBlockEndian(block, t.endian);
  const uint32_t base_page = t.base, mip_page = t.mip;
  for (unsigned level = 0; level <= t.max_level; ++level) {
    unsigned stored_level = std::min(level, layout.packed_level);
    const auto& gl = level ? layout.mips[stored_level] : layout.base;
    uint32_t address = level ? mip_page + layout.mip_offsets_bytes[stored_level] : base_page;
    unsigned ox = 0, oy = 0, oz = 0;
    if (t.packed_mips) texture_util::GetPackedMipOffset(t.width, t.height, volume ? t.depth_or_slices : 1, t.format, level, ox, oy, oz);
    unsigned bx = (std::max(1u, t.width >> level) + fi->block_width - 1) / fi->block_width;
    unsigned by = (std::max(1u, t.height >> level) + fi->block_height - 1) / fi->block_height;
    const std::span<const uint8_t> source = level ? mip_source : base_source;
    const uint32_t pitch_blocks = gl.row_pitch_bytes / block;
    for (unsigned slice = 0; slice < t.array_size; ++slice) {
    const size_t level_offset = size_t(address - (level ? mip_page : base_page)) + size_t(slice) * gl.array_slice_stride_bytes;
    const auto& footprint = footprints[level + slice * levels];
    for (unsigned z = 0; z < (t.host_volume ? std::max(1u, t.depth_or_slices >> level) : 1u); ++z)
    for (unsigned y = 0; y < by; ++y) {
      uint8_t* row = linear + footprint.Offset + (uint64_t(z) * by + y) * footprint.Footprint.RowPitch;
      const uint32_t linear_row = ((z + oz) * gl.z_slice_stride_block_rows + y + oy) * gl.row_pitch_bytes + ox * block;
      // Whole linear rows of 32/64/128-bit blocks are word aligned; the
      // per-word endian swap equals the per-byte address XOR below.
      if (raw && !t.tiled && block >= 4 && !((level_offset + linear_row) & 3)) {
        ReadTextureBlock(source, level_offset + linear_row, bx * block, endian, row);
        continue;
      }
      if (raw && t.tiled && !volume) {
        const unsigned run = TiledContiguousBlocks(block_log2);
        for (unsigned x = 0; x < bx;) {
          const unsigned n = std::min(run - ((x + ox) & (run - 1)), bx - x);
          const uint32_t offset = texture_util::GetTiledOffset2D(x + ox, y + oy, pitch_blocks, block_log2);
          ReadTextureBlock(source, level_offset + offset, n * block, endian, row + size_t(x) * block);
          x += n;
        }
        continue;
      }
      for (unsigned x = 0; x < bx; ++x) {
      uint32_t offset = t.tiled ? (volume ? uint32_t(texture_util::GetTiledOffset3D(x + ox, y + oy, z + oz,
          pitch_blocks, gl.z_slice_stride_block_rows, block_log2)) :
          texture_util::GetTiledOffset2D(x + ox, y + oy, pitch_blocks, block_log2)) :
          linear_row + x * block;
      uint8_t* dest = row + size_t(x) * host_block;
      if (raw) { ReadTextureBlock(source, level_offset + offset, block, endian, dest); continue; }
      uint8_t texel[16];
      ReadTextureBlock(source, level_offset + offset, block, endian, texel);
      if (depth_texture) {
        uint32_t bits; std::memcpy(&bits, texel, 4);
        const float value = DecodeDepthTexture(bits, t.format == TextureFormat::k_24_8_FLOAT);
        std::memcpy(dest, &value, 4);
      } else if (rgb10_signed) {
        uint32_t bits; std::memcpy(&bits, texel, 4);
        float values[]{PackedSnorm(bits & 1023, 10), PackedSnorm((bits >> 10) & 1023, 10),
            PackedSnorm((bits >> 20) & 1023, 10), PackedSnorm(bits >> 30, 2)};
        std::memcpy(dest, values, sizeof(values));
      } else if (rgba4) {
        uint16_t bits; std::memcpy(&bits, texel, 2);
        if (t.is_signed) {
          float values[4];
          for (unsigned c = 0; c < 4; ++c) values[c] = Rgba4Snorm((bits >> (c * 4)) & 15);
          std::memcpy(dest, values, sizeof(values));
        } else {
          uint32_t expanded = ExpandRgba4(bits);
          std::memcpy(dest, &expanded, 4);
        }
      } else {
        uint16_t bits; std::memcpy(&bits, texel, 2);
        float value = t.is_signed ? float(int16_t(bits)) : float(bits);
        std::memcpy(dest, &value, 4);
      }
      }
    }
    }
  }
}
DXGI_FORMAT UintPow2Format(uint32_t log2) {
  switch (log2) {
    case 0: return DXGI_FORMAT_R8_UINT; case 1: return DXGI_FORMAT_R16_UINT; case 2: return DXGI_FORMAT_R32_UINT;
    case 3: return DXGI_FORMAT_R32G32_UINT; case 4: return DXGI_FORMAT_R32G32B32A32_UINT;
  }
  throw std::runtime_error("Native texture load: element size");
}
Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateTextureLoadRoot(ID3D12Device* device) {
  D3D12_ROOT_PARAMETER p[3]{};
  p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  p[0].Constants = {0, 0, sizeof(LoadConstants) / 4};
  D3D12_DESCRIPTOR_RANGE ranges[]{{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0}, {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0}};
  for (unsigned i = 1; i < 3; ++i) { p[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[i].DescriptorTable = {1, &ranges[i - 1]}; }
  D3D12_ROOT_SIGNATURE_DESC desc{3, p, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
  Microsoft::WRL::ComPtr<ID3DBlob> blob, errors;
  Check(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors), "root serialization");
  Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
  Check(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)), "root");
  return root;
}
Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateTextureLoadPipeline(ID3D12Device* device, ID3D12RootSignature* root, LoadShader shader) {
  using namespace bytecode;
  D3D12_SHADER_BYTECODE code{};
  switch (shader) {
    case LoadShader::k8bpb: code = {texture_load_8bpb_cs, sizeof(texture_load_8bpb_cs)}; break;
    case LoadShader::k16bpb: code = {texture_load_16bpb_cs, sizeof(texture_load_16bpb_cs)}; break;
    case LoadShader::k32bpb: code = {texture_load_32bpb_cs, sizeof(texture_load_32bpb_cs)}; break;
    case LoadShader::k64bpb: code = {texture_load_64bpb_cs, sizeof(texture_load_64bpb_cs)}; break;
    case LoadShader::k128bpb: code = {texture_load_128bpb_cs, sizeof(texture_load_128bpb_cs)}; break;
    case LoadShader::kDepthUnorm: code = {texture_load_depth_unorm_cs, sizeof(texture_load_depth_unorm_cs)}; break;
    case LoadShader::kDepthFloat: code = {texture_load_depth_float_cs, sizeof(texture_load_depth_float_cs)}; break;
    default: throw std::runtime_error("Native texture load: no pipeline for shader");
  }
  D3D12_COMPUTE_PIPELINE_STATE_DESC desc{}; desc.pRootSignature = root; desc.CS = code;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
  Check(device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipeline)), "pipeline");
  return pipeline;
}
void RecordTextureLoad(ID3D12GraphicsCommandList* list, const TextureLoadPlan& plan, uint64_t scratch_offset,
    ID3D12Resource* scratch, ID3D12Resource* texture) {
  for (const auto& d : plan.dispatches) {
    auto c = d.constants;
    c.host_offset += uint32_t(scratch_offset);
    list->SetComputeRoot32BitConstants(0, sizeof(c) / 4, &c, 0);
    list->Dispatch(d.groups[0], d.groups[1], d.groups[2]);
  }
  D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition = {scratch, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE};
  list->ResourceBarrier(1, &b);
  for (const auto& copy : plan.copies) {
    D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = scratch; src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = copy.footprint; src.PlacedFootprint.Offset += scratch_offset;
    D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = texture; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = copy.subresource;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, copy.boxed ? &copy.box : nullptr);
  }
}
}
