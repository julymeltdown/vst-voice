#include "seam/interchange/smf_project_conversion.hpp"

#include "seam/domain/note.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
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
using ImportedLyrics = std::map<std::pair<std::uint16_t, time::Tick>, const SmfText*>;

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

core::Result<std::uint16_t> representablePpq(const domain::Project& project) {
  const auto ppq = project.ppq();
  if (ppq < 1 || ppq > 32767)
    return core::failure<std::uint16_t>(core::ErrorCode::Unsupported,
        "Project PPQ is outside the SMF 1..32767 range; export at a representable PPQ");
  return static_cast<std::uint16_t>(ppq);
}

bool hasAdditionalSerializedEventSlot(const SmfScore& score,
                                     const SmfLimits& limits) noexcept {
  const auto trackCount = score.tracks.empty() ? 1U : score.tracks.size();
  std::size_t events = trackCount;
  const auto add = [&](std::size_t count) {
    if (events > limits.maximumSerializedEvents ||
        count > limits.maximumSerializedEvents - events)
      return false;
    events += count;
    return true;
  };
  if (!add(score.tempos.size()) || !add(score.meters.size()) ||
      !add(score.texts.size()) || events > limits.maximumSerializedEvents ||
      score.notes.size() > (limits.maximumSerializedEvents - events) / 2U)
    return false;
  events += score.notes.size() * 2U;
  return events < limits.maximumSerializedEvents;
}

core::Result<std::size_t> mandatoryProjectEvents(const domain::Project& project,
                                                const SmfLimits& limits) {
  std::size_t events = project.vocalTracks().size(); // One end-of-track event per track.
  const auto add = [&](std::size_t count) {
    if (events > limits.maximumSerializedEvents ||
        count > limits.maximumSerializedEvents - events)
      return false;
    events += count;
    return true;
  };
  if (!add(project.tempoMap().events().size()) ||
      !add(project.meterMap().events().size()))
    return core::failure<std::size_t>(core::ErrorCode::InvalidArgument,
        "SMF mandatory event count exceeds bounds");
  std::size_t notes = 0U;
  for (const auto& track : project.vocalTracks())
    for (const auto& region : track.regions) {
      if (region.notes.size() > limits.maximumNotes - notes)
        return core::failure<std::size_t>(core::ErrorCode::InvalidArgument,
            "SMF note count exceeds bounds");
      notes += region.notes.size();
    }
  if (events > limits.maximumSerializedEvents ||
      notes > (limits.maximumSerializedEvents - events) / 2U)
    return core::failure<std::size_t>(core::ErrorCode::InvalidArgument,
        "SMF mandatory note and timeline event count exceeds bounds");
  events += notes * 2U;
  return events;
}

core::Result<ImportedLyrics> importLyricsAndDiscloseLosses(
    SmfScore& source, const SmfLimits& limits) {
  ImportLossCount velocities, channels, nonLyrics, unmatchedLyrics, extraLyrics,
      crossTrackLyrics;
  for (const auto& note : source.notes) {
    // These are the exact fixed defaults used by exportSmfProject. A source
    // already using them loses no distinct value when round-tripped.
    if (note.velocity != 100U) velocities.observe(note.start);
    if (note.channel != 0U) channels.observe(note.start);
  }
  std::vector<bool> sourceTrackHasNotes(source.tracks.empty() ? 1U : source.tracks.size(), false);
  for (const auto& note : source.notes) sourceTrackHasNotes[note.track] = true;
  const auto noteBearingTrackCount = static_cast<std::size_t>(std::count(
      sourceTrackHasNotes.begin(), sourceTrackHasNotes.end(), true));
  std::uint16_t soleNoteBearingTrack = 0U;
  if (noteBearingTrackCount == 1U) {
    soleNoteBearingTrack = static_cast<std::uint16_t>(std::distance(
        sourceTrackHasNotes.begin(),
        std::find(sourceTrackHasNotes.begin(), sourceTrackHasNotes.end(), true)));
  }
  ImportedLyrics lyrics;
  const auto findNote = [&](std::uint16_t track, time::Tick tick) {
    return std::lower_bound(source.notes.begin(), source.notes.end(),
        std::pair{track, tick}, [](const SmfNote& item, const auto& key) {
          return std::pair{item.track, item.start} < key;
        });
  };
  // First assign lyrics colocated with notes. A lyric track's file order must
  // never let a conductor-track lyric displace a note track's own lyric.
  for (const auto& text : source.texts) {
    if (!text.lyric) { nonLyrics.observe(text.tick); continue; }
    if (!sourceTrackHasNotes[text.track]) continue;
    const auto note = findNote(text.track, text.tick);
    if (note == source.notes.end() || note->track != text.track || note->start != text.tick) {
      unmatchedLyrics.observe(text.tick);
      continue;
    }
    const auto key = std::pair{text.track, text.tick};
    if (lyrics.contains(key)) { extraLyrics.observe(text.tick); continue; }
    if (lyrics.size() >= limits.maximumNotes)
      return core::failure<ImportedLyrics>(core::ErrorCode::InvalidArgument,
          "SMF lyric assignments exceed note bounds");
    // Borrow only the first lyric at each assigned note onset. Stable source
    // event ordering determines which lyric wins when events share a tick.
    lyrics.emplace(key, &text);
  }
  // Then allow lyrics on note-free tracks to pair by exact onset only when
  // exactly one note-bearing track makes that fallback unambiguous.
  for (const auto& text : source.texts) {
    if (!text.lyric || sourceTrackHasNotes[text.track]) continue;
    if (noteBearingTrackCount != 1U) {
      unmatchedLyrics.observe(text.tick);
      continue;
    }
    const auto note = findNote(soleNoteBearingTrack, text.tick);
    if (note == source.notes.end() || note->track != soleNoteBearingTrack ||
        note->start != text.tick) {
      unmatchedLyrics.observe(text.tick);
      continue;
    }
    const auto key = std::pair{soleNoteBearingTrack, text.tick};
    if (lyrics.contains(key)) { extraLyrics.observe(text.tick); continue; }
    if (lyrics.size() >= limits.maximumNotes)
      return core::failure<ImportedLyrics>(core::ErrorCode::InvalidArgument,
          "SMF lyric assignments exceed note bounds");
    crossTrackLyrics.observe(text.tick);
    lyrics.emplace(key, &text);
  }
  const std::array<std::pair<const ImportLossCount*, std::string_view>, 5U> losses{{
      {&velocities, "note-on velocities are not preserved; SEAM exports velocity 100"},
      {&channels, "note channel assignments are not preserved; SEAM exports channel 1"},
      {&nonLyrics, "non-lyric text events are not represented in the SEAM project"},
      {&unmatchedLyrics, "lyric events have no unambiguous matching note onset and are not retained"},
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
  if (crossTrackLyrics.count != 0U) {
    if (source.issues.size() >= limits.maximumEvents)
      return core::failure<ImportedLyrics>(core::ErrorCode::Unsupported,
          "SMF conversion diagnostic report exceeds event bounds");
    source.issues.push_back({SmfIssueSeverity::Warning, crossTrackLyrics.first,
        std::to_string(crossTrackLyrics.count) +
            " lyric events on note-free source tracks were assigned by tick to the only note-bearing track; affected source ticks " +
            std::to_string(crossTrackLyrics.first.value()) + ".." +
            std::to_string(crossTrackLyrics.last.value()) + " at " +
            std::to_string(source.ppq) + " PPQ"});
  }

  std::vector<bool> trackHasMatchedLyrics(sourceTrackHasNotes.size(), false);
  std::vector<bool> sourceTrackHasPercussion(sourceTrackHasNotes.size(), false);
  for (const auto& [key, lyric] : lyrics) {
    (void)lyric;
    trackHasMatchedLyrics[key.first] = true;
  }
  for (const auto& note : source.notes)
    if (note.channel == 9U) sourceTrackHasPercussion[note.track] = true;
  for (std::size_t track = 0U; track < sourceTrackHasNotes.size(); ++track) {
    if (!sourceTrackHasNotes[track]) continue;
    const bool containsPercussion = sourceTrackHasPercussion[track];
    if (trackHasMatchedLyrics[track] && !containsPercussion) continue;
    if (source.issues.size() >= limits.maximumEvents)
      return core::failure<ImportedLyrics>(core::ErrorCode::Unsupported,
          "SMF conversion diagnostic report exceeds event bounds");
    std::string message = "Source track " + std::to_string(track + 1U);
    if (!trackHasMatchedLyrics[track]) {
      message += " has no matched lyric events; notes were imported with default lyric text";
      if (containsPercussion)
        message += "; the track also includes MIDI channel 10 percussion notes, which remain mapped to vocal notes";
    } else if (containsPercussion) {
      message += " contains MIDI channel 10 percussion notes, which are still mapped to vocal notes";
    }
    message += "; review this track before rendering vocals";
    source.issues.push_back({SmfIssueSeverity::Warning, time::Tick{0}, std::move(message)});
  }
  return lyrics;
}

std::string truncateProjectTrackName(std::string value, bool& truncated) {
  constexpr std::size_t maximumNameBytes = 256U;
  if (value.size() <= maximumNameBytes) return value;
  value.resize(maximumNameBytes);
  while (!value.empty() && !domain::fromUtf8(value)) value.pop_back();
  truncated = true;
  return value;
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
  std::vector<bool> sourceTrackHasNotes(decoded.value().tracks.empty() ? 1U : decoded.value().tracks.size(), false);
  for (const auto& note : decoded.value().notes) sourceTrackHasNotes[note.track] = true;
  std::vector<std::string> importedTrackNames(sourceTrackHasNotes.size());
  for (std::size_t sourceTrack = 0U; sourceTrack < sourceTrackHasNotes.size(); ++sourceTrack) {
    if (!sourceTrackHasNotes[sourceTrack]) continue;
    std::string trackName = request.trackName;
    if (!decoded.value().tracks.empty() && !decoded.value().tracks[sourceTrack].name.empty())
      trackName = decoded.value().tracks[sourceTrack].name;
    else if (sourceTrackHasNotes.size() > 1U)
      trackName += " " + std::to_string(sourceTrack + 1U);
    bool truncated = false;
    importedTrackNames[sourceTrack] = truncateProjectTrackName(std::move(trackName), truncated);
    if (truncated) {
      if (decoded.value().issues.size() >= limits.maximumEvents)
        return core::failure<Output>(core::ErrorCode::Unsupported,
            "SMF conversion diagnostic report exceeds event bounds");
      decoded.value().issues.push_back({SmfIssueSeverity::Loss, time::Tick{0},
          "Source track " + std::to_string(sourceTrack + 1U) +
              " name exceeded 256 UTF-8 bytes and was truncated in the SEAM project"});
    }
  }
  auto project = factory.createProject(request.projectName);
  std::map<std::uint16_t, domain::VocalRegion*> targetBySourceTrack;
  for (std::size_t sourceTrack = 0U; sourceTrack < sourceTrackHasNotes.size(); ++sourceTrack) {
    if (!sourceTrackHasNotes[sourceTrack]) continue;
    const auto track = factory.addVocalTrack(project, importedTrackNames[sourceTrack]);
    const auto region = factory.addRegion(project, track,
        sourceTrackHasNotes.size() == 1U ? request.regionName : request.regionName + " " + std::to_string(sourceTrack + 1U),
        time::Tick{0}, end);
    auto* target = project.findRegion(region);
    if (!target) return core::failure<Output>(core::ErrorCode::InvariantViolation, "SMF import region was not created");
    targetBySourceTrack.emplace(static_cast<std::uint16_t>(sourceTrack), target);
  }
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
    const auto found = lyrics.value().find({note.track, note.start});
    if (found != lyrics.value().end()) {
      const auto decodedText = domain::fromUtf8(found->second->text);
      if (!decodedText) return core::Result<Output>{decodedText.error()};
      text = decodedText.value();
    }
    const auto start = position(note.start);
    const auto duration = position(note.start + note.duration) - start;
    auto [lyric, scoreNote] = factory.makeNote(start, duration, note.midi,
        std::move(text), request.language);
    auto* target = targetBySourceTrack.at(note.track);
    target->lyrics.push_back(std::move(lyric));
    target->notes.push_back(std::move(scoreNote));
  }
  for (auto& track : project.vocalTracks())
    for (auto& region : track.regions) region.sortNotes();
  const auto valid = project.validate(); if (!valid) return core::Result<Output>{valid.error()};
  return Output{std::move(project), std::move(decoded).value()};
}

core::Result<SmfScore> exportSmfProject(
    const domain::Project& project, domain::TrackId trackId,
    domain::RegionId regionId, SmfLimits limits) {
  using Output = SmfScore;
  const auto validProject = project.validate(); if (!validProject) return core::Result<Output>{validProject.error()};
  const auto ppq = representablePpq(project);
  if (!ppq) return core::Result<Output>{ppq.error()};
  const auto* track = project.findVocalTrack(trackId);
  const auto* region = track ? track->findRegion(regionId) : nullptr;
  if (!track || !region) return core::failure<Output>(core::ErrorCode::NotFound, "SMF export track or region is missing");
  Output score; score.ppq = ppq.value();
  score.tracks.push_back({});
  for (const auto& tempo : project.tempoMap().events()) score.tempos.push_back({tempo.tick, tempo.bpm});
  for (const auto& meter : project.meterMap().events()) score.meters.push_back({meter.tick, meter.numerator, denominatorPower(meter.denominator)});
  std::map<domain::LyricTokenId, const domain::LyricToken*> lyrics;
  for (const auto& lyric : region->lyrics) lyrics.emplace(lyric.id, &lyric);
  std::map<time::Tick, std::optional<std::string>> lyricsAtOnset;
  ImportLossCount nulLyrics;
  for (const auto& note : region->notes) {
    const auto found = lyrics.find(note.lyricTokenId);
    std::optional<std::string> surface;
    if (found != lyrics.end() && !found->second->surface.empty())
      surface = domain::toUtf8(found->second->surface);
    const auto [existing, inserted] = lyricsAtOnset.emplace(note.startTick, surface);
    if (!inserted && existing->second != surface)
      return core::failure<Output>(core::ErrorCode::Unsupported,
          "SMF cannot preserve different lyric surfaces on notes sharing one track onset");
    if (inserted && surface) {
      if (surface->find('\0') != std::string::npos) nulLyrics.observe(note.startTick);
      else score.texts.push_back({note.startTick, std::move(*surface), true});
    }
    score.notes.push_back({note.startTick, note.durationTick, note.midiKey, 100U, 0U});
  }
  if (nulLyrics.count != 0U) {
    if (score.issues.size() >= limits.maximumEvents)
      return core::failure<Output>(core::ErrorCode::Unsupported,
          "SMF conversion diagnostic report exceeds event bounds");
    score.issues.push_back({SmfIssueSeverity::Loss, nulLyrics.first,
        std::to_string(nulLyrics.count) +
            " lyric surfaces contained NUL and were omitted from SMF text events; affected source ticks " +
            std::to_string(nulLyrics.first.value()) + ".." +
            std::to_string(nulLyrics.last.value())});
  }
  const auto& sourceTrackName = track->name;
  bool lyricBytesFit = true;
  std::size_t lyricTextBytes = 0U;
  for (const auto& text : score.texts) {
    if (text.text.size() > limits.maximumTextBytes - lyricTextBytes) {
      lyricBytesFit = false;
      break;
    }
    lyricTextBytes += text.text.size();
  }
  const bool trackNameIsUtf8 = sourceTrackName.size() <= limits.maximumTextBytes &&
      sourceTrackName.find('\0') == std::string::npos && domain::fromUtf8(sourceTrackName);
  if (sourceTrackName.empty()) {
    // Keep the codec's normal unnamed-track representation.
  } else if (trackNameIsUtf8 && lyricBytesFit &&
      sourceTrackName.size() <= limits.maximumTextBytes - lyricTextBytes &&
      hasAdditionalSerializedEventSlot(score, limits)) {
    score.tracks.front().name = sourceTrackName;
  } else {
    if (score.issues.size() >= limits.maximumEvents)
      return core::failure<Output>(core::ErrorCode::Unsupported,
          "SMF conversion diagnostic report exceeds event bounds");
    score.issues.push_back({SmfIssueSeverity::Loss, time::Tick{0},
        "SEAM track name is not NUL-free UTF-8 or does not fit the remaining SMF text/event budget; track name was omitted"});
  }
  if (!region->phonemeOverrides.empty() || !region->unitSelectionOverrides.empty() || !region->seamOverrides.empty() ||
      !region->dynamicsAutomation.points().empty() || !region->formantAutomation.points().empty() ||
      region->performance.pronunciation.has_value())
    score.issues.push_back({SmfIssueSeverity::Loss, time::Tick{0}, "SEAM phoneme, expression, performance and unit metadata are not representable in SMF v1"});
  const auto valid = score.validate(limits); if (!valid) return core::Result<Output>{valid.error()};
  return score;
}

core::Result<SmfScore> exportSmfProject(
    const domain::Project& project, SmfLimits limits) {
  using Output = SmfScore;
  const auto validProject = project.validate();
  if (!validProject) return core::Result<Output>{validProject.error()};
  if (project.vocalTracks().empty())
    return core::failure<Output>(core::ErrorCode::NotFound,
        "SMF export requires at least one vocal track");
  if (project.vocalTracks().size() > limits.maximumTracks ||
      project.vocalTracks().size() > std::numeric_limits<std::uint16_t>::max())
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "SMF vocal track count exceeds bounds");

  const auto ppq = representablePpq(project);
  if (!ppq) return core::Result<Output>{ppq.error()};
  const auto mandatoryEvents = mandatoryProjectEvents(project, limits);
  if (!mandatoryEvents) return core::Result<Output>{mandatoryEvents.error()};

  Output score;
  score.ppq = ppq.value();
  score.tracks.resize(project.vocalTracks().size());
  if (project.tempoMap().events().size() > limits.maximumSerializedEvents ||
      project.meterMap().events().size() > limits.maximumSerializedEvents)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "SMF tempo or meter count exceeds bounds");
  for (const auto& tempo : project.tempoMap().events())
    score.tempos.push_back({tempo.tick, tempo.bpm});
  for (const auto& meter : project.meterMap().events())
    score.meters.push_back({meter.tick, meter.numerator,
        denominatorPower(meter.denominator)});

  std::vector<ImportLossCount> nulLyrics(project.vocalTracks().size());
  std::vector<ImportLossCount> overBudgetLyrics(project.vocalTracks().size());
  std::vector<ImportLossCount> unsupportedTrackData(project.vocalTracks().size());
  std::size_t textBytes = 0U;
  const auto optionalTextEventSlots =
      limits.maximumSerializedEvents - mandatoryEvents.value();
  std::size_t admittedTrackNameEvents = 0U;
  for (std::size_t trackIndex = 0U;
       trackIndex < project.vocalTracks().size(); ++trackIndex) {
    const auto& track = project.vocalTracks()[trackIndex];
    std::map<time::Tick, std::optional<std::string>> lyricsAtOnset;
    for (const auto& region : track.regions) {
      std::map<domain::LyricTokenId, const domain::LyricToken*> lyricsById;
      for (const auto& lyric : region.lyrics) lyricsById.emplace(lyric.id, &lyric);
      for (const auto& note : region.notes) {
        if (score.notes.size() >= limits.maximumNotes)
          return core::failure<Output>(core::ErrorCode::InvalidArgument,
              "SMF note count exceeds bounds");
        const auto base = region.startTick.value();
        const auto relative = note.startTick.value();
        if (base < 0 || relative < 0 || base > limits.maximumTick ||
            relative > limits.maximumTick - base)
          return core::failure<Output>(core::ErrorCode::InvalidArgument,
              "SMF project note position exceeds tick bounds");
        const auto absolute = base + relative;
        if (note.durationTick.value() <= 0 ||
            note.durationTick.value() > limits.maximumTick - absolute)
          return core::failure<Output>(core::ErrorCode::InvalidArgument,
              "SMF project note duration exceeds tick bounds");
        const time::Tick start{absolute};
        const auto lyric = lyricsById.find(note.lyricTokenId);
        std::optional<std::string> surface;
        if (lyric != lyricsById.end() && !lyric->second->surface.empty())
          surface = domain::toUtf8(lyric->second->surface);
        const auto [existingLyric, firstAtOnset] = lyricsAtOnset.emplace(start, surface);
        if (!firstAtOnset && existingLyric->second != surface)
          return core::failure<Output>(core::ErrorCode::Unsupported,
              "SMF cannot preserve different lyric surfaces on notes sharing one track onset");
        if (firstAtOnset && surface) {
          if (surface->find('\0') != std::string::npos) {
            nulLyrics[trackIndex].observe(start);
          } else if (score.texts.size() >= optionalTextEventSlots ||
              textBytes > limits.maximumTextBytes ||
              surface->size() > limits.maximumTextBytes - textBytes) {
            overBudgetLyrics[trackIndex].observe(start);
          } else {
            textBytes += surface->size();
            score.texts.push_back({start, std::move(*surface), true,
                static_cast<std::uint16_t>(trackIndex)});
          }
        }
        score.notes.push_back({start, note.durationTick, note.midiKey, 100U,
            0U, static_cast<std::uint16_t>(trackIndex)});
        const bool unsupportedNote =
            note.articulation != domain::NoteArticulation::Normal ||
            note.slurGroup.has_value() || note.vibrato.enabled ||
            note.phoneticHint.has_value() ||
            (lyric != lyricsById.end() && lyric->second->readingHint.has_value());
        if (unsupportedNote) unsupportedTrackData[trackIndex].observe(start);
      }
      if (!region.phonemeOverrides.empty() ||
          !region.unitSelectionOverrides.empty() ||
          !region.seamOverrides.empty() ||
          !region.pitchAutomation.points().empty() ||
          !region.dynamicsAutomation.points().empty() ||
          !region.formantAutomation.points().empty() ||
          !region.breathinessAutomation.points().empty() ||
          !region.tensionAutomation.points().empty() ||
          !region.airinessAutomation.points().empty() ||
          !region.genderAutomation.points().empty() ||
          !region.growlAutomation.points().empty() ||
          region.performance.pronunciation.has_value())
        unsupportedTrackData[trackIndex].observe(region.startTick);
    }
    if (track.gainDb != 0.0F || track.pan != 0.0F || track.muted || track.solo)
      unsupportedTrackData[trackIndex].observe(time::Tick{0});
  }

  const auto addLoss = [&](time::Tick tick, std::string message) -> core::Result<void> {
    if (score.issues.size() >= limits.maximumEvents)
      return core::failure(core::ErrorCode::Unsupported,
          "SMF conversion diagnostic report exceeds event bounds");
    score.issues.push_back({SmfIssueSeverity::Loss, tick, std::move(message)});
    return core::success();
  };
  for (std::size_t index = 0U; index < project.vocalTracks().size(); ++index) {
    const auto trackNumber = std::to_string(index + 1U);
    if (nulLyrics[index].count != 0U) {
      const auto& loss = nulLyrics[index];
      const auto recorded = addLoss(loss.first,
          std::to_string(loss.count) +
          " lyric surfaces on source track " + trackNumber +
          " contained NUL and were omitted; affected source ticks " +
          std::to_string(loss.first.value()) + ".." +
          std::to_string(loss.last.value()));
      if (!recorded) return core::Result<Output>{recorded.error()};
    }
    if (overBudgetLyrics[index].count != 0U) {
      const auto& loss = overBudgetLyrics[index];
      const auto recorded = addLoss(loss.first,
          std::to_string(loss.count) +
          " lyric surfaces on source track " + trackNumber +
          " exceeded SMF event or text-byte limits and were omitted; affected source ticks " +
          std::to_string(loss.first.value()) + ".." +
          std::to_string(loss.last.value()));
      if (!recorded) return core::Result<Output>{recorded.error()};
    }
    if (unsupportedTrackData[index].count != 0U) {
      const auto& loss = unsupportedTrackData[index];
      const auto recorded = addLoss(loss.first,
          std::to_string(loss.count) +
          " SEAM-only track, region, note, or performance settings on source track " +
          trackNumber + " were not represented in SMF; affected project ticks " +
          std::to_string(loss.first.value()) + ".." +
          std::to_string(loss.last.value()));
      if (!recorded) return core::Result<Output>{recorded.error()};
    }
  }
  if (!project.audioTracks().empty()) {
    const auto recorded = addLoss(time::Tick{0},
        std::to_string(project.audioTracks().size()) +
        " audio tracks are not represented in the SMF score");
    if (!recorded) return core::Result<Output>{recorded.error()};
  }

  // Preserve lyrics within the cumulative text budget first; names that do not
  // fit are explicitly omitted rather than evicting vocal content.
  for (std::size_t index = 0U; index < project.vocalTracks().size(); ++index) {
    const auto& name = project.vocalTracks()[index].name;
    if (name.empty()) continue;
    if (name.find('\0') != std::string::npos || !domain::fromUtf8(name) ||
        score.texts.size() >= optionalTextEventSlots ||
        admittedTrackNameEvents >= optionalTextEventSlots - score.texts.size() ||
        textBytes > limits.maximumTextBytes ||
        name.size() > limits.maximumTextBytes - textBytes) {
      const auto recorded = addLoss(time::Tick{0},
          "Track name on source track " + std::to_string(index + 1U) +
          " is not NUL-free UTF-8 or does not fit the remaining SMF text budget; it was omitted");
      if (!recorded) return core::Result<Output>{recorded.error()};
      continue;
    }
    textBytes += name.size();
    score.tracks[index].name = name;
    ++admittedTrackNameEvents;
  }

  const auto valid = score.validate(limits);
  if (!valid) return core::Result<Output>{valid.error()};
  return score;
}

}  // namespace seam::interchange
