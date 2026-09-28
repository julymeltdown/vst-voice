#include "review_transition_internal.hpp"

#include "candidate_publication_internal.hpp"

#include "seam/voicebank_production/candidate_publication.hpp"
#include "seam/voicebank_production/take_inspection_receipt.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace seam::voicebank_production::review_internal {
namespace {

using candidate_publication_internal::ReviewBasisIndex;
using candidate_publication_internal::reviewMetadata;
using TakeIds = std::set<std::string, std::less<>>;

core::Result<void> refuse(std::string message, std::string context = {}) {
  return core::failure(core::ErrorCode::Conflict, std::move(message), std::move(context));
}

int stateLevel(UnitQueueState state) noexcept {
  return state == UnitQueueState::Approved ? 2 : state == UnitQueueState::PitchReview ? 1 : 0;
}

// The review status each take holds: 0 unreviewed, 1 markers reviewed, 2 approved. A take's own
// state and, while it is active, its assignment's state and review flags all count.
std::map<std::string, int, std::less<>> reviewLevels(const VoicebankProductionProject& project) {
  std::map<std::string, int, std::less<>> levels;
  for (const auto& take : project.takes) levels[take.takeId] = stateLevel(take.state);
  for (const auto& assignment : project.unitAssignments) {
    if (assignment.takeId.empty()) continue;
    auto& level = levels[assignment.takeId];
    level = std::max({level, stateLevel(assignment.state),
                      assignment.markerReviewed || assignment.pitchReviewed ? 1 : 0});
  }
  return levels;
}

bool sameReview(const ReviewRecord& left, const ReviewRecord& right) {
  return left.reviewId == right.reviewId && left.takeId == right.takeId && left.reviewerId == right.reviewerId &&
         left.result == right.result && left.reviewedAtUtc == right.reviewedAtUtc;
}

bool sameDerived(const DerivedRevision& left, const DerivedRevision& right) {
  return left.revisionId == right.revisionId && left.inputSha256 == right.inputSha256 &&
         left.outputSha256 == right.outputSha256 && left.operation == right.operation &&
         left.operationVersion == right.operationVersion && left.parameters == right.parameters &&
         left.operatorId == right.operatorId && left.performedAtUtc == right.performedAtUtc;
}

bool sameAsset(const AssetRecord& left, const AssetRecord& right) {
  return left.sha256 == right.sha256 && left.relativePath == right.relativePath &&
         left.byteSize == right.byteSize && left.kind == right.kind;
}

// Everything a take's review basis reads from the take row itself; its state is review status.
bool sameTakeMaterial(const TakeRecord& left, const TakeRecord& right) {
  return left.takeId == right.takeId && left.promptId == right.promptId && left.coverageKey == right.coverageKey &&
         left.pitchLayer == right.pitchLayer && left.rawAssetSha256 == right.rawAssetSha256 &&
         left.derivedRevisionIds == right.derivedRevisionIds && left.supersedesTakeId == right.supersedesTakeId &&
         left.sourceBindingId == right.sourceBindingId && left.style == right.style;
}

template <typename Row, typename Same>
bool extends(const std::vector<Row>& before, const std::vector<Row>& after, Same same) {
  return after.size() >= before.size() && std::equal(before.begin(), before.end(), after.begin(), same);
}

core::Result<void> preserveHistory(const VoicebankProductionProject& current,
    const VoicebankProductionProject& proposed, bool allowStyleOwnershipTransition) {
  if (!extends(current.reviews, proposed.reviews, sameReview))
    return refuse("Review history is append-only; an earlier decision cannot be removed or rewritten");
  if (!extends(current.metadataRevisions, proposed.metadataRevisions, sameMetadataRevision))
    return refuse("Annotations, receipts and review material are append-only history");
  if (!extends(current.derivedRevisions, proposed.derivedRevisions, sameDerived))
    return refuse("Processing history is append-only; an earlier edit cannot be removed or rewritten");
  if (!extends(current.assets, proposed.assets, sameAsset))
    return refuse("Stored take audio is append-only");
  std::map<std::string_view, const TakeRecord*> next;
  for (const auto& take : proposed.takes) next.emplace(take.takeId, &take);
  for (const auto& take : current.takes) {
    const auto found = next.find(take.takeId);
    if (found == next.end()) return refuse("A take and its manual work cannot be removed", take.takeId);
    const auto& kept = *found->second;
    if (kept.promptId != take.promptId || kept.coverageKey != take.coverageKey || kept.pitchLayer != take.pitchLayer ||
        kept.rawAssetSha256 != take.rawAssetSha256 || kept.supersedesTakeId != take.supersedesTakeId ||
        kept.sourceBindingId != take.sourceBindingId || (!allowStyleOwnershipTransition && kept.style != take.style) ||
        kept.derivedRevisionIds.size() < take.derivedRevisionIds.size() ||
        !std::equal(take.derivedRevisionIds.begin(), take.derivedRevisionIds.end(), kept.derivedRevisionIds.begin()))
      return refuse("A take's identity, lineage and processing chain are kept; edits only extend them", take.takeId);
  }
  return core::success();
}

// Indexed facts about one generation that review checks need for many takes at once.
class Material final {
public:
  explicit Material(const VoicebankProductionProject& project) : basis_{project} {
    for (const auto& take : project.takes) takes_.emplace(take.takeId, &take);
    for (const auto& revision : project.derivedRevisions) derived_.emplace(revision.revisionId, &revision);
    for (const auto& review : project.reviews) latestReview_[review.takeId] = &review;
    for (const auto& revision : project.metadataRevisions)
      if (reviewMetadata(revision.kind)) latestDecisionMaterial_[revision.takeId] = &revision;
  }

  [[nodiscard]] const TakeRecord* take(std::string_view takeId) const {
    const auto found = takes_.find(takeId);
    return found == takes_.end() ? nullptr : found->second;
  }

  // The audio a take's review hears: its raw asset through its processing chain.
  [[nodiscard]] std::string audio(const TakeRecord& take) const {
    auto audio = take.rawAssetSha256;
    for (const auto& id : take.derivedRevisionIds) {
      const auto found = derived_.find(id);
      if (found == derived_.end() || found->second->inputSha256 != audio) return {};
      audio = found->second->outputSha256;
    }
    return audio;
  }

  [[nodiscard]] std::string basis(std::string_view takeId) const { return basis_.basis(takeId); }

  // A take's approval still applies when its latest decision is PASS and the material that
  // decision recorded is this generation's audio and review basis.
  [[nodiscard]] bool approvalApplies(const TakeRecord& take) const {
    const auto review = latestReview_.find(take.takeId);
    const auto recorded = latestDecisionMaterial_.find(take.takeId);
    if (review == latestReview_.end() || recorded == latestDecisionMaterial_.end() ||
        review->second->result != "PASS" || recorded->second->kind != kSampleCandidateReviewKind)
      return false;
    const auto& values = recorded->second->values;
    const auto value = [&](std::string_view key) {
      const auto found = values.find(key);
      return found == values.end() ? std::string{} : found->second;
    };
    return value("reviewId") == review->second->reviewId && value("audioSha256") == audio(take) &&
           value("reviewBasisSha256") == basis(take.takeId);
  }

private:
  ReviewBasisIndex basis_;
  std::map<std::string_view, const TakeRecord*, std::less<>> takes_;
  std::map<std::string_view, const DerivedRevision*, std::less<>> derived_;
  std::map<std::string_view, const ReviewRecord*, std::less<>> latestReview_;
  std::map<std::string_view, const MetadataRevision*, std::less<>> latestDecisionMaterial_;
};

// Independence from the take's own material, as far as the producer itself records it: the
// importer, whoever processed its audio, and whoever authored its annotations or receipts.
// Candidate publication additionally checks the journal attribution of the original import.
core::Result<void> independentReviewer(const VoicebankProductionProject& project, const TakeRecord& take,
                                       std::string_view reviewerId) {
  if (std::none_of(project.operators.begin(), project.operators.end(), [&](const auto& value) {
        return value.operatorId == reviewerId && value.role == "REVIEWER";
      }))
    return refuse("A review decision needs a registered reviewer", take.takeId);
  const auto binding = std::find_if(project.sourceBindings.begin(), project.sourceBindings.end(),
      [&](const auto& value) { return value.id == take.sourceBindingId; });
  if (!take.sourceBindingId.empty() && binding != project.sourceBindings.end() && binding->importerId == reviewerId)
    return refuse("Whoever imported a take cannot independently review it", take.takeId);
  for (const auto& revision : project.derivedRevisions)
    if (revision.operatorId == reviewerId &&
        std::find(take.derivedRevisionIds.begin(), take.derivedRevisionIds.end(), revision.revisionId) !=
            take.derivedRevisionIds.end())
      return refuse("Whoever processed a take's audio cannot independently review it", take.takeId);
  for (const auto& revision : project.metadataRevisions)
    if (revision.takeId == take.takeId && !reviewMetadata(revision.kind) && revision.operatorId == reviewerId)
      return refuse("Whoever annotated or inspected a take cannot independently review it", take.takeId);
  return core::success();
}

// Each decision appended by a review event must be independent, recorded once per take with its
// material, bound to this generation's audio and basis, and set exactly its own take's state.
core::Result<void> checkDecisions(const VoicebankProductionProject& current,
    const VoicebankProductionProject& proposed, const Material& material, const TakeIds& granted) {
  std::map<std::string_view, const ReviewRecord*, std::less<>> decisions;
  for (auto row = current.reviews.size(); row < proposed.reviews.size(); ++row) {
    const auto& review = proposed.reviews[row];
    if (!decisions.emplace(review.takeId, &review).second)
      return refuse("A review decides each take at most once", review.takeId);
  }
  std::map<std::string_view, const MetadataRevision*, std::less<>> recorded;
  for (auto row = current.metadataRevisions.size(); row < proposed.metadataRevisions.size(); ++row) {
    const auto& revision = proposed.metadataRevisions[row];
    if (!reviewMetadata(revision.kind)) continue;
    if (revision.kind != kSampleCandidateReviewKind || !recorded.emplace(revision.takeId, &revision).second)
      return refuse("Review material is current and recorded once for each decided take", revision.takeId);
  }
  if (decisions.empty() || recorded.size() != decisions.size())
    return refuse("A review event records each decision together with the material it was made on");
  static const std::set<std::string, std::less<>> kValues{
      "unitId", "reviewId", "audioSha256", "unitManifestSha256", "reviewBasisSha256"};
  for (const auto& [takeId, review] : decisions) {
    const auto* take = material.take(takeId);
    const auto found = recorded.find(takeId);
    if (take == nullptr || found == recorded.end() || review->reviewId.empty() ||
        (review->result != "PASS" && review->result != "REJECTED"))
      return refuse("A review decision names an existing take, its material and PASS or REJECTED", std::string{takeId});
    const auto independent = independentReviewer(proposed, *take, review->reviewerId);
    if (!independent) return independent;
    const bool pass = review->result == "PASS";
    // An approval admits the take's stored bytes, so the reviewer must have had their current
    // automated inspection. A rejection admits nothing and needs none.
    if (pass && std::none_of(proposed.metadataRevisions.begin(), proposed.metadataRevisions.end(), [&](const auto& revision) {
          return revision.takeId == takeId && revision.kind == kTakeInspectionRevisionKind &&
                 revision.rawAssetSha256 == take->rawAssetSha256;
        }))
      return refuse("An approval needs the take's current take-inspection.v2 receipt for its stored bytes",
                    std::string{takeId});
    const auto& values = found->second->values;
    std::set<std::string, std::less<>> keys;
    for (const auto& entry : values) keys.insert(entry.first);
    if (keys != kValues || values.at("reviewId") != review->reviewId ||
        found->second->operatorId != review->reviewerId || found->second->performedAtUtc != review->reviewedAtUtc ||
        found->second->rawAssetSha256 != take->rawAssetSha256 || values.at("audioSha256") != material.audio(*take) ||
        values.at("reviewBasisSha256") != material.basis(takeId))
      return refuse("Review material does not describe this take's current audio and review basis", std::string{takeId});
    const auto assignment = std::find_if(proposed.unitAssignments.begin(), proposed.unitAssignments.end(),
        [&](const auto& value) { return value.takeId == takeId; });
    const auto decided = pass ? UnitQueueState::Approved : UnitQueueState::Rejected;
    if (assignment == proposed.unitAssignments.end() || take->state != decided || assignment->state != decided ||
        assignment->markerReviewed != pass || assignment->pitchReviewed != pass)
      return refuse("A review decision sets exactly the review state of its own active take", std::string{takeId});
  }
  for (const auto& takeId : granted) {
    const auto decision = decisions.find(takeId);
    if (decision == decisions.end() || decision->second->result != "PASS")
      return refuse("Only an independent PASS review can approve a take or mark it reviewed", takeId);
  }
  return core::success();
}

using AssignmentMaterial = std::tuple<std::string, std::int32_t, std::string, std::string, std::string>;

std::map<std::string, std::vector<AssignmentMaterial>, std::less<>> assignmentsByTake(
    const VoicebankProductionProject& project) {
  std::map<std::string, std::vector<AssignmentMaterial>, std::less<>> rows;
  for (const auto& value : project.unitAssignments)
    if (!value.takeId.empty())
      rows[value.takeId].emplace_back(value.coverageKey, value.pitchLayer, value.promptId, value.plannedTakeId, value.style);
  return rows;
}

// The takes whose review basis may differ between the two generations; nothing means every take.
// The basis keeps the producer's identity, inventory and source policy, so a change there
// reaches every take, while row changes reach only the takes they belong to.
std::optional<TakeIds> affectedTakes(const VoicebankProductionProject& current,
                                     const VoicebankProductionProject& proposed) {
  if (current.schemaVersion != proposed.schemaVersion || current.projectId != proposed.projectId ||
      current.inventoryId != proposed.inventoryId || current.inventorySha256 != proposed.inventorySha256 ||
      current.selectedSourceStrategyId != proposed.selectedSourceStrategyId ||
      current.licenseLocator != proposed.licenseLocator || current.licenseSha256 != proposed.licenseSha256 ||
      current.immutableAssetRoot != proposed.immutableAssetRoot || current.language != proposed.language ||
      ((current.schemaVersion < 2 || proposed.schemaVersion < 2) && current.lifecycle != proposed.lifecycle) ||
      current.sourceStrategies != proposed.sourceStrategies ||
      current.sourceQualityAssessments != proposed.sourceQualityAssessments)
    return std::nullopt;
  TakeIds affected;
  std::map<std::string_view, const TakeRecord*> before;
  for (const auto& take : current.takes) before.emplace(take.takeId, &take);
  for (const auto& take : proposed.takes) {
    const auto found = before.find(take.takeId);
    if (found == before.end() || !sameTakeMaterial(*found->second, take)) affected.insert(take.takeId);
  }
  const auto was = assignmentsByTake(current), now = assignmentsByTake(proposed);
  for (const auto& [takeId, rows] : now) {
    const auto found = was.find(takeId);
    if (found == was.end() || found->second != rows) affected.insert(takeId);
  }
  for (const auto& [takeId, rows] : was)
    if (!now.contains(takeId)) affected.insert(takeId);
  for (auto row = current.metadataRevisions.size(); row < proposed.metadataRevisions.size(); ++row)
    if (!reviewMetadata(proposed.metadataRevisions[row].kind)) affected.insert(proposed.metadataRevisions[row].takeId);
  return affected;
}

void lowerToMarkerReview(VoicebankProductionProject& project, std::string_view takeId) {
  for (auto& take : project.takes)
    if (take.takeId == takeId && take.state != UnitQueueState::Retake) take.state = UnitQueueState::MarkerReview;
  for (auto& assignment : project.unitAssignments) {
    if (assignment.takeId != takeId) continue;
    assignment.state = UnitQueueState::MarkerReview;
    assignment.markerReviewed = false;
    assignment.pitchReviewed = false;
  }
  invalidateProductionQualification(project);
}

}  // namespace

core::Result<void> applyReviewTransition(const VoicebankProductionProject* current,
    VoicebankProductionProject& proposed, const ProductionJournalEvent& event, bool allowStyleOwnershipTransition) {
  for (const auto& assignment : proposed.unitAssignments)
    if (assignment.takeId.empty() && (assignment.markerReviewed || assignment.pitchReviewed ||
                                      stateLevel(assignment.state) > 0))
      return refuse("Review status belongs to a take; an empty assignment cannot be reviewed");
  const auto after = reviewLevels(proposed);
  if (current == nullptr) {
    if (!proposed.reviews.empty() ||
        std::any_of(proposed.metadataRevisions.begin(), proposed.metadataRevisions.end(),
                    [](const auto& value) { return reviewMetadata(value.kind); }) ||
        std::any_of(after.begin(), after.end(), [](const auto& entry) { return entry.second > 0; }))
      return refuse("A new producer starts unreviewed; review decisions are recorded on its takes later");
    return core::success();
  }
  const auto history = preserveHistory(*current, proposed, allowStyleOwnershipTransition);
  if (!history) return history;
  const auto before = reviewLevels(*current);
  TakeIds granted;
  for (const auto& [takeId, level] : after) {
    const auto found = before.find(takeId);
    if (level > (found == before.end() ? 0 : found->second)) granted.insert(takeId);
  }
  const bool appendsDecisions = proposed.reviews.size() != current->reviews.size() ||
      std::any_of(proposed.metadataRevisions.begin() + static_cast<std::ptrdiff_t>(current->metadataRevisions.size()),
                  proposed.metadataRevisions.end(), [](const auto& value) { return reviewMetadata(value.kind); });
  const Material material{proposed};
  if (event.action == "review") {
    const auto decided = checkDecisions(*current, proposed, material, granted);
    if (!decided) return decided;
  } else if (appendsDecisions) {
    return refuse("Reviews and their material are recorded only by an independent review decision");
  } else if (!granted.empty()) {
    return refuse("Only an independent review can approve a take or mark it reviewed", *granted.begin());
  }
  // Approvals carried forward must still apply to their take's current material.
  const auto affected = affectedTakes(*current, proposed);
  std::vector<std::string> stale;
  for (const auto& [takeId, level] : after) {
    if (level == 0 || granted.contains(takeId) || (affected && !affected->contains(takeId))) continue;
    const auto* take = material.take(takeId);
    if (take == nullptr || !material.approvalApplies(*take)) stale.push_back(takeId);
  }
  for (const auto& takeId : stale) lowerToMarkerReview(proposed, takeId);
  return core::success();
}

}  // namespace seam::voicebank_production::review_internal
