#pragma once
#include <rex/graphics/xenos.h>
#include <span>
#include <vector>
#include "../textures/texture_block.h"
namespace x360n::native {
inline std::vector<uint32_t> ExpandIndices(std::span<const uint8_t> source, uint32_t count,
    rex::graphics::xenos::IndexFormat format, rex::graphics::xenos::Endian endian) {
  using namespace rex::graphics::xenos;
  if (!count) return {};
  const unsigned bytes = format == IndexFormat::kInt16 ? 2 : 4;
  // R16 word endian modes also exchange adjacent indices, including the
  // unused partner of an odd final index. Check that extent before reading.
  const unsigned pair_xor = endian == Endian::k8in32 || endian == Endian::k16in32 ? 2 : 0;
  const uint64_t needed = uint64_t(count) * bytes + (bytes == 2 && pair_xor && (count & 1) ? 2 : 0);
  if (needed > source.size()) throw std::runtime_error("Native index endianness crosses buffer extent");
  std::vector<uint32_t> result(count);
  if (bytes == 4) {
    ReadTextureBlock(source, 0, count * 4, endian, reinterpret_cast<uint8_t*>(result.data()));
  } else {
    const bool swap = endian == Endian::k8in16 || endian == Endian::k8in32;
    for (uint32_t i = 0; i < count; ++i) {
      uint16_t value;
      std::memcpy(&value, source.data() + ((size_t(i) * 2) ^ pair_xor), 2);
      result[i] = swap ? GpuSwap(value, Endian::k8in16) : value;
    }
  }
  return result;
}
struct IndexRange { uint32_t offset, count; };
struct ConvertedIndices { std::vector<uint32_t> values; std::vector<IndexRange> ranges; };
inline bool IsLinePrimitive(rex::graphics::xenos::PrimitiveType type) {
  using P = rex::graphics::xenos::PrimitiveType;
  return type == P::kLineList || type == P::kLineStrip || type == P::kLineLoop || type == P::k2DLineStrip;
}
inline bool NeedsPrimitiveConversion(rex::graphics::xenos::PrimitiveType type) {
  using P = rex::graphics::xenos::PrimitiveType;
  return type == P::kTriangleFan || type == P::kQuadList || type == P::kQuadStrip ||
      type == P::kPolygon || type == P::kLineStrip || type == P::kLineLoop || type == P::k2DLineStrip;
}
inline ConvertedIndices VertexOnlyIndices(std::span<const uint32_t> input,
    rex::graphics::xenos::PrimitiveType type, bool restart, uint32_t reset) {
  using P = rex::graphics::xenos::PrimitiveType;
  restart &= type == P::kTriangleStrip || type == P::kTriangleFan || type == P::kLineStrip ||
             type == P::kLineLoop || type == P::kQuadStrip || type == P::kPolygon ||
             type == P::k2DLineStrip || type == P::k2DTriStrip;
  ConvertedIndices out;
  out.values.reserve(input.size());
  for (auto value : input) if (!restart || (value & 0xFFFFFFu) != reset) out.values.push_back(value);
  if (!out.values.empty()) out.ranges.push_back({0, uint32_t(out.values.size())});
  return out;
}
inline ConvertedIndices ConvertIndices(std::span<const uint32_t> input,
    rex::graphics::xenos::PrimitiveType type, bool restart, uint32_t reset) {
  using P = rex::graphics::xenos::PrimitiveType;
  ConvertedIndices out;
  // Like the pinned Xenos primitive processor, reset applies to strips/fans,
  // not independent list primitives. Compare before VGT index offset/clamp.
  restart &= type == P::kTriangleStrip || type == P::kTriangleFan || type == P::kLineStrip ||
             type == P::kLineLoop || type == P::kQuadStrip || type == P::kPolygon ||
             type == P::k2DLineStrip || type == P::k2DTriStrip;
  auto emit = [&](size_t first, size_t last) {
    const auto start = uint32_t(out.values.size());
    if (type == P::kTriangleFan || type == P::kPolygon) {
      for (size_t i = first + 1; i + 1 < last; ++i)
        for (auto j : {first, i, i + 1}) out.values.push_back(input[j]);
    } else if (type == P::kLineStrip || type == P::k2DLineStrip || type == P::kLineLoop) {
      for (size_t i = first; i + 1 < last; ++i) {
        out.values.push_back(input[i]); out.values.push_back(input[i + 1]);
      }
      if (type == P::kLineLoop && last - first >= 2) {
        out.values.push_back(input[last - 1]); out.values.push_back(input[first]);
      }
    } else if (type == P::kQuadStrip) {
      // Quad strip perimeter is (v0,v1,v3,v2); preserve its shared diagonal
      // and winding when converting each pair of successive edges.
      for (size_t i = first; i + 3 < last; i += 2)
        for (unsigned j : {0u,1u,2u,2u,1u,3u}) out.values.push_back(input[i+j]);
    } else if (type == P::kQuadList) {
      for (size_t i = first; i + 3 < last; i += 4)
        for (unsigned j : {0u,1u,3u,3u,1u,2u}) out.values.push_back(input[i+j]);
    } else out.values.insert(out.values.end(), input.begin()+first, input.begin()+last);
    const auto count = uint32_t(out.values.size()) - start;
    if (count) out.ranges.push_back({start, count});
  };
  size_t first = 0;
  for (size_t i = 0; i < input.size(); ++i) if (restart && (input[i] & 0xFFFFFFu) == reset) {
    emit(first, i); first = i + 1;
  }
  emit(first, input.size());
  return out;
}
}
