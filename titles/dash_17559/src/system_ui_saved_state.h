#pragma once
#include <rex/system/xmemory.h>
#include <cstdint>
#include <span>

namespace dash17559 {
// Sign-in 17559 90106EC0 duplicates four 34-byte strings into its serialized
// 956-byte state, at 252 + 232*i. Restore 90106CF8 consumes/frees them. When
// UnloadStack discards a suspended app, no restore will consume that ownership.
inline void ReleaseDiscardedSystemUiState(rex::memory::Memory& memory,
                                         uint32_t message,
                                         std::span<const uint8_t> state) {
  if (message != 0x21001 || state.size() != 956) return;
  for (uint32_t i = 0; i < 4; ++i) {
    const auto pointer = rex::memory::load_and_swap<uint32_t>(state.data() + 252 + 232 * i);
    if (pointer) memory.SystemHeapFree(pointer);
  }
}
}
