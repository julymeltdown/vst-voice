#include "seam/voicebank/waveform.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace seam::voicebank {
namespace {

WaveformBucket aggregate(std::span<const float> samples) noexcept {
  WaveformBucket bucket;
  if (samples.empty()) return bucket;
  bucket.minimum = std::numeric_limits<float>::max();
  bucket.maximum = std::numeric_limits<float>::lowest();
  long double square = 0.0L;
  for (const auto sample : samples) {
    const auto value = std::isfinite(sample) ? sample : 0.0F;
    bucket.minimum = std::min(bucket.minimum, value);
    bucket.maximum = std::max(bucket.maximum, value);
    square += static_cast<long double>(value) * static_cast<long double>(value);
  }
  bucket.rms = static_cast<float>(std::sqrt(
      static_cast<double>(square / static_cast<long double>(samples.size()))));
  return bucket;
}

std::string escape(std::string_view input) {
  std::string output;
  for (const auto value : input) {
    switch (value) {
      case '&': output += "&amp;"; break;
      case '<': output += "&lt;"; break;
      case '>': output += "&gt;"; break;
      case '"': output += "&quot;"; break;
      default: output.push_back(value); break;
    }
  }
  return output;
}

}  // namespace

core::Result<WaveformPyramid> WaveformPyramid::build(
    std::span<const float> samples,
    std::size_t baseFramesPerBucket,
    std::size_t maximumLevels) {
  if (samples.empty()) {
    return core::failure<WaveformPyramid>(core::ErrorCode::InvalidArgument,
                                          "Waveform source must not be empty");
  }
  if (baseFramesPerBucket == 0 || maximumLevels == 0) {
    return core::failure<WaveformPyramid>(core::ErrorCode::InvalidArgument,
                                          "Waveform pyramid parameters must be positive");
  }
  WaveformPyramid pyramid;
  std::size_t framesPerBucket = baseFramesPerBucket;
  for (std::size_t levelIndex = 0; levelIndex < maximumLevels; ++levelIndex) {
    WaveformLevel level;
    level.framesPerBucket = framesPerBucket;
    const auto count = (samples.size() + framesPerBucket - 1U) / framesPerBucket;
    level.buckets.reserve(count);
    for (std::size_t index = 0; index < samples.size(); index += framesPerBucket) {
      const auto length = std::min(framesPerBucket, samples.size() - index);
      level.buckets.push_back(aggregate(samples.subspan(index, length)));
    }
    pyramid.levels_.push_back(std::move(level));
    if (count <= 1 || framesPerBucket > samples.size() / 2U) break;
    if (framesPerBucket > std::numeric_limits<std::size_t>::max() / 2U) break;
    framesPerBucket *= 2U;
  }
  return pyramid;
}

const WaveformLevel& WaveformPyramid::levelFor(double framesPerPixel) const noexcept {
  const auto target = std::max(1.0, framesPerPixel);
  const WaveformLevel* chosen = &levels_.front();
  for (const auto& level : levels_) {
    if (static_cast<double>(level.framesPerBucket) <= target * 2.0) {
      chosen = &level;
    } else {
      break;
    }
  }
  return *chosen;
}

core::Result<void> writeWaveformSvg(const std::filesystem::path& path,
                                    const WaveformLevel& level,
                                    double width,
                                    double height,
                                    std::string_view title) {
  if (level.buckets.empty() || !std::isfinite(width) || !std::isfinite(height) ||
      width <= 0.0 || height <= 0.0) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Waveform SVG arguments are invalid");
  }
  std::error_code error;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
      return core::failure(core::ErrorCode::IoError,
                           "Unable to create waveform output directory",
                           error.message());
    }
  }
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to create waveform SVG",
                         path.string());
  }
  stream << std::fixed << std::setprecision(2);
  stream << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width
         << "\" height=\"" << height << "\" viewBox=\"0 0 " << width << ' '
         << height << "\">\n";
  stream << "<rect width=\"100%\" height=\"100%\" fill=\"#121117\"/>\n";
  stream << "<text x=\"16\" y=\"24\" fill=\"#ddd5e3\" font-family=\"monospace\" font-size=\"13\">"
         << escape(title) << "</text>\n";
  const auto top = 38.0;
  const auto graphHeight = std::max(1.0, height - top - 12.0);
  const auto center = top + graphHeight / 2.0;
  stream << "<line x1=\"0\" y1=\"" << center << "\" x2=\"" << width
         << "\" y2=\"" << center << "\" stroke=\"#342f3a\"/>\n";
  const auto scaleX = width / static_cast<double>(level.buckets.size());
  for (std::size_t index = 0; index < level.buckets.size(); ++index) {
    const auto& bucket = level.buckets[index];
    const auto x = (static_cast<double>(index) + 0.5) * scaleX;
    const auto y1 = center - static_cast<double>(bucket.maximum) * graphHeight * 0.45;
    const auto y2 = center - static_cast<double>(bucket.minimum) * graphHeight * 0.45;
    stream << "<line x1=\"" << x << "\" y1=\"" << y1 << "\" x2=\"" << x
           << "\" y2=\"" << y2 << "\" stroke=\"#a36d88\" stroke-width=\""
           << std::max(0.6, scaleX) << "\"/>\n";
  }
  stream << "</svg>\n";
  stream.flush();
  if (!stream) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to write waveform SVG",
                         path.string());
  }
  return core::success();
}

}  // namespace seam::voicebank
