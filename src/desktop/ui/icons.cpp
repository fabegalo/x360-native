#include "icons.h"
#include <cmath>
#include <cstring>
#include <initializer_list>

namespace x360n::ui {
namespace {
struct Pen {
  ImDrawList* dl; ImVec2 c; float h; Color color; float t;
  ImVec2 P(float x, float y) const { return ImVec2(c.x + x * h, c.y + y * h); }
  void Line(float x0, float y0, float x1, float y1) const { dl->AddLine(P(x0, y0), P(x1, y1), color, t); }
  void Poly(std::initializer_list<ImVec2> unit, bool closed = false) const {
    ImVec2 points[16]; int n = 0;
    for (auto u : unit) points[n++] = P(u.x, u.y);
    dl->AddPolyline(points, n, color, closed ? ImDrawFlags_Closed : 0, t);
  }
  void Rect(float x0, float y0, float x1, float y1, float r = 0.0f) const { dl->AddRect(P(x0, y0), P(x1, y1), color, r * h, 0, t); }
  void Fill(float x0, float y0, float x1, float y1, float r = 0.0f) const { dl->AddRectFilled(P(x0, y0), P(x1, y1), color, r * h); }
  void Ring(float x, float y, float r) const { dl->AddCircle(P(x, y), r * h, color, 0, t); }
  void Dot(float x, float y, float r) const { dl->AddCircleFilled(P(x, y), r * h, color); }
  void Arc(float x, float y, float r, float a0, float a1) const {
    dl->PathArcTo(P(x, y), r * h, a0, a1, 24); dl->PathStroke(color, 0, t);
  }
};
}  // namespace
void Icon(ImDrawList* dl, const char* name, const ImRect& r, Color color, float size) {
  if (!name || !*name) return;
  const float h = T().S(size) * 0.5f;
  const Pen p{dl, r.GetCenter(), h, color, std::max(1.4f, h * 0.15f)};
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
    p.Rect(-0.85f, -0.85f, -0.08f, -0.08f, 0.12f); p.Rect(0.08f, -0.85f, 0.85f, -0.08f, 0.12f);
    p.Rect(-0.85f, 0.08f, -0.08f, 0.85f, 0.12f); p.Rect(0.08f, 0.08f, 0.85f, 0.85f, 0.12f);
  } else if (is("settings")) {
    p.Ring(0, 0, 0.28f); p.Ring(0, 0, 0.6f);
    for (int i = 0; i < 8; ++i) {
      const float a = i * pi / 4, s = std::sin(a), co = std::cos(a);
      p.Line(co * 0.6f, s * 0.6f, co * 0.88f, s * 0.88f);
    }
  } else if (is("info")) {
    p.Ring(0, 0, 0.85f); p.Dot(0, -0.42f, 0.11f); p.Line(0, -0.12f, 0, 0.5f);
  } else if (is("language")) {
    p.Ring(0, 0, 0.85f); p.Line(-0.85f, 0, 0.85f, 0);
    p.dl->AddEllipse(p.P(0, 0), ImVec2(0.38f * h, 0.85f * h), color, 0, 0, p.t);
    p.Line(-0.72f, -0.45f, 0.72f, -0.45f); p.Line(-0.72f, 0.45f, 0.72f, 0.45f);
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
    p.Arc(0, 0, 0.72f, -pi * 0.35f, pi * 1.45f);
    const ImVec2 tip(std::cos(-pi * 0.35f) * 0.72f, std::sin(-pi * 0.35f) * 0.72f);
    p.Poly({{tip.x - 0.42f, tip.y - 0.12f}, {tip.x, tip.y}, {tip.x + 0.05f, tip.y - 0.42f}});
  } else if (is("cancel")) {
    p.Line(-0.65f, -0.65f, 0.65f, 0.65f); p.Line(0.65f, -0.65f, -0.65f, 0.65f);
  } else if (is("add")) {
    p.Line(0, -0.8f, 0, 0.8f); p.Line(-0.8f, 0, 0.8f, 0);
  } else if (is("check")) {
    p.Poly({{-0.75f, 0.02f}, {-0.25f, 0.52f}, {0.78f, -0.55f}});
  } else if (is("warning")) {
    p.Poly({{0, -0.85f}, {0.92f, 0.78f}, {-0.92f, 0.78f}}, true);
    p.Line(0, -0.28f, 0, 0.25f); p.Dot(0, 0.5f, 0.09f);
  } else if (is("error")) {
    p.Ring(0, 0, 0.85f); p.Line(-0.35f, -0.35f, 0.35f, 0.35f); p.Line(0.35f, -0.35f, -0.35f, 0.35f);
  } else if (is("gamepad")) {
    p.Rect(-0.92f, -0.5f, 0.92f, 0.55f, 0.45f);
    p.Line(-0.6f, 0.02f, -0.18f, 0.02f); p.Line(-0.39f, -0.19f, -0.39f, 0.23f);
    p.Dot(0.35f, 0.12f, 0.1f); p.Dot(0.58f, -0.1f, 0.1f);
  } else if (is("chip")) {
    p.Rect(-0.5f, -0.5f, 0.5f, 0.5f, 0.1f); p.Rect(-0.2f, -0.2f, 0.2f, 0.2f);
    for (float x : {-0.25f, 0.0f, 0.25f}) {
      p.Line(x, -0.5f, x, -0.82f); p.Line(x, 0.5f, x, 0.82f);
      p.Line(-0.5f, x, -0.82f, x); p.Line(0.5f, x, 0.82f, x);
    }
  } else if (is("save")) {
    p.Poly({{-0.82f, -0.82f}, {0.5f, -0.82f}, {0.82f, -0.5f}, {0.82f, 0.82f}, {-0.82f, 0.82f}}, true);
    p.Rect(-0.45f, -0.82f, 0.35f, -0.35f); p.Rect(-0.5f, 0.15f, 0.5f, 0.82f);
  } else if (is("tools")) {
    p.Line(-0.72f, 0.72f, 0.1f, -0.1f);
    p.Arc(0.38f, -0.38f, 0.4f, pi * 0.95f, pi * 2.55f);
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
