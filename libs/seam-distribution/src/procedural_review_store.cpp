#include "seam/distribution/procedural_review_store.hpp"

#include "seam/core/exclusive_file_lock.hpp"
#include "seam/core/file_io.hpp"
#include "seam/formats/json_value.hpp"

#include <algorithm>
#include <cctype>

namespace seam::distribution {
namespace {

using seam::formats::JsonValue;
using Object = JsonValue::Object;
using Array = JsonValue::Array;

constexpr std::string_view kStoreFormatId = "com.project-seam.procedural-review-store";
constexpr std::int32_t kStoreSchemaVersion = 1;
constexpr std::uint64_t kMaximumStoreBytes = 16ULL * 1024ULL * 1024ULL;

// A basis digest is lowercase SHA-256 hex. A stored value of any other shape cannot describe a basis.
bool isLowercaseSha256Hex(std::string_view value) noexcept {
  if (value.size() != 64U) return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char character) {
    return std::isdigit(character) != 0 || (character >= 'a' && character <= 'f');
  });
}

bool boundedText(std::string_view value, std::size_t maximum) noexcept {
  if (value.empty() || value.size() > maximum) return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char character) {
    return character >= 0x20 && character != 0x7F;
  });
}

core::Result<std::string> requiredString(const JsonValue& root, std::string_view field,
                                         std::size_t maximum) {
  const auto* value = root.find(field);
  if (value == nullptr || !value->isString() || !boundedText(value->asString(), maximum)) {
    return core::failure<std::string>(core::ErrorCode::ParseError,
                                      "Procedural review store field is missing or invalid",
                                      std::string{field});
  }
  return value->asString();
}

}  // namespace

ProceduralReviewStore::ProceduralReviewStore(std::filesystem::path statePath,
                                             std::size_t maximumDecisions)
    : statePath_(std::move(statePath)), maximumDecisions_(maximumDecisions) {}

core::Result<ProceduralReviewStore> ProceduralReviewStore::open(std::filesystem::path statePath,
                                                                std::size_t maximumDecisions) {
  using Output = ProceduralReviewStore;
  if (statePath.empty())
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
                                 "A procedural review store needs a state path");
  if (maximumDecisions == 0U || maximumDecisions > 65536U)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
                                 "Procedural review store bound is invalid");
  std::error_code error;
  auto absolute = std::filesystem::absolute(statePath, error);
  if (error)
    return core::failure<Output>(core::ErrorCode::IoError,
                                 "Unable to resolve the review store path", error.message());
  ProceduralReviewStore store((error ? absolute : std::filesystem::weakly_canonical(absolute, error))
                                  .lexically_normal(),
                              maximumDecisions);
  // An existing file must be readable and well formed before the store is handed out. A corrupt
  // store is reported rather than silently replaced, because it is the only record of what a
  // reviewer approved.
  std::error_code existsError;
  if (std::filesystem::is_regular_file(store.statePath_, existsError)) {
    auto loaded = store.load();
    if (!loaded) return core::Result<Output>{loaded.error()};
  }
  return store;
}

core::Result<std::vector<ProceduralReviewDecision>> ProceduralReviewStore::load() const {
  using Output = std::vector<ProceduralReviewDecision>;
  std::error_code error;
  if (!std::filesystem::exists(statePath_, error)) return Output{};
  auto text = core::readTextFileLimited(statePath_, kMaximumStoreBytes);
  if (!text) return core::Result<Output>{text.error()};
  auto parsed = formats::parseJson(text.value(), formats::JsonParseLimits{
      .maximumInputBytes = kMaximumStoreBytes,
      .maximumDepth = 8U,
      .maximumNodes = 1U << 20U,
      .maximumStringBytes = 64U * 1024U,
      .maximumCollectionEntries = 1U << 20U,
  });
  if (!parsed) return core::Result<Output>{parsed.error()};
  if (!parsed.value().isObject())
    return core::failure<Output>(core::ErrorCode::ParseError,
                                 "Procedural review store root must be an object");
  const auto& root = parsed.value();
  const auto* formatId = root.find("formatId");
  const auto* schema = root.find("schemaVersion");
  if (formatId == nullptr || !formatId->isString() ||
      formatId->asString() != kStoreFormatId)
    return core::failure<Output>(core::ErrorCode::Unsupported,
                                 "Unsupported procedural review store format");
  if (schema == nullptr || !schema->isNumber() || schema->asInt64() != kStoreSchemaVersion)
    return core::failure<Output>(core::ErrorCode::Unsupported,
                                 "Unsupported procedural review store schema");
  const auto* decisions = root.find("decisions");
  if (decisions == nullptr || !decisions->isArray() ||
      decisions->asArray().size() > maximumDecisions_)
    return core::failure<Output>(core::ErrorCode::ParseError,
                                 "Procedural review store decisions are invalid");
  std::vector<ProceduralReviewDecision> result;
  result.reserve(decisions->asArray().size());
  for (const auto& entry : decisions->asArray()) {
    if (!entry.isObject())
      return core::failure<Output>(core::ErrorCode::ParseError,
                                   "A stored procedural review decision is not an object");
    ProceduralReviewDecision decision;
    auto reviewId = requiredString(entry, "reviewId", 128U);
    auto candidateId = requiredString(entry, "candidateId", 128U);
    auto basisDigest = requiredString(entry, "basisDigest", 64U);
    auto reviewerId = requiredString(entry, "reviewerId", 128U);
    auto reviewedAtUtc = requiredString(entry, "reviewedAtUtc", 40U);
    if (!reviewId) return core::Result<Output>{reviewId.error()};
    if (!candidateId) return core::Result<Output>{candidateId.error()};
    if (!basisDigest) return core::Result<Output>{basisDigest.error()};
    if (!reviewerId) return core::Result<Output>{reviewerId.error()};
    if (!reviewedAtUtc) return core::Result<Output>{reviewedAtUtc.error()};
    if (!isLowercaseSha256Hex(basisDigest.value()))
      return core::failure<Output>(core::ErrorCode::ParseError,
                                   "A stored procedural review basis digest is malformed",
                                   reviewId.value());
    // The stored kind is the whole decision, so an unrecognised value is refused instead of being
    // read as a rejection, which would silently discard an approval.
    const auto* kind = entry.find("kind");
    if (kind == nullptr || !kind->isString() ||
        (kind->asString() != "accept" && kind->asString() != "reject"))
      return core::failure<Output>(core::ErrorCode::ParseError,
                                   "A stored procedural review decision has an unknown kind",
                                   reviewId.value());
    decision.reviewId = reviewId.value();
    decision.candidateId = candidateId.value();
    decision.basisDigest = basisDigest.value();
    decision.reviewerId = reviewerId.value();
    decision.reviewedAtUtc = reviewedAtUtc.value();
    decision.kind = kind->asString() == "accept" ? ProceduralReviewDecisionKind::Accept
                                                  : ProceduralReviewDecisionKind::Reject;
    const auto* note = entry.find("note");
    if (note != nullptr) {
      if (!note->isString() || note->asString().size() > 4096U)
        return core::failure<Output>(core::ErrorCode::ParseError,
                                     "A stored procedural review note is invalid",
                                     reviewId.value());
      decision.note = note->asString();
    }
    // The basis a decision was made about is persisted, because resolving a stored approval after a
    // restart otherwise has nothing to compare against. A decision without its basis could only be
    // trusted on its own word, which is exactly what this store refuses to do.
    const auto* basis = entry.find("basis");
    if (basis == nullptr || !basis->isObject())
      return core::failure<Output>(core::ErrorCode::ParseError,
                                   "A stored procedural review decision carries no basis",
                                   reviewId.value());
    const auto encoded = formats::stringifyJson(*basis, true);
    auto decoded = ProceduralReviewBasisJsonCodec{}.decode(encoded);
    if (!decoded) return core::Result<Output>{decoded.error()};
    decision.recordedBasis = decoded.value();
    result.push_back(std::move(decision));
  }
  return result;
}

core::Result<void> ProceduralReviewStore::persist(
    const std::vector<ProceduralReviewDecision>& decisions) const {
  Array entries;
  for (const auto& decision : decisions) {
    // A decision without the basis it was made about could not be resolved after a restart, and
    // trusting it on its own word is exactly what this store refuses to do.
    if (!decision.recordedBasis.has_value())
      return core::failure(core::ErrorCode::InvalidArgument,
                           "A stored procedural review decision must carry its basis",
                           decision.reviewId);
    const auto encodedBasis =
        ProceduralReviewBasisJsonCodec{}.encode(decision.recordedBasis.value());
    if (!encodedBasis) return core::Result<void>{encodedBasis.error()};
    auto basisValue = formats::parseJson(encodedBasis.value(), formats::JsonParseLimits{});
    if (!basisValue) return core::Result<void>{basisValue.error()};
    entries.emplace_back(Object{
        {"reviewId", JsonValue{decision.reviewId}},
        {"candidateId", JsonValue{decision.candidateId}},
        {"basisDigest", JsonValue{decision.basisDigest}},
        {"kind", JsonValue{std::string{proceduralReviewDecisionName(decision.kind)}}},
        {"reviewerId", JsonValue{decision.reviewerId}},
        {"reviewedAtUtc", JsonValue{decision.reviewedAtUtc}},
        {"note", JsonValue{decision.note}},
        {"basis", std::move(basisValue).value()},
    });
  }
  const auto text = formats::stringifyJson(JsonValue{Object{
      {"formatId", JsonValue{std::string{kStoreFormatId}}},
      {"schemaVersion", JsonValue{static_cast<std::int64_t>(kStoreSchemaVersion)}},
      {"decisions", JsonValue{std::move(entries)}},
  }}, true);
  std::error_code error;
  const auto parent = statePath_.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, error);
    if (error)
      return core::failure(core::ErrorCode::IoError,
                           "Unable to create the review store directory", error.message());
  }
  // Decisions are durable and append-only; an atomic write means a crash cannot leave a half-written
  // approval record in place of the previous one.
  return core::durableAtomicWriteText(statePath_, text);
}

core::Result<void> ProceduralReviewStore::record(const ProceduralReviewCandidate& candidate,
                                                 const ProceduralReviewDecision& decision) {
  // The same checks the library applies, so a stored decision is one that could have been recorded.
  const auto valid = recordProceduralReviewDecision(candidate, decision);
  if (!valid) return valid;
  // A read-modify-write of a shared document must not interleave with another writer, or a decision
  // can be silently lost. The lock is held across the load and the atomic publish, and it is a real
  // OS lock so a second process is excluded as well as a second caller.
  // The lock lives beside the store, so its directory must exist before the lock can be created.
  {
    std::error_code directoryError;
    const auto parent = statePath_.parent_path();
    if (!parent.empty()) {
      std::filesystem::create_directories(parent, directoryError);
      if (directoryError)
        return core::failure(core::ErrorCode::IoError,
                             "Unable to create the review store directory",
                             directoryError.message());
    }
  }
  core::ExclusiveFileLock lock;
  const auto locked = lock.acquire(statePath_.string() + ".lock");
  if (!locked) return core::Result<void>{locked.error()};
  auto decisions = load();
  if (!decisions) return core::Result<void>{decisions.error()};
  // A review id is the identity of one decision, so a duplicate is refused rather than appended.
  const auto duplicate = std::find_if(decisions.value().begin(), decisions.value().end(),
                                      [&decision](const ProceduralReviewDecision& existing) {
                                        return existing.reviewId == decision.reviewId;
                                      });
  if (duplicate != decisions.value().end())
    return core::failure(core::ErrorCode::Conflict,
                         "A procedural review decision with this id already exists",
                         decision.reviewId);
  if (decisions.value().size() >= maximumDecisions_)
    return core::failure(core::ErrorCode::Unsupported,
                         "The procedural review store is full",
                         std::to_string(maximumDecisions_));
  decisions.value().push_back(decision);
  return persist(decisions.value());
}

core::Result<std::vector<ProceduralReviewDecision>> ProceduralReviewStore::decisionsFor(
    std::string_view candidateId) const {
  auto decisions = load();
  if (!decisions) return decisions;
  std::vector<ProceduralReviewDecision> result;
  for (auto& decision : decisions.value()) {
    if (decision.candidateId == candidateId) result.push_back(std::move(decision));
  }
  return result;
}

core::Result<ProceduralReviewReceipt> ProceduralReviewStore::resolve(
    const ProceduralReviewCandidate& candidate) const {
  auto decisions = decisionsFor(candidate.candidateId);
  if (!decisions) return core::Result<ProceduralReviewReceipt>{decisions.error()};
  return resolveProceduralReviewDecisions(candidate, decisions.value());
}

std::size_t ProceduralReviewStore::size() const {
  auto decisions = load();
  return decisions ? decisions.value().size() : 0U;
}

}  // namespace seam::distribution

