#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace offline_achievement_fixture {
inline void Put16(std::vector<uint8_t>& data, uint16_t value) {
  data.push_back(uint8_t(value >> 8)); data.push_back(uint8_t(value));
}
inline void Put32(std::vector<uint8_t>& data, uint32_t value) {
  Put16(data, uint16_t(value >> 16)); Put16(data, uint16_t(value));
}
inline void Put64(std::vector<uint8_t>& data, uint64_t value) {
  Put32(data, uint32_t(value >> 32)); Put32(data, uint32_t(value));
}
inline std::vector<uint8_t> Metadata(uint16_t first_score = 10, unsigned count = 2,
                                     std::span<const uint8_t> image_bytes = {}, uint16_t first_id = 1) {
  // Synthetic original-resource fixture: two achievements, real-format strings
  // and image bytes. These test points never enter user runtime stores.
  std::vector<uint8_t> xstc;
  Put32(xstc, 0x58535443); Put32(xstc, 1); Put32(xstc, 12); Put32(xstc, 1);
  std::vector<uint8_t> strings;
  const std::array<std::pair<uint16_t, const char*>, 7> texts{{
      {0x8000, "Test resource title"}, {1, "First"}, {2, "Completed first"},
      {3, "Complete first"}, {4, "Second"}, {5, "Completed second"}, {6, "Complete second"}}};
  for (const auto& [id, text] : texts) {
    const std::string str(text);
    Put16(strings, id); Put16(strings, uint16_t(str.size()));
    strings.insert(strings.end(), str.begin(), str.end());
  }
  std::vector<uint8_t> xstr;
  Put32(xstr, 0x58535452); Put32(xstr, 1); Put32(xstr, uint32_t(10 + strings.size())); Put16(xstr, uint16_t(texts.size()));
  xstr.insert(xstr.end(), strings.begin(), strings.end());
  std::vector<uint8_t> xach;
  Put32(xach, 0x58414348); Put32(xach, 1); Put32(xach, 10 + count * 36); Put16(xach, uint16_t(count));
  for (unsigned i = 0; i < count; ++i) {
    Put16(xach, uint16_t(i + first_id)); Put16(xach, uint16_t(i * 3 + 1));
    Put16(xach, uint16_t(i * 3 + 2)); Put16(xach, uint16_t(i * 3 + 3));
    Put32(xach, 100 + i); Put16(xach, i ? 25 : first_score); Put16(xach, 0);
    Put32(xach, 3); for (unsigned j = 0; j < 4; ++j) Put32(xach, 0);
  }
  std::vector<uint8_t> image{0x89, 'P', 'N', 'G', 13, 10, 26, 10};
  if (!image_bytes.empty()) image.assign(image_bytes.begin(), image_bytes.end());
  std::vector<uint8_t> result;
  Put32(result, 0x58444246); Put32(result, 0x00010000); Put32(result, 4); Put32(result, 4);
  Put32(result, 0); Put32(result, 0);
  uint32_t offset = 0;
  auto entry = [&](uint16_t section, uint64_t id, const std::vector<uint8_t>& block) {
    Put16(result, section); Put64(result, id); Put32(result, offset); Put32(result, uint32_t(block.size()));
    offset += uint32_t(block.size());
  };
  entry(1, 0x58535443, xstc); entry(3, 1, xstr); entry(1, 0x58414348, xach); entry(2, 100, image);
  for (const auto* block : {&xstc, &xstr, &xach, &image}) result.insert(result.end(), block->begin(), block->end());
  return result;
}
}  // namespace offline_achievement_fixture
