#include "seam/distribution/procedural_review.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <system_error>
#include <utility>

namespace seam::distribution {
namespace {

using seam::formats::JsonValue;
using Object = JsonValue::Object;

// Evidence larger than this is refused rather than hashed, so a mistaken path to a large file cannot
// turn a review freeze into an unbounded read.
constexpr std::uint64_t kMaximumEvidenceBytes = 512ULL * 1024ULL * 1024ULL;

bool isLowercaseSha256(std::string_view value) noexcept {
  if (value.size() != 64U) return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char character) {
    return std::isdigit(character) != 0 || (character >= 'a' && character <= 'f');
  });
}

// A digest that a caller wrote by hand may be malformed; refusing is correct because the digest is
// what binds a decision to evidence.
core::Result<void> requireDigest(std::string_view value, std::string_view field) {
  if (!isLowercaseSha256(value))
    return core::failure(core::ErrorCode::InvalidArgument,
                         "A procedural review digest must be lowercase SHA-256 hex",
                         std::string{field});
  return core::success();
}

core::Result<std::string> requiredString(const JsonValue& root, std::string_view field,
                                         std::size_t maximum) {
  const auto* value = root.find(field);
  if (value == nullptr || !value->isString() || value->asString().empty() ||
      value->asString().size() > maximum) {
    return core::failure<std::string>(core::ErrorCode::ParseError,
                                      "Procedural review basis field is missing or oversized",
                                      std::string{field});
  }
  return value->asString();
}

core::Result<std::uint32_t> requiredCount(const JsonValue& root, std::string_view field) {
  const auto* value = root.find(field);
  if (value == nullptr || !value->isInteger() || value->asInt64() <= 0 ||
      value->asInt64() > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
    return core::failure<std::uint32_t>(core::ErrorCode::ParseError,
                                        "Procedural review basis count is invalid",
                                        std::string{field});
  }
  return static_cast<std::uint32_t>(value->asInt64());
}

}  // namespace

core::Result<void> ProceduralReviewBasis::validate() const {
  if (resourceId.empty() || resourceId.size() > 128U)
    return core::failure(core::ErrorCode::InvalidArgument,
                         "A review basis needs a bounded resource id");
  if (version.empty() || version.size() > 128U)
    return core::failure(core::ErrorCode::InvalidArgument,
                         "A review basis needs a bounded resource version");
  auto recipe = requireDigest(recipeSha256, "recipeSha256");
  if (!recipe) return recipe;
  if (contentHash.empty() || contentHash.size() > 256U)
    return core::failure(core::ErrorCode::InvalidArgument,
                         "A review basis needs the installed content hash it reviewed");
  if (engineId.empty() || engineId.size() > 128U)
    return core::failure(core::ErrorCode::InvalidArgument, "A review basis needs an engine id");
  if (engineRevision == 0U)
    return core::failure(core::ErrorCode::InvalidArgument,
                         "A review basis needs a nonzero engine revision");
  if (renderAbi.empty() || renderAbi.size() > 128U)
    return core::failure(core::ErrorCode::InvalidArgument, "A review basis needs a render ABI");
  if (compilerRevision == 0U)
    return core::failure(core::ErrorCode::InvalidArgument,
                         "A review basis needs a nonzero compiler revision");
  if (sampleRate == 0U)
    return core::failure(core::ErrorCode::InvalidArgument, "A review basis needs a sample rate");
  // Evidence digests may be empty on a basis that records a rejection, but a digest that is present
  // must be well formed rather than a value nobody can check.
  if (!scoreSha256.empty()) {
    auto score = requireDigest(scoreSha256, "scoreSha256");
    if (!score) return score;
  }
  if (!audioSha256.empty()) {
    auto audio = requireDigest(audioSha256, "audioSha256");
    if (!audio) return audio;
  }
  if (settingsDigest.empty() || settingsDigest.size() > 256U)
    return core::failure(core::ErrorCode::InvalidArgument,
                         "A review basis needs the render settings it was taken at");
  return core::success();
}

core::Result<std::string> ProceduralReviewBasis::digest() const {
  const auto valid = validate();
  if (!valid) return core::Result<std::string>{valid.error()};
  // The digest is over the canonical encoding, so two bases that mean the same thing hash the same
  // way and a field-order or whitespace difference cannot be mistaken for a new basis.
  const auto encoded = formats::stringifyJson(JsonValue{Object{
      {"formatId", JsonValue{std::string{kFormatId}}},
      {"schemaVersion", JsonValue{static_cast<std::int64_t>(kSchemaVersion)}},
      {"resourceId", JsonValue{resourceId}},
      {"version", JsonValue{version}},
      {"recipeSha256", JsonValue{recipeSha256}},
      {"contentHash", JsonValue{contentHash}},
      {"engineId", JsonValue{engineId}},
      {"engineRevision", JsonValue{static_cast<std::int64_t>(engineRevision)}},
      {"renderAbi", JsonValue{renderAbi}},
      {"compilerRevision", JsonValue{static_cast<std::int64_t>(compilerRevision)}},
      {"sampleRate", JsonValue{static_cast<std::int64_t>(sampleRate)}},
      {"scoreSha256", JsonValue{scoreSha256}},
      {"audioSha256", JsonValue{audioSha256}},
      {"settingsDigest", JsonValue{settingsDigest}},
  }}, true);
  return core::sha256Hex(std::string_view{encoded});
}

core::Result<std::string> ProceduralReviewBasisJsonCodec::encode(
    const ProceduralReviewBasis& basis) const {
  const auto valid = basis.validate();
  if (!valid) return core::Result<std::string>{valid.error()};
  return formats::stringifyJson(JsonValue{Object{
      {"formatId", JsonValue{std::string{ProceduralReviewBasis::kFormatId}}},
      {"schemaVersion",
       JsonValue{static_cast<std::int64_t>(ProceduralReviewBasis::kSchemaVersion)}},
      {"resourceId", JsonValue{basis.resourceId}},
      {"version", JsonValue{basis.version}},
      {"recipeSha256", JsonValue{basis.recipeSha256}},
      {"contentHash", JsonValue{basis.contentHash}},
      {"engineId", JsonValue{basis.engineId}},
      {"engineRevision", JsonValue{static_cast<std::int64_t>(basis.engineRevision)}},
      {"renderAbi", JsonValue{basis.renderAbi}},
      {"compilerRevision", JsonValue{static_cast<std::int64_t>(basis.compilerRevision)}},
      {"sampleRate", JsonValue{static_cast<std::int64_t>(basis.sampleRate)}},
      {"scoreSha256", JsonValue{basis.scoreSha256}},
      {"audioSha256", JsonValue{basis.audioSha256}},
      {"settingsDigest", JsonValue{basis.settingsDigest}},
  }}, true);
}

core::Result<ProceduralReviewBasis> ProceduralReviewBasisJsonCodec::decode(
    std::string_view json) const {
  auto parsed = formats::parseJson(json, formats::JsonParseLimits{
      .maximumInputBytes = 1024U * 1024U,
      .maximumDepth = 8U,
      .maximumNodes = 4096U,
      .maximumStringBytes = 16U * 1024U,
      .maximumCollectionEntries = 256U,
  });
  if (!parsed) return core::Result<ProceduralReviewBasis>{parsed.error()};
  if (!parsed.value().isObject())
    return core::failure<ProceduralReviewBasis>(core::ErrorCode::ParseError,
                                                "Procedural review basis root must be an object");
  const auto& root = parsed.value();
  const auto* formatId = root.find("formatId");
  const auto* schema = root.find("schemaVersion");
  if (formatId == nullptr || !formatId->isString() ||
      formatId->asString() != ProceduralReviewBasis::kFormatId) {
    return core::failure<ProceduralReviewBasis>(core::ErrorCode::Unsupported,
                                                "Unsupported procedural review basis format");
  }
  if (schema == nullptr || !schema->isNumber() ||
      schema->asInt64() != ProceduralReviewBasis::kSchemaVersion) {
    return core::failure<ProceduralReviewBasis>(core::ErrorCode::Unsupported,
                                                "Unsupported procedural review basis schema");
  }
  ProceduralReviewBasis basis;
  auto resourceId = requiredString(root, "resourceId", 128U);
  auto version = requiredString(root, "version", 128U);
  auto recipeSha256 = requiredString(root, "recipeSha256", 64U);
  auto contentHash = requiredString(root, "contentHash", 256U);
  auto engineId = requiredString(root, "engineId", 128U);
  auto renderAbi = requiredString(root, "renderAbi", 128U);
  auto settingsDigest = requiredString(root, "settingsDigest", 256U);
  if (!resourceId) return core::Result<ProceduralReviewBasis>{resourceId.error()};
  if (!version) return core::Result<ProceduralReviewBasis>{version.error()};
  if (!recipeSha256) return core::Result<ProceduralReviewBasis>{recipeSha256.error()};
  if (!contentHash) return core::Result<ProceduralReviewBasis>{contentHash.error()};
  if (!engineId) return core::Result<ProceduralReviewBasis>{engineId.error()};
  if (!renderAbi) return core::Result<ProceduralReviewBasis>{renderAbi.error()};
  if (!settingsDigest) return core::Result<ProceduralReviewBasis>{settingsDigest.error()};
  auto engineRevision = requiredCount(root, "engineRevision");
  auto compilerRevision = requiredCount(root, "compilerRevision");
  auto sampleRate = requiredCount(root, "sampleRate");
  if (!engineRevision) return core::Result<ProceduralReviewBasis>{engineRevision.error()};
  if (!compilerRevision) return core::Result<ProceduralReviewBasis>{compilerRevision.error()};
  if (!sampleRate) return core::Result<ProceduralReviewBasis>{sampleRate.error()};
  // The evidence digests are optional on decode, but a present value must still be well formed.
  const auto optionalDigest = [&root](std::string_view field,
                                      std::string& target) -> core::Result<void> {
    const auto* value = root.find(field);
    if (value == nullptr) return core::success();
    if (!value->isString() || value->asString().size() > 64U)
      return core::failure(core::ErrorCode::ParseError,
                           "Procedural review evidence digest is invalid", std::string{field});
    target = value->asString();
    return core::success();
  };
  auto score = optionalDigest("scoreSha256", basis.scoreSha256);
  if (!score) return core::Result<ProceduralReviewBasis>{score.error()};
  auto audio = optionalDigest("audioSha256", basis.audioSha256);
  if (!audio) return core::Result<ProceduralReviewBasis>{audio.error()};
  basis.resourceId = resourceId.value();
  basis.version = version.value();
  basis.recipeSha256 = recipeSha256.value();
  basis.contentHash = contentHash.value();
  basis.engineId = engineId.value();
  basis.renderAbi = renderAbi.value();
  basis.settingsDigest = settingsDigest.value();
  basis.engineRevision = engineRevision.value();
  basis.compilerRevision = compilerRevision.value();
  basis.sampleRate = sampleRate.value();
  const auto valid = basis.validate();
  if (!valid) return core::Result<ProceduralReviewBasis>{valid.error()};
  return basis;
}

std::vector<std::string> proceduralReviewBasisDifferences(const ProceduralReviewBasis& recorded,
                                                          const ProceduralReviewBasis& current) {
  std::vector<std::string> differences;
  if (recorded.resourceId != current.resourceId) differences.emplace_back("resourceId");
  if (recorded.version != current.version) differences.emplace_back("version");
  if (recorded.recipeSha256 != current.recipeSha256) differences.emplace_back("recipeSha256");
  if (recorded.contentHash != current.contentHash) differences.emplace_back("contentHash");
  if (recorded.engineId != current.engineId) differences.emplace_back("engineId");
  if (recorded.engineRevision != current.engineRevision) differences.emplace_back("engineRevision");
  if (recorded.renderAbi != current.renderAbi) differences.emplace_back("renderAbi");
  if (recorded.compilerRevision != current.compilerRevision)
    differences.emplace_back("compilerRevision");
  if (recorded.sampleRate != current.sampleRate) differences.emplace_back("sampleRate");
  if (recorded.scoreSha256 != current.scoreSha256) differences.emplace_back("scoreSha256");
  if (recorded.audioSha256 != current.audioSha256) differences.emplace_back("audioSha256");
  if (recorded.settingsDigest != current.settingsDigest)
    differences.emplace_back("settingsDigest");
  return differences;
}

std::string_view proceduralReviewDecisionName(ProceduralReviewDecisionKind kind) noexcept {
  switch (kind) {
    case ProceduralReviewDecisionKind::Accept: return "accept";
    case ProceduralReviewDecisionKind::Reject: return "reject";
  }
  return "reject";
}

bool ProceduralReviewReceipt::accepted() const noexcept {
  if (current.empty()) return false;
  // The most recent recorded decision decides. A rejection after an acceptance withdraws it, and a
  // rejection that stands alone is not approval.
  const ProceduralReviewDecision* latest = &current.front();
  for (const auto& decision : current) {
    if (decision.reviewedAtUtc > latest->reviewedAtUtc ||
        (decision.reviewedAtUtc == latest->reviewedAtUtc && decision.reviewId > latest->reviewId)) {
      latest = &decision;
    }
  }
  return latest->kind == ProceduralReviewDecisionKind::Accept;
}

bool ProceduralReviewReceipt::reject() const noexcept {
  if (current.empty()) return false;
  const ProceduralReviewDecision* latest = &current.front();
  for (const auto& decision : current) {
    if (decision.reviewedAtUtc > latest->reviewedAtUtc ||
        (decision.reviewedAtUtc == latest->reviewedAtUtc && decision.reviewId > latest->reviewId)) {
      latest = &decision;
    }
  }
  return latest->kind == ProceduralReviewDecisionKind::Reject;
}

core::Result<ProceduralReviewBasis> freezeProceduralReviewBasis(
    const ProceduralSingerManifest& manifest,
    std::string_view contentHash,
    std::string_view renderAbi,
    std::uint32_t compilerRevision,
    std::uint32_t sampleRate,
    const std::filesystem::path& scorePath,
    const std::filesystem::path& audioPath,
    std::string_view settingsDigest) {
  ProceduralReviewBasis basis;
  basis.resourceId = manifest.id;
  basis.version = manifest.version;
  basis.recipeSha256 = manifest.recipeSha256;
  basis.contentHash = std::string{contentHash};
  basis.engineId = manifest.engineId;
  basis.engineRevision = manifest.engineRevision;
  basis.renderAbi = std::string{renderAbi};
  basis.compilerRevision = compilerRevision;
  basis.sampleRate = sampleRate;
  basis.settingsDigest = std::string{settingsDigest};
  // The evidence digest is computed from the file rather than accepted from the caller, so a review
  // cannot claim evidence it was never performed against.
  const auto score = core::sha256File(scorePath, kMaximumEvidenceBytes);
  if (!score) return core::Result<ProceduralReviewBasis>{score.error()};
  basis.scoreSha256 = score.value();
  const auto audio = core::sha256File(audioPath, kMaximumEvidenceBytes);
  if (!audio) return core::Result<ProceduralReviewBasis>{audio.error()};
  basis.audioSha256 = audio.value();
  const auto valid = basis.validate();
  if (!valid) return core::Result<ProceduralReviewBasis>{valid.error()};
  return basis;
}

core::Result<ProceduralReviewReceipt> resolveProceduralReviewDecisions(
    const ProceduralReviewCandidate& candidate,
    const std::vector<ProceduralReviewDecision>& decisions) {
  const auto valid = candidate.basis.validate();
  if (!valid) return core::Result<ProceduralReviewReceipt>{valid.error()};
  const auto candidateDigest = candidate.basis.digest();
  if (!candidateDigest) return core::Result<ProceduralReviewReceipt>{candidateDigest.error()};
  ProceduralReviewReceipt receipt;
  receipt.candidateId = candidate.candidateId;
  receipt.basisDigest = candidateDigest.value();
  for (const auto& decision : decisions) {
    // A decision for another candidate is not this candidate's evidence and is ignored rather than
    // counted, so two resources never share a review by accident.
    if (!decision.candidateId.empty() && decision.candidateId != candidate.candidateId) continue;
    if (!isLowercaseSha256(decision.basisDigest)) {
      return core::failure<ProceduralReviewReceipt>(
          core::ErrorCode::InvalidArgument, "A stored procedural decision has a malformed basis",
          decision.reviewId);
    }
    if (decision.basisDigest == receipt.basisDigest) {
      receipt.current.push_back(decision);
      continue;
    }
    // The decision was about a different rendering of this resource. It is reported as stale with
    // the differing fields named, never silently inherited as an approval.
    ProceduralReviewedDecision stale;
    stale.decision = decision;
    if (decision.recordedBasis.has_value()) {
      // The recorded basis lets the report name the fields that changed, which is what makes a
      // stale decision actionable rather than merely refused.
      stale.differences = proceduralReviewBasisDifferences(decision.recordedBasis.value(),
                                                           candidate.basis);
    }
    if (stale.differences.empty()) stale.differences.emplace_back("basisDigest");
    receipt.stale.push_back(std::move(stale));
  }
  return receipt;
}

core::Result<void> recordProceduralReviewDecision(
    const ProceduralReviewCandidate& candidate,
    const ProceduralReviewDecision& decision) {
  const auto valid = candidate.basis.validate();
  if (!valid) return core::Result<void>{valid.error()};
  if (decision.reviewId.empty() || decision.reviewId.size() > 128U)
    return core::failure(core::ErrorCode::InvalidArgument,
                         "A procedural review decision needs a bounded review id");
  if (decision.reviewerId.empty() || decision.reviewerId.size() > 128U)
    return core::failure(core::ErrorCode::InvalidArgument,
                         "A procedural review decision needs a reviewer");
  if (decision.reviewedAtUtc.empty() || decision.reviewedAtUtc.size() > 40U)
    return core::failure(core::ErrorCode::InvalidArgument,
                         "A procedural review decision needs a review time");
  const auto candidateDigest = candidate.basis.digest();
  if (!candidateDigest) return core::Result<void>{candidateDigest.error()};
  // A carried basis must agree with the digest it was recorded under, or the decision would name
  // material it does not describe while appearing to carry it.
  if (decision.recordedBasis.has_value()) {
    const auto recordedDigest = decision.recordedBasis->digest();
    if (!recordedDigest) return core::Result<void>{recordedDigest.error()};
    if (recordedDigest.value() != decision.basisDigest)
      return core::failure(core::ErrorCode::Conflict,
                           "A procedural review decision carries a basis that does not match its "
                           "own digest",
                           decision.reviewId);
  }
  // The recorded digest must name the basis the reviewer actually saw. Anything else is a decision
  // being attached to material it does not describe.
  if (decision.basisDigest != candidateDigest.value())
    return core::failure(core::ErrorCode::Conflict,
                         "A procedural review decision does not match the candidate it names",
                         decision.reviewId);
  // Acceptance is a claim that evidence was examined, so a candidate without evidence cannot be
  // accepted. A rejection may be recorded without it.
  if (decision.kind == ProceduralReviewDecisionKind::Accept &&
      (candidate.basis.scoreSha256.empty() || candidate.basis.audioSha256.empty())) {
    return core::failure(core::ErrorCode::Conflict,
                         "A procedural candidate cannot be accepted without score and audio "
                         "evidence to review",
                         candidate.candidateId);
  }
  return core::success();
}

}  // namespace seam::distribution
