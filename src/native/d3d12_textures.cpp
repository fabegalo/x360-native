#include "d3d12_render.h"
#include "texture.h"
#include "texture_block.h"
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/logging.h>
#include <stdexcept>
#include <cfloat>
#include <bit>
namespace x360n::native {
using namespace rex::graphics;
using namespace rex::graphics::xenos;
namespace {
void Require(bool v, const char* t) { if (!v) throw std::runtime_error(std::string("Native texture: ") + t); }
DXGI_FORMAT Format(TextureFormat f, bool sign) {
  switch(GetBaseFormat(f)) {
    case TextureFormat::k_2_10_10_10:
      return sign ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R10G10B10A2_UNORM;
    case TextureFormat::k_4_4_4_4:
      return sign ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
    case TextureFormat::k_8: return sign ? DXGI_FORMAT_R8_SNORM : DXGI_FORMAT_R8_UNORM;
    case TextureFormat::k_8_8: return sign ? DXGI_FORMAT_R8G8_SNORM : DXGI_FORMAT_R8G8_UNORM;
    case TextureFormat::k_8_8_8_8: return sign ? DXGI_FORMAT_R8G8B8A8_SNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
    case TextureFormat::k_16: return sign ? DXGI_FORMAT_R16_SNORM : DXGI_FORMAT_R16_UNORM;
    case TextureFormat::k_16_16: return sign ? DXGI_FORMAT_R16G16_SNORM : DXGI_FORMAT_R16G16_UNORM;
    case TextureFormat::k_16_16_16_16: return sign ? DXGI_FORMAT_R16G16B16A16_SNORM : DXGI_FORMAT_R16G16B16A16_UNORM;
    case TextureFormat::k_16_FLOAT: return DXGI_FORMAT_R16_FLOAT;
    case TextureFormat::k_24_8: case TextureFormat::k_24_8_FLOAT:
    case TextureFormat::k_32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
    case TextureFormat::k_DXT1: case TextureFormat::k_DXT2_3: case TextureFormat::k_DXT4_5:
    case TextureFormat::k_DXT5A: case TextureFormat::k_DXN:
      // Only reached when a signed component is actually sampled.
      if (sign) throw std::runtime_error("Native signed block-compressed texture format " + std::to_string(uint32_t(f)) + " not implemented");
      return GetBaseFormat(f) == TextureFormat::k_DXT1 ? DXGI_FORMAT_BC1_UNORM :
          GetBaseFormat(f) == TextureFormat::k_DXT2_3 ? DXGI_FORMAT_BC2_UNORM :
          GetBaseFormat(f) == TextureFormat::k_DXT4_5 ? DXGI_FORMAT_BC3_UNORM :
          GetBaseFormat(f) == TextureFormat::k_DXT5A ? DXGI_FORMAT_BC4_UNORM : DXGI_FORMAT_BC5_UNORM;
    case TextureFormat::k_16_16_FLOAT: return DXGI_FORMAT_R16G16_FLOAT;
    case TextureFormat::k_16_16_16_16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: throw std::runtime_error("Native texture format " + std::to_string(uint32_t(f)) + " not implemented");
  }
}
// Pinned D3D12 mapping: halfway and mirror-to-border modes have no host
// equivalent and are normalized exactly as NormalizeClampMode does.
D3D12_TEXTURE_ADDRESS_MODE Clamp(ClampMode c) {
  switch(c) { case ClampMode::kRepeat: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    case ClampMode::kMirroredRepeat: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    case ClampMode::kClampToEdge: case ClampMode::kClampToHalfway: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case ClampMode::kMirrorClampToEdge: case ClampMode::kMirrorClampToHalfway:
    case ClampMode::kMirrorClampToBorder: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
    case ClampMode::kClampToBorder: return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    default: throw std::runtime_error("Native texture clamp mode not implemented"); }
}
}
void D3D12Render::BindTextures(const RegisterFile& r, rex::memory::Memory& memory, DxbcShader& shader, unsigned root, unsigned sampler_root) {
  Timer timer(stats_enabled_, texture_ns_);
  const auto& textures = shader.GetTextureBindingsAfterTranslation();
  if (!textures.empty()) {
    auto first = Views(uint32_t(textures.size()));
    for (unsigned i = 0; i < textures.size(); ++i) {
      const auto& binding = textures[i]; auto f = r.GetTextureFetch(binding.fetch_constant);
      // As the pinned texture cache, a fetch constant that is not a texture,
      // or has no data, gives an invalid binding: its view is null (zeros)
      // and the draw still runs. Rate-limited, never silent.
      bool valid = f.type == FetchConstantType::kTexture;
      if (!valid && invalid_texture_fetches_++ < 8)
        REXLOG_WARN("Native: texture fetch {} has type {} ({:08X} {:08X} {:08X} {:08X} {:08X} {:08X}); view is null as in the pinned texture cache",
            binding.fetch_constant, uint32_t(f.type), f.dword_0, f.dword_1, f.dword_2, f.dword_3, f.dword_4, f.dword_5);
      uint32_t wm = 0, hm = 0, dm = 0, base = 0, mip = 0, min_level = 0, max_level = 0;
      if (valid) {
        texture_util::GetSubresourcesFromFetchConstant(f, &wm, &hm, &dm, &base, &mip, &min_level, &max_level);
        valid = base || mip;
        if (valid && f.dimension == DataDimension::k1D && (wm >= kTexture2DCubeMaxWidthHeight || f.tiled || f.packed_mips)) {
          REXLOG_ERROR("Native: invalid 1D texture fetch {} (width {}, tiled {}, packed mips {}); view is null as in the pinned texture cache",
              binding.fetch_constant, wm + 1, uint32_t(f.tiled), uint32_t(f.packed_mips));
          valid = false;
        }
      }
      if (valid && f.dimension == DataDimension::k3D && f.stacked) throw std::runtime_error("Native texture dimension=" + std::to_string(uint32_t(f.dimension)) + " stacked=" + std::to_string(f.stacked) + " type=" + std::to_string(uint32_t(f.type)) + " format=" + std::to_string(uint32_t(f.format)) + " binding=" + std::to_string(binding.fetch_constant));
      Require(!valid || !f.border_size, "border texture storage not implemented");
      // Pinned D3D12 compatibility: 2D views accept 1D/2D/3D storage (3D as
      // its first slice), 3D-or-stacked views only 3D, cube views only cubes.
      // The translated shader selects the view by the fetch dimension at run
      // time, and per component between signed and unsigned results.
      const bool compatible = binding.dimension == FetchOpDimension::kCube ? f.dimension == DataDimension::kCube :
          binding.dimension == FetchOpDimension::k3DOrStacked ? f.dimension == DataDimension::k3D : f.dimension != DataDimension::kCube;
      const uint8_t signs = texture_util::SwizzleSigns(f);
      if (!valid || !compatible || (binding.is_signed ? !texture_util::IsAnySignSigned(signs) : !texture_util::IsAnySignNotSigned(signs))) {
        D3D12_SHADER_RESOURCE_VIEW_DESC unused{}; unused.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        unused.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (binding.dimension == FetchOpDimension::k3DOrStacked) {
          unused.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D; unused.Texture3D.MipLevels = 1;
        } else if (binding.dimension == FetchOpDimension::kCube) {
          unused.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE; unused.TextureCube.MipLevels = 1;
        } else {
          unused.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY; unused.Texture2DArray.MipLevels = 1; unused.Texture2DArray.ArraySize = 1;
        }
        device_->CreateShaderResourceView(nullptr, &unused, Cpu(first + i));
        continue;
      }
      Require(base != 0, "mip-only texture not implemented");
      const bool volume = f.dimension == DataDimension::k3D;
      const bool host_volume = volume && binding.dimension == FetchOpDimension::k3DOrStacked;
      // The translator also declares a 2D alternative for stacked fetches.
      // A 2D fetch of volume storage views its first base-level slice.
      if (volume && !host_volume) max_level = 0;
      auto format = Format(f.format, binding.is_signed); auto fi = FormatInfo::Get(f.format);
      unsigned block = fi->bytes_per_block();
      const bool depth_texture = f.format == TextureFormat::k_24_8 || f.format == TextureFormat::k_24_8_FLOAT;
      const bool integer16 = f.num_format && f.format == TextureFormat::k_16;
      const bool rgba4 = f.format == TextureFormat::k_4_4_4_4;
      const bool rgb10_signed = GetBaseFormat(f.format) == TextureFormat::k_2_10_10_10 && binding.is_signed;
      if (f.num_format && !integer16 && !depth_texture && !IsFloatTexture(f.format)) throw std::runtime_error("Native integer texture format=" + std::to_string(uint32_t(f.format)) + " signed=" + std::to_string(binding.is_signed) + " binding=" + std::to_string(binding.fetch_constant));
      if (integer16) format = DXGI_FORMAT_R32_FLOAT;
      unsigned host_block = (rgba4 && binding.is_signed) || rgb10_signed ? 16 : integer16 || rgba4 ? 4 : block;
      auto layout = texture_util::GetGuestTextureLayout(f.dimension, f.pitch, wm + 1, hm + 1, dm + 1, f.tiled, f.format, f.packed_mips, true, max_level);
      const uint32_t base_bytes = (layout.base.level_data_extent_bytes + 3u) & ~3u;
      const uint32_t mip_bytes = max_level ? (layout.mips_total_extent_bytes + 3u) & ~3u : 0;
      Require(uint64_t(base << 12) + base_bytes <= 0x20000000 &&
          uint64_t(mip << 12) + mip_bytes <= 0x20000000, "texture source extent overflow");
      // Draw synchronized every texture range with incomplete resolves first.
      Require(!writeback_.Overlaps(base << 12, base_bytes) && (!mip_bytes || !writeback_.Overlaps(mip << 12, mip_bytes)),
          "texture read overlaps an incomplete resolve");
      std::span<const uint8_t> base_source(memory.TranslatePhysical<const uint8_t*>(base << 12), base_bytes);
      std::span<const uint8_t> mip_source;
      if (mip_bytes) mip_source = {memory.TranslatePhysical<const uint8_t*>(mip << 12), mip_bytes};
      // Cube faces and stacked 2D slices are array slices of every level.
      const bool cube = f.dimension == DataDimension::kCube;
      const unsigned array_size = cube || (f.dimension == DataDimension::k2DOrStacked && f.stacked) ? dm + 1 : 1;
      const unsigned levels = max_level + 1;
      // The guest swizzle is applied by this per-bind view, not stored bytes.
      D3D12_SHADER_RESOURCE_VIEW_DESC view{}; view.Format = format;
      if (host_volume) {
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D; view.Texture3D.MipLevels = levels;
      } else if (cube) {
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE; view.TextureCube.MipLevels = levels;
      } else {
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY; view.Texture2DArray.MipLevels = levels; view.Texture2DArray.ArraySize = array_size;
      }
      unsigned components = 4;
      switch (GetBaseFormat(f.format)) {
        case TextureFormat::k_DXT5A: case TextureFormat::k_24_8: case TextureFormat::k_24_8_FLOAT: case TextureFormat::k_8: case TextureFormat::k_16: case TextureFormat::k_16_FLOAT: case TextureFormat::k_32_FLOAT: components = 1; break;
        case TextureFormat::k_DXN: case TextureFormat::k_8_8: case TextureFormat::k_16_16: case TextureFormat::k_16_16_FLOAT: components = 2; break;
        default: break;
      }
      auto component = [&](unsigned channel) { unsigned s = (f.swizzle >> (channel * 3)) & 7; Require(s <= 5, "reserved swizzle component"); return s < 4 ? std::min(s, components - 1) : s; };
      view.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(component(0), component(1), component(2), component(3));
      // Content identity: every field that selects or converts guest bytes.
      // Sampler-only fetch fields (filters, clamps, LOD, swizzle) share it.
      const uint32_t key_fields[]{base, mip, wm, hm, dm, levels, f.pitch, uint32_t(f.format),
          uint32_t(f.tiled) | uint32_t(f.packed_mips) << 1 | uint32_t(f.endianness) << 2 | uint32_t(f.dimension) << 4 |
          uint32_t(f.stacked) << 6 | uint32_t(binding.is_signed) << 7 | uint32_t(host_volume) << 8 | uint32_t(f.num_format) << 9};
      std::string cache_key(reinterpret_cast<const char*>(key_fields), sizeof(key_fields));
      CachedTexture* entry = nullptr;
      if (texture_cache_enabled_) {
        auto found = textures_.find(cache_key);
        if (found != textures_.end()) {
          entry = &found->second;
          entry->last_use = ++texture_use_;
          // Guest bytes are compared once per guest epoch: guest-visible
          // writes (CPU after a sync point, resolve results) start a new one.
          if (entry->epoch == guest_epoch_ || entry->source.Matches(base_source, mip_source)) {
            entry->epoch = guest_epoch_;
            device_->CreateShaderResourceView(entry->resource.Get(), &view, Cpu(first + i));
            transient_.push_back(entry->resource); // Keep alive through in-flight eviction.
            if (stats_enabled_) ++texture_cache_hits_;
            continue;
          }
        }
      }
      TextureSnapshot snapshot;
      snapshot.Assign(base_source, mip_source);
      D3D12_RESOURCE_DESC d{}; d.Dimension = host_volume ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = wm + 1; d.Height = hm + 1;
      d.DepthOrArraySize = UINT16(host_volume ? dm + 1 : array_size); d.MipLevels = UINT16(levels); d.Format = format; d.SampleDesc.Count = 1;
      Ptr<ID3D12Resource> texture;
      auto state = D3D12_RESOURCE_STATE_COPY_DEST;
      if (entry) {
        // Same identity, new guest bytes: rewrite in place. Barriers order the
        // copy after earlier draws of this list and before later ones.
        texture = entry->resource;
        state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        Transition(texture.Get(), state, D3D12_RESOURCE_STATE_COPY_DEST);
      } else {
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        Require(SUCCEEDED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture))), "create resource");
      }
      std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(levels * array_size);
      uint64_t bytes;
      device_->GetCopyableFootprints(&d, 0, levels * array_size, 0, footprints.data(), nullptr, nullptr, &bytes);
      // Convert straight into the fenced upload page; every texel of every
      // footprint row is written below, padding is never sampled.
      uint8_t* linear;
      auto upload = Reserve(size_t(bytes), linear);
      const bool raw = !depth_texture && !rgb10_signed && !rgba4 && !integer16;
      const unsigned block_log2 = std::countr_zero(block);
      for (unsigned level = 0; level <= max_level; ++level) {
        unsigned stored_level = std::min(level, layout.packed_level);
        const auto& gl = level ? layout.mips[stored_level] : layout.base;
        uint32_t address = level ? (mip << 12) + layout.mip_offsets_bytes[stored_level] : base << 12;
        unsigned ox = 0, oy = 0, oz = 0;
        if (f.packed_mips) texture_util::GetPackedMipOffset(wm + 1, hm + 1, volume ? dm + 1 : 1, f.format, level, ox, oy, oz);
        unsigned bx = (std::max(1u, (wm + 1) >> level) + fi->block_width - 1) / fi->block_width;
        unsigned by = (std::max(1u, (hm + 1) >> level) + fi->block_height - 1) / fi->block_height;
        const std::span<const uint8_t> source = level ? std::span<const uint8_t>(snapshot.mips) : std::span<const uint8_t>(snapshot.base);
        const uint32_t pitch_blocks = gl.row_pitch_bytes / block;
        for (unsigned slice = 0; slice < array_size; ++slice) {
        const size_t level_offset = size_t(address - (level ? mip : base) * 4096u) + size_t(slice) * gl.array_slice_stride_bytes;
        const auto& footprint = footprints[level + slice * levels];
        for (unsigned z = 0; z < (host_volume ? std::max(1u, (dm + 1) >> level) : 1u); ++z)
        for (unsigned y = 0; y < by; ++y) {
          uint8_t* row = linear + footprint.Offset + (uint64_t(z) * by + y) * footprint.Footprint.RowPitch;
          const uint32_t linear_row = ((z + oz) * gl.z_slice_stride_block_rows + y + oy) * gl.row_pitch_bytes + ox * block;
          // Whole linear rows of 32/64/128-bit blocks are word aligned; the
          // per-word endian swap equals the per-byte address XOR below.
          if (raw && !f.tiled && block >= 4 && !((level_offset + linear_row) & 3)) {
            ReadTextureBlock(source, level_offset + linear_row, bx * block, f.endianness, row);
            continue;
          }
          if (raw && f.tiled && !volume) {
            const unsigned run = TiledContiguousBlocks(block_log2);
            for (unsigned x = 0; x < bx;) {
              const unsigned n = std::min(run - ((x + ox) & (run - 1)), bx - x);
              const uint32_t offset = texture_util::GetTiledOffset2D(x + ox, y + oy, pitch_blocks, block_log2);
              ReadTextureBlock(source, level_offset + offset, n * block, f.endianness, row + size_t(x) * block);
              x += n;
            }
            continue;
          }
          for (unsigned x = 0; x < bx; ++x) {
          uint32_t offset = f.tiled ? (volume ? uint32_t(texture_util::GetTiledOffset3D(x + ox, y + oy, z + oz,
              pitch_blocks, gl.z_slice_stride_block_rows, block_log2)) :
              texture_util::GetTiledOffset2D(x + ox, y + oy, pitch_blocks, block_log2)) :
              linear_row + x * block;
          uint8_t* dest = row + size_t(x) * host_block;
          if (raw) { ReadTextureBlock(source, level_offset + offset, block, f.endianness, dest); continue; }
          uint8_t texel[16];
          ReadTextureBlock(source, level_offset + offset, block, f.endianness, texel);
          if (depth_texture) {
            uint32_t bits; std::memcpy(&bits, texel, 4);
            const float value = DecodeDepthTexture(bits, f.format == TextureFormat::k_24_8_FLOAT);
            std::memcpy(dest, &value, 4);
          } else if (rgb10_signed) {
            uint32_t bits; std::memcpy(&bits, texel, 4);
            float values[]{PackedSnorm(bits & 1023, 10), PackedSnorm((bits >> 10) & 1023, 10),
                PackedSnorm((bits >> 20) & 1023, 10), PackedSnorm(bits >> 30, 2)};
            std::memcpy(dest, values, sizeof(values));
          } else if (rgba4) {
            uint16_t bits; std::memcpy(&bits, texel, 2);
            if (binding.is_signed) {
              float values[4];
              for (unsigned c = 0; c < 4; ++c) values[c] = Rgba4Snorm((bits >> (c * 4)) & 15);
              std::memcpy(dest, values, sizeof(values));
            } else {
              uint32_t expanded = ExpandRgba4(bits);
              std::memcpy(dest, &expanded, 4);
            }
          } else {
            uint16_t bits; std::memcpy(&bits, texel, 2);
            float value = binding.is_signed ? float(int16_t(bits)) : float(bits);
            std::memcpy(dest, &value, 4);
          }
          }
        }
        }
      }
      if (stats_enabled_) { ++texture_uploads_; texture_upload_bytes_ += bytes; }
      for (unsigned subresource = 0; subresource < levels * array_size; ++subresource) {
        D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = upload.resource.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = footprints[subresource];
        src.PlacedFootprint.Offset += upload.offset;
        D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = texture.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dst.SubresourceIndex = subresource;
        list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
      }
      Transition(texture.Get(), state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      device_->CreateShaderResourceView(texture.Get(), &view, Cpu(first + i)); transient_.push_back(texture);
      if (entry) {
        entry->source = std::move(snapshot);
        entry->epoch = guest_epoch_;
      } else if (texture_cache_enabled_) {
        const uint64_t cost = snapshot.base.size() + snapshot.mips.size() + bytes;
        if (cost <= kTextureCacheBudget) {
          // Least recently bound first; in-flight users hold their reference.
          while (!textures_.empty() && (textures_.size() >= kTextureCacheEntries || texture_cache_bytes_ + cost > kTextureCacheBudget)) {
            auto oldest = std::min_element(textures_.begin(), textures_.end(),
                [](const auto& a, const auto& b) { return a.second.last_use < b.second.last_use; });
            texture_cache_bytes_ -= oldest->second.bytes;
            textures_.erase(oldest);
          }
          textures_.emplace(std::move(cache_key), CachedTexture{std::move(snapshot), texture, cost, ++texture_use_, guest_epoch_});
          texture_cache_bytes_ += cost;
        }
      }
    }
    list_->SetGraphicsRootDescriptorTable(root, Gpu(first));
  }
  const auto& samplers = shader.GetSamplerBindingsAfterTranslation();
  if (!samplers.empty()) {
    auto first = Samplers(uint32_t(samplers.size()));
    for (unsigned i = 0; i < samplers.size(); ++i) {
      const auto& b = samplers[i]; auto f = r.GetTextureFetch(b.fetch_constant);
      auto mag = b.mag_filter == TextureFilter::kUseFetchConst ? f.mag_filter : b.mag_filter;
      auto min = b.min_filter == TextureFilter::kUseFetchConst ? f.min_filter : b.min_filter;
      auto mip = b.mip_filter == TextureFilter::kUseFetchConst ? f.mip_filter : b.mip_filter;
      // Mirrors the pinned D3D12 GetSamplerParameters/WriteSampler.
      D3D12_SAMPLER_DESC s{};
      s.Filter = D3D12_ENCODE_BASIC_FILTER(min == TextureFilter::kLinear ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT,
          mag == TextureFilter::kLinear ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT,
          mip == TextureFilter::kLinear ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT, D3D12_FILTER_REDUCTION_TYPE_STANDARD);
      ClampMode cx, cy, cz;
      texture_util::GetClampModesForDimension(f, cx, cy, cz);
      s.AddressU = Clamp(cx); s.AddressV = Clamp(cy); s.AddressW = Clamp(cz);
      auto aniso = std::min(b.aniso_filter == AnisoFilter::kUseFetchConst ? f.aniso_filter : b.aniso_filter, AnisoFilter::kMax_16_1);
      s.MaxAnisotropy = 1;
      if (aniso != AnisoFilter::kDisabled) { s.Filter = D3D12_FILTER_ANISOTROPIC; s.MaxAnisotropy = 1u << (uint32_t(aniso) - 1); }
      s.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
      uint32_t lo, hi;
      texture_util::GetSubresourcesFromFetchConstant(f, nullptr, nullptr, nullptr, nullptr, nullptr, &lo, &hi);
      // The highest level is bounded by the bound resource itself.
      s.MinLOD = float(lo); s.MaxLOD = mip == TextureFilter::kBaseMap ? s.MinLOD + (aniso == AnisoFilter::kDisabled ? 0.25f : 0.0f) : FLT_MAX;
      const bool border = s.AddressU == D3D12_TEXTURE_ADDRESS_MODE_BORDER || s.AddressV == D3D12_TEXTURE_ADDRESS_MODE_BORDER ||
          s.AddressW == D3D12_TEXTURE_ADDRESS_MODE_BORDER;
      switch(border ? f.border_color : BorderColor::k_ABGR_Black) {
        case BorderColor::k_ABGR_White: for (float& c : s.BorderColor) c = 1; break;
        case BorderColor::k_ACBYCR_Black: s.BorderColor[0] = s.BorderColor[2] = 0.5f; break;
        case BorderColor::k_ACBCRY_Black: s.BorderColor[1] = s.BorderColor[2] = 0.5f; break;
        default: break;
      }
      device_->CreateSampler(&s, Cpu(first + i, true));
    }
    list_->SetGraphicsRootDescriptorTable(sampler_root, Gpu(first, true));
  }
}
}
