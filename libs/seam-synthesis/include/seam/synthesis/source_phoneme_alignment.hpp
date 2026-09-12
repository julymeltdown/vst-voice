#pragma once

#include "seam/voicebank/voicebank.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace seam::synthesis {
struct SourcePhoneLandmark final {
  std::string phone;
  time::SampleFrame frame{0};
  friend bool operator==(const SourcePhoneLandmark&, const SourcePhoneLandmark&) = default;
};

// Authored source positions, bound to exact encoded audio bytes and phone order.
// Structural validation does not establish phonetic accuracy or reviewer consent.
struct SourcePhonemeAlignment final {
  std::string unitId;
  std::string audioSha256;
  std::vector<SourcePhoneLandmark> landmarks;
  [[nodiscard]] core::Result<void> validate(const voicebank::Unit& unit,
      std::string_view verifiedAudioSha256, time::SampleFrame decodedFrames) const;
  friend bool operator==(const SourcePhonemeAlignment&, const SourcePhonemeAlignment&) = default;
};
[[nodiscard]] core::Result<std::string> encodeSourcePhonemeAlignment(
    const SourcePhonemeAlignment& alignment, const voicebank::Unit& unit,
    std::string_view verifiedAudioSha256, time::SampleFrame decodedFrames);
[[nodiscard]] core::Result<SourcePhonemeAlignment> decodeSourcePhonemeAlignment(
    std::string_view json, const voicebank::Unit& unit,
    std::string_view verifiedAudioSha256, time::SampleFrame decodedFrames);
}
