#include <catch2/catch_test_macros.hpp>
#include <windows.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <memory>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

extern "C" REX_FUNC(__imp__XamFormatDateString);
extern "C" REX_FUNC(__imp__XamFormatTimeString);

TEST_CASE("XAM date and time preserve FILETIME and support both dashboard ABIs", "[sdk][datetime]") {
  SYSTEMTIME utc{2026, 10, 0, 12, 12, 34, 56, 0}, local{};
  FILETIME ft{};
  REQUIRE(SystemTimeToFileTime(&utc, &ft));
  REQUIRE(SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local));
  const uint64_t filetime = (uint64_t(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
  auto ctx = std::make_unique<PPCContext>();
  for (bool modern : {false, true}) {
    for (bool time : {false, true}) {
      CAPTURE(modern, time);
      char expected[32];
      if (time) std::snprintf(expected, sizeof(expected), "%02u:%02u", local.wHour, local.wMinute);
      else std::snprintf(expected, sizeof(expected), "%02u/%02u/%u", local.wMonth, local.wDay, local.wYear);
      for (uint32_t bytes : {22u, 5u, 0u}) {
        CAPTURE(bytes);
        std::array<uint8_t, 0x2000> memory;
        memory.fill(0xCD);
        if (modern) {
          ctx->r3.u64 = filetime; ctx->r4.u64 = 0x1000;
          ctx->r5.u64 = bytes; ctx->r6.u64 = 0;
        } else {
          ctx->r3.u64 = 1; ctx->r4.u64 = filetime;
          ctx->r5.u64 = 0x1000; ctx->r6.u64 = bytes;
        }
        (time ? __imp__XamFormatTimeString : __imp__XamFormatDateString)(*ctx, memory.data());
        const size_t capacity = bytes / 2;
        if (capacity) {
          const size_t length = std::min(std::strlen(expected), capacity - 1);
          for (size_t i = 0; i < length; ++i) {
            CHECK(memory[0x1000 + i * 2] == 0);
            CHECK(memory[0x1001 + i * 2] == uint8_t(expected[i]));
          }
          CHECK(memory[0x1000 + length * 2] == 0);
          CHECK(memory[0x1001 + length * 2] == 0);
        }
        CHECK(memory[0x1000 + capacity * 2] == 0xCD);
        CHECK(memory[0x1000 + bytes] == 0xCD);
        CHECK(memory[0x0FFF] == 0xCD);
      }
    }
  }
}
