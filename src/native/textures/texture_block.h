#pragma once
#include <rex/graphics/xenos.h>
#include <span>
#include <cstring>
#include <stdexcept>
#include <algorithm>
namespace x360n::native {
// Texture blocks are naturally aligned. Normalize endian once per word,
// retaining the byte-address XOR rules for sub-word formats.
inline void ReadTextureBlock(std::span<const uint8_t> source, size_t offset,
    unsigned bytes, rex::graphics::xenos::Endian endian, uint8_t* output) {
  using E = rex::graphics::xenos::Endian;
  if (bytes >= 4 && !(offset & 3) && !(bytes & 3)) {
    if (offset > source.size() || bytes > source.size() - offset)
      throw std::runtime_error("Native texture snapshot extent overflow");
    if (endian == E::kNone) { std::memcpy(output, source.data()+offset, bytes); return; }
    for (unsigned b = 0; b < bytes; b += 4) {
      uint32_t word; std::memcpy(&word, source.data()+offset+b, 4);
      word = rex::graphics::xenos::GpuSwap(word, endian);
      std::memcpy(output+b, &word, 4);
    }
  } else {
    const unsigned mask = endian == E::k8in16 ? 1 : endian == E::k8in32 ? 3 : endian == E::k16in32 ? 2 : 0;
    for (unsigned b = 0; b < bytes; ++b) {
      const size_t address = (offset+b) ^ mask;
      if (address >= source.size()) throw std::runtime_error("Native texture snapshot extent overflow");
      output[b] = source[address];
    }
  }
}
// In the 2D tiled layout (texture_util::GetTiledOffset2D) block X bits below
// this run size only form the low 16-byte address bits: such aligned X runs
// are contiguous in guest memory, ascending with X.
constexpr unsigned TiledContiguousBlocks(unsigned bytes_per_block_log2) {
  return bytes_per_block_log2 >= 4 ? 1u : std::min(8u, 16u >> bytes_per_block_log2);
}
}
