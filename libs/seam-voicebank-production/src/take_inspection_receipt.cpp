#include "seam/voicebank_production/take_inspection_receipt.hpp"

#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace seam::voicebank_production {
namespace {

using Json = formats::JsonValue;

constexpr std::size_t kMaximumEvidenceBytes = 64U * 1024U;
constexpr std::string_view kPassedStatus = "SIGNAL_CHECKS_PASSED";
constexpr std::string_view kNeedsReviewStatus = "SIGNAL_CHECKS_NEED_REVIEW";

core::Result<TakeInspectionEvidence> invalid(std::string message) {
  return core::failure<TakeInspectionEvidence>(core::ErrorCode::InvariantViolation,
                                               std::move(message));
}

bool isLowerDigest(std::string_view value) {
  return value.size() == 64U && std::all_of(value.begin(), value.end(), [](char item) {
    return (item >= '0' && item <= '9') || (item >= 'a' && item <= 'f');
  });
}

Json optionalInteger(const std::optional<std::int32_t>& value) {
  return value ? Json{static_cast<std::int64_t>(*value)} : Json{};
}

Json optionalNumber(const std::optional<double>& value) {
  return value ? Json{*value} : Json{};
}

bool exactKeys(const Json& object, std::initializer_list<std::string_view> keys) {
  if (!object.isObject() || object.asObject().size() != keys.size()) return false;
  return std::all_of(keys.begin(), keys.end(), [&](std::string_view key) {
    return object.find(key) != nullptr;
  });
}

std::optional<std::int64_t> integerIn(const Json* value, std::int64_t minimum, std::int64_t maximum) {
  if (value == nullptr || !value->isInteger()) return std::nullopt;
  const auto result = value->asInt64();
  if (result < minimum || result > maximum) return std::nullopt;
  return result;
}

std::optional<double> finiteNumber(const Json* value) {
  if (value == nullptr || !value->isNumber()) return std::nullopt;
  const auto result = value->asNumber();
  if (!std::isfinite(result)) return std::nullopt;
  return result;
}

}  // namespace

core::Result<voicebank::TakeInspectionRequest> takeInspectionRequestFor(
    std::string_view coverageKey, std::int32_t pitchLayer) {
  const auto policy = voicebank::takeQcPolicyForCoverageKey(coverageKey);
  if (!policy) return core::Result<voicebank::TakeInspectionRequest>{policy.error()};
  voicebank::TakeInspectionRequest request{.policy = policy.value()};
  if (policy.value() == voicebank::TakeQcPolicy::Voiced) {
    if (pitchLayer < 0 || pitchLayer > 127) {
      return core::failure<voicebank::TakeInspectionRequest>(core::ErrorCode::InvalidArgument,
          "A voiced unit needs a pitch layer between MIDI 0 and 127");
    }
    request.expectedRootMidi = pitchLayer;
  }
  return request;
}

formats::JsonValue takeInspectionJson(const voicebank::TakeInspection& inspection) {
  const auto& measured = inspection.measurements;
  Json::Object checks;
  for (const auto check : voicebank::kTakeChecks) {
    checks.emplace(std::string{voicebank::takeCheckName(check)},
                   Json{std::string{voicebank::takeCheckOutcomeName(inspection.outcome(check))}});
  }
  Json::Object measurements{
      {"sampleRate", Json{static_cast<std::int64_t>(measured.sampleRate)}},
      {"channels", Json{static_cast<std::int64_t>(measured.channels)}},
      {"bitsPerSample", Json{static_cast<std::int64_t>(measured.bitsPerSample)}},
      {"frameCount", Json{static_cast<std::int64_t>(measured.frameCount)}},
      {"nonFiniteSamples", Json{static_cast<std::int64_t>(measured.nonFiniteSamples)}},
      {"clippedSamples", Json{static_cast<std::int64_t>(measured.clippedSamples)}},
      {"peak", Json{measured.peak}},
      {"rms", Json{measured.rms}},
      {"dcOffset", Json{measured.dcOffset}},
      {"expectedRootMidi", optionalInteger(measured.expectedRootMidi)},
      {"analyzedRootMidi", optionalInteger(measured.analyzedRootMidi)},
      {"rootPitchDeviationCents", optionalNumber(measured.rootPitchDeviationCents)},
      {"voicedShare", optionalNumber(measured.voicedShare)},
  };
  return Json{Json::Object{
      {"inspectorId", Json{std::string{voicebank::kTakeInspectorId}}},
      {"inspectorVersion", Json{std::string{voicebank::kTakeInspectorVersion}}},
      {"policy", Json{std::string{voicebank::takeQcPolicyName(inspection.policy)}}},
      {"policyVersion", Json{voicebank::kTakeQcPolicyVersion}},
      {"takeSha256", Json{inspection.sourceSha256}},
      {"byteSize", Json{static_cast<std::int64_t>(inspection.byteSize)}},
      {"measurements", Json{std::move(measurements)}},
      {"checks", Json{std::move(checks)}},
      {"status", Json{std::string{inspection.accepted() ? kPassedStatus : kNeedsReviewStatus}}},
  }};
}

std::string encodeTakeInspectionEvidence(const TakeInspectionEvidence& evidence) {
  Json::Object binding{
      {"takeId", Json{evidence.binding.takeId}},
      {"promptId", Json{evidence.binding.promptId}},
      {"coverageKey", Json{evidence.binding.coverageKey}},
      {"pitchLayer", Json{static_cast<std::int64_t>(evidence.binding.pitchLayer)}},
  };
  auto object = takeInspectionJson(evidence.inspection);
  object.asObject().emplace("schemaVersion", Json{std::int64_t{2}});
  object.asObject().emplace("binding", Json{std::move(binding)});
  return formats::stringifyJson(object, false);
}

core::Result<TakeInspectionEvidence> decodeTakeInspectionEvidence(std::string_view text) {
  if (text.size() > kMaximumEvidenceBytes) return invalid("Take inspection evidence is too large");
  const auto parsed = formats::parseJson(text, {.maximumInputBytes = kMaximumEvidenceBytes,
      .maximumDepth = 4U, .maximumNodes = 96U, .maximumStringBytes = 4096U,
      .maximumCollectionEntries = 16U});
  if (!parsed) return invalid("Take inspection evidence is not bounded JSON");
  const auto& root = parsed.value();
  if (!exactKeys(root, {"schemaVersion", "inspectorId", "inspectorVersion", "policy",
          "policyVersion", "binding", "takeSha256", "byteSize", "measurements", "checks", "status"}))
    return invalid("Take inspection evidence fields are incomplete or unknown");
  const auto* inspectorId = root.find("inspectorId");
  const auto* inspectorVersion = root.find("inspectorVersion");
  const auto* policyName = root.find("policy");
  const auto* takeSha256 = root.find("takeSha256");
  const auto* status = root.find("status");
  if (integerIn(root.find("schemaVersion"), 2, 2) != 2 ||
      !inspectorId->isString() || inspectorId->asString() != voicebank::kTakeInspectorId ||
      !inspectorVersion->isString() || inspectorVersion->asString() != voicebank::kTakeInspectorVersion ||
      integerIn(root.find("policyVersion"), voicebank::kTakeQcPolicyVersion, voicebank::kTakeQcPolicyVersion) !=
          voicebank::kTakeQcPolicyVersion ||
      !policyName->isString() || !takeSha256->isString() || !isLowerDigest(takeSha256->asString()) ||
      !status->isString())
    return invalid("Take inspection evidence is not a current inspector and policy version");
  const auto policy = voicebank::parseTakeQcPolicy(policyName->asString());
  const auto byteSize = integerIn(root.find("byteSize"), 1, std::numeric_limits<std::int64_t>::max());
  if (!policy || !byteSize) return invalid("Take inspection policy or byte size is invalid");

  const auto& binding = *root.find("binding");
  if (!exactKeys(binding, {"takeId", "promptId", "coverageKey", "pitchLayer"}))
    return invalid("Take inspection binding fields are incomplete or unknown");
  const auto* takeId = binding.find("takeId");
  const auto* promptId = binding.find("promptId");
  const auto* coverageKey = binding.find("coverageKey");
  const auto pitchLayer = integerIn(binding.find("pitchLayer"), std::numeric_limits<std::int32_t>::min(),
                                    std::numeric_limits<std::int32_t>::max());
  if (!takeId->isString() || takeId->asString().empty() || !promptId->isString() ||
      !coverageKey->isString() || !pitchLayer)
    return invalid("Take inspection binding values are invalid");

  const auto& measurements = *root.find("measurements");
  if (!exactKeys(measurements, {"sampleRate", "channels", "bitsPerSample", "frameCount",
          "nonFiniteSamples", "clippedSamples", "peak", "rms", "dcOffset", "expectedRootMidi",
          "analyzedRootMidi", "rootPitchDeviationCents", "voicedShare"}))
    return invalid("Take inspection measurements are incomplete or unknown");
  constexpr auto maximumFrames = std::int64_t{1} << 53;
  const auto sampleRate = integerIn(measurements.find("sampleRate"), 8000, 384000);
  const auto channels = integerIn(measurements.find("channels"), 1, 8);
  const auto bits = integerIn(measurements.find("bitsPerSample"), 8, 32);
  const auto frames = integerIn(measurements.find("frameCount"), 0, maximumFrames);
  const auto nonFinite = integerIn(measurements.find("nonFiniteSamples"), 0, maximumFrames);
  const auto clipped = integerIn(measurements.find("clippedSamples"), 0, maximumFrames);
  const auto peak = finiteNumber(measurements.find("peak"));
  const auto rms = finiteNumber(measurements.find("rms"));
  const auto dcOffset = finiteNumber(measurements.find("dcOffset"));
  constexpr auto maximumMagnitude = static_cast<double>(std::numeric_limits<float>::max());
  if (!sampleRate || !channels || !bits || !frames || !nonFinite || !clipped || !peak || !rms ||
      !dcOffset || *nonFinite > *frames || *clipped > *frames || *peak < 0.0 ||
      *peak > maximumMagnitude || *rms < 0.0 || *rms > maximumMagnitude ||
      std::abs(*dcOffset) > maximumMagnitude ||
      static_cast<double>(static_cast<float>(*peak)) != *peak)
    return invalid("Take inspection measurements are out of range");
  voicebank::TakeMeasurements measured{
      .sampleRate = static_cast<std::uint32_t>(*sampleRate),
      .channels = static_cast<std::uint16_t>(*channels),
      .bitsPerSample = static_cast<std::uint16_t>(*bits),
      .frameCount = static_cast<std::uint64_t>(*frames),
      .nonFiniteSamples = static_cast<std::uint64_t>(*nonFinite),
      .clippedSamples = static_cast<std::uint64_t>(*clipped),
      .peak = static_cast<float>(*peak),
      .rms = *rms,
      .dcOffset = *dcOffset,
  };
  const auto optionalMidi = [&](std::string_view key, std::optional<std::int32_t>& target) {
    const auto* value = measurements.find(key);
    if (value->isNull()) return true;
    const auto midi = integerIn(value, 0, 127);
    if (!midi) return false;
    target = static_cast<std::int32_t>(*midi);
    return true;
  };
  const auto optionalFinite = [&](std::string_view key, std::optional<double>& target) {
    const auto* value = measurements.find(key);
    if (value->isNull()) return true;
    const auto number = finiteNumber(value);
    if (!number) return false;
    target = *number;
    return true;
  };
  if (!optionalMidi("expectedRootMidi", measured.expectedRootMidi) ||
      !optionalMidi("analyzedRootMidi", measured.analyzedRootMidi) ||
      !optionalFinite("rootPitchDeviationCents", measured.rootPitchDeviationCents) ||
      !optionalFinite("voicedShare", measured.voicedShare) ||
      (measured.voicedShare && (*measured.voicedShare < 0.0 || *measured.voicedShare > 1.0)))
    return invalid("Take inspection pitch measurements are invalid");

  const auto& checks = *root.find("checks");
  if (!checks.isObject() || checks.asObject().size() != voicebank::kTakeCheckCount)
    return invalid("Take inspection checks are incomplete or unknown");
  voicebank::TakeCheckOutcomes outcomes{};
  for (const auto check : voicebank::kTakeChecks) {
    const auto* value = checks.find(voicebank::takeCheckName(check));
    const auto outcome = value != nullptr && value->isString()
        ? voicebank::parseTakeCheckOutcome(value->asString()) : std::nullopt;
    if (!outcome) return invalid("Take inspection check outcome is invalid");
    outcomes[static_cast<std::size_t>(check)] = *outcome;
  }
  TakeInspectionEvidence evidence{
      .binding = {takeId->asString(), promptId->asString(), coverageKey->asString(),
                  static_cast<std::int32_t>(*pitchLayer)},
      .inspection = {.policy = *policy, .sourceSha256 = takeSha256->asString(),
                     .byteSize = static_cast<std::uint64_t>(*byteSize), .measurements = measured,
                     .checks = outcomes},
  };
  if (status->asString() != (evidence.inspection.accepted() ? kPassedStatus : kNeedsReviewStatus))
    return invalid("Take inspection status does not match its check outcomes");
  if (encodeTakeInspectionEvidence(evidence) != text)
    return invalid("Take inspection evidence is not in canonical form");
  return evidence;
}

MetadataRevision makeTakeInspectionRevision(const TakeInspectionEvidence& evidence,
                                            std::string operatorId, std::string performedAtUtc) {
  auto evidenceJson = encodeTakeInspectionEvidence(evidence);
  auto evidenceSha256 = core::sha256Hex(evidenceJson);
  return MetadataRevision{
      .revisionId = "take-inspection-" + evidenceSha256.substr(0U, 32U),
      .takeId = evidence.binding.takeId,
      .rawAssetSha256 = evidence.inspection.sourceSha256,
      .kind = std::string{kTakeInspectionRevisionKind},
      .values = {{"evidenceJson", std::move(evidenceJson)},
                 {"evidenceSha256", std::move(evidenceSha256)}},
      .operatorId = std::move(operatorId),
      .performedAtUtc = std::move(performedAtUtc),
  };
}

core::Result<TakeInspectionEvidence> validateTakeInspectionRevision(
    const VoicebankProductionProject& project, const MetadataRevision& revision) {
  if (revision.kind != kTakeInspectionRevisionKind) return invalid("Revision is not a take inspection receipt");
  const auto& values = revision.values;
  if (values.size() != 2U || !values.contains("evidenceJson") || !values.contains("evidenceSha256") ||
      !isLowerDigest(values.at("evidenceSha256")) ||
      core::sha256Hex(values.at("evidenceJson")) != values.at("evidenceSha256") ||
      revision.revisionId != "take-inspection-" + values.at("evidenceSha256").substr(0U, 32U))
    return invalid("Take inspection receipt digest or identity is invalid");
  auto evidence = decodeTakeInspectionEvidence(values.at("evidenceJson"));
  if (!evidence) return evidence;
  const auto& decoded = evidence.value();
  const auto take = std::find_if(project.takes.begin(), project.takes.end(),
      [&](const TakeRecord& value) { return value.takeId == revision.takeId; });
  if (take == project.takes.end()) return invalid("Take inspection receipt names no take");
  const TakeInspectionBinding expected{take->takeId, take->promptId, take->coverageKey,
                                       take->pitchLayer};
  if (decoded.binding != expected)
    return invalid("Take inspection receipt was made for a different assignment");
  if (decoded.inspection.sourceSha256 != revision.rawAssetSha256 ||
      revision.rawAssetSha256 != take->rawAssetSha256)
    return invalid("Take inspection receipt does not describe the stored take bytes");
  const auto asset = std::find_if(project.assets.begin(), project.assets.end(),
      [&](const AssetRecord& value) { return value.sha256 == take->rawAssetSha256; });
  if (asset == project.assets.end() || asset->byteSize != decoded.inspection.byteSize)
    return invalid("Take inspection receipt byte size differs from the stored asset");
  const auto request = takeInspectionRequestFor(take->coverageKey, take->pitchLayer);
  if (!request || request.value().policy != decoded.inspection.policy)
    return invalid("Take inspection receipt used a QC policy that does not apply to its unit");
  const auto& measured = decoded.inspection.measurements;
  const bool voiced = decoded.inspection.policy == voicebank::TakeQcPolicy::Voiced;
  const bool breath = decoded.inspection.policy == voicebank::TakeQcPolicy::Breath;
  if (measured.expectedRootMidi != request.value().expectedRootMidi ||
      (!voiced && (measured.analyzedRootMidi || measured.rootPitchDeviationCents)) ||
      (!breath && measured.voicedShare) ||
      measured.analyzedRootMidi.has_value() != measured.rootPitchDeviationCents.has_value())
    return invalid("Take inspection receipt reports measurements its policy does not use");
  if (decoded.inspection.checks != voicebank::evaluateTakeChecks(decoded.inspection.policy, measured))
    return invalid("Take inspection outcomes do not follow from their measurements");
  return evidence;
}

std::optional<TakeInspectionEvidence> currentTakeInspection(
    const VoicebankProductionProject& project, std::string_view takeId) {
  const auto take = std::find_if(project.takes.begin(), project.takes.end(),
      [&](const TakeRecord& value) { return value.takeId == takeId; });
  if (take == project.takes.end()) return std::nullopt;
  for (auto revision = project.metadataRevisions.rbegin(); revision != project.metadataRevisions.rend(); ++revision) {
    if (revision->takeId != takeId || revision->kind != kTakeInspectionRevisionKind ||
        revision->rawAssetSha256 != take->rawAssetSha256) continue;
    auto evidence = validateTakeInspectionRevision(project, *revision);
    if (evidence) return std::move(evidence).value();
  }
  return std::nullopt;
}

bool sameMetadataRevision(const MetadataRevision& left, const MetadataRevision& right) {
  return left.revisionId == right.revisionId && left.takeId == right.takeId &&
         left.rawAssetSha256 == right.rawAssetSha256 && left.kind == right.kind &&
         left.values == right.values && left.operatorId == right.operatorId &&
         left.performedAtUtc == right.performedAtUtc;
}

}  // namespace seam::voicebank_production
