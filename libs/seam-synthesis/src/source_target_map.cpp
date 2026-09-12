#include "seam/synthesis/source_target_map.hpp"
#include <algorithm>
#include <set>
#include <cmath>
#include <limits>

namespace seam::synthesis {
core::Result<SourceTargetMap> compileShortUnitMarkerMap(
    const voicebank::Unit& unit, time::SampleFrame start, time::SampleFrame vowel,
    time::SampleFrame end, std::uint32_t sourceRate, std::uint32_t outputRate,
    time::SampleFrame decodedFrames) {
  if (decodedFrames <= 0 || decodedFrames > 32LL * 1024LL * 1024LL ||
      sourceRate < 8000U || sourceRate > 384000U || outputRate < 8000U || outputRate > 384000U ||
      start < -(1LL << 52) || end > (1LL << 52) || start > vowel || vowel >= end ||
      end - start > 32LL * 1024LL * 1024LL)
    return core::failure<SourceTargetMap>(core::ErrorCode::InvalidArgument, "Short-unit map exceeds frame/rate bounds");
  const auto valid = unit.validate(); if (!valid) return core::Result<SourceTargetMap>{valid.error()};
  const auto markers = unit.markers.validate(decodedFrames); if (!markers) return core::Result<SourceTargetMap>{markers.error()};
  if (!((unit.kind == voicebank::UnitKind::Cv && unit.phones.size() == 2U) ||
        (unit.kind == voicebank::UnitKind::Sustain && unit.phones.size() == 1U)))
    return core::failure<SourceTargetMap>(core::ErrorCode::Unsupported, "Short marker mapping requires a simple CV or sustain unit");
  const auto& m = unit.markers;
  const auto release = m.releaseStart.value_or(m.audioEnd);
  if (m.vowelOnset < m.audioOffset || m.stableStart <= m.vowelOnset ||
      release <= m.stableStart || release >= m.audioEnd ||
      ((m.vowelOnset == m.audioOffset) != (vowel == start)))
    return core::failure<SourceTargetMap>(core::ErrorCode::Conflict, "Short-unit landmarks cannot preserve onset, sustain and release");
  const auto voicedFrames = end - vowel;
  if (voicedFrames < 3)
    return core::failure<SourceTargetMap>(core::ErrorCode::Conflict, "Short-unit span requires transition, sustain and release frames");
  const auto scaled = [&](time::SampleFrame frames) {
    return (frames * static_cast<time::SampleFrame>(outputRate) + sourceRate / 2U) / sourceRate;
  };
  const auto transition = std::max<time::SampleFrame>(1, std::min(scaled(m.stableStart - m.vowelOnset), voicedFrames / 3));
  const auto tail = std::max<time::SampleFrame>(1, std::min(scaled(m.audioEnd - release), voicedFrames / 3));
  SourceTargetMap map;
  map.knots.push_back({m.audioOffset, start});
  if (m.vowelOnset != m.audioOffset) map.knots.push_back({m.vowelOnset, vowel});
  map.knots.push_back({m.stableStart, vowel + transition});
  map.knots.push_back({release, end - tail});
  map.knots.push_back({m.audioEnd, end});
  const auto checked = map.validate(decodedFrames); if (!checked) return core::Result<SourceTargetMap>{checked.error()};
  return core::success(std::move(map));
}

core::Result<void> SourceTargetMap::validate(time::SampleFrame sourceFrames) const {
  if (sourceFrames <= 0 || sourceFrames > 32LL * 1024LL * 1024LL || knots.size() < 2U || knots.size() > 770U) {
    return core::failure(core::ErrorCode::InvalidArgument, "Source map exceeds supported bounds");
  }
  for (std::size_t i = 0; i < knots.size(); ++i) {
    if (knots[i].sourceFrame < 0 || knots[i].sourceFrame > sourceFrames ||
        knots[i].targetFrame < -(1LL << 52) || knots[i].targetFrame > (1LL << 52) ||
        (i && (knots[i].sourceFrame <= knots[i - 1U].sourceFrame || knots[i].targetFrame <= knots[i - 1U].targetFrame))) {
      return core::failure(core::ErrorCode::Conflict, "Source map requires ordered in-bounds landmarks");
    }
  }
  const auto extent = static_cast<std::uint64_t>(knots.back().targetFrame) - static_cast<std::uint64_t>(knots.front().targetFrame);
  if (!extent || extent > 32ULL * 1024ULL * 1024ULL) return core::failure(core::ErrorCode::InvalidArgument, "Source map output exceeds bounds");
  if (voicing.size() > 256U) return core::failure(core::ErrorCode::InvalidArgument, "Source voicing exceeds bounds");
  for (std::size_t i = 0; i < voicing.size(); ++i) {
    if (voicing[i].start >= voicing[i].end || voicing[i].start != (i ? voicing[i - 1U].end : knots.front().sourceFrame) ||
        voicing[i].end > knots.back().sourceFrame) return core::failure(core::ErrorCode::Conflict, "Source voicing must cover ordered contiguous spans");
  }
  if (!voicing.empty() && voicing.back().end != knots.back().sourceFrame) return core::failure(core::ErrorCode::Conflict, "Source voicing coverage is incomplete");
  return {};
}
std::optional<bool> SourceTargetMap::voicedAtSource(double frame) const noexcept {
  if (!std::isfinite(frame)) return std::nullopt;
  const auto next = std::upper_bound(voicing.begin(), voicing.end(), frame,
      [](double value, const auto& span) { return value < static_cast<double>(span.start); });
  if (next == voicing.begin()) return std::nullopt;
  const auto& span = *std::prev(next);
  return frame < static_cast<double>(span.end) ? span.voiced : std::nullopt;
}
namespace {
double evaluateMap(const SourceTargetMap& map, double frame, bool sourceToTarget) noexcept {
  if (map.knots.empty() || !std::isfinite(frame)) return 0.0;
  const auto x = [&](const auto& knot) { return static_cast<double>(sourceToTarget ? knot.sourceFrame : knot.targetFrame); };
  const auto y = [&](const auto& knot) { return static_cast<double>(sourceToTarget ? knot.targetFrame : knot.sourceFrame); };
  if (frame <= x(map.knots.front())) return y(map.knots.front());
  const auto next = std::upper_bound(map.knots.begin(), map.knots.end(), frame,
      [&](double value, const auto& knot) { return value < x(knot); });
  if (next == map.knots.end()) return y(map.knots.back());
  const auto& previous = *std::prev(next);
  const auto width = x(*next) - x(previous);
  return width > 0.0 ? y(previous) + (frame - x(previous)) / width * (y(*next) - y(previous)) : y(previous);
}
}
double SourceTargetMap::sourceAt(double frame) const noexcept { return evaluateMap(*this, frame, false); }
double SourceTargetMap::targetAt(double frame) const noexcept { return evaluateMap(*this, frame, true); }

core::Result<SourceTargetMap> compileSourceTargetMap(
    const SourcePhonemeAlignment& alignment, const voicebank::Unit& unit,
    const TimedUnitPlacement& placement, std::string_view verifiedAudioSha256,
    time::SampleFrame decodedFrames) {
  const auto valid = alignment.validate(unit, verifiedAudioSha256, decodedFrames);
  if (!valid) return core::Result<SourceTargetMap>{valid.error()};
  const auto& targets = placement.phonemeTargets;
  if (placement.unitId != unit.id || targets.size() != alignment.landmarks.size() ||
      placement.tokenCount != targets.size() || targets.empty() || placement.startKey != targets.front().key ||
      placement.destinationEnd <= placement.destinationStart ||
      static_cast<std::uint64_t>(placement.destinationEnd) - static_cast<std::uint64_t>(placement.destinationStart) > 100'000'000ULL) {
    return core::failure<SourceTargetMap>(core::ErrorCode::Conflict, "Source alignment and target coverage disagree", unit.id);
  }
  SourceTargetMap map{{{unit.markers.audioOffset, placement.destinationStart}, {unit.markers.audioEnd, placement.destinationEnd}}};
  std::set<domain::PhonemeKey> keys;
  for (std::size_t i = 0U; i < targets.size(); ++i) {
    const auto& target = targets[i];
    map.voicing.push_back({i ? alignment.landmarks[i].frame : unit.markers.audioOffset,
        i + 1U < targets.size() ? alignment.landmarks[i + 1U].frame : unit.markers.audioEnd, target.voiced});
    if (!keys.insert(target.key).second) return core::failure<SourceTargetMap>(core::ErrorCode::Conflict, "Repeated source-map target key", unit.id);
    if (target.explicitStartFrame) map.knots.push_back({alignment.landmarks[i].frame, *target.explicitStartFrame});
    if (target.nucleusKey && *target.nucleusKey == target.key) map.knots.push_back({alignment.landmarks[i].frame, target.nucleusFrame});
    if (target.endExplicit) map.knots.push_back({i + 1U < targets.size() ? alignment.landmarks[i + 1U].frame : unit.markers.audioEnd, target.endFrame});
  }
  std::sort(map.knots.begin(), map.knots.end(), [](const auto& a, const auto& b) {
    return a.sourceFrame == b.sourceFrame ? a.targetFrame < b.targetFrame : a.sourceFrame < b.sourceFrame;
  });
  map.knots.erase(std::unique(map.knots.begin(), map.knots.end()), map.knots.end());
  for (std::size_t i = 0U; i < map.knots.size(); ++i) {
    const auto& knot = map.knots[i];
    if (knot.targetFrame < placement.destinationStart || knot.targetFrame > placement.destinationEnd ||
        (i > 0U && (knot.sourceFrame <= map.knots[i - 1U].sourceFrame || knot.targetFrame <= map.knots[i - 1U].targetFrame))) {
      return core::failure<SourceTargetMap>(core::ErrorCode::Conflict, "Source-to-target landmarks cross or contradict a boundary", unit.id);
    }
  }
  return map;
}

core::Result<MappedSourceAudio> applySourceTargetMap(std::span<const float> source,
    const SourceTargetMap& map, std::stop_token stopToken) {
  constexpr std::uint64_t maximumFrames = 32ULL * 1024ULL * 1024ULL;
  if (source.empty() || source.size() > maximumFrames || map.knots.size() < 2U || map.knots.size() > 770U) {
    return core::failure<MappedSourceAudio>(core::ErrorCode::InvalidArgument, "Mapped audio input exceeds bounds");
  }
  const auto cancelled = [] { return core::failure<MappedSourceAudio>(core::ErrorCode::Conflict, "Source mapping was cancelled"); };
  if (stopToken.stop_requested()) return cancelled();
  for (std::size_t i = 0U; i < map.knots.size(); ++i) {
    const auto& knot = map.knots[i];
    if (knot.sourceFrame < 0 || static_cast<std::uint64_t>(knot.sourceFrame) > source.size() ||
        (i > 0U && (knot.sourceFrame <= map.knots[i - 1U].sourceFrame || knot.targetFrame <= map.knots[i - 1U].targetFrame))) {
      return core::failure<MappedSourceAudio>(core::ErrorCode::Conflict, "Mapped audio requires ordered in-bounds landmarks");
    }
  }
  const auto length = static_cast<std::uint64_t>(map.knots.back().targetFrame) - static_cast<std::uint64_t>(map.knots.front().targetFrame);
  if (length == 0U || length > maximumFrames) return core::failure<MappedSourceAudio>(core::ErrorCode::InvalidArgument, "Mapped audio output exceeds bounds");
  for (std::size_t i = 0U; i < source.size(); ++i) {
    if ((i & 4095U) == 0U && stopToken.stop_requested()) return cancelled();
    if (!std::isfinite(source[i])) return core::failure<MappedSourceAudio>(core::ErrorCode::InvalidArgument, "Mapped source audio contains non-finite samples");
  }
  MappedSourceAudio result{map.knots.front().targetFrame, std::vector<float>(static_cast<std::size_t>(length))};
  std::size_t segment = 0U;
  for (std::size_t i = 0U; i < result.samples.size(); ++i) {
    if ((i & 4095U) == 0U && stopToken.stop_requested()) return cancelled();
    const auto frame = result.startFrame + static_cast<time::SampleFrame>(i);
    while (segment + 2U < map.knots.size() && frame >= map.knots[segment + 1U].targetFrame) ++segment;
    const auto& a = map.knots[segment];
    const auto& b = map.knots[segment + 1U];
    const auto fraction = static_cast<double>(frame - a.targetFrame) / static_cast<double>(b.targetFrame - a.targetFrame);
    const auto position = static_cast<double>(a.sourceFrame) + fraction * static_cast<double>(b.sourceFrame - a.sourceFrame);
    const auto left = std::min(static_cast<std::size_t>(position), static_cast<std::size_t>(map.knots.back().sourceFrame - 1));
    const auto right = std::min(left + 1U, static_cast<std::size_t>(map.knots.back().sourceFrame - 1));
    const auto blend = std::clamp(position - static_cast<double>(left), 0.0, 1.0);
    result.samples[i] = static_cast<float>(static_cast<double>(source[left]) * (1.0 - blend) + static_cast<double>(source[right]) * blend);
  }
  return result;
}

core::Result<MappedSourceAudio> renderAlignedRawUnit(std::span<const float> source,
    const SourcePhonemeAlignment& alignment, const voicebank::Unit& unit,
    const TimedUnitPlacement& placement, std::string_view verifiedAudioSha256,
    std::stop_token stopToken) {
  if (source.size() > 32ULL * 1024ULL * 1024ULL) return core::failure<MappedSourceAudio>(core::ErrorCode::InvalidArgument, "Aligned raw source exceeds bounds");
  if (placement.targetMidi != unit.rootMidi) return core::failure<MappedSourceAudio>(core::ErrorCode::Unsupported,
      "Aligned raw mapping cannot satisfy requested pitch transposition", unit.id);
  const auto map = compileSourceTargetMap(alignment, unit, placement, verifiedAudioSha256,
      static_cast<time::SampleFrame>(source.size()));
  if (!map) return core::Result<MappedSourceAudio>{map.error()};
  auto rendered = applySourceTargetMap(source, map.value(), stopToken);
  if (!rendered) return rendered;
  const auto gain = std::pow(10.0, static_cast<double>(unit.gainDb) / 20.0);
  for (std::size_t i = 0U; i < rendered.value().samples.size(); ++i) {
    if ((i & 4095U) == 0U && stopToken.stop_requested()) return core::failure<MappedSourceAudio>(core::ErrorCode::Conflict, "Aligned raw rendering was cancelled");
    const auto value = static_cast<double>(rendered.value().samples[i]) * gain;
    if (!std::isfinite(value) || std::abs(value) > static_cast<double>(std::numeric_limits<float>::max())) {
      return core::failure<MappedSourceAudio>(core::ErrorCode::InvalidArgument, "Aligned raw gain produces non-finite samples", unit.id);
    }
    rendered.value().samples[i] = static_cast<float>(value);
  }
  return rendered;
}
}
