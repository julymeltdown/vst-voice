#include "seam/application/tempo_commands.hpp"

namespace seam::application {
namespace {
template<class Map>
core::Result<void> restore(Map& current, const std::optional<Map>& expected,
                           const std::optional<Map>& replacement) {
  if (!expected || !replacement || current != *expected)
    return core::failure(core::ErrorCode::Conflict, "Tempo or meter history no longer matches the project");
  current = *replacement;
  return core::success();
}
}
core::Result<void> EditTempoCommand::apply(domain::Project& project) {
  if (after_) return restore(project.tempoMap(), before_, after_);
  auto candidate = project.tempoMap();
  const auto result = bpm_ ? candidate.addOrReplace(tick_, *bpm_) : candidate.remove(tick_);
  if (!result) return result;
  before_ = project.tempoMap(); after_ = std::move(candidate);
  return restore(project.tempoMap(), before_, after_);
}
core::Result<void> EditTempoCommand::revert(domain::Project& project) {
  return restore(project.tempoMap(), after_, before_);
}
core::Result<void> EditMeterCommand::apply(domain::Project& project) {
  if (after_) return restore(project.meterMap(), before_, after_);
  auto candidate = project.meterMap();
  const auto result = signature_ ? candidate.addOrReplace(tick_, signature_->numerator, signature_->denominator)
                                 : candidate.remove(tick_);
  if (!result) return result;
  before_ = project.meterMap(); after_ = std::move(candidate);
  return restore(project.meterMap(), before_, after_);
}
core::Result<void> EditMeterCommand::revert(domain::Project& project) {
  return restore(project.meterMap(), after_, before_);
}
}
