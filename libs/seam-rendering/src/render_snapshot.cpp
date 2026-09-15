#include "seam/rendering/render_snapshot.hpp"
#include "seam/neural_synthesis/diffsinger_inputs.hpp"
#include "seam/rendering/render_pipeline.hpp"

#include "seam/build/version.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/core/stable_hash.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/phonemizer/language_resolver.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"
#include "seam/phonemizer/language_resolver.hpp"
#include "seam/voicebank/asset_path.hpp"
#include "seam/voice_design/procedural_renderer.hpp"
#include "seam/voice_design/vocal_tract.hpp"

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

constexpr std::uint64_t kMaximumFrozenPhraseEncodedBytes =
    256ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumFrozenPhraseDecodedBytes =
    512ULL * 1024ULL * 1024ULL;

// The formant channel is a control a concatenative bank and an admitted model do not have. A curve
// that asks for one would otherwise be dropped in silence, which is exactly what the capability rule
// forbids, so the request is refused by name instead. A curve that is entirely neutral is not a
// request.
bool requiresFormantShift(const domain::VocalRegion& region) noexcept {
  return std::any_of(region.formantAutomation.points().begin(),
                     region.formantAutomation.points().end(),
                     [](const domain::FormantAutomationPoint& point) {
                       return point.semitones != 0.0F;
                     });
}

std::string formantShiftUnsupportedMessage(std::string_view carrier) {
  return std::string{"The selected "} + std::string{carrier} +
         " cannot apply the project's formant curve: it has no vocal-tract resonances of its own, so "
         "the shift would be dropped in silence. Remove the curve or select a source-filter singer.";
}

// Breathiness is the same kind of request one layer down: it rebalances the excitation the source-filter
// engine generates for itself, and neither a concatenative bank nor an admitted model has that
// excitation to rebalance. A curve that asks for nothing is not a request.
bool requiresBreathiness(const domain::VocalRegion& region) noexcept {
  return std::any_of(region.breathinessAutomation.points().begin(),
                     region.breathinessAutomation.points().end(),
                     [](const domain::BreathinessAutomationPoint& point) {
                       return point.amount != 0.0F;
                     });
}

std::string breathinessUnsupportedMessage(std::string_view carrier) {
  return std::string{"The selected "} + std::string{carrier} +
         " cannot apply the project's breathiness curve: it does not generate the excitation that would "
         "be rebalanced, so the curve would be dropped in silence. Remove the curve or select a "
         "source-filter singer.";
}

// Tension is the same request again, on the source's own spectrum: neither a concatenative bank nor an
// admitted model hands the application the harmonic source it would have to tilt. A curve that asks for
// nothing is not a request.
bool requiresTension(const domain::VocalRegion& region) noexcept {
  return std::any_of(region.tensionAutomation.points().begin(),
                     region.tensionAutomation.points().end(),
                     [](const domain::TensionAutomationPoint& point) {
                       return point.amount != 0.0F;
                     });
}

std::string tensionUnsupportedMessage(std::string_view carrier) {
  return std::string{"The selected "} + std::string{carrier} +
         " cannot apply the project's tension curve: it does not generate the harmonic source whose "
         "spectrum would change, so the curve would be dropped in silence. Remove the curve or select a "
         "source-filter singer.";
}

// Airiness is a band of the source's own noise, which is the same kind of request as breathiness and
// tension: the carrier has to own the source to have a band to shape.
bool requiresAiriness(const domain::VocalRegion& region) noexcept {
  return std::any_of(region.airinessAutomation.points().begin(),
                     region.airinessAutomation.points().end(),
                     [](const domain::AirinessAutomationPoint& point) {
                       return point.amount != 0.0F;
                     });
}

std::string airinessUnsupportedMessage(std::string_view carrier) {
  return std::string{"The selected "} + std::string{carrier} +
         " cannot apply the project's airiness curve: it does not generate the noise band the curve "
         "would add, so the curve would be dropped in silence. Remove the curve or select a "
         "source-filter singer.";
}

// Gender is the coupled request: it needs the carrier to own both the tract and the source, because
// applying half of it would silently become a different channel.
bool requiresGender(const domain::VocalRegion& region) noexcept {
  return std::any_of(region.genderAutomation.points().begin(),
                     region.genderAutomation.points().end(),
                     [](const domain::GenderAutomationPoint& point) {
                       return point.amount != 0.0F;
                     });
}

std::string genderUnsupportedMessage(std::string_view carrier) {
  return std::string{"The selected "} + std::string{carrier} +
         " cannot apply the project's gender curve: gender moves the tract's resonances and the source's "
         "spectrum together, and this carrier owns neither half, so the curve would be dropped in "
         "silence. Remove the curve or select a source-filter singer.";
}

// Growl is a roughness of the source's own excitation, which a bank and an admitted model do not hand the
// application to shape. A curve that asks for nothing is not a request.
bool requiresGrowl(const domain::VocalRegion& region) noexcept {
  return std::any_of(region.growlAutomation.points().begin(),
                     region.growlAutomation.points().end(),
                     [](const domain::GrowlAutomationPoint& point) {
                       return point.amount != 0.0F;
                     });
}

std::string growlUnsupportedMessage(std::string_view carrier) {
  return std::string{"The selected "} + std::string{carrier} +
         " cannot apply the project's growl curve: it does not generate the excitation the roughness is "
         "added to, so the curve would be dropped in silence. Remove the curve or select a source-filter "
         "singer.";
}

core::Result<domain::VocalRegion> extractPhraseRegion(
    const domain::VocalRegion& source, const PhraseSegment& segment) {
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
  const auto& dynamics = source.dynamicsAutomation.points();
  const auto beforeTick = [](const domain::DynamicsAutomationPoint& point,
                             time::Tick tick) { return point.tick < tick; };
  auto first = std::lower_bound(dynamics.begin(), dynamics.end(),
                                segment.startTick, beforeTick);
  if (first != dynamics.begin() &&
      (first == dynamics.end() || first->tick > segment.startTick)) {
    --first;
  }
  auto last = std::lower_bound(first, dynamics.end(), segment.endTick, beforeTick);
  if (last != dynamics.end()) ++last;
  const auto copied = result.dynamicsAutomation.replacePoints(
      std::vector<domain::DynamicsAutomationPoint>{first, last});
  if (!copied) return core::Result<domain::VocalRegion>{copied.error()};
  // The formant curve is windowed exactly like the dynamics curve: the point in force at the phrase
  // start, the points inside it, and the one that follows, so a phrase never ends on the value of a
  // point it does not contain.
  const auto& formant = source.formantAutomation.points();
  const auto beforeFormantTick = [](const domain::FormantAutomationPoint& point,
                                    time::Tick tick) { return point.tick < tick; };
  auto firstFormant = std::lower_bound(formant.begin(), formant.end(), segment.startTick,
                                       beforeFormantTick);
  if (firstFormant != formant.begin() &&
      (firstFormant == formant.end() || firstFormant->tick > segment.startTick)) {
    --firstFormant;
  }
  auto lastFormant = std::lower_bound(firstFormant, formant.end(), segment.endTick,
                                      beforeFormantTick);
  if (lastFormant != formant.end()) ++lastFormant;
  const auto formantCopied = result.formantAutomation.replacePoints(
      std::vector<domain::FormantAutomationPoint>{firstFormant, lastFormant});
  if (!formantCopied) return core::Result<domain::VocalRegion>{formantCopied.error()};
  // The breathiness curve is windowed by the same rule, so the phrase carries the balance that is in
  // force where it starts, every change inside it, and the one that follows.
  const auto& breathiness = source.breathinessAutomation.points();
  const auto beforeBreathinessTick = [](const domain::BreathinessAutomationPoint& point,
                                        time::Tick tick) { return point.tick < tick; };
  auto firstBreathiness = std::lower_bound(breathiness.begin(), breathiness.end(),
                                           segment.startTick, beforeBreathinessTick);
  if (firstBreathiness != breathiness.begin() &&
      (firstBreathiness == breathiness.end() || firstBreathiness->tick > segment.startTick)) {
    --firstBreathiness;
  }
  auto lastBreathiness = std::lower_bound(firstBreathiness, breathiness.end(), segment.endTick,
                                          beforeBreathinessTick);
  if (lastBreathiness != breathiness.end()) ++lastBreathiness;
  const auto breathinessCopied = result.breathinessAutomation.replacePoints(
      std::vector<domain::BreathinessAutomationPoint>{firstBreathiness, lastBreathiness});
  if (!breathinessCopied) return core::Result<domain::VocalRegion>{breathinessCopied.error()};
  // The tension curve is windowed by the same rule as the curves beside it.
  const auto& tension = source.tensionAutomation.points();
  const auto beforeTensionTick = [](const domain::TensionAutomationPoint& point,
                                    time::Tick tick) { return point.tick < tick; };
  auto firstTension = std::lower_bound(tension.begin(), tension.end(), segment.startTick,
                                       beforeTensionTick);
  if (firstTension != tension.begin() &&
      (firstTension == tension.end() || firstTension->tick > segment.startTick)) {
    --firstTension;
  }
  auto lastTension = std::lower_bound(firstTension, tension.end(), segment.endTick,
                                      beforeTensionTick);
  if (lastTension != tension.end()) ++lastTension;
  const auto tensionCopied = result.tensionAutomation.replacePoints(
      std::vector<domain::TensionAutomationPoint>{firstTension, lastTension});
  if (!tensionCopied) return core::Result<domain::VocalRegion>{tensionCopied.error()};
  // The airiness curve is windowed by the same rule as the curves beside it.
  const auto& airiness = source.airinessAutomation.points();
  const auto beforeAirinessTick = [](const domain::AirinessAutomationPoint& point,
                                     time::Tick tick) { return point.tick < tick; };
  auto firstAiriness = std::lower_bound(airiness.begin(), airiness.end(), segment.startTick,
                                        beforeAirinessTick);
  if (firstAiriness != airiness.begin() &&
      (firstAiriness == airiness.end() || firstAiriness->tick > segment.startTick)) {
    --firstAiriness;
  }
  auto lastAiriness = std::lower_bound(firstAiriness, airiness.end(), segment.endTick,
                                       beforeAirinessTick);
  if (lastAiriness != airiness.end()) ++lastAiriness;
  const auto airinessCopied = result.airinessAutomation.replacePoints(
      std::vector<domain::AirinessAutomationPoint>{firstAiriness, lastAiriness});
  if (!airinessCopied) return core::Result<domain::VocalRegion>{airinessCopied.error()};
  // The gender curve is windowed by the same rule as the curves beside it.
  const auto& gender = source.genderAutomation.points();
  const auto beforeGenderTick = [](const domain::GenderAutomationPoint& point,
                                   time::Tick tick) { return point.tick < tick; };
  auto firstGender = std::lower_bound(gender.begin(), gender.end(), segment.startTick,
                                      beforeGenderTick);
  if (firstGender != gender.begin() &&
      (firstGender == gender.end() || firstGender->tick > segment.startTick)) {
    --firstGender;
  }
  auto lastGender = std::lower_bound(firstGender, gender.end(), segment.endTick, beforeGenderTick);
  if (lastGender != gender.end()) ++lastGender;
  const auto genderCopied = result.genderAutomation.replacePoints(
      std::vector<domain::GenderAutomationPoint>{firstGender, lastGender});
  if (!genderCopied) return core::Result<domain::VocalRegion>{genderCopied.error()};
  // The growl curve is windowed by the same rule as the curves beside it.
  const auto& growl = source.growlAutomation.points();
  const auto beforeGrowlTick = [](const domain::GrowlAutomationPoint& point,
                                  time::Tick tick) { return point.tick < tick; };
  auto firstGrowl = std::lower_bound(growl.begin(), growl.end(), segment.startTick,
                                     beforeGrowlTick);
  if (firstGrowl != growl.begin() &&
      (firstGrowl == growl.end() || firstGrowl->tick > segment.startTick)) {
    --firstGrowl;
  }
  auto lastGrowl = std::lower_bound(firstGrowl, growl.end(), segment.endTick, beforeGrowlTick);
  if (lastGrowl != growl.end()) ++lastGrowl;
  const auto growlCopied = result.growlAutomation.replacePoints(
      std::vector<domain::GrowlAutomationPoint>{firstGrowl, lastGrowl});
  if (!growlCopied) return core::Result<domain::VocalRegion>{growlCopied.error()};
  const auto effectiveScope = [&](const domain::PerformanceScope& scope)
      -> std::optional<domain::PerformanceScope> {
    if (const auto* note = std::get_if<domain::NoteId>(&scope)) {
      if (noteIds.contains(*note)) return *note;
      return std::nullopt;
    }
    const auto range = std::get<domain::PerformanceTimeRange>(scope);
    const auto start = std::max(range.startTick, segment.startTick);
    const auto end = std::min(range.endTick, segment.endTick);
    if (start >= end) return std::nullopt;
    return domain::PerformanceTimeRange{start, end};
  };
  result.performance.pronunciation = source.performance.pronunciation;
  for (const auto& entry : source.performance.ownership) {
    const auto scope = effectiveScope(entry.scope);
    if (!scope.has_value()) continue;
    auto selected = entry;
    selected.scope = *scope;
    selected.revision = {};
    result.performance.ownership.push_back(std::move(selected));
  }
  std::unordered_set<std::string> selectedTakes;
  for (const auto& entry : source.performance.accepted) {
    const auto scope = effectiveScope(entry.scope);
    if (!scope.has_value()) continue;
    auto selected = entry;
    selected.scope = *scope;
    selectedTakes.insert(selected.takeId);
    result.performance.accepted.push_back(std::move(selected));
  }
  for (const auto& take : source.performance.takes) {
    if (selectedTakes.contains(take.id)) result.performance.takes.push_back(take);
  }
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
  // Phrase snapshots are renderer-local mono sources. Project routing is
  // applied later by ProductionProjectRenderer, so the extracted snapshot
  // must use its own canonical stereo routing rather than retaining the
  // source project's potentially 1-8 channel track matrix.
  track.outputRoute = domain::TrackOutputRoute{
      .bus = domain::BusId{1U},
      .matrix = domain::RoutingMatrix::monoToStereo(track.pan),
  };
  track.regions.clear();
  auto phraseRegion = extractPhraseRegion(*sourceRegion, segment);
  if (!phraseRegion) return core::Result<domain::Project>{phraseRegion.error()};
  phraseRegion.value().name = "Render phrase";
  track.regions.push_back(std::move(phraseRegion).value());
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
  writer.tag("render-options-v4");
  addEnum(writer, options.renderer.policy);
  writer.boolean(options.renderer.allowRawFallback);
  writer.boolean(options.renderer.rendererOverride.has_value());
  if (options.renderer.rendererOverride.has_value()) {
    addEnum(writer, *options.renderer.rendererOverride);
  }
  writer.floating(options.renderer.raw.loopPrint);
  writer.floating(options.renderer.raw.additionalGainDb);
  addPitchCurve(writer, options.renderer.raw.pitchCurve);
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
  writer.tag("required-controls-v1");
  for (const auto required : options.renderer.controls.required) {
    writer.boolean(required);
  }
  writer.boolean(options.renderer.controls.requiresPitchPreservingTransient);
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
    const synthesis::PhraseRenderOptions& renderOptions,
    std::optional<synthesis::PhraseFrameRange> ownedFrames) {
  formats::ProjectJsonCodec codec;
  auto projectJson = codec.encode(
      phraseProject, formats::ProjectJsonEncodeOptions{.includeRendererProvenance = false});
  if (!projectJson) return core::Result<std::string>{projectJson.error()};

  IdentityWriter writer;
  writer.tag("project-seam-render-identity-v7");
  writer.integer(ownedFrames.has_value() ? 1U : 0U);
  if (ownedFrames) { writer.integer(ownedFrames->start); writer.integer(ownedFrames->end); }
  writer.tag("resource.sample.v1");
  writer.integer(synthesis::kPerformanceCompilerRevision);
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
    writer.tag(selectedUnits[index].sourceAlignmentSha256);
  }
  return writer.finish();
}

core::Result<std::string> buildProceduralIdentity(const domain::Project& project,
    const synthesis::ProceduralSingerResource& resource, const domain::PronunciationIdentity& pronunciation,
    std::string_view style, RenderQuality quality, std::uint32_t sampleRate,
    std::optional<synthesis::PhraseFrameRange> ownedFrames) {
  // Identity excludes the recorded renderer. Which build produced a sound is a fact about the audio,
  // not an input to what the audio is, so a stamp must not move the identity it describes.
  const auto json = formats::ProjectJsonCodec{}.encode(
      project, formats::ProjectJsonEncodeOptions{.includeRendererProvenance = false});
  if (!json) return core::Result<std::string>{json.error()};
  IdentityWriter identity;
  identity.tag("project-seam-procedural-articulation-v2");
  identity.integer(voice_design::kSustainedPoseRendererRevision);
  identity.integer(voice_design::ArticulatedStream::algorithmRevision);
  identity.integer(voice_design::ArticulationPlan::algorithmRevision);
  identity.integer(voice_design::FricationGestureStream::algorithmRevision);
  identity.integer(voice_design::FricationSource::algorithmRevision);
  identity.integer(voice_design::PlosiveSource::algorithmRevision);
  if (resource.identity.version=="6") identity.integer(voice_design::VoicedPlosiveSource::algorithmRevision);
  identity.integer(synthesis::kPerformanceCompilerRevision);
  identity.integer(synthesis::kProceduralTimingPolicyRevision);
  identity.tag(build::kRenderAbiId); identity.tag(json.value());
  identity.tag(resource.identity.id); identity.tag(resource.identity.version); identity.tag(resource.identity.contentHash);
  identity.tag(pronunciation.resourceHash); identity.tag(pronunciation.sequenceHash);
  identity.tag(style); identity.integer(sampleRate); addEnum(identity, quality);
  identity.boolean(ownedFrames.has_value());
  if (ownedFrames) { identity.integer(ownedFrames->start); identity.integer(ownedFrames->end); }
  return identity.finish();
}

core::Result<std::string> buildNeuralIdentity(const domain::Project& project,
    const neural_synthesis::AdmittedNeuralBundle& bundle, const domain::PronunciationIdentity& pronunciation,
    const NeuralRenderProvenance& provenance, std::string_view style, RenderQuality quality,
    std::uint32_t sampleRate, std::optional<synthesis::PhraseFrameRange> ownedFrames) {
  const auto json = formats::ProjectJsonCodec{}.encode(
      project, formats::ProjectJsonEncodeOptions{.includeRendererProvenance = false});
  if (!json) return core::Result<std::string>{json.error()};
  const auto& execution = bundle.execution();
  const auto& metadata = bundle.metadata();
  IdentityWriter identity;
  identity.tag("project-seam-neural-bundle-v1");
  identity.integer(synthesis::kPerformanceCompilerRevision);
  identity.integer(synthesis::kProceduralTimingPolicyRevision);
  identity.integer(neural_synthesis::kDiffSingerInputRevision);
  identity.tag(build::kRenderAbiId);
  identity.tag(json.value());
  // Model identity, feature declaration and declared controls all participate:
  // two bundles with the same music but any different declaration must not share
  // cached audio.
  identity.tag(execution.modelId);
  identity.tag(execution.modelVersion);
  identity.tag(execution.bundleContentHash);
  identity.integer(execution.configurationVersion);
  identity.integer(execution.inferenceSteps);
  identity.integer(metadata.model.maximumFrames);
  identity.integer(metadata.features.sampleRate);
  identity.integer(metadata.features.hopSize);
  identity.integer(metadata.features.bins);
  identity.tag(metadata.features.layout);
  identity.tag(metadata.features.amplitudeScale);
  identity.floating(metadata.features.multiplier);
  identity.floating(metadata.features.offset);
  identity.floating(metadata.features.minimumHz);
  identity.floating(metadata.features.maximumHz);
  identity.tag(metadata.model.vocabularyHash);
  identity.tag(metadata.stepsLayout);
  identity.tag(metadata.vocoderOutput);
  identity.tag(provenance.workerVersion);
  identity.tag(provenance.runtimeVersion);
  identity.tag(provenance.provider);
  identity.tag(pronunciation.resourceHash);
  identity.tag(pronunciation.sequenceHash);
  identity.tag(style);
  identity.integer(sampleRate);
  addEnum(identity, quality);
  identity.boolean(ownedFrames.has_value());
  if (ownedFrames) { identity.integer(ownedFrames->start); identity.integer(ownedFrames->end); }
  return identity.finish();
}

}  // namespace

core::Result<void> NeuralRenderProvenance::validate() const {
  const auto acceptable=[](std::string_view value) {
    return !value.empty() && value.size()<=256U &&
        std::all_of(value.begin(),value.end(),[](char c){return static_cast<unsigned char>(c)>=32U && static_cast<unsigned char>(c)<127U;});
  };
  if (!acceptable(workerVersion) || !acceptable(runtimeVersion) || !acceptable(provider))
    return core::failure(core::ErrorCode::InvalidArgument,
        "Neural render provenance requires a bounded printable worker, runtime and provider identity");
  return core::success();
}

RenderResourceFamily renderResourceFamily(const RenderSnapshot& snapshot) noexcept {
  if (snapshot.neuralExecution) return RenderResourceFamily::Neural;
  if (std::holds_alternative<synthesis::ProceduralSingerResource>(snapshot.resource)) return RenderResourceFamily::Procedural;
  if (std::holds_alternative<synthesis::NeuralSingerResource>(snapshot.resource)) return RenderResourceFamily::Neural;
  return RenderResourceFamily::Sample;
}

std::string fnv1aHex(std::string_view value) {
  core::StableHash64 hash;
  hash.addString(value);
  return hash.hex();
}

core::Result<RenderSnapshot> RenderSnapshotFactory::createProcedural(
    const domain::Project& project, const synthesis::ProceduralSingerResource& resource,
    domain::TrackId trackId, domain::RegionId regionId, std::uint64_t revision,
    RenderQuality quality, std::uint32_t sampleRate, std::string style,
    std::optional<synthesis::PhraseFrameRange> ownedFrames) const {
  const auto valid = project.validate();
  if (!valid) return core::Result<RenderSnapshot>{valid.error()};
  const auto* track = project.findVocalTrack(trackId);
  const auto* region = track ? track->findRegion(regionId) : nullptr;
  if (track && track->proceduralRecipe &&
      (track->proceduralRecipe->resource != resource.identity || track->proceduralRecipe->style != style)) {
    return core::failure<RenderSnapshot>(core::ErrorCode::Conflict, "Procedural snapshot differs from the saved recipe selection");
  }
  if (!region || region->notes.empty() || region->notes.size() > 4096U || sampleRate < 8000U || sampleRate > 384000U) {
    return core::failure<RenderSnapshot>(core::ErrorCode::InvalidArgument, "Procedural snapshot region/rate is invalid");
  }
  const auto recipe = voice_design::decodeVoiceRecipeResource(resource);
  if (!recipe) return core::Result<RenderSnapshot>{recipe.error()};
  // Select the same explicit language service used by native inspection and
  // sample snapshots. A recipe still has to advertise matching poses or
  // frication bindings; language resolution alone must never invent a voice
  // resource for symbols it cannot render.
  const auto pronunciation = phonemizer::resolvePronunciation(*region);
  if (!pronunciation) return core::Result<RenderSnapshot>{pronunciation.error()};
  const auto phrase = voice_design::validateProceduralPhrase(*region, pronunciation.value().pronunciation.tokens);
  if (!phrase) return core::Result<RenderSnapshot>{phrase.error()};
  const bool articulated = voice_design::requiresArticulation(pronunciation.value().pronunciation.tokens);
  if (!articulated) {
    const auto tract = voice_design::validateVowelRecipePoses(recipe.value(), pronunciation.value().pronunciation.tokens, style, sampleRate);
    if (!tract) return core::Result<RenderSnapshot>{tract.error()};
  }
  PhraseSegment segment{.id = regionId.toString() + ":procedural", .regionId = regionId,
      .startTick = region->notes.front().startTick, .endTick = region->notes.front().endTick(), .noteIds = {}};
  for (const auto& note : region->notes) {
    segment.startTick = std::min(segment.startTick, note.startTick);
    segment.endTick = std::max(segment.endTick, note.endTick());
    segment.noteIds.push_back(note.id);
  }
  auto frozenProject = extractPhraseProject(project, trackId, segment);
  if (!frozenProject) return core::Result<RenderSnapshot>{frozenProject.error()};
  const auto* frozenRegion = frozenProject.value().findRegion(regionId);
  const auto performance = synthesis::compileScorePerformance(frozenProject.value(), *frozenRegion, sampleRate,
      pronunciation.value().pronunciation.tokens, synthesis::PhonemeTimingPolicy::ProceduralInNote);
  if (!performance) return core::Result<RenderSnapshot>{performance.error()};
  if (articulated) {
    const auto plan = voice_design::ArticulationPlan::compileRecipe(resource, performance.value(),
        pronunciation.value().pronunciation.tokens, style);
    if (!plan) return core::Result<RenderSnapshot>{plan.error()};
  } else {
    const auto timing = voice_design::validateVowelTiming(performance.value());
    if (!timing) return core::Result<RenderSnapshot>{timing.error()};
  }
  const synthesis::PhraseFrameRange context{performance.value().notes().front().startFrame, performance.value().notes().back().endFrame};
  const auto outputValid = synthesis::PhraseOutputContract{sampleRate, context, ownedFrames.value_or(context)}.validate();
  if (!outputValid) return core::Result<RenderSnapshot>{outputValid.error()};
  const auto identity = buildProceduralIdentity(frozenProject.value(), resource, pronunciation.value().identity,
      style, quality, sampleRate, ownedFrames);
  if (!identity) return core::Result<RenderSnapshot>{identity.error()};
  return RenderSnapshot{.revision = revision, .quality = quality, .renderAbiId = std::string{build::kRenderAbiId},
      .contentHash = identity.value(), .segment = std::move(segment), .trackId = trackId, .sourceProjectId = project.id(),
      .project = std::make_shared<const domain::Project>(std::move(frozenProject).value()),
      .phonemes = std::make_shared<const phonemizer::Result>(pronunciation.value().pronunciation), .resource = resource,
      .sampleRate = sampleRate, .style = std::move(style), .pronunciationIdentity = pronunciation.value().identity,
      .compiledPerformance = std::make_shared<const synthesis::CompiledScorePerformance>(performance.value()), .ownedFrames = ownedFrames};
}

core::Result<RenderSnapshot> RenderSnapshotFactory::createNeural(
    const domain::Project& project, const neural_synthesis::AdmittedNeuralBundle& bundle,
    const NeuralRenderProvenance& provenance, domain::TrackId trackId, domain::RegionId regionId,
    std::uint64_t revision, RenderQuality quality, std::uint32_t sampleRate, std::string style,
    std::optional<synthesis::PhraseFrameRange> ownedFrames) const {
  if (!bundle.valid()) return core::failure<RenderSnapshot>(core::ErrorCode::InvalidArgument,
      "Neural snapshot requires an admitted model bundle");
  const auto provenanceValid = provenance.validate();
  if (!provenanceValid) return core::Result<RenderSnapshot>{provenanceValid.error()};
  const auto valid = project.validate();
  if (!valid) return core::Result<RenderSnapshot>{valid.error()};
  const auto* track = project.findVocalTrack(trackId);
  const auto* region = track ? track->findRegion(regionId) : nullptr;
  if (!track || !region || region->notes.empty() || region->notes.size() > 4096U ||
      sampleRate < 8000U || sampleRate > 384000U) {
    return core::failure<RenderSnapshot>(core::ErrorCode::InvalidArgument,
        "Neural snapshot track, region or output rate is invalid");
  }
  if (track->proceduralRecipe) return core::failure<RenderSnapshot>(core::ErrorCode::Conflict,
      "A saved procedural singer cannot render through an admitted neural bundle");
  if (requiresFormantShift(*region))
    return core::failure<RenderSnapshot>(core::ErrorCode::Unsupported,
        formantShiftUnsupportedMessage("neural model"), trackId.toString());
  if (requiresBreathiness(*region))
    return core::failure<RenderSnapshot>(core::ErrorCode::Unsupported,
        breathinessUnsupportedMessage("neural model"), trackId.toString());
  if (requiresTension(*region))
    return core::failure<RenderSnapshot>(core::ErrorCode::Unsupported,
        tensionUnsupportedMessage("neural model"), trackId.toString());
  if (requiresAiriness(*region))
    return core::failure<RenderSnapshot>(core::ErrorCode::Unsupported,
        airinessUnsupportedMessage("neural model"), trackId.toString());
  if (requiresGender(*region))
    return core::failure<RenderSnapshot>(core::ErrorCode::Unsupported,
        genderUnsupportedMessage("neural model"), trackId.toString());
  if (requiresGrowl(*region))
    return core::failure<RenderSnapshot>(core::ErrorCode::Unsupported,
        growlUnsupportedMessage("neural model"), trackId.toString());
  // A persisted selection is a promise about which voice this music used. A
  // snapshot may run unbound for a preview, but it may never contradict a saved
  // selection, and it may never silently substitute a different bundle.
  if (track->neuralResource && (track->neuralResource->resource.id != bundle.execution().modelId ||
      track->neuralResource->resource.version != bundle.execution().modelVersion ||
      track->neuralResource->resource.contentHash != bundle.execution().bundleContentHash))
    return core::failure<RenderSnapshot>(core::ErrorCode::Conflict,
        "Neural snapshot bundle differs from the saved track selection");
  // The admitted acoustic graph is bound to one rate and hop; resampling the
  // request would silently change the model's input contract.
  if (bundle.metadata().model.sampleRate != sampleRate) return core::failure<RenderSnapshot>(
      core::ErrorCode::Conflict, "Neural snapshot rate differs from the admitted model rate");
  const auto pronunciation = phonemizer::resolvePronunciation(*region);
  if (!pronunciation) return core::Result<RenderSnapshot>{pronunciation.error()};
  PhraseSegment segment{.id = regionId.toString() + ":neural", .regionId = regionId,
      .startTick = region->notes.front().startTick, .endTick = region->notes.front().endTick(), .noteIds = {}};
  for (const auto& note : region->notes) {
    segment.startTick = std::min(segment.startTick, note.startTick);
    segment.endTick = std::max(segment.endTick, note.endTick());
    segment.noteIds.push_back(note.id);
  }
  auto frozenProject = extractPhraseProject(project, trackId, segment);
  if (!frozenProject) return core::Result<RenderSnapshot>{frozenProject.error()};
  const auto* frozenRegion = frozenProject.value().findRegion(regionId);
  if (!frozenRegion) return core::failure<RenderSnapshot>(core::ErrorCode::InvariantViolation,
      "Neural phrase extraction lost its source region");
  // A neural bundle has no bank source and no recipe poses, so consonant timing
  // uses the existing source-independent in-note policy with its documented
  // engineering default rather than a measured phonetic duration. The worker
  // receives that compiled timing and never re-derives it, and the policy
  // revision participates in the render identity above.
  const auto performance = synthesis::compileScorePerformance(frozenProject.value(), *frozenRegion, sampleRate,
      pronunciation.value().pronunciation.tokens, synthesis::PhonemeTimingPolicy::ProceduralInNote);
  if (!performance) return core::Result<RenderSnapshot>{performance.error()};
  const synthesis::PhraseFrameRange context{performance.value().notes().front().startFrame,
      performance.value().notes().back().endFrame};
  const auto outputValid = synthesis::PhraseOutputContract{sampleRate, context, ownedFrames.value_or(context)}.validate();
  if (!outputValid) return core::Result<RenderSnapshot>{outputValid.error()};
  const auto identity = buildNeuralIdentity(frozenProject.value(), bundle, pronunciation.value().identity,
      provenance, style, quality, sampleRate, ownedFrames);
  if (!identity) return core::Result<RenderSnapshot>{identity.error()};
  return RenderSnapshot{.revision = revision, .quality = quality, .renderAbiId = std::string{build::kRenderAbiId},
      .contentHash = identity.value(), .segment = std::move(segment), .trackId = trackId,
      .sourceProjectId = project.id(),
      .project = std::make_shared<const domain::Project>(std::move(frozenProject).value()),
      .phonemes = std::make_shared<const phonemizer::Result>(pronunciation.value().pronunciation),
      // Inert family tag only; the admitted bundle is the execution carrier.
      .resource = synthesis::NeuralSingerResource{},
      .sampleRate = sampleRate, .style = std::move(style),
      .pronunciationIdentity = pronunciation.value().identity,
      .compiledPerformance = std::make_shared<const synthesis::CompiledScorePerformance>(performance.value()),
      .ownedFrames = ownedFrames,
      .neuralExecution = std::make_shared<const neural_synthesis::AdmittedNeuralBundle>(bundle)};
}

core::Result<std::vector<RenderSnapshot>> RenderSnapshotFactory::splitOwnedOutput(
    const RenderSnapshot& source, synthesis::PhraseFrameRange output,
    std::uint32_t maximumChunkFrames, std::size_t maximumChunks) const {
  using Output = std::vector<RenderSnapshot>;
  const auto windows = synthesis::planOwnedPhraseWindows(output, maximumChunkFrames, maximumChunks);
  if (!windows) return core::Result<Output>{windows.error()};
  // Subdivision needs a per-window identity that includes the admitted bundle.
  // Refuse it explicitly instead of letting a neural snapshot fall into the
  // sample branch and fail on missing sample material.
  if (source.neuralExecution) return core::failure<Output>(core::ErrorCode::Unsupported,
      "Neural snapshot subdivision is not implemented for prepared bundles");
  if (const auto* procedural = std::get_if<synthesis::ProceduralSingerResource>(&source.resource)) {
    if (!source.sourceProjectId.valid() || !source.pronunciationIdentity) return core::failure<Output>(
        core::ErrorCode::InvalidArgument, "Procedural chunk source identity is missing");
    if (source.ownedFrames && (output.start < source.ownedFrames->start || output.end > source.ownedFrames->end)) {
      return core::failure<Output>(core::ErrorCode::Conflict, "Chunk subdivision cannot expand existing output ownership");
    }
    auto requested = source; requested.ownedFrames = output;
    const auto valid = validateProceduralSnapshot(requested);
    if (!valid) return core::Result<Output>{valid.error()};
    if (source.segment.noteIds.size() > 65536U / windows.value().size()) return core::failure<Output>(
        core::ErrorCode::Unsupported, "Procedural chunks exceed aggregate note metadata budget");
    Output result;
    result.reserve(windows.value().size());
    for (const auto window : windows.value()) {
      const auto identity = buildProceduralIdentity(*source.project, *procedural, *source.pronunciationIdentity,
          source.style, source.quality, source.sampleRate, window);
      if (!identity) return core::Result<Output>{identity.error()};
      auto chunk = source; chunk.ownedFrames = window; chunk.contentHash = identity.value();
      result.push_back(std::move(chunk));
    }
    return result;
  }
  if (!std::holds_alternative<synthesis::SampleSingerResource>(source.resource)) return core::failure<Output>(
      core::ErrorCode::Unsupported, "Chunk snapshot factory has no adapter for this resource family");
  const auto& sample = source.sample();
  if (!source.project || !source.sourceProjectId.valid() || !source.phonemes || !source.pronunciationIdentity || !source.compiledPerformance ||
      !sample.voicebank || !sample.unitPlan || sample.unitPlan->entries.empty() ||
      sample.selectedUnits.size() != sample.unitPlan->entries.size() ||
      sample.frozenAudio.size() != sample.unitPlan->entries.size() ||
      source.sampleRate != source.compiledPerformance->sampleRate()) return core::failure<Output>(
          core::ErrorCode::InvalidArgument, "Chunk source snapshot is incomplete");
  if (source.ownedFrames && (output.start < source.ownedFrames->start || output.end > source.ownedFrames->end)) {
    return core::failure<Output>(core::ErrorCode::Conflict, "Chunk subdivision cannot expand existing output ownership");
  }
  if (sample.frozenAudio.size() > 65536U / windows.value().size()) return core::failure<Output>(
      core::ErrorCode::Unsupported, "Chunk snapshots exceed aggregate sample metadata budget");
  Output result;
  result.reserve(windows.value().size());
  for (const auto window : windows.value()) {
    const auto identity = buildIdentity(*source.project, *sample.voicebank, *sample.unitPlan,
        sample.selectedUnits, source.quality, source.sampleRate, source.style, sample.renderOptions, window);
    if (!identity) return core::Result<Output>{identity.error()};
    auto chunk = source;
    chunk.ownedFrames = window;
    chunk.contentHash = core::sha256Hex(identity.value() + source.pronunciationIdentity->resourceHash +
        phonemizer::pronunciationSequenceHash(source.phonemes->tokens));
    result.push_back(std::move(chunk));
  }
  return result;
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
    const synthesis::PhraseRenderOptions& renderOptions,
    std::optional<synthesis::PhraseFrameRange> ownedFrames) const {
  const auto projectValidation = project.validate();
  if (!projectValidation) return core::Result<RenderSnapshot>{projectValidation.error()};
  if (renderOptions.renderer.raw.performance || renderOptions.renderer.raw.performanceVowelFrame || renderOptions.renderer.psola.performance || renderOptions.renderer.psola.sourceMap ||
      renderOptions.renderer.spectral.performance || renderOptions.renderer.spectral.sourceMap ||
      renderOptions.renderer.stretch.performance || renderOptions.renderer.stretch.sourceMap) {
    return core::failure<RenderSnapshot>(core::ErrorCode::Unsupported,
        "Externally compiled performance cannot bypass snapshot-owned performance identity");
  }
  const auto bankValidation = voicebankValue.validate();
  if (!bankValidation) return core::Result<RenderSnapshot>{bankValidation.error()};
  const auto* track = project.findVocalTrack(trackId);
  if (track == nullptr) {
    return core::failure<RenderSnapshot>(core::ErrorCode::NotFound,
                                         "Render snapshot track was not found",
                                         trackId.toString());
  }
  const auto* region = track->findRegion(segment.regionId);
  if (track->proceduralRecipe) return core::failure<RenderSnapshot>(core::ErrorCode::Conflict,
      "A saved procedural singer cannot render through a sample-bank snapshot");
  if (track->neuralResource) return core::failure<RenderSnapshot>(core::ErrorCode::Conflict,
      "A saved neural singer cannot render through a sample-bank snapshot");
  if (region == nullptr) {
    return core::failure<RenderSnapshot>(core::ErrorCode::NotFound,
                                         "Render snapshot region was not found",
                                         segment.regionId.toString());
  }
  if (requiresFormantShift(*region))
    return core::failure<RenderSnapshot>(core::ErrorCode::Unsupported,
        formantShiftUnsupportedMessage("sample bank"), trackId.toString());
  if (requiresBreathiness(*region))
    return core::failure<RenderSnapshot>(core::ErrorCode::Unsupported,
        breathinessUnsupportedMessage("sample bank"), trackId.toString());
  if (requiresTension(*region))
    return core::failure<RenderSnapshot>(core::ErrorCode::Unsupported,
        tensionUnsupportedMessage("sample bank"), trackId.toString());
  if (requiresAiriness(*region))
    return core::failure<RenderSnapshot>(core::ErrorCode::Unsupported,
        airinessUnsupportedMessage("sample bank"), trackId.toString());
  if (requiresGender(*region))
    return core::failure<RenderSnapshot>(core::ErrorCode::Unsupported,
        genderUnsupportedMessage("sample bank"), trackId.toString());
  if (requiresGrowl(*region))
    return core::failure<RenderSnapshot>(core::ErrorCode::Unsupported,
        growlUnsupportedMessage("sample bank"), trackId.toString());
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
  if (ownedFrames) {
    const auto valid = synthesis::PhraseOutputContract{sampleRate, *ownedFrames, *ownedFrames}.validate();
    if (!valid) return core::Result<RenderSnapshot>{valid.error()};
  }
  if (style.empty()) {
    const auto validStyle = track->styleSelection.validate();
    if (!validStyle) return core::Result<RenderSnapshot>{validStyle.error()};
    if (!track->styleSelection.styleId.empty()) {
      style = track->styleSelection.styleId;
    } else if (track->styleSelection.origin == domain::VoiceStyleOrigin::LegacyNeedsExactBankResolution) {
      return core::failure<RenderSnapshot>(core::ErrorCode::Conflict,
          "Legacy style requires exact-bank resolution before rendering");
    } else if (voicebankValue.styles.size() == 1U) {
      style = voicebankValue.styles.front();
    } else {
      return core::failure<RenderSnapshot>(core::ErrorCode::Conflict,
          "Voicebank has multiple styles; select a style before rendering");
    }
  }
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
  const auto pronunciation = phonemizer::resolvePronunciationForLanguage(*region, voicebankValue.language);
  if (!pronunciation) return core::Result<RenderSnapshot>{pronunciation.error()};
  auto phonemes = pronunciation.value().pronunciation;
  const std::unordered_set<domain::NoteId> phraseNotes(segment.noteIds.begin(), segment.noteIds.end());
  const auto completeRelationship = [&](domain::PhonemeKey key, std::size_t count, bool seam) {
    const auto start = std::find_if(phonemes.tokens.begin(), phonemes.tokens.end(), [&](const auto& token) { return token.key == key; });
    if (start == phonemes.tokens.end()) return false;
    if (seam) return start == phonemes.tokens.begin() || phraseNotes.contains(std::prev(start)->key.noteId);
    if (count == 0U || count > static_cast<std::size_t>(phonemes.tokens.end() - start)) return false;
    return std::all_of(start, start + static_cast<std::ptrdiff_t>(count),
        [&](const auto& token) { return phraseNotes.contains(token.key.noteId); });
  };
  for (const auto& edit : phraseRegion->unitSelectionOverrides) {
    if (!edit.unresolved && !completeRelationship(edit.startKey, edit.tokenCount, false)) {
      return core::failure<RenderSnapshot>(core::ErrorCode::Conflict,
          "Phrase boundary splits an active unit span", edit.startKey.toString());
    }
  }
  for (const auto& edit : phraseRegion->seamOverrides) {
    if (!edit.unresolved && !completeRelationship(edit.incomingStartKey, 1U, true)) {
      return core::failure<RenderSnapshot>(core::ErrorCode::Conflict,
          "Phrase boundary splits an active seam relationship", edit.incomingStartKey.toString());
    }
  }
  std::vector<const domain::Note*> orderedNotes;
  for (const auto& note : region->notes) orderedNotes.push_back(&note);
  std::sort(orderedNotes.begin(), orderedNotes.end(), [](const auto* a, const auto* b) {
    return a->startTick == b->startTick ? a->id < b->id : a->startTick < b->startTick;
  });
  for (std::size_t i = 1U; i < orderedNotes.size(); ++i) {
    if (domain::continuesSharedLyric(*orderedNotes[i - 1U], *orderedNotes[i]) &&
        phraseNotes.contains(orderedNotes[i - 1U]->id) != phraseNotes.contains(orderedNotes[i]->id)) {
      return core::failure<RenderSnapshot>(core::ErrorCode::Conflict,
          "Phrase boundary splits shared-lyric vowel continuation", orderedNotes[i]->id.toString());
    }
  }
  std::erase_if(phonemes.tokens, [&](const auto& token) { return !phraseNotes.contains(token.key.noteId); });
  std::erase_if(phonemes.warnings, [&](const auto& warning) { return !phraseNotes.contains(warning.noteId); });
  if (phonemes.tokens.empty()) {
    return core::failure<RenderSnapshot>(core::ErrorCode::NotFound,
                                         "Phonemizer produced no renderable tokens");
  }
  struct FrozenAsset final {
    std::string sha256;
    std::shared_ptr<const voicebank::AudioBuffer> audio;
  };
  std::map<std::filesystem::path, FrozenAsset> frozenByPath;
  struct FrozenAlignment {
    std::optional<synthesis::SourcePhonemeAlignment> value;
    std::string sha256;
  };
  std::map<std::string, FrozenAlignment> alignments;
  std::uint64_t alignmentBytes = 0U;
  std::uint64_t frozenEncodedBytes = 0U;
  std::uint64_t frozenDecodedBytes = 0U;
  std::vector<SelectedUnitIdentity> selectedUnits;
  std::vector<synthesis::FrozenUnitAudio> frozenAudio;
  std::map<std::string, synthesis::FrozenUnitAudio> frozenByUnit;
  const auto freezeUnit = [&](const voicebank::Unit& value) -> core::Result<void> {
    const auto* unit = &value;
    if (frozenByUnit.contains(unit->id)) return {};
    auto resolved = voicebank::resolveBankAsset(bankRoot, unit->audioPath);
    if (!resolved) return core::Result<void>{resolved.error()};
    auto asset = frozenByPath.find(resolved.value());
    if (asset == frozenByPath.end()) {
      const auto remainingEncoded =
          kMaximumFrozenPhraseEncodedBytes - frozenEncodedBytes;
      auto bytes = core::readFileBytesLimited(
          resolved.value(),
          std::min<std::uint64_t>(voicebank::kMaximumSupportedWavBytes,
                                  remainingEncoded));
      if (!bytes) return core::Result<void>{bytes.error()};
      auto decoded = voicebank::readWav(bytes.value(), resolved.value().string());
      if (!decoded) return core::Result<void>{decoded.error()};
      const auto decodedBytes =
          static_cast<std::uint64_t>(decoded.value().interleaved.size()) *
          static_cast<std::uint64_t>(sizeof(float));
      if (decodedBytes > kMaximumFrozenPhraseDecodedBytes - frozenDecodedBytes) {
        return core::failure<void>(
            core::ErrorCode::Unsupported,
            "Selected Phrase audio exceeds the frozen decode budget",
            resolved.value().string());
      }
      frozenEncodedBytes += static_cast<std::uint64_t>(bytes.value().size());
      frozenDecodedBytes += decodedBytes;
      asset = frozenByPath.emplace(
          resolved.value(),
          FrozenAsset{
              .sha256 = core::sha256Hex(bytes.value()),
              .audio = std::make_shared<const voicebank::AudioBuffer>(
                  std::move(decoded).value()),
          }).first;
    }
    auto alignment = alignments.find(unit->id);
    if (alignment == alignments.end()) {
      FrozenAlignment frozen;
      const auto relative = std::filesystem::path{"alignments"} / (core::sha256Hex(unit->id) + ".json");
      bool present = true;
      auto path = bankRoot;
      for (const auto& component : relative) {
        path /= component;
        std::error_code error;
        const auto status = std::filesystem::symlink_status(path, error);
        if (error == std::errc::no_such_file_or_directory || (!error && !std::filesystem::exists(status))) { present = false; break; }
        if (error) return core::failure<void>(core::ErrorCode::IoError, "Cannot inspect source alignment", path.string());
        if (std::filesystem::is_symlink(status)) return core::failure<void>(core::ErrorCode::Conflict, "Source alignment paths may not contain symbolic links", path.string());
      }
      if (present) {
        const auto resolvedAlignment = voicebank::resolveBankAsset(bankRoot, relative);
        if (!resolvedAlignment) return core::Result<void>{resolvedAlignment.error()};
        constexpr std::uint64_t maximumAlignmentBytes = 4U * 1024U * 1024U;
        const auto json = core::readTextFileLimited(resolvedAlignment.value(),
            std::min<std::uint64_t>(512U * 1024U, maximumAlignmentBytes - alignmentBytes));
        if (!json) return core::Result<void>{json.error()};
        const auto decodedAlignment = synthesis::decodeSourcePhonemeAlignment(json.value(), *unit,
            asset->second.sha256, static_cast<time::SampleFrame>(asset->second.audio->frameCount()));
        if (!decodedAlignment) return core::Result<void>{decodedAlignment.error()};
        alignmentBytes += static_cast<std::uint64_t>(json.value().size());
        frozen.value = decodedAlignment.value();
        frozen.sha256 = core::sha256Hex(json.value());
      }
      alignment = alignments.emplace(unit->id, std::move(frozen)).first;
    }
    frozenByUnit.emplace(unit->id, synthesis::FrozenUnitAudio{
        .unitId = unit->id,
        .audio = asset->second.audio,
        .sourceAlignment = alignment->second.value,
        .verifiedAudioSha256 = asset->second.sha256,
    });
    return {};
  };

  // Probe only phone-matching units whose edited span needs extra landmarks.
  // Reuse the same bounded frozen assets during final selection and rendering.
  std::vector<synthesis::SourceAlignmentEvidence> evidence;
  for (const auto& unit : voicebankValue.units) {
    if (!unit.enabled || unit.style != style || unit.phones.empty() || unit.phones.size() > phonemes.tokens.size()) continue;
    bool needsAlignment = false;
    for (std::size_t start = 0; start <= phonemes.tokens.size() - unit.phones.size(); ++start) {
      const auto covered = std::span<const domain::PhonemeToken>{phonemes.tokens}.subspan(start, unit.phones.size());
      if (synthesis::supportsExplicitPhonemeTiming(covered) && !synthesis::hasMultipleNuclei(covered)) continue;
      if (std::equal(unit.phones.begin(), unit.phones.end(), covered.begin(),
          [](const auto& phone, const auto& token) { return phone == token.symbol; })) {
        needsAlignment = true;
        break;
      }
    }
    if (!needsAlignment) continue;
    const auto sidecar = bankRoot / "alignments" / (core::sha256Hex(unit.id) + ".json");
    std::error_code probeError;
    const auto sidecarStatus = std::filesystem::symlink_status(sidecar, probeError);
    if (probeError == std::errc::no_such_file_or_directory ||
        (!probeError && !std::filesystem::exists(sidecarStatus))) continue;
    if (probeError) return core::failure<RenderSnapshot>(core::ErrorCode::IoError,
        "Cannot inspect candidate source alignment", sidecar.string());
    const auto frozen = freezeUnit(unit);
    if (!frozen) return core::Result<RenderSnapshot>{frozen.error()};
    const auto& resource = frozenByUnit.at(unit.id);
    if (resource.sourceAlignment) evidence.push_back({&*resource.sourceAlignment,
        resource.verifiedAudioSha256, static_cast<time::SampleFrame>(resource.audio->frameCount())});
  }
  synthesis::DeterministicUnitSelector selector;
  auto plan = selector.select(voicebankValue, *phraseRegion, phonemes.tokens,
                              style, phraseRegion->unitSelectionOverrides, evidence, true);
  if (!plan) return core::Result<RenderSnapshot>{plan.error()};
  selectedUnits.reserve(plan.value().entries.size());
  frozenAudio.reserve(plan.value().entries.size());
  for (const auto& entry : plan.value().entries) {
    const auto* unit = voicebankValue.findUnit(entry.unitId);
    if (!unit) return core::failure<RenderSnapshot>(core::ErrorCode::NotFound, "Selected unit is absent from voicebank", entry.unitId);
    const auto frozen = freezeUnit(*unit);
    if (!frozen) return core::Result<RenderSnapshot>{frozen.error()};
    const auto& resource = frozenByUnit.at(entry.unitId);
    selectedUnits.push_back({entry.unitId, resource.verifiedAudioSha256, alignments.at(entry.unitId).sha256});
    frozenAudio.push_back(resource);
  }

  std::shared_ptr<const synthesis::CompiledScorePerformance> compiledPerformance;
  const bool needsPerformance = std::any_of(plan.value().entries.begin(), plan.value().entries.end(),
      [&](const auto& entry) {
        const auto renderer = synthesis::resolveRequestedRenderer(*voicebankValue.findUnit(entry.unitId),
            renderOptions.renderer.policy, entry.renderer);
        return renderer == voicebank::RendererHint::Raw || renderer == voicebank::RendererHint::ClassicPsola || renderer == voicebank::RendererHint::SpectralClassic ||
            renderer == voicebank::RendererHint::Stretch;
      });
  if (needsPerformance) {
    const auto compiled = synthesis::compileScorePerformance(phraseProject.value(), *phraseRegion, sampleRate, phonemes.tokens);
    if (!compiled) return core::Result<RenderSnapshot>{compiled.error()};
    compiledPerformance = std::make_shared<const synthesis::CompiledScorePerformance>(compiled.value());
  }
  auto identity = buildIdentity(phraseProject.value(), voicebankValue,
                                plan.value(), selectedUnits, quality,
                                sampleRate, style, renderOptions, ownedFrames);
  if (!identity) return core::Result<RenderSnapshot>{identity.error()};

  return RenderSnapshot{
      .revision = revision,
      .quality = quality,
      .renderAbiId = std::string{build::kRenderAbiId},
      .contentHash = core::sha256Hex(identity.value() + pronunciation.value().identity.resourceHash +
                                     phonemizer::pronunciationSequenceHash(phonemes.tokens)),
      .segment = segment,
      .trackId = trackId,
      .sourceProjectId = project.id(),
      .project = std::make_shared<const domain::Project>(
          std::move(phraseProject).value()),
      .phonemes = std::make_shared<const phonemizer::Result>(std::move(phonemes)),
      .resource = synthesis::SampleSingerResource{
          .voicebank = std::make_shared<const voicebank::Manifest>(voicebankValue),
          .unitPlan = std::make_shared<const synthesis::UnitPlan>(std::move(plan).value()),
          .selectedUnits = std::move(selectedUnits),
          .frozenAudio = std::move(frozenAudio),
          .bankRoot = std::move(bankRoot),
          .renderOptions = renderOptions,
      },
      .sampleRate = sampleRate,
      .style = std::move(style),
      .pronunciationIdentity = pronunciation.value().identity,
      .compiledPerformance = std::move(compiledPerformance),
      .ownedFrames = ownedFrames,
  };
}

}  // namespace seam::rendering
