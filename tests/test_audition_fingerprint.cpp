#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/voice_design/audition_fingerprint.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voice_design/voice_recipe.hpp"
#include "seam/formats/json_value.hpp"
#include <algorithm>
#include <array>
#include <limits>

namespace {

TEST_CASE("audition fingerprints identify deterministic pitch and reject byte drift") {
  using namespace seam;
  const auto samples = test::support::sineWave(48000U, 220.0, 1.0, 0.35F);
  const auto first = voice_design::fingerprintAudition(samples, 48000U, 220.0);
  const auto repeat = voice_design::fingerprintAudition(samples, 48000U, 220.0);
  CHECK(first); CHECK(repeat);
  CHECK(first.value() == repeat.value());
  CHECK(first.value().frames == 48000U);
  CHECK_NEAR(first.value().f0Hz, 220.0, 0.15);
  CHECK(first.value().periodicity > 0.99);
  CHECK(first.value().peak > 0.34 && first.value().peak < 0.36);
  CHECK(std::all_of(first.value().bandLevelsDb.begin(), first.value().bandLevelsDb.end(),
      [](double value) { return std::isfinite(value) && value <= 0.0; }));

  auto changed = samples;
  changed[24000U] += 0.0001F;
  const auto changedFingerprint = voice_design::fingerprintAudition(changed, 48000U, 220.0);
  CHECK(changedFingerprint);
  CHECK(changedFingerprint.value().pcmSha256 != first.value().pcmSha256);
  CHECK(!voice_design::compareAuditionFingerprint(first.value(), changedFingerprint.value(), {}));

  auto nonFinite = samples;
  nonFinite[100U] = std::numeric_limits<float>::quiet_NaN();
  CHECK(!voice_design::fingerprintAudition(nonFinite, 48000U, 220.0));
  CHECK(!voice_design::fingerprintAudition(std::span<const float>{samples.data(), 16383U}, 48000U, 220.0));
  std::stop_source cancelled;
  cancelled.request_stop();
  CHECK(!voice_design::fingerprintAudition(samples, 48000U, 220.0, cancelled.get_token()));
}

TEST_CASE("discontinuity screen responds to a click and refuses non-finite PCM") {
  using namespace seam;
  const auto clean = test::support::sineWave(48000U, 440.0, 1.0, 0.25F);
  const auto cleanReport = voice_design::measureDiscontinuity(clean, 48000U);
  CHECK(cleanReport.finite);
  CHECK(cleanReport.peak > 0.24 && cleanReport.peak < 0.26);

  auto clicked = clean;
  clicked[24000U] += 0.6F;
  const auto clickReport = voice_design::measureDiscontinuity(clicked, 48000U);
  CHECK(clickReport.finite);
  CHECK(clickReport.worstRatio > cleanReport.worstRatio);
  CHECK(clickReport.worstFrame >= 24000U - 48U && clickReport.worstFrame <= 24000U + 48U);

  auto invalid = clean;
  invalid[50U] = std::numeric_limits<float>::infinity();
  CHECK(!voice_design::measureDiscontinuity(invalid, 48000U).finite);
}

TEST_CASE("Japanese audition manifest covers every oral vowel and syllabic N without claiming review") {
  using namespace seam;
  const auto recipe = voice_design::makeJapaneseStarterRecipe("audition-fingerprint-test");
  const std::array<std::uint8_t, 3U> range{48U, 60U, 72U};
  const auto cases = voice_design::auditionCasesForRecipe(recipe, range);
  CHECK(cases);
  CHECK(cases.value().size() == 18U);
  CHECK(std::any_of(cases.value().begin(), cases.value().end(), [](const auto& entry) {
    return entry.phone == "N" && entry.midiKey == 60U;
  }));
  CHECK(std::any_of(cases.value().begin(), cases.value().end(), [](const auto& entry) {
    return entry.phone == "a" && entry.midiKey == 60U;
  }));
  const std::array<std::uint8_t, 0U> noKeys{};
  CHECK(!voice_design::auditionCasesForRecipe(recipe, noKeys));
  const std::array<std::uint8_t, 2U> duplicateKeys{60U, 60U};
  CHECK(!voice_design::auditionCasesForRecipe(recipe, duplicateKeys));
  const std::array<std::uint8_t, 2U> unorderedKeys{60U, 48U};
  CHECK(!voice_design::auditionCasesForRecipe(recipe, unorderedKeys));

  const auto resource = voice_design::freezeVoiceRecipeResource(recipe); CHECK(resource);
  const std::array<std::uint8_t, 1U> key{60U};
  const auto manifest = voice_design::buildAuditionFingerprintManifest(resource.value(),
      "tests/fixtures/voice-design/japanese-starter", key);
  CHECK(manifest);
  CHECK(manifest.value().recipeHash == resource.value().identity.contentHash);
  CHECK(manifest.value().engineRevision == voice_design::kSourceFilterEngineRevision);
  CHECK(manifest.value().auditionRevision == voice_design::kPoseAuditionRevision);
  CHECK(manifest.value().cases.size() == 6U);

  const auto encoded = voice_design::encodeAuditionFingerprintManifest(manifest.value());
  const auto decoded = voice_design::decodeAuditionFingerprintManifest(encoded);
  CHECK(decoded);
  CHECK(decoded.value() == manifest.value());
  CHECK(voice_design::verifyAuditionFingerprints(decoded.value(), resource.value()));

  auto incomplete = decoded.value();
  incomplete.cases.pop_back();
  CHECK(!voice_design::verifyAuditionFingerprints(incomplete, resource.value()));
  auto wrongPlatform = decoded.value();
  wrongPlatform.measuredPlatform += "; other toolchain";
  CHECK(!voice_design::verifyAuditionFingerprints(wrongPlatform, resource.value()));
  auto changedBytes = decoded.value();
  changedBytes.cases.front().fingerprint.pcmSha256.front() =
      changedBytes.cases.front().fingerprint.pcmSha256.front() == '0' ? '1' : '0';
  CHECK(!voice_design::verifyAuditionFingerprints(changedBytes, resource.value()));

  auto statusClaim = formats::parseJson(encoded); CHECK(statusClaim);
  statusClaim.value().asObject()["status"] = formats::JsonValue{"LISTENING_PASS"};
  CHECK(!voice_design::decodeAuditionFingerprintManifest(formats::stringifyJson(statusClaim.value())));
  auto listeningClaim = formats::parseJson(encoded); CHECK(listeningClaim);
  listeningClaim.value().asObject()["listening"] = formats::JsonValue{"REVIEWED_PASS"};
  CHECK(!voice_design::decodeAuditionFingerprintManifest(formats::stringifyJson(listeningClaim.value())));
  auto crossPlatformClaim = formats::parseJson(encoded); CHECK(crossPlatformClaim);
  crossPlatformClaim.value().asObject().at("tolerance").asObject().at("crossPlatform").asObject()["status"] =
      formats::JsonValue{"MEASURED_PASS"};
  CHECK(!voice_design::decodeAuditionFingerprintManifest(formats::stringifyJson(crossPlatformClaim.value())));
}

}  // namespace
