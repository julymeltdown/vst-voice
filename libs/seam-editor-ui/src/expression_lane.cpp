#include "seam/ui/expression_lane.hpp"

#include "seam/application/performance_commands.hpp"
#include "seam/domain/airiness_automation.hpp"
#include "seam/domain/breathiness_automation.hpp"
#include "seam/domain/formant_automation.hpp"
#include "seam/domain/gender_automation.hpp"
#include "seam/domain/growl_automation.hpp"
#include "seam/domain/tension_automation.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>

namespace seam::ui {
namespace {

// The persisted bound of each channel is the domain's own constant, so a surface cannot widen a
// channel's range merely by drawing it.
constexpr ExpressionChannelDescriptor kDescriptors[kExpressionChannelCount] = {
    {ExpressionChannel::Formant, "formant", "Formant", "semitones",
     -domain::kMaximumFormantShiftSemitones, domain::kMaximumFormantShiftSemitones, 0.0F, 1.0F,
     true, synthesis::RendererControl::Formant},
    {ExpressionChannel::Breathiness, "breathiness", "Breathiness", "normalized share", 0.0F,
     domain::kMaximumBreathiness, 0.0F, 0.1F, false, synthesis::RendererControl::Breathiness},
    {ExpressionChannel::Tension, "tension", "Tension", "normalized share", 0.0F,
     domain::kMaximumTension, 0.0F, 0.1F, false, synthesis::RendererControl::Tension},
    {ExpressionChannel::Airiness, "airiness", "Airiness", "normalized share", 0.0F,
     domain::kMaximumAiriness, 0.0F, 0.1F, false, synthesis::RendererControl::Airiness},
    {ExpressionChannel::Gender, "gender", "Gender", "bipolar", -domain::kMaximumGender,
     domain::kMaximumGender, 0.0F, 0.1F, true, synthesis::RendererControl::Gender},
    {ExpressionChannel::Growl, "growl", "Growl", "normalized share", 0.0F, domain::kMaximumGrowl,
     0.0F, 0.1F, false, synthesis::RendererControl::Growl},
};

std::vector<ExpressionPoint> readPoints(const domain::VocalRegion& region,
                                        ExpressionChannel channel) {
  return readExpressionPoints(region, channel);
}

}  // namespace

std::vector<ExpressionPoint> readExpressionPoints(const domain::VocalRegion& region,
                                                  ExpressionChannel channel) {
  std::vector<ExpressionPoint> points;
  switch (channel) {
    case ExpressionChannel::Formant:
      for (const auto& point : region.formantAutomation.points())
        points.push_back({point.tick, point.semitones});
      break;
    case ExpressionChannel::Breathiness:
      for (const auto& point : region.breathinessAutomation.points())
        points.push_back({point.tick, point.amount});
      break;
    case ExpressionChannel::Tension:
      for (const auto& point : region.tensionAutomation.points())
        points.push_back({point.tick, point.amount});
      break;
    case ExpressionChannel::Airiness:
      for (const auto& point : region.airinessAutomation.points())
        points.push_back({point.tick, point.amount});
      break;
    case ExpressionChannel::Gender:
      for (const auto& point : region.genderAutomation.points())
        points.push_back({point.tick, point.amount});
      break;
    case ExpressionChannel::Growl:
      for (const auto& point : region.growlAutomation.points())
        points.push_back({point.tick, point.amount});
      break;
  }
  return points;
}

namespace {

std::unique_ptr<application::EditPerformanceCommand> makeChannelCommand(
    domain::RegionId region, ExpressionChannel channel, std::vector<ExpressionPoint> points) {
  switch (channel) {
    case ExpressionChannel::Formant: {
      domain::FormantAutomation curve;
      std::vector<domain::FormantAutomationPoint> converted;
      for (const auto& point : points) converted.push_back({point.tick, point.amount});
      if (!curve.replacePoints(std::move(converted))) return nullptr;
      return std::make_unique<application::EditPerformanceCommand>(
          std::vector<application::NoteExpressionEdit>{},
          std::vector<application::RegionDynamicsEdit>{},
          std::vector<application::TrackStyleEdit>{},
          std::vector<application::RegionOwnershipEdit>{},
          std::vector<application::RegionFormantEdit>{{region, std::move(curve)}});
    }
    case ExpressionChannel::Breathiness: {
      domain::BreathinessAutomation curve;
      std::vector<domain::BreathinessAutomationPoint> converted;
      for (const auto& point : points) converted.push_back({point.tick, point.amount});
      if (!curve.replacePoints(std::move(converted))) return nullptr;
      return std::make_unique<application::EditPerformanceCommand>(
          std::vector<application::NoteExpressionEdit>{},
          std::vector<application::RegionDynamicsEdit>{},
          std::vector<application::TrackStyleEdit>{},
          std::vector<application::RegionOwnershipEdit>{},
          std::vector<application::RegionFormantEdit>{},
          std::vector<application::RegionBreathinessEdit>{{region, std::move(curve)}});
    }
    case ExpressionChannel::Tension: {
      domain::TensionAutomation curve;
      std::vector<domain::TensionAutomationPoint> converted;
      for (const auto& point : points) converted.push_back({point.tick, point.amount});
      if (!curve.replacePoints(std::move(converted))) return nullptr;
      return std::make_unique<application::EditPerformanceCommand>(
          std::vector<application::NoteExpressionEdit>{},
          std::vector<application::RegionDynamicsEdit>{},
          std::vector<application::TrackStyleEdit>{},
          std::vector<application::RegionOwnershipEdit>{},
          std::vector<application::RegionFormantEdit>{},
          std::vector<application::RegionBreathinessEdit>{},
          std::vector<application::RegionTensionEdit>{{region, std::move(curve)}});
    }
    case ExpressionChannel::Airiness: {
      domain::AirinessAutomation curve;
      std::vector<domain::AirinessAutomationPoint> converted;
      for (const auto& point : points) converted.push_back({point.tick, point.amount});
      if (!curve.replacePoints(std::move(converted))) return nullptr;
      return std::make_unique<application::EditPerformanceCommand>(
          std::vector<application::NoteExpressionEdit>{},
          std::vector<application::RegionDynamicsEdit>{},
          std::vector<application::TrackStyleEdit>{},
          std::vector<application::RegionOwnershipEdit>{},
          std::vector<application::RegionFormantEdit>{},
          std::vector<application::RegionBreathinessEdit>{},
          std::vector<application::RegionTensionEdit>{},
          std::vector<application::RegionAirinessEdit>{{region, std::move(curve)}});
    }
    case ExpressionChannel::Gender: {
      domain::GenderAutomation curve;
      std::vector<domain::GenderAutomationPoint> converted;
      for (const auto& point : points) converted.push_back({point.tick, point.amount});
      if (!curve.replacePoints(std::move(converted))) return nullptr;
      return std::make_unique<application::EditPerformanceCommand>(
          std::vector<application::NoteExpressionEdit>{},
          std::vector<application::RegionDynamicsEdit>{},
          std::vector<application::TrackStyleEdit>{},
          std::vector<application::RegionOwnershipEdit>{},
          std::vector<application::RegionFormantEdit>{},
          std::vector<application::RegionBreathinessEdit>{},
          std::vector<application::RegionTensionEdit>{},
          std::vector<application::RegionAirinessEdit>{},
          std::vector<application::RegionGenderEdit>{{region, std::move(curve)}});
    }
    case ExpressionChannel::Growl: {
      domain::GrowlAutomation curve;
      std::vector<domain::GrowlAutomationPoint> converted;
      for (const auto& point : points) converted.push_back({point.tick, point.amount});
      if (!curve.replacePoints(std::move(converted))) return nullptr;
      return std::make_unique<application::EditPerformanceCommand>(
          std::vector<application::NoteExpressionEdit>{},
          std::vector<application::RegionDynamicsEdit>{},
          std::vector<application::TrackStyleEdit>{},
          std::vector<application::RegionOwnershipEdit>{},
          std::vector<application::RegionFormantEdit>{},
          std::vector<application::RegionBreathinessEdit>{},
          std::vector<application::RegionTensionEdit>{},
          std::vector<application::RegionAirinessEdit>{},
          std::vector<application::RegionGenderEdit>{},
          std::vector<application::RegionGrowlEdit>{{region, std::move(curve)}});
    }
  }
  return nullptr;
}

std::optional<domain::TrackId> trackOwningRegion(const domain::Project& project,
                                                 domain::RegionId region) {
  for (const auto& track : project.vocalTracks())
    if (track.findRegion(region) != nullptr) return track.id;
  return std::nullopt;
}

}  // namespace

ExpressionChannelDescriptor describeExpressionChannel(ExpressionChannel channel) noexcept {
  return kDescriptors[expressionChannelIndex(channel)];
}

ExpressionChannel expressionChannelAt(std::size_t index) noexcept {
  return kDescriptors[index % kExpressionChannelCount].channel;
}

std::size_t expressionChannelIndex(ExpressionChannel channel) noexcept {
  for (std::size_t index = 0U; index < kExpressionChannelCount; ++index)
    if (kDescriptors[index].channel == channel) return index;
  return 0U;
}

ExpressionChannel nextExpressionChannel(ExpressionChannel channel, int direction) noexcept {
  const auto size = static_cast<int>(kExpressionChannelCount);
  const auto index = static_cast<int>(expressionChannelIndex(channel)) + direction;
  return expressionChannelAt(static_cast<std::size_t>(((index % size) + size) % size));
}

core::Result<void> validateExpressionCarrier(const domain::Project& project,
                                             domain::TrackId trackId,
                                             ExpressionChannel channel) {
  const auto* track = project.findVocalTrack(trackId);
  if (track == nullptr)
    return core::failure(core::ErrorCode::NotFound, "Expression channel has no vocal track");
  // The source-filter carrier owns its own excitation and tract; the sample-bank and neural carriers
  // receive audio and refuse these channels by name. The surface reports the same decision the
  // renderer makes, so a drawn curve is never silently dropped.
  const auto carrier = track->proceduralRecipe ? synthesis::RendererCarrier::SourceFilter
                                               : synthesis::RendererCarrier::SampleBank;
  synthesis::RendererControlRequest request;
  request.require(describeExpressionChannel(channel).control);
  const auto allowed = synthesis::validateRendererCapabilities(carrier, request);
  if (!allowed)
    return core::failure(core::ErrorCode::Unsupported,
        std::string{"The selected singer cannot apply the "} +
            std::string{describeExpressionChannel(channel).label} +
            " channel: " + allowed.error().message +
            ". Select a source-filter (voice designer) singer to edit it.");
  return core::success();
}

ExpressionLaneModel::ExpressionLaneModel(application::PerformanceJobContext context,
                                         domain::RegionId region, domain::TrackId track,
                                         ExpressionChannel channel, std::uint64_t revision,
                                         std::vector<ExpressionPoint> points)
    : context_(std::move(context)), region_(region), track_(track), channel_(channel),
      revision_(revision), source_(std::move(points)), draft_(source_) {}

core::Result<ExpressionLaneModel> ExpressionLaneModel::prepare(
    const application::EditorSession& session, domain::RegionId regionId,
    ExpressionChannel channel, std::stop_token stop) {
  if (stop.stop_requested())
    return core::failure<ExpressionLaneModel>(core::ErrorCode::Conflict,
                                              "Expression lane capture cancelled");
  const auto* region = session.project().findRegion(regionId);
  if (region == nullptr)
    return core::failure<ExpressionLaneModel>(core::ErrorCode::NotFound,
                                              "Choose a region to edit expression on");
  if (region->notes.size() > 10000U)
    return core::failure<ExpressionLaneModel>(core::ErrorCode::InvalidArgument,
                                              "Choose a region with at most 10000 notes");
  const auto track = trackOwningRegion(session.project(), regionId);
  if (!track.has_value())
    return core::failure<ExpressionLaneModel>(core::ErrorCode::NotFound,
                                              "Expression lane region has no track");
  auto context = session.capturePerformanceJob();
  if (!context) return core::Result<ExpressionLaneModel>{context.error()};
  if (stop.stop_requested())
    return core::failure<ExpressionLaneModel>(core::ErrorCode::Conflict,
                                              "Expression lane capture cancelled");
  auto points = readPoints(*region, channel);
  return ExpressionLaneModel{std::move(context.value()), regionId, *track, channel,
                             session.revision(), std::move(points)};
}

core::Result<void> ExpressionLaneModel::editable() const {
  if (state_ != State::Ready)
    return core::failure(core::ErrorCode::Conflict, "Expression lane draft is closed");
  return validateExpressionCarrier(context_.sourceProject(), track_, channel_);
}

float ExpressionLaneModel::valueAt(time::Tick tick) const noexcept {
  if (draft_.empty()) return describeExpressionChannel(channel_).neutral;
  const auto after = std::lower_bound(draft_.begin(), draft_.end(), tick,
      [](const ExpressionPoint& point, time::Tick value) { return point.tick < value; });
  if (after == draft_.begin()) return after->amount;
  if (after == draft_.end()) return draft_.back().amount;
  if (after->tick == tick) return after->amount;
  const auto before = after - 1;
  const auto span = (after->tick - before->tick).value();
  if (span <= 0) return after->amount;
  const auto position = static_cast<double>((tick - before->tick).value()) /
                        static_cast<double>(span);
  return static_cast<float>(static_cast<double>(before->amount) +
                            static_cast<double>(after->amount - before->amount) * position);
}

core::Result<void> ExpressionLaneModel::validatePoint(ExpressionPoint point) const {
  const auto descriptor = describeExpressionChannel(channel_);
  if (point.tick < time::Tick{0} || !std::isfinite(point.amount) ||
      point.amount < descriptor.minimum || point.amount > descriptor.maximum)
    return core::failure(core::ErrorCode::InvalidArgument,
        std::string{descriptor.label} + " requires a finite value between its documented bounds");
  const auto* region = context_.sourceProject().findRegion(region_);
  if (region == nullptr)
    return core::failure(core::ErrorCode::NotFound, "Expression lane region is missing");
  if (point.tick > region->durationTick)
    return core::failure(core::ErrorCode::InvariantViolation,
        std::string{descriptor.label} + " point extends beyond the region");
  return core::success();
}

core::Result<void> ExpressionLaneModel::upsert(ExpressionPoint point) {
  const auto ready = editable();
  if (!ready) return ready;
  const auto valid = validatePoint(point);
  if (!valid) return valid;
  const auto existing = std::lower_bound(draft_.begin(), draft_.end(), point.tick,
      [](const ExpressionPoint& candidate, time::Tick value) { return candidate.tick < value; });
  if (existing != draft_.end() && existing->tick == point.tick) *existing = point;
  else draft_.insert(existing, point);
  return core::success();
}

core::Result<void> ExpressionLaneModel::erase(time::Tick tick) {
  const auto ready = editable();
  if (!ready) return ready;
  const auto existing = std::lower_bound(draft_.begin(), draft_.end(), tick,
      [](const ExpressionPoint& candidate, time::Tick value) { return candidate.tick < value; });
  if (existing == draft_.end() || existing->tick != tick)
    return core::failure(core::ErrorCode::NotFound,
        std::string{describeExpressionChannel(channel_).label} + " point no longer exists");
  draft_.erase(existing);
  return core::success();
}

core::Result<void> ExpressionLaneModel::replacePoints(std::vector<ExpressionPoint> points) {
  const auto ready = editable();
  if (!ready) return ready;
  std::optional<time::Tick> previous;
  for (const auto& point : points) {
    const auto valid = validatePoint(point);
    if (!valid) return valid;
    if (previous.has_value() && point.tick <= *previous)
      return core::failure(core::ErrorCode::InvariantViolation,
          std::string{describeExpressionChannel(channel_).label} +
              " points must be strictly ordered and unique");
    previous = point.tick;
  }
  draft_ = std::move(points);
  return core::success();
}

core::Result<void> ExpressionLaneModel::move(time::Tick from, ExpressionPoint to) {
  const auto ready = editable();
  if (!ready) return ready;
  const auto valid = validatePoint(to);
  if (!valid) return valid;
  const auto contains = [&](time::Tick tick) {
    const auto found = std::lower_bound(draft_.begin(), draft_.end(), tick,
        [](const ExpressionPoint& candidate, time::Tick value) { return candidate.tick < value; });
    return found != draft_.end() && found->tick == tick;
  };
  if (!contains(from))
    return core::failure(core::ErrorCode::NotFound,
        std::string{describeExpressionChannel(channel_).label} + " point no longer exists");
  if (from != to.tick && contains(to.tick))
    return core::failure(core::ErrorCode::Conflict,
        std::string{describeExpressionChannel(channel_).label} +
            " point already occupies the destination tick");
  auto next = draft_;
  const auto existing = std::lower_bound(next.begin(), next.end(), from,
      [](const ExpressionPoint& candidate, time::Tick value) { return candidate.tick < value; });
  next.erase(existing);
  const auto insertion = std::lower_bound(next.begin(), next.end(), to.tick,
      [](const ExpressionPoint& candidate, time::Tick value) { return candidate.tick < value; });
  next.insert(insertion, to);
  draft_ = std::move(next);
  return core::success();
}

core::Result<void> ExpressionLaneModel::reset() {
  const auto ready = editable();
  if (!ready) return ready;
  draft_ = source_;
  return core::success();
}

core::Result<void> ExpressionLaneModel::commit(application::EditorSession& session) const {
  auto command = makeChannelCommand(region_, channel_, draft_);
  if (command == nullptr)
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Expression lane draft could not be stored in its channel");
  return session.executePerformanceResult(context_, std::move(command));
}

core::Result<void> ExpressionLaneModel::apply(application::EditorSession& session,
                                             domain::RegionId activeRegion,
                                             std::stop_token stop) {
  if (stop.stop_requested() || !matches(session, activeRegion))
    return core::failure(core::ErrorCode::Conflict,
                         "Expression lane draft is stale, cancelled or closed");
  const auto ready = editable();
  if (!ready) return ready;
  if (hasChanges()) {
    const auto applied = commit(session);
    if (!applied) return applied;
  }
  state_ = State::Applied;
  return core::success();
}

void ExpressionLaneModel::cancel() noexcept {
  if (state_ == State::Ready) state_ = State::Cancelled;
}

bool ExpressionLaneModel::matches(const application::EditorSession& session,
                                  domain::RegionId activeRegion) const {
  return state_ == State::Ready && activeRegion == region_ &&
      session.revision() == revision_ &&
      static_cast<bool>(session.validatePerformanceJob(context_));
}

}  // namespace seam::ui
