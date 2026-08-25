#pragma once

#include "seam/core/result.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace seam::platform {

struct AudioDeviceDescription final {
  std::string id;
  std::string name;
  bool isDefault{false};
  bool physical{false};
  std::uint32_t generation{0U};
  std::vector<std::uint32_t> supportedSampleRates;
  std::uint32_t minimumBlockFrames{0U};
  std::uint32_t maximumBlockFrames{0U};
  std::uint8_t minimumOutputChannels{0U};
  std::uint8_t maximumOutputChannels{0U};
};

struct AudioDeviceCatalogSnapshot final {
  std::uint32_t generation{0U};
  std::vector<AudioDeviceDescription> devices;
};

class IAudioDeviceCatalog {
public:
  virtual ~IAudioDeviceCatalog() = default;
  [[nodiscard]] virtual core::Result<AudioDeviceCatalogSnapshot> enumerate() = 0;
};

[[nodiscard]] std::unique_ptr<IAudioDeviceCatalog>
createSystemAudioDeviceCatalog();

}
