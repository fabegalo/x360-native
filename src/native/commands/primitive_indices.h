#pragma once
#include <rex/graphics/xenos.h>
#include <span>
#include <vector>
namespace x360n::native {
struct IndexRange { uint32_t offset, count; };
struct ConvertedIndices { std::vector<uint32_t> values; std::vector<IndexRange> ranges; };
inline ConvertedIndices ConvertIndices(std::span<const uint32_t> input,
    rex::graphics::xenos::PrimitiveType type, bool restart, uint32_t reset) {
  using P = rex::graphics::xenos::PrimitiveType;
  ConvertedIndices out;
  // Like the pinned Xenos primitive processor, reset applies to strips/fans,
  // not independent list primitives. Compare before VGT index offset/clamp.
  restart &= type == P::kTriangleStrip || type == P::kTriangleFan;
  auto emit = [&](size_t first, size_t last) {
    const auto start = uint32_t(out.values.size());
    if (type == P::kTriangleFan) {
      for (size_t i = first + 1; i + 1 < last; ++i)
        for (auto j : {first, i, i + 1}) out.values.push_back(input[j]);
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
