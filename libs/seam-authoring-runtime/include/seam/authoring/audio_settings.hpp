#pragma once

#include "seam/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

namespace seam::authoring {

struct AudioSettings final {
  std::string deviceId;
  std::uint32_t sampleRate{48000U};
  std::size_t blockFrames{256U};
  std::uint8_t outputChannels{2U};
  std::uint64_t revision{1U};

  friend bool operator==(const AudioSettings&, const AudioSettings&) = default;
};

class AudioSettingsStore final {
public:
  explicit AudioSettingsStore(std::filesystem::path path)
      : path_(std::move(path)) {}

  [[nodiscard]] core::Result<AudioSettings> load() const;
  [[nodiscard]] core::Result<void> save(const AudioSettings& settings) const;

private:
  std::filesystem::path path_;
};

}
