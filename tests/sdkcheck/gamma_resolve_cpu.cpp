#include "gamma_resolve_cases.h"
#include "gamma_resolve_math.h"
#include <iostream>

int main() {
  using namespace gamma_probe;
  unsigned checks = 0, failures = 0;
  auto check = [&](bool ok, const char *name) {
    ++checks;
    if (!ok) {
      ++failures;
      if (failures < 10)
        std::cerr << "FAIL " << name << '\n';
    }
  };
  for (unsigned g = 0; g < 256; ++g)
    check(XePWLGammaToLinearCode(g) == kDecode[g],
          "all gamma bytes and segment boundaries");
  check(kDecode[0] == 0 && kDecode[63] == 63 && kDecode[64] == 64 &&
            kDecode[95] == 126 && kDecode[96] == 128 && kDecode[191] == 509 &&
            kDecode[192] == 516 && kDecode[255] == 1023,
        "reference anchors");
  for (unsigned count : {1u, 2u, 4u})
    for (unsigned mean = 0; mean <= 1023; ++mean)
      for (unsigned remainder = 0; remainder < count; ++remainder) {
        unsigned sum = mean * count + remainder;
        if (sum > 1023 * count)
          continue;
        auto actual = XeResolveAveragePWLGamma(float(sum), 1.f / float(count));
        auto expected = Quantize(sum, count);
        check(std::bit_cast<unsigned>(actual) ==
                  std::bit_cast<unsigned>(expected),
              "all 10-bit averages, fractional boundaries and 24-bit "
              "normalization");
        for (int bias : {-3, 0, 3})
          check(std::ldexp(actual, bias) == std::ldexp(expected, bias),
                "exp bias after gamma quantization");
      }
  // Distinguishes code averaging from averaging encoded bytes and from
  // retaining an unquantized linear mean for a high-precision destination.
  check(Quantize(kDecode[63] + kDecode[192], 2) !=
            float(kDecode[(63 + 192) / 2]) / 1023.f,
        "decode before average");
  check(Quantize(127, 1) == Quantize(126, 1) &&
            Quantize(128, 1) > Quantize(127, 1),
        "requantization at an encoder segment boundary");
  check(Half(0) == 0 && Half(1) == 0x3c00 && Half(.5f) == 0x3800,
        "independent half packing anchors");
  for (unsigned msaa : {0u, 1u, 2u})
    for (unsigned selection = 0; selection < 7; ++selection) {
      auto selected = draw_util::SanitizeCopySampleSelect(
          xenos::CopySampleSelect(selection), xenos::MsaaSamples(msaa), false);
      draw_util::ResolveInfo info{};
      info.color_edram_info.format =
          unsigned(xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA);
      info.color_edram_info.msaa_samples = xenos::MsaaSamples(msaa);
      info.color_edram_info.pitch_tiles = 2;
      info.coordinate_info.width_div_8 = 4;
      info.height_div_8 = 1;
      info.copy_dest_info.copy_dest_format = xenos::ColorFormat::k_8_8_8_8;
      info.copy_dest_coordinate_info.copy_sample_select = selected;
      draw_util::ResolveCopyShaderConstants constants{};
      unsigned x = 0, y = 0;
      check(info.GetCopyShader(1, 1, constants, x, y, true) ==
                draw_util::ResolveCopyShaderIndex::kFull32bpp,
            "gamma never takes raw-copy fast shader");
      const bool single = xenos::IsSingleCopySampleSelected(selected);
      const auto legacy_gamma = info.GetCopyShader(1, 1, constants, x, y);
      check(single ? (legacy_gamma == draw_util::ResolveCopyShaderIndex::kFast32bpp1x2xMSAA ||
                      legacy_gamma == draw_util::ResolveCopyShaderIndex::kFast32bpp4xMSAA)
                   : legacy_gamma == draw_util::ResolveCopyShaderIndex::kFull32bpp,
            "Native callers retain the original default selection");
      info.color_edram_info.format =
          unsigned(xenos::ColorRenderTargetFormat::k_8_8_8_8);
      auto shader = info.GetCopyShader(1, 1, constants, x, y, true);
      check(
          single
              ? (shader ==
                     draw_util::ResolveCopyShaderIndex::kFast32bpp1x2xMSAA ||
                 shader == draw_util::ResolveCopyShaderIndex::kFast32bpp4xMSAA)
              : shader == draw_util::ResolveCopyShaderIndex::kFull32bpp,
          "non-gamma selection preserved");
    }
  check(sizeof(draw_util::ResolveCopyShaderConstants) == 20 &&
            sizeof(draw_util::ResolveCopyShaderConstants::DestRelative) == 16,
        "push constant ABI preserved");
  std::cout << checks - failures << '/' << checks << " gamma CPU checks passed"
            << std::endl;
  return failures ? 1 : 0;
}
