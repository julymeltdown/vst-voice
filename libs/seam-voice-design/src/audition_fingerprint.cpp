#include "seam/voice_design/audition_fingerprint.hpp"
#include "seam/voice_design/phonation_source.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voice_design/vocal_tract.hpp"
#include "seam/core/sha256.hpp"
#include "seam/domain/project.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/phonemizer/phonemizer.hpp"
#include "seam/synthesis/performance_compiler.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <set>
#include <sstream>

namespace seam::voice_design {
namespace {
constexpr std::string_view kManifestFormat{"com.project-seam.audition-fingerprints"};
constexpr std::string_view kNotReviewed{"NOT_REVIEWED"};
constexpr std::string_view kUnmeasured{"UNMEASURED"};
constexpr std::string_view kEngineeringFixture{"ENGINEERING_FIXTURE"};
constexpr std::string_view kListeningStatement{"Reproducibility evidence for rendered bytes and signal features only. No listener has reviewed these auditions; nothing here is an intelligibility, identity, quality or acceptance judgement."};
constexpr std::string_view kAnalysisDescription{"Steady middle half; 4096-point Hann spectra at a 2048-frame hop; twelve log-spaced bands from 80 Hz; autocorrelation F0 within seven semitones of the score pitch."};
constexpr std::string_view kRepeatabilityStatement{"Every case rendered twice in the generating process was bit-identical. The bounds are declared engineering margins, not a measured spread."};
constexpr std::string_view kCrossPlatformStatement{"No tolerance is claimed for another platform or toolchain until its deviation has been measured and recorded."};
constexpr std::size_t kFftSize{4096U};
constexpr std::size_t kFftHop{2048U};
constexpr double kLowestBandHz{80.0};
constexpr std::size_t kMaximumCases{1024U};

double midiHz(std::uint8_t key) noexcept { return 440.0 * std::exp2((static_cast<double>(key) - 69.0) / 12.0); }

std::string pcmDigest(std::span<const float> samples) {
  core::Sha256 digest;
  std::array<std::byte, 4096U> block{};
  std::size_t used = 0U;
  for (const float sample : samples) {
    const auto bits = std::bit_cast<std::uint32_t>(sample);
    for (unsigned shift = 0U; shift < 32U; shift += 8U) block[used++] = static_cast<std::byte>((bits >> shift) & 0xFFU);
    if (used == block.size()) {
      digest.update(std::span<const std::byte>{block.data(), used});
      used = 0U;
    }
  }
  if (used != 0U) digest.update(std::span<const std::byte>{block.data(), used});
  return digest.hexDigest();
}

void transform(std::vector<std::complex<double>>& data) {
  const auto size = data.size();
  for (std::size_t index = 1U, reversed = 0U; index < size; ++index) {
    auto bit = size >> 1U;
    for (; (reversed & bit) != 0U; bit >>= 1U) reversed ^= bit;
    reversed ^= bit;
    if (index < reversed) std::swap(data[index], data[reversed]);
  }
  for (std::size_t length = 2U; length <= size; length <<= 1U) {
    const auto angle = -2.0 * std::numbers::pi / static_cast<double>(length);
    const std::complex<double> step{std::cos(angle), std::sin(angle)};
    for (std::size_t start = 0U; start < size; start += length) {
      std::complex<double> twiddle{1.0, 0.0};
      for (std::size_t offset = 0U; offset < length / 2U; ++offset) {
        const auto even = data[start + offset];
        const auto odd = data[start + offset + length / 2U] * twiddle;
        data[start + offset] = even + odd;
        data[start + offset + length / 2U] = even - odd;
        twiddle *= step;
      }
    }
  }
}

double median(std::vector<double> values) {
  if (values.empty()) return 0.0;
  const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2U);
  std::nth_element(values.begin(), middle, values.end());
  return *middle;
}

bool validHash(std::string_view value) noexcept {
  return value.size() == 64U && std::all_of(value.begin(), value.end(), [](char character) {
    return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
  });
}

core::Result<void> invalidManifest(std::string message) {
  return core::failure(core::ErrorCode::InvalidArgument, "Audition fingerprint manifest " + std::move(message));
}

formats::JsonValue fingerprintJson(const AuditionFingerprint& value) {
  formats::JsonValue::Array bands;
  for (const auto level : value.bandLevelsDb) bands.emplace_back(level);
  return formats::JsonValue{formats::JsonValue::Object{
      {"pcmSha256", formats::JsonValue{value.pcmSha256}},
      {"frames", formats::JsonValue{static_cast<std::int64_t>(value.frames)}},
      {"peak", formats::JsonValue{value.peak}}, {"rmsDbfs", formats::JsonValue{value.rmsDbfs}},
      {"f0Hz", formats::JsonValue{value.f0Hz}}, {"periodicity", formats::JsonValue{value.periodicity}},
      {"spectralCentroidHz", formats::JsonValue{value.spectralCentroidHz}},
      {"bandLevelsDb", formats::JsonValue{std::move(bands)}}}};
}

formats::JsonValue toleranceJson(const AuditionTolerance& value) {
  return formats::JsonValue{formats::JsonValue::Object{
      {"frames", formats::JsonValue{std::int64_t{0}}},
      {"peakRelative", formats::JsonValue{value.peakRelative}}, {"rmsDb", formats::JsonValue{value.rmsDb}},
      {"f0Cents", formats::JsonValue{value.f0Cents}}, {"periodicity", formats::JsonValue{value.periodicity}},
      {"centroidRelative", formats::JsonValue{value.centroidRelative}},
      {"bandLevelDb", formats::JsonValue{value.bandLevelDb}}}};
}

const formats::JsonValue* member(const formats::JsonValue& object, std::string_view key) {
  return object.isObject() ? object.find(key) : nullptr;
}

core::Result<std::string> text(const formats::JsonValue& object, std::string_view key) {
  const auto* value = member(object, key);
  if (value == nullptr || !value->isString() || value->asString().empty() || value->asString().size() > 4096U)
    return core::failure<std::string>(core::ErrorCode::InvalidArgument,
        "Audition fingerprint manifest field '" + std::string{key} + "' must be bounded text");
  return value->asString();
}

core::Result<double> number(const formats::JsonValue& object, std::string_view key) {
  const auto* value = member(object, key);
  if (value == nullptr || !value->isNumber() || !std::isfinite(value->asNumber()))
    return core::failure<double>(core::ErrorCode::InvalidArgument,
        "Audition fingerprint manifest field '" + std::string{key} + "' must be a finite number");
  return value->asNumber();
}

core::Result<std::int64_t> integer(const formats::JsonValue& object, std::string_view key) {
  const auto* value = member(object, key);
  if (value == nullptr || !value->isInteger())
    return core::failure<std::int64_t>(core::ErrorCode::InvalidArgument,
        "Audition fingerprint manifest field '" + std::string{key} + "' must be an integer");
  return value->asInt64();
}

core::Result<AuditionFingerprint> decodeFingerprint(const formats::JsonValue& object) {
  using Output = AuditionFingerprint;
  AuditionFingerprint result;
  auto hash = text(object, "pcmSha256");
  if (!hash) return core::Result<Output>{hash.error()};
  if (!validHash(hash.value())) return core::failure<Output>(core::ErrorCode::InvalidArgument,
      "Audition fingerprint digest must be lowercase SHA-256 hex");
  result.pcmSha256 = std::move(hash).value();
  const auto frames = integer(object, "frames");
  if (!frames) return core::Result<Output>{frames.error()};
  if (frames.value() <= 0 || frames.value() > 48000LL * 600LL)
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Audition fingerprint frame count is out of bounds");
  result.frames = static_cast<std::uint64_t>(frames.value());
  for (const auto& [key, target] : {std::pair{"peak", &result.peak}, std::pair{"rmsDbfs", &result.rmsDbfs},
           std::pair{"f0Hz", &result.f0Hz}, std::pair{"periodicity", &result.periodicity},
           std::pair{"spectralCentroidHz", &result.spectralCentroidHz}}) {
    const auto value = number(object, key);
    if (!value) return core::Result<Output>{value.error()};
    *target = value.value();
  }
  const auto* bands = member(object, "bandLevelsDb");
  if (bands == nullptr || !bands->isArray() || bands->asArray().size() != kAuditionBandCount)
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Audition fingerprint needs twelve band levels");
  for (std::size_t index = 0U; index < kAuditionBandCount; ++index) {
    const auto& band = bands->asArray()[index];
    if (!band.isNumber() || !std::isfinite(band.asNumber()))
      return core::failure<Output>(core::ErrorCode::InvalidArgument, "Audition fingerprint band levels must be finite");
    result.bandLevelsDb[index] = band.asNumber();
  }
  if (result.peak <= 0.0 || result.f0Hz <= 0.0 || result.spectralCentroidHz <= 0.0)
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Audition fingerprint describes silence or no pitch");
  return result;
}
}  // namespace

core::Result<std::vector<float>> finalizeAuditionPcm(std::vector<float> samples, std::stop_token stopToken) {
  using Output = std::vector<float>;
  float peak = 0.0F;
  for (const auto sample : samples) {
    if (!std::isfinite(sample)) return core::failure<Output>(core::ErrorCode::InvalidState, "Designer preview produced non-finite audio");
    peak = std::max(peak, std::abs(sample));
  }
  const auto gain = peak > 0.9F ? 0.9F / peak : 1.0F;
  for (std::size_t index = 0U; index < samples.size(); ++index) {
    const auto edge = std::min(index, samples.size() - 1U - index);
    samples[index] *= gain * std::min(1.0F, static_cast<float>(edge) / 240.0F);
  }
  if (stopToken.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Designer audition cancelled");
  return samples;
}

core::Result<std::vector<float>> renderPoseAudition(const VoiceRecipe& recipe, std::string_view phone,
    std::string_view style, std::uint8_t midiKey, std::stop_token stopToken) {
  using Output = std::vector<float>;
  if (midiKey < 36U || midiKey > 96U)
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Select a recipe pose and an audition pitch from MIDI 36 to 96");
  domain::Project project{domain::ProjectId{1U}, "Designer audition"};
  domain::VocalRegion region{.id = domain::RegionId{3U}, .name = "Sustained pose", .durationTick = time::Tick{1920},
      .lyrics = {{domain::LyricTokenId{4U}, U"\u3042", domain::Language::Japanese}},
      .notes = {{.id = domain::NoteId{5U}, .durationTick = time::Tick{1920}, .midiKey = midiKey, .lyricTokenId = domain::LyricTokenId{4U}}}};
  project.vocalTracks().push_back({.id = domain::TrackId{2U}, .name = "Draft voice", .regions = {region}});
  const auto performance = synthesis::compileScorePerformance(project, region, kPoseAuditionSampleRate);
  if (!performance) return core::Result<Output>{performance.error()};
  auto source = PhonationSource::create(recipe, performance.value(), 0);
  if (!source) return core::Result<Output>{source.error()};
  auto tract = VocalTract::create(recipe, phone, style, kPoseAuditionSampleRate);
  if (!tract) return core::Result<Output>{tract.error()};
  auto excitation = source.value().render(kPoseAuditionFrames, stopToken);
  if (!excitation) return core::Result<Output>{excitation.error()};
  auto filtered = tract.value().process(excitation.value().samples, stopToken);
  if (!filtered) return core::Result<Output>{filtered.error()};
  return finalizeAuditionPcm(std::move(filtered.value()), stopToken);
}

core::Result<AuditionFingerprint> fingerprintAudition(std::span<const float> samples,
    std::uint32_t sampleRate, double expectedF0Hz, std::stop_token stopToken) {
  using Output = AuditionFingerprint;
  if (sampleRate < 8000U || sampleRate > 384000U || samples.size() < 4U * kFftSize ||
      samples.size() > static_cast<std::size_t>(sampleRate) * 600U || !std::isfinite(expectedF0Hz) ||
      expectedF0Hz < 20.0 || expectedF0Hz > 0.25 * static_cast<double>(sampleRate))
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Audition fingerprints need a supported rate, a plausible expected pitch and at least 16384 samples");
  AuditionFingerprint result;
  result.frames = samples.size();
  for (const auto sample : samples) {
    if (!std::isfinite(sample)) return core::failure<Output>(core::ErrorCode::InvalidArgument, "Audition contains non-finite samples");
    result.peak = std::max(result.peak, static_cast<double>(std::abs(sample)));
  }
  result.pcmSha256 = pcmDigest(samples);
  const auto quarter = samples.size() / 4U;
  const auto steady = samples.subspan(quarter, samples.size() - 2U * quarter);
  double energy = 0.0;
  for (const auto sample : steady) energy += static_cast<double>(sample) * static_cast<double>(sample);
  if (!(energy > 0.0)) return core::failure<Output>(core::ErrorCode::InvalidArgument, "Audition steady window is silent");
  result.rmsDbfs = 10.0 * std::log10(energy / static_cast<double>(steady.size()));

  const auto period = static_cast<double>(sampleRate) / expectedF0Hz;
  const auto shortest = std::max<std::size_t>(2U, static_cast<std::size_t>(std::floor(period / 1.5)));
  const auto longest = static_cast<std::size_t>(std::ceil(period * 1.5)) + 1U;
  if (longest * 2U >= steady.size()) return core::failure<Output>(core::ErrorCode::InvalidArgument,
      "Audition steady window is too short for its expected pitch");
  const auto length = steady.size() - longest - 1U;
  const auto correlation = [&](std::size_t lag) {
    double product = 0.0, first = 0.0, second = 0.0;
    for (std::size_t index = 0U; index < length; ++index) {
      const auto left = static_cast<double>(steady[index]);
      const auto right = static_cast<double>(steady[index + lag]);
      product += left * right; first += left * left; second += right * right;
    }
    return first > 0.0 && second > 0.0 ? product / std::sqrt(first * second) : 0.0;
  };
  std::size_t bestLag = shortest;
  double best = -2.0;
  for (std::size_t lag = shortest; lag < longest; ++lag) {
    if (stopToken.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Audition fingerprint cancelled");
    const auto value = correlation(lag);
    if (value > best) { best = value; bestLag = lag; }
  }
  const auto before = correlation(bestLag - 1U), after = correlation(bestLag + 1U);
  const auto curvature = before - 2.0 * best + after;
  const auto offset = curvature < 0.0 ? std::clamp(0.5 * (before - after) / curvature, -1.0, 1.0) : 0.0;
  result.f0Hz = static_cast<double>(sampleRate) / (static_cast<double>(bestLag) + offset);
  result.periodicity = best;

  std::vector<double> power(kFftSize / 2U + 1U, 0.0);
  std::vector<std::complex<double>> frame(kFftSize);
  for (std::size_t start = 0U; start + kFftSize <= steady.size(); start += kFftHop) {
    if (stopToken.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Audition fingerprint cancelled");
    for (std::size_t index = 0U; index < kFftSize; ++index) {
      const auto window = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(index) / static_cast<double>(kFftSize));
      frame[index] = {static_cast<double>(steady[start + index]) * window, 0.0};
    }
    transform(frame);
    for (std::size_t bin = 0U; bin < power.size(); ++bin) power[bin] += std::norm(frame[bin]);
  }
  const auto binHz = static_cast<double>(sampleRate) / static_cast<double>(kFftSize);
  const auto top = std::min(16000.0, 0.45 * static_cast<double>(sampleRate));
  std::array<double, kAuditionBandCount + 1U> edges{};
  for (std::size_t index = 0U; index < edges.size(); ++index)
    edges[index] = kLowestBandHz * std::pow(top / kLowestBandHz, static_cast<double>(index) / static_cast<double>(kAuditionBandCount));
  std::array<double, kAuditionBandCount> bands{};
  double total = 0.0, weighted = 0.0;
  for (std::size_t bin = 1U; bin < power.size(); ++bin) {
    const auto frequency = static_cast<double>(bin) * binHz;
    if (frequency < edges.front() || frequency >= edges.back()) continue;
    total += power[bin];
    weighted += frequency * power[bin];
    const auto band = static_cast<std::size_t>(std::upper_bound(edges.begin(), edges.end(), frequency) - edges.begin()) - 1U;
    bands[std::min(band, kAuditionBandCount - 1U)] += power[bin];
  }
  if (!(total > 0.0)) return core::failure<Output>(core::ErrorCode::InvalidArgument, "Audition has no analysable spectrum");
  result.spectralCentroidHz = weighted / total;
  for (std::size_t index = 0U; index < kAuditionBandCount; ++index)
    result.bandLevelsDb[index] = 10.0 * std::log10(std::max(bands[index] / total, 1.0e-30));
  return result;
}

core::Result<void> compareAuditionFingerprint(const AuditionFingerprint& reference,
    const AuditionFingerprint& measured, const AuditionTolerance& tolerance) {
  const auto differ = [](std::string_view feature, double expected, double actual, double delta, double bound) {
    std::ostringstream message;
    message.precision(9);
    message << "Audition fingerprint " << feature << " differs by " << delta << " (reference " << expected
            << ", measured " << actual << ", tolerance " << bound << ")";
    return core::failure(core::ErrorCode::Conflict, message.str());
  };
  if (reference.frames != measured.frames)
    return differ("frames", static_cast<double>(reference.frames), static_cast<double>(measured.frames),
        std::abs(static_cast<double>(reference.frames) - static_cast<double>(measured.frames)), 0.0);
  if (!validHash(reference.pcmSha256) || !validHash(measured.pcmSha256) ||
      reference.pcmSha256 != measured.pcmSha256)
    return core::failure(core::ErrorCode::Conflict,
        "Audition fingerprint PCM SHA-256 differs; same-runtime reproduction must be byte-identical");
  const auto relative = [](double expected, double actual) {
    return std::abs(actual - expected) / std::max(std::abs(expected), 1.0e-12);
  };
  const auto check = [&](std::string_view feature, double expected, double actual, double delta, double bound) {
    return std::isfinite(delta) && delta <= bound ? core::success() : differ(feature, expected, actual, delta, bound);
  };
  if (auto result = check("peak", reference.peak, measured.peak, relative(reference.peak, measured.peak), tolerance.peakRelative); !result) return result;
  if (auto result = check("rmsDbfs", reference.rmsDbfs, measured.rmsDbfs, std::abs(measured.rmsDbfs - reference.rmsDbfs), tolerance.rmsDb); !result) return result;
  const auto cents = reference.f0Hz > 0.0 && measured.f0Hz > 0.0
      ? std::abs(1200.0 * std::log2(measured.f0Hz / reference.f0Hz)) : std::numeric_limits<double>::infinity();
  if (auto result = check("f0Hz (cents)", reference.f0Hz, measured.f0Hz, cents, tolerance.f0Cents); !result) return result;
  if (auto result = check("periodicity", reference.periodicity, measured.periodicity, std::abs(measured.periodicity - reference.periodicity), tolerance.periodicity); !result) return result;
  if (auto result = check("spectralCentroidHz", reference.spectralCentroidHz, measured.spectralCentroidHz,
          relative(reference.spectralCentroidHz, measured.spectralCentroidHz), tolerance.centroidRelative); !result) return result;
  for (std::size_t index = 0U; index < kAuditionBandCount; ++index) {
    const auto name = "bandLevelsDb[" + std::to_string(index) + "]";
    if (auto result = check(name, reference.bandLevelsDb[index], measured.bandLevelsDb[index],
            std::abs(measured.bandLevelsDb[index] - reference.bandLevelsDb[index]), tolerance.bandLevelDb); !result) return result;
  }
  return core::success();
}

DiscontinuityReport measureDiscontinuity(std::span<const float> samples, std::uint32_t sampleRate) {
  DiscontinuityReport report;
  for (const auto sample : samples) {
    if (!std::isfinite(sample)) { report.finite = false; return report; }
    report.peak = std::max(report.peak, static_cast<double>(std::abs(sample)));
  }
  const auto window = std::max<std::size_t>(8U, sampleRate / 1000U);
  if (samples.size() < 2U * window || report.peak == 0.0) return report;
  std::vector<double> energy;
  energy.reserve(samples.size() / window);
  double sum = 0.0;
  for (std::size_t start = 0U; start + window <= samples.size(); start += window) {
    double windowEnergy = 0.0;
    for (std::size_t index = std::max<std::size_t>(start, 2U); index < start + window; ++index) {
      const auto change = static_cast<double>(samples[index]) - 2.0 * static_cast<double>(samples[index - 1U]) +
          static_cast<double>(samples[index - 2U]);
      windowEnergy += change * change;
    }
    energy.push_back(std::sqrt(windowEnergy / static_cast<double>(window)));
    sum += windowEnergy;
  }
  const auto global = std::sqrt(sum / static_cast<double>(energy.size() * window));
  const auto floor = std::max(1.0e-12, 1.0e-3 * global);
  constexpr std::size_t context = 20U;
  std::vector<double> side;
  side.reserve(context);
  for (std::size_t index = 0U; index < energy.size(); ++index) {
    side.clear();
    for (std::size_t offset = 2U; offset <= context + 1U && offset <= index; ++offset) side.push_back(energy[index - offset]);
    const auto previous = median(side);
    side.clear();
    for (std::size_t offset = 2U; offset <= context + 1U && index + offset < energy.size(); ++offset) side.push_back(energy[index + offset]);
    const auto next = median(side);
    const auto ratio = energy[index] / std::max({previous, next, floor});
    if (ratio > report.worstRatio) {
      report.worstRatio = ratio;
      report.worstFrame = index * window;
    }
  }
  return report;
}

core::Result<std::vector<AuditionCase>> auditionCasesForRecipe(const VoiceRecipe& recipe,
    std::span<const std::uint8_t> midiKeys) {
  using Output = std::vector<AuditionCase>;
  if (midiKeys.empty() || midiKeys.size() > 16U || !std::is_sorted(midiKeys.begin(), midiKeys.end()) ||
      std::adjacent_find(midiKeys.begin(), midiKeys.end()) != midiKeys.end() ||
      std::any_of(midiKeys.begin(), midiKeys.end(), [](auto key) { return key < 36U || key > 96U; }))
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Audition keys must be one to sixteen ascending distinct MIDI keys from 36 to 96");
  Output cases;
  for (const auto& pose : recipe.poses) {
    if (!phonemizer::isVowelSymbol(pose.phone) && pose.phone != "N") continue;
    for (const auto key : midiKeys)
      cases.push_back({.id = pose.phone + "-" + pose.style + "-" + std::to_string(key), .phone = pose.phone,
          .style = pose.style, .midiKey = key, .fingerprint = {}});
  }
  if (cases.empty()) return core::failure<Output>(core::ErrorCode::Unsupported,
      "Recipe declares no oral vowel or syllabic nasal pose to audition");
  if (cases.size() > kMaximumCases) return core::failure<Output>(core::ErrorCode::Unsupported, "Too many audition cases");
  return cases;
}

std::string auditionMeasurementPlatform() {
  std::string platform =
#if defined(__APPLE__)
      "macOS";
#elif defined(_WIN32)
      "Windows";
#elif defined(__linux__)
      "Linux";
#else
      "unknown-os";
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
  platform += " arm64";
#elif defined(__x86_64__) || defined(_M_X64)
  platform += " x86_64";
#else
  platform += " unknown-architecture";
#endif
#if defined(__VERSION__)
  platform += "; ";
  platform += __VERSION__;
#endif
#if defined(NDEBUG)
  platform += "; optimized (NDEBUG)";
#else
  platform += "; assertions enabled";
#endif
  return platform;
}

core::Result<AuditionFingerprintManifest> buildAuditionFingerprintManifest(
    const synthesis::ProceduralSingerResource& resource, std::string recipeSource,
    std::span<const std::uint8_t> midiKeys, std::stop_token stopToken) {
  using Output = AuditionFingerprintManifest;
  const auto recipe = decodeVoiceRecipeResource(resource, stopToken);
  if (!recipe) return core::Result<Output>{recipe.error()};
  auto cases = auditionCasesForRecipe(recipe.value(), midiKeys);
  if (!cases) return core::Result<Output>{cases.error()};
  AuditionFingerprintManifest manifest{.recipeId = recipe.value().id, .recipeHash = resource.identity.contentHash,
      .recipeSource = std::move(recipeSource), .engineId = recipe.value().engineId, .seed = recipe.value().seed,
      .engineRevision = kSourceFilterEngineRevision, .auditionRevision = kPoseAuditionRevision, .tolerance = {},
      .measuredPlatform = auditionMeasurementPlatform(), .cases = std::move(cases).value()};
  for (auto& entry : manifest.cases) {
    auto first = renderPoseAudition(recipe.value(), entry.phone, entry.style, entry.midiKey, stopToken);
    if (!first) return core::Result<Output>{first.error()};
    const auto second = renderPoseAudition(recipe.value(), entry.phone, entry.style, entry.midiKey, stopToken);
    if (!second) return core::Result<Output>{second.error()};
    if (first.value() != second.value()) return core::failure<Output>(core::ErrorCode::InvalidState,
        "Audition " + entry.id + " is not bit-identical across two renders in one process");
    auto fingerprint = fingerprintAudition(first.value(), kPoseAuditionSampleRate, midiHz(entry.midiKey), stopToken);
    if (!fingerprint) return core::Result<Output>{fingerprint.error()};
    entry.fingerprint = std::move(fingerprint).value();
  }
  return manifest;
}

std::string encodeAuditionFingerprintManifest(const AuditionFingerprintManifest& manifest) {
  using formats::JsonValue;
  JsonValue::Array cases;
  for (const auto& entry : manifest.cases) {
    auto object = fingerprintJson(entry.fingerprint);
    object.asObject().emplace("id", JsonValue{entry.id});
    object.asObject().emplace("phone", JsonValue{entry.phone});
    object.asObject().emplace("style", JsonValue{entry.style});
    object.asObject().emplace("midiKey", JsonValue{static_cast<std::int64_t>(entry.midiKey)});
    cases.push_back(std::move(object));
  }
  JsonValue::Array unmeasured{JsonValue{"macOS x86_64"}, JsonValue{"Windows x64"}, JsonValue{"Linux x86_64"}};
  JsonValue::Object subject{
      {"recipeId", JsonValue{manifest.recipeId}}, {"recipeHash", JsonValue{manifest.recipeHash}},
      {"recipeSource", JsonValue{manifest.recipeSource}}, {"seed", JsonValue{std::to_string(manifest.seed)}},
      {"engineId", JsonValue{manifest.engineId}},
      {"engineRevision", JsonValue{static_cast<std::int64_t>(manifest.engineRevision)}}};
  JsonValue::Object audition{
      {"definition", JsonValue{"seam.designer-pose-audition"}},
      {"revision", JsonValue{static_cast<std::int64_t>(manifest.auditionRevision)}},
      {"sampleRate", JsonValue{static_cast<std::int64_t>(kPoseAuditionSampleRate)}},
      {"frames", JsonValue{static_cast<std::int64_t>(kPoseAuditionFrames)}},
      {"analysis", JsonValue{std::string{kAnalysisDescription}}}};
  JsonValue::Object crossPlatform{
      {"status", JsonValue{std::string{kUnmeasured}}},
      {"platforms", JsonValue{std::move(unmeasured)}},
      {"statement", JsonValue{std::string{kCrossPlatformStatement}}}};
  JsonValue::Object tolerance{
      {"sameRecipeSeedAndRuntime", toleranceJson(manifest.tolerance)},
      {"measuredOn", JsonValue{manifest.measuredPlatform}},
      {"repeatability", JsonValue{std::string{kRepeatabilityStatement}}},
      {"crossPlatform", JsonValue{std::move(crossPlatform)}}};
  const JsonValue document{JsonValue::Object{
      {"formatId", JsonValue{std::string{kManifestFormat}}},
      {"schemaVersion", JsonValue{std::int64_t{1}}},
      {"status", JsonValue{std::string{kEngineeringFixture}}},
      {"listening", JsonValue{std::string{kNotReviewed}}},
      {"statement", JsonValue{std::string{kListeningStatement}}},
      {"subject", JsonValue{std::move(subject)}},
      {"audition", JsonValue{std::move(audition)}},
      {"tolerance", JsonValue{std::move(tolerance)}},
      {"cases", JsonValue{std::move(cases)}}}};
  return formats::stringifyJson(document, true) + "\n";
}

core::Result<AuditionFingerprintManifest> decodeAuditionFingerprintManifest(std::string_view json) {
  using Output = AuditionFingerprintManifest;
  if (json.size() > 8U * 1024U * 1024U) return core::failure<Output>(core::ErrorCode::InvalidArgument,
      "Audition fingerprint manifest exceeds its size bound");
  const auto parsed = formats::parseJson(json);
  if (!parsed) return core::Result<Output>{parsed.error()};
  const auto& root = parsed.value();
  const auto fail = [](std::string message) { return core::Result<Output>{invalidManifest(std::move(message)).error()}; };
  const auto format = text(root, "formatId");
  const auto version = integer(root, "schemaVersion");
  const auto status = text(root, "status");
  const auto statement = text(root, "statement");
  if (!format || format.value() != kManifestFormat || !version || version.value() != 1 ||
      !status || status.value() != kEngineeringFixture || !statement || statement.value() != kListeningStatement)
    return fail("has an unsupported format or schema version");
  const auto listening = text(root, "listening");
  if (!listening || listening.value() != kNotReviewed)
    return fail("cannot carry a listening result; fingerprints are never listening evidence");
  const auto* subject = member(root, "subject");
  const auto* audition = member(root, "audition");
  const auto* tolerance = member(root, "tolerance");
  const auto* cases = member(root, "cases");
  if (subject == nullptr || audition == nullptr || tolerance == nullptr || cases == nullptr || !cases->isArray() ||
      cases->asArray().empty() || cases->asArray().size() > kMaximumCases)
    return fail("needs a subject, an audition definition, a tolerance and one to 1024 cases");
  AuditionFingerprintManifest manifest;
  for (const auto& [key, target] : {std::pair{"recipeId", &manifest.recipeId}, std::pair{"recipeHash", &manifest.recipeHash},
           std::pair{"recipeSource", &manifest.recipeSource}, std::pair{"engineId", &manifest.engineId}}) {
    auto value = text(*subject, key);
    if (!value) return core::Result<Output>{value.error()};
    *target = std::move(value).value();
  }
  if (!validHash(manifest.recipeHash)) return fail("recipe hash must be lowercase SHA-256 hex");
  const auto seed = text(*subject, "seed");
  if (!seed) return core::Result<Output>{seed.error()};
  const auto* begin = seed.value().data();
  const auto parsedSeed = std::from_chars(begin, begin + seed.value().size(), manifest.seed);
  if (parsedSeed.ec != std::errc{} || parsedSeed.ptr != begin + seed.value().size() ||
      std::to_string(manifest.seed) != seed.value()) return fail("seed must be canonical unsigned decimal text");
  const auto engineRevision = integer(*subject, "engineRevision");
  const auto auditionRevision = integer(*audition, "revision");
  const auto rate = integer(*audition, "sampleRate");
  const auto frames = integer(*audition, "frames");
  if (!engineRevision || engineRevision.value() <= 0 || engineRevision.value() > 1000000 || !auditionRevision ||
      auditionRevision.value() <= 0 || auditionRevision.value() > 1000000 || !rate ||
      rate.value() != static_cast<std::int64_t>(kPoseAuditionSampleRate) || !frames ||
      frames.value() != static_cast<std::int64_t>(kPoseAuditionFrames))
    return fail("revisions or audition shape are invalid");
  manifest.engineRevision = static_cast<std::uint32_t>(engineRevision.value());
  manifest.auditionRevision = static_cast<std::uint32_t>(auditionRevision.value());
  const auto* bounds = member(*tolerance, "sameRecipeSeedAndRuntime");
  const auto* crossPlatform = member(*tolerance, "crossPlatform");
  if (bounds == nullptr || crossPlatform == nullptr) return fail("tolerance needs its same-runtime and cross-platform parts");
  const auto repeatability = text(*tolerance, "repeatability");
  if (!repeatability || repeatability.value() != kRepeatabilityStatement)
    return fail("repeatability text must describe only the measured two-render comparison");
  const auto crossStatus = text(*crossPlatform, "status");
  const auto crossStatement = text(*crossPlatform, "statement");
  if (!crossStatus || crossStatus.value() != kUnmeasured || !crossStatement ||
      crossStatement.value() != kCrossPlatformStatement)
    return fail("cross-platform tolerance must stay UNMEASURED until a deviation is measured and recorded");
  const auto definition = text(*audition, "definition");
  const auto analysis = text(*audition, "analysis");
  if (!definition || definition.value() != "seam.designer-pose-audition" || !analysis ||
      analysis.value() != kAnalysisDescription)
    return fail("audition definition or analysis differs from the versioned implementation");
  const auto frameBound = integer(*bounds, "frames");
  if (!frameBound || frameBound.value() != 0) return fail("frame count tolerance must be exact");
  for (const auto& [key, target] : {std::pair{"peakRelative", &manifest.tolerance.peakRelative},
           std::pair{"rmsDb", &manifest.tolerance.rmsDb}, std::pair{"f0Cents", &manifest.tolerance.f0Cents},
           std::pair{"periodicity", &manifest.tolerance.periodicity},
           std::pair{"centroidRelative", &manifest.tolerance.centroidRelative},
           std::pair{"bandLevelDb", &manifest.tolerance.bandLevelDb}}) {
    const auto value = number(*bounds, key);
    if (!value) return core::Result<Output>{value.error()};
    if (value.value() < 0.0 || value.value() > 1.0) return fail("tolerance '" + std::string{key} + "' must lie in [0, 1]");
    *target = value.value();
  }
  const auto measured = text(*tolerance, "measuredOn");
  if (!measured) return core::Result<Output>{measured.error()};
  manifest.measuredPlatform = measured.value();
  std::set<std::string> identities;
  for (const auto& entry : cases->asArray()) {
    AuditionCase value;
    for (const auto& [key, target] : {std::pair{"id", &value.id}, std::pair{"phone", &value.phone}, std::pair{"style", &value.style}}) {
      auto field = text(entry, key);
      if (!field) return core::Result<Output>{field.error()};
      *target = std::move(field).value();
    }
    const auto key = integer(entry, "midiKey");
    if (!key || key.value() < 36 || key.value() > 96) return fail("case MIDI key must lie in 36..96");
    value.midiKey = static_cast<std::uint8_t>(key.value());
    if (value.id != value.phone + "-" + value.style + "-" + std::to_string(value.midiKey) || !identities.insert(value.id).second)
      return fail("case identity must be unique and name its phone, style and key");
    auto fingerprint = decodeFingerprint(entry);
    if (!fingerprint) return core::Result<Output>{fingerprint.error()};
    if (fingerprint.value().frames != kPoseAuditionFrames) return fail("case frame count differs from the audition definition");
    value.fingerprint = std::move(fingerprint).value();
    manifest.cases.push_back(std::move(value));
  }
  return manifest;
}

core::Result<void> verifyAuditionFingerprints(const AuditionFingerprintManifest& committed,
    const synthesis::ProceduralSingerResource& resource, std::stop_token stopToken) {
  const auto recipe = decodeVoiceRecipeResource(resource, stopToken);
  if (!recipe) return core::Result<void>{recipe.error()};
  if (committed.recipeHash != resource.identity.contentHash || committed.recipeId != recipe.value().id ||
      committed.seed != recipe.value().seed || committed.engineId != recipe.value().engineId)
    return core::failure(core::ErrorCode::Conflict,
        "Audition fingerprints describe another recipe identity or seed; regenerate them deliberately for a changed voice");
  if (committed.engineRevision != kSourceFilterEngineRevision || committed.auditionRevision != kPoseAuditionRevision)
    return core::failure(core::ErrorCode::Conflict,
        "Audition fingerprints were measured with another engine or audition revision; regenerate them deliberately");
  if (committed.measuredPlatform != auditionMeasurementPlatform())
    return core::failure(core::ErrorCode::Unsupported,
        "Audition fingerprints were measured on another platform or toolchain; cross-platform tolerance is UNMEASURED");
  std::vector<std::uint8_t> keys;
  keys.reserve(committed.cases.size());
  for (const auto& entry : committed.cases) keys.push_back(entry.midiKey);
  std::sort(keys.begin(), keys.end());
  keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
  const auto expected = auditionCasesForRecipe(recipe.value(), keys);
  if (!expected) return core::Result<void>{expected.error()};
  std::set<std::string> expectedIds;
  for (const auto& entry : expected.value()) expectedIds.insert(entry.id);
  std::set<std::string> committedIds;
  for (const auto& entry : committed.cases) {
    if (entry.id != entry.phone + "-" + entry.style + "-" + std::to_string(entry.midiKey) ||
        entry.midiKey < 36U || entry.midiKey > 96U || !committedIds.insert(entry.id).second)
      return core::failure(core::ErrorCode::InvalidArgument, "Audition fingerprint case identity is invalid or duplicated");
  }
  if (committedIds != expectedIds)
    return core::failure(core::ErrorCode::Conflict,
        "Audition fingerprint cases do not cover every oral vowel and syllabic N for their declared keys");
  for (const auto& entry : committed.cases) {
    const auto audio = renderPoseAudition(recipe.value(), entry.phone, entry.style, entry.midiKey, stopToken);
    if (!audio) return core::Result<void>{audio.error()};
    const auto measured = fingerprintAudition(audio.value(), kPoseAuditionSampleRate, midiHz(entry.midiKey), stopToken);
    if (!measured) return core::Result<void>{measured.error()};
    const auto compared = compareAuditionFingerprint(entry.fingerprint, measured.value(), committed.tolerance);
    if (!compared) return core::failure(compared.error().code, entry.id + ": " + compared.error().message);
  }
  return core::success();
}
}  // namespace seam::voice_design
