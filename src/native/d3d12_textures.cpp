#include "d3d12_render.h"
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
      Require(!f.num_format || integer16, "integer texture format not implemented");
      if (integer16) format = DXGI_FORMAT_R32_FLOAT;
      unsigned host_block = integer16 ? 4 : block;
      auto layout = texture_util::GetGuestTextureLayout(f.dimension, f.pitch, wm + 1, hm + 1, 1, f.tiled, f.format, f.packed_mips, true, max_level);
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
            texel[b] = *memory.TranslatePhysical<uint8_t*>(address + byte);
          }
          if (integer16) {
            uint16_t bits; std::memcpy(&bits, texel, 2);
            float value = binding.is_signed ? float(int16_t(bits)) : float(bits);
            std::memcpy(linear.data() + size_t(dest), &value, 4);
          } else std::memcpy(linear.data() + size_t(dest), texel, block);
        }
      }
      auto upload = Upload(linear.data(), linear.size());
      for (unsigned level = 0; level <= max_level; ++level) {
        D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = upload.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = footprints[level];
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
