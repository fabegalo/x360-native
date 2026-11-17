#pragma once
#include <rex/gpu/graphics_system.h>
#include <rex/logging/log_category.h>
#include <fstream>
#include <thread>
#include <algorithm>
#include "../../src/supervisor/fs_io.h"

namespace x360n {
// Bounded opt-in diagnostics, shared by either GPU plugin. Captures the actual
// presenter resource; no dependency on desktop visibility or renderer internals.
class GuestCapture {
 public:
  void Start(rex::gpu::IGraphicsSystem& graphics, const std::string& directory, unsigned seconds = 30) {
    if (directory.empty()) return;
    if (!graphics.HasDriver()) return;
    worker_ = std::jthread([&graphics, directory, seconds = std::clamp(seconds, 1u, 300u)](std::stop_token stop) {
      try {
        // Each title transition has its own capture set, even when all children
        // inherit the same diagnostic directory from the session supervisor.
        auto capture_directory = std::filesystem::path(directory) / std::to_string(
            std::chrono::system_clock::now().time_since_epoch().count());
        x360n::io::create_directories(capture_directory);
        for (unsigned tick = 0; tick < seconds * 10 && !stop.stop_requested(); ++tick) {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          if (tick % 20 != 19) continue;
          rex::gpu::RawImage image;
          if (!graphics.CaptureGuestOutput(image)) continue;
          auto path = capture_directory / ("frame-" + std::to_string((tick + 1) / 10) + ".ppm");
          auto pending = path; pending += ".tmp";
          x360n::io::ofstream output(pending, std::ios::binary);
          output << "P6\n" << image.width << " " << image.height << "\n255\n";
          for (unsigned y = 0; y < image.height; ++y)
            for (unsigned x = 0; x < image.width; ++x)
              output.write(reinterpret_cast<const char*>(&image.data[y * image.stride + x * 4]), 3);
          if (!output) throw std::runtime_error("cannot write guest capture");
          output.close();
          if (!output) throw std::runtime_error("cannot finish guest capture");
          x360n::io::rename(pending, path);
          REXLOG_INFO("Guest GPU capture {} {}x{}", path.string(), image.width, image.height);
        }
      } catch (const std::exception& e) { REXLOG_ERROR("Guest GPU capture: {}", e.what()); }
    });
  }
  void Stop() { if (worker_.joinable()) { worker_.request_stop(); worker_.join(); } }
 private:
  std::jthread worker_;
};
}
