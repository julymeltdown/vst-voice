#include "seam/voicebank_production/project.hpp"

#include <algorithm>
#include <array>

namespace seam::voicebank_production {

ProductionQueueSummary summarizeQueues(
    const VoicebankProductionProject& project) noexcept {
  ProductionQueueSummary summary;
  for (const auto& assignment : project.unitAssignments) {
    switch (assignment.state) {
      case UnitQueueState::Missing: ++summary.missing; break;
      case UnitQueueState::Rejected: ++summary.rejected; break;
      case UnitQueueState::Retake: ++summary.retake; break;
      case UnitQueueState::MarkerReview: ++summary.markerReview; break;
      case UnitQueueState::PitchReview: ++summary.pitchReview; break;
      case UnitQueueState::Approved: ++summary.approved; break;
    }
  }
  return summary;
}

bool isProductionUtcTimestamp(std::string_view value) noexcept {
  if (value.size() != 20U || value.back() != 'Z') return false;
  constexpr std::array<std::size_t, 5U> positions{4U, 7U, 10U, 13U, 16U};
  constexpr std::array<char, 5U> separators{'-', '-', 'T', ':', ':'};
  for (std::size_t index = 0U; index < positions.size(); ++index) {
    if (value[positions[index]] != separators[index]) return false;
  }
  for (std::size_t index = 0U; index + 1U < value.size(); ++index) {
    if (std::find(positions.begin(), positions.end(), index) !=
        positions.end()) {
      continue;
    }
    if (value[index] < '0' || value[index] > '9') return false;
  }
  return true;
}

bool isProductionJournalAction(std::string_view value) noexcept {
  return value == "create" || value == "import" || value == "transform" ||
         value == "marker" || value == "retake" || value == "review" ||
         value == "save" || value == "candidate-export";
}

bool selectedStrategyReady(
    const VoicebankProductionProject& project) noexcept {
  const auto found = std::find_if(
      project.sourceStrategies.begin(), project.sourceStrategies.end(),
      [&project](const SourceStrategyAssessment& strategy) {
        return strategy.id == project.selectedSourceStrategyId;
      });
  if (found == project.sourceStrategies.end()) return false;
  return found->rights == Feasibility::Pass &&
         found->coverage == Feasibility::Pass &&
         found->listening == Feasibility::Pass &&
         found->permissions.sourceUse && found->permissions.transformation &&
         found->permissions.singingBankRedistribution &&
         found->permissions.commercialRenders &&
         !found->licenseLocator.empty() && !found->licenseSha256.empty();
}

std::string toString(SourceStrategyKind value) {
  switch (value) {
    case SourceStrategyKind::HumanRecording: return "HUMAN_RECORDING";
    case SourceStrategyKind::ProceduralSynthesis: return "PROCEDURAL_SYNTHESIS";
    case SourceStrategyKind::TtsDerived: return "TTS_DERIVED";
  }
  return {};
}

std::string toString(Feasibility value) {
  switch (value) {
    case Feasibility::Pass: return "PASS";
    case Feasibility::Blocked: return "BLOCKED";
    case Feasibility::NotAssessed: return "NOT_ASSESSED";
  }
  return {};
}

std::string toString(AssetKind value) {
  switch (value) {
    case AssetKind::Raw: return "RAW";
    case AssetKind::Derived: return "DERIVED";
  }
  return {};
}

std::string toString(UnitQueueState value) {
  switch (value) {
    case UnitQueueState::Missing: return "MISSING";
    case UnitQueueState::Rejected: return "REJECTED";
    case UnitQueueState::Retake: return "RETAKE";
    case UnitQueueState::MarkerReview: return "MARKER_REVIEW";
    case UnitQueueState::PitchReview: return "PITCH_REVIEW";
    case UnitQueueState::Approved: return "APPROVED";
  }
  return {};
}

std::string toString(OperationKind value) {
  switch (value) {
    case OperationKind::ChannelSelect: return "CHANNEL_SELECT";
    case OperationKind::Downmix: return "DOWNMIX";
    case OperationKind::Resample: return "RESAMPLE";
    case OperationKind::RemoveDc: return "REMOVE_DC";
    case OperationKind::NormalizeGain: return "NORMALIZE_GAIN";
    case OperationKind::Trim: return "TRIM";
    case OperationKind::Segment: return "SEGMENT";
  }
  return {};
}

}
