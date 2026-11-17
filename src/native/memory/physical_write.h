#pragma once
#include <rex/guest.h>
#include <cstdint>
#include <stdexcept>

namespace x360n::native {
// The runtime publishes the completed bytes to every allocated physical alias.
inline void NotifyPhysicalWrite(rex::IPhysicalMemory& memory, uint32_t address, uint32_t length) {
  const auto result = memory.NotifyPhysicalWrite({address, length});
  if (!result) throw std::runtime_error(result.error().message);
}
}
