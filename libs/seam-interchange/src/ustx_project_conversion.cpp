#include "seam/interchange/ustx_project_conversion.hpp"

#include "seam/domain/note.hpp"
#include "seam/domain/dynamics_automation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <string_view>
#include <utility>

namespace seam::interchange {
namespace {

constexpr std::int64_t kUstxPpq = 480;
constexpr std::int64_t kSeamPpq = 960;

void addIssue(std::vector<UstxIssue>& issues, UstxIssueSeverity severity,
              std::string path, std::string message, const UstxLimits& limits);

std::int16_t sampleDynamics(const std::vector<UstxDynamicsPoint>& curve,
                            std::int64_t tick) {
  const auto right = std::lower_bound(curve.begin(), curve.end(), tick,
      [](const auto& point, std::int64_t value) { return point.position.value() < value; });
  if (right != curve.end() && right->position.value() == tick) return right->tenthDecibels;
  if (right == curve.begin() || right == curve.end()) return 0;
  const auto& left = *(right - 1);
  const double fraction = static_cast<double>(tick - left.position.value()) /
      static_cast<double>((right->position - left.position).value());
  const double value = std::lerp(static_cast<double>(left.tenthDecibels),
                                 static_cast<double>(right->tenthDecibels), fraction);
  // OpenUtau UCurve.Sample uses Math.Round, whose midpoint rule is ties-to-even.
  const double lower = std::floor(value);
  const double remainder = value - lower;
  const double rounded = remainder < 0.5 ||
      (remainder == 0.5 && std::fmod(lower, 2.0) == 0.0) ? lower : lower + 1.0;
  return static_cast<std::int16_t>(rounded);
}

void importDynamics(const UstxPart& source, domain::VocalRegion& region,
                    std::string_view path, std::vector<UstxIssue>& issues,
                    const UstxLimits& limits) {
  if (source.dynamics.empty()) return;
  const auto first = source.dynamics.front().position.value();
  const auto last = source.dynamics.back().position.value();
  const auto gridStart = first - first % 5;
  const auto gridEnd = std::min(last + (5 - last % 5) % 5,
                                source.duration.value() - source.duration.value() % 5);
  const auto gridCount = static_cast<std::uint64_t>((gridEnd - gridStart) / 5 + 1);
  if (gridCount + 3U > domain::kMaximumDynamicsPoints) {
    addIssue(issues, UstxIssueSeverity::Loss, std::string(path) + ".curves",
             "dynamics span exceeds SEAM's bounded automation point count and was omitted", limits);
    return;
  }
  if (std::any_of(source.dynamics.begin(), source.dynamics.end(),
                  [](const auto& point) { return point.position.value() % 5 != 0; }))
    addIssue(issues, UstxIssueSeverity::Loss, std::string(path) + ".curves",
             "off-grid dynamics editing points were sampled to OpenUtau's five-tick render grid", limits);
  std::vector<domain::DynamicsAutomationPoint> points;
  points.reserve(static_cast<std::size_t>(gridCount + 3U));
  if (gridStart > 0) points.push_back({time::Tick{0}, 1.0F});
  if (gridStart > 5) points.push_back({time::Tick{(gridStart - 5) * 2}, 1.0F});
  for (std::int64_t tick = gridStart; tick <= gridEnd; tick += 5) {
    const auto y = sampleDynamics(source.dynamics, tick);
    const auto gain = y == -240 ? 0.0F : static_cast<float>(std::pow(10.0, static_cast<double>(y) / 200.0));
    points.push_back({time::Tick{tick * 2}, gain});
  }
  if (gridEnd < source.duration.value()) {
    const auto nextTick = std::min(gridEnd + 5, source.duration.value());
    points.push_back({time::Tick{nextTick * 2}, 1.0F});
  }
  const auto applied = region.dynamicsAutomation.replacePoints(std::move(points));
  if (!applied)
    addIssue(issues, UstxIssueSeverity::Loss, std::string(path) + ".curves",
             "dynamics curve could not be represented by SEAM automation", limits);
}

void addIssue(std::vector<UstxIssue>& issues, UstxIssueSeverity severity,
              std::string path, std::string message, const UstxLimits& limits) {
  // One overflow witness makes the final admission fail without retaining an
  // unbounded report. Neither import nor export may publish a partial report.
  if (issues.size() <= std::min<std::size_t>(limits.maximumNodes, 4'096U))
    issues.push_back({severity, std::move(path), std::move(message)});
}

core::Result<std::int64_t> scaleTick(std::int64_t value, std::int64_t source,
                                     std::int64_t target, std::string_view path,
                                     std::vector<UstxIssue>& issues,
                                     const UstxLimits& limits) {
  if (value < 0 || source <= 0 || target <= 0) return core::failure<std::int64_t>(core::ErrorCode::InvalidArgument, "USTX tick scaling input is invalid", std::string(path));
  const long double scaled = static_cast<long double>(value) * static_cast<long double>(target) / static_cast<long double>(source);
  if (!std::isfinite(static_cast<double>(scaled)) || scaled < 0.0L || scaled > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
    return core::failure<std::int64_t>(core::ErrorCode::InvalidArgument, "USTX tick scaling overflows", std::string(path));
  const auto rounded = static_cast<std::int64_t>(std::llround(static_cast<double>(scaled)));
  if (static_cast<long double>(rounded) != scaled)
    addIssue(issues, UstxIssueSeverity::Warning, std::string(path), "tick was rounded to the USTX 480 PPQ grid", limits);
  return rounded;
}

double secondsAt(const std::vector<UstxTempo>& tempos, std::int64_t tick) noexcept {
  if (tempos.empty() || tick <= 0) return tick <= 0 ? static_cast<double>(tick) * 60.0 / (120.0 * kUstxPpq) : 0.0;
  double seconds = 0.0;
  std::int64_t cursor = 0;
  double bpm = tempos.front().bpm;
  for (std::size_t index = 1U; index < tempos.size(); ++index) {
    if (tick <= tempos[index].position.value()) break;
    const auto end = tempos[index].position.value();
    seconds += static_cast<double>(end - cursor) * 60.0 / (bpm * kUstxPpq);
    cursor = end;
    bpm = tempos[index].bpm;
  }
  return seconds + static_cast<double>(tick - cursor) * 60.0 / (bpm * kUstxPpq);
}

std::int64_t tickAtSeconds(const std::vector<UstxTempo>& tempos, double seconds) noexcept {
  if (tempos.empty() || !std::isfinite(seconds)) return 0;
  // OpenUtau TimeAxis.MsPosToTickPos uses Math.Round's ties-to-even rule on
  // the complete tick position, including the tempo segment's origin.
  const auto roundTick = [](double value) {
    const auto lower = std::floor(value);
    const auto fraction = value - lower;
    const auto rounded = fraction < 0.5 || (fraction == 0.5 && std::fmod(lower, 2.0) == 0.0) ? lower : lower + 1.0;
    return static_cast<std::int64_t>(rounded);
  };
  if (seconds <= 0.0) return roundTick(seconds * tempos.front().bpm * kUstxPpq / 60.0);
  double elapsed = 0.0;
  std::int64_t cursor = 0;
  double bpm = tempos.front().bpm;
  for (std::size_t index = 1U; index < tempos.size(); ++index) {
    const auto end = tempos[index].position.value();
    const auto segment = static_cast<double>(end - cursor) * 60.0 / (bpm * kUstxPpq);
    if (seconds <= elapsed + segment) return roundTick(static_cast<double>(cursor) + (seconds - elapsed) * bpm * kUstxPpq / 60.0);
    elapsed += segment;
    cursor = end;
    bpm = tempos[index].bpm;
  }
  return roundTick(static_cast<double>(cursor) + (seconds - elapsed) * bpm * kUstxPpq / 60.0);
}

std::string shapeFor(domain::CurveInterpolation interpolation,
                     std::string_view path, std::vector<UstxIssue>& issues,
                     const UstxLimits& limits) {
  switch (interpolation) {
    case domain::CurveInterpolation::Step:
      addIssue(issues, UstxIssueSeverity::Loss, std::string(path),
               "USTX has no step pitch shape; the segment was approximated as linear", limits);
      return "l";
    case domain::CurveInterpolation::Smooth:
      addIssue(issues, UstxIssueSeverity::Loss, std::string(path),
               "SEAM smoothstep pitch was approximated as USTX sine-in-out", limits);
      return "io";
    case domain::CurveInterpolation::Linear: return "l";
  }
  return "l";
}

domain::CurveInterpolation interpolationFor(std::string_view shape,
                                             std::string_view path,
                                             std::vector<UstxIssue>& issues,
                                             const UstxLimits& limits) {
  if (shape == "l") return domain::CurveInterpolation::Linear;
  // OpenUtau PitchPointShape.sp is a spline, not a step. Its sine and spline
  // shapes have no exact equivalent in SEAM's cubic smoothstep interpolation.
  if (shape == "sp" || shape == "io" || shape == "i" || shape == "o") {
    addIssue(issues, UstxIssueSeverity::Loss, std::string(path),
             "USTX " + std::string(shape) + " pitch shape was approximated as SEAM smoothstep", limits);
    return domain::CurveInterpolation::Smooth;
  }
  addIssue(issues, UstxIssueSeverity::Loss, std::string(path), "unknown USTX pitch shape was approximated as smooth", limits);
  return domain::CurveInterpolation::Smooth;
}

// OpenUtau composes pitch contributions in absolute cents. A negative-X point
// cannot be copied into SEAM's region-wide offset curve across a tone change.
// Sine easing is linearized with an analytic error/work budget before the
// sparse event sweep. Long notes do not require one allocation per tick.
// Splines, unknown shapes and polyphony retain their explicitly lossy fallback.
core::Result<std::optional<domain::PitchAutomation>> composePitch(
    const UstxPart& part, const std::vector<UstxTempo>& tempos,
    const std::string& partPath, std::vector<UstxIssue>& issues,
    const UstxLimits& limits) {
  using Output = std::optional<domain::PitchAutomation>;
  const bool needed = std::any_of(part.notes.begin(), part.notes.end(), [&](const auto& note) {
    return !note.pitch.empty() && (note.snapFirst ||
        (part.notes.size() > 1U && std::any_of(note.pitch.begin(), note.pitch.end(),
            [](const auto& point) { return point.offsetMilliseconds < 0.0; })));
  });
  if (!needed) return Output{};
  std::vector<std::size_t> order;
  order.reserve(part.notes.size());
  for (std::size_t index = 0U; index < part.notes.size(); ++index) order.push_back(index);
  std::sort(order.begin(), order.end(), [&](auto left, auto right) {
    return part.notes[left].position != part.notes[right].position
        ? part.notes[left].position < part.notes[right].position : left < right;
  });
  for (std::size_t index = 1U; index < order.size(); ++index) {
    const auto& previous = part.notes[order[index - 1U]];
    if (previous.position + previous.duration > part.notes[order[index]].position) {
      addIssue(issues, UstxIssueSeverity::Loss, partPath + ".pitch.composition",
               "overlapping score notes cannot share a monophonic USTX pitch composition", limits);
      return Output{};
    }
  }
  struct Point final { std::int64_t tick; double absoluteCents; std::string shape; };
  struct Curve final { std::vector<Point> points; double tone; double previousTone; std::int64_t previousEnd; };
  std::vector<Curve> curves;
  curves.reserve(order.size());
  std::vector<UstxIssue> compositionIssues;
  // The performance compiler accepts at most 16,384 region pitch points.
  const auto pointLimit = std::min<std::size_t>(limits.maximumCurvePoints, 16'384U);
  std::size_t preparedPoints = 0U;
  for (std::size_t ordered = 0U; ordered < order.size(); ++ordered) {
    const auto index = order[ordered];
    const auto& note = part.notes[index];
    const auto notePath = partPath + ".notes[" + std::to_string(index) + "]";
    const auto* previous = ordered == 0U ? nullptr : &part.notes[order[ordered - 1U]];
    const auto tone = static_cast<double>(note.tone) * 100.0 + note.tuning;
    const auto previousTone = previous ? static_cast<double>(previous->tone) * 100.0 + previous->tuning : tone;
    Curve curve{{}, tone, previousTone,
                previous ? (previous->position.value() + previous->duration.value()) * 2 : 0};
    const auto noteStart = note.position.value() * 2;
    const auto noteEnd = (note.position.value() + note.duration.value()) * 2;
    const auto absoluteNote = part.position.value() + note.position.value();
    for (std::size_t pointIndex = 0U; pointIndex < note.pitch.size(); ++pointIndex) {
      const auto& point = note.pitch[pointIndex];
      if (pointIndex > 0U && point.offsetMilliseconds < note.pitch[pointIndex - 1U].offsetMilliseconds) {
        addIssue(issues, UstxIssueSeverity::Loss, notePath + ".pitch.composition",
                 "unordered pitch points are outside the supported pitch composition", limits);
        return Output{};
      }
      const auto target = tickAtSeconds(tempos, secondsAt(tempos, absoluteNote) + point.offsetMilliseconds / 1000.0);
      const auto pointPath = notePath + ".pitch[" + std::to_string(pointIndex) + "]";
      if (target < part.position.value() || target > part.position.value() + part.duration.value()) {
        addIssue(compositionIssues, UstxIssueSeverity::Loss, pointPath, "pitch point falls outside its part and was omitted", limits);
        continue;
      }
      const auto tick = (target - part.position.value()) * 2;
      if (tick > noteEnd) {
        addIssue(compositionIssues, UstxIssueSeverity::Loss, pointPath, "pitch point falls after its note and was omitted", limits);
        continue;
      }
      auto cents = tone + point.y * 10.0;
      if (pointIndex == 0U && note.snapFirst)
        cents = previous && previous->position + previous->duration == note.position ? previousTone : tone;
      if (!curve.points.empty() && curve.points.back().tick == tick) {
        curve.points.back() = {tick, cents, point.shape};
        addIssue(compositionIssues, UstxIssueSeverity::Loss, pointPath,
                 "pitch points coincident on the USTX tick grid were merged", limits);
      } else {
        if (++preparedPoints > pointLimit)
          return core::failure<Output>(core::ErrorCode::InvalidArgument, "USTX composed pitch exceeds the supported point limit");
        curve.points.push_back({tick, cents, point.shape});
      }
    }
    if (curve.points.empty()) {
      curve.points.push_back({noteStart, tone, "l"});
      curve.points.push_back({noteEnd, tone, "l"});
      preparedPoints += 2U;
    } else {
      if (curve.points.front().tick > noteStart) {
        curve.points.insert(curve.points.begin(), {noteStart, curve.points.front().absoluteCents, "l"});
        ++preparedPoints;
      }
      if (curve.points.back().tick < noteEnd) {
        curve.points.push_back({noteEnd, curve.points.back().absoluteCents, "l"});
        ++preparedPoints;
      }
    }
    if (preparedPoints > pointLimit)
      return core::failure<Output>(core::ErrorCode::InvalidArgument, "USTX composed pitch exceeds the supported point limit");
    const bool changingCurve = std::any_of(curve.points.begin(), curve.points.end(), [&](const auto& point) {
      return point.absoluteCents != curve.points.front().absoluteCents;
    });
    for (std::size_t point = 1U; point < curve.points.size(); ++point) {
      const auto& left = curve.points[point - 1U];
      // A spline with equal endpoints can still bend toward a neighboring
      // control. Only an entirely constant spline curve is safely flat here.
      if ((left.shape == "sp" && changingCurve) ||
          (left.shape != "l" && left.shape != "io" && left.shape != "i" && left.shape != "o" &&
           left.absoluteCents != curve.points[point].absoluteCents)) {
        addIssue(issues, UstxIssueSeverity::Loss, notePath + ".pitch.composition",
                 "spline or unknown USTX pitch composition is unsupported; individual points use the documented approximation", limits);
        return Output{};
      }
    }
    curves.push_back(std::move(curve));
  }
  // Bound the SUM of simultaneous curve errors, not each curve independently.
  // Half-open spans ensure adjacent segments do not inflate this count. This
  // depends on overlap, not song duration or the total number of score notes.
  std::vector<std::pair<std::int64_t, int>> nonlinearSpans;
  for (const auto& curve : curves) {
    for (std::size_t index = 1U; index < curve.points.size(); ++index) {
      const auto& left = curve.points[index - 1U];
      const auto& right = curve.points[index];
      if (left.shape != "l" && left.absoluteCents != right.absoluteCents) {
        nonlinearSpans.emplace_back(left.tick, 1);
        nonlinearSpans.emplace_back(right.tick, -1);
      }
    }
  }
  std::sort(nonlinearSpans.begin(), nonlinearSpans.end());
  int active = 0, maximumOverlap = 0;
  for (const auto& [tick, change] : nonlinearSpans) {
    (void)tick;
    active += change;
    maximumOverlap = std::max(maximumOverlap, active);
  }
  constexpr double kLinearizationBudgetCents = 0.25;
  const auto segmentBudget = kLinearizationBudgetCents / std::max(1, maximumOverlap);
  std::size_t subdivisionWork = 0U;
  for (auto& curve : curves) {
    std::vector<Point> linear;
    linear.push_back({curve.points.front().tick, curve.points.front().absoluteCents, "l"});
    for (std::size_t index = 1U; index < curve.points.size(); ++index) {
      const auto& left = curve.points[index - 1U];
      const auto& right = curve.points[index];
      if (left.shape == "l" || left.absoluteCents == right.absoluteCents) {
        linear.push_back({right.tick, right.absoluteCents, "l"});
        continue;
      }
      const auto width = static_cast<double>(right.tick - left.tick);
      const auto delta = right.absoluteCents - left.absoluteCents;
      // OpenUtau MusicMath.cs:123-150, after RenderPhrase converts X to ticks.
      const auto valueAt = [&](std::int64_t tick) {
        const auto t = static_cast<double>(tick - left.tick) / width;
        const auto eased = left.shape == "io" ? (1.0 - std::cos(std::numbers::pi * t)) / 2.0
            : left.shape == "i" ? 1.0 - std::cos(std::numbers::pi * t / 2.0)
                                : std::sin(std::numbers::pi * t / 2.0);
        return left.absoluteCents + delta * eased;
      };
      const auto curvature = std::abs(delta) * std::numbers::pi * std::numbers::pi *
          (left.shape == "io" ? 0.5 : 0.25);
      const auto subdivide = [&](auto&& self, std::int64_t begin, std::int64_t end,
                                  double endValue) -> bool {
        if (++subdivisionWork > pointLimit * 2U) return false;
        const auto relativeWidth = static_cast<double>(end - begin) / width;
        // Linear interpolation error <= max|f''| * h^2 / 8. At a one-tick
        // interval both representable sample positions are exact endpoints;
        // sub-tick temporal reconstruction is explicitly outside this bound.
        if (end - begin <= 1 || curvature * relativeWidth * relativeWidth / 8.0 <= segmentBudget) {
          linear.push_back({end, endValue, "l"});
          return true;
        }
        if (++preparedPoints > pointLimit) return false;
        const auto middle = begin + (end - begin) / 2;
        return self(self, begin, middle, valueAt(middle)) && self(self, middle, end, endValue);
      };
      if (!subdivide(subdivide, left.tick, right.tick, right.absoluteCents))
        return core::failure<Output>(core::ErrorCode::InvalidArgument,
            "USTX nonlinear pitch linearization exceeds the supported point/work budget");
    }
    curve.points = std::move(linear);
  }
  struct Event final { double valueDelta{0.0}; double slopeDelta{0.0}; };
  std::map<std::int64_t, Event> events;
  const auto event = [&](std::int64_t tick, double value, double slope) {
    if (value == 0.0 && slope == 0.0) return true;
    if (!events.contains(tick) && events.size() >= pointLimit) return false;
    auto& entry = events[tick];
    entry.valueDelta += value;
    entry.slopeDelta += slope;
    return true;
  };
  const auto span = [&](std::int64_t begin, std::int64_t end, double first, double last) {
    if (begin >= end) return true;
    const auto slope = (last - first) / static_cast<double>(end - begin);
    return event(begin, first, slope) && event(end, -last, -slope);
  };
  bool bounded = event(0, part.notes[order.front()].tuning, 0.0);
  for (std::size_t ordered = 0U; ordered < order.size() && bounded; ++ordered) {
    const auto& note = part.notes[order[ordered]];
    const auto& curve = curves[ordered];
    if (ordered > 0U) bounded = event(curve.previousEnd,
        note.tuning - part.notes[order[ordered - 1U]].tuning, 0.0);
    for (std::size_t index = 1U; index < curve.points.size() && bounded; ++index) {
      const auto& left = curve.points[index - 1U];
      const auto& right = curve.points[index];
      const auto split = std::clamp(curve.previousEnd, left.tick, right.tick);
      const auto middle = left.absoluteCents + (right.absoluteCents - left.absoluteCents) *
          static_cast<double>(split - left.tick) / static_cast<double>(right.tick - left.tick);
      bounded = span(left.tick, split, left.absoluteCents - curve.previousTone, middle - curve.previousTone) &&
                span(split, right.tick, middle - curve.tone, right.absoluteCents - curve.tone);
    }
  }
  if (!bounded)
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "USTX composed pitch exceeds the supported point limit");
  domain::PitchAutomation pitch;
  const auto emit = [&](std::int64_t tick, double cents, domain::CurveInterpolation interpolation) {
    if ((pitch.points().empty() || pitch.points().back().tick != time::Tick{tick}) && pitch.points().size() >= pointLimit)
      return core::failure(core::ErrorCode::InvalidArgument, "USTX composed pitch exceeds the supported point limit");
    return pitch.upsert({time::Tick{tick}, static_cast<float>(cents), interpolation});
  };
  double value = 0.0, slope = 0.0;
  std::int64_t previousTick = 0;
  bool tickGridLoss = false;
  for (const auto& [tick, change] : events) {
    const auto left = value + slope * static_cast<double>(tick - previousTick);
    if (tick > 0 && std::abs(change.valueDelta) > 1e-7) {
      const auto guard = emit(tick - 1, left - slope, domain::CurveInterpolation::Step);
      if (!guard) return core::Result<Output>{guard.error()};
      tickGridLoss = true;
    }
    value = left + change.valueDelta;
    slope += change.slopeDelta;
    previousTick = tick;
    const auto added = emit(tick, value, domain::CurveInterpolation::Linear);
    if (!added) return core::Result<Output>{added.error()};
  }
  for (auto& issue : compositionIssues)
    addIssue(issues, issue.severity, std::move(issue.path), std::move(issue.message), limits);
  if (maximumOverlap > 0)
    addIssue(issues, UstxIssueSeverity::Loss, partPath + ".pitch.approximation",
             "sine/ease pitch was linearized with a total 0.25-cent analytic interpolation budget at 960 PPQ ticks; floating-point, endpoint rounding, sub-tick/frame sampling and OpenUtau's 5-tick render grid are additional differences", limits);
  if (tickGridLoss)
    addIssue(issues, UstxIssueSeverity::Loss, partPath + ".pitch.tick_grid",
             "pitch discontinuities were materialized on adjacent 960 PPQ ticks; sub-tick contours are approximated", limits);
  return Output{std::move(pitch)};
}

}  // namespace

core::Result<UstxProjectDraft> importUstxProject(
    std::span<const std::uint8_t> bytes, application::ProjectFactory& factory,
    UstxImportRequest request, UstxLimits limits) {
  using Output = UstxProjectDraft;
  const auto decoded = decodeUstx(bytes, limits);
  if (!decoded) return core::Result<Output>{decoded.error()};
  const auto& document = decoded.value();
  if (request.projectName.empty()) request.projectName = document.name.empty() ? "Imported USTX" : document.name;
  if (request.projectName.size() > limits.maximumScalarBytes || request.voicebankId.size() > limits.maximumScalarBytes || request.characterId.size() > limits.maximumScalarBytes)
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "USTX import identity is oversized");
  auto project = factory.createProject(std::move(request.projectName));
  std::vector<UstxIssue> issues = document.issues;
  for (const auto& tempo : document.tempos) {
    auto tick = scaleTick(tempo.position.value(), kUstxPpq, kSeamPpq, "ustx.tempos.position", issues, limits);
    if (!tick) return core::Result<Output>{tick.error()};
    const auto added = project.tempoMap().addOrReplace(time::Tick{tick.value()}, tempo.bpm);
    if (!added) return core::Result<Output>{added.error()};
  }
  // ProjectFactory starts with a valid default meter.  Compute each USTX bar
  // from the previous meter so changes remain at their musical bar boundary.
  std::int64_t currentBar = 0;
  std::int64_t currentTick = 0;
  std::uint8_t currentNumerator = 4U;
  std::uint8_t currentDenominator = 4U;
  for (const auto& meter : document.meters) {
    const auto bars = meter.barPosition - currentBar;
    const auto beat = static_cast<std::int64_t>(kUstxPpq) * 4 / currentDenominator;
    if (bars < 0 || bars > (std::numeric_limits<std::int64_t>::max() - currentTick) / (beat * currentNumerator))
      return core::failure<Output>(core::ErrorCode::InvalidArgument, "USTX meter map overflows the canonical tick range");
    const auto tick = currentTick + bars * beat * currentNumerator;
    if (tick > std::numeric_limits<std::int64_t>::max() / 2)
      return core::failure<Output>(core::ErrorCode::InvalidArgument, "USTX meter map cannot be scaled to 960 PPQ");
    const auto added = project.meterMap().addOrReplace(time::Tick{tick * 2}, meter.numerator, meter.denominator);
    if (!added) return core::Result<Output>{added.error()};
    currentBar = meter.barPosition;
    currentTick = tick;
    currentNumerator = meter.numerator;
    currentDenominator = meter.denominator;
  }
  std::vector<domain::TrackId> trackIds;
  trackIds.reserve(document.tracks.size());
  for (const auto& source : document.tracks) {
    const auto trackId = factory.addVocalTrack(project, source.name.empty() ? "Track" : source.name);
    auto* track = project.findVocalTrack(trackId);
    if (!track) return core::failure<Output>(core::ErrorCode::InvariantViolation, "USTX import track was not created");
    track->voicebank = {request.voicebankId, request.voicebankVersion, request.voicebankContentHash};
    track->character = {request.characterId, request.characterVersion};
    track->gainDb = static_cast<float>(source.volume);
    track->pan = static_cast<float>(source.pan);
    track->muted = source.mute;
    track->solo = source.solo;
    trackIds.push_back(trackId);
    if (!source.voiceColors.empty() && std::any_of(source.voiceColors.begin(), source.voiceColors.end(), [](const auto& value) { return !value.empty(); }))
      addIssue(issues, UstxIssueSeverity::Loss, "ustx.tracks[" + std::to_string(trackIds.size() - 1U) + "].voice_color_names", "voice colors require an explicit SEAM style choice and were not imported", limits);
  }
  for (std::size_t partIndex = 0U; partIndex < document.parts.size(); ++partIndex) {
    const auto& source = document.parts[partIndex];
    if (source.trackNo >= trackIds.size()) return core::failure<Output>(core::ErrorCode::ParseError, "USTX part references an unavailable track");
    auto partStart = scaleTick(source.position.value(), kUstxPpq, kSeamPpq, "ustx.voice_parts.position", issues, limits); if (!partStart) return core::Result<Output>{partStart.error()};
    auto partDuration = scaleTick(source.duration.value(), kUstxPpq, kSeamPpq, "ustx.voice_parts.duration", issues, limits); if (!partDuration) return core::Result<Output>{partDuration.error()};
    const auto regionId = factory.addRegion(project, trackIds[source.trackNo], source.name.empty() ? "Voice Part" : source.name, time::Tick{partStart.value()}, time::Tick{partDuration.value()});
    auto* region = project.findRegion(regionId);
    if (!region) return core::failure<Output>(core::ErrorCode::InvariantViolation, "USTX import region was not created");
    importDynamics(source, *region,
                   "ustx.voice_parts[" + std::to_string(partIndex) + "]", issues, limits);
    auto composed = composePitch(source, document.tempos,
        "ustx.voice_parts[" + std::to_string(partIndex) + "]", issues, limits);
    if (!composed) return core::Result<Output>{composed.error()};
    // Query the latest end of every preceding note without scanning the whole
    // part per pitch point. Input note order need not be chronological.
    std::vector<std::pair<std::int64_t, std::int64_t>> priorNoteEnds;
    priorNoteEnds.reserve(source.notes.size());
    for (const auto& sourceNote : source.notes)
      priorNoteEnds.emplace_back(sourceNote.position.value(), sourceNote.position.value() + sourceNote.duration.value());
    std::sort(priorNoteEnds.begin(), priorNoteEnds.end());
    for (std::size_t index = 1U; index < priorNoteEnds.size(); ++index)
      priorNoteEnds[index].second = std::max(priorNoteEnds[index - 1U].second, priorNoteEnds[index].second);
    for (std::size_t noteIndex = 0U; noteIndex < source.notes.size(); ++noteIndex) {
      const auto& inputNote = source.notes[noteIndex];
      const auto notePath = "ustx.voice_parts[" + std::to_string(partIndex) + "].notes[" + std::to_string(noteIndex) + "]";
      if (inputNote.tuning != 0.0)
        addIssue(issues, UstxIssueSeverity::Loss, notePath + ".tuning",
                 "note tuning is included in the imported pitch contour but its separate edit control is not retained", limits);
      const auto earlier = std::lower_bound(priorNoteEnds.begin(), priorNoteEnds.end(), inputNote.position.value(),
                                            [](const auto& entry, std::int64_t position) { return entry.first < position; });
      const auto priorEnd = earlier == priorNoteEnds.begin() ? -1 : (earlier - 1)->second;
      auto noteStart = scaleTick(inputNote.position.value(), kUstxPpq, kSeamPpq, "ustx.voice_parts.notes.position", issues, limits); if (!noteStart) return core::Result<Output>{noteStart.error()};
      auto noteDuration = scaleTick(inputNote.duration.value(), kUstxPpq, kSeamPpq, "ustx.voice_parts.notes.duration", issues, limits); if (!noteDuration) return core::Result<Output>{noteDuration.error()}; if (noteDuration.value() <= 0) return core::failure<Output>(core::ErrorCode::InvalidArgument, "USTX note duration rounded to zero");
      const auto lyric = domain::fromUtf8(inputNote.lyric); if (!lyric) return core::Result<Output>{lyric.error()};
      auto [token, note] = factory.makeNote(time::Tick{noteStart.value()}, time::Tick{noteDuration.value()}, inputNote.tone, lyric.value(), request.language);
      if (inputNote.hasVibrato && inputNote.vibrato.length > 0.0) {
        note.vibrato.enabled = true;
        note.vibrato.startFraction = static_cast<float>(std::clamp(1.0 - inputNote.vibrato.length / 100.0, 0.0, 1.0));
        note.vibrato.fadeInFraction = static_cast<float>(std::clamp(inputNote.vibrato.fadeIn / 100.0, 0.0, 1.0));
        note.vibrato.fadeOutFraction = static_cast<float>(std::clamp(inputNote.vibrato.fadeOut / 100.0, 0.0, 1.0));
        const auto normalized = [&](double value, double minimum, double maximum,
                                    std::string_view field) {
          const auto clamped = std::clamp(value, minimum, maximum);
          if (clamped != value)
            addIssue(issues, UstxIssueSeverity::Loss, notePath + ".vibrato." + std::string(field),
                     "vibrato value was clamped to the OpenUtau range", limits);
          return clamped;
        };
        // OpenUtau UVibrato uses cents directly and a percentage of one period
        // for shift. PitchPoint.Y has different units (tenths of a semitone).
        note.vibrato.depthCents = static_cast<float>(normalized(inputNote.vibrato.depth, 5.0, 200.0, "depth"));
        note.vibrato.periodMilliseconds = static_cast<float>(normalized(inputNote.vibrato.period, 5.0, 500.0, "period"));
        note.vibrato.phaseTurns = static_cast<float>(std::fmod(normalized(inputNote.vibrato.shift, 0.0, 100.0, "shift") / 100.0, 1.0));
        if (inputNote.vibrato.drift != 0.0)
          addIssue(issues, UstxIssueSeverity::Loss, notePath + ".vibrato.drift",
                   "vibrato pitch drift is not represented in SEAM", limits);
        if (inputNote.vibrato.volumeLink != 0.0)
          addIssue(issues, UstxIssueSeverity::Loss, notePath + ".vibrato.vol_link",
                   "vibrato-linked volume modulation is not represented in SEAM", limits);
        if (note.vibrato.fadeInFraction + note.vibrato.fadeOutFraction > 1.0F) { note.vibrato.fadeOutFraction = 1.0F - note.vibrato.fadeInFraction; addIssue(issues, UstxIssueSeverity::Warning, "ustx.voice_parts[" + std::to_string(partIndex) + "].notes[" + std::to_string(noteIndex) + "].vibrato", "vibrato fades were clamped to the SEAM span", limits); }
      }
      region->lyrics.push_back(std::move(token));
      region->notes.push_back(std::move(note));
      if (source.position.value() > limits.maximumTick - inputNote.position.value() ||
          source.position.value() + inputNote.position.value() > limits.maximumTick - inputNote.duration.value())
        return core::failure<Output>(core::ErrorCode::InvalidArgument, "USTX note absolute tick overflows the configured limit");
      const auto absoluteNoteTick = source.position.value() + inputNote.position.value();
      const auto noteEnd = absoluteNoteTick + inputNote.duration.value();
      if (composed.value().has_value()) continue;
      if (inputNote.pitch.empty()) {
        const auto start = scaleTick(inputNote.position.value(), kUstxPpq, kSeamPpq, notePath + ".position", issues, limits);
        const auto end = scaleTick(inputNote.position.value() + inputNote.duration.value(), kUstxPpq, kSeamPpq, notePath + ".duration", issues, limits);
        if (!start || !end) return core::Result<Output>{(!start ? start.error() : end.error())};
        const auto first = region->pitchAutomation.upsert({time::Tick{start.value()}, static_cast<float>(inputNote.tuning), domain::CurveInterpolation::Linear});
        const auto last = region->pitchAutomation.upsert({time::Tick{end.value()}, static_cast<float>(inputNote.tuning), domain::CurveInterpolation::Linear});
        if (!first || !last) return core::Result<Output>{(!first ? first.error() : last.error())};
      } else {
        for (std::size_t pointIndex = 0U; pointIndex < inputNote.pitch.size(); ++pointIndex) {
          const auto& point = inputNote.pitch[pointIndex];
          const auto target = tickAtSeconds(document.tempos, secondsAt(document.tempos, absoluteNoteTick) + point.offsetMilliseconds / 1000.0);
          if (target < source.position.value() || target > source.position.value() + source.duration.value()) {
            addIssue(issues, UstxIssueSeverity::Loss, notePath + ".pitch[" + std::to_string(pointIndex) + "]", "pitch point falls outside its part and was omitted", limits); continue;
          }
          if (target > noteEnd) {
            addIssue(issues, UstxIssueSeverity::Loss, notePath + ".pitch[" + std::to_string(pointIndex) + "]", "pitch point falls after its note and was omitted", limits); continue;
          }
          if (target < absoluteNoteTick && target - source.position.value() <= priorEnd) {
            // OpenUtau composes overlapping note curves in absolute pitch,
            // subtracting the preceding note's base tone. A single region
            // cents curve cannot reproduce that by copying one negative-X
            // point. Rest pickups are safe; cross-note ramps need composition.
            addIssue(issues, UstxIssueSeverity::Loss, notePath + ".pitch[" + std::to_string(pointIndex) + "]",
                     "cross-note portamento requires absolute pitch curve composition and was omitted", limits); continue;
          }
          auto relative = scaleTick(target - source.position.value(), kUstxPpq, kSeamPpq, notePath + ".pitch[" + std::to_string(pointIndex) + "]", issues, limits); if (!relative) return core::Result<Output>{relative.error()};
          const auto interpolation = interpolationFor(point.shape, notePath + ".pitch[" + std::to_string(pointIndex) + "].shape", issues, limits);
          const auto result = region->pitchAutomation.upsert({time::Tick{relative.value()}, static_cast<float>(inputNote.tuning + point.y * 10.0), interpolation});
          if (!result) return core::Result<Output>{result.error()};
        }
      }
      if (inputNote.snapFirst) addIssue(issues, UstxIssueSeverity::Warning, notePath + ".pitch.snap_first", "snap_first is retained only as imported pitch points; neighboring-note materialization is not repeated", limits);
    }
    if (composed.value().has_value()) {
      region->pitchAutomation = std::move(*composed.value());
      // This curve already composes the source's absolute pitch, represented
      // as offsets from each active score note. It owns pitch, including
      // portamento; applying SEAM's automatic continuation glide as well
      // would apply the note-base transition twice. Persist that authority
      // through the existing manual-performance contract, not import flags.
      region->performance.ownership.push_back({domain::PerformanceChannel::Pitch,
          domain::PerformanceTimeRange{time::Tick{0}, region->durationTick},
          domain::ManualPerformanceMode::Replace, region->performance.revision});
    }
    region->sortNotes();
  }
  const auto valid = project.validate(); if (!valid) return core::Result<Output>{valid.error()};
  if (issues.size() > std::min<std::size_t>(limits.maximumNodes, 4'096U))
    return core::failure<Output>(core::ErrorCode::Unsupported,
        "USTX diagnostic report exceeds its capacity; conversion would conceal losses");
  return Output{std::move(project), std::move(issues)};
}

core::Result<UstxExportResult> exportUstxProject(const domain::Project& project, UstxLimits limits) {
  using Output = UstxExportResult;
  const auto valid = project.validate(); if (!valid) return core::Result<Output>{valid.error()};
  UstxDocument document; document.name = project.name();
  std::vector<UstxIssue> issues;
  for (const auto& tempo : project.tempoMap().events()) {
    auto tick = scaleTick(tempo.tick.value(), project.ppq(), kUstxPpq, "project.tempoMap.tick", issues, limits); if (!tick) return core::Result<Output>{tick.error()};
    document.tempos.push_back({time::Tick{tick.value()}, tempo.bpm});
  }
  std::int64_t previousBar = 0;
  for (const auto& meter : project.meterMap().events()) {
    const auto barBeat = project.meterMap().barBeatAt(meter.tick);
    const auto bar = std::max<std::int64_t>(0, barBeat.bar - 1);
    if (!document.meters.empty() && bar <= previousBar) { addIssue(issues, UstxIssueSeverity::Loss, "project.meterMap", "meter change is not representable at a unique USTX bar and was omitted", limits); continue; }
    document.meters.push_back({bar, meter.numerator, meter.denominator}); previousBar = bar;
  }
  if (document.meters.empty()) document.meters.push_back({0, 4U, 4U});
  for (std::size_t trackNumber = 0U; trackNumber < project.vocalTracks().size(); ++trackNumber) {
    const auto& track = project.vocalTracks()[trackNumber];
    document.tracks.push_back({track.name, track.voicebank.id, track.gainDb, track.pan, track.muted, track.solo, {""}});
    if (!track.voicebank.id.empty() || !track.character.id.empty() || !track.styleSelection.styleId.empty() || track.proceduralRecipe.has_value()) addIssue(issues, UstxIssueSeverity::Loss, "project.vocalTracks[" + std::to_string(trackNumber) + "].voicebank", "SEAM singer/style identity is not executable in USTX 0.9 and was emitted as an inert label", limits);
    if (!track.outputRoute.matrix.gains.empty()) addIssue(issues, UstxIssueSeverity::Loss, "project.vocalTracks[" + std::to_string(trackNumber) + "].outputRoute", "SEAM bus routing is not represented in USTX", limits);
    for (std::size_t regionNumber = 0U; regionNumber < track.regions.size(); ++regionNumber) {
      const auto& region = track.regions[regionNumber];
      auto partPosition = scaleTick(region.startTick.value(), project.ppq(), kUstxPpq, "project.vocalTracks.regions.startTick", issues, limits); if (!partPosition) return core::Result<Output>{partPosition.error()};
      auto partDuration = scaleTick(region.durationTick.value(), project.ppq(), kUstxPpq, "project.vocalTracks.regions.durationTick", issues, limits); if (!partDuration) return core::Result<Output>{partDuration.error()};
      UstxPart part{region.name, static_cast<std::uint32_t>(trackNumber), time::Tick{partPosition.value()}, time::Tick{partDuration.value()}, {}, {}};
      const auto dynamicsPath = "project.vocalTracks[" + std::to_string(trackNumber) +
          "].regions[" + std::to_string(regionNumber) + "].dynamics";
      bool dynamicsQuantized = false;
      bool dynamicsSparse = false;
      bool dynamicsRoundedTicks = false;
      const double minimumAudible = std::pow(10.0, -239.0 / 200.0);
      for (const auto& point : region.dynamicsAutomation.points()) {
        auto tick = scaleTick(point.tick.value(), project.ppq(), kUstxPpq,
                              dynamicsPath, issues, limits);
        if (!tick) return core::Result<Output>{tick.error()};
        const auto boundedTick = std::min(tick.value(), part.duration.value());
        if (boundedTick != tick.value()) dynamicsRoundedTicks = true;
        const double decibels = static_cast<double>(point.linearGain) <= minimumAudible / 2.0 ? -240.0 :
            std::max(-239.0, 200.0 * std::log10(static_cast<double>(point.linearGain)));
        const auto rounded = static_cast<std::int16_t>(std::clamp(std::llround(decibels), -240LL, 120LL));
        const double reconstructed = rounded == -240 ? 0.0 :
            std::pow(10.0, static_cast<double>(rounded) / 200.0);
        if (static_cast<float>(reconstructed) != point.linearGain) dynamicsQuantized = true;
        if (!part.dynamics.empty() && boundedTick <= part.dynamics.back().position.value()) {
          part.dynamics.back().tenthDecibels = rounded;
          dynamicsRoundedTicks = true;
        } else {
          if (!part.dynamics.empty() &&
              boundedTick - part.dynamics.back().position.value() > 5)
            dynamicsSparse = true;
          part.dynamics.push_back({time::Tick{boundedTick}, rounded});
        }
      }
      if (dynamicsQuantized)
        addIssue(issues, UstxIssueSeverity::Loss, dynamicsPath,
                 "linear gain was quantized to OpenUtau's 0.1 dB dynamics units", limits);
      if (dynamicsSparse)
        addIssue(issues, UstxIssueSeverity::Loss, dynamicsPath,
                 "sparse SEAM linear-gain interpolation differs from OpenUtau's decibel interpolation between curve points", limits);
      if (dynamicsRoundedTicks)
        addIssue(issues, UstxIssueSeverity::Loss, dynamicsPath,
                 "dynamics points collided or exceeded the USTX part after tick rounding", limits);
      std::map<domain::LyricTokenId, const domain::LyricToken*> lyrics;
      for (const auto& lyric : region.lyrics) lyrics.emplace(lyric.id, &lyric);
      // A pitch point in a rest belongs to the next note as a negative-X
      // pickup. Resolve that ownership before writing notes so storage order
      // does not matter, and never attach it across an intervening note span.
      struct PitchNoteSpan final {
        time::Tick start;
        time::Tick latestEnd;
        std::size_t noteIndex;
      };
      std::vector<PitchNoteSpan> pitchSpans;
      pitchSpans.reserve(region.notes.size());
      for (std::size_t index = 0U; index < region.notes.size(); ++index)
        pitchSpans.push_back({region.notes[index].startTick, region.notes[index].endTick(), index});
      std::sort(pitchSpans.begin(), pitchSpans.end(), [](const auto& left, const auto& right) {
        return left.start != right.start ? left.start < right.start : left.noteIndex < right.noteIndex;
      });
      for (std::size_t index = 1U; index < pitchSpans.size(); ++index)
        pitchSpans[index].latestEnd = std::max(pitchSpans[index - 1U].latestEnd, pitchSpans[index].latestEnd);
      const auto& pitchPoints = region.pitchAutomation.points();
      std::vector<std::size_t> pickupOwners(pitchPoints.size(), region.notes.size());
      for (std::size_t index = 0U; index < pitchPoints.size(); ++index) {
        const auto next = std::upper_bound(pitchSpans.begin(), pitchSpans.end(), pitchPoints[index].tick,
                                           [](time::Tick tick, const auto& span) { return tick < span.start; });
        if (next != pitchSpans.begin() && (next - 1)->latestEnd >= pitchPoints[index].tick) continue;
        const auto path = "project.vocalTracks[" + std::to_string(trackNumber) + "].regions[" + std::to_string(regionNumber) + "].pitch[" + std::to_string(index) + "]";
        if (next == pitchSpans.end()) {
          addIssue(issues, UstxIssueSeverity::Loss, path, "pitch point after the last note has no USTX note owner and was omitted", limits);
        } else if (next + 1 != pitchSpans.end() && (next + 1)->start == next->start) {
          addIssue(issues, UstxIssueSeverity::Loss, path, "rest pickup precedes simultaneous notes with ambiguous ownership and was omitted", limits);
        } else {
          pickupOwners[index] = next->noteIndex;
        }
      }
      for (std::size_t noteNumber = 0U; noteNumber < region.notes.size(); ++noteNumber) {
        const auto& note = region.notes[noteNumber];
        const auto notePath = "project.vocalTracks[" + std::to_string(trackNumber) + "].regions[" + std::to_string(regionNumber) + "].notes[" + std::to_string(noteNumber) + "]";
        const auto lyric = lyrics.find(note.lyricTokenId);
        if (lyric == lyrics.end()) return core::failure<Output>(core::ErrorCode::InvariantViolation, "USTX export note references a missing lyric", note.id.toString());
        auto notePosition = scaleTick(note.startTick.value(), project.ppq(), kUstxPpq, "project.note.startTick", issues, limits); if (!notePosition) return core::Result<Output>{notePosition.error()};
        auto noteDuration = scaleTick(note.durationTick.value(), project.ppq(), kUstxPpq, "project.note.durationTick", issues, limits); if (!noteDuration) return core::Result<Output>{noteDuration.error()};
        UstxNote exported{time::Tick{notePosition.value()}, time::Tick{noteDuration.value()}, note.midiKey, domain::toUtf8(lyric->second->surface), 0.0, {}, false, {}, false};
        for (std::size_t pointIndex = 0U; pointIndex < pitchPoints.size(); ++pointIndex) {
          const auto& point = pitchPoints[pointIndex];
          if (point.tick > note.endTick() || (point.tick < note.startTick && pickupOwners[pointIndex] != noteNumber)) continue;
          const auto absolutePoint = region.startTick + point.tick;
          const auto absoluteNote = region.startTick + note.startTick;
          const auto milliseconds = (project.tempoMap().secondsAt(absolutePoint) - project.tempoMap().secondsAt(absoluteNote)) * 1000.0;
          if (!std::isfinite(milliseconds)) continue;
          exported.pitch.push_back({milliseconds, static_cast<double>(point.cents) / 10.0,
                                    shapeFor(point.interpolation, notePath + ".pitch[" + std::to_string(exported.pitch.size()) + "].shape", issues, limits)});
        }
        if (note.vibrato.enabled) {
          exported.hasVibrato = true;
          exported.vibrato.length = std::clamp((1.0 - static_cast<double>(note.vibrato.startFraction)) * 100.0, 0.0, 100.0);
          exported.vibrato.period = note.vibrato.periodMilliseconds;
          exported.vibrato.depth = std::max(5.0, static_cast<double>(note.vibrato.depthCents));
          if (note.vibrato.depthCents < 5.0F)
            addIssue(issues, UstxIssueSeverity::Loss, notePath + ".vibrato.depth",
                     "vibrato depth was raised to OpenUtau's minimum of 5 cents", limits);
          exported.vibrato.fadeIn = static_cast<double>(note.vibrato.fadeInFraction) * 100.0;
          exported.vibrato.fadeOut = static_cast<double>(note.vibrato.fadeOutFraction) * 100.0;
          exported.vibrato.shift = static_cast<double>(note.vibrato.phaseTurns) * 100.0;
        }
        part.notes.push_back(std::move(exported));
        if (note.articulation != domain::NoteArticulation::Normal || note.slurGroup.has_value() || note.phoneticHint.has_value()) addIssue(issues, UstxIssueSeverity::Loss, "project.note[" + std::to_string(noteNumber) + "]", "SEAM articulation/slur/phonetic hint is not represented in USTX", limits);
      }
      if (!region.phonemeOverrides.empty() || !region.unitSelectionOverrides.empty() || !region.seamOverrides.empty() || !region.formantAutomation.points().empty() || !region.performance.ownership.empty() || !region.performance.takes.empty() || !region.performance.accepted.empty()) addIssue(issues, UstxIssueSeverity::Loss, "project.vocalTracks.regions[" + std::to_string(regionNumber) + "]", "SEAM phoneme, formant, manual-ownership and generated-performance metadata is not represented in USTX", limits);
      const auto requiredDuration = part.notes.empty() ? 0 : std::max_element(part.notes.begin(), part.notes.end(), [](const auto& lhs, const auto& rhs) { return lhs.position + lhs.duration < rhs.position + rhs.duration; })->position.value() + std::max_element(part.notes.begin(), part.notes.end(), [](const auto& lhs, const auto& rhs) { return lhs.position + lhs.duration < rhs.position + rhs.duration; })->duration.value();
      if (part.duration.value() < requiredDuration) { part.duration = time::Tick{requiredDuration}; addIssue(issues, UstxIssueSeverity::Warning, "project.vocalTracks.regions.durationTick", "part duration was extended to contain all rounded notes", limits); }
      document.parts.push_back(std::move(part));
    }
  }
  if (!project.audioTracks().empty()) addIssue(issues, UstxIssueSeverity::Loss, "project.audioTracks", "USTX export contains vocal tracks only", limits);
  if (issues.size() > std::min<std::size_t>(limits.maximumNodes, 4'096U))
    return core::failure<Output>(core::ErrorCode::Unsupported,
        "USTX diagnostic report exceeds its capacity; conversion would conceal losses");
  const auto encoded = encodeUstx(document, limits); if (!encoded) return core::Result<Output>{encoded.error()};
  return Output{std::move(encoded).value(), std::move(issues)};
}

}  // namespace seam::interchange
