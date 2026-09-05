#include "seam/clap/session.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <numbers>
#include <sstream>

namespace seam::clap {

core::Result<void> PluginSession::validate() const {
  if (sampleRate < kMinimumSampleRate || sampleRate > kMaximumSampleRate) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "CLAP session sample rate is outside the supported range");
  }
  if (channelCount == 0U || channelCount > kMaximumChannels) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "CLAP session channel count must be between one and eight");
  }
  if (!std::isfinite(masterGainDb) || masterGainDb < kMinimumMasterGainDb ||
      masterGainDb > kMaximumMasterGainDb) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "CLAP session master gain is invalid");
  }
  if (title.size() > 4096U) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "CLAP session title is too long");
  }
  if (interleavedSamples.empty() ||
      interleavedSamples.size() % channelCount != 0U) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "CLAP session PCM is empty or not channel aligned");
  }
  const auto frames = frameCount();
  if (frames > static_cast<std::uint64_t>(sampleRate) * kMaximumDurationSeconds) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "CLAP session exceeds the ten-minute state limit");
  }
  if (interleavedSamples.size() > kMaximumStateBytes / sizeof(float)) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "CLAP session exceeds the bounded state size");
  }
  for (const auto sample : interleavedSamples) {
    if (!std::isfinite(sample)) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "CLAP session contains a non-finite PCM sample");
    }
  }
  return core::success();
}

core::Result<PluginSession> resampleSession(const PluginSession& source,
                                             std::uint32_t targetSampleRate) {
  const auto sourceValidation = source.validate();
  if (!sourceValidation) return core::Result<PluginSession>{sourceValidation.error()};
  if (targetSampleRate < kMinimumSampleRate || targetSampleRate > kMaximumSampleRate) {
    return core::failure<PluginSession>(core::ErrorCode::InvalidArgument,
                                        "Target CLAP sample rate is invalid");
  }
  if (targetSampleRate == source.sampleRate) return source;

  const auto sourceFrames = source.frameCount();
  const long double ratio = static_cast<long double>(targetSampleRate) /
                            static_cast<long double>(source.sampleRate);
  const auto targetFrames = std::max<std::uint64_t>(
      1U, static_cast<std::uint64_t>(std::llround(
              static_cast<long double>(sourceFrames) * ratio)));
  if (targetFrames > static_cast<std::uint64_t>(targetSampleRate) *
                         kMaximumDurationSeconds) {
    return core::failure<PluginSession>(core::ErrorCode::InvalidArgument,
                                        "Resampled CLAP session exceeds duration limit");
  }

  PluginSession output = source;
  output.sampleRate = targetSampleRate;
  output.interleavedSamples.assign(
      static_cast<std::size_t>(targetFrames) * source.channelCount, 0.0F);
  for (std::uint64_t targetFrame = 0U; targetFrame < targetFrames; ++targetFrame) {
    const long double sourcePosition = static_cast<long double>(targetFrame) /
                                       ratio;
    const auto left = std::min<std::uint64_t>(
        static_cast<std::uint64_t>(sourcePosition), sourceFrames - 1U);
    const auto right = std::min<std::uint64_t>(left + 1U, sourceFrames - 1U);
    const auto fraction = static_cast<float>(sourcePosition - static_cast<long double>(left));
    for (std::uint8_t channel = 0U; channel < source.channelCount; ++channel) {
      const auto leftSample = source.interleavedSamples[
          static_cast<std::size_t>(left) * source.channelCount + channel];
      const auto rightSample = source.interleavedSamples[
          static_cast<std::size_t>(right) * source.channelCount + channel];
      output.interleavedSamples[
          static_cast<std::size_t>(targetFrame) * source.channelCount + channel] =
          leftSample + (rightSample - leftSample) * fraction;
    }
  }
  return output;
}

core::Result<PluginSession> makeDiagnosticSession(std::uint32_t sampleRate,
                                                   std::uint8_t channels,
                                                   double durationSeconds) {
  if (sampleRate < kMinimumSampleRate || sampleRate > kMaximumSampleRate ||
      channels == 0U || channels > kMaximumChannels ||
      !std::isfinite(durationSeconds) || durationSeconds <= 0.0 ||
      durationSeconds > 30.0) {
    return core::failure<PluginSession>(core::ErrorCode::InvalidArgument,
                                        "Diagnostic CLAP session arguments are invalid");
  }
  PluginSession session;
  session.sampleRate = sampleRate;
  session.channelCount = channels;
  session.title = "Project SEAM Phase 10 diagnostic render";
  const auto frames = static_cast<std::uint64_t>(std::llround(
      static_cast<double>(sampleRate) * durationSeconds));
  session.interleavedSamples.resize(
      static_cast<std::size_t>(frames) * channels);
  for (std::uint64_t frame = 0U; frame < frames; ++frame) {
    const auto time = static_cast<double>(frame) / sampleRate;
    for (std::uint8_t channel = 0U; channel < channels; ++channel) {
      const auto frequency = 110.0 * static_cast<double>(channel + 1U);
      const auto envelope = std::min(1.0, time * 12.0) *
                            std::min(1.0, (durationSeconds - time) * 12.0);
      session.interleavedSamples[static_cast<std::size_t>(frame) * channels + channel] =
          static_cast<float>(0.18 * envelope *
                             std::sin(2.0 * std::numbers::pi * frequency * time));
    }
  }
  return session;
}

core::Result<double> parseMasterGainDb(std::string_view text) {
  if (text.empty()) {
    return core::failure<double>(core::ErrorCode::InvalidArgument,
                                 "Master gain must be an exact finite decimal");
  }
  std::istringstream stream{std::string{text}};
  stream.imbue(std::locale::classic());
  double value = 0.0;
  stream >> std::noskipws >> value;
  if (!stream || !stream.eof() || !std::isfinite(value) ||
      value < kMinimumMasterGainDb || value > kMaximumMasterGainDb) {
    return core::failure<double>(
        core::ErrorCode::InvalidArgument,
        "Master gain must be an exact finite decimal between -60 and 6 dB");
  }
  return value;
}

float gainFromDecibels(double decibels) noexcept {
  if (!std::isfinite(decibels)) return 0.0F;
  if (decibels <= kMinimumMasterGainDb) return decibels == kMinimumMasterGainDb
                                               ? static_cast<float>(std::pow(10.0, decibels / 20.0))
                                               : 0.0F;
  return static_cast<float>(std::pow(10.0, std::clamp(decibels,
                                                      kMinimumMasterGainDb,
                                                      kMaximumMasterGainDb) /
                                             20.0));
}

}  // namespace seam::clap
