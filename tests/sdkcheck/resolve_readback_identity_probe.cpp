#include <iostream>
#include <unordered_map>
#include <rex/graphics/register_file.h>
#if __has_include(<rex/graphics/readback_resolve_key.h>)
#include <rex/graphics/readback_resolve_key.h>
#else
// The old production identity, so this check also runs against the baseline.
namespace rex::graphics {
struct ReadbackResolveKey {
  uint32_t address, length, signature;
  bool operator==(const ReadbackResolveKey&) const = default;
};
inline ReadbackResolveKey MakeReadbackResolveKey(const RegisterFile&, uint32_t address, uint32_t length) {
  return {address, length, 0};
}
}
#endif
using namespace rex::graphics;
namespace {
struct ConstantHash {
  size_t operator()(const ReadbackResolveKey&) const { return 0; }
};
}
int main() {
  int checks = 0, failures = 0;
  auto check = [&](bool ok, const char* name) {
    ++checks;
    if (!ok && ++failures <= 16) std::cerr << "FAIL " << name << '\n';
  };
  RegisterFile regs{};
  reg::RB_COPY_CONTROL control{};
  reg::RB_COPY_DEST_INFO dest{};
  dest.copy_dest_format = xenos::ColorFormat::k_8_8_8_8;
  auto key = [&] {
    regs[XE_GPU_REG_RB_COPY_CONTROL] = control.value;
    regs[XE_GPU_REG_RB_COPY_DEST_INFO] = dest.value;
    return MakeReadbackResolveKey(regs, 0x04000000, 0x384000);
  };
  const auto color = key();
  control.copy_src_select = 4;
  const auto depth = key();
  check(color != depth, "color readback cannot reuse depth history at the same range");
  // Deliberately collide the hashes: equality, not a packed hash alone, must
  // preserve all representations and the full guest range.
  std::unordered_map<ReadbackResolveKey, uint32_t, ConstantHash> histories;
  histories[color] = 0xAABBCCDD;
  histories[depth] = 0xFFFF0000;
  check(histories.size() == 2 && histories[color] == 0xAABBCCDD && histories[depth] == 0xFFFF0000,
        "hash collisions do not merge resolve histories");
  control.copy_src_select = 0;
  const auto original_dest = dest;
  for (uint32_t bit : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u, 24u}) {
    dest.value = original_dest.value ^ (1u << bit);
    check(key() != color, "every destination representation bit affects identity");
  }
  dest = original_dest;
  for (uint32_t bit : {16u, 17u, 18u, 19u, 20u, 21u, 22u, 23u, 25u, 26u, 27u, 28u, 29u, 30u, 31u}) {
    dest.value = original_dest.value ^ (1u << bit);
    check(key() == color, "exponent and reserved bits do not fragment the same pass");
  }
  dest = original_dest;
  for (uint32_t rt = 0; rt < 4; ++rt) {
    control.copy_src_select = rt;
    check(key() == color, "color render target indices share representation");
  }
  control.copy_src_select = 0;
  check(MakeReadbackResolveKey(regs, 0x04000004, 0x384000) != color, "address is part of identity");
  check(MakeReadbackResolveKey(regs, 0x04000000, 0x384004) != color, "length is part of identity");
  // Swapping the source and destination dimensions must not introduce a new
  // first-seen pass or an extra GPU wait if its written range is unchanged.
  regs[XE_GPU_REG_RB_SURFACE_INFO] = 0x12345678;
  regs[XE_GPU_REG_RB_DEPTH_INFO] = 0x87654321;
  regs[XE_GPU_REG_RB_COPY_DEST_PITCH] = 0x01230321;
  check(key() == color, "source storage and pitch are not representation identity");
  std::cout << "resolve-readback-identity checks=" << checks << " failures=" << failures << '\n';
  return failures ? 1 : 0;
}
