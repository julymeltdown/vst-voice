#include "seam/interchange/ustx_project_conversion.hpp"

#include "seam/domain/note.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <string_view>
#include <utility>

namespace seam::interchange {
namespace {

constexpr std::int64_t kUstxPpq = 480;
constexpr std::int64_t kSeamPpq = 960;

void addIssue(std::vector<UstxIssue>& issues, UstxIssueSeverity severity,
              std::string path, std::string message, const UstxLimits& limits) {
  if (issues.size() < std::min<std::size_t>(limits.maximumNodes, 4'096U))
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
  if (seconds <= 0.0) return static_cast<std::int64_t>(std::llround(seconds * tempos.front().bpm * kUstxPpq / 60.0));
  double elapsed = 0.0;
  std::int64_t cursor = 0;
  double bpm = tempos.front().bpm;
  for (std::size_t index = 1U; index < tempos.size(); ++index) {
    const auto end = tempos[index].position.value();
    const auto segment = static_cast<double>(end - cursor) * 60.0 / (bpm * kUstxPpq);
    if (seconds <= elapsed + segment) return cursor + static_cast<std::int64_t>(std::llround((seconds - elapsed) * bpm * kUstxPpq / 60.0));
    elapsed += segment;
    cursor = end;
    bpm = tempos[index].bpm;
  }
  return cursor + static_cast<std::int64_t>(std::llround((seconds - elapsed) * bpm * kUstxPpq / 60.0));
}

std::string shapeFor(domain::CurveInterpolation interpolation) {
  switch (interpolation) {
    case domain::CurveInterpolation::Step: return "sp";
    case domain::CurveInterpolation::Smooth: return "io";
    case domain::CurveInterpolation::Linear: return "l";
  }
  return "l";
}

domain::CurveInterpolation interpolationFor(std::string_view shape,
                                             std::string_view path,
                                             std::vector<UstxIssue>& issues,
                                             const UstxLimits& limits) {
  if (shape == "l") return domain::CurveInterpolation::Linear;
  if (shape == "sp") return domain::CurveInterpolation::Step;
  if (shape == "io" || shape == "i" || shape == "o") return domain::CurveInterpolation::Smooth;
  addIssue(issues, UstxIssueSeverity::Loss, std::string(path), "unknown USTX pitch shape was approximated as smooth", limits);
  return domain::CurveInterpolation::Smooth;
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
    for (std::size_t noteIndex = 0U; noteIndex < source.notes.size(); ++noteIndex) {
      const auto& inputNote = source.notes[noteIndex];
      auto noteStart = scaleTick(inputNote.position.value(), kUstxPpq, kSeamPpq, "ustx.voice_parts.notes.position", issues, limits); if (!noteStart) return core::Result<Output>{noteStart.error()};
      auto noteDuration = scaleTick(inputNote.duration.value(), kUstxPpq, kSeamPpq, "ustx.voice_parts.notes.duration", issues, limits); if (!noteDuration) return core::Result<Output>{noteDuration.error()}; if (noteDuration.value() <= 0) return core::failure<Output>(core::ErrorCode::InvalidArgument, "USTX note duration rounded to zero");
      const auto lyric = domain::fromUtf8(inputNote.lyric); if (!lyric) return core::Result<Output>{lyric.error()};
      auto [token, note] = factory.makeNote(time::Tick{noteStart.value()}, time::Tick{noteDuration.value()}, inputNote.tone, lyric.value(), request.language);
      if (inputNote.hasVibrato && inputNote.vibrato.length > 0.0) {
        note.vibrato.enabled = true;
        note.vibrato.startFraction = static_cast<float>(std::clamp(1.0 - inputNote.vibrato.length / 100.0, 0.0, 1.0));
        note.vibrato.fadeInFraction = static_cast<float>(std::clamp(inputNote.vibrato.fadeIn / 100.0, 0.0, 1.0));
        note.vibrato.fadeOutFraction = static_cast<float>(std::clamp(inputNote.vibrato.fadeOut / 100.0, 0.0, 1.0));
        note.vibrato.depthCents = static_cast<float>(std::clamp(inputNote.vibrato.depth * 10.0, 0.0, 200.0));
        note.vibrato.periodMilliseconds = static_cast<float>(std::clamp(inputNote.vibrato.period, 5.0, 500.0));
        note.vibrato.phaseTurns = static_cast<float>(std::fmod(std::abs(inputNote.vibrato.shift) / 360.0, 1.0));
        if (note.vibrato.fadeInFraction + note.vibrato.fadeOutFraction > 1.0F) { note.vibrato.fadeOutFraction = 1.0F - note.vibrato.fadeInFraction; addIssue(issues, UstxIssueSeverity::Warning, "ustx.voice_parts[" + std::to_string(partIndex) + "].notes[" + std::to_string(noteIndex) + "].vibrato", "vibrato fades were clamped to the SEAM span", limits); }
      }
      region->lyrics.push_back(std::move(token));
      region->notes.push_back(std::move(note));
      const auto notePath = "ustx.voice_parts[" + std::to_string(partIndex) + "].notes[" + std::to_string(noteIndex) + "]";
      if (source.position.value() > limits.maximumTick - inputNote.position.value() ||
          source.position.value() + inputNote.position.value() > limits.maximumTick - inputNote.duration.value())
        return core::failure<Output>(core::ErrorCode::InvalidArgument, "USTX note absolute tick overflows the configured limit");
      const auto absoluteNoteTick = source.position.value() + inputNote.position.value();
      const auto noteEnd = absoluteNoteTick + inputNote.duration.value();
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
          if (target < absoluteNoteTick || target > noteEnd || target < source.position.value() || target > source.position.value() + source.duration.value()) {
            addIssue(issues, UstxIssueSeverity::Loss, notePath + ".pitch[" + std::to_string(pointIndex) + "]", "pitch point falls outside its note or part and was omitted", limits); continue;
          }
          auto relative = scaleTick(target - source.position.value(), kUstxPpq, kSeamPpq, notePath + ".pitch[" + std::to_string(pointIndex) + "]", issues, limits); if (!relative) return core::Result<Output>{relative.error()};
          const auto interpolation = interpolationFor(point.shape, notePath, issues, limits);
          const auto result = region->pitchAutomation.upsert({time::Tick{relative.value()}, static_cast<float>(inputNote.tuning + point.y * 10.0), interpolation});
          if (!result) return core::Result<Output>{result.error()};
        }
      }
      if (inputNote.snapFirst) addIssue(issues, UstxIssueSeverity::Warning, notePath + ".pitch.snap_first", "snap_first is retained only as imported pitch points; neighboring-note materialization is not repeated", limits);
    }
    region->sortNotes();
  }
  const auto valid = project.validate(); if (!valid) return core::Result<Output>{valid.error()};
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
      UstxPart part{region.name, static_cast<std::uint32_t>(trackNumber), time::Tick{partPosition.value()}, time::Tick{partDuration.value()}, {}};
      std::map<domain::LyricTokenId, const domain::LyricToken*> lyrics;
      for (const auto& lyric : region.lyrics) lyrics.emplace(lyric.id, &lyric);
      for (std::size_t noteNumber = 0U; noteNumber < region.notes.size(); ++noteNumber) {
        const auto& note = region.notes[noteNumber];
        const auto lyric = lyrics.find(note.lyricTokenId);
        if (lyric == lyrics.end()) return core::failure<Output>(core::ErrorCode::InvariantViolation, "USTX export note references a missing lyric", note.id.toString());
        auto notePosition = scaleTick(note.startTick.value(), project.ppq(), kUstxPpq, "project.note.startTick", issues, limits); if (!notePosition) return core::Result<Output>{notePosition.error()};
        auto noteDuration = scaleTick(note.durationTick.value(), project.ppq(), kUstxPpq, "project.note.durationTick", issues, limits); if (!noteDuration) return core::Result<Output>{noteDuration.error()};
        UstxNote exported{time::Tick{notePosition.value()}, time::Tick{noteDuration.value()}, note.midiKey, domain::toUtf8(lyric->second->surface), 0.0, {}, false, {}, false};
        for (const auto& point : region.pitchAutomation.points()) {
          if (point.tick < note.startTick || point.tick > note.endTick()) continue;
          const auto absolutePoint = region.startTick + point.tick;
          const auto absoluteNote = region.startTick + note.startTick;
          const auto milliseconds = (project.tempoMap().secondsAt(absolutePoint) - project.tempoMap().secondsAt(absoluteNote)) * 1000.0;
          if (!std::isfinite(milliseconds) || milliseconds < 0.0) continue;
          exported.pitch.push_back({milliseconds, static_cast<double>(point.cents) / 10.0, shapeFor(point.interpolation)});
        }
        if (note.vibrato.enabled) {
          exported.hasVibrato = true;
          exported.vibrato.length = std::clamp((1.0 - static_cast<double>(note.vibrato.startFraction)) * 100.0, 0.0, 100.0);
          exported.vibrato.period = note.vibrato.periodMilliseconds;
          exported.vibrato.depth = static_cast<double>(note.vibrato.depthCents) / 10.0;
          exported.vibrato.fadeIn = static_cast<double>(note.vibrato.fadeInFraction) * 100.0;
          exported.vibrato.fadeOut = static_cast<double>(note.vibrato.fadeOutFraction) * 100.0;
          exported.vibrato.shift = static_cast<double>(note.vibrato.phaseTurns) * 360.0;
        }
        part.notes.push_back(std::move(exported));
        if (note.articulation != domain::NoteArticulation::Normal || note.slurGroup.has_value() || note.phoneticHint.has_value()) addIssue(issues, UstxIssueSeverity::Loss, "project.note[" + std::to_string(noteNumber) + "]", "SEAM articulation/slur/phonetic hint is not represented in USTX", limits);
      }
      if (!region.phonemeOverrides.empty() || !region.unitSelectionOverrides.empty() || !region.seamOverrides.empty() || !region.dynamicsAutomation.points().empty() || !region.performance.takes.empty() || !region.performance.accepted.empty()) addIssue(issues, UstxIssueSeverity::Loss, "project.vocalTracks.regions[" + std::to_string(regionNumber) + "]", "SEAM phoneme, dynamics and generated-performance metadata is not represented in USTX", limits);
      const auto requiredDuration = part.notes.empty() ? 0 : std::max_element(part.notes.begin(), part.notes.end(), [](const auto& lhs, const auto& rhs) { return lhs.position + lhs.duration < rhs.position + rhs.duration; })->position.value() + std::max_element(part.notes.begin(), part.notes.end(), [](const auto& lhs, const auto& rhs) { return lhs.position + lhs.duration < rhs.position + rhs.duration; })->duration.value();
      if (part.duration.value() < requiredDuration) { part.duration = time::Tick{requiredDuration}; addIssue(issues, UstxIssueSeverity::Warning, "project.vocalTracks.regions.durationTick", "part duration was extended to contain all rounded notes", limits); }
      document.parts.push_back(std::move(part));
    }
  }
  if (!project.audioTracks().empty()) addIssue(issues, UstxIssueSeverity::Loss, "project.audioTracks", "USTX export contains vocal tracks only", limits);
  const auto encoded = encodeUstx(document, limits); if (!encoded) return core::Result<Output>{encoded.error()};
  return Output{std::move(encoded).value(), std::move(issues)};
}

}  // namespace seam::interchange
