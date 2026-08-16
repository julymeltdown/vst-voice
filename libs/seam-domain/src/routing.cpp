#include "seam/domain/routing.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace seam::domain {
namespace {

bool finiteGain(float value) noexcept {
  return std::isfinite(value) && std::abs(value) <= 16.0F;
}

}  // namespace

core::Result<void> RoutingMatrix::validate() const {
  if (sourceChannels == 0U || sourceChannels > kMaximumAudioChannels ||
      destinationChannels == 0U ||
      destinationChannels > kMaximumAudioChannels) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Routing matrix channel count is invalid");
  }
  const auto expected = static_cast<std::size_t>(sourceChannels) *
                        static_cast<std::size_t>(destinationChannels);
  if (gains.size() != expected) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Routing matrix gain count does not match dimensions");
  }
  for (const auto value : gains) {
    if (!finiteGain(value)) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Routing matrix contains an invalid gain");
    }
  }
  return core::success();
}

float RoutingMatrix::gain(std::uint8_t destination,
                          std::uint8_t source) const noexcept {
  if (destination >= destinationChannels || source >= sourceChannels) return 0.0F;
  const auto index = static_cast<std::size_t>(destination) * sourceChannels + source;
  return index < gains.size() ? gains[index] : 0.0F;
}

void RoutingMatrix::setGain(std::uint8_t destination, std::uint8_t source,
                            float value) noexcept {
  if (destination >= destinationChannels || source >= sourceChannels) return;
  const auto index = static_cast<std::size_t>(destination) * sourceChannels + source;
  if (index < gains.size()) gains[index] = value;
}

RoutingMatrix RoutingMatrix::identity(std::uint8_t channels) {
  RoutingMatrix result;
  result.sourceChannels = channels;
  result.destinationChannels = channels;
  result.gains.assign(static_cast<std::size_t>(channels) * channels, 0.0F);
  for (std::uint8_t channel = 0U; channel < channels; ++channel) {
    result.setGain(channel, channel, 1.0F);
  }
  return result;
}

RoutingMatrix RoutingMatrix::monoToStereo(float pan) {
  const auto clamped = std::clamp(std::isfinite(pan) ? pan : 0.0F, -1.0F, 1.0F);
  const auto angle = (static_cast<double>(clamped) + 1.0) *
                     (std::numbers::pi / 4.0);
  return RoutingMatrix{
      .sourceChannels = 1U,
      .destinationChannels = 2U,
      .gains = {static_cast<float>(std::cos(angle)),
                static_cast<float>(std::sin(angle))},
  };
}

core::Result<void> TrackOutputRoute::validate() const {
  if (!bus.valid()) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Track output route requires a valid bus ID");
  }
  return matrix.validate();
}

core::Result<void> AudioBus::validate() const {
  if (!id.valid() || name.empty() || channelCount == 0U ||
      channelCount > kMaximumAudioChannels || !finiteGain(gainDb)) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Audio bus is invalid", id.toString());
  }
  return core::success();
}

core::Result<void> BusSend::validate() const {
  if (!sourceBus.valid() || !destinationBus.valid() ||
      sourceBus == destinationBus || !finiteGain(gainDb)) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Bus send is invalid");
  }
  return matrix.validate();
}

core::Result<void> DeviceOutputRoute::validate(
    std::uint8_t deviceChannels) const {
  if (!sourceBus.valid() || !finiteGain(gainDb)) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Device output route is invalid");
  }
  const auto matrixValidation = matrix.validate();
  if (!matrixValidation) return matrixValidation;
  if (matrix.destinationChannels != deviceChannels) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Device output route matrix does not target the configured device channel count");
  }
  return core::success();
}

const AudioBus* ProjectRouting::findBus(BusId id) const noexcept {
  const auto iterator = std::find_if(buses.begin(), buses.end(),
                                     [id](const AudioBus& bus) { return bus.id == id; });
  return iterator == buses.end() ? nullptr : &*iterator;
}

AudioBus* ProjectRouting::findBus(BusId id) noexcept {
  const auto iterator = std::find_if(buses.begin(), buses.end(),
                                     [id](const AudioBus& bus) { return bus.id == id; });
  return iterator == buses.end() ? nullptr : &*iterator;
}

core::Result<std::vector<BusId>> ProjectRouting::topologicalOrder() const {
  std::unordered_map<BusId, std::size_t> indegree;
  std::unordered_map<BusId, std::vector<BusId>> adjacency;
  for (const auto& bus : buses) indegree.emplace(bus.id, 0U);
  for (const auto& send : sends) {
    if (!send.enabled) continue;
    if (!indegree.contains(send.sourceBus) ||
        !indegree.contains(send.destinationBus)) {
      return core::failure<std::vector<BusId>>(
          core::ErrorCode::InvariantViolation,
          "Routing graph contains a send that references a missing bus");
    }
    adjacency[send.sourceBus].push_back(send.destinationBus);
    ++indegree[send.destinationBus];
  }

  std::vector<BusId> ready;
  ready.reserve(buses.size());
  for (const auto& [id, degree] : indegree) {
    if (degree == 0U) ready.push_back(id);
  }
  std::sort(ready.begin(), ready.end());

  std::vector<BusId> order;
  order.reserve(buses.size());
  while (!ready.empty()) {
    const auto id = ready.front();
    ready.erase(ready.begin());
    order.push_back(id);
    auto destinations = adjacency[id];
    std::sort(destinations.begin(), destinations.end());
    for (const auto destination : destinations) {
      auto& degree = indegree[destination];
      if (--degree == 0U) {
        ready.push_back(destination);
        std::sort(ready.begin(), ready.end());
      }
    }
  }
  if (order.size() != buses.size()) {
    return core::failure<std::vector<BusId>>(
        core::ErrorCode::InvariantViolation,
        "Routing graph contains a bus cycle");
  }
  return order;
}

core::Result<void> ProjectRouting::validate() const {
  if (deviceOutputChannels == 0U ||
      deviceOutputChannels > kMaximumAudioChannels || !masterBus.valid() ||
      buses.empty()) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Project routing header is invalid");
  }

  std::unordered_set<BusId> busIds;
  for (const auto& bus : buses) {
    const auto busValidation = bus.validate();
    if (!busValidation) return busValidation;
    if (!busIds.insert(bus.id).second) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Audio bus IDs must be unique", bus.id.toString());
    }
  }
  if (!busIds.contains(masterBus)) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Project master bus does not exist");
  }

  for (const auto& send : sends) {
    const auto validation = send.validate();
    if (!validation) return validation;
    const auto* source = findBus(send.sourceBus);
    const auto* destination = findBus(send.destinationBus);
    if (source == nullptr || destination == nullptr ||
        send.matrix.sourceChannels != source->channelCount ||
        send.matrix.destinationChannels != destination->channelCount) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Bus send matrix dimensions do not match its buses");
    }
  }
  const auto order = topologicalOrder();
  if (!order) return core::Result<void>{order.error()};

  if (deviceRoutes.empty()) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Project routing has no physical output route");
  }
  for (const auto& route : deviceRoutes) {
    const auto validation = route.validate(deviceOutputChannels);
    if (!validation) return validation;
    const auto* source = findBus(route.sourceBus);
    if (source == nullptr || route.matrix.sourceChannels != source->channelCount) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Device output route matrix does not match its source bus");
    }
  }
  return core::success();
}

float decibelsToLinear(float gainDb) noexcept {
  if (!std::isfinite(gainDb)) return 1.0F;
  return std::pow(10.0F, gainDb / 20.0F);
}

}  // namespace seam::domain
