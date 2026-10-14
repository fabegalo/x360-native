#include "d3d12_render.h"
#include "texture.h"
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/logging.h>
#include <stdexcept>
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
    case TextureFormat::k_32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
    case TextureFormat::k_DXT1: return DXGI_FORMAT_BC1_UNORM;
    case TextureFormat::k_DXT2_3: return DXGI_FORMAT_BC2_UNORM;
    case TextureFormat::k_DXT4_5: return DXGI_FORMAT_BC3_UNORM;
    case TextureFormat::k_16_16_FLOAT: return DXGI_FORMAT_R16G16_FLOAT;
    case TextureFormat::k_16_16_16_16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: throw std::runtime_error("Native texture format " + std::to_string(uint32_t(f)) + " not implemented");
  }
}
D3D12_TEXTURE_ADDRESS_MODE Clamp(ClampMode c) {
  switch(c) { case ClampMode::kRepeat: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    case ClampMode::kMirroredRepeat: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    case ClampMode::kClampToEdge: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case ClampMode::kMirrorClampToEdge: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
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
      Require(f.type == FetchConstantType::kTexture && f.dimension == DataDimension::k2DOrStacked && !f.stacked, "requires 2D texture");
      Require(!f.border_size, "border texture storage not implemented");
      uint32_t wm, hm, dm, base, mip, min_level, max_level;
      texture_util::GetSubresourcesFromFetchConstant(f, &wm, &hm, &dm, &base, &mip, &min_level, &max_level);
      Require(base != 0, "mip-only texture not implemented");
      auto format = Format(f.format, binding.is_signed); auto fi = FormatInfo::Get(f.format);
      unsigned block = fi->bytes_per_block();
      const bool integer16 = f.num_format && f.format == TextureFormat::k_16;
      const bool rgba4 = f.format == TextureFormat::k_4_4_4_4;
      const bool rgb10_signed = GetBaseFormat(f.format) == TextureFormat::k_2_10_10_10 && binding.is_signed;
      Require(!f.num_format || integer16, "integer texture format not implemented");
      if (integer16) format = DXGI_FORMAT_R32_FLOAT;
      unsigned host_block = (rgba4 && binding.is_signed) || rgb10_signed ? 16 : integer16 || rgba4 ? 4 : block;
      auto layout = texture_util::GetGuestTextureLayout(f.dimension, f.pitch, wm + 1, hm + 1, 1, f.tiled, f.format, f.packed_mips, true, max_level);
      const uint32_t base_bytes = (layout.base.level_data_extent_bytes + 3u) & ~3u;
      const uint32_t mip_bytes = max_level ? (layout.mips_total_extent_bytes + 3u) & ~3u : 0;
      Require(uint64_t(base << 12) + base_bytes <= 0x20000000 &&
          uint64_t(mip << 12) + mip_bytes <= 0x20000000, "texture source extent overflow");
      std::span<const uint8_t> base_source(memory.TranslatePhysical<const uint8_t*>(base << 12), base_bytes);
      std::span<const uint8_t> mip_source;
      if (mip_bytes) mip_source = {memory.TranslatePhysical<const uint8_t*>(mip << 12), mip_bytes};
      std::string cache_key(reinterpret_cast<const char*>(&f), sizeof(f));
      cache_key.push_back(char(binding.is_signed));
      cache_key.push_back(char(binding.dimension));
      if (texture_cache_enabled_) {
        auto found = textures_.find(cache_key);
        if (found != textures_.end()) {
          if (found->second.source.Matches(base_source, mip_source)) {
            device_->CreateShaderResourceView(found->second.resource.Get(), &found->second.view, Cpu(first + i));
            transient_.push_back(found->second.resource); // Keep alive through in-flight eviction.
            if (stats_enabled_) ++texture_cache_hits_;
            continue;
          }
          texture_cache_bytes_ -= found->second.bytes;
          textures_.erase(found);
        }
      }
      TextureSnapshot snapshot;
      snapshot.Assign(base_source, mip_source);
      D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = wm + 1; d.Height = hm + 1;
      d.DepthOrArraySize = 1; d.MipLevels = UINT16(max_level + 1); d.Format = format; d.SampleDesc.Count = 1;
      D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
      Ptr<ID3D12Resource> texture;
      Require(SUCCEEDED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture))), "create resource");
      std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(max_level + 1);
      uint64_t bytes;
      device_->GetCopyableFootprints(&d, 0, max_level + 1, 0, footprints.data(), nullptr, nullptr, &bytes);
      std::vector<uint8_t> linear(size_t(bytes), 0);
      for (unsigned level = 0; level <= max_level; ++level) {
        unsigned stored_level = std::min(level, layout.packed_level);
        const auto& gl = level ? layout.mips[stored_level] : layout.base;
        uint32_t address = level ? (mip << 12) + layout.mip_offsets_bytes[stored_level] : base << 12;
        unsigned ox = 0, oy = 0, oz = 0;
        if (f.packed_mips) texture_util::GetPackedMipOffset(wm + 1, hm + 1, 1, f.format, level, ox, oy, oz);
        unsigned bx = (std::max(1u, (wm + 1) >> level) + fi->block_width - 1) / fi->block_width;
        unsigned by = (std::max(1u, (hm + 1) >> level) + fi->block_height - 1) / fi->block_height;
        for (unsigned y = 0; y < by; ++y) for (unsigned x = 0; x < bx; ++x) {
          uint32_t offset = f.tiled ? texture_util::GetTiledOffset2D(x + ox, y + oy, gl.row_pitch_bytes / block, std::countr_zero(block)) : (y + oy) * gl.row_pitch_bytes + (x + ox) * block;
          auto dest = footprints[level].Offset + y * footprints[level].Footprint.RowPitch + x * host_block;
          Require(uint64_t(address) + offset + block <= 0x20000000, "guest extent overflow");
          uint8_t texel[16]{};
          for (unsigned b = 0; b < block; ++b) {
            unsigned byte = offset + b;
            switch(f.endianness) { case Endian::k8in16: byte ^= 1; break; case Endian::k8in32: byte ^= 3; break; case Endian::k16in32: byte ^= 2; break; default: break; }
            const auto& source = level ? snapshot.mips : snapshot.base;
            const auto source_offset = uint64_t(address - (level ? mip : base) * 4096u) + byte;
            Require(source_offset < source.size(), "texture snapshot extent overflow");
            texel[b] = source[size_t(source_offset)];
          }
          if (rgb10_signed) {
            uint32_t bits; std::memcpy(&bits, texel, 4);
            float values[]{PackedSnorm(bits & 1023, 10), PackedSnorm((bits >> 10) & 1023, 10),
                PackedSnorm((bits >> 20) & 1023, 10), PackedSnorm(bits >> 30, 2)};
            std::memcpy(linear.data() + size_t(dest), values, sizeof(values));
          } else if (rgba4) {
            uint16_t bits; std::memcpy(&bits, texel, 2);
            if (binding.is_signed) {
              float values[4];
              for (unsigned c = 0; c < 4; ++c) values[c] = Rgba4Snorm((bits >> (c * 4)) & 15);
              std::memcpy(linear.data() + size_t(dest), values, sizeof(values));
            } else {
              uint32_t expanded = ExpandRgba4(bits);
              std::memcpy(linear.data() + size_t(dest), &expanded, 4);
            }
          } else if (integer16) {
            uint16_t bits; std::memcpy(&bits, texel, 2);
            float value = binding.is_signed ? float(int16_t(bits)) : float(bits);
            std::memcpy(linear.data() + size_t(dest), &value, 4);
          } else std::memcpy(linear.data() + size_t(dest), texel, block);
        }
      }
      auto upload = Upload(linear.data(), linear.size());
      if (stats_enabled_) { ++texture_uploads_; texture_upload_bytes_ += linear.size(); }
      for (unsigned level = 0; level <= max_level; ++level) {
        D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = upload.resource.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = footprints[level];
        src.PlacedFootprint.Offset += upload.offset;
        D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = texture.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dst.SubresourceIndex = level;
        list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
      }
      auto state = D3D12_RESOURCE_STATE_COPY_DEST;
      Transition(texture.Get(), state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      D3D12_SHADER_RESOURCE_VIEW_DESC view{}; view.Format = format;
      Require(binding.dimension == FetchOpDimension::k2D, "texture dimensionality not implemented");
      view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY; view.Texture2DArray.MipLevels = max_level + 1; view.Texture2DArray.ArraySize = 1;
      unsigned components = 4;
      switch (GetBaseFormat(f.format)) {
        case TextureFormat::k_8: case TextureFormat::k_16: case TextureFormat::k_16_FLOAT: case TextureFormat::k_32_FLOAT: components = 1; break;
        case TextureFormat::k_8_8: case TextureFormat::k_16_16: case TextureFormat::k_16_16_FLOAT: components = 2; break;
        default: break;
      }
      auto component = [&](unsigned channel) { unsigned s = (f.swizzle >> (channel * 3)) & 7; Require(s <= 5, "reserved swizzle component"); return s < 4 ? std::min(s, components - 1) : s; };
      view.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(component(0), component(1), component(2), component(3));
      device_->CreateShaderResourceView(texture.Get(), &view, Cpu(first + i)); transient_.push_back(texture);
      if (texture_cache_enabled_) {
        const uint64_t cost = snapshot.base.size() + snapshot.mips.size() + bytes;
        constexpr uint64_t budget = 256ull * 1024 * 1024;
        if (cost <= budget) {
          if (textures_.size() >= 256 || texture_cache_bytes_ + cost > budget) {
            textures_.clear(); texture_cache_bytes_ = 0;
          }
          textures_.emplace(std::move(cache_key), CachedTexture{std::move(snapshot), texture, view, cost});
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
      D3D12_SAMPLER_DESC s{};
      s.Filter = D3D12_ENCODE_BASIC_FILTER(min == TextureFilter::kLinear ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT,
          mag == TextureFilter::kLinear ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT,
          mip == TextureFilter::kLinear ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT, D3D12_FILTER_REDUCTION_TYPE_STANDARD);
      s.AddressU = Clamp(f.clamp_x); s.AddressV = Clamp(f.clamp_y); s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
      auto aniso = b.aniso_filter == AnisoFilter::kUseFetchConst ? f.aniso_filter : b.aniso_filter;
      Require(aniso <= AnisoFilter::kMax_16_1, "invalid anisotropic filter");
      s.MaxAnisotropy = 1;
      if (aniso != AnisoFilter::kDisabled) { s.Filter = D3D12_FILTER_ANISOTROPIC; s.MaxAnisotropy = 1u << (uint32_t(aniso) - 1); }
      s.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
      uint32_t lo, hi;
      texture_util::GetSubresourcesFromFetchConstant(f, nullptr, nullptr, nullptr, nullptr, nullptr, &lo, &hi);
      s.MinLOD = float(lo); s.MaxLOD = mip == TextureFilter::kBaseMap ? s.MinLOD + (aniso == AnisoFilter::kDisabled ? 0.25f : 0.0f) : float(hi);
      switch(f.border_color) {
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
