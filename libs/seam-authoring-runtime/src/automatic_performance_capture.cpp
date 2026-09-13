#include "seam/authoring/automatic_performance_capture.hpp"

#include "seam/phonemizer/language_resolver.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace seam::authoring {

core::Result<AutomaticPerformanceCapture> AutomaticPerformanceCapture::prepare(
    const application::EditorSession& session,domain::RegionId region,
    domain::PerformanceTimeRange range,std::vector<domain::PerformanceChannel> channels,
    domain::SingerResourceIdentity resource,std::uint64_t seed,std::string takeId) {
  using Output=AutomaticPerformanceCapture;
  if (!region.valid()) return core::failure<Output>(core::ErrorCode::InvalidArgument,
      "Automatic performance capture requires a region");
  const auto& project=session.project();
  const auto* selected=project.findRegion(region);
  if (selected==nullptr) return core::failure<Output>(core::ErrorCode::NotFound,
      "Automatic performance capture region is missing");
  if (channels.empty() || channels.size()>12U) return core::failure<Output>(
      core::ErrorCode::InvalidArgument,"Automatic performance channel set is invalid");
  if (takeId.empty() || takeId.size()>128U) return core::failure<Output>(
      core::ErrorCode::InvalidArgument,"Automatic performance take identity is invalid");
  if (range.endTick>selected->durationTick) return core::failure<Output>(
      core::ErrorCode::InvalidArgument,"Automatic performance range is outside the region");
  const auto validResource=resource.validate();
  if (!validResource) return core::Result<Output>{validResource.error()};
  auto context=session.capturePerformanceJob();
  if (!context) return core::Result<Output>{context.error()};
  return core::success(Output{std::move(context).value(),region,range,std::move(channels),
      std::move(resource),seed,std::move(takeId)});
}

bool AutomaticPerformanceCapture::matches(const application::EditorSession& session) const {
  return session.project()==context_.sourceProject() &&
      static_cast<bool>(session.validatePerformanceJob(context_));
}

core::Result<domain::PerformanceTake> AutomaticPerformanceCapture::generate(
    std::stop_token stop) const {
  using Output=domain::PerformanceTake;
  const auto& captured=context_.sourceProject();
  const auto* region=captured.findRegion(region_);
  if (region==nullptr) return core::failure<Output>(core::ErrorCode::NotFound,
      "Captured automatic performance region is missing");
  auto pronunciation=phonemizer::resolvePronunciation(*region,stop);
  if (!pronunciation) return core::Result<Output>{pronunciation.error()};
  synthesis::AutomaticPerformanceRequest request{
      .takeId=takeId_,
      .regionId=region_,
      .capturedRevision=region->performance.revision,
      .resource=resource_,
      .pronunciation=pronunciation.value().identity,
      .generatorId=std::string{synthesis::kPhraseAwareGeneratorId},
      .generatorVersion=std::string{synthesis::kPhraseAwareGeneratorVersion},
      .seed=seed_,
      .range=range_,
      .channels=channels_,
  };
  return synthesis::generatePhraseAwarePerformance(captured,*region,
      pronunciation.value(),std::move(request),stop);
}

core::Result<void> AutomaticPerformanceCapture::apply(
    const domain::PerformanceTake& take,application::EditorSession& session) const {
  if (!matches(session)) return core::failure(core::ErrorCode::Conflict,
      "Automatic performance capture no longer matches the session");
  const auto* current=session.project().findRegion(region_);
  if (current==nullptr) return core::failure(core::ErrorCode::NotFound,
      "Automatic performance region is missing");
  // The proposal must be this capture's own material. A take generated for another
  // region, revision, range or generator is refused rather than relabelled.
  if (take.sourceRegionId!=region_ || take.capturedRevision!=current->performance.revision ||
      take.range!=range_ || take.generatorId!=synthesis::kPhraseAwareGeneratorId ||
      take.generatorVersion!=synthesis::kPhraseAwareGeneratorVersion || take.seed!=seed_ ||
      take.id!=takeId_ || take.resource!=resource_)
    return core::failure(core::ErrorCode::Conflict,
        "Automatic performance proposal does not belong to this capture");
  return session.executePerformanceResult(context_,
      std::make_unique<application::AddPerformanceProposalCommand>(region_,
          current->performance,take));
}

}  // namespace seam::authoring
