#pragma once
#include <rex/graphics/xenos.h>
namespace x360n::native {
struct DrawIndices {
  rex::graphics::xenos::IndexFormat format;
  rex::graphics::xenos::Endian endianness;
  uint32_t count, guest_base, length;
};
}
