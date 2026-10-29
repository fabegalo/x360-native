#include "soft_raster.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>

namespace x360n::ui {
// The texture data itself is the identifier: its CPU pixels stay alive with it.
void UpdateSoftTexture(ImTextureData* tex) {
  if (tex->Status == ImTextureStatus_WantCreate || tex->Status == ImTextureStatus_WantUpdates) {
    tex->SetTexID(ImTextureID(reinterpret_cast<intptr_t>(tex)));
    tex->SetStatus(ImTextureStatus_OK);
  } else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) {
    tex->SetTexID(ImTextureID_Invalid);
    tex->SetStatus(ImTextureStatus_Destroyed);
  }
}
namespace {
struct Vertex { float x, y, u, v; float c[4]; };
// Bilinear, clamped: the same filtering the GPU samplers use, which the
// texture-based anti-aliased lines and the scaled package icons rely on.
inline void Sample(const ImTextureData* tex, float u, float v, float out[4]) {
  if (!tex || !tex->Pixels) { out[0] = out[1] = out[2] = out[3] = 1.0f; return; }
  const float fx = u * tex->Width - 0.5f, fy = v * tex->Height - 0.5f;
  const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
  const float tx = fx - x0, ty = fy - y0;
  auto texel = [&](int x, int y, float c[4]) {
    x = std::clamp(x, 0, tex->Width - 1); y = std::clamp(y, 0, tex->Height - 1);
    if (tex->Format == ImTextureFormat_Alpha8) { c[0] = c[1] = c[2] = 1.0f; c[3] = tex->Pixels[size_t(y) * tex->Width + x] / 255.0f; return; }
    const unsigned char* p = tex->Pixels + (size_t(y) * tex->Width + x) * 4;
    for (int i = 0; i < 4; ++i) c[i] = p[i] / 255.0f;
  };
  float a[4], b[4], c[4], d[4];
  texel(x0, y0, a); texel(x0 + 1, y0, b); texel(x0, y0 + 1, c); texel(x0 + 1, y0 + 1, d);
  for (int i = 0; i < 4; ++i) out[i] = (a[i] * (1 - tx) + b[i] * tx) * (1 - ty) + (c[i] * (1 - tx) + d[i] * tx) * ty;
}
inline void Blend(uint32_t& dst, const float src[4]) {
  const float a = std::clamp(src[3], 0.0f, 1.0f);
  if (a <= 0.0f) return;
  auto* d = reinterpret_cast<unsigned char*>(&dst);
  for (int i = 0; i < 3; ++i) d[i] = static_cast<unsigned char>(std::lround(std::clamp(src[i], 0.0f, 1.0f) * 255.0f * a + d[i] * (1.0f - a)));
  d[3] = 255;
}
// Pixel centers inside the triangle, top-left rule so shared edges are drawn once.
void Triangle(Canvas& c, const Vertex& a, const Vertex& b, const Vertex& v, const ImVec4& clip, const ImTextureData* tex) {
  const Vertex* p[3] = {&a, &b, &v};
  float area = (b.x - a.x) * (v.y - a.y) - (b.y - a.y) * (v.x - a.x);
  if (std::fabs(area) < 1e-6f) return;
  if (area < 0) { std::swap(p[1], p[2]); area = -area; }
  const int x0 = std::max(int(std::floor(std::max(clip.x, std::min({a.x, b.x, v.x})))), 0);
  const int y0 = std::max(int(std::floor(std::max(clip.y, std::min({a.y, b.y, v.y})))), 0);
  const int x1 = std::min(int(std::ceil(std::min(clip.z, std::max({a.x, b.x, v.x})))), c.width);
  const int y1 = std::min(int(std::ceil(std::min(clip.w, std::max({a.y, b.y, v.y})))), c.height);
  if (x0 >= x1 || y0 >= y1) return;
  auto edge = [](const Vertex* s, const Vertex* e, float x, float y) { return (e->x - s->x) * (y - s->y) - (e->y - s->y) * (x - s->x); };
  auto top_left = [](const Vertex* s, const Vertex* e) { const float dy = e->y - s->y, dx = e->x - s->x; return (dy == 0 && dx < 0) || dy > 0; };
  const bool tl[3] = {top_left(p[1], p[2]), top_left(p[2], p[0]), top_left(p[0], p[1])};
  // Constant color and a rectangle-sized flat region are the common case: skip sampling then.
  const bool flat_color = a.c[0] == b.c[0] && a.c[0] == v.c[0] && a.c[1] == b.c[1] && a.c[1] == v.c[1] &&
                          a.c[2] == b.c[2] && a.c[2] == v.c[2] && a.c[3] == b.c[3] && a.c[3] == v.c[3];
  for (int y = y0; y < y1; ++y) {
    const float py = y + 0.5f;
    for (int x = x0; x < x1; ++x) {
      const float px = x + 0.5f;
      const float w0 = edge(p[1], p[2], px, py), w1 = edge(p[2], p[0], px, py), w2 = edge(p[0], p[1], px, py);
      if (w0 < 0 || w1 < 0 || w2 < 0) continue;
      if ((w0 == 0 && !tl[0]) || (w1 == 0 && !tl[1]) || (w2 == 0 && !tl[2])) continue;
      const float l0 = w0 / area, l1 = w1 / area, l2 = w2 / area;
      float color[4], texel[4];
      for (int i = 0; i < 4; ++i) color[i] = flat_color ? a.c[i] : p[0]->c[i] * l0 + p[1]->c[i] * l1 + p[2]->c[i] * l2;
      Sample(tex, p[0]->u * l0 + p[1]->u * l1 + p[2]->u * l2, p[0]->v * l0 + p[1]->v * l1 + p[2]->v * l2, texel);
      for (int i = 0; i < 4; ++i) color[i] *= texel[i];
      Blend(c.pixels[size_t(y) * c.width + x], color);
    }
  }
}
}  // namespace
void Rasterize(Canvas& canvas, ImDrawData* data) {
  if (data->Textures) for (auto* tex : *data->Textures) if (tex->Status != ImTextureStatus_OK) UpdateSoftTexture(tex);
  const ImVec2 off = data->DisplayPos, scale = data->FramebufferScale;
  for (const ImDrawList* list : data->CmdLists) {
    for (const ImDrawCmd& cmd : list->CmdBuffer) {
      if (cmd.UserCallback) {
        if (cmd.UserCallback != ImDrawCallback_ResetRenderState) cmd.UserCallback(list, &cmd);
        continue;
      }
      const ImVec4 clip((cmd.ClipRect.x - off.x) * scale.x, (cmd.ClipRect.y - off.y) * scale.y,
                        (cmd.ClipRect.z - off.x) * scale.x, (cmd.ClipRect.w - off.y) * scale.y);
      const auto id = cmd.GetTexID();
      const auto* tex = id == ImTextureID_Invalid ? nullptr : reinterpret_cast<const ImTextureData*>(static_cast<intptr_t>(id));
      for (unsigned i = 0; i + 2 < cmd.ElemCount; i += 3) {
        Vertex v[3];
        for (int k = 0; k < 3; ++k) {
          const ImDrawVert& src = list->VtxBuffer[cmd.VtxOffset + list->IdxBuffer[cmd.IdxOffset + i + k]];
          v[k].x = (src.pos.x - off.x) * scale.x; v[k].y = (src.pos.y - off.y) * scale.y;
          v[k].u = src.uv.x; v[k].v = src.uv.y;
          for (int j = 0; j < 4; ++j) v[k].c[j] = ((src.col >> (8 * j)) & 0xFF) / 255.0f;
        }
        Triangle(canvas, v[0], v[1], v[2], clip, tex);
      }
    }
  }
}
namespace {
uint32_t Crc(const unsigned char* data, size_t size, uint32_t crc = 0xFFFFFFFFu) {
  static const auto table = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t n = 0; n < 256; ++n) { uint32_t c = n; for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; t[n] = c; }
    return t;
  }();
  for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return crc;
}
void Be32(std::vector<unsigned char>& out, uint32_t v) { for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<unsigned char>(v >> s)); }
void Chunk(std::ofstream& out, const char* type, const std::vector<unsigned char>& data) {
  std::vector<unsigned char> head; Be32(head, uint32_t(data.size()));
  std::vector<unsigned char> body(type, type + 4); body.insert(body.end(), data.begin(), data.end());
  std::vector<unsigned char> tail; Be32(tail, Crc(body.data(), body.size()) ^ 0xFFFFFFFFu);
  out.write(reinterpret_cast<const char*>(head.data()), 4);
  out.write(reinterpret_cast<const char*>(body.data()), std::streamsize(body.size()));
  out.write(reinterpret_cast<const char*>(tail.data()), 4);
}
}  // namespace
// Uncompressed (stored) deflate: captures only, no compressor dependency.
bool WritePng(const std::filesystem::path& file, const Canvas& c) {
  std::ofstream out(file, std::ios::binary);
  if (!out) return false;
  const unsigned char signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  out.write(reinterpret_cast<const char*>(signature), 8);
  std::vector<unsigned char> header;
  Be32(header, uint32_t(c.width)); Be32(header, uint32_t(c.height));
  header.insert(header.end(), {8, 6, 0, 0, 0});
  Chunk(out, "IHDR", header);
  std::vector<unsigned char> raw;
  raw.reserve(size_t(c.height) * (size_t(c.width) * 4 + 1));
  for (int y = 0; y < c.height; ++y) {
    raw.push_back(0);
    const auto* row = reinterpret_cast<const unsigned char*>(c.pixels.data() + size_t(y) * c.width);
    raw.insert(raw.end(), row, row + size_t(c.width) * 4);
  }
  std::vector<unsigned char> z{0x78, 0x01};
  uint32_t s1 = 1, s2 = 0;
  for (unsigned char b : raw) { s1 = (s1 + b) % 65521; s2 = (s2 + s1) % 65521; }
  for (size_t at = 0; at < raw.size() || raw.empty();) {
    const size_t n = std::min<size_t>(65535, raw.size() - at);
    z.push_back(at + n >= raw.size() ? 1 : 0);
    z.push_back(static_cast<unsigned char>(n)); z.push_back(static_cast<unsigned char>(n >> 8));
    z.push_back(static_cast<unsigned char>(~n)); z.push_back(static_cast<unsigned char>(~n >> 8));
    z.insert(z.end(), raw.begin() + std::ptrdiff_t(at), raw.begin() + std::ptrdiff_t(at + n));
    at += n;
    if (raw.empty()) break;
  }
  Be32(z, (s2 << 16) | s1);
  Chunk(out, "IDAT", z);
  Chunk(out, "IEND", {});
  return bool(out);
}
}  // namespace x360n::ui
