#include "seam/voicebank_production/manifest_draft.hpp"
#include "seam/voicebank_production/repository.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "candidate_publication_internal.hpp"
#include "seam/core/exclusive_file_lock.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/pitch_marks.hpp"
#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <set>

namespace seam::voicebank_production {
namespace {
using namespace candidate_publication_internal;
using J = formats::JsonValue;
constexpr std::string_view kEstimator = "seam.sample-manifest-estimates.v1";
std::atomic<std::uint64_t> draftSequence{0U};
bool text(std::string_view value, std::size_t maximum) {
  return !value.empty() && value.size() <= maximum && std::none_of(value.begin(), value.end(),
      [](unsigned char c) { return c < 32U || c == 127U; });
}
bool identifier(std::string_view value) {
  return text(value, 128U) && std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
  });
}
struct Cleanup final {
  std::filesystem::path path;
  DirectoryIdentity parentIdentity, stageIdentity;
  ~Cleanup() { cleanupOwnedDirectory(path, parentIdentity, stageIdentity); }
};
core::Result<voicebank::Unit> unitFromAssignment(const UnitAssignment& assignment,
    const VoicebankProductionProject& project, const SampleManifestDraftIdentity& identity) {
  const auto separator = assignment.coverageKey.find(':');
  if (separator == std::string::npos || assignment.coverageKey.size() > 4096U)
    return core::failure<voicebank::Unit>(core::ErrorCode::InvalidArgument, "Assignment must have a canonical kind:phone coverage key", assignment.coverageKey);
  const auto name = assignment.coverageKey.substr(0U, separator);
  const auto kind = name == "glottal-attack" ? voicebank::UnitKind::Glottal : voicebank::parseUnitKind(name);
  if ((kind != voicebank::UnitKind::Glottal && voicebank::unitKindName(kind) != name) || name == "glottal")
    return core::failure<voicebank::Unit>(core::ErrorCode::Unsupported, "Assignment kind is not a supported canonical coverage key", name);
  voicebank::Unit unit;
  const auto& style = project.schemaVersion >= kProductionStyleSchemaVersion ? assignment.style : identity.style;
  // Includes the explicit language/style and inventory, not row position or
  // shared audio digest. This does not migrate the producer's U10 identity.
  const auto key = formats::stringifyJson(J{J::Array{project.inventorySha256, static_cast<std::int64_t>(identity.language),
      style, assignment.coverageKey, static_cast<std::int64_t>(assignment.pitchLayer)}}, false);
  unit.id = "unit-" + core::sha256Hex(key);
  unit.kind = kind; unit.rootMidi = assignment.pitchLayer; unit.style = style;
  std::size_t start = separator + 1U;
  while (start <= assignment.coverageKey.size()) {
    const auto end = assignment.coverageKey.find(':', start);
    const auto phone = assignment.coverageKey.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (!text(phone, 128U) || unit.phones.size() >= 64U || phone.find(' ') != std::string::npos)
      return core::failure<voicebank::Unit>(core::ErrorCode::InvalidArgument, "Assignment phone sequence is empty, ambiguous or oversized", assignment.coverageKey);
    if (!unit.alias.empty()) unit.alias += ' ';
    unit.alias += phone; unit.phones.push_back(phone);
    if (end == std::string::npos) break;
    start = end + 1U;
  }
  if (coverageKey(unit) != assignment.coverageKey)
    return core::failure<voicebank::Unit>(core::ErrorCode::Conflict, "Assignment phone mapping is not canonical");
  return unit;
}
struct AudioFacts final {
  std::uint32_t sampleRate{};
  time::SampleFrame frames{};
  std::vector<voicebank::PitchMark> pitch;
  std::string diagnostic;
};
core::Result<AudioFacts> inspectAudio(std::span<const std::byte> bytes, std::uint64_t& remainingWork,
    const SampleManifestDraftOptions& options, std::stop_token stop) {
  const auto decoded = voicebank::readWav(bytes, "immutable producer take",
      {.maximumFrames = options.maximumFramesPerAsset, .maximumChannels = 1U,
       .maximumDecodedSamples = options.maximumFramesPerAsset}, stop);
  if (!decoded) return core::Result<AudioFacts>{decoded.error()};
  const auto& audio = decoded.value();
  if (audio.channels != 1U || audio.frameCount() == 0U || audio.frameCount() > options.maximumFramesPerAsset ||
      !std::all_of(audio.interleaved.begin(), audio.interleaved.end(), [](float sample) { return std::isfinite(sample); }))
    return core::failure<AudioFacts>(core::ErrorCode::InvalidArgument, "Editable draft audio must be finite nonempty mono within its frame budget; use an explicit production edit for conversion");
  AudioFacts facts{audio.sampleRate, static_cast<time::SampleFrame>(audio.frameCount()), {}, {}};
  constexpr std::size_t frameSize = 2048U, hop = 256U;
  constexpr std::uint64_t workPerFrame = 4096U * 12U; // Both transforms of a 2*frameSize FFT.
  const auto analysisFrames = audio.frameCount() <= frameSize ? 1U : 1U + (audio.frameCount() - frameSize) / hop;
  if (analysisFrames > remainingWork / workPerFrame)
    return core::failure<AudioFacts>(core::ErrorCode::Unsupported, "Draft pitch estimation exceeds the aggregate work budget; split the production batch");
  remainingWork -= static_cast<std::uint64_t>(analysisFrames) * workPerFrame;
  const voicebank::PitchMarkGenerationConfig config{.pitch = {.frameSize = frameSize, .hopSize = hop,
      .minimumHz = 60.0, .maximumHz = 1200.0, .voicingThreshold = 0.32, .correlationMethod = voicebank::PitchCorrelationMethod::Fft}};
  const auto marks = voicebank::generatePitchMarks(audio.interleaved, audio.sampleRate, 0, facts.frames, config, stop,
      {.maximumFrames = 4096U, .maximumCorrelationTerms = 0U, .maximumTransformButterflies = analysisFrames * workPerFrame});
  if (marks) facts.pitch = marks.value();
  else if (marks.error().code == core::ErrorCode::NotFound) facts.diagnostic = "No reliable pitch-mark estimate. Raw is provisional; inspect voicing and select appropriate processing before review.";
  else return core::Result<AudioFacts>{marks.error()};
  return facts;
}
void estimateMarkers(voicebank::Unit& unit, const AudioFacts& facts) {
  unit.markers = {.audioOffset = 0, .consonantEnd = 0, .vowelOnset = 0, .stableStart = 0,
      .loopStart = {}, .loopEnd = {}, .releaseStart = {}, .audioEnd = facts.frames};
  unit.renderer = voicebank::RendererHint::Raw;
  // Breath/noise/closure-like units must never acquire a fabricated voiced
  // sustain merely because correlation detected a periodic noise component.
  const bool sustained = unit.kind == voicebank::UnitKind::Sustain || unit.kind == voicebank::UnitKind::Cv ||
      unit.kind == voicebank::UnitKind::Vcv || unit.kind == voicebank::UnitKind::Vc || unit.kind == voicebank::UnitKind::Vv;
  if (!sustained || facts.pitch.size() < 6U) return;
  unit.pitchMarks = facts.pitch;
  const auto stable = facts.pitch.front().frame;
  if (unit.kind == voicebank::UnitKind::Cv || unit.kind == voicebank::UnitKind::Vcv)
    unit.markers.consonantEnd = unit.markers.vowelOnset = stable;
  unit.markers.stableStart = stable;
  unit.markers.loopStart = facts.pitch[facts.pitch.size() / 4U].frame;
  unit.markers.loopEnd = facts.pitch[(facts.pitch.size() * 3U) / 4U].frame;
  unit.markers.releaseStart = facts.pitch.back().frame;
  unit.renderer = voicebank::RendererHint::ClassicPsola;
}
} // namespace

core::Result<CreatedSampleManifestDraft> createSampleManifestDraft(
    const std::filesystem::path& repositoryRoot, const VoicebankProductionProject& project,
    const SampleManifestDraftIdentity& identity, const std::filesystem::path& destination,
    const SampleManifestDraftOptions& options, std::stop_token stop) {
  using Output = CreatedSampleManifestDraft;
  if (project.schemaVersion >= kProductionStyleSchemaVersion &&
      (project.language != (identity.language == domain::Language::Japanese ? "ja" : identity.language == domain::Language::English ? "en" : "ko") ||
       std::none_of(project.unitAssignments.begin(), project.unitAssignments.end(), [&](const auto& row) { return row.style == identity.style; })))
    return core::failure<Output>(core::ErrorCode::Conflict, "Draft language and selected style must belong to the producer workspace");
  if (!identifier(identity.id) || !identifier(identity.version) || !text(identity.displayName, 256U) || !text(identity.style, 128U) ||
      (identity.language != domain::Language::Japanese && identity.language != domain::Language::English && identity.language != domain::Language::Korean) ||
      project.unitAssignments.empty() || project.unitAssignments.size() > 4096U || project.takes.size() > 65536U ||
      options.maximumAudioBytes == 0U || options.maximumAudioBytes > 256ULL * 1024ULL * 1024ULL ||
      options.maximumFramesPerAsset == 0U || options.maximumFramesPerAsset > 1024ULL * 1024ULL ||
      options.maximumPitchTransformButterflies == 0U || options.maximumPitchTransformButterflies > 1024ULL * 1024ULL * 1024ULL ||
      !destination.is_absolute() || destination != destination.lexically_normal() || destination.filename().empty() ||
      destination.filename().string().front() == '.' || destination == destination.root_path())
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Draft requires explicit bank/language/style identity, bounded assignments and a new output directory");
  auto checked = cancelled(stop); if (!checked) return core::Result<Output>{checked.error()};
  const auto workspace = realDirectory(repositoryRoot), parent = realDirectory(destination.parent_path());
  if (!workspace) return core::Result<Output>{workspace.error()};
  if (!parent) return core::Result<Output>{parent.error()};
  const auto finalPath = parent.value() / destination.filename();
  const auto relative = finalPath.lexically_relative(workspace.value());
#if defined(_WIN32)
  const bool sameRoot = CompareStringOrdinal(finalPath.root_name().c_str(), -1,
      workspace.value().root_name().c_str(), -1, TRUE) == CSTR_EQUAL;
#else
  const bool sameRoot = finalPath.root_name() == workspace.value().root_name();
#endif
  if (finalPath == workspace.value() || (relative.empty() ? sameRoot : *relative.begin() != ".."))
    return core::failure<Output>(core::ErrorCode::Conflict, "Editable drafts must be created outside the producer workspace");
  core::ExclusiveFileLock workspaceLock, outputLock;
  checked = workspaceLock.acquire(workspace.value() / ".writer.lock"); if (!checked) return core::Result<Output>{checked.error()};
  ProductionProjectRepository repository{workspace.value()};
  checked = repository.verify(project); if (!checked) return core::Result<Output>{checked.error()};
  checked = outputLock.acquire(parent.value() / ".seam-manifest-draft-writer.lock"); if (!checked) return core::Result<Output>{checked.error()};
  const auto parentIdentity = captureDirectoryIdentity(parent.value());
  if (!parentIdentity) return core::Result<Output>{parentIdentity.error()};
  checked = absentDestination(finalPath); if (!checked) return core::Result<Output>{checked.error()};
  Output result{.root = finalPath, .manifestSha256 = {}, .draftSha256 = {},
      .sourceProjectSha256 = core::sha256Hex(encodeProductionProject(project)), .sourceGeneration = project.lastDurableGeneration,
      .missingAssignments = {}, .diagnostics = {"ALL MARKERS AND PITCH ARE UNREVIEWED ESTIMATES. No source or musical qualification is granted."}};
  voicebank::Manifest manifest{.id = identity.id, .version = identity.version, .displayName = identity.displayName,
      .characterId = {}, .characterVersion = {}, .language = identity.language, .expectedSampleRate = 48000U,
      .styles = {identity.style}, .units = {}};
  if (project.schemaVersion >= kProductionStyleSchemaVersion) {
    std::set<std::string> styles;
    for (const auto& assignment : project.unitAssignments) styles.insert(assignment.style);
    manifest.styles.assign(styles.begin(), styles.end());
    result.diagnostics.push_back("All assignment-owned styles are retained; the selected style does not relabel or filter units.");
  }
  std::map<std::string, const AssetRecord*> assets;
  J::Array bindings;
  std::set<std::string> selectedTakes, unitIds;
  for (const auto& assignment : project.unitAssignments) {
    checked = cancelled(stop); if (!checked) return core::Result<Output>{checked.error()};
    if (assignment.takeId.empty()) {
      result.missingAssignments.push_back((project.schemaVersion >= kProductionStyleSchemaVersion ? assignment.style + ":" : "") +
          assignment.coverageKey + "@" + std::to_string(assignment.pitchLayer));
      continue;
    }
    auto unit = unitFromAssignment(assignment, project, identity); if (!unit) return core::Result<Output>{unit.error()};
    if (!selectedTakes.insert(assignment.takeId).second || !unitIds.insert(unit.value().id).second)
      return core::failure<Output>(core::ErrorCode::Conflict, "Draft assignments are ambiguous or repeat an active take");
    checked = requireTakeSourceExecution(project, assignment.takeId); if (!checked) return core::Result<Output>{checked.error()};
    const auto take = std::find_if(project.takes.begin(), project.takes.end(), [&](const auto& value) { return value.takeId == assignment.takeId; });
    if (take == project.takes.end()) return core::failure<Output>(core::ErrorCode::NotFound, "Draft active take is missing");
    auto digest = take->rawAssetSha256;
    for (const auto& revisionId : take->derivedRevisionIds) {
      const auto revision = std::find_if(project.derivedRevisions.begin(), project.derivedRevisions.end(), [&](const auto& value) { return value.revisionId == revisionId; });
      if (revision == project.derivedRevisions.end() || revision->inputSha256 != digest)
        return core::failure<Output>(core::ErrorCode::Conflict, "Draft take processing chain is incomplete");
      digest = revision->outputSha256;
    }
    const auto asset = std::find_if(project.assets.begin(), project.assets.end(), [&](const auto& value) { return value.sha256 == digest; });
    if (asset == project.assets.end()) return core::failure<Output>(core::ErrorCode::NotFound, "Draft effective audio is missing");
    assets.emplace(digest, &*asset);
    unit.value().audioPath = std::filesystem::path{"audio"} / (digest + ".wav");
    bindings.emplace_back(J::Object{{"unitId", unit.value().id}, {"takeId", take->takeId}, {"audioSha256", digest},
        {"parentRevisionId", take->derivedRevisionIds.empty() ? "" : take->derivedRevisionIds.back()},
        {"sourceBindingId", take->sourceBindingId}, {"producerState", toString(assignment.state)}, {"annotationStatus", "UNREVIEWED_ESTIMATE"}});
    manifest.units.push_back(std::move(unit.value()));
  }
  if (manifest.units.empty()) return core::failure<Output>(core::ErrorCode::InvalidState, "Collect at least one take before creating an editable manifest");
  // A deterministic destination-specific prefix plus process-local sequence;
  // create_directory is exclusive. Occupied private staging is never reused.
  const auto stage = parent.value() / (".seam-draft-" + core::sha256Hex(finalPath.generic_string()).substr(0U, 16U) + "-" + std::to_string(draftSequence.fetch_add(1U)));
  std::error_code error;
  if (!std::filesystem::create_directory(stage, error) || error)
    return core::failure<Output>(core::ErrorCode::Conflict, "Cannot create private editable-draft staging", stage.string());
  const auto stageIdentity = captureDirectoryIdentity(stage);
  if (!stageIdentity) return core::Result<Output>{stageIdentity.error()};
  Cleanup cleanup{stage, parentIdentity.value(), stageIdentity.value()};
  std::filesystem::permissions(stage, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, error);
  if (error) return core::failure<Output>(core::ErrorCode::IoError, "Cannot restrict draft staging", error.message());
  std::map<std::string, AudioFacts> facts;
  std::uint64_t bytesUsed = 0U, remainingWork = options.maximumPitchTransformButterflies;
  for (const auto& [digest, asset] : assets) {
    checked = cancelled(stop); if (!checked) return core::Result<Output>{checked.error()};
    if (asset->byteSize > options.maximumAudioBytes - bytesUsed)
      return core::failure<Output>(core::ErrorCode::Unsupported, "Draft audio exceeds its aggregate byte budget");
    const auto bytes = core::readFileBytesLimited(repository.assetPath(*asset), options.maximumAudioBytes - bytesUsed);
    if (!bytes) return core::Result<Output>{bytes.error()};
    if (bytes.value().size() != asset->byteSize || core::sha256Hex(bytes.value()) != digest)
      return core::failure<Output>(core::ErrorCode::Conflict, "Draft input bytes no longer match the current take");
    bytesUsed += bytes.value().size();
    auto inspected = inspectAudio(bytes.value(), remainingWork, options, stop); if (!inspected) return core::Result<Output>{inspected.error()};
    if (facts.empty()) manifest.expectedSampleRate = inspected.value().sampleRate;
    else if (manifest.expectedSampleRate != inspected.value().sampleRate)
      return core::failure<Output>(core::ErrorCode::Conflict, "Draft takes have mixed sample rates; resample explicitly in production first");
    checked = core::durableAtomicWriteNew(stage / "audio" / (digest + ".wav"), bytes.value());
    if (!checked) return core::Result<Output>{checked.error()};
    facts.emplace(digest, std::move(inspected.value()));
  }
  for (auto& unit : manifest.units) {
    const auto digest = unit.audioPath.stem().string();
    estimateMarkers(unit, facts.at(digest));
    checked = unit.markers.validate(facts.at(digest).frames); if (!checked) return core::Result<Output>{checked.error()};
    if (!facts.at(digest).diagnostic.empty()) result.diagnostics.push_back(unit.id + ": " + facts.at(digest).diagnostic);
  }
  if (options.faultInjector) { checked = options.faultInjector(ManifestDraftStage::AudioStaged); if (!checked) return core::Result<Output>{checked.error()}; }
  const auto encoded = boundedManifest(manifest); if (!encoded) return core::Result<Output>{encoded.error()};
  result.manifestSha256 = core::sha256Hex(encoded.value());
  J::Array missing, diagnostics;
  for (const auto& item : result.missingAssignments) missing.emplace_back(item);
  for (const auto& item : result.diagnostics) diagnostics.emplace_back(item);
  const auto descriptor = formats::stringifyJson(J{J::Object{{"format", "com.project-seam.editable-sample-draft"},
      {"schemaVersion", std::int64_t{1}}, {"estimator", std::string{kEstimator}}, {"status", "UNREVIEWED_DRAFT"},
      {"approval", "unchanged"}, {"releaseEligible", false}, {"projectId", project.projectId},
      {"sourceGeneration", static_cast<std::int64_t>(project.lastDurableGeneration)}, {"sourceProjectSha256", result.sourceProjectSha256},
      {"manifestSha256", result.manifestSha256}, {"missingAssignments", std::move(missing)}, {"diagnostics", std::move(diagnostics)},
      {"unitBindings", std::move(bindings)}}}, true);
  result.draftSha256 = core::sha256Hex(descriptor);
  checked = core::durableAtomicWriteTextNew(stage / "manifest.json", encoded.value()); if (!checked) return core::Result<Output>{checked.error()};
  checked = core::durableAtomicWriteTextNew(stage / "draft.json", descriptor); if (!checked) return core::Result<Output>{checked.error()};
  for (const auto& directory : {stage / "audio", stage}) {
    checked = syncDirectory(directory); if (!checked) return core::Result<Output>{checked.error()};
  }
  if (options.faultInjector) { checked = options.faultInjector(ManifestDraftStage::BeforeCommit); if (!checked) return core::Result<Output>{checked.error()}; }
  checked = cancelled(stop); if (!checked) return core::Result<Output>{checked.error()};
  checked = validateDirectoryIdentity(parent.value(), parentIdentity.value()); if (!checked) return core::Result<Output>{checked.error()};
  checked = validateDirectoryIdentity(stage, stageIdentity.value()); if (!checked) return core::Result<Output>{checked.error()};
  checked = repository.verify(project); if (!checked) return core::Result<Output>{checked.error()};
  const auto reopened = voicebank::ManifestJsonCodec{}.load(stage / "manifest.json");
  const auto manifestHash = core::sha256File(stage / "manifest.json");
  const auto draftHash = core::sha256File(stage / "draft.json");
  if (!reopened || reopened.value() != manifest || !manifestHash || manifestHash.value() != result.manifestSha256 ||
      !draftHash || draftHash.value() != result.draftSha256)
    return core::failure<Output>(core::ErrorCode::Conflict, "Draft manifest or descriptor changed before its commit");
  for (const auto& [digest, asset] : assets) {
    const auto hash = core::sha256File(stage / "audio" / (digest + ".wav"), asset->byteSize);
    if (!hash || hash.value() != digest) return core::failure<Output>(core::ErrorCode::Conflict, "Draft staged audio changed before its commit");
  }
  checked = cancelled(stop); if (!checked) return core::Result<Output>{checked.error()};
  checked = publishNewDirectory(stage, finalPath, parentIdentity.value(), stageIdentity.value()); if (!checked) return core::Result<Output>{checked.error()};
  checked = options.faultInjector ? options.faultInjector(ManifestDraftStage::AfterCommitBeforeParentSync) : core::success();
  if (checked) checked = syncDirectory(parent.value());
  if (!checked) { result.durabilityConfirmed = false; result.diagnostics.push_back(
      "Draft committed but parent durability is uncertain. Inspect the returned hashes before retrying; do not overwrite or repeat. " + checked.error().message); }
  return result;
}
} // namespace seam::voicebank_production
