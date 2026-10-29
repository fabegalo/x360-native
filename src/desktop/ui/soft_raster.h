#pragma once
// CPU rasterizer for ImGui draw data. It draws the launcher when no GPU device
// can be created (so the application always opens), and renders headless
// captures for layout review without any window, GPU or screen capture.
#include <imgui.h>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace x360n::ui {
struct Canvas {
  int width = 0, height = 0;
  std::vector<uint32_t> pixels;  // bytes R, G, B, A in memory order
  void Resize(int w, int h) { width = w; height = h; pixels.assign(size_t(w) * size_t(h), 0); }
};
// Texture requests of a frame (font atlas and package icons) for this rasterizer.
void UpdateSoftTexture(ImTextureData* texture);
void Rasterize(Canvas& canvas, ImDrawData* draw_data);
bool WritePng(const std::filesystem::path& file, const Canvas& canvas);
}  // namespace x360n::ui
