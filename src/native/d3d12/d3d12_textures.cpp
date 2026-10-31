#include "d3d12_render.h"
#include "../quality/settings.h"
#include "../textures/texture.h"
#include "../textures/texture_block.h"
#include "../textures/texture_load.h"
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/logging.h>
#include <xxhash.h>
#include <cstring>
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
    case TextureFormat::k_32_32_FLOAT: return DXGI_FORMAT_R32G32_FLOAT;
    case TextureFormat::k_32_32_32_32_FLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
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
D3D12Render::TextureTables D3D12Render::BindTextures(const RegisterFile& r, rex::memory::Memory& memory, DxbcShader& shader) {
  Timer timer(stats_enabled_, texture_ns_);
  TextureTables tables;
  const auto& textures = shader.GetTextureBindingsAfterTranslation();
  const auto& samplers = shader.GetSamplerBindingsAfterTranslation();
  size_t index_count = 4;
  // Translator indices are not zero-based dense binding ordinals. Include the
  // greatest assigned index, including sampler and signed-view aliases.
  for (const auto& b : textures) index_count = std::max(index_count, size_t(b.bindless_descriptor_index) + 1);
  for (const auto& b : samplers) index_count = std::max(index_count, size_t(b.bindless_descriptor_index) + 1);
  std::vector<uint32_t> indices(index_count);
  if (!textures.empty()) {
    auto first = Views(uint32_t(textures.size()));
    for (unsigned i = 0; i < textures.size(); ++i) {
      const auto& binding = textures[i]; auto f = r.GetTextureFetch(binding.fetch_constant);
      indices.at(binding.bindless_descriptor_index) = first + i;
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
      if (!valid || !TextureViewActive(f, binding.dimension, binding.is_signed)) {
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
      const bool volume = f.dimension == DataDimension::k3D;
      const bool host_volume = volume && binding.dimension == FetchOpDimension::k3DOrStacked;
      // The translator also declares a 2D alternative for stacked fetches.
      // A 2D fetch of volume storage views its first base-level slice.
      if (volume && !host_volume) max_level = base ? 0 : min_level;
      auto format = (ExpandedTextureBlock(f.format) || ExpandedTextureComponents(f.format, f.num_format)) ? DXGI_FORMAT_R32G32B32A32_FLOAT : Format(f.format, binding.is_signed);
      const bool depth_texture = f.format == TextureFormat::k_24_8 || f.format == TextureFormat::k_24_8_FLOAT;
      // Cube faces and stacked 2D slices are array slices of every level.
      const bool cube = f.dimension == DataDimension::kCube;
      const unsigned array_size = cube || (f.dimension == DataDimension::k2DOrStacked && f.stacked) ? dm + 1 : 1;
      const unsigned levels = max_level + 1;
      TextureLoadInput input{f.format, f.dimension, f.endianness, bool(f.tiled), bool(f.packed_mips), wm + 1, hm + 1, dm + 1,
          f.pitch, base << 12, mip << 12, max_level, host_volume, array_size, format, bool(binding.is_signed), bool(f.num_format)};
      const auto layout = input.Layout();
      const uint32_t base_bytes = (layout.base.level_data_extent_bytes + 3u) & ~3u;
      const uint32_t mip_bytes = max_level ? (layout.mips_total_extent_bytes + 3u) & ~3u : 0;
      Require(uint64_t(base << 12) + base_bytes <= 0x20000000 &&
          uint64_t(mip << 12) + mip_bytes <= 0x20000000, "texture source extent overflow");
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
        case TextureFormat::k_DXN: case TextureFormat::k_8_8: case TextureFormat::k_16_16: case TextureFormat::k_16_16_FLOAT: case TextureFormat::k_32_32_FLOAT: components = 2; break;
        default: break;
      }
      auto component = [&](unsigned channel) { unsigned s = (f.swizzle >> (channel * 3)) & 7; Require(s <= 5, "reserved swizzle component"); return s < 4 ? std::min(s, components - 1) : s; };
      view.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(component(0), component(1), component(2), component(3));
      const auto* scale_format = FormatInfo::Get(input.format);
      const bool scaled_texture = scaled_.enabled() && !input.host_volume &&
          scale_format->block_width == 1 && scale_format->block_height == 1 &&
          (scaled_.Overlaps(input.base, base_bytes) || scaled_.Overlaps(input.mip, mip_bytes));
      if (scaled_texture) { input.scale_x=scale_x_; input.scale_y=scale_y_; input.scaled=&scaled_;
        textures_resolution_scaled_ |= uint32_t(1) << binding.fetch_constant; }
      // Content identity: every field that selects or converts guest bytes.
      // Sampler-only fetch fields (filters, clamps, LOD, swizzle) share it.
      const uint32_t key_fields[]{input.scale_x, input.scale_y, base, mip, wm, hm, dm, levels, f.pitch, uint32_t(f.format),
          uint32_t(f.tiled) | uint32_t(f.packed_mips) << 1 | uint32_t(f.endianness) << 2 | uint32_t(f.dimension) << 4 |
          uint32_t(f.stacked) << 6 | uint32_t(binding.is_signed) << 7 | uint32_t(host_volume) << 8 | uint32_t(f.num_format) << 9};
      std::string cache_key(reinterpret_cast<const char*>(key_fields), sizeof(key_fields));
      CachedTexture* entry = nullptr;
      if (texture_cache_enabled_) {
        auto found = textures_.find(cache_key);
        if (found != textures_.end()) {
          entry = &found->second;
          entry->last_use = ++texture_use_;
          const bool current = !std::atomic_ref(entry->outdated).load(std::memory_order_relaxed);
          // Unchanged since loaded: no CPU or GPU write touched its pages.
          if (current) {
            device_->CreateShaderResourceView(entry->resource.Get(), &view, Cpu(first + i));
            transient_.push_back(entry->resource); // Keep alive through in-flight eviction.
            if (stats_enabled_) ++texture_cache_hits_;
            continue;
          }
        }
      }
      D3D12_RESOURCE_DESC d{}; d.Dimension = host_volume ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = uint64_t(wm + 1)*input.scale_x; d.Height = (hm + 1)*input.scale_y;
      d.DepthOrArraySize = UINT16(host_volume ? dm + 1 : array_size); d.MipLevels = UINT16(levels); d.Format = format; d.SampleDesc.Count = 1;
      Require(d.Width <= D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION && d.Height <= D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION, "scaled texture exceeds device dimensions");
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
        if (texture_cache_enabled_) {
          uint64_t cost;
          device_->GetCopyableFootprints(&d, 0, levels * array_size, 0, nullptr, nullptr, nullptr, &cost);
          if (cost <= kTextureCacheBudget) {
            // Least recently bound first; in-flight users hold their reference.
            while (!textures_.empty() && (textures_.size() >= kTextureCacheEntries || texture_cache_bytes_ + cost > kTextureCacheBudget)) {
              auto oldest = std::min_element(textures_.begin(), textures_.end(),
                  [](const auto& a, const auto& b) { return a.second.last_use < b.second.last_use; });
              texture_cache_bytes_ -= oldest->second.bytes;
              Unwatch(oldest->second);
              textures_.erase(oldest);
            }
            entry = &textures_.emplace(std::move(cache_key), CachedTexture{texture, cost, ++texture_use_}).first->second;
            texture_cache_bytes_ += cost;
          }
        }
      }
      if (entry) {
        // Watch before the mirror upload: a CPU write after this point marks
        // the texture outdated again, so no write between upload and use is lost.
        auto lock = GuestMemoryMirror::Lock();
        std::atomic_ref(entry->outdated).store(false, std::memory_order_relaxed);
        if (base_bytes && !entry->watches[0]) entry->watches[0] = mirror_->Watch(base << 12, base_bytes, TextureWatch, this, entry, 0);
        if (mip_bytes && !entry->watches[1]) entry->watches[1] = mirror_->Watch(mip << 12, mip_bytes, TextureWatch, this, entry, 1);
      }
      // Pages the CPU wrote are uploaded; resolve results are already resident.
      mirror_->RequestRange(base << 12, base_bytes);
      if (mip_bytes) mirror_->RequestRange(mip << 12, mip_bytes);
      const auto shader_index = (texture_cpu_ || scaled_texture) ? LoadShader::kUnknown : GpuLoadShader(input);
      if (shader_index != LoadShader::kUnknown) {
        LoadTextureGpu(PlanGpuTextureLoad(input, shader_index), texture.Get());
        if (stats_enabled_) { ++gpu_texture_loads_; ++texture_uploads_; }
      } else {
        // Draw synchronized every CPU-converted range with incomplete resolves first.
        Require(!GuestWritesOverlap(base << 12, base_bytes) && (!mip_bytes || !GuestWritesOverlap(mip << 12, mip_bytes)),
            "texture read overlaps an incomplete resolve");
        std::span<const uint8_t> base_source(memory.TranslatePhysical<const uint8_t*>(base << 12), base_bytes);
        std::span<const uint8_t> mip_source;
        if (mip_bytes) mip_source = {memory.TranslatePhysical<const uint8_t*>(mip << 12), mip_bytes};
        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(levels * array_size);
        uint64_t bytes;
        device_->GetCopyableFootprints(&d, 0, levels * array_size, 0, footprints.data(), nullptr, nullptr, &bytes);
        // Convert straight into the fenced upload page; every texel of every
        // footprint row is written, padding is never sampled.
        uint8_t* linear;
        auto upload = Reserve(size_t(bytes), linear);
        ConvertTextureCpu(input, base_source, mip_source, footprints, linear);
        for (unsigned subresource = 0; subresource < levels * array_size; ++subresource) {
          if (!base && subresource % levels == 0) continue;
          D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = upload.resource.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = footprints[subresource];
          src.PlacedFootprint.Offset += upload.offset;
          D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = texture.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dst.SubresourceIndex = subresource;
          cmd_.CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        }
        if (stats_enabled_) { ++cpu_texture_loads_; ++texture_uploads_; texture_upload_bytes_ += bytes; }
      }
      Transition(texture.Get(), state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      device_->CreateShaderResourceView(texture.Get(), &view, Cpu(first + i)); transient_.push_back(texture);
    }
  }
  if (!samplers.empty()) {
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
      auto aniso = NativeAnisotropy(std::min(b.aniso_filter == AnisoFilter::kUseFetchConst ? f.aniso_filter : b.aniso_filter, AnisoFilter::kMax_16_1));
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
      indices.at(b.bindless_descriptor_index) = Sampler(s);
    }
  }
  tables.indices = Constants(indices.data(), indices.size() * sizeof(uint32_t));
  return tables;
}
static_assert(sizeof(D3D12_SAMPLER_DESC) == 13 * 4, "sampler key covers every byte of the description");
size_t D3D12Render::SamplerKeyHash::operator()(const SamplerKey& key) const {
  return size_t(XXH3_64bits(key.data(), sizeof(key)));
}
uint32_t D3D12Render::Sampler(const D3D12_SAMPLER_DESC& s) {
  // A descriptor written earlier in this arena with identical bytes samples
  // identically and lives until the same fence; reuse its index.
  SamplerKey key; std::memcpy(key.data(), &s, sizeof(s));
  auto [it, inserted] = arena_samplers_.try_emplace(key, 0);
  if (!inserted) { if (stats_enabled_) ++sampler_reuses_; return it->second; }
  it->second = Samplers(1);
  device_->CreateSampler(&s, Cpu(it->second, true));
  return it->second;
}
void D3D12Render::SetTextureTables(const TextureTables& tables, unsigned root) {
  if (tables.indices) cmd_.SetGraphicsRootConstantBufferView(root, tables.indices);
}
void D3D12Render::TextureWatch(void*, void* data, uint64_t argument, bool) {
  // Under GuestMemoryMirror::Lock(); the fired watch is already retired.
  auto* entry = static_cast<CachedTexture*>(data);
  std::atomic_ref(entry->outdated).store(true, std::memory_order_relaxed);
  entry->watches[argument] = nullptr;
}
void D3D12Render::Unwatch(CachedTexture& entry) {
  auto lock = GuestMemoryMirror::Lock();
  for (auto& watch : entry.watches) { mirror_->Unwatch(watch); watch = nullptr; }
}
void D3D12Render::LoadTextureGpu(const TextureLoadPlan& plan, ID3D12Resource* texture) {
  if (!load_root_) load_root_ = CreateTextureLoadRoot(device_);
  auto& pipeline = load_pipelines_[size_t(plan.shader)];
  if (!pipeline.valid()) {
    const auto code = TextureLoadBytecode(plan.shader);
    pipeline = ComputePSO(load_root_.Get(), code.pShaderBytecode, code.BytecodeLength);
  }
  // Scratch regions live until the submission's fence (reset in Flush).
  const uint64_t need = (plan.scratch_bytes + 65535) & ~uint64_t(65535);
  Scratch* scratch = nullptr;
  for (auto& s : scratch_) if (s.size - s.used >= need) { scratch = &s; break; }
  if (!scratch) {
    const uint64_t size = std::max<uint64_t>(need, 64ull << 20);
    scratch_.push_back({Buffer(size, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
        size, 0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS});
    scratch = &scratch_.back();
  }
  const uint64_t offset = scratch->used; scratch->used += need;
  Transition(scratch->buffer.Get(), scratch->state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  Transition(memory_.Get(), memory_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  const unsigned v = Views(2);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format = UintPow2Format(plan.source_bpe_log2);
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER; srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.NumElements = UINT(0x20000000u >> plan.source_bpe_log2);
  device_->CreateShaderResourceView(memory_.Get(), &srv, Cpu(v));
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav{}; uav.Format = UintPow2Format(plan.dest_bpe_log2); uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = UINT(scratch->size >> plan.dest_bpe_log2);
  device_->CreateUnorderedAccessView(scratch->buffer.Get(), nullptr, &uav, Cpu(v + 1));
  ID3D12DescriptorHeap* heaps[]{views_.Get(), samplers_.Get()}; cmd_.SetDescriptorHeaps(2, heaps);
  cmd_.SetComputeRootSignature(load_root_.Get()); cmd_.SetPipelineState(pipeline);
  cmd_.SetComputeRootDescriptorTable(1, Gpu(v)); cmd_.SetComputeRootDescriptorTable(2, Gpu(v + 1));
  RecordTextureLoad(cmd_, plan, offset, scratch->buffer.Get(), texture);
  scratch->state = D3D12_RESOURCE_STATE_COPY_SOURCE;
}
}
