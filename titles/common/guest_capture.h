#pragma once
#include <rex/runtime.h>
#include <rex/ui/presenter.h>
#include <rex/logging.h>
#include <fstream>
#include <thread>
#include <algorithm>

namespace x360n {
// Bounded opt-in diagnostics, shared by either GPU plugin. Captures the actual
// presenter resource; no dependency on desktop visibility or renderer internals.
class GuestCapture {
 public:
  void Start(rex::Runtime& runtime, const std::string& directory, unsigned seconds = 30) {
    if (directory.empty()) return;
    auto* gpu = runtime.graphics_system();
    if (!gpu || !gpu->presenter()) return;
    worker_ = std::jthread([presenter = gpu->presenter(), directory, seconds = std::clamp(seconds, 1u, 300u)](std::stop_token stop) {
      try {
        // Each title transition has its own capture set, even when all children
        // inherit the same diagnostic directory from the session supervisor.
        auto capture_directory = std::filesystem::path(directory) / std::to_string(
            std::chrono::system_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(capture_directory);
        for (unsigned tick = 0; tick < seconds * 10 && !stop.stop_requested(); ++tick) {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          if (tick % 20 != 19) continue;
          rex::ui::RawImage image;
          if (!presenter->CaptureGuestOutput(image)) continue;
          auto path = capture_directory / ("frame-" + std::to_string((tick + 1) / 10) + ".ppm");
          auto pending = path; pending += ".tmp";
          std::ofstream output(pending, std::ios::binary);
          output << "P6\n" << image.width << " " << image.height << "\n255\n";
          for (unsigned y = 0; y < image.height; ++y)
            for (unsigned x = 0; x < image.width; ++x)
              output.write(reinterpret_cast<const char*>(&image.data[y * image.stride + x * 4]), 3);
          if (!output) throw std::runtime_error("cannot write guest capture");
          output.close();
          if (!output) throw std::runtime_error("cannot finish guest capture");
          std::filesystem::rename(pending, path);
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
