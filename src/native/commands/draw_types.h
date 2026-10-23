#pragma once
#include <rex/graphics/xenos.h>
namespace x360n::native {
// Visibility probing runs before the guest pixel shader. A separate probe
// suppresses vertex stores; a query-only guest draw keeps its real VS stores.
enum class DrawPurpose { kGuest, kVisibility, kVisibilityWithVertexExports, kGuestWithoutVertexExports };
struct DrawIndices {
  rex::graphics::xenos::IndexFormat format;
  rex::graphics::xenos::Endian endianness;
  uint32_t count, guest_base, length;
};
}
