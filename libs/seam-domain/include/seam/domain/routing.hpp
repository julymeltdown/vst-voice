#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/ids.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace seam::domain {

constexpr std::uint8_t kMaximumAudioChannels = 8U;

struct RoutingMatrix final {
  std::uint8_t sourceChannels{1U};
  std::uint8_t destinationChannels{2U};
  std::vector<float> gains{1.0F, 1.0F};

  [[nodiscard]] core::Result<void> validate() const;
  [[nodiscard]] float gain(std::uint8_t destination,
                           std::uint8_t source) const noexcept;
  void setGain(std::uint8_t destination, std::uint8_t source,
               float value) noexcept;

  [[nodiscard]] static RoutingMatrix identity(std::uint8_t channels);
  [[nodiscard]] static RoutingMatrix monoToStereo(float pan = 0.0F);

  friend bool operator==(const RoutingMatrix&, const RoutingMatrix&) = default;
};

struct TrackOutputRoute final {
  BusId bus{BusId{1U}};
  RoutingMatrix matrix{RoutingMatrix::monoToStereo()};

  [[nodiscard]] core::Result<void> validate() const;

  friend bool operator==(const TrackOutputRoute&, const TrackOutputRoute&) = default;
};

struct AudioBus final {
  BusId id{BusId{1U}};
  std::string name{"Master"};
  std::uint8_t channelCount{2U};
  float gainDb{0.0F};
  bool muted{false};
  bool solo{false};

  [[nodiscard]] core::Result<void> validate() const;

  friend bool operator==(const AudioBus&, const AudioBus&) = default;
};

struct BusSend final {
  BusId sourceBus;
  BusId destinationBus;
  RoutingMatrix matrix;
  float gainDb{0.0F};
  bool enabled{true};

  [[nodiscard]] core::Result<void> validate() const;

  friend bool operator==(const BusSend&, const BusSend&) = default;
};

struct DeviceOutputRoute final {
  BusId sourceBus{BusId{1U}};
  RoutingMatrix matrix{RoutingMatrix::identity(2U)};
  float gainDb{0.0F};
  bool enabled{true};

  [[nodiscard]] core::Result<void> validate(
      std::uint8_t deviceChannels) const;

  friend bool operator==(const DeviceOutputRoute&,
                         const DeviceOutputRoute&) = default;
};

struct ProjectRouting final {
  std::uint8_t deviceOutputChannels{2U};
  BusId masterBus{BusId{1U}};
  std::vector<AudioBus> buses{
      AudioBus{.id = BusId{1U}, .name = "Master", .channelCount = 2U}};
  std::vector<BusSend> sends;
  std::vector<DeviceOutputRoute> deviceRoutes{
      DeviceOutputRoute{.sourceBus = BusId{1U},
                        .matrix = RoutingMatrix::identity(2U)}};

  [[nodiscard]] const AudioBus* findBus(BusId id) const noexcept;
  [[nodiscard]] AudioBus* findBus(BusId id) noexcept;
  [[nodiscard]] core::Result<std::vector<BusId>> topologicalOrder() const;
  [[nodiscard]] core::Result<void> validate() const;

  friend bool operator==(const ProjectRouting&, const ProjectRouting&) = default;
};

[[nodiscard]] float decibelsToLinear(float gainDb) noexcept;

}  // namespace seam::domain
