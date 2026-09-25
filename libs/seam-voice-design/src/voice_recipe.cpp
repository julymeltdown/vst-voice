#include "seam/voice_design/voice_recipe.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/text/unicode.hpp"
#include "seam/phonemizer/phonemizer.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>

namespace seam::voice_design {
namespace {
bool bounded(double value, double low, double high) { return std::isfinite(value) && value >= low && value <= high; }
bool text(const std::string& value) {
  return !value.empty() && value.size() <= 128U && seam::text::decodeUtf8Strict(value) && std::none_of(value.begin(), value.end(),
      [](char c) { return static_cast<unsigned char>(c) < 32U || c == 127; });
}
using J = formats::JsonValue;
bool fields(const J& value, std::initializer_list<std::string_view> names) {
  return value.isObject() && value.asObject().size() == names.size() &&
      std::all_of(names.begin(), names.end(), [&](auto name) { return value.find(name) != nullptr; });
}
bool number(const J& value, std::string_view key) { const auto* field = value.find(key); return field && field->isNumber(); }
core::Result<VoiceRecipe> malformed() { return core::failure<VoiceRecipe>(core::ErrorCode::ParseError, "Voice recipe fields are invalid"); }
bool parseSeed(const J& value, std::uint64_t& seed) {
  if (!value.isString() || value.asString().empty() || value.asString().size() > 20U) return false;
  const auto& text = value.asString();
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), seed);
  return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && text == std::to_string(seed);
}
}

VoiceRecipe makeJapaneseStarterRecipe(std::string id) {
  VoiceRecipe recipe;
  recipe.id = std::move(id);
  recipe.seed = 7130U;
  recipe.poses = {
      {"a", "neutral", 0.0, {{800.0, 90.0, 0.0}, {1250.0, 110.0, -3.0}, {2800.0, 160.0, -6.0}}},
      {"i", "neutral", 0.0, {{300.0, 70.0, 0.0}, {2300.0, 120.0, -3.0}, {3200.0, 170.0, -6.0}}},
      {"u", "neutral", 0.0, {{350.0, 80.0, 0.0}, {1100.0, 100.0, -3.0}, {2500.0, 160.0, -6.0}}},
      {"e", "neutral", 0.0, {{500.0, 80.0, 0.0}, {1900.0, 110.0, -3.0}, {2900.0, 160.0, -6.0}}},
      {"o", "neutral", 0.0, {{500.0, 90.0, 0.0}, {900.0, 110.0, -3.0}, {2600.0, 160.0, -6.0}}},
      {"m", "neutral", 0.85, {{300.0, 80.0, 0.0}, {1100.0, 110.0, -6.0}, {2500.0, 160.0, -9.0}},
       NasalResonance{280.0, 80.0, 1200.0, 120.0}},
      {"n", "neutral", 0.75, {{300.0, 80.0, 0.0}, {1700.0, 110.0, -6.0}, {2800.0, 160.0, -9.0}},
       NasalResonance{300.0, 90.0, 1500.0, 120.0}},
      {"r", "neutral", 0.0, {{400.0, 80.0, 0.0}, {1400.0, 110.0, -3.0}, {2200.0, 160.0, -6.0}}},
      {"w", "neutral", 0.0, {{300.0, 80.0, 0.0}, {610.0, 100.0, -3.0}, {2200.0, 160.0, -6.0}}},
      {"y", "neutral", 0.0, {{250.0, 70.0, 0.0}, {2200.0, 120.0, -3.0}, {3000.0, 170.0, -6.0}}},
      {"j", "neutral", 0.0, {{300.0, 80.0, 0.0}, {1900.0, 110.0, -3.0}, {2900.0, 160.0, -6.0}}},
      {"g", "neutral", 0.0, {{300.0, 80.0, 0.0}, {1700.0, 110.0, -3.0}, {2800.0, 160.0, -6.0}}},
      {"d", "neutral", 0.0, {{300.0, 80.0, 0.0}, {1700.0, 110.0, -3.0}, {2800.0, 160.0, -6.0}}},
      {"N", "neutral", 0.8, {{300.0, 80.0, 0.0}, {1500.0, 110.0, -6.0}, {2700.0, 160.0, -9.0}},
       NasalResonance{300.0, 90.0, 1500.0, 120.0}},
  };
  recipe.frications = {
      {"s", "neutral", FricationConfig{.seed = 7130U, .centerHz = 5500.0, .bandwidthHz = 3000.0, .gain = 0.12}},
      {"sh", "neutral", FricationConfig{.seed = 7131U, .centerHz = 3500.0, .bandwidthHz = 3000.0, .gain = 0.12}},
      {"h", "neutral", FricationConfig{.seed = 7132U, .centerHz = 1200.0, .bandwidthHz = 2400.0, .gain = 0.06}},
      {"f", "neutral", FricationConfig{.seed = 7139U, .centerHz = 5200.0, .bandwidthHz = 3500.0, .gain = 0.10}},
  };
  recipe.plosives = {
      {"t", "neutral", FricationConfig{.seed = 7133U, .centerHz = 4500.0, .bandwidthHz = 3000.0, .gain = 0.12}, 10.0},
      {"k", "neutral", FricationConfig{.seed = 7134U, .centerHz = 2500.0, .bandwidthHz = 2200.0, .gain = 0.12}, 10.0},
      {"p", "neutral", FricationConfig{.seed = 7135U, .centerHz = 1200.0, .bandwidthHz = 1800.0, .gain = 0.12}, 10.0},
      {"b", "neutral", FricationConfig{.seed = 7135U, .centerHz = 1200.0, .bandwidthHz = 1800.0, .gain = 0.12}, 10.0,
       VoiceRecipe::VoicedClosure{0.2, 400.0}},
      {"d", "neutral", FricationConfig{.seed = 7141U, .centerHz = 3000.0, .bandwidthHz = 2200.0, .gain = 0.12}, 10.0,
       VoiceRecipe::VoicedClosure{0.2, 400.0}},
      {"g", "neutral", FricationConfig{.seed = 7142U, .centerHz = 2400.0, .bandwidthHz = 2000.0, .gain = 0.12}, 10.0,
       VoiceRecipe::VoicedClosure{0.2, 400.0}},
  };
  recipe.affricates = {
      {"ts", "neutral", FricationConfig{.seed = 7143U, .centerHz = 6000.0, .bandwidthHz = 2600.0, .gain = 0.12},
       FricationConfig{.seed = 7144U, .centerHz = 6500.0, .bandwidthHz = 2400.0, .gain = 0.10}, 10.0},
      {"ch", "neutral", FricationConfig{.seed = 7145U, .centerHz = 3500.0, .bandwidthHz = 2600.0, .gain = 0.12},
       FricationConfig{.seed = 7146U, .centerHz = 3800.0, .bandwidthHz = 2800.0, .gain = 0.10}, 10.0},
  };
  recipe.poses.push_back({"z", "neutral", 0.0, {{300.0, 80.0, 0.0}, {1700.0, 110.0, -3.0}, {2800.0, 160.0, -6.0}}});
  recipe.frications.push_back({"z", "neutral",
      FricationConfig{.seed = 7136U, .centerHz = 5000.0, .bandwidthHz = 2500.0, .gain = 0.10}, 0.35});
  recipe.poses.push_back({"v", "neutral", 0.0, {{300.0, 80.0, 0.0}, {1500.0, 110.0, -3.0}, {2600.0, 160.0, -6.0}}});
  recipe.frications.push_back({"v", "neutral",
      FricationConfig{.seed = 7140U, .centerHz = 3500.0, .bandwidthHz = 2500.0, .gain = 0.10}, 0.30});
  recipe.voicedAffricates.push_back({"j", "neutral",
      FricationConfig{.seed = 7137U, .centerHz = 3000.0, .bandwidthHz = 2500.0, .gain = 0.12},
      FricationConfig{.seed = 7138U, .centerHz = 4500.0, .bandwidthHz = 3500.0, .gain = 0.12},
      12.0, 0.2, 400.0, 0.35});
  recipe.approximants = {{"r", "neutral", 45.0}, {"w", "neutral", 60.0}, {"y", "neutral", 40.0}};
  const auto palatalPose = [](std::string phone, double f1, double f2, double f3) {
    return VoicePose{std::move(phone), "neutral", 0.0,
        {{f1, 75.0, 0.0}, {f2, 105.0, -3.0}, {f3, 155.0, -6.0}}};
  };
  recipe.poses.push_back(palatalPose("ky", 280.0, 2100.0, 3100.0));
  recipe.poses.push_back(palatalPose("gy", 300.0, 1950.0, 2950.0));
  recipe.poses.push_back(palatalPose("ny", 280.0, 2050.0, 3000.0));
  recipe.poses.back().nasalCoupling = 0.8;
  recipe.poses.back().nasal = NasalResonance{300.0, 90.0, 1500.0, 120.0};
  recipe.poses.push_back(palatalPose("hy", 320.0, 2050.0, 3000.0));
  recipe.poses.push_back(palatalPose("by", 300.0, 1950.0, 2900.0));
  recipe.poses.push_back(palatalPose("py", 300.0, 2050.0, 3000.0));
  recipe.poses.push_back(palatalPose("my", 280.0, 2050.0, 3000.0));
  recipe.poses.back().nasalCoupling = 0.8;
  recipe.poses.back().nasal = NasalResonance{280.0, 80.0, 1450.0, 120.0};
  recipe.poses.push_back(palatalPose("ry", 330.0, 1900.0, 2900.0));
  recipe.poses.push_back(palatalPose("fy", 320.0, 2050.0, 3000.0));
  recipe.poses.push_back(palatalPose("vy", 320.0, 1900.0, 2800.0));
  recipe.palatalized = {
      {"ky", "neutral", "k"}, {"gy", "neutral", "g"}, {"ny", "neutral", "n"},
      {"hy", "neutral", "h"}, {"by", "neutral", "b"}, {"py", "neutral", "p"},
      {"my", "neutral", "m"}, {"ry", "neutral", "r"}, {"fy", "neutral", "f"},
      {"vy", "neutral", "v"},
  };
  recipe.closures = {{"cl", "neutral"}, {"pau", "neutral"}, {"R", "neutral"}, {"glottal", "neutral"}};
  recipe.breaths = {{"br", "neutral",
      FricationConfig{.seed = 7147U, .centerHz = 4200.0, .bandwidthHz = 7000.0, .gain = 0.08}}};
  return recipe;
}

core::Result<void> VoiceRecipe::validate() const {
  if (engineId != "seam.source-filter.v1") return core::failure(core::ErrorCode::Unsupported, "Voice recipe engine is unsupported");
  if (!text(id) || poses.empty() || poses.size() > 64U ||
      !bounded(phonation.openQuotient, 0.05, 0.95) || !bounded(phonation.spectralTiltDbPerOctave, -48.0, 0.0) ||
      !bounded(phonation.aspiration, 0.0, 1.0) || !bounded(modulation.jitterCents, 0.0, 100.0) ||
      !bounded(modulation.shimmerAmount, 0.0, 1.0) || !bounded(modulation.rateHz, 0.0, 20.0)) {
    return core::failure(core::ErrorCode::InvalidArgument, "Voice recipe source or modulation exceeds bounds");
  }
  std::set<std::pair<std::string, std::string>> identities;
  for (const auto& pose : poses) {
    if (!text(pose.phone) || !text(pose.style) || !identities.emplace(pose.phone, pose.style).second ||
        !bounded(pose.nasalCoupling, 0.0, 1.0) || pose.formants.size() < 3U || pose.formants.size() > 8U) {
      return core::failure(core::ErrorCode::InvalidArgument, "Voice recipe pose identity or resonance count is invalid");
    }
    double previous = 0.0;
    if (pose.nasal && (!bounded(pose.nasal->resonanceHz,50.0,4000.0) ||
        !bounded(pose.nasal->antiresonanceHz,50.0,16000.0) ||
        !bounded(pose.nasal->resonanceBandwidthHz,10.0,5000.0) ||
        !bounded(pose.nasal->antiresonanceBandwidthHz,10.0,5000.0)))
      return core::failure(core::ErrorCode::InvalidArgument,"Nasal resonance and antiresonance must be finite and bounded");
    for (const auto& band : pose.formants) {
      if (!bounded(band.frequencyHz, 50.0, 16000.0) || band.frequencyHz <= previous ||
          !bounded(band.bandwidthHz, 10.0, 5000.0) || !bounded(band.gainDb, -48.0, 24.0)) {
        return core::failure(core::ErrorCode::InvalidArgument, "Voice recipe resonance bands must be finite, bounded and ordered");
      }
      previous = band.frequencyHz;
    }
  }
  if (frications.size() > 64U) return core::failure(core::ErrorCode::InvalidArgument, "Too many recipe frication poses");
  identities.clear();
  for (const auto& pose : frications) {
    const auto& source = pose.source;
    if (!text(pose.phone) || !text(pose.style) || !identities.emplace(pose.phone, pose.style).second ||
        std::none_of(poses.begin(), poses.end(), [&](const auto& vowel) { return vowel.style == pose.style; }) ||
        !bounded(source.centerHz, 80.0, 16000.0) || !bounded(source.bandwidthHz, 20.0, 16000.0) ||
        !bounded(source.gain, 0.0, 0.25) || !bounded(source.centerHz / source.bandwidthHz, 0.25, 20.0))
      return core::failure(core::ErrorCode::InvalidArgument, "Recipe frication identity, style or source is invalid");
    if (pose.voicingGain && (!bounded(*pose.voicingGain,0.0,1.0) || *pose.voicingGain==0.0 ||
        phonemizer::isVowelSymbol(pose.phone) || phonemizer::isNasalSymbol(pose.phone) ||
        std::none_of(poses.begin(),poses.end(),[&](const auto& resonance){return resonance.phone==pose.phone && resonance.style==pose.style;})))
      return core::failure(core::ErrorCode::InvalidArgument,"Voiced frication requires a gain in (0,1] and an explicit same-phone/style resonance pose");
  }
  if (plosives.size()>64U) return core::failure(core::ErrorCode::InvalidArgument,"Too many plosive poses");
  for (const auto& pose:plosives) {
    const auto& source=pose.source;
    const bool validPhone=pose.voicedClosure ? (pose.phone=="b" || pose.phone=="d" || pose.phone=="g") :
        (pose.phone=="p" || pose.phone=="t" || pose.phone=="k");
    if (!validPhone || !text(pose.style) ||
        !identities.emplace(pose.phone,pose.style).second ||
        std::none_of(poses.begin(),poses.end(),[&](const auto& voice) { return voice.style==pose.style; }) ||
        !bounded(source.centerHz,80.0,16000.0) || !bounded(source.bandwidthHz,20.0,16000.0) ||
        !bounded(source.gain,0.0,0.25) || !bounded(source.centerHz/source.bandwidthHz,0.25,20.0) ||
        !bounded(pose.burstMilliseconds,1.0,100.0))
      return core::failure(core::ErrorCode::InvalidArgument,"Plosive identity, style, spectrum or duration is invalid or ambiguous with frication");
    if (pose.voicedClosure && (!bounded(pose.voicedClosure->gain,0.0,0.5) || pose.voicedClosure->gain==0.0 ||
        !bounded(pose.voicedClosure->lowpassHz,40.0,2000.0)))
      return core::failure(core::ErrorCode::InvalidArgument,"Voiced closure gain or lowpass cutoff is invalid");
  }
  if (affricates.size() > 64U) return core::failure(core::ErrorCode::InvalidArgument, "Too many affricate poses");
  for (const auto& pose : affricates) {
    // Only unvoiced affricates are admitted here. A voiced affricate is not an unvoiced noise pair
    // with a different label, so it has its own binding family below and this one keeps refusing
    // the symbol.
    const bool supportedPhone = pose.phone == "ts" || pose.phone == "ch";
    const auto spectrumIsBounded = [&](const FricationConfig& source) {
      return bounded(source.centerHz, 80.0, 16000.0) && bounded(source.bandwidthHz, 20.0, 16000.0) &&
             bounded(source.gain, 0.0, 0.25) && bounded(source.centerHz / source.bandwidthHz, 0.25, 20.0);
    };
    if (!supportedPhone || !text(pose.phone) || !text(pose.style) ||
        !identities.emplace(pose.phone, pose.style).second ||
        std::none_of(poses.begin(), poses.end(), [&](const auto& voice) { return voice.style == pose.style; }) ||
        !spectrumIsBounded(pose.burst) || !spectrumIsBounded(pose.tail) ||
        !bounded(pose.burstMilliseconds, 1.0, 100.0)) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "Affricate identity, style, burst or tail spectrum is invalid or ambiguous");
    }
  }
  if (voicedAffricates.size() > 64U) return core::failure(core::ErrorCode::InvalidArgument,
      "Too many voiced affricate poses");
  for (const auto& pose : voicedAffricates) {
    // A voiced affricate is a prevoiced closure, a burst and a voiced frication tail. The tail is
    // voiced through the tract, so this phone needs its own resonance pose, and the burst and tail
    // spectra are bounded exactly like every other noise source in a recipe.
    const auto spectrumIsBounded = [&](const FricationConfig& source) {
      return bounded(source.centerHz, 80.0, 16000.0) && bounded(source.bandwidthHz, 20.0, 16000.0) &&
             bounded(source.gain, 0.0, 0.25) && bounded(source.centerHz / source.bandwidthHz, 0.25, 20.0);
    };
    if (pose.phone != "j" || !text(pose.style) ||
        !identities.emplace(pose.phone, pose.style).second ||
        !spectrumIsBounded(pose.burst) || !spectrumIsBounded(pose.tail) ||
        !bounded(pose.burstMilliseconds, 1.0, 100.0) ||
        !bounded(pose.closureVoicingGain, 0.0, 0.5) || pose.closureVoicingGain == 0.0 ||
        !bounded(pose.closureLowpassHz, 40.0, 2000.0) ||
        !bounded(pose.tailVoicingGain, 0.0, 1.0) || pose.tailVoicingGain == 0.0 ||
        std::none_of(poses.begin(), poses.end(), [&](const auto& resonance) {
          return resonance.phone == pose.phone && resonance.style == pose.style; }))
      return core::failure(core::ErrorCode::InvalidArgument,
          "Voiced affricate identity, style, spectra, closure or resonance pose is invalid or ambiguous");
  }
  if (approximants.size() > 64U) return core::failure(core::ErrorCode::InvalidArgument, "Too many approximant poses");
  for (const auto& pose : approximants) {
    // Only the voiced approximants the pilot's language needs. Each one must own a resonance
    // pose, because the transition is between two resonance banks and not a new source.
    const bool supportedPhone = pose.phone == "r" || pose.phone == "w" || pose.phone == "y";
    if (!supportedPhone || !text(pose.phone) || !text(pose.style) ||
        !identities.emplace(pose.phone, pose.style).second ||
        std::none_of(poses.begin(), poses.end(), [&](const auto& voice) {
          return voice.phone == pose.phone && voice.style == pose.style;
        }) ||
        !bounded(pose.transitionMilliseconds, 5.0, 200.0)) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "Approximant identity, style, resonance pose or transition is invalid or ambiguous");
    }
  }
  if (palatalized.size() > 64U) return core::failure(core::ErrorCode::InvalidArgument,
      "Too many palatalized poses");
  for (const auto& pose : palatalized) {
    // The palatal shape is declared under the palatalized phone's own name, and the release comes
    // from a base that this recipe already binds. Without both, the phone would be the base sung
    // with a different label, which is exactly what must not be admitted.
    const auto sameStyle = [&](const auto& value) { return value.style == pose.style; };
    const bool baseIsNoise = std::any_of(frications.begin(), frications.end(), [&](const auto& source) {
             return sameStyle(source) && source.phone == pose.basePhone; }) ||
           std::any_of(plosives.begin(), plosives.end(), [&](const auto& source) {
             return sameStyle(source) && source.phone == pose.basePhone; }) ||
           std::any_of(affricates.begin(), affricates.end(), [&](const auto& source) {
             return sameStyle(source) && source.phone == pose.basePhone; });
    const bool baseIsResonant = std::any_of(approximants.begin(), approximants.end(), [&](const auto& source) {
             return sameStyle(source) && source.phone == pose.basePhone; }) ||
           std::any_of(poses.begin(), poses.end(), [&](const auto& resonance) {
             return sameStyle(resonance) && resonance.phone == pose.basePhone && resonance.nasal.has_value() &&
                    phonemizer::isNasalSymbol(resonance.phone); });
    if (!text(pose.phone) || !text(pose.style) || !text(pose.basePhone) || pose.phone == pose.basePhone ||
        !identities.emplace(pose.phone, pose.style).second || !(baseIsNoise || baseIsResonant) ||
        std::none_of(poses.begin(), poses.end(), [&](const auto& resonance) {
          return sameStyle(resonance) && resonance.phone == pose.phone; }))
      return core::failure(core::ErrorCode::InvalidArgument,
          "Palatalized identity, style, base source or resonance pose is invalid or ambiguous");
  }
  // An event phone is admitted only for the symbols whose own semantics it is, and only when the
  // recipe names it in a style it also declares. A closure carries no source at all: its whole
  // meaning is that the resolved span is silent, which is why an undeclared closure symbol is a
  // refusal rather than silence.
  if (closures.size() > 64U) return core::failure(core::ErrorCode::InvalidArgument, "Too many closure poses");
  for (const auto& pose : closures) {
    const bool supportedPhone = pose.phone == "cl" || pose.phone == "pau" || pose.phone == "R" ||
        pose.phone == "glottal";
    if (!supportedPhone || !text(pose.style) || !identities.emplace(pose.phone, pose.style).second ||
        std::none_of(poses.begin(), poses.end(), [&](const auto& voice) { return voice.style == pose.style; }))
      return core::failure(core::ErrorCode::InvalidArgument,
          "Closure identity, style or declaration is invalid or ambiguous");
  }
  if (breaths.size() > 64U) return core::failure(core::ErrorCode::InvalidArgument, "Too many breath poses");
  for (const auto& pose : breaths) {
    const auto& source = pose.source;
    if (pose.phone != "br" || !text(pose.style) || !identities.emplace(pose.phone, pose.style).second ||
        std::none_of(poses.begin(), poses.end(), [&](const auto& voice) { return voice.style == pose.style; }) ||
        !bounded(source.centerHz, 80.0, 16000.0) || !bounded(source.bandwidthHz, 20.0, 16000.0) ||
        !bounded(source.gain, 0.0, 0.25) || !bounded(source.centerHz / source.bandwidthHz, 0.25, 20.0))
      return core::failure(core::ErrorCode::InvalidArgument,
          "Breath identity, style or source spectrum is invalid or ambiguous");
  }
  return core::success();
}

std::int64_t voiceRecipeSchemaVersion(const VoiceRecipe& recipe) noexcept {
  if (!recipe.closures.empty() || !recipe.breaths.empty()) return 11;
  if (!recipe.voicedAffricates.empty()) return 10;
  if (!recipe.palatalized.empty()) return 9;
  if (!recipe.approximants.empty()) return 8;
  if (!recipe.affricates.empty()) return 7;
  if (std::any_of(recipe.plosives.begin(),recipe.plosives.end(),[](const auto& pose){return pose.voicedClosure.has_value();})) return 6;
  if (std::any_of(recipe.frications.begin(),recipe.frications.end(),[](const auto& pose){return pose.voicingGain.has_value();})) return 5;
  if (!recipe.plosives.empty()) return 4;
  return std::any_of(recipe.poses.begin(),recipe.poses.end(),[](const auto& pose) { return pose.nasal.has_value(); }) ? 3 : recipe.frications.empty() ? 1 : 2;
}

core::Result<std::string> encodeVoiceRecipe(const VoiceRecipe& recipe) {
  const auto valid = recipe.validate();
  if (!valid) return core::Result<std::string>{valid.error()};
  J::Array poses;
  const auto version = voiceRecipeSchemaVersion(recipe);
  for (const auto& pose : recipe.poses) {
    J::Array bands;
    for (const auto& band : pose.formants) bands.emplace_back(J::Object{
        {"frequencyHz", J{band.frequencyHz}}, {"bandwidthHz", J{band.bandwidthHz}}, {"gainDb", J{band.gainDb}}});
    J row{J::Object{{"phone", J{pose.phone}}, {"style", J{pose.style}},
        {"nasalCoupling", J{pose.nasalCoupling}}, {"formants", J{std::move(bands)}}}};
    if (version>=3) {
      J nasal;
      if (pose.nasal) nasal=J::Object{{"resonanceHz",pose.nasal->resonanceHz},{"resonanceBandwidthHz",pose.nasal->resonanceBandwidthHz},
          {"antiresonanceHz",pose.nasal->antiresonanceHz},{"antiresonanceBandwidthHz",pose.nasal->antiresonanceBandwidthHz}};
      row.asObject().emplace("nasal",std::move(nasal));
    }
    poses.push_back(std::move(row));
  }
  J root{J::Object{{"formatId", J{"com.project-seam.voice-recipe"}},
      {"schemaVersion", J{version}}, {"id", J{recipe.id}}, {"engineId", J{recipe.engineId}},
      {"seed", J{std::to_string(recipe.seed)}},
      {"phonation", J{J::Object{{"openQuotient", J{recipe.phonation.openQuotient}},
          {"spectralTiltDbPerOctave", J{recipe.phonation.spectralTiltDbPerOctave}}, {"aspiration", J{recipe.phonation.aspiration}}}}},
      {"modulation", J{J::Object{{"jitterCents", J{recipe.modulation.jitterCents}},
          {"shimmerAmount", J{recipe.modulation.shimmerAmount}}, {"rateHz", J{recipe.modulation.rateHz}}}}},
      {"poses", J{std::move(poses)}}}};
  if (version>=2) {
    J::Array frications;
    for (const auto& pose : recipe.frications) {
      J row{J::Object{
        {"phone", pose.phone}, {"style", pose.style}, {"seed", std::to_string(pose.source.seed)},
        {"centerHz", pose.source.centerHz}, {"bandwidthHz", pose.source.bandwidthHz}, {"gain", pose.source.gain}}};
      if (version>=5) row.asObject().emplace("voicingGain",pose.voicingGain?J{*pose.voicingGain}:J{});
      frications.push_back(std::move(row));
    }
    root.asObject().emplace("frications", std::move(frications));
  }
  if (version>=4) {
    J::Array plosives;
    for (const auto& pose:recipe.plosives) {
      J row{J::Object{
        {"phone",pose.phone},{"style",pose.style},{"seed",std::to_string(pose.source.seed)},
        {"centerHz",pose.source.centerHz},{"bandwidthHz",pose.source.bandwidthHz},{"gain",pose.source.gain},
        {"burstMilliseconds",pose.burstMilliseconds}}};
      if (version>=6) row.asObject().emplace("voicedClosure",pose.voicedClosure ?
          J{J::Object{{"gain",pose.voicedClosure->gain},{"lowpassHz",pose.voicedClosure->lowpassHz}}} : J{});
      plosives.push_back(std::move(row));
    }
    root.asObject().emplace("plosives",std::move(plosives));
  }
  if (version>=7) {
    J::Array affricates;
    for (const auto& pose : recipe.affricates) {
      const auto spectrum = [](const FricationConfig& source) {
        return J::Object{{"seed", std::to_string(source.seed)}, {"centerHz", J{source.centerHz}},
            {"bandwidthHz", J{source.bandwidthHz}}, {"gain", J{source.gain}}};
      };
      affricates.emplace_back(J::Object{{"phone", pose.phone}, {"style", pose.style},
          {"burstMilliseconds", J{pose.burstMilliseconds}}, {"burst", J{spectrum(pose.burst)}},
          {"tail", J{spectrum(pose.tail)}}});
    }
    root.asObject().emplace("affricates", std::move(affricates));
  }
  if (version>=8) {
    J::Array approximants;
    for (const auto& pose : recipe.approximants) {
      approximants.emplace_back(J::Object{{"phone", pose.phone}, {"style", pose.style},
          {"transitionMilliseconds", J{pose.transitionMilliseconds}}});
    }
    root.asObject().emplace("approximants", std::move(approximants));
  }
  if (version>=9) {
    J::Array palatalized;
    for (const auto& pose : recipe.palatalized) {
      palatalized.emplace_back(J::Object{{"phone", pose.phone}, {"style", pose.style},
          {"basePhone", pose.basePhone}});
    }
    root.asObject().emplace("palatalized", std::move(palatalized));
  }
  if (version>=10) {
    const auto spectrum = [](const FricationConfig& source) {
      return J::Object{{"seed", std::to_string(source.seed)}, {"centerHz", J{source.centerHz}},
          {"bandwidthHz", J{source.bandwidthHz}}, {"gain", J{source.gain}}};
    };
    J::Array voicedAffricates;
    for (const auto& pose : recipe.voicedAffricates) {
      voicedAffricates.emplace_back(J::Object{{"phone", pose.phone}, {"style", pose.style},
          {"burstMilliseconds", J{pose.burstMilliseconds}}, {"burst", J{spectrum(pose.burst)}},
          {"tail", J{spectrum(pose.tail)}}, {"closureVoicingGain", J{pose.closureVoicingGain}},
          {"closureLowpassHz", J{pose.closureLowpassHz}}, {"tailVoicingGain", J{pose.tailVoicingGain}}});
    }
    root.asObject().emplace("voicedAffricates", std::move(voicedAffricates));
  }
  if (version>=11) {
    J::Array closures;
    for (const auto& pose : recipe.closures) {
      closures.emplace_back(J::Object{{"phone", pose.phone}, {"style", pose.style}});
    }
    root.asObject().emplace("closures", std::move(closures));
    J::Array breaths;
    for (const auto& pose : recipe.breaths) {
      breaths.emplace_back(J::Object{{"phone", pose.phone}, {"style", pose.style},
          {"seed", std::to_string(pose.source.seed)}, {"centerHz", J{pose.source.centerHz}},
          {"bandwidthHz", J{pose.source.bandwidthHz}}, {"gain", J{pose.source.gain}}});
    }
    root.asObject().emplace("breaths", std::move(breaths));
  }
  return formats::stringifyJson(root);
}

core::Result<VoiceRecipe> decodeVoiceRecipe(std::string_view json) {
  const auto parsed = formats::parseJson(json, {.maximumInputBytes = 512U * 1024U, .maximumDepth = 6U,
      .maximumNodes = 8192U, .maximumStringBytes = 128U, .maximumCollectionEntries = 64U});
  if (!parsed) return core::Result<VoiceRecipe>{parsed.error()};
  const auto& root = parsed.value();
  if (!root.isObject() || !root.find("formatId") || !root.find("schemaVersion") ||
      !root.find("formatId")->isString() || root.find("formatId")->asString() != "com.project-seam.voice-recipe" ||
      !root.find("schemaVersion")->isInteger()) return malformed();
  const auto version = root.find("schemaVersion")->asInt64();
  if (version < 1 || version > 11) return core::failure<VoiceRecipe>(core::ErrorCode::Unsupported, "Voice recipe schema is unsupported");
  if (!(version == 1 ? fields(root, {"formatId", "schemaVersion", "id", "engineId", "seed", "phonation", "modulation", "poses"}) :
      version>=11 ? fields(root,{"formatId","schemaVersion","id","engineId","seed","phonation","modulation","poses","frications","plosives","affricates","approximants","palatalized","voicedAffricates","closures","breaths"}) :
      version>=10 ? fields(root,{"formatId","schemaVersion","id","engineId","seed","phonation","modulation","poses","frications","plosives","affricates","approximants","palatalized","voicedAffricates"}) :
      version>=9 ? fields(root,{"formatId","schemaVersion","id","engineId","seed","phonation","modulation","poses","frications","plosives","affricates","approximants","palatalized"}) :
      version>=8 ? fields(root,{"formatId","schemaVersion","id","engineId","seed","phonation","modulation","poses","frications","plosives","affricates","approximants"}) :
      version>=7 ? fields(root,{"formatId","schemaVersion","id","engineId","seed","phonation","modulation","poses","frications","plosives","affricates"}) :
      version>=4 ? fields(root,{"formatId","schemaVersion","id","engineId","seed","phonation","modulation","poses","frications","plosives"}) :
      fields(root, {"formatId", "schemaVersion", "id", "engineId", "seed", "phonation", "modulation", "poses", "frications"}))) return malformed();
  if (!root.find("id")->isString() || !root.find("engineId")->isString() || !root.find("seed")->isString() ||
      !root.find("poses")->isArray()) return malformed();
  VoiceRecipe recipe;
  recipe.id = root.find("id")->asString(); recipe.engineId = root.find("engineId")->asString();
  if (!parseSeed(*root.find("seed"), recipe.seed)) return malformed();
  const auto& p = *root.find("phonation"); const auto& m = *root.find("modulation");
  if (!fields(p, {"openQuotient", "spectralTiltDbPerOctave", "aspiration"}) ||
      !number(p, "openQuotient") || !number(p, "spectralTiltDbPerOctave") || !number(p, "aspiration") ||
      !fields(m, {"jitterCents", "shimmerAmount", "rateHz"}) || !number(m, "jitterCents") ||
      !number(m, "shimmerAmount") || !number(m, "rateHz")) return malformed();
  recipe.phonation = {p.find("openQuotient")->asNumber(), p.find("spectralTiltDbPerOctave")->asNumber(), p.find("aspiration")->asNumber()};
  recipe.modulation = {m.find("jitterCents")->asNumber(), m.find("shimmerAmount")->asNumber(), m.find("rateHz")->asNumber()};
  for (const auto& pose : root.find("poses")->asArray()) {
    if (!(version>=3 ? fields(pose,{"phone","style","nasalCoupling","formants","nasal"}) : fields(pose, {"phone", "style", "nasalCoupling", "formants"})) || !pose.find("phone")->isString() ||
        !pose.find("style")->isString() || !number(pose, "nasalCoupling") || !pose.find("formants")->isArray()) return malformed();
    VoicePose value{pose.find("phone")->asString(), pose.find("style")->asString(), pose.find("nasalCoupling")->asNumber(), {}};
    if (version>=3 && !pose.find("nasal")->isNull()) {
      const auto& nasal=*pose.find("nasal");
      if (!fields(nasal,{"resonanceHz","resonanceBandwidthHz","antiresonanceHz","antiresonanceBandwidthHz"}) ||
          !number(nasal,"resonanceHz") || !number(nasal,"resonanceBandwidthHz") || !number(nasal,"antiresonanceHz") || !number(nasal,"antiresonanceBandwidthHz")) return malformed();
      value.nasal=NasalResonance{nasal.find("resonanceHz")->asNumber(),nasal.find("resonanceBandwidthHz")->asNumber(),
          nasal.find("antiresonanceHz")->asNumber(),nasal.find("antiresonanceBandwidthHz")->asNumber()};
    }
    for (const auto& band : pose.find("formants")->asArray()) {
      if (!fields(band, {"frequencyHz", "bandwidthHz", "gainDb"}) || !number(band, "frequencyHz") ||
          !number(band, "bandwidthHz") || !number(band, "gainDb")) return malformed();
      value.formants.push_back({band.find("frequencyHz")->asNumber(), band.find("bandwidthHz")->asNumber(), band.find("gainDb")->asNumber()});
    }
    recipe.poses.push_back(std::move(value));
  }
  if (version >= 2) {
    const auto& list = *root.find("frications");
    if (!list.isArray() || (version==2 && list.asArray().empty())) return malformed();
    for (const auto& pose : list.asArray()) {
      if (!(version>=5?fields(pose,{"phone","style","seed","centerHz","bandwidthHz","gain","voicingGain"}):fields(pose, {"phone", "style", "seed", "centerHz", "bandwidthHz", "gain"})) ||
          !pose.find("phone")->isString() || !pose.find("style")->isString() ||
          !number(pose, "centerHz") || !number(pose, "bandwidthHz") || !number(pose, "gain")) return malformed();
      FricationConfig source;
      if (!parseSeed(*pose.find("seed"), source.seed)) return malformed();
      source.centerHz = pose.find("centerHz")->asNumber(); source.bandwidthHz = pose.find("bandwidthHz")->asNumber(); source.gain = pose.find("gain")->asNumber();
      recipe.frications.push_back({pose.find("phone")->asString(), pose.find("style")->asString(), source});
      if (version>=5 && !pose.find("voicingGain")->isNull()) {
        if (!pose.find("voicingGain")->isNumber()) return malformed();
        recipe.frications.back().voicingGain=pose.find("voicingGain")->asNumber();
      }
    }
  }
  if (version>=4) {
    const auto& list=*root.find("plosives");
    if (!list.isArray() || (version==4 && list.asArray().empty())) return malformed();
    for (const auto& pose:list.asArray()) {
      if (!(version>=6 ? fields(pose,{"phone","style","seed","centerHz","bandwidthHz","gain","burstMilliseconds","voicedClosure"}) :
          fields(pose,{"phone","style","seed","centerHz","bandwidthHz","gain","burstMilliseconds"})) ||
          !pose.find("phone")->isString() || !pose.find("style")->isString() ||
          !number(pose,"centerHz") || !number(pose,"bandwidthHz") || !number(pose,"gain") || !number(pose,"burstMilliseconds")) return malformed();
      FricationConfig source;
      if (!parseSeed(*pose.find("seed"),source.seed)) return malformed();
      source.centerHz=pose.find("centerHz")->asNumber(); source.bandwidthHz=pose.find("bandwidthHz")->asNumber(); source.gain=pose.find("gain")->asNumber();
      recipe.plosives.push_back({pose.find("phone")->asString(),pose.find("style")->asString(),source,pose.find("burstMilliseconds")->asNumber()});
      if (version>=6 && !pose.find("voicedClosure")->isNull()) {
        const auto& closure=*pose.find("voicedClosure");
        if (!fields(closure,{"gain","lowpassHz"}) || !number(closure,"gain") || !number(closure,"lowpassHz")) return malformed();
        recipe.plosives.back().voicedClosure=VoiceRecipe::VoicedClosure{closure.find("gain")->asNumber(),closure.find("lowpassHz")->asNumber()};
      }
    }
  }
  if (version>=7) {
    const auto& list=*root.find("affricates");
    if (!list.isArray()) return malformed();
    for (const auto& pose:list.asArray()) {
      if (!fields(pose,{"phone","style","burstMilliseconds","burst","tail"}) ||
          !pose.find("phone")->isString() || !pose.find("style")->isString() ||
          !number(pose,"burstMilliseconds")) return malformed();
      const auto parseSpectrum = [](const J& value, FricationConfig& source) {
        if (!fields(value,{"seed","centerHz","bandwidthHz","gain"}) ||
            !value.find("seed")->isString() || !number(value,"centerHz") ||
            !number(value,"bandwidthHz") || !number(value,"gain")) return false;
        if (!parseSeed(*value.find("seed"),source.seed)) return false;
        source.centerHz=value.find("centerHz")->asNumber();
        source.bandwidthHz=value.find("bandwidthHz")->asNumber();
        source.gain=value.find("gain")->asNumber();
        return true;
      };
      VoiceRecipe::AffricatePose row{pose.find("phone")->asString(),pose.find("style")->asString(),{},{},
          pose.find("burstMilliseconds")->asNumber()};
      if (!parseSpectrum(*pose.find("burst"),row.burst) || !parseSpectrum(*pose.find("tail"),row.tail)) return malformed();
      recipe.affricates.push_back(std::move(row));
    }
  }
  if (version>=8) {
    const auto& list=*root.find("approximants");
    if (!list.isArray()) return malformed();
    for (const auto& pose:list.asArray()) {
      if (!fields(pose,{"phone","style","transitionMilliseconds"}) ||
          !pose.find("phone")->isString() || !pose.find("style")->isString() ||
          !number(pose,"transitionMilliseconds")) return malformed();
      recipe.approximants.push_back({pose.find("phone")->asString(),
          pose.find("style")->asString(), pose.find("transitionMilliseconds")->asNumber()});
    }
  }
  if (version>=9) {
    const auto& list=*root.find("palatalized");
    if (!list.isArray()) return malformed();
    for (const auto& pose:list.asArray()) {
      if (!fields(pose,{"phone","style","basePhone"}) ||
          !pose.find("phone")->isString() || !pose.find("style")->isString() ||
          !pose.find("basePhone")->isString()) return malformed();
      recipe.palatalized.push_back({pose.find("phone")->asString(),
          pose.find("style")->asString(), pose.find("basePhone")->asString()});
    }
  }
  if (version>=10) {
    const auto& list=*root.find("voicedAffricates");
    if (!list.isArray()) return malformed();
    for (const auto& pose:list.asArray()) {
      if (!fields(pose,{"phone","style","burstMilliseconds","burst","tail","closureVoicingGain",
              "closureLowpassHz","tailVoicingGain"}) ||
          !pose.find("phone")->isString() || !pose.find("style")->isString() ||
          !number(pose,"burstMilliseconds") || !number(pose,"closureVoicingGain") ||
          !number(pose,"closureLowpassHz") || !number(pose,"tailVoicingGain")) return malformed();
      const auto parseSpectrum = [](const J& value, FricationConfig& source) {
        if (!fields(value,{"seed","centerHz","bandwidthHz","gain"}) ||
            !value.find("seed")->isString() || !number(value,"centerHz") ||
            !number(value,"bandwidthHz") || !number(value,"gain")) return false;
        if (!parseSeed(*value.find("seed"),source.seed)) return false;
        source.centerHz=value.find("centerHz")->asNumber();
        source.bandwidthHz=value.find("bandwidthHz")->asNumber();
        source.gain=value.find("gain")->asNumber();
        return true;
      };
      VoiceRecipe::VoicedAffricatePose row{pose.find("phone")->asString(),pose.find("style")->asString(),{},{},0.0,0.0,0.0,0.0};
      if (!parseSpectrum(*pose.find("burst"),row.burst) || !parseSpectrum(*pose.find("tail"),row.tail)) return malformed();
      row.burstMilliseconds=pose.find("burstMilliseconds")->asNumber();
      row.closureVoicingGain=pose.find("closureVoicingGain")->asNumber();
      row.closureLowpassHz=pose.find("closureLowpassHz")->asNumber();
      row.tailVoicingGain=pose.find("tailVoicingGain")->asNumber();
      recipe.voicedAffricates.push_back(std::move(row));
    }
  }
  if (version>=11) {
    const auto& closureList=*root.find("closures");
    if (!closureList.isArray()) return malformed();
    for (const auto& pose:closureList.asArray()) {
      if (!fields(pose,{"phone","style"}) || !pose.find("phone")->isString() ||
          !pose.find("style")->isString()) return malformed();
      recipe.closures.push_back({pose.find("phone")->asString(), pose.find("style")->asString()});
    }
    const auto& breathList=*root.find("breaths");
    if (!breathList.isArray()) return malformed();
    for (const auto& pose:breathList.asArray()) {
      if (!fields(pose,{"phone","style","seed","centerHz","bandwidthHz","gain"}) ||
          !pose.find("phone")->isString() || !pose.find("style")->isString() ||
          !number(pose,"centerHz") || !number(pose,"bandwidthHz") || !number(pose,"gain")) return malformed();
      FricationConfig source;
      if (!parseSeed(*pose.find("seed"),source.seed)) return malformed();
      source.centerHz=pose.find("centerHz")->asNumber();
      source.bandwidthHz=pose.find("bandwidthHz")->asNumber();
      source.gain=pose.find("gain")->asNumber();
      recipe.breaths.push_back({pose.find("phone")->asString(), pose.find("style")->asString(), source});
    }
  }
  const auto valid = recipe.validate();
  if (!valid) return core::Result<VoiceRecipe>{valid.error()};
  if (voiceRecipeSchemaVersion(recipe)!=version) return malformed();
  return recipe;
}
}
