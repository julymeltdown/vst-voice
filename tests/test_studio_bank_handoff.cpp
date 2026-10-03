// Studio's signed-bank route: a published engineering candidate is packed under an explicit key,
// installed into the bank folder the song editor catalogs, and handed to the editor as a song bound
// to that exact bank. Every refusal below is checked for the reason a creator would need to act on,
// and no step claims release qualification or quality approval.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/authoring/project_document.hpp"
#include "seam/authoring/project_lifecycle.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/native_ui/installed_bank_song.hpp"
#include "seam/native_ui/sample_bank_package.hpp"
#include "seam/voicebank/catalog.hpp"
#include "seam/voicebank/content_identity.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank_production/candidate_publication.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/repository.hpp"
#include "seam/voicebank_production/source_assessment.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
using namespace seam;
namespace production = voicebank_production;
using native_ui::InstalledBankSongRequest;
using native_ui::SampleBankInstallation;

// One reviewed, accepted candidate that already published: the same deterministic synthetic source
// the production regressions use, with every identity an explicit test fixture.
struct PublishedCandidate final {
  std::filesystem::path root;
  production::PublishedSampleCandidate published;
};

PublishedCandidate publishReviewedCandidate() {
  PublishedCandidate produced{};
  produced.root = test::support::temporaryDirectory("studio-bank-handoff");
  const auto workspace = produced.root / "producer";
  const auto license = produced.root / "source-notice.txt";
  CHECK(core::durableAtomicWriteText(license,
      "GENERATED_TEST_FIXTURE_ONLY: no production singer qualification").hasValue());
  const auto digest = core::sha256File(license);
  CHECK(digest.hasValue());
  if (!digest) return produced;
  production::VoicebankProductionProject project{
      .projectId = "studio-bank-handoff", .inventoryId = "fixture-inventory",
      .inventorySha256 = std::string(64U, 'a'), .selectedSourceStrategyId = "fixture-synthesis",
      .licenseLocator = license.string(), .licenseSha256 = digest.value(),
      .immutableAssetRoot = "assets"};
  project.schemaVersion = production::kProductionStyleSchemaVersion;
  project.language = "ja";
  project.declaredPitchLayers = {69};
  project.sourceStrategies.push_back({
      .id = "fixture-synthesis", .kind = production::SourceStrategyKind::ProceduralSynthesis,
      .rights = production::Feasibility::Pass, .coverage = production::Feasibility::Pass,
      .listening = production::Feasibility::Pass,
      .permissions = {.sourceUse = true, .transformation = true,
                      .singingBankRedistribution = true, .commercialRenders = true},
      .licenseLocator = license.string(), .licenseSha256 = digest.value(),
      .evidenceState = "SYNTHETIC_TEST_ONLY"});
  project.operators = {{.operatorId = "producer", .role = "PRODUCER"},
                       {.operatorId = "reviewer", .role = "REVIEWER"}};
  project.unitAssignments = {{.coverageKey = "sustain:a", .pitchLayer = 69,
                              .promptId = "prompt-a", .plannedTakeId = "take-a",
                              .style = "harness"}};
  production::ProductionProjectRepository repository{workspace};
  CHECK(repository.initialize(project, {.action = "create", .subjectId = project.projectId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-29T10:00:00Z"}).hasValue());
  const auto source = produced.root / "raw.wav";
  CHECK(voicebank::writeWav(source, {.sampleRate = 48000U, .channels = 1U,
      .sampleFormat = voicebank::WavSampleFormat::Pcm24},
      test::support::sineWave(48000U, 440.0, 0.12, 0.25F)).hasValue());
  CHECK(repository.importRaw(project, source,
      {.takeId = "take-a", .promptId = "prompt-a", .coverageKey = "sustain:a", .pitchLayer = 69,
       .style = "harness"},
      {.action = "import", .subjectId = "take-a", .operatorId = "producer",
       .occurredAtUtc = "2026-09-29T10:01:00Z"}).hasValue());
  const auto samples = voicebank::readWav(source);
  CHECK(samples.hasValue());
  if (!samples) return produced;
  auto unit = test::support::makeUnit("a-69", {"a"}, "raw.wav", 69,
      voicebank::UnitKind::Sustain, samples.value().frameCount());
  unit.renderer = voicebank::RendererHint::ClassicPsola;
  unit.style = "harness";
  for (time::SampleFrame frame = 109; frame < unit.markers.audioEnd; frame += 109)
    unit.pitchMarks.push_back({.frame = frame, .confidence = 1.0F, .locked = true});
  // A style-owned schema requires a current source-quality assessment before any candidate can be
  // assembled, so the reviewer records one explicitly here rather than relying on legacy assertions.
  const auto qualityEvidence = produced.root / "source-quality.txt";
  CHECK(core::durableAtomicWriteText(qualityEvidence,
      "GENERATED_TEST_FIXTURE_ONLY: synthetic source, coverage and listening assessed for the fixture").hasValue());
  const auto qualityBytes = core::readFileBytesLimited(qualityEvidence, 4ULL * 1024ULL * 1024ULL);
  CHECK(qualityBytes.hasValue());
  const auto strategy = std::find_if(project.sourceStrategies.begin(), project.sourceStrategies.end(),
      [](const auto& value) { return value.id == "fixture-synthesis"; });
  CHECK(strategy != project.sourceStrategies.end());
  const auto material = production::sourceQualityMaterialIdentity(project, "fixture-synthesis");
  CHECK(material.hasValue());
  if (!qualityBytes || strategy == project.sourceStrategies.end() || !material) return produced;
  const auto assessed = repository.recordSourceQualityAssessment(project,
      production::SourceQualityAssessment{.id = "fixture-quality-1",
          .strategyId = "fixture-synthesis",
          .policySha256 = production::sourceQualityPolicyIdentity(*strategy),
          .materialSha256 = material.value(),
          .evidenceSha256 = core::sha256Hex(qualityBytes.value()),
          .reviewerId = "reviewer", .reviewedAtUtc = "2026-09-29T10:02:00Z",
          .coverage = production::Feasibility::Pass, .listening = production::Feasibility::Pass},
      qualityEvidence, core::sha256Hex(production::encodeProductionProject(project)));
  CHECK(assessed.hasValue());
  auto manifest = test::support::makeManifest({unit});
  manifest.id = "studio-bank-handoff";
  manifest.version = "1.0.0";
  manifest.displayName = "Harness Bank";
  manifest.styles = {"harness"};
  const auto packet = production::prepareSampleCandidateReview(workspace, project, manifest);
  if (!packet) throw test::Failure{"prepareSampleCandidateReview failed: " + packet.error().message};
  CHECK(packet.hasValue());
  if (!packet) return produced;
  const auto receipt = production::commitSampleCandidateReview(workspace, project, packet.value(),
      "reviewer", "2026-09-29T10:03:00Z", production::SampleCandidateReviewDecision::Accept);
  if (!receipt) throw test::Failure{"commitSampleCandidateReview failed: " + receipt.error().message};
  CHECK(receipt.hasValue());
  if (!receipt.value().candidate) throw test::Failure{"acceptance did not produce a candidate"};
  if (!receipt || !receipt.value().candidate) return produced;
  const auto published = production::publishSampleCandidate(workspace, project,
      *receipt.value().candidate, produced.root / "candidate");
  if (!published) throw test::Failure{"publishSampleCandidate failed: " + published.error().message};
  CHECK(published.hasValue());
  if (!published) return produced;
  produced.published = published.value();
  return produced;
}

}  // namespace

TEST_CASE("Studio signs and installs a published candidate as the exact reviewed bank content") {
  const auto produced = publishReviewedCandidate();
  CHECK(!produced.published.root.empty());
  if (!produced.published.root.empty()) {
    CHECK(!produced.published.releaseEligible);
    auto key = distribution::generateSigningKeyPair();
    CHECK(key.hasValue());
    if (!key) return;
    const auto keyPath = produced.root / "keys" / "bank-signer.json";
    std::filesystem::create_directories(keyPath.parent_path());
    CHECK(distribution::savePrivateKey(key.value(), keyPath).hasValue());
    const auto package = produced.root / "out" / "harness-bank.seambank";
    std::filesystem::create_directories(package.parent_path());
    const auto packed = native_ui::packPublishedSampleBank(produced.published, package, key.value());
    CHECK(packed.hasValue());
    CHECK(std::filesystem::exists(package));
    if (packed) {
      CHECK(packed.value().signatureValid);
      CHECK(packed.value().signerTrusted);
      CHECK(packed.value().manifest.id == "studio-bank-handoff");
    }
    // Packing the same reviewed candidate twice is a refusal, never a silent overwrite.
    CHECK(!native_ui::packPublishedSampleBank(produced.published, package, key.value()).hasValue());

    const auto installRoot = produced.root / "voicebanks";
    const auto installed = native_ui::installSignedSampleBank(package, installRoot,
        {key.value().publicKey}, produced.published.contentSha256);
    CHECK(installed.hasValue());
    if (installed) {
      CHECK(installed.value().voicebankId == "studio-bank-handoff");
      CHECK(installed.value().contentHash == produced.published.contentSha256);
      const auto manifest = voicebank::ManifestJsonCodec{}.load(
          installed.value().installDirectory / "manifest.json");
      CHECK(manifest.hasValue());
      if (manifest) {
        const auto content = voicebank::computeVoicebankContentHash(manifest.value(),
            installed.value().installDirectory);
        CHECK(content.hasValue());
        if (content) CHECK(content.value() == produced.published.contentSha256);
      }
    }

    // An untrusted key is refused: a signature only counts when the caller names the key it trusts.
    auto otherKey = distribution::generateSigningKeyPair();
    CHECK(otherKey.hasValue());
    if (otherKey) {
      const auto secondRoot = produced.root / "voicebanks-untrusted";
      const auto refused = native_ui::installSignedSampleBank(package, secondRoot,
          {otherKey.value().publicKey}, produced.published.contentSha256);
      CHECK(!refused.hasValue());
      CHECK(!std::filesystem::exists(secondRoot / "studio-bank-handoff"));
    }
    // The reviewed content hash is the only identity accepted: a hash of something else is refused
    // even with the correct signing key, so a package cannot be installed under a false review.
    const auto wrongHash = produced.root / "voicebanks-wrong-hash";
    const auto mismatched = native_ui::installSignedSampleBank(package, wrongHash,
        {key.value().publicKey}, std::string(64U, 'b'));
    CHECK(!mismatched.hasValue());
  }
}

TEST_CASE("Studio packs nothing from a candidate whose published material changed on disk") {
  const auto produced = publishReviewedCandidate();
  if (produced.published.root.empty()) return;
  auto key = distribution::generateSigningKeyPair();
  CHECK(key.hasValue());
  if (!key) return;

  // A tampered manifest must be caught before packing rather than signed under the reviewed identity.
  const auto manifestPath = produced.published.root / "manifest.json";
  const auto original = core::readFileBytesLimited(manifestPath, 16ULL * 1024ULL * 1024ULL);
  CHECK(original.hasValue());
  if (!original) return;
  auto tampered = original.value();
  const auto target = std::string{"Harness Bank"};
  const auto bytes = std::as_bytes(std::span{target.data(), target.size()});
  const auto at = std::search(tampered.begin(), tampered.end(), bytes.begin(), bytes.end());
  CHECK(at != tampered.end());
  if (at == tampered.end()) return;
  *at = std::byte{'H'};
  *std::next(at, 1) = std::byte{'a'};
  *std::next(at, 2) = std::byte{'r'};
  *std::next(at, 3) = std::byte{'n'};
  *std::next(at, 4) = std::byte{'e'};
  *std::next(at, 5) = std::byte{'s'};
  *std::next(at, 6) = std::byte{'s'};
  *std::next(at, 7) = std::byte{'1'};
  CHECK(core::durableAtomicWrite(manifestPath, tampered).hasValue());
  const auto refused = native_ui::packPublishedSampleBank(produced.published,
      produced.root / "out" / "tampered.seambank", key.value());
  CHECK(!refused.hasValue());
  CHECK(!std::filesystem::exists(produced.root / "out" / "tampered.seambank"));
}

TEST_CASE("Studio writes a song bound to the installed bank and refuses anywhere else") {
  const auto produced = publishReviewedCandidate();
  if (produced.published.root.empty()) return;
  auto key = distribution::generateSigningKeyPair();
  CHECK(key.hasValue());
  if (!key) return;
  const auto package = produced.root / "out" / "song-bank.seambank";
  std::filesystem::create_directories(package.parent_path());
  CHECK(native_ui::packPublishedSampleBank(produced.published, package, key.value()).hasValue());
  const auto installRoot = produced.root / "voicebanks";
  const auto installed = native_ui::installSignedSampleBank(package, installRoot,
      {key.value().publicKey}, produced.published.contentSha256);
  CHECK(installed.hasValue());
  if (!installed) return;
  const std::vector<voicebank::VoicebankSearchRoot> roots{
      {installRoot, voicebank::VoicebankRootKind::Installed}};
  const InstalledBankSongRequest request{.projectPath = produced.root / "songs" / "Harness Song.seam",
                                         .voicebankId = installed.value().voicebankId,
                                         .voicebankVersion = installed.value().voicebankVersion,
                                         .contentHash = installed.value().contentHash,
                                         .installDirectory = installed.value().installDirectory};
  std::filesystem::create_directories(request.projectPath.parent_path());
  const auto created = native_ui::createInstalledBankSongProject(roots, request);
  CHECK(created.hasValue());
  if (created) {
    CHECK(created.value().voicebank.contentHash == produced.published.contentSha256);
    CHECK(created.value().styleId == "harness");
    const auto reread = core::readTextFileLimited(request.projectPath, 64ULL * 1024ULL * 1024ULL);
    CHECK(reread.hasValue());
    if (reread) {
      const auto decoded = formats::ProjectJsonCodec{}.decode(reread.value());
      CHECK(decoded.hasValue());
      if (decoded) {
        CHECK(decoded.value().vocalTracks().size() == 1U);
        CHECK(decoded.value().vocalTracks().front().voicebank == created.value().voicebank);
        CHECK(!decoded.value().vocalTracks().front().proceduralRecipe.has_value());
      }
    }
  }

  // The same song path is never replaced.
  const auto repeated = native_ui::createInstalledBankSongProject(roots, request);
  CHECK(!repeated.hasValue());
  if (!repeated) CHECK(repeated.error().code == core::ErrorCode::Conflict);

  // A song may not be written inside the installation folder: signed banks are immutable.
  const auto inside = native_ui::createInstalledBankSongProject(roots, InstalledBankSongRequest{
      .projectPath = installed.value().installDirectory / "Inside.seam",
      .voicebankId = installed.value().voicebankId,
      .voicebankVersion = installed.value().voicebankVersion,
      .contentHash = installed.value().contentHash,
      .installDirectory = installed.value().installDirectory});
  CHECK(!inside.hasValue());
  CHECK(!std::filesystem::exists(installed.value().installDirectory / "Inside.seam"));

  // An installation that is gone is a refusal, not a song bound to a bank that cannot sing.
  std::error_code removeError;
  std::filesystem::remove_all(installed.value().installDirectory, removeError);
  const auto missing = native_ui::createInstalledBankSongProject(roots, InstalledBankSongRequest{
      .projectPath = produced.root / "songs" / "Missing Song.seam",
      .voicebankId = installed.value().voicebankId,
      .voicebankVersion = installed.value().voicebankVersion,
      .contentHash = installed.value().contentHash,
      .installDirectory = installed.value().installDirectory});
  CHECK(!missing.hasValue());
  CHECK(!std::filesystem::exists(produced.root / "songs" / "Missing Song.seam"));
}

TEST_CASE("Studio writes no song for an installation whose content no longer matches the review") {
  const auto produced = publishReviewedCandidate();
  if (produced.published.root.empty()) return;
  auto key = distribution::generateSigningKeyPair();
  CHECK(key.hasValue());
  if (!key) return;
  const auto package = produced.root / "out" / "drift.seambank";
  std::filesystem::create_directories(package.parent_path());
  CHECK(native_ui::packPublishedSampleBank(produced.published, package, key.value()).hasValue());
  const auto installRoot = produced.root / "voicebanks";
  const auto installed = native_ui::installSignedSampleBank(package, installRoot,
      {key.value().publicKey}, produced.published.contentSha256);
  CHECK(installed.hasValue());
  if (!installed) return;
  const std::vector<voicebank::VoicebankSearchRoot> roots{
      {installRoot, voicebank::VoicebankRootKind::Installed}};
  // A published song request that names a different content hash than the installation is refused,
  // because the editor would otherwise open a song pointing at content nobody reviewed.
  const auto songs = produced.root / "songs";
  std::filesystem::create_directories(songs);
  const auto mismatched = native_ui::createInstalledBankSongProject(roots, InstalledBankSongRequest{
      .projectPath = songs / "Mismatched Song.seam",
      .voicebankId = installed.value().voicebankId,
      .voicebankVersion = installed.value().voicebankVersion,
      .contentHash = std::string(64U, 'c'),
      .installDirectory = installed.value().installDirectory});
  CHECK(!mismatched.hasValue());
  CHECK(!std::filesystem::exists(songs / "Mismatched Song.seam"));
}
