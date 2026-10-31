#include "icons.h"
#include <cmath>
#include <cstring>
#include <initializer_list>

namespace x360n::ui {
namespace {
struct Pen {
  ImDrawList* dl; ImVec2 c; float h; Color color; float t, density;
  ImVec2 P(float x, float y) const {
    return ImVec2(c.x + std::round(x * h * density) / density, c.y + std::round(y * h * density) / density);
  }
  // Use one coordinate convention for every contour. AddLine/AddRect/AddCircle
  // each introduce different half-pixel offsets in ImGui; mixing them made
  // stems, dots and outlines disagree inside the same small icon.
  void Line(float x0, float y0, float x1, float y1) const {
    const ImVec2 points[] = {P(x0, y0), P(x1, y1)};
    dl->AddPolyline(points, 2, color, 0, t);
    dl->AddCircleFilled(points[0], t / 2, color, 16);
    dl->AddCircleFilled(points[1], t / 2, color, 16);
  }
  void Poly(std::initializer_list<ImVec2> unit, bool closed = false) const {
    ImVec2 points[16]; int n = 0;
    for (auto u : unit) points[n++] = P(u.x, u.y);
    dl->AddPolyline(points, n, color, closed ? ImDrawFlags_Closed : 0, t);
    if (!closed && n > 0) {
      dl->AddCircleFilled(points[0], t / 2, color, 16);
      dl->AddCircleFilled(points[n - 1], t / 2, color, 16);
    }
  }
  void Rect(float x0, float y0, float x1, float y1, float r = 0.0f) const {
    dl->PathRect(P(x0, y0), P(x1, y1), r * h); dl->PathStroke(color, ImDrawFlags_Closed, t);
  }
  void Fill(float x0, float y0, float x1, float y1, float r = 0.0f) const { dl->AddRectFilled(P(x0, y0), P(x1, y1), color, r * h); }
  void Ring(float x, float y, float r) const {
    dl->PathArcTo(P(x, y), r * h, 0, 2 * IM_PI * 31 / 32, 31); dl->PathStroke(color, ImDrawFlags_Closed, t);
  }
  void Meridian(float rx, float ry) const {
    // Share the globe's center and poles; do not snap individual curve points.
    dl->PathEllipticalArcTo(P(0, 0), ImVec2(rx * h, ry * h), 0, 0, 2 * IM_PI * 31 / 32, 31);
    dl->PathStroke(color, ImDrawFlags_Closed, t);
  }
  void Dot(float x, float y, float r) const { dl->AddCircleFilled(P(x, y), std::max(0.75f / density, r * h), color, 16); }
  void Arc(float x, float y, float r, float a0, float a1) const {
    dl->PathArcTo(P(x, y), r * h, a0, a1, 24); dl->PathStroke(color, 0, t);
  }
};
}  // namespace
void Icon(ImDrawList* dl, const char* name, const ImRect& r, Color color, float size) {
  if (!name || !*name) return;
  const float h = T().S(size) * 0.5f;
  const float density = std::max(1.0f, ImGui::GetIO().DisplayFramebufferScale.x);
  const float pixels = std::max(2.0f, std::round(h * 0.2f * density));
  const float phase = std::fmod(pixels, 2.0f) * 0.5f;
  const auto center = r.GetCenter();
  const ImVec2 aligned((std::round(center.x * density - phase) + phase) / density,
                       (std::round(center.y * density - phase) + phase) / density);
  const Pen p{dl, aligned, h, color, pixels / density, density};
  auto is = [&](const char* n) { return std::strcmp(name, n) == 0; };
  constexpr float pi = 3.14159265f;
  if (is("home")) {
    p.Poly({{-0.9f, 0.05f}, {0, -0.8f}, {0.9f, 0.05f}});
    p.Poly({{-0.62f, -0.15f}, {-0.62f, 0.82f}, {0.62f, 0.82f}, {0.62f, -0.15f}});
    p.Poly({{-0.2f, 0.82f}, {-0.2f, 0.3f}, {0.2f, 0.3f}, {0.2f, 0.82f}});
  } else if (is("library")) {
    p.Rect(-0.85f, -0.75f, -0.45f, 0.8f); p.Rect(-0.35f, -0.55f, 0.05f, 0.8f);
    p.Poly({{0.2f, -0.6f}, {0.55f, -0.72f}, {0.9f, 0.68f}, {0.55f, 0.8f}}, true);
  } else if (is("download")) {
    p.Line(0, -0.85f, 0, 0.35f); p.Poly({{-0.42f, -0.05f}, {0, 0.38f}, {0.42f, -0.05f}});
    p.Poly({{-0.85f, 0.45f}, {-0.85f, 0.82f}, {0.85f, 0.82f}, {0.85f, 0.45f}});
  } else if (is("dashboard")) {
    // Leave a visible gutter after stroke expansion, including at 16 px.
    p.Rect(-0.85f, -0.85f, -0.25f, -0.25f, 0.08f); p.Rect(0.25f, -0.85f, 0.85f, -0.25f, 0.08f);
    p.Rect(-0.85f, 0.25f, -0.25f, 0.85f, 0.08f); p.Rect(0.25f, 0.25f, 0.85f, 0.85f, 0.08f);
  } else if (is("settings")) {
    ImVec2 outline[32];
    for (int i = 0; i < 32; ++i) {
      const float a = i * pi / 16, radius = (i % 4 == 0 || i % 4 == 3) ? 0.82f : 0.65f;
      outline[i] = p.P(std::cos(a) * radius, std::sin(a) * radius);
    }
    dl->AddPolyline(outline, 32, color, ImDrawFlags_Closed, p.t);
    p.Ring(0, 0, 0.26f);
  } else if (is("info")) {
    p.Ring(0, 0, 0.85f); p.Dot(0, -0.42f, 0.11f); p.Line(0, -0.12f, 0, 0.5f);
  } else if (is("language")) {
    p.Ring(0, 0, 0.85f); p.Line(-0.85f, 0, 0.85f, 0);
    p.Meridian(0.38f, 0.85f);
  } else if (is("play")) {
    p.dl->AddTriangleFilled(p.P(-0.5f, -0.78f), p.P(0.75f, 0), p.P(-0.5f, 0.78f), color);
  } else if (is("folder")) {
    p.Poly({{-0.9f, 0.75f}, {-0.9f, -0.7f}, {-0.3f, -0.7f}, {-0.15f, -0.5f}, {0.9f, -0.5f}, {0.9f, 0.75f}}, true);
    p.Line(-0.9f, -0.25f, 0.9f, -0.25f);
  } else if (is("file")) {
    p.Poly({{-0.65f, -0.88f}, {0.25f, -0.88f}, {0.65f, -0.48f}, {0.65f, 0.88f}, {-0.65f, 0.88f}}, true);
    p.Poly({{0.25f, -0.88f}, {0.25f, -0.48f}, {0.65f, -0.48f}});
  } else if (is("search")) {
    p.Ring(-0.18f, -0.18f, 0.55f); p.Line(0.22f, 0.22f, 0.85f, 0.85f);
  } else if (is("refresh")) {
    p.Arc(0, 0, 0.7f, pi * 1.05f, pi * 1.9f);
    p.Poly({{0.31f, -0.15f}, {0.75f, -0.15f}, {0.75f, -0.59f}});
    p.Arc(0, 0, 0.7f, pi * 0.05f, pi * 0.9f);
    p.Poly({{-0.31f, 0.15f}, {-0.75f, 0.15f}, {-0.75f, 0.59f}});
  } else if (is("cancel")) {
    p.Line(-0.65f, -0.65f, 0.65f, 0.65f); p.Line(0.65f, -0.65f, -0.65f, 0.65f);
  } else if (is("add")) {
    p.Line(0, -0.8f, 0, 0.8f); p.Line(-0.8f, 0, 0.8f, 0);
  } else if (is("check")) {
    p.Poly({{-0.75f, 0.02f}, {-0.25f, 0.52f}, {0.78f, -0.55f}});
  } else if (is("warning")) {
    dl->AddTriangleFilled(p.P(0, -0.9f), p.P(0.9f, 0.9f), p.P(-0.9f, 0.9f), color);
    const Pen mark{dl, p.c, p.h, P.sidebar, p.t, density};
    mark.Line(0, -0.26f, 0, 0.22f); mark.Dot(0, 0.57f, 0.1f);
  } else if (is("error")) {
    p.Ring(0, 0, 0.85f); p.Line(-0.35f, -0.35f, 0.35f, 0.35f); p.Line(0.35f, -0.35f, -0.35f, 0.35f);
  } else if (is("gamepad")) {
    p.Rect(-0.92f, -0.5f, 0.92f, 0.55f, 0.45f);
    p.Line(-0.6f, 0.02f, -0.18f, 0.02f); p.Line(-0.39f, -0.19f, -0.39f, 0.23f);
    p.Dot(0.35f, 0.12f, 0.1f); p.Dot(0.58f, -0.1f, 0.1f);
  } else if (is("chip")) {
    p.Rect(-0.55f, -0.55f, 0.55f, 0.55f, 0.08f);
    p.Fill(-0.15f, -0.15f, 0.15f, 0.15f);
    for (float x : {-0.3f, 0.3f}) {
      p.Line(x, -0.55f, x, -0.85f); p.Line(x, 0.55f, x, 0.85f);
      p.Line(-0.55f, x, -0.85f, x); p.Line(0.55f, x, 0.85f, x);
    }
  } else if (is("save")) {
    p.Poly({{-0.82f, -0.82f}, {0.5f, -0.82f}, {0.82f, -0.5f}, {0.82f, 0.82f}, {-0.82f, 0.82f}}, true);
    p.Rect(-0.45f, -0.82f, 0.35f, -0.35f); p.Rect(-0.5f, 0.15f, 0.5f, 0.82f);
  } else if (is("tools")) {
    // Rounded open jaws and a full handle distinguish the wrench from a key.
    dl->PathArcTo(p.P(0.25f, -0.25f), 0.62f * h, -pi / 2, -pi * 13 / 12, 14);
    dl->PathLineTo(p.P(-0.8f, 0.36f));
    dl->PathArcTo(p.P(-0.61f, 0.55f), 0.27f * h, -pi * 0.75f, -pi * 1.75f, 12);
    dl->PathLineTo(p.P(0.09f, 0.35f));
    dl->PathArcTo(p.P(0.25f, -0.25f), 0.62f * h, pi * 7 / 12, 0, 14);
    dl->PathLineTo(p.P(0.48f, -0.25f));
    dl->PathLineTo(p.P(0.25f, -0.48f));
    dl->PathStroke(color, ImDrawFlags_Closed, p.t);
  } else if (is("disc")) {
    p.Ring(0, 0, 0.85f); p.Ring(0, 0, 0.22f); p.Arc(0, 0, 0.55f, -pi * 0.9f, -pi * 0.55f);
  } else if (is("remove")) {
    p.Line(-0.85f, -0.6f, 0.85f, -0.6f); p.Poly({{-0.3f, -0.6f}, {-0.3f, -0.85f}, {0.3f, -0.85f}, {0.3f, -0.6f}});
    p.Poly({{-0.62f, -0.6f}, {-0.5f, 0.85f}, {0.5f, 0.85f}, {0.62f, -0.6f}});
    p.Line(-0.18f, -0.3f, -0.15f, 0.55f); p.Line(0.18f, -0.3f, 0.15f, 0.55f);
  } else if (is("package")) {
    p.Poly({{0, -0.88f}, {0.82f, -0.45f}, {0.82f, 0.45f}, {0, 0.88f}, {-0.82f, 0.45f}, {-0.82f, -0.45f}}, true);
    p.Poly({{-0.82f, -0.45f}, {0, 0}, {0.82f, -0.45f}}); p.Line(0, 0, 0, 0.88f);
  }
}
}  // namespace x360n::ui
