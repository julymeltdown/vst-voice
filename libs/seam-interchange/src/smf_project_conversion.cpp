#include "seam/interchange/smf_project_conversion.hpp"

#include "seam/domain/note.hpp"

#include <algorithm>
#include <map>
#include <utility>

namespace seam::interchange {
namespace {

std::uint8_t denominatorPower(std::uint8_t denominator) noexcept {
  std::uint8_t power = 0U;
  while (denominator > 1U) { denominator = static_cast<std::uint8_t>(denominator / 2U); ++power; }
  return power;
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
  const auto decoded = decodeSmf(bytes, limits);
  if (!decoded) return core::Result<Output>{decoded.error()};
  auto project = factory.createProject(request.projectName);
  const auto track = factory.addVocalTrack(project, request.trackName);
  const auto end = decoded.value().notes.empty() ? time::Tick{960} :
      std::max_element(decoded.value().notes.begin(), decoded.value().notes.end(),
          [](const auto& lhs, const auto& rhs) { return lhs.start + lhs.duration < rhs.start + rhs.duration; })->start +
      std::max_element(decoded.value().notes.begin(), decoded.value().notes.end(),
          [](const auto& lhs, const auto& rhs) { return lhs.start + lhs.duration < rhs.start + rhs.duration; })->duration;
  const auto region = factory.addRegion(project, track, request.regionName,
      time::Tick{0}, std::max(time::Tick{960}, end));
  auto* target = project.findRegion(region);
  if (!target) return core::failure<Output>(core::ErrorCode::InvariantViolation, "SMF import region was not created");
  for (const auto& tempo : decoded.value().tempos) {
    const auto added = project.tempoMap().addOrReplace(tempo.tick, tempo.bpm);
    if (!added) return core::Result<Output>{added.error()};
  }
  for (const auto& meter : decoded.value().meters) {
    const auto denominator = static_cast<std::uint8_t>(1U << meter.denominatorPower);
    const auto added = project.meterMap().addOrReplace(meter.tick, meter.numerator, denominator);
    if (!added) return core::Result<Output>{added.error()};
  }
  std::map<time::Tick, std::vector<std::string>> lyrics;
  for (const auto& text : decoded.value().texts) if (text.lyric) lyrics[text.tick].push_back(text.text);
  for (const auto& note : decoded.value().notes) {
    std::u32string text;
    const auto found = lyrics.find(note.start);
    if (found != lyrics.end() && !found->second.empty()) {
      const auto decodedText = domain::fromUtf8(found->second.front());
      if (!decodedText) return core::Result<Output>{decodedText.error()};
      text = decodedText.value();
    }
    auto [lyric, scoreNote] = factory.makeNote(note.start, note.duration, note.midi,
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
      !region->dynamicsAutomation.points().empty() || region->performance.pronunciation.has_value())
    score.issues.push_back({SmfIssueSeverity::Loss, time::Tick{0}, "SEAM phoneme, expression, performance and unit metadata are not representable in SMF v1"});
  const auto valid = score.validate(limits); if (!valid) return core::Result<Output>{valid.error()};
  return score;
}

}  // namespace seam::interchange
