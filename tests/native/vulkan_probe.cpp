// Headless contracts for the Native-owned Vulkan components. This does not
// launch a guest, certify gameplay, or make Native/Vulkan available in the UI.
#include "vulkan/vulkan_scanout.h"
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/logging.h>
#include <rex/math.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace x360n::native;
using namespace rex::graphics;
namespace {
void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void CheckSubmissionCopies(const rex::ui::vulkan::VulkanDevice& device) {
  VulkanSubmissionRing submissions(device);
  struct Result { std::shared_ptr<VulkanBuffer> bytes; uint64_t serial; uint32_t seed; };
  std::vector<Result> results;
  for (uint32_t seed = 1; seed <= 12; ++seed) {
    auto upload = std::make_shared<VulkanBuffer>(device, 4096, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VulkanBuffer::Access::kUpload);
    auto resident = std::make_shared<VulkanBuffer>(device, 4096,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VulkanBuffer::Access::kDevice);
    auto readback = std::make_shared<VulkanBuffer>(device, 4096, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VulkanBuffer::Access::kReadback);
    for (uint32_t i = 0; i < 4096; ++i) upload->mapped()[i] = std::byte((i * 17 + seed * 71) & 255);
    upload->Flush(0, 4096);
    auto commands = submissions.Begin();
    const auto& fn = device.functions();
    const VkBufferCopy copy{0, 0, 4096};
    fn.vkCmdCopyBuffer(commands, upload->handle(), resident->handle(), 1, &copy);
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = resident->handle();
    barrier.size = 4096;
    fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                            0, 0, nullptr, 1, &barrier, 0, nullptr);
    fn.vkCmdCopyBuffer(commands, resident->handle(), readback->handle(), 1, &copy);
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    barrier.buffer = readback->handle();
    fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                            0, 0, nullptr, 1, &barrier, 0, nullptr);
    submissions.Retain(upload);
    submissions.Retain(resident);
    submissions.Retain(readback);
    results.push_back({readback, submissions.Submit(), seed});
    // Upload and device memory have no owner outside the submission ring now.
  }
  for (const auto& result : results) {
    submissions.Wait(result.serial);
    result.bytes->Invalidate(0, 4096);
    for (uint32_t i = 0; i < 4096; ++i)
      Require(result.bytes->mapped()[i] == std::byte((i * 17 + result.seed * 71) & 255),
              "Native Vulkan transfer fence/lifetime corrupted data");
  }
  Require(submissions.completed() == 12, "Native Vulkan submission completion serial");
}

uint32_t ByteOffset(const ScanoutTexture& t, uint32_t x, uint32_t y) {
  return t.tiled ? texture_util::GetTiledOffset2D(x, y, t.pitch, 2) : (y * t.pitch + x) * 4;
}
void Store(std::vector<uint8_t>& pixels, const ScanoutTexture& t, uint32_t x, uint32_t y, uint32_t value) {
  value = xenos::GpuSwap(value, xenos::Endian(t.endian));
  const auto offset = ByteOffset(t, x, y);
  Require(uint64_t(offset) + 4 <= pixels.size(), "scanout test allocation");
  std::memcpy(pixels.data() + offset, &value, sizeof(value));
}
std::array<uint32_t, 4> Color(uint32_t x, uint32_t y, uint32_t frame, bool rgb10) {
  const uint32_t mask = rgb10 ? 1023 : 255;
  return {(x * 7 + frame * 13) & mask, (y * 11 + frame * 3) & mask,
          ((x ^ y) * 5 + frame * 29) & mask, mask};
}
constexpr unsigned kOverlayColors[4][4]{{255, 255, 255, 0}, {255, 0, 0, 255},
                                      {0, 255, 0, 128}, {0, 0, 255, 255}};
void CheckScanout(rex::ui::vulkan::VulkanProvider& provider) {
  auto presenter = provider.CreatePresenter();
  Require(bool(presenter), "Native Vulkan headless presenter creation");
  VulkanScanout scanout(provider);
  uint64_t checked = 0;
  unsigned largest_error = 0;
  for (uint32_t frame = 0; frame < 32; ++frame) {
    const bool rgb10 = frame >= 16;
    ScanoutTexture t{4096, frame & 1 ? 96u : 128u, 64, 128, (frame >> 2) & 1,
                     frame & 3, 0 | (1 << 3) | (2 << 6) | (3 << 9), 128 * 64 * 4, rgb10};
    if (frame & 8) t.swizzle = 2 | (1 << 3) | (0 << 6) | (3 << 9);
    if ((frame % 5) == 4) t.swizzle = 5 | (4 << 3) | (3 << 6) | (3 << 9);
    std::vector<uint8_t> pixels(t.bytes);
    for (uint32_t y = 0; y < t.height; ++y) for (uint32_t x = 0; x < t.width; ++x) {
      const auto c = Color(x, y, frame, rgb10);
      const auto packed = rgb10 ? c[0] | (c[1] << 10) | (c[2] << 20) | (3u << 30)
                                : c[0] | (c[1] << 8) | (c[2] << 16) | (255u << 24);
      Store(pixels, t, x, y, packed);
    }
    std::array<uint32_t, 384> lut{};
    if (rgb10) {
      for (unsigned i = 0; i < 128; ++i) for (unsigned c = 0; c < 3; ++c)
        lut[i * 3 + c] = (((i * 512) >> c) & ~63u) | ((512 >> c) << 16);
    } else {
      for (unsigned i = 0; i < 256; ++i) {
        const auto r = (i * i * 1023 + 32512) / 65025;
        const auto g = (i * 1023 + 127) / 255;
        lut[i] = r | (g << 10) | ((1023 - g) << 20);
      }
    }
    const ScanoutOverlay overlay{{8192, 64, 64, 64, (frame >> 1) & 1, (frame + 1) & 3,
                                  0 | (1 << 3) | (2 << 6) | (3 << 9), 64 * 64 * 4, false},
                                 32, 24, 96, 88, 32, 24, 256, 128};
    std::vector<uint8_t> plane(overlay.surface.bytes);
    for (uint32_t y = 0; y < 64; ++y) for (uint32_t x = 0; x < 64; ++x) {
      const auto& c = kOverlayColors[x / 16];
      Store(plane, overlay.surface, x, y, c[0] | (c[1] << 8) | (c[2] << 16) | (c[3] << 24));
    }
    const bool composed = frame % 3 != 0;
    scanout.Present(*presenter, t, pixels.data(), lut.data(), composed ? &overlay : nullptr,
                    composed ? plane.data() : nullptr);
    rex::ui::RawImage capture;
    Require(presenter->CaptureGuestOutput(capture), "Native Vulkan scanout capture");
    Require(capture.width == t.width && capture.height == t.height, "Native Vulkan scanout resize");
    const uint32_t full = rgb10 ? 1023 : 255;
    for (uint32_t y = 0; y < t.height; ++y) for (uint32_t x = 0; x < t.width; ++x) {
      const auto input = Color(x, y, frame, rgb10);
      const uint32_t components[]{input[0], input[1], input[2], full, 0, full};
      for (unsigned c = 0; c < 3; ++c) {
        uint32_t index = components[(t.swizzle >> (c * 3)) & 7];
        if (composed) {
          float value = float(index) / full;
          const float sx = (float(x) + 0.5f) * 256.0f / t.width;
          const float sy = (float(y) + 0.5f) * 128.0f / t.height;
          if (sx >= 32 && sx < 96 && sy >= 24 && sy < 88) {
            const auto& plane_color = kOverlayColors[unsigned(sx - 32) / 16];
            const float alpha = float(plane_color[3]) / 255.0f;
            value = value * (1.0f - alpha) + (float(plane_color[c]) / 255.0f) * alpha;
          }
          value = rex::xenos_half_to_float(rex::float_to_xenos_half(value, true, true), true);
          index = uint32_t(value * full + 0.5f);
        }
        double normalized;
        if (rgb10) {
          const uint32_t entry = lut[(index >> 3) * 3 + c];
          normalized = std::clamp(((entry & 65535) + double(entry >> 16) * (index & 7) / 8) / 65472.0, 0.0, 1.0);
        } else normalized = double((lut[index] >> (c * 10)) & 1023) / 1023.0;
        const int expected = int(std::lround(normalized * 255));
        const auto actual = capture.data[y * capture.stride + x * 4 + c];
        const auto error = unsigned(std::abs(expected - actual));
        if (error > 1) {
          std::cerr << "frame=" << frame << " x=" << x << " y=" << y << " channel=" << c
                    << " expected=" << expected << " actual=" << unsigned(actual) << '\n';
          throw std::runtime_error("Native Vulkan scanout differs by more than one 8-bit level");
        }
        largest_error = std::max(largest_error, error);
        ++checked;
      }
    }
  }
  scanout.Drain();
  std::cout << "PASS Native Vulkan component scanout: frames=32 checked_channels=" << checked
            << " max_error=" << largest_error << " tiled/endian/swizzle/RGBA8/RGB10/PWL/D1OVL/resize\n";
}
}
int main(int argc, char** argv) {
  try {
    const std::filesystem::path output = argc > 1 ? argv[1] : ".";
    std::filesystem::create_directories(output);
    rex::InitLogging((output / "native-vulkan-components.log").string().c_str());
    auto provider = rex::ui::vulkan::VulkanProvider::Create(true, true);
    Require(bool(provider), "Native Vulkan provider unavailable");
    const auto& capabilities = provider->vulkan_device()->properties();
    std::cout << "Native Vulkan device=" << capabilities.deviceName
              << " sample_interlock=" << capabilities.fragmentShaderSampleInterlock
              << " vertex_storage=" << capabilities.vertexPipelineStoresAndAtomics
              << " fragment_storage=" << capabilities.fragmentStoresAndAtomics << '\n';
    CheckSubmissionCopies(*provider->vulkan_device());
    CheckScanout(*provider);
    rex::FlushLogging();
    std::cout << "PASS Native Vulkan components: 12 fence-governed buffer roundtrips; no guest execution or FPS claim\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
