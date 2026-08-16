#include "seam/rendering/render_snapshot.hpp"

#include "seam/build/version.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/core/stable_hash.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"
#include "seam/voicebank/asset_path.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <type_traits>
#include <unordered_set>

namespace seam::rendering {
namespace {

domain::VocalRegion extractPhraseRegion(const domain::VocalRegion& source,
                                         const PhraseSegment& segment) {
  std::unordered_set<domain::NoteId> noteIds;
  noteIds.reserve(segment.noteIds.size());
  for (const auto noteId : segment.noteIds) noteIds.insert(noteId);

  domain::VocalRegion result{
      .id = source.id,
      .name = source.name,
      .startTick = source.startTick,
      .durationTick = source.durationTick,
      .lyrics = {},
      .notes = {},
      .phonemeOverrides = {},
      .unitSelectionOverrides = {},
      .seamOverrides = {},
      .pitchAutomation = {},
  };
  std::unordered_set<domain::LyricTokenId> lyricIds;
  for (const auto& note : source.notes) {
    if (!noteIds.contains(note.id)) continue;
    result.notes.push_back(note);
    lyricIds.insert(note.lyricTokenId);
  }
  for (const auto& lyric : source.lyrics) {
    if (lyricIds.contains(lyric.id)) result.lyrics.push_back(lyric);
  }
  for (const auto& value : source.phonemeOverrides) {
    if (noteIds.contains(value.key.noteId)) result.phonemeOverrides.push_back(value);
  }
  for (const auto& value : source.unitSelectionOverrides) {
    if (noteIds.contains(value.startKey.noteId)) {
      result.unitSelectionOverrides.push_back(value);
    }
  }
  for (const auto& value : source.seamOverrides) {
    if (noteIds.contains(value.incomingStartKey.noteId)) {
      result.seamOverrides.push_back(value);
    }
  }

  const auto& points = source.pitchAutomation.points();
  const domain::PitchAutomationPoint* previous = nullptr;
  const domain::PitchAutomationPoint* next = nullptr;
  for (const auto& point : points) {
    if (point.tick <= segment.startTick) previous = &point;
    if (point.tick >= segment.endTick && next == nullptr) next = &point;
    if (point.tick >= segment.startTick && point.tick <= segment.endTick) {
      static_cast<void>(result.pitchAutomation.upsert(point));
    }
  }
  if (previous != nullptr) static_cast<void>(result.pitchAutomation.upsert(*previous));
  if (next != nullptr) static_cast<void>(result.pitchAutomation.upsert(*next));
  result.sortNotes();
  return result;
}

core::Result<domain::Project> extractPhraseProject(
    const domain::Project& source,
    domain::TrackId trackId,
    const PhraseSegment& segment) {
  const auto* sourceTrack = source.findVocalTrack(trackId);
  const auto* sourceRegion = sourceTrack == nullptr
      ? nullptr
      : sourceTrack->findRegion(segment.regionId);
  if (sourceTrack == nullptr || sourceRegion == nullptr) {
    return core::failure<domain::Project>(
        core::ErrorCode::NotFound,
        "Render snapshot phrase track or region was not found");
  }

  domain::Project result{domain::ProjectId{1}, "Render snapshot", source.ppq()};
  result.settings().sampleRate = source.settings().sampleRate;
  result.settings().characterDisplay = domain::CharacterDisplayMode::Off;
  result.settings().snapEnabled = false;
  result.settings().snapGrid = time::Tick{source.ppq()};

  const auto absolutePhraseEnd = sourceRegion->startTick + segment.endTick;
  for (const auto& event : source.tempoMap().events()) {
    if (event.tick > absolutePhraseEnd) break;
    const auto inserted = result.tempoMap().addOrReplace(event.tick, event.bpm);
    if (!inserted) return core::Result<domain::Project>{inserted.error()};
  }
  for (const auto& event : source.meterMap().events()) {
    if (event.tick > absolutePhraseEnd) break;
    const auto inserted = result.meterMap().addOrReplace(
        event.tick, event.numerator, event.denominator);
    if (!inserted) return core::Result<domain::Project>{inserted.error()};
  }

  auto track = *sourceTrack;
  track.name = "Render track";
  track.character = {};
  track.muted = false;
  track.solo = false;
  track.regions.clear();
  auto phraseRegion = extractPhraseRegion(*sourceRegion, segment);
  phraseRegion.name = "Render phrase";
  track.regions.push_back(std::move(phraseRegion));
  result.vocalTracks().push_back(std::move(track));
  return result;
}

class IdentityWriter final {
public:
  void tag(std::string_view value) {
    addUnsigned(static_cast<std::uint64_t>(value.size()));
    hash_.update(value);
  }

  void boolean(bool value) { addUnsigned(value ? 1U : 0U); }

  template <typename T>
  void integer(T value) requires std::is_integral_v<T> {
    using Unsigned = std::make_unsigned_t<T>;
    addUnsigned(static_cast<std::uint64_t>(static_cast<Unsigned>(value)));
  }

  void floating(float value) {
    integer(std::bit_cast<std::uint32_t>(value));
  }

  void floating(double value) {
    integer(std::bit_cast<std::uint64_t>(value));
  }

  [[nodiscard]] std::string finish() const { return hash_.hexDigest(); }

private:
  void addUnsigned(std::uint64_t value) {
    std::array<std::byte, 8> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
      bytes[index] = static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
    }
    hash_.update(bytes);
  }

  core::Sha256 hash_;
};

template <typename Enum>
void addEnum(IdentityWriter& writer, Enum value) {
  using Underlying = std::underlying_type_t<Enum>;
  writer.integer(static_cast<Underlying>(value));
}

void addPitchCurve(IdentityWriter& writer, const synthesis::PitchCurve& curve) {
  writer.integer(curve.points().size());
  for (const auto& point : curve.points()) {
    writer.integer(point.frame);
    writer.floating(point.cents);
    addEnum(writer, point.interpolation);
  }
}

void addRenderOptions(IdentityWriter& writer,
                      const synthesis::PhraseRenderOptions& options) {
  writer.tag("render-options-v3");
  addEnum(writer, options.renderer.policy);
  writer.boolean(options.renderer.allowRawFallback);
  writer.boolean(options.renderer.rendererOverride.has_value());
  if (options.renderer.rendererOverride.has_value()) {
    addEnum(writer, *options.renderer.rendererOverride);
  }
  writer.floating(options.renderer.raw.loopPrint);
  writer.floating(options.renderer.raw.additionalGainDb);
  writer.floating(options.renderer.psola.sourcePitchResidual);
  writer.floating(options.renderer.psola.additionalGainDb);
  addPitchCurve(writer, options.renderer.psola.pitchCurve);
  writer.integer(options.renderer.spectral.fftSize);
  writer.integer(options.renderer.spectral.hopSize);
  writer.floating(options.renderer.spectral.formantFollow);
  writer.floating(options.renderer.spectral.phaseReset);
  writer.floating(options.renderer.spectral.additionalGainDb);
  addPitchCurve(writer, options.renderer.spectral.pitchCurve);
  writer.integer(options.renderer.stretch.grainSize);
  writer.integer(options.renderer.stretch.hopSize);
  writer.floating(options.renderer.stretch.transientPreservation);
  writer.floating(options.renderer.stretch.sourceDrift);
  writer.floating(options.renderer.stretch.additionalGainDb);
  addPitchCurve(writer, options.renderer.stretch.pitchCurve);
  writer.floating(options.defaultSeam.seamAmount);
  addEnum(writer, options.defaultSeam.curve);
}

void addUnitMetadata(IdentityWriter& writer, const voicebank::Unit& unit) {
  writer.tag(unit.id);
  writer.tag(unit.alias);
  writer.integer(unit.phones.size());
  for (const auto& phone : unit.phones) writer.tag(phone);
  addEnum(writer, unit.kind);
  writer.tag(unit.audioPath.generic_string());
  writer.integer(unit.rootMidi);
  writer.tag(unit.style);
  writer.integer(unit.take);
  writer.integer(unit.priority);
  writer.floating(unit.gainDb);
  addEnum(writer, unit.renderer);
  writer.integer(unit.markers.audioOffset);
  writer.integer(unit.markers.consonantEnd);
  writer.integer(unit.markers.vowelOnset);
  writer.integer(unit.markers.stableStart);
  const auto addOptionalFrame = [&writer](std::optional<time::SampleFrame> value) {
    writer.boolean(value.has_value());
    if (value.has_value()) writer.integer(*value);
  };
  addOptionalFrame(unit.markers.loopStart);
  addOptionalFrame(unit.markers.loopEnd);
  addOptionalFrame(unit.markers.releaseStart);
  writer.integer(unit.markers.audioEnd);
  writer.integer(unit.pitchMarks.size());
  for (const auto& mark : unit.pitchMarks) {
    writer.integer(mark.frame);
    writer.floating(mark.confidence);
    writer.boolean(mark.locked);
  }
  writer.boolean(unit.enabled);
}

void addAlgorithmRevisions(IdentityWriter& writer) {
  writer.tag(build::kRenderAbiId);
  writer.integer(build::kPhonemizerRevision);
  writer.integer(build::kUnitSelectorRevision);
  writer.integer(build::kTimingSolverRevision);
  writer.integer(build::kRawRendererRevision);
  writer.integer(build::kPsolaRendererRevision);
  writer.integer(build::kSpectralRendererRevision);
  writer.integer(build::kStretchRendererRevision);
  writer.integer(build::kSeamComposerRevision);
}

core::Result<std::string> buildIdentity(
    const domain::Project& phraseProject,
    const voicebank::Manifest& manifest,
    const synthesis::UnitPlan& unitPlan,
    const std::vector<SelectedUnitIdentity>& selectedUnits,
    RenderQuality quality,
    std::uint32_t sampleRate,
    std::string_view style,
    const synthesis::PhraseRenderOptions& renderOptions) {
  formats::ProjectJsonCodec codec;
  auto projectJson = codec.encode(phraseProject);
  if (!projectJson) return core::Result<std::string>{projectJson.error()};

  IdentityWriter writer;
  writer.tag("project-seam-render-identity-v3");
  addAlgorithmRevisions(writer);
  writer.tag(projectJson.value());
  addEnum(writer, quality);
  writer.integer(sampleRate);
  writer.tag(style);
  writer.tag(manifest.id);
  writer.tag(manifest.version);
  addEnum(writer, manifest.language);
  writer.integer(manifest.expectedSampleRate);
  addRenderOptions(writer, renderOptions);
  writer.integer(unitPlan.entries.size());
  for (std::size_t index = 0; index < unitPlan.entries.size(); ++index) {
    const auto& entry = unitPlan.entries[index];
    const auto* unit = manifest.findUnit(entry.unitId);
    if (unit == nullptr || index >= selectedUnits.size() ||
        selectedUnits[index].unitId != entry.unitId) {
      return core::failure<std::string>(
          core::ErrorCode::InvariantViolation,
          "Render identity unit plan is not aligned with selected audio");
    }
    writer.integer(entry.tokenStart);
    writer.integer(entry.tokenCount);
    writer.integer(entry.targetMidi);
    writer.boolean(entry.forced);
    addEnum(writer, entry.renderer);
    addUnitMetadata(writer, *unit);
    writer.tag(selectedUnits[index].audioSha256);
  }
  return writer.finish();
}

}  // namespace

std::string fnv1aHex(std::string_view value) {
  core::StableHash64 hash;
  hash.addString(value);
  return hash.hex();
}

core::Result<RenderSnapshot> RenderSnapshotFactory::create(
    const domain::Project& project,
    const voicebank::Manifest& voicebankValue,
    domain::TrackId trackId,
    const PhraseSegment& segment,
    std::uint64_t revision,
    RenderQuality quality,
    std::filesystem::path bankRoot,
    std::uint32_t sampleRate,
    std::string style,
    const synthesis::PhraseRenderOptions& renderOptions) const {
  const auto projectValidation = project.validate();
  if (!projectValidation) return core::Result<RenderSnapshot>{projectValidation.error()};
  const auto bankValidation = voicebankValue.validate();
  if (!bankValidation) return core::Result<RenderSnapshot>{bankValidation.error()};
  const auto* track = project.findVocalTrack(trackId);
  if (track == nullptr) {
    return core::failure<RenderSnapshot>(core::ErrorCode::NotFound,
                                         "Render snapshot track was not found",
                                         trackId.toString());
  }
  const auto* region = track->findRegion(segment.regionId);
  if (region == nullptr) {
    return core::failure<RenderSnapshot>(core::ErrorCode::NotFound,
                                         "Render snapshot region was not found",
                                         segment.regionId.toString());
  }
  if (segment.id.empty() || segment.noteIds.empty() || bankRoot.empty()) {
    return core::failure<RenderSnapshot>(core::ErrorCode::InvalidArgument,
                                         "Render snapshot identity is incomplete");
  }
  for (const auto noteId : segment.noteIds) {
    if (region->findNote(noteId) == nullptr) {
      return core::failure<RenderSnapshot>(core::ErrorCode::InvariantViolation,
                                           "Render segment references a missing note",
                                           noteId.toString());
    }
  }
  if (sampleRate == 0U) {
    const auto configured = project.settings().sampleRate;
    if (configured < 8000.0 || configured > 384000.0) {
      return core::failure<RenderSnapshot>(core::ErrorCode::InvalidArgument,
                                           "Render snapshot sample rate is invalid");
    }
    sampleRate = static_cast<std::uint32_t>(configured);
  }
  if (sampleRate < 8000U || sampleRate > 384000U) {
    return core::failure<RenderSnapshot>(core::ErrorCode::InvalidArgument,
                                         "Render snapshot sample rate is unsupported");
  }
  if (style.empty()) style = voicebankValue.styles.front();
  if (std::find(voicebankValue.styles.begin(), voicebankValue.styles.end(), style) ==
      voicebankValue.styles.end()) {
    return core::failure<RenderSnapshot>(core::ErrorCode::NotFound,
                                         "Render snapshot style is not in the voicebank",
                                         style);
  }

  auto phraseProject = extractPhraseProject(project, trackId, segment);
  if (!phraseProject) return core::Result<RenderSnapshot>{phraseProject.error()};
  const auto phraseValidation = phraseProject.value().validate();
  if (!phraseValidation) return core::Result<RenderSnapshot>{phraseValidation.error()};
  const auto* phraseTrack = phraseProject.value().findVocalTrack(trackId);
  const auto* phraseRegion = phraseTrack == nullptr
      ? nullptr
      : phraseTrack->findRegion(segment.regionId);
  if (phraseRegion == nullptr) {
    return core::failure<RenderSnapshot>(core::ErrorCode::InvariantViolation,
                                         "Extracted phrase region is missing");
  }
  if (voicebankValue.language != domain::Language::Japanese) {
    return core::failure<RenderSnapshot>(
        core::ErrorCode::Unsupported,
        "Phase 4.1 render identity currently supports Japanese voicebanks only");
  }

  phonemizer::JapaneseKanaPhonemizer japanese;
  auto phonemes = japanese.phonemize(*phraseRegion);
  if (phonemes.tokens.empty()) {
    return core::failure<RenderSnapshot>(core::ErrorCode::NotFound,
                                         "Phonemizer produced no renderable tokens");
  }
  synthesis::DeterministicUnitSelector selector;
  auto plan = selector.select(voicebankValue, *phraseRegion, phonemes.tokens,
                              style, phraseRegion->unitSelectionOverrides);
  if (!plan) return core::Result<RenderSnapshot>{plan.error()};

  struct FrozenAsset final {
    std::string sha256;
    std::shared_ptr<const voicebank::AudioBuffer> audio;
  };
  std::map<std::filesystem::path, FrozenAsset> frozenByPath;
  std::vector<SelectedUnitIdentity> selectedUnits;
  std::vector<synthesis::FrozenUnitAudio> frozenAudio;
  selectedUnits.reserve(plan.value().entries.size());
  frozenAudio.reserve(plan.value().entries.size());
  for (const auto& entry : plan.value().entries) {
    const auto* unit = voicebankValue.findUnit(entry.unitId);
    if (unit == nullptr) {
      return core::failure<RenderSnapshot>(core::ErrorCode::NotFound,
                                           "Selected unit is absent from voicebank",
                                           entry.unitId);
    }
    auto resolved = voicebank::resolveBankAsset(bankRoot, unit->audioPath);
    if (!resolved) return core::Result<RenderSnapshot>{resolved.error()};
    auto asset = frozenByPath.find(resolved.value());
    if (asset == frozenByPath.end()) {
      auto bytes = core::readFileBytesLimited(
          resolved.value(), voicebank::kMaximumSupportedWavBytes);
      if (!bytes) return core::Result<RenderSnapshot>{bytes.error()};
      auto decoded = voicebank::readWav(bytes.value(), resolved.value().string());
      if (!decoded) return core::Result<RenderSnapshot>{decoded.error()};
      asset = frozenByPath.emplace(
          resolved.value(),
          FrozenAsset{
              .sha256 = core::sha256Hex(bytes.value()),
              .audio = std::make_shared<const voicebank::AudioBuffer>(
                  std::move(decoded).value()),
          }).first;
    }
    selectedUnits.push_back(SelectedUnitIdentity{
        .unitId = entry.unitId,
        .audioSha256 = asset->second.sha256,
    });
    frozenAudio.push_back(synthesis::FrozenUnitAudio{
        .unitId = entry.unitId,
        .audio = asset->second.audio,
    });
  }

  auto identity = buildIdentity(phraseProject.value(), voicebankValue,
                                plan.value(), selectedUnits, quality,
                                sampleRate, style, renderOptions);
  if (!identity) return core::Result<RenderSnapshot>{identity.error()};

  return RenderSnapshot{
      .revision = revision,
      .quality = quality,
      .renderAbiId = std::string{build::kRenderAbiId},
      .contentHash = std::move(identity).value(),
      .segment = segment,
      .trackId = trackId,
      .project = std::make_shared<const domain::Project>(
          std::move(phraseProject).value()),
      .voicebank = std::make_shared<const voicebank::Manifest>(voicebankValue),
      .phonemes = std::make_shared<const phonemizer::Result>(std::move(phonemes)),
      .unitPlan = std::make_shared<const synthesis::UnitPlan>(std::move(plan).value()),
      .selectedUnits = std::move(selectedUnits),
      .frozenAudio = std::move(frozenAudio),
      .renderOptions = renderOptions,
      .bankRoot = std::move(bankRoot),
      .sampleRate = sampleRate,
      .style = std::move(style),
  };
}

}  // namespace seam::rendering
