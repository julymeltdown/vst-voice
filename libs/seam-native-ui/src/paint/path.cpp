#include "seam/native_ui/paint/canvas2d.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace seam::native_ui::paint {

Path& Path::moveTo(ui::Point p) {
  elements_.push_back({Verb::Move, p, {}, {}});
  return *this;
}

Path& Path::lineTo(ui::Point p) {
  if (elements_.empty()) return moveTo(p);
  elements_.push_back({Verb::Line, p, {}, {}});
  return *this;
}

Path& Path::quadTo(ui::Point control, ui::Point p) {
  if (elements_.empty()) moveTo(control);
  elements_.push_back({Verb::Quad, control, p, {}});
  return *this;
}

Path& Path::cubicTo(ui::Point c1, ui::Point c2, ui::Point p) {
  if (elements_.empty()) moveTo(c1);
  elements_.push_back({Verb::Cubic, c1, c2, p});
  return *this;
}

Path& Path::arc(ui::Point center, double radius, double startRadians, double sweepRadians) {
  if (!(radius > 0.0) || !std::isfinite(sweepRadians) || sweepRadians == 0.0) return *this;
  // Approximate with cubic segments of at most a quarter turn each.
  const auto segments = std::max(1, static_cast<int>(std::ceil(std::abs(sweepRadians) /
                                                               (std::numbers::pi / 2.0))));
  const auto step = sweepRadians / segments;
  const auto k = 4.0 / 3.0 * std::tan(step / 4.0);
  auto angle = startRadians;
  const auto point = [&](double a) {
    return ui::Point{center.x + radius * std::cos(a), center.y + radius * std::sin(a)};
  };
  if (elements_.empty() || elements_.back().verb == Verb::Close) moveTo(point(angle));
  else lineTo(point(angle));
  for (int i = 0; i < segments; ++i) {
    const auto a0 = angle;
    const auto a1 = angle + step;
    const auto p0 = point(a0);
    const auto p1 = point(a1);
    const ui::Point c1{p0.x - k * radius * std::sin(a0), p0.y + k * radius * std::cos(a0)};
    const ui::Point c2{p1.x + k * radius * std::sin(a1), p1.y - k * radius * std::cos(a1)};
    elements_.push_back({Verb::Cubic, c1, c2, p1});
    angle = a1;
  }
  return *this;
}

Path& Path::close() {
  if (!elements_.empty()) elements_.push_back({Verb::Close, {}, {}, {}});
  return *this;
}

Path Path::rect(ui::Rect r) {
  Path p;
  p.moveTo({r.x, r.y}).lineTo({r.right(), r.y}).lineTo({r.right(), r.bottom()})
      .lineTo({r.x, r.bottom()}).close();
  return p;
}

Path Path::roundedRect(ui::Rect r, double radius) {
  const auto rr = std::clamp(radius, 0.0, std::min(r.width, r.height) * 0.5);
  if (rr <= 0.0) return rect(r);
  Path p;
  constexpr auto pi = std::numbers::pi;
  p.moveTo({r.x + rr, r.y});
  p.lineTo({r.right() - rr, r.y});
  p.arc({r.right() - rr, r.y + rr}, rr, -pi / 2.0, pi / 2.0);
  p.lineTo({r.right(), r.bottom() - rr});
  p.arc({r.right() - rr, r.bottom() - rr}, rr, 0.0, pi / 2.0);
  p.lineTo({r.x + rr, r.bottom()});
  p.arc({r.x + rr, r.bottom() - rr}, rr, pi / 2.0, pi / 2.0);
  p.lineTo({r.x, r.y + rr});
  p.arc({r.x + rr, r.y + rr}, rr, pi, pi / 2.0);
  p.close();
  return p;
}

Path Path::circle(ui::Point center, double radius) {
  Path p;
  p.arc(center, radius, 0.0, 2.0 * std::numbers::pi);
  p.close();
  return p;
}

Path Path::capsule(ui::Rect r) { return roundedRect(r, std::min(r.width, r.height) * 0.5); }

}  // namespace seam::native_ui::paint
