#include "seam/application/editor_session.hpp"

#include <algorithm>
#include <unordered_set>

namespace seam::application {
namespace {

bool sameSelectedTakes(const domain::RegionPerformanceState& left,
                       const domain::RegionPerformanceState& right) {
  std::unordered_set<std::string_view> compared;
  for (const auto& selection : left.accepted) {
    if (!compared.insert(selection.takeId).second) continue;
    const auto source = std::find_if(left.takes.begin(), left.takes.end(),
        [&selection](const auto& take) { return take.id == selection.takeId; });
    const auto counterpart = std::find_if(right.takes.begin(), right.takes.end(),
        [&selection](const auto& take) { return take.id == selection.takeId; });
    if (source == left.takes.end() || counterpart == right.takes.end() || *counterpart != *source) return false;
  }
  return true;
}

bool sameRegion(const domain::VocalRegion& left, const domain::VocalRegion& right) {
  return left.id == right.id && left.startTick == right.startTick &&
      left.durationTick == right.durationTick && left.lyrics == right.lyrics &&
      left.notes == right.notes && left.phonemeOverrides == right.phonemeOverrides &&
      left.unitSelectionOverrides == right.unitSelectionOverrides &&
      left.seamOverrides == right.seamOverrides && left.pitchAutomation == right.pitchAutomation &&
      left.dynamicsAutomation == right.dynamicsAutomation &&
      left.performance.revision == right.performance.revision &&
      left.performance.pronunciation == right.performance.pronunciation &&
      left.performance.ownership == right.performance.ownership &&
      left.performance.accepted == right.performance.accepted &&
      sameSelectedTakes(left.performance, right.performance);
}

}

bool EditorSession::samePerformanceInputs(const domain::Project& left,
                                          const domain::Project& right) {
  if (left.id() != right.id() || left.tempoMap() != right.tempoMap() ||
      left.meterMap() != right.meterMap() ||
      left.settings().sampleRate != right.settings().sampleRate ||
      left.settings().hostStartOffsetTick != right.settings().hostStartOffsetTick ||
      left.vocalTracks().size() != right.vocalTracks().size()) return false;
  for (std::size_t index = 0U; index < left.vocalTracks().size(); ++index) {
    const auto& source = left.vocalTracks()[index];
    const auto& current = right.vocalTracks()[index];
    if (source.id != current.id || source.voicebank != current.voicebank || source.proceduralRecipe != current.proceduralRecipe ||
        source.styleSelection != current.styleSelection || source.regions.size() != current.regions.size()) {
      return false;
    }
    for (std::size_t region = 0U; region < source.regions.size(); ++region) {
      if (!sameRegion(source.regions[region], current.regions[region])) return false;
    }
  }
  return true;
}

core::Result<PerformanceJobContext> EditorSession::capturePerformanceJob() const {
  if (health_ != SessionHealth::Ready) {
    return core::failure<PerformanceJobContext>(core::ErrorCode::Conflict,
        "Performance generation requires a healthy editor session");
  }
  const auto valid = project_.validate();
  if (!valid) return core::Result<PerformanceJobContext>{valid.error()};
  return PerformanceJobContext{std::make_shared<const domain::Project>(project_),
      performanceGeneration_, std::make_shared<detail::PerformanceJobReceipt>()};
}

core::Result<void> EditorSession::validatePerformanceJob(const PerformanceJobContext& context) const {
  if (health_ != SessionHealth::Ready || !context.generation_ || context.generation_ != performanceGeneration_ ||
      !context.source_ || !context.receipt_ || context.receipt_->consumed) {
    return core::failure(core::ErrorCode::Conflict,
        "Performance result belongs to an expired, replaced or already consumed job context");
  }
  const auto valid = project_.validate();
  if (!valid) return valid;
  if (!samePerformanceInputs(*context.source_, project_)) {
    return core::failure(core::ErrorCode::Conflict,
        "Performance result no longer matches current musical inputs or resource intent");
  }
  return core::success();
}

core::Result<void> EditorSession::executePerformanceResult(
    const PerformanceJobContext& context, std::unique_ptr<ICommand> command) {
  const auto current = validatePerformanceJob(context);
  if (!current) return current;
  const auto result = execute(std::move(command));
  if (result) context.receipt_->consumed = true;
  return result;
}

}
