#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/authoring/voicebank_browser.hpp"

#include <string>
#include <vector>

namespace {
seam::voicebank::VoicebankCandidate candidate(
    std::string id, std::string version, std::string name,
    seam::voicebank::VoicebankTrust trust, std::string hash,
    bool character = false, bool enabled = true) {
  auto manifest = seam::test::support::makeManifest({
      seam::test::support::makeUnit("a", {"a"}, "audio/a.wav", 60,
                                    seam::voicebank::UnitKind::Sustain),
      seam::test::support::makeUnit("release", {"a"}, "audio/r.wav", 67,
                                    seam::voicebank::UnitKind::Release),
  });
  manifest.id = std::move(id);
  manifest.version = std::move(version);
  manifest.displayName = std::move(name);
  manifest.styles = {"original", "soft"};
  manifest.units.back().enabled = enabled;
  if (character) {
    manifest.characterId = "character.one";
    manifest.characterVersion = "1.0.0";
  }
  return seam::voicebank::VoicebankCandidate{
      .manifest = std::move(manifest),
      .bankRoot = "/tmp/voicebank",
      .contentHash = std::move(hash),
      .trust = trust,
      .packageDigest = "package-digest",
      .signerKeyId = "signer-key",
  };
}
}  // namespace

TEST_CASE("voicebank_browser_sorts_trusted_then_development_then_untrusted") {
  std::vector<seam::voicebank::VoicebankCandidate> candidates;
  candidates.push_back(candidate("untrusted", "1.0.0", "Zulu",
      seam::voicebank::VoicebankTrust::UntrustedInstalled,
      std::string(64U, '3')));
  candidates.push_back(candidate("development", "1.0.0", "Beta",
      seam::voicebank::VoicebankTrust::DevelopmentFixture,
      std::string(64U, '2')));
  candidates.push_back(candidate("trusted", "1.0.0", "Alpha",
      seam::voicebank::VoicebankTrust::TrustedInstalled,
      std::string(64U, '1'), true, false));

  seam::authoring::VoicebankBrowserModel browser{/*allowDevelopmentFixtures=*/false};
  browser.rebuild(candidates);
  const auto& cards = browser.cards();
  CHECK(cards.size() == 3U);
  CHECK(cards[0].id == "trusted");
  CHECK(cards[1].id == "development");
  CHECK(cards[2].id == "untrusted");
  CHECK(cards[0].installed);
  CHECK(cards[0].selectable);
  CHECK(cards[0].characterAvailable);
  CHECK(cards[0].signerKeyId == "signer-key");
  CHECK(cards[0].contentHashAbbreviation == std::string(12U, '1'));
  CHECK(cards[0].enabledUnitCount == 1U);
  CHECK(cards[0].disabledUnitCount == 1U);
  CHECK(cards[0].hasSustain);
  CHECK(cards[0].hasRelease);
  CHECK(!cards[0].hasBreath);
  CHECK(!cards[1].installed);
  CHECK(!cards[1].selectable);
  CHECK(!cards[2].selectable);
}

TEST_CASE("voicebank_browser_development_override_changes_only_selectability") {
  const auto development = candidate(
      "development", "1.0.0", "Development Bank",
      seam::voicebank::VoicebankTrust::DevelopmentFixture,
      std::string(64U, 'a'), true);

  const std::vector candidates{development};
  seam::authoring::VoicebankBrowserModel denied{false};
  denied.rebuild(candidates);
  seam::authoring::VoicebankBrowserModel allowed{true};
  allowed.rebuild(candidates);

  CHECK(!denied.cards().front().selectable);
  CHECK(allowed.cards().front().selectable);
  CHECK(denied.cards().front().characterAvailable ==
        allowed.cards().front().characterAvailable);
  CHECK(denied.cards().front().enabledUnitCount ==
        allowed.cards().front().enabledUnitCount);
}
