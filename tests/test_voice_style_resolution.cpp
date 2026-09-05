#include "test_framework.hpp"

#include "seam/voicebank/style_resolution.hpp"

using seam::domain::VoiceStyleOrigin;
using seam::domain::VoiceStyleSelection;
using seam::voicebank::VoiceStyleStatus;

namespace {

seam::domain::VoicebankReference bankReference() {
  return {"style-test", "1.0.0", std::string(64U, 'a')};
}

seam::voicebank::VoicebankCandidate bankCandidate() {
  seam::voicebank::VoicebankCandidate candidate;
  candidate.manifest.id = "style-test";
  candidate.manifest.version = "1.0.0";
  candidate.manifest.displayName = "Style contract fixture";
  candidate.manifest.language = seam::domain::Language::Japanese;
  candidate.manifest.styles = {"soft", "original"};
  candidate.contentHash = std::string(64U, 'a');
  candidate.trust = seam::voicebank::VoicebankTrust::TrustedInstalled;
  return candidate;
}

seam::voicebank::VoicebankResolution resolveBank(
    const seam::voicebank::VoicebankCandidate& candidate) {
  return seam::voicebank::VoicebankCatalog{}.resolve(bankReference(), {candidate});
}

}

TEST_CASE("legacy style resolves the exact trusted bank first style without sorting") {
  const VoiceStyleSelection legacy{VoiceStyleOrigin::LegacyNeedsExactBankResolution, {}};
  const auto bank = resolveBank(bankCandidate());
  CHECK(bank.resolved());
  const auto resolved = seam::voicebank::resolveVoiceStyle(bankReference(), legacy, bank);
  CHECK(resolved);
  CHECK(resolved.value().status == VoiceStyleStatus::Resolved);
  CHECK(resolved.value().selection.styleId == "soft");
  CHECK(resolved.value().selection.origin == VoiceStyleOrigin::LegacyManifestFirst);
  CHECK(legacy.origin == VoiceStyleOrigin::LegacyNeedsExactBankResolution);
  CHECK(legacy.styleId.empty());
}

TEST_CASE("unavailable untrusted and mismatched banks cannot invent a legacy style") {
  const VoiceStyleSelection legacy{VoiceStyleOrigin::LegacyNeedsExactBankResolution, {}};
  const auto check = [&](const seam::domain::VoicebankReference& reference,
                          const seam::voicebank::VoicebankResolution& bank) {
    const auto result = seam::voicebank::resolveVoiceStyle(reference, legacy, bank);
    CHECK(result);
    CHECK(result.value().status == VoiceStyleStatus::BankUnresolved);
    CHECK(result.value().selection == legacy);
  };
  check(bankReference(), {});
  const auto exact = resolveBank(bankCandidate());
  for (const auto field : {&seam::domain::VoicebankReference::id,
                           &seam::domain::VoicebankReference::version,
                           &seam::domain::VoicebankReference::contentHash}) {
    auto reference = bankReference();
    reference.*field = "different";
    check(reference, exact);
  }
  auto candidate = bankCandidate();
  candidate.trust = seam::voicebank::VoicebankTrust::DevelopmentFixture;
  const auto development = resolveBank(candidate);
  CHECK(development.resolved());
  check(bankReference(), development);
  candidate.trust = seam::voicebank::VoicebankTrust::UntrustedInstalled;
  check(bankReference(), resolveBank(candidate));
  auto opaque = exact;
  opaque.candidate->contentHash = "legacy-opaque-value";
  auto reference = bankReference();
  reference.contentHash = "legacy-opaque-value";
  check(reference, opaque);
}

TEST_CASE("new multi style banks require a choice and sole styles are explicit") {
  const auto multiple = seam::voicebank::resolveVoiceStyle(
      bankReference(), {}, resolveBank(bankCandidate()));
  CHECK(multiple);
  CHECK(multiple.value().status == VoiceStyleStatus::NeedsChoice);
  CHECK(multiple.value().selection.styleId.empty());
  auto single = bankCandidate();
  single.manifest.styles = {"soft"};
  const auto resolved = seam::voicebank::resolveVoiceStyle(bankReference(), {}, resolveBank(single));
  CHECK(resolved);
  CHECK(resolved.value().status == VoiceStyleStatus::Resolved);
  CHECK(resolved.value().selection.styleId == "soft");
  CHECK(resolved.value().selection.origin == VoiceStyleOrigin::SoleDeclaredStyle);
}

TEST_CASE("missing selected style is preserved and never replaced by another style") {
  const VoiceStyleSelection explicitStyle{VoiceStyleOrigin::Explicit, "power"};
  const auto missing = seam::voicebank::resolveVoiceStyle(
      bankReference(), explicitStyle, resolveBank(bankCandidate()));
  CHECK(missing);
  CHECK(missing.value().status == VoiceStyleStatus::MissingStyle);
  CHECK(missing.value().selection == explicitStyle);
  const VoiceStyleSelection available{VoiceStyleOrigin::Explicit, "original"};
  const auto resolved = seam::voicebank::resolveVoiceStyle(
      bankReference(), available, resolveBank(bankCandidate()));
  CHECK(resolved);
  CHECK(resolved.value().status == VoiceStyleStatus::Resolved);
  CHECK(resolved.value().selection == available);
}

TEST_CASE("style selection rejects contradictory malformed and unknown intent") {
  CHECK(!VoiceStyleSelection(VoiceStyleOrigin::Explicit, "").validate());
  CHECK(!VoiceStyleSelection(VoiceStyleOrigin::Unselected, "soft").validate());
  CHECK(!VoiceStyleSelection(VoiceStyleOrigin::LegacyNeedsExactBankResolution, "soft").validate());
  CHECK(!VoiceStyleSelection(static_cast<VoiceStyleOrigin>(255), "soft").validate());
  CHECK(!VoiceStyleSelection(VoiceStyleOrigin::Explicit, std::string(1025U, 'a')).validate());
  CHECK(!VoiceStyleSelection(VoiceStyleOrigin::Explicit, "\xff").validate());
  auto candidate = bankCandidate();
  candidate.manifest.styles.clear();
  CHECK(!seam::voicebank::resolveVoiceStyle(bankReference(), {}, resolveBank(candidate)));
  candidate.manifest.styles = {"soft", "soft"};
  CHECK(!seam::voicebank::resolveVoiceStyle(bankReference(), {}, resolveBank(candidate)));
}
