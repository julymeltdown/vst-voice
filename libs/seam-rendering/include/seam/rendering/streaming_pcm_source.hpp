#pragma once

#include "seam/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace seam::rendering {

struct StreamingPcmSourceInfo final {
  std::uint32_t sampleRate{0U};
  std::uint16_t channels{0U};
  std::uint64_t frameCount{0U};
  std::uint64_t byteSize{0U};
  std::string contentHash;
};

class StreamingPcmSource final {
public:
  [[nodiscard]] static core::Result<std::unique_ptr<StreamingPcmSource>> open(
      const std::filesystem::path& path, std::size_t chunkFrames = 48000U);

  [[nodiscard]] const StreamingPcmSourceInfo& info() const noexcept {
    return info_;
  }
  [[nodiscard]] std::size_t chunkFrames() const noexcept { return chunkFrames_; }
  [[nodiscard]] std::size_t chunkCount() const noexcept;
  [[nodiscard]] core::Result<std::vector<float>> readChunk(
      std::size_t chunkIndex) const;

private:
  StreamingPcmSource(StreamingPcmSourceInfo info, std::filesystem::path path,
                     std::uint64_t dataOffset, std::uint64_t dataBytes,
                     std::uint16_t formatCode, std::uint16_t bitsPerSample,
                     std::uint16_t blockAlign, std::size_t chunkFrames)
      : info_(std::move(info)), path_(std::move(path)),
        dataOffset_(dataOffset), dataBytes_(dataBytes),
        formatCode_(formatCode), bitsPerSample_(bitsPerSample),
        blockAlign_(blockAlign), chunkFrames_(chunkFrames) {}

  StreamingPcmSourceInfo info_;
  std::filesystem::path path_;
  std::uint64_t dataOffset_{0U};
  std::uint64_t dataBytes_{0U};
  std::uint16_t formatCode_{0U};
  std::uint16_t bitsPerSample_{0U};
  std::uint16_t blockAlign_{0U};
  std::size_t chunkFrames_{48000U};
};

}
