#include "seam/voicebank_production/project.hpp"
#include "seam/voicebank_production/source_assessment.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"

#include <algorithm>
#include <array>

namespace seam::voicebank_production {
namespace {
bool digest(std::string_view value) {
  return value.size() == 64U && std::all_of(value.begin(), value.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}
core::Result<void> executionPolicy(const SourceStrategyAssessment& strategy) {
  if (strategy.rights != Feasibility::Pass || !strategy.permissions.sourceUse || !strategy.permissions.transformation ||
      strategy.licenseLocator.empty() || !digest(strategy.licenseSha256))
    return core::failure(core::ErrorCode::Conflict, "Source execution requires recorded source-use and transformation authorization", strategy.id);
  return core::success();
}
core::Result<const TakeSourceBinding*> sourceForTake(const VoicebankProductionProject& project, std::string_view takeId) {
  const auto take = std::find_if(project.takes.begin(), project.takes.end(), [&](const auto& value) { return value.takeId == takeId; });
  if (take == project.takes.end()) return core::failure<const TakeSourceBinding*>(core::ErrorCode::NotFound, "Source-owned take is missing");
  if (take->sourceBindingId.empty()) return core::failure<const TakeSourceBinding*>(core::ErrorCode::Unsupported,
      "Legacy or unattributed take requires explicit source attribution before execution or qualification", take->takeId);
  const auto source = std::find_if(project.sourceBindings.begin(), project.sourceBindings.end(),
      [&](const auto& value) { return value.id == take->sourceBindingId; });
  if (source == project.sourceBindings.end() || source->takeId != take->takeId || source->rawAssetSha256 != take->rawAssetSha256)
    return core::failure<const TakeSourceBinding*>(core::ErrorCode::Conflict, "Take source binding differs from its immutable input", take->takeId);
  return &*source;
}
}  // namespace

std::string productionUnitIdentitySha256(const ProductionUnitIdentity& identity) {
  return core::sha256Hex(formats::stringifyJson(formats::JsonValue::Array{
      identity.language, identity.style, identity.coverageKey,
      static_cast<std::int64_t>(identity.pitchLayer)}, false));
}

core::Result<void> requireSelectedSourceExecution(const VoicebankProductionProject& project) {
  const auto strategy = std::find_if(project.sourceStrategies.begin(), project.sourceStrategies.end(),
      [&](const auto& value) { return value.id == project.selectedSourceStrategyId; });
  if (strategy == project.sourceStrategies.end())
    return core::failure(core::ErrorCode::Conflict, "Select an authorized source before importing or generating audio");
  const auto policy = executionPolicy(*strategy);
  if (!policy) return policy;
  const auto evidence = core::sha256File(strategy->licenseLocator, 4ULL * 1024ULL * 1024ULL);
  if (!evidence || evidence.value() != strategy->licenseSha256)
    return core::failure(core::ErrorCode::Conflict, "Selected source execution evidence is missing or changed", strategy->id);
  return core::success();
}

core::Result<void> requireTakeSourceExecution(const VoicebankProductionProject& project, std::string_view takeId) {
  const auto source = sourceForTake(project, takeId);
  if (!source) return core::Result<void>{source.error()};
  const auto captured = executionPolicy(source.value()->strategy);
  if (!captured) return captured;
  const auto current = std::find_if(project.sourceStrategies.begin(), project.sourceStrategies.end(),
      [&](const auto& value) { return value.id == source.value()->strategy.id; });
  if (current == project.sourceStrategies.end() || current->kind != source.value()->strategy.kind ||
      current->licenseSha256 != source.value()->strategy.licenseSha256)
    return core::failure(core::ErrorCode::Conflict, "Current source policy no longer identifies the take's captured source", std::string{takeId});
  return executionPolicy(*current);
}

core::Result<void> requireTakeSourceQualification(const VoicebankProductionProject& project, std::string_view takeId) {
  const auto executable = requireTakeSourceExecution(project, takeId);
  if (!executable) return executable;
  const auto source = sourceForTake(project, takeId);
  if (!source) return core::Result<void>{source.error()};
  const auto current = std::find_if(project.sourceStrategies.begin(), project.sourceStrategies.end(),
      [&](const auto& value) { return value.id == source.value()->strategy.id; });
  if (!source.value()->strategy.permissions.singingBankRedistribution || !source.value()->strategy.permissions.commercialRenders ||
      current->coverage != Feasibility::Pass || current->listening != Feasibility::Pass ||
      !current->permissions.singingBankRedistribution || !current->permissions.commercialRenders)
    return core::failure(core::ErrorCode::Conflict, "Take source is executable but lacks complete candidate qualification", std::string{takeId});
  return requireCurrentSourceQualityAssessment(project, source.value()->strategy.id);
}

void invalidateProductionQualification(VoicebankProductionProject& project) noexcept {
  if (project.lifecycle == ProductionLifecycle::Qualified) project.lifecycle = ProductionLifecycle::Experimental;
}

std::string toString(ProductionLifecycle value) {
  switch (value) {
    case ProductionLifecycle::LegacyUnclassified: return "LEGACY_UNCLASSIFIED";
    case ProductionLifecycle::Draft: return "DRAFT";
    case ProductionLifecycle::Experimental: return "EXPERIMENTAL";
    case ProductionLifecycle::Qualified: return "QUALIFIED";
  }
  return {};
}

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
  return value == "source-register" || value == "source-quality-assessment" || value == "create" || value == "import" || value == "transform" ||
         value == "marker" || value == "retake" || value == "review" ||
         value == "save" || value == "candidate-export" || value == "import-procedural" || value == "import-generated-batch";
}

bool selectedStrategyReady(
    const VoicebankProductionProject& project) noexcept {
  const auto found = std::find_if(
      project.sourceStrategies.begin(), project.sourceStrategies.end(),
      [&project](const SourceStrategyAssessment& strategy) {
        return strategy.id == project.selectedSourceStrategyId;
      });
  if (found == project.sourceStrategies.end()) return false;
  const bool ready = found->rights == Feasibility::Pass &&
         found->coverage == Feasibility::Pass &&
         found->listening == Feasibility::Pass &&
         found->permissions.sourceUse && found->permissions.transformation &&
         found->permissions.singingBankRedistribution &&
         found->permissions.commercialRenders &&
         !found->licenseLocator.empty() && !found->licenseSha256.empty();
  if (!ready) return false;
  // Legacy declarations remain unchanged. Once an assessment exists, UI and
  // legacy export consumers must not call stale assessed material ready.
  try { return static_cast<bool>(requireCurrentSourceQualityAssessment(project,found->id)); }
  catch (...) { return false; }
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
