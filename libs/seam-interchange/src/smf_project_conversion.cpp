#include "seam/interchange/smf_project_conversion.hpp"

#include "seam/domain/note.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace seam::interchange {
namespace {

std::uint8_t denominatorPower(std::uint8_t denominator) noexcept {
  std::uint8_t power = 0U;
  while (denominator > 1U) { denominator = static_cast<std::uint8_t>(denominator / 2U); ++power; }
  return power;
}

using ImportedTimeline = std::map<time::Tick, time::Tick>;
using ImportedLyrics = std::map<time::Tick, const SmfText*>;

struct ImportLossCount final {
  std::size_t count{0U};
  time::Tick first;
  time::Tick last;

  void observe(time::Tick tick) noexcept {
    if (count == 0U) first = last = tick;
    else { first = std::min(first, tick); last = std::max(last, tick); }
    ++count;
  }
};

core::Result<ImportedLyrics> importLyricsAndDiscloseLosses(
    SmfScore& source, const SmfLimits& limits) {
  ImportLossCount velocities, channels, nonLyrics, unmatchedLyrics, extraLyrics;
  for (const auto& note : source.notes) {
    // These are the exact fixed defaults used by exportSmfProject. A source
    // already using them loses no distinct value when round-tripped.
    if (note.velocity != 100U) velocities.observe(note.start);
    if (note.channel != 0U) channels.observe(note.start);
  }
  ImportedLyrics lyrics;
  for (const auto& text : source.texts) {
    if (!text.lyric) { nonLyrics.observe(text.tick); continue; }
    const auto note = std::lower_bound(source.notes.begin(), source.notes.end(),
        text.tick, [](const SmfNote& item, time::Tick tick) { return item.start < tick; });
    if (note == source.notes.end() || note->start != text.tick) {
      unmatchedLyrics.observe(text.tick);
      continue;
    }
    if (lyrics.contains(text.tick)) { extraLyrics.observe(text.tick); continue; }
    if (lyrics.size() >= limits.maximumNotes)
      return core::failure<ImportedLyrics>(core::ErrorCode::InvalidArgument,
          "SMF lyric assignments exceed note bounds");
    // Borrow only the first lyric at each matched source onset. Appending
    // diagnostics cannot invalidate these pointers into the untouched texts.
    lyrics.emplace(text.tick, &text);
  }
  const std::array<std::pair<const ImportLossCount*, std::string_view>, 5U> losses{{
      {&velocities, "note-on velocities are not preserved; SEAM exports velocity 100"},
      {&channels, "note channel assignments are not preserved; SEAM exports channel 1"},
      {&nonLyrics, "non-lyric text events are not represented in the SEAM project"},
      {&unmatchedLyrics, "lyric events have no matching note onset and are not retained"},
      {&extraLyrics, "extra lyric events at assigned note onsets are not retained; the first source lyric is used"},
  }};
  for (const auto& [loss, description] : losses) {
    if (loss->count == 0U) continue;
    if (source.issues.size() >= limits.maximumEvents)
      return core::failure<ImportedLyrics>(core::ErrorCode::Unsupported,
          "SMF conversion diagnostic report exceeds event bounds");
    source.issues.push_back({SmfIssueSeverity::Loss, loss->first,
        std::to_string(loss->count) + " " + std::string{description} +
            "; affected source ticks " + std::to_string(loss->first.value()) +
            ".." + std::to_string(loss->last.value()) + " at " +
            std::to_string(source.ppq) + " PPQ"});
  }
  return lyrics;
}

// All absolute positions share one quantization map. Scaling durations in
// isolation would make adjacent note endpoints disagree after rounding.
// Diagnostics deliberately retain source ticks and source PPQ in SmfScore.
core::Result<ImportedTimeline> importTimeline(SmfScore& source,
                                            const SmfLimits& limits) {
  ImportedTimeline positions;
  const auto remember = [&](time::Tick tick) -> core::Result<void> {
    if (positions.contains(tick)) return core::success();
    // Admit the zero origin and retained event positions against the same
    // declared event budget, before allocating another map node.
    if (positions.size() >= limits.maximumEvents)
      return core::failure(core::ErrorCode::InvalidArgument,
          "SMF import timeline exceeds event bounds");
    positions.emplace(tick, time::Tick{0});
    return core::success();
  };
  auto added = remember(time::Tick{0});
  if (!added) return core::Result<ImportedTimeline>{added.error()};
  for (const auto& note : source.notes) {
    added = remember(note.start);
    if (!added) return core::Result<ImportedTimeline>{added.error()};
    // decodeSmf validated the addition against maximumTick before admission.
    added = remember(note.start + note.duration);
    if (!added) return core::Result<ImportedTimeline>{added.error()};
  }
  for (const auto& tempo : source.tempos) {
    added = remember(tempo.tick);
    if (!added) return core::Result<ImportedTimeline>{added.error()};
  }
  for (const auto& meter : source.meters) {
    added = remember(meter.tick);
    if (!added) return core::Result<ImportedTimeline>{added.error()};
  }
  for (const auto& text : source.texts) {
    added = remember(text.tick);
    if (!added) return core::Result<ImportedTimeline>{added.error()};
  }

  constexpr std::int64_t targetPpq = time::kDefaultPpq;
  const auto sourcePpq = static_cast<std::int64_t>(source.ppq);
  std::int64_t previous = -1;
  for (auto& [sourceTick, targetTick] : positions) {
    const auto whole = sourceTick.value() / sourcePpq;
    const auto remainder = sourceTick.value() % sourcePpq;
    // No large tick multiplication, floating point, or unchecked addition.
    // remainder < 32768, so this small numerator cannot overflow int64.
    if (whole > limits.maximumTick / targetPpq)
      return core::failure<ImportedTimeline>(core::ErrorCode::InvalidArgument,
          "SMF scaled timeline exceeds tick bounds");
    const auto integral = whole * targetPpq;
    const auto numerator = remainder * targetPpq;
    const auto fraction = (numerator + sourcePpq / 2) / sourcePpq;
    if (fraction > limits.maximumTick - integral)
      return core::failure<ImportedTimeline>(core::ErrorCode::InvalidArgument,
          "SMF scaled timeline exceeds tick bounds");
    const auto scaled = integral + fraction;
    if (scaled <= previous)
      return core::failure<ImportedTimeline>(core::ErrorCode::Unsupported,
          "SMF timeline positions collide when rounded to SEAM PPQ",
          "source tick " + std::to_string(sourceTick.value()));
    if (numerator % sourcePpq != 0) {
      if (source.issues.size() >= limits.maximumEvents)
        return core::failure<ImportedTimeline>(core::ErrorCode::Unsupported,
            "SMF timing diagnostic report exceeds event bounds");
      source.issues.push_back({SmfIssueSeverity::Warning, sourceTick,
          "Source tick " + std::to_string(sourceTick.value()) + " at " +
              std::to_string(source.ppq) + " PPQ was rounded to tick " +
              std::to_string(scaled) + " at " + std::to_string(targetPpq) +
              " PPQ (nearest tick; half ticks round up)"});
    }
    targetTick = time::Tick{scaled};
    previous = scaled;
  }
  return positions;
}

}  // namespace

core::Result<SmfProjectDraft> importSmfProject(
    std::span<const std::uint8_t> bytes, application::ProjectFactory& factory,
    SmfImportRequest request, SmfLimits limits) {
  using Output = SmfProjectDraft;
  if (request.projectName.empty() || request.projectName.size() > 256U ||
      request.trackName.empty() || request.trackName.size() > 256U ||
      request.regionName.empty() || request.regionName.size() > 256U)
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "SMF import names are empty or oversized");
  auto decoded = decodeSmf(bytes, limits);
  if (!decoded) return core::Result<Output>{decoded.error()};
  const auto timeline = importTimeline(decoded.value(), limits);
  if (!timeline) return core::Result<Output>{timeline.error()};
  const auto position = [&](time::Tick sourceTick) {
    return timeline.value().at(sourceTick);
  };
  time::Tick end{time::kDefaultPpq};
  for (const auto& note : decoded.value().notes)
    end = std::max(end, position(note.start + note.duration));
  if (end.value() > limits.maximumTick)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "SMF scaled region exceeds tick bounds");
  const auto lyrics = importLyricsAndDiscloseLosses(decoded.value(), limits);
  if (!lyrics) return core::Result<Output>{lyrics.error()};
  auto project = factory.createProject(request.projectName);
  const auto track = factory.addVocalTrack(project, request.trackName);
  const auto region = factory.addRegion(project, track, request.regionName,
      time::Tick{0}, end);
  auto* target = project.findRegion(region);
  if (!target) return core::failure<Output>(core::ErrorCode::InvariantViolation, "SMF import region was not created");
  for (const auto& tempo : decoded.value().tempos) {
    const auto added = project.tempoMap().addOrReplace(position(tempo.tick), tempo.bpm);
    if (!added) return core::Result<Output>{added.error()};
  }
  for (const auto& meter : decoded.value().meters) {
    const auto denominator = static_cast<std::uint8_t>(1U << meter.denominatorPower);
    const auto added = project.meterMap().addOrReplace(position(meter.tick), meter.numerator, denominator);
    if (!added) return core::Result<Output>{added.error()};
  }
  for (const auto& note : decoded.value().notes) {
    std::u32string text;
    const auto found = lyrics.value().find(note.start);
    if (found != lyrics.value().end()) {
      const auto decodedText = domain::fromUtf8(found->second->text);
      if (!decodedText) return core::Result<Output>{decodedText.error()};
      text = decodedText.value();
    }
    const auto start = position(note.start);
    const auto duration = position(note.start + note.duration) - start;
    auto [lyric, scoreNote] = factory.makeNote(start, duration, note.midi,
        std::move(text), request.language);
    target->lyrics.push_back(std::move(lyric));
    target->notes.push_back(std::move(scoreNote));
  }
  target->sortNotes();
  const auto valid = project.validate(); if (!valid) return core::Result<Output>{valid.error()};
  return Output{std::move(project), std::move(decoded).value()};
}

core::Result<SmfScore> exportSmfProject(
    const domain::Project& project, domain::TrackId trackId,
    domain::RegionId regionId, SmfLimits limits) {
  using Output = SmfScore;
  const auto validProject = project.validate(); if (!validProject) return core::Result<Output>{validProject.error()};
  const auto* track = project.findVocalTrack(trackId);
  const auto* region = track ? track->findRegion(regionId) : nullptr;
  if (!track || !region) return core::failure<Output>(core::ErrorCode::NotFound, "SMF export track or region is missing");
  Output score; score.ppq = static_cast<std::uint16_t>(std::clamp(project.ppq(), 1, 32767));
  for (const auto& tempo : project.tempoMap().events()) score.tempos.push_back({tempo.tick, tempo.bpm});
  for (const auto& meter : project.meterMap().events()) score.meters.push_back({meter.tick, meter.numerator, denominatorPower(meter.denominator)});
  std::map<domain::LyricTokenId, const domain::LyricToken*> lyrics;
  for (const auto& lyric : region->lyrics) lyrics.emplace(lyric.id, &lyric);
  for (const auto& note : region->notes) {
    const auto found = lyrics.find(note.lyricTokenId);
    if (found != lyrics.end() && !found->second->surface.empty()) score.texts.push_back({note.startTick, domain::toUtf8(found->second->surface), true});
    score.notes.push_back({note.startTick, note.durationTick, note.midiKey, 100U, 0U});
  }
  if (!region->phonemeOverrides.empty() || !region->unitSelectionOverrides.empty() || !region->seamOverrides.empty() ||
      !region->dynamicsAutomation.points().empty() || !region->formantAutomation.points().empty() ||
      region->performance.pronunciation.has_value())
    score.issues.push_back({SmfIssueSeverity::Loss, time::Tick{0}, "SEAM phoneme, expression, performance and unit metadata are not representable in SMF v1"});
  const auto valid = score.validate(limits); if (!valid) return core::Result<Output>{valid.error()};
  return score;
}

}  // namespace seam::interchange
