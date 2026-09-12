#include "seam/voicebank_production/candidate_publication.hpp"
#include "candidate_publication_internal.hpp"

#include "seam/core/exclusive_file_lock.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/repository.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace seam::voicebank_production {
namespace {
using J = formats::JsonValue;
using namespace candidate_publication_internal;
constexpr std::size_t kMaximumReviewUnits = 4096U;
constexpr std::uint64_t kMaximumReviewAudioBytes = 256ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaximumPacketBytes = 64U * 1024U * 1024U;
constexpr std::string_view kPacketFormat = "com.project-seam.sample-candidate-review";

core::Result<J> packetPayload(const SampleCandidateReviewPacket& packet) {
  if (packet.projectId.empty() || packet.sourceGeneration == 0U ||
      packet.sourceGeneration > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
      !isDigest(packet.sourceProjectSha256) || !isDigest(packet.reviewBasisSha256) ||
      packet.units.empty() || packet.units.size() > kMaximumReviewUnits ||
      packet.units.size() != packet.manifest.units.size())
    return core::failure<J>(core::ErrorCode::InvalidArgument, "Sample review packet identity or unit coverage is invalid");
  const auto manifest = boundedManifest(packet.manifest);
  if (!manifest) return core::Result<J>{manifest.error()};
  const auto manifestValue = formats::parseJson(manifest.value());
  if (!manifestValue) return core::Result<J>{manifestValue.error()};
  J::Array units;
  std::set<std::string> unitIds, takeIds;
  for (const auto& unit : packet.units) {
    const auto* mapped = packet.manifest.findUnit(unit.unitId);
    if (!mapped || !mapped->enabled || unit.takeId.empty() || !isDigest(unit.audioSha256) ||
        mapped->audioPath.generic_string() != "audio/" + unit.audioSha256 + ".wav" ||
        unit.originOperatorId.empty() || !isDigest(unit.originJournalSha256) ||
        unit.originGeneration == 0U || unit.originGeneration > packet.sourceGeneration ||
        unit.sampleRate != packet.manifest.expectedSampleRate || unit.frameCount == 0U ||
        unit.frameCount > kMaximumReviewAudioBytes || !std::isfinite(unit.peak) || !std::isfinite(unit.rms) ||
        unit.peak < 0.0 || unit.rms < 0.0 || unit.rms > unit.peak ||
        !unitIds.insert(unit.unitId).second || !takeIds.insert(unit.takeId).second)
      return core::failure<J>(core::ErrorCode::InvalidArgument, "Sample review unit material is invalid or duplicated", unit.unitId);
    units.emplace_back(J::Object{{"unitId", unit.unitId}, {"takeId", unit.takeId}, {"audioSha256", unit.audioSha256},
        {"originOperatorId", unit.originOperatorId}, {"originJournalSha256", unit.originJournalSha256},
        {"originGeneration", static_cast<std::int64_t>(unit.originGeneration)},
        {"sampleRate", static_cast<std::int64_t>(unit.sampleRate)}, {"frameCount", static_cast<std::int64_t>(unit.frameCount)},
        {"peak", unit.peak}, {"rms", unit.rms}});
  }
  return J{J::Object{{"format", std::string{kPacketFormat}}, {"schemaVersion", std::int64_t{1}},
      {"projectId", packet.projectId}, {"sourceGeneration", static_cast<std::int64_t>(packet.sourceGeneration)},
      {"sourceProjectSha256", packet.sourceProjectSha256}, {"reviewBasisSha256", packet.reviewBasisSha256},
      {"manifest", manifestValue.value()}, {"units", std::move(units)}}};
}

bool exactFields(const J& object, std::initializer_list<std::string_view> fields) {
  return object.isObject() && object.asObject().size() == fields.size() &&
      std::all_of(fields.begin(), fields.end(), [&](auto field) { return object.find(field) != nullptr; });
}

std::vector<SampleCandidateUnitBinding> packetBindings(const SampleCandidateReviewPacket& packet) {
  std::vector<SampleCandidateUnitBinding> result;
  result.reserve(packet.units.size());
  for (const auto& unit : packet.units)
    result.push_back({unit.unitId, unit.takeId, unit.audioSha256, {}, {}});
  return result;
}

core::Result<SampleCandidateRequest> currentCandidate(const VoicebankProductionProject& project,
    const SampleCandidateReviewPacket& packet) {
  if (packet.units.size() != project.unitAssignments.size())
    return core::failure<SampleCandidateRequest>(core::ErrorCode::Conflict, "Candidate must cover every current producer assignment");
  SampleCandidateRequest candidate{project.lastDurableGeneration, core::sha256Hex(encodeProductionProject(project)), packet.manifest, {}};
  for (const auto& unit : packet.units) {
    const auto qualifiedSource = requireTakeSourceQualification(project, unit.takeId);
    if (!qualifiedSource) return core::Result<SampleCandidateRequest>{qualifiedSource.error()};
    const auto review = std::find_if(project.reviews.rbegin(), project.reviews.rend(),
        [&](const auto& value) { return value.takeId == unit.takeId; });
    const auto metadata = std::find_if(project.metadataRevisions.rbegin(), project.metadataRevisions.rend(),
        [&](const auto& value) { return value.takeId == unit.takeId && value.kind == kSampleCandidateReviewKind; });
    if (review == project.reviews.rend() || metadata == project.metadataRevisions.rend())
      return core::failure<SampleCandidateRequest>(core::ErrorCode::Conflict, "Candidate still has unreviewed units");
    SampleCandidateUnitBinding binding{unit.unitId, unit.takeId, unit.audioSha256, review->reviewId, metadata->revisionId};
    const auto accepted = validateReviewedBinding(project, packet.manifest, binding);
    if (!accepted) return core::Result<SampleCandidateRequest>{accepted.error()};
    const auto independent = validateIndependentReviewer(project, binding, review->reviewerId,
        {unit.originOperatorId, unit.originGeneration, unit.originJournalSha256});
    if (!independent) return core::Result<SampleCandidateRequest>{independent.error()};
    candidate.units.push_back(std::move(binding));
  }
  return candidate;
}
}  // namespace

core::Result<std::string> encodeSampleCandidateReviewPacket(const SampleCandidateReviewPacket& packet) {
  auto payload = packetPayload(packet);
  if (!payload) return core::Result<std::string>{payload.error()};
  const auto digest = core::sha256Hex(formats::stringifyJson(payload.value(), false));
  if (!isDigest(packet.packetSha256) || packet.packetSha256 != digest)
    return core::failure<std::string>(core::ErrorCode::Conflict, "Sample review packet content differs from its captured digest");
  payload.value().asObject().emplace("packetSha256", packet.packetSha256);
  auto encoded = formats::stringifyJson(payload.value(), true) + "\n";
  if (encoded.size() > kMaximumPacketBytes)
    return core::failure<std::string>(core::ErrorCode::Unsupported, "Sample review packet exceeds its serialized byte limit");
  return encoded;
}

core::Result<SampleCandidateReviewPacket> decodeSampleCandidateReviewPacket(std::string_view json) {
  using Output = SampleCandidateReviewPacket;
  const auto parsed = formats::parseJson(json, {.maximumInputBytes = kMaximumPacketBytes, .maximumDepth = 64U,
      .maximumNodes = 2'000'000U, .maximumStringBytes = 2U * 1024U * 1024U, .maximumCollectionEntries = 250'000U});
  if (!parsed) return core::Result<Output>{parsed.error()};
  const auto& value = parsed.value();
  const auto malformed = [] { return core::failure<Output>(core::ErrorCode::ParseError, "Sample review packet schema or field type is invalid"); };
  if (!exactFields(value, {"format", "schemaVersion", "projectId", "sourceGeneration", "sourceProjectSha256",
      "reviewBasisSha256", "manifest", "units", "packetSha256"})) return malformed();
  for (const auto* key : {"format", "projectId", "sourceProjectSha256", "reviewBasisSha256", "packetSha256"})
    if (!value.find(key)->isString()) return malformed();
  if (value.find("format")->asString() != kPacketFormat || !value.find("schemaVersion")->isInteger() ||
      value.find("schemaVersion")->asInt64() != 1 || !value.find("sourceGeneration")->isInteger() ||
      value.find("sourceGeneration")->asInt64() <= 0 || !value.find("units")->isArray() ||
      value.find("units")->asArray().empty() || value.find("units")->asArray().size() > kMaximumReviewUnits)
    return malformed();
  const auto manifest = voicebank::ManifestJsonCodec{}.decode(formats::stringifyJson(*value.find("manifest"), false));
  if (!manifest) return core::Result<Output>{manifest.error()};
  Output packet{value.find("projectId")->asString(), static_cast<std::uint64_t>(value.find("sourceGeneration")->asInt64()),
      value.find("sourceProjectSha256")->asString(), value.find("reviewBasisSha256")->asString(), manifest.value(), {},
      value.find("packetSha256")->asString()};
  for (const auto& row : value.find("units")->asArray()) {
    if (!exactFields(row, {"unitId", "takeId", "audioSha256", "originOperatorId", "originJournalSha256",
        "originGeneration", "sampleRate", "frameCount", "peak", "rms"})) return malformed();
    for (const auto* key : {"unitId", "takeId", "audioSha256", "originOperatorId", "originJournalSha256"})
      if (!row.find(key)->isString()) return malformed();
    for (const auto* key : {"originGeneration", "sampleRate", "frameCount"})
      if (!row.find(key)->isInteger() || row.find(key)->asInt64() <= 0) return malformed();
    if (row.find("sampleRate")->asInt64() > 384000 || !row.find("peak")->isNumber() || !row.find("rms")->isNumber()) return malformed();
    packet.units.push_back({row.find("unitId")->asString(), row.find("takeId")->asString(), row.find("audioSha256")->asString(),
        row.find("originOperatorId")->asString(), row.find("originJournalSha256")->asString(),
        static_cast<std::uint64_t>(row.find("originGeneration")->asInt64()), static_cast<std::uint32_t>(row.find("sampleRate")->asInt64()),
        static_cast<std::uint64_t>(row.find("frameCount")->asInt64()), row.find("peak")->asNumber(), row.find("rms")->asNumber()});
  }
  const auto encoded = encodeSampleCandidateReviewPacket(packet);
  if (!encoded) return core::Result<Output>{encoded.error()};
  return packet;
}

core::Result<SampleCandidateReviewPacket> prepareSampleCandidateReview(
    const std::filesystem::path& repositoryRoot, const VoicebankProductionProject& project,
    const voicebank::Manifest& manifest, std::stop_token stop) {
  using Output = SampleCandidateReviewPacket;
  auto checked = cancelled(stop);
  if (!checked) return core::Result<Output>{checked.error()};
  const auto workspace = realDirectory(repositoryRoot);
  if (!workspace) return core::Result<Output>{workspace.error()};
  core::ExclusiveFileLock captureLock;
  checked = captureLock.acquire(workspace.value() / ".writer.lock");
  if (!checked) return core::Result<Output>{checked.error()};
  ProductionProjectRepository repository{workspace.value()};
  checked = repository.verify(project);
  if (!checked) return core::Result<Output>{checked.error()};
  // No review/feasibility PASS is created or newly required here. Legacy
  // repository admission still owns its existing source-policy requirements.
  const auto validManifest = boundedManifest(manifest);
  if (!validManifest) return core::Result<Output>{validManifest.error()};
  if (manifest.styles.size() != 1U)
    return core::failure<Output>(core::ErrorCode::Unsupported, "Multi-style review requires style-owned producer assignments");
  if (project.schemaVersion >= kProductionStyleSchemaVersion && project.language !=
      (manifest.language == domain::Language::Japanese ? "ja" : manifest.language == domain::Language::English ? "en" : "ko"))
    return core::failure<Output>(core::ErrorCode::Conflict, "Review language differs from the producer workspace");
  if (manifest.units.empty() || manifest.units.size() > kMaximumReviewUnits || manifest.units.size() > project.unitAssignments.size())
    return core::failure<Output>(core::ErrorCode::Conflict, "Review manifest must name a bounded subset of current producer assignments");
  Output packet{project.projectId, project.lastDurableGeneration, core::sha256Hex(encodeProductionProject(project)),
      reviewBasis(project), manifest, {}, {}};
  std::vector<SampleCandidateUnitBinding> bindings;
  std::set<std::string> mappedTakes;
  for (auto& unit : packet.manifest.units) {
    const auto assignment = std::find_if(project.unitAssignments.begin(), project.unitAssignments.end(), [&](const auto& value) {
      return value.coverageKey == coverageKey(unit) && value.pitchLayer == unit.rootMidi &&
          (project.schemaVersion < kProductionStyleSchemaVersion || value.style == unit.style);
    });
    if (!unit.enabled || assignment == project.unitAssignments.end() || assignment->takeId.empty() ||
        !mappedTakes.insert(assignment->takeId).second)
      return core::failure<Output>(core::ErrorCode::Conflict, "Review unit lacks an unambiguous active take", unit.id);
    const auto take = std::find_if(project.takes.begin(), project.takes.end(), [&](const auto& value) { return value.takeId == assignment->takeId; });
    if (take == project.takes.end()) return core::failure<Output>(core::ErrorCode::NotFound, "Review take is missing", unit.id);
    auto audioDigest = take->rawAssetSha256;
    for (const auto& id : take->derivedRevisionIds) {
      const auto revision = std::find_if(project.derivedRevisions.begin(), project.derivedRevisions.end(), [&](const auto& value) { return value.revisionId == id; });
      if (revision == project.derivedRevisions.end() || revision->inputSha256 != audioDigest)
        return core::failure<Output>(core::ErrorCode::Conflict, "Review processing lineage is incomplete", unit.id);
      audioDigest = revision->outputSha256;
    }
    unit.audioPath = std::filesystem::path{"audio"} / (audioDigest + ".wav");
    bindings.push_back({unit.id, take->takeId, audioDigest, {}, {}});
  }
  const auto origins = collectOriginAttribution(workspace.value(), project, bindings, stop);
  if (!origins) return core::Result<Output>{origins.error()};
  struct AudioFacts { std::uint32_t sampleRate; std::uint64_t frames; double peak, rms; };
  std::map<std::string, AudioFacts> decodedAudio;
  std::uint64_t audioBytes = 0U;
  for (const auto& binding : bindings) {
    checked = cancelled(stop);
    if (!checked) return core::Result<Output>{checked.error()};
    if (!decodedAudio.contains(binding.audioSha256)) {
      const auto asset = std::find_if(project.assets.begin(), project.assets.end(), [&](const auto& value) { return value.sha256 == binding.audioSha256; });
      if (asset == project.assets.end() || asset->byteSize > kMaximumReviewAudioBytes - audioBytes)
        return core::failure<Output>(core::ErrorCode::Unsupported, "Review audio is missing or exceeds its aggregate budget");
      audioBytes += asset->byteSize;
      const auto audio = voicebank::readWav(repository.assetPath(*asset));
      if (!audio) return core::Result<Output>{audio.error()};
      const auto digest = core::sha256File(repository.assetPath(*asset), kMaximumReviewAudioBytes);
      if (!digest || digest.value() != binding.audioSha256)
        return core::failure<Output>(core::ErrorCode::Conflict, "Review audio changed during inspection", binding.takeId);
      if (audio.value().channels != 1U || audio.value().sampleRate != packet.manifest.expectedSampleRate ||
          !std::all_of(audio.value().interleaved.begin(), audio.value().interleaved.end(), [](float value) { return std::isfinite(value); }))
        return core::failure<Output>(core::ErrorCode::Conflict, "Review audio must be finite mono at the declared rate", binding.takeId);
      const auto statistics = voicebank::analyzeAudio(audio.value().interleaved);
      decodedAudio.emplace(binding.audioSha256, AudioFacts{audio.value().sampleRate,
          static_cast<std::uint64_t>(audio.value().frameCount()), statistics.peak, statistics.rms});
    }
    const auto& audio = decodedAudio.at(binding.audioSha256);
    const auto* unit = packet.manifest.findUnit(binding.unitId);
    checked = unit->markers.validate(static_cast<time::SampleFrame>(audio.frames));
    if (!checked) return core::Result<Output>{checked.error()};
    const auto& origin = origins.value().at(binding.takeId);
    packet.units.push_back({binding.unitId, binding.takeId, binding.audioSha256, origin.actor, origin.journalSha256,
        origin.generation, audio.sampleRate, audio.frames, audio.peak, audio.rms});
  }
  checked = cancelled(stop);
  if (!checked) return core::Result<Output>{checked.error()};
  checked = repository.verify(project);
  if (!checked) return core::Result<Output>{checked.error()};
  const auto payload = packetPayload(packet);
  if (!payload) return core::Result<Output>{payload.error()};
  packet.packetSha256 = core::sha256Hex(formats::stringifyJson(payload.value(), false));
  const auto encoded = encodeSampleCandidateReviewPacket(packet);
  if (!encoded) return core::Result<Output>{encoded.error()};
  return packet;
}

core::Result<SampleCandidateReviewReceipt> commitSampleCandidateReview(
    const std::filesystem::path& repositoryRoot, VoicebankProductionProject& project,
    const SampleCandidateReviewPacket& packet, std::string reviewerId, std::string reviewedAtUtc,
    SampleCandidateReviewDecision decision, std::span<const std::string> unitIds, std::stop_token stop) {
  using Output = SampleCandidateReviewReceipt;
  const auto encoded = encodeSampleCandidateReviewPacket(packet);
  if (!encoded) return core::Result<Output>{encoded.error()};
  if (reviewerId.empty() || !isProductionUtcTimestamp(reviewedAtUtc) ||
      (decision != SampleCandidateReviewDecision::Accept && decision != SampleCandidateReviewDecision::Reject))
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Explicit sample review actor, time, or decision is invalid");
  const auto current = prepareSampleCandidateReview(repositoryRoot, project, packet.manifest, stop);
  if (!current) return core::Result<Output>{current.error()};
  if (current.value().packetSha256 != packet.packetSha256)
    return core::failure<Output>(core::ErrorCode::Conflict, "Review packet is stale for the current generation or material");
  std::set<std::string> selected;
  if (unitIds.empty()) for (const auto& unit : packet.units) selected.insert(unit.unitId);
  else for (const auto& id : unitIds)
    if (!packet.manifest.findUnit(id) || !selected.insert(id).second)
      return core::failure<Output>(core::ErrorCode::InvalidArgument, "Review selection has missing or duplicate units", id);
  const auto bindings = packetBindings(packet);
  for (std::size_t index = 0U; index < packet.units.size(); ++index) {
    const auto& unit = packet.units[index];
    if (!selected.contains(unit.unitId)) continue;
    const auto independent = validateIndependentReviewer(project, bindings[index], reviewerId,
        {unit.originOperatorId, unit.originGeneration, unit.originJournalSha256});
    if (!independent) return core::Result<Output>{independent.error()};
  }
  auto next = project;
  if (decision == SampleCandidateReviewDecision::Reject) invalidateProductionQualification(next);
  Output receipt;
  for (std::size_t index = 0U; index < packet.units.size(); ++index) {
    const auto& unit = packet.units[index];
    if (!selected.contains(unit.unitId)) continue;
    const auto decisionName = decision == SampleCandidateReviewDecision::Accept ? "PASS" : "REJECTED";
    const auto identity = core::sha256Hex(formats::stringifyJson(J{J::Object{
        {"format", "sample-review-decision-v1"}, {"packetSha256", packet.packetSha256}, {"unitId", unit.unitId},
        {"reviewerId", reviewerId}, {"reviewedAtUtc", reviewedAtUtc}, {"decision", decisionName}}}, false));
    auto binding = bindings[index];
    binding.reviewId = "review-" + identity;
    binding.reviewMetadataRevisionId = "review-material-" + identity;
    if (std::any_of(next.reviews.begin(), next.reviews.end(), [&](const auto& value) { return value.reviewId == binding.reviewId; }) ||
        std::any_of(next.metadataRevisions.begin(), next.metadataRevisions.end(), [&](const auto& value) { return value.revisionId == binding.reviewMetadataRevisionId; }))
      return core::failure<Output>(core::ErrorCode::Conflict, "Sample review decision identity already exists");
    const auto material = sampleCandidateReviewValues(project, packet.manifest, binding);
    if (!material) return core::Result<Output>{material.error()};
    const auto take = std::find_if(next.takes.begin(), next.takes.end(), [&](const auto& value) { return value.takeId == unit.takeId; });
    const auto assignment = std::find_if(next.unitAssignments.begin(), next.unitAssignments.end(), [&](const auto& value) { return value.takeId == unit.takeId; });
    if (take == next.takes.end() || assignment == next.unitAssignments.end())
      return core::failure<Output>(core::ErrorCode::Conflict, "Review active take disappeared");
    ReviewRecord review{binding.reviewId, unit.takeId, reviewerId, decisionName, reviewedAtUtc};
    next.reviews.push_back(review);
    receipt.reviews.push_back(std::move(review));
    next.metadataRevisions.push_back({binding.reviewMetadataRevisionId, unit.takeId, take->rawAssetSha256,
        std::string{kSampleCandidateReviewKind}, material.value(), reviewerId, reviewedAtUtc});
    take->state = decision == SampleCandidateReviewDecision::Accept ? UnitQueueState::Approved : UnitQueueState::Rejected;
    assignment->state = take->state;
    assignment->markerReviewed = decision == SampleCandidateReviewDecision::Accept;
    assignment->pitchReviewed = decision == SampleCandidateReviewDecision::Accept;
  }
  ProductionProjectRepository repository{repositoryRoot};
  const auto verified = repository.verify(project);
  if (!verified) return core::Result<Output>{verified.error()};
  // save owns the writer lock and generation comparison. The capture lock was
  // released by prepare; reacquiring it here would deadlock the canonical save.
  const auto saved = repository.save(next, {.action = "review", .subjectId = packet.packetSha256,
      .operatorId = reviewerId, .occurredAtUtc = reviewedAtUtc}, stop);
  if (!saved) {
    const auto recovered = repository.recover();
    if (!recovered || recovered.value().lastDurableGeneration <= project.lastDurableGeneration)
      return core::failure<Output>(saved.error().code,
          "Review save did not confirm a commit. Recover the producer and compare the intended review before retrying: " + saved.error().message,
          "Review packet " + packet.packetSha256);
    auto comparable = recovered.value();
    comparable.lastDurableGeneration = next.lastDurableGeneration;
    if (encodeProductionProject(comparable) != encodeProductionProject(next))
      return core::failure<Output>(core::ErrorCode::Conflict,
          "Review save failed and recovery found a different generation. Recover before retrying; no matching decision is confirmed.",
          "Review packet " + packet.packetSha256);
    next = recovered.value();
    receipt.durabilityConfirmed = false;
    receipt.diagnostic = "The exact review decision is recoverably committed, but pointer durability is uncertain. Recover and inspect the returned generation and project digest before further edits or publication; do not repeat this review. " + saved.error().message;
  }
  project = std::move(next);
  receipt.committedGeneration = project.lastDurableGeneration;
  receipt.committedProjectSha256 = core::sha256Hex(encodeProductionProject(project));
  const auto candidate = resolveReviewedSampleCandidate(repositoryRoot, project, packet.manifest);
  if (candidate) receipt.candidate = candidate.value();
  return receipt;
}

core::Result<SampleCandidateRequest> resolveReviewedSampleCandidate(
    const std::filesystem::path& repositoryRoot, const VoicebankProductionProject& project,
    const voicebank::Manifest& editedManifest, std::stop_token stop) {
  const auto current = prepareSampleCandidateReview(repositoryRoot, project, editedManifest, stop);
  if (!current) return core::Result<SampleCandidateRequest>{current.error()};
  return currentCandidate(project, current.value());
}

}  // namespace seam::voicebank_production
