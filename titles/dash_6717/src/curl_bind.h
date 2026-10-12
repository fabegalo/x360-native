// Retail 6717: the table dash.xex hands to xam ordinal 0xAE6. See curl_bind.cpp.
#pragma once

#include <cstdint>

namespace dash6717 {

struct CurlBinding {
  static constexpr uint32_t kWords = 8;  // seven function pointers, then 0x18
  uint32_t table = 0;                    // guest address passed in r3
  uint32_t words[kWords] = {};           // as read at the call
  uint32_t calls = 0;
};

// The last binding, for diagnostics and tests.
const CurlBinding& LastCurlBinding();

}  // namespace dash6717
