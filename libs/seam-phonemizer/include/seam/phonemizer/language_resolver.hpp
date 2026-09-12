#pragma once

#include "seam/phonemizer/pronunciation_resolver.hpp"

#include <string_view>
#include <vector>

namespace seam::phonemizer {

// A stable error discriminator for inspection-only UI. It does not authorize
// rendering or editing a region through an alternative language service.
inline constexpr std::string_view kMixedPronunciationLanguagesContext =
    "mixed-pronunciation-languages";

// All language values currently persisted by the domain have a registered
// resolver.  Unspecified deliberately means "use the historical Japanese
// default" at a region boundary; it is still a valid value for command-side
// validation and migration.
[[nodiscard]] constexpr bool hasPronunciationService(domain::Language language) noexcept {
  return language == domain::Language::Unspecified ||
      language == domain::Language::Japanese ||
      language == domain::Language::English ||
      language == domain::Language::Korean;
}

// Validate an explicit note hint with the inventory belonging to the note's
// language.  Keeping this dispatch beside resolvePronunciation prevents edit
// commands from silently applying Japanese inventory rules to another voice.
[[nodiscard]] core::Result<void> validatePhoneHintForLanguage(
    domain::Language language, std::string_view text);

// Versioned rule-backed language services. They preserve the same source,
// token-context and pronunciation-identity contract as the Japanese resolver;
// a production bank must still advertise matching vocabulary before rendering.
[[nodiscard]] core::Result<ResolvedPronunciation> resolveEnglishPronunciation(
    const domain::VocalRegion& region, std::stop_token stop = {});
[[nodiscard]] core::Result<ResolvedPronunciation> resolveKoreanPronunciation(
    const domain::VocalRegion& region, std::stop_token stop = {});

// Chooses one language service from the lyric metadata. Mixed explicit
// languages are rejected instead of silently applying the wrong vocabulary;
// an all-unspecified region retains the historical Japanese default.
[[nodiscard]] core::Result<ResolvedPronunciation> resolvePronunciation(
    const domain::VocalRegion& region, std::stop_token stop = {});
[[nodiscard]] core::Result<ResolvedPronunciation> resolvePronunciationForLanguage(
    const domain::VocalRegion& region, domain::Language language,
    std::stop_token stop = {});

[[nodiscard]] Result inspectEnglishPronunciation(
    const domain::VocalRegion& region);
[[nodiscard]] Result inspectKoreanPronunciation(
    const domain::VocalRegion& region);
[[nodiscard]] Result inspectPronunciation(const domain::VocalRegion& region);

}  // namespace seam::phonemizer
