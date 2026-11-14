// Differential report conversion, not GPU query execution. The reference
// header is unmodified Canary 4f04bc7, blob 51aab4909e6239c76247978c8c82c16b1387f693.
#include <rex/graphics/xenos_zpd_report.h>
#include <iostream>

namespace xe::gpu { namespace xenos = rex::graphics::xenos; }
#include "fixtures/canary_zpd_report.h"

int main() {
  using Report = rex::graphics::XenosZPDReport;
  using Reference = xe::gpu::XenosZPDReport;
  using Guest = rex::graphics::xenos::xe_gpu_depth_sample_counts;
  unsigned checks = 0, failures = 0;
  auto compare = [&](const Report& actual, const Reference& expected) {
    ++checks;
    Guest a, b;
    std::memset(&a, 0xCD, sizeof(a));
    std::memset(&b, 0xCD, sizeof(b));
    actual.WriteTo(&a);
    expected.WriteTo(&b);
    if (actual.z_fail != expected.z_fail || actual.z_pass != expected.z_pass ||
        actual.stencil_fail != expected.stencil_fail || actual.total() != expected.total() ||
        std::memcmp(&a, &b, sizeof(a))) {
      ++failures;
      std::cerr << "FAIL ZPD reference comparison " << checks << '\n';
    }
  };
  constexpr uint64_t counts[] = {0, 1, 2, 3, 7, 0xFFFFFF, 0x1000000,
      0xFFFFFFFF, 0x100000000, 0x100000001, 0x1FFFFFFFF, UINT64_MAX};
  constexpr uint32_t scales[] = {0, 1, 2, 3, 4, 6, 9, 16};
  Report running;
  Reference reference_running;
  for (uint64_t passed : counts) {
    compare(Report::FromNativeQuery(passed), Reference::FromNativeQuery(passed));
    for (uint64_t coverage : counts) {
      const auto actual = Report::FromNativeQueryAndTotal(passed, coverage);
      const auto expected = Reference::FromNativeQueryAndTotal(passed, coverage);
      for (uint32_t scale : scales) compare(actual.Normalized(scale), expected.Normalized(scale));
      running += actual;
      reference_running += expected;
      compare(running, reference_running);
      const uint32_t slot[] = {uint32_t(coverage), uint32_t(coverage >> 1),
                              uint32_t(passed), uint32_t(passed >> 1)};
      const auto counter = Report::FromCounterSlot(slot);
      const auto reference_counter = Reference::FromCounterSlot(slot);
      for (uint32_t scale : scales) compare(counter.Normalized(scale), reference_counter.Normalized(scale));
    }
  }
  // These also diagnose the legacy writers: saturation, one A lane, and rounding
  // a nonzero supersampled result down to zero each violate the reference.
  Guest wrapped{};
  Report::FromNativeQuery(0x100000003).WriteTo(&wrapped);
  ++checks;
  if (uint32_t(wrapped.ZPass_A) != 2 || uint32_t(wrapped.ZPass_B) != 1 ||
      uint32_t(wrapped.Total_A) != 2 || uint32_t(wrapped.Total_B) != 1 ||
      Report::FromNativeQuery(1).Normalized(16).z_pass != 1) ++failures;
  std::cout << (checks - failures) << '/' << checks << " ZPD report comparisons passed\n";
  return failures ? 1 : 0;
}
