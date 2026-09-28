#include "seam/voicebank/take_inspection.hpp"

#include "seam/core/sha256.hpp"
#include "seam/voicebank/pitch.hpp"

#include <algorithm>
#include <cmath>
#include <span>
#include <system_error>
#include <vector>

namespace seam::voicebank {
namespace {

constexpr std::size_t kMaximumDiagnosticMessageBytes = 256U;
constexpr std::size_t kMaximumDiagnosticContextBytes = 512U;
constexpr std::size_t kMaximumCoverageKeyBytes = 4096U;
constexpr std::size_t kMaximumCoveragePhoneBytes = 128U;
constexpr std::size_t kMaximumCoveragePhones = 64U;

// Diagnostics name user-supplied paths and decoder messages. Keep both short
// enough to show and log, and never end inside a UTF-8 sequence.
std::string bounded(std::string value, std::size_t limit) {
  if (value.size() <= limit) return value;
  value.resize(limit - 3U);
  while (!value.empty() &&
         (static_cast<unsigned char>(value.back()) & 0xC0U) == 0x80U) {
    value.pop_back();
  }
  if (!value.empty() && static_cast<unsigned char>(value.back()) >= 0xC0U) {
    value.pop_back();
  }
  value += "...";
  return value;
}

template <typename T>
core::Result<T> fail(core::ErrorCode code, std::string message,
                     std::string context = {}) {
  return core::Result<T>{core::Error{
      code, bounded(std::move(message), kMaximumDiagnosticMessageBytes),
      bounded(std::move(context), kMaximumDiagnosticContextBytes)}};
}

template <typename T>
core::Result<T> fail(const core::Error& error) {
  return fail<T>(error.code, error.message, error.context);
}

double midiToHz(std::int32_t midi) noexcept {
  return 440.0 * std::pow(2.0, (static_cast<double>(midi) - 69.0) / 12.0);
}

struct MonoMeasurements final {
  std::uint64_t nonFinite{0U};
  std::uint64_t clipped{0U};
  float peak{0.0F};
  double rms{0.0};
  double dcOffset{0.0};
};

// The same mono mix the version-1 dry-take inspector measured: the channel mean
// per frame, with non-finite frames counted and then treated as zero.
core::Result<MonoMeasurements> measureMono(const AudioBuffer& audio,
                                           std::stop_token stopToken) {
  if (audio.channels == 0U) {
    return fail<MonoMeasurements>(core::ErrorCode::ParseError,
                                  "Take audio has no channels");
  }
  const auto frames = audio.frameCount();
  const auto channels = static_cast<std::size_t>(audio.channels);
  long double squareSum = 0.0L;
  long double sum = 0.0L;
  MonoMeasurements result;
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    if ((frame & 4095U) == 0U && stopToken.stop_requested()) {
      return fail<MonoMeasurements>(core::ErrorCode::Conflict,
                                    "Take inspection cancelled");
    }
    double mixed = 0.0;
    for (std::size_t channel = 0U; channel < channels; ++channel) {
      mixed += audio.interleaved[frame * channels + channel];
    }
    const auto sample = static_cast<float>(mixed / static_cast<double>(channels));
    if (!std::isfinite(sample)) {
      ++result.nonFinite;
      continue;
    }
    result.peak = std::max(result.peak, std::abs(sample));
    squareSum += static_cast<long double>(sample) * static_cast<long double>(sample);
    sum += sample;
    if (std::abs(sample) >= kTakeClipLevel) ++result.clipped;
  }
  if (frames != 0U) {
    const auto count = static_cast<long double>(frames);
    result.rms = std::sqrt(static_cast<double>(squareSum / count));
    result.dcOffset = static_cast<double>(sum / count);
  }
  return result;
}

// Voiced share of the analysis windows that carry energy. Quiet windows say
// nothing about whether material is sung, so they are left out.
core::Result<double> energeticVoicedShare(std::span<const float> mono,
                                          std::span<const PitchFrame> frames,
                                          const PitchConfig& config,
                                          std::stop_token stopToken) {
  const auto hop = config.hopSize;
  const auto blocks = (mono.size() + hop - 1U) / hop;
  std::vector<double> blockEnergy(blocks, 0.0);
  for (std::size_t index = 0U; index < mono.size(); ++index) {
    if ((index & 65535U) == 0U && stopToken.stop_requested()) {
      return fail<double>(core::ErrorCode::Conflict, "Take inspection cancelled");
    }
    const auto sample = static_cast<double>(mono[index]);
    blockEnergy[index / hop] += sample * sample;
  }
  std::uint64_t energetic = 0U;
  std::uint64_t voiced = 0U;
  for (const auto& frame : frames) {
    if (frame.sourceFrame >= mono.size()) continue;
    const auto available = std::min<std::size_t>(config.frameSize, mono.size() - frame.sourceFrame);
    const auto first = frame.sourceFrame / hop;
    const auto last = std::min(blocks, (frame.sourceFrame + available + hop - 1U) / hop);
    double energy = 0.0;
    for (auto block = first; block < last; ++block) energy += blockEnergy[block];
    const auto rms = std::sqrt(energy / static_cast<double>(available));
    if (!(rms > kTakeEnergeticWindowRms)) continue;
    ++energetic;
    if (frame.voiced) ++voiced;
  }
  return energetic == 0U ? 0.0
                         : static_cast<double>(voiced) / static_cast<double>(energetic);
}

bool finiteAtMost(double value, double limit) noexcept {
  return std::isfinite(value) && value <= limit;
}

}  // namespace

bool TakeInspection::accepted() const noexcept { return takeChecksPassed(checks); }

core::Result<TakeQcPolicy> takeQcPolicyForCoverageKey(std::string_view coverageKey) {
  const auto separator = coverageKey.find(':');
  if (coverageKey.empty() || coverageKey.size() > kMaximumCoverageKeyBytes ||
      separator == std::string_view::npos || separator == 0U) {
    return fail<TakeQcPolicy>(core::ErrorCode::InvalidArgument,
        "Coverage key must be kind:phone with a bounded phone sequence");
  }
  const auto kind = coverageKey.substr(0U, separator);
  bool anyBreath = false, anyPause = false, anyClosure = false, anyOther = false;
  std::size_t phones = 0U;
  std::size_t start = separator + 1U;
  while (true) {
    const auto end = coverageKey.find(':', start);
    const auto phone = coverageKey.substr(start, end == std::string_view::npos
                                                     ? std::string_view::npos
                                                     : end - start);
    if (phone.empty() || phone.size() > kMaximumCoveragePhoneBytes ||
        ++phones > kMaximumCoveragePhones ||
        std::any_of(phone.begin(), phone.end(), [](char item) {
          return static_cast<unsigned char>(item) <= 32U || item == 127;
        })) {
      return fail<TakeQcPolicy>(core::ErrorCode::InvalidArgument,
          "Coverage key phone sequence is empty, ambiguous or oversized");
    }
    if (phone == "br") anyBreath = true;
    else if (phone == "pau" || phone == "sil") anyPause = true;
    else if (phone == "cl" || phone == "R" || phone == "glottal") anyClosure = true;
    else anyOther = true;
    if (end == std::string_view::npos) break;
    start = end + 1U;
  }
  if (kind == "breath") return TakeQcPolicy::Breath;
  if (kind != "special") return TakeQcPolicy::Voiced;
  const auto classes = static_cast<int>(anyBreath) + static_cast<int>(anyPause) +
                       static_cast<int>(anyClosure) + static_cast<int>(anyOther);
  if (classes != 1) {
    return fail<TakeQcPolicy>(core::ErrorCode::InvalidArgument,
        "Special coverage key mixes breath, pause, closure or voiced phones");
  }
  if (anyBreath) return TakeQcPolicy::Breath;
  if (anyPause) return TakeQcPolicy::Pause;
  if (anyClosure) return TakeQcPolicy::Closure;
  return TakeQcPolicy::Voiced;
}

bool takeCheckApplies(TakeQcPolicy policy, TakeCheck check) noexcept {
  switch (check) {
    case TakeCheck::Format:
    case TakeCheck::Finite:
    case TakeCheck::Clipping:
    case TakeCheck::DcOffset:
      return true;
    case TakeCheck::SignalPresent:
      return policy == TakeQcPolicy::Voiced || policy == TakeQcPolicy::Breath;
    case TakeCheck::Quiet:
      return policy == TakeQcPolicy::Closure || policy == TakeQcPolicy::Pause;
    case TakeCheck::Unvoiced:
      return policy == TakeQcPolicy::Breath;
    case TakeCheck::RootPitch:
      return policy == TakeQcPolicy::Voiced;
  }
  return false;
}

TakeCheckOutcomes evaluateTakeChecks(TakeQcPolicy policy,
                                     const TakeMeasurements& measured) noexcept {
  TakeCheckOutcomes result{};
  const auto set = [&](TakeCheck check, bool passed) {
    result[static_cast<std::size_t>(check)] =
        !takeCheckApplies(policy, check) ? TakeCheckOutcome::Inapplicable
        : passed                         ? TakeCheckOutcome::Pass
                                         : TakeCheckOutcome::Fail;
  };
  set(TakeCheck::Format, measured.sampleRate == kTakeSampleRate &&
                             measured.channels == 1U &&
                             (measured.bitsPerSample == 24U || measured.bitsPerSample == 32U));
  set(TakeCheck::Finite, measured.nonFiniteSamples == 0U);
  set(TakeCheck::Clipping, measured.clippedSamples == 0U);
  set(TakeCheck::DcOffset, std::isfinite(measured.dcOffset) &&
                               std::abs(measured.dcOffset) <= kTakeMaximumDcOffset);
  set(TakeCheck::SignalPresent, std::isfinite(measured.rms) && measured.rms > kTakeSignalFloorRms);
  set(TakeCheck::Quiet, finiteAtMost(measured.rms, kTakeQuietCeilingRms) &&
                            finiteAtMost(static_cast<double>(measured.peak), kTakeQuietCeilingPeak));
  set(TakeCheck::Unvoiced, measured.voicedShare.has_value() &&
                               finiteAtMost(*measured.voicedShare, kTakeMaximumVoicedShare));
  set(TakeCheck::RootPitch, measured.rootPitchDeviationCents.has_value() &&
                                std::isfinite(*measured.rootPitchDeviationCents) &&
                                std::abs(*measured.rootPitchDeviationCents) <=
                                    kTakeRootPitchToleranceCents);
  return result;
}

bool takeChecksPassed(const TakeCheckOutcomes& checks) noexcept {
  return std::none_of(checks.begin(), checks.end(), [](TakeCheckOutcome outcome) {
    return outcome == TakeCheckOutcome::Fail;
  });
}

core::Result<TakeInspection> inspectTake(const std::filesystem::path& path,
                                         const TakeInspectionRequest& request,
                                         std::stop_token stopToken) {
  using Output = TakeInspection;
  const auto cancelled = [] {
    return fail<Output>(core::ErrorCode::Conflict, "Take inspection cancelled");
  };
  if (stopToken.stop_requested()) return cancelled();
  if (path.empty()) {
    return fail<Output>(core::ErrorCode::InvalidArgument,
                        "Take inspection needs an audio file path");
  }
  if (request.maximumBytes == 0U || request.maximumBytes > kMaximumSupportedWavBytes) {
    return fail<Output>(core::ErrorCode::InvalidArgument,
                        "Take inspection size limit is out of range");
  }
  const bool voiced = request.policy == TakeQcPolicy::Voiced;
  if (voiced != request.expectedRootMidi.has_value()) {
    return fail<Output>(core::ErrorCode::InvalidArgument, voiced
        ? "Voiced take inspection needs the expected root MIDI note"
        : "Only voiced take inspection checks an expected root note");
  }
  if (voiced && (*request.expectedRootMidi < 0 || *request.expectedRootMidi > 127)) {
    return fail<Output>(core::ErrorCode::InvalidArgument,
                        "Expected root MIDI note must be between 0 and 127");
  }
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (error || status.type() == std::filesystem::file_type::not_found) {
    return fail<Output>(core::ErrorCode::NotFound, "Take audio is unavailable",
                        path.string());
  }
  if (status.type() != std::filesystem::file_type::regular) {
    return fail<Output>(core::ErrorCode::InvalidArgument,
                        "Take audio must be a regular file", path.string());
  }
  const auto size = std::filesystem::file_size(path, error);
  if (error) {
    return fail<Output>(core::ErrorCode::IoError, "Unable to read the take audio size",
                        path.string());
  }
  if (size == 0U) {
    return fail<Output>(core::ErrorCode::ParseError, "Take audio file is empty",
                        path.string());
  }
  if (size > request.maximumBytes) {
    return fail<Output>(core::ErrorCode::InvalidArgument,
                        "Take audio exceeds the inspection size limit", path.string());
  }
  const auto before = core::sha256File(path, request.maximumBytes, stopToken);
  if (!before) return fail<Output>(before.error());
  WavReadLimits limits;
  limits.maximumFrames = request.maximumBytes;
  limits.maximumDecodedSamples = request.maximumBytes;
  auto audio = readWav(path, limits, stopToken);
  if (!audio) return fail<Output>(audio.error());
  const auto after = core::sha256File(path, request.maximumBytes, stopToken);
  if (!after) return fail<Output>(after.error());
  const auto sizeAfter = std::filesystem::file_size(path, error);
  if (error || before.value() != after.value() || sizeAfter != size) {
    return fail<Output>(core::ErrorCode::Conflict,
                        "Take audio changed while it was being inspected", path.string());
  }

  const auto mono = measureMono(audio.value(), stopToken);
  if (!mono) return fail<Output>(mono.error());
  TakeMeasurements measured{
      .sampleRate = audio.value().sampleRate,
      .channels = audio.value().channels,
      .bitsPerSample = audio.value().bitsPerSample,
      .frameCount = static_cast<std::uint64_t>(audio.value().frameCount()),
      .nonFiniteSamples = mono.value().nonFinite,
      .clippedSamples = mono.value().clipped,
      .peak = mono.value().peak,
      .rms = mono.value().rms,
      .dcOffset = mono.value().dcOffset,
      .expectedRootMidi = request.expectedRootMidi,
  };
  // Pitch is measured on mono, finite audio long enough for one analysis
  // window. Otherwise the pitch measurements stay absent and the applicable
  // pitch check fails rather than being guessed.
  const bool pitchPolicy = voiced || request.policy == TakeQcPolicy::Breath;
  if (pitchPolicy && measured.channels == 1U && measured.nonFiniteSamples == 0U &&
      measured.frameCount >= kTakePitchMinimumFrames) {
    PitchConfig config;
    config.correlationMethod = PitchCorrelationMethod::Fft;
    const auto pitch = analyzePitch(audio.value().interleaved, measured.sampleRate, config, stopToken);
    if (stopToken.stop_requested()) return cancelled();
    if (pitch && voiced) {
      const auto median = medianVoicedPitch(pitch.value());
      if (median > 0.0 && std::isfinite(median)) {
        measured.analyzedRootMidi = static_cast<std::int32_t>(
            std::clamp(std::lround(69.0 + 12.0 * std::log2(median / 440.0)), 0L, 127L));
        measured.rootPitchDeviationCents =
            1200.0 * std::log2(median / midiToHz(*request.expectedRootMidi));
      }
    } else if (pitch) {
      const auto share = energeticVoicedShare(audio.value().interleaved, pitch.value(), config, stopToken);
      if (!share) return fail<Output>(share.error());
      measured.voicedShare = share.value();
    }
  }
  if (stopToken.stop_requested()) return cancelled();
  TakeInspection result{
      .policy = request.policy,
      .sourceSha256 = after.value(),
      .byteSize = size,
      .measurements = measured,
      .checks = evaluateTakeChecks(request.policy, measured),
  };
  return result;
}

std::string_view takeQcPolicyName(TakeQcPolicy policy) noexcept {
  switch (policy) {
    case TakeQcPolicy::Voiced: return "voiced";
    case TakeQcPolicy::Breath: return "breath";
    case TakeQcPolicy::Closure: return "closure";
    case TakeQcPolicy::Pause: return "pause";
  }
  return "voiced";
}

std::optional<TakeQcPolicy> parseTakeQcPolicy(std::string_view value) noexcept {
  if (value == "voiced") return TakeQcPolicy::Voiced;
  if (value == "breath") return TakeQcPolicy::Breath;
  if (value == "closure") return TakeQcPolicy::Closure;
  if (value == "pause") return TakeQcPolicy::Pause;
  return std::nullopt;
}

std::string_view takeCheckName(TakeCheck check) noexcept {
  switch (check) {
    case TakeCheck::Format: return "format";
    case TakeCheck::Finite: return "finite";
    case TakeCheck::Clipping: return "clipping";
    case TakeCheck::DcOffset: return "dcOffset";
    case TakeCheck::SignalPresent: return "signalPresent";
    case TakeCheck::Quiet: return "quiet";
    case TakeCheck::Unvoiced: return "unvoiced";
    case TakeCheck::RootPitch: return "rootPitch";
  }
  return "format";
}

std::string_view takeCheckOutcomeName(TakeCheckOutcome outcome) noexcept {
  switch (outcome) {
    case TakeCheckOutcome::Pass: return "PASS";
    case TakeCheckOutcome::Fail: return "FAIL";
    case TakeCheckOutcome::Inapplicable: return "INAPPLICABLE";
  }
  return "FAIL";
}

std::optional<TakeCheckOutcome> parseTakeCheckOutcome(std::string_view value) noexcept {
  if (value == "PASS") return TakeCheckOutcome::Pass;
  if (value == "FAIL") return TakeCheckOutcome::Fail;
  if (value == "INAPPLICABLE") return TakeCheckOutcome::Inapplicable;
  return std::nullopt;
}

}  // namespace seam::voicebank
