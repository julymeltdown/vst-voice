#include "test_framework.hpp"
#include "test_support.hpp"
#include "prepared_candidate_fixture.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/authoring/export_service.hpp"
#include "seam/authoring/helper_process.hpp"
#include "seam/authoring/voicebank_installer_service.hpp"
#include "seam/candidate_packaging/candidate_package.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/rendering/phrase_segmenter.hpp"
#include "seam/rendering/project_renderer.hpp"
#include "seam/rendering/render_snapshot.hpp"
#include "seam/voicebank/acoustic_analysis.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank_production/draft_inventory.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/repository.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>

namespace {
using namespace seam;
namespace production = voicebank_production;
using J = formats::JsonValue;

struct Fixture final {
  bool retained{std::getenv("SEAM_ENGLISH_LIFECYCLE_CAPTURE_DIR") != nullptr};
  std::filesystem::path root;
  production::VoicebankProductionProject project;
  std::size_t commandSequence{0};
  explicit Fixture(bool distinguish, bool prepared = false) {
    if (retained) {
      const std::filesystem::path parent{std::getenv("SEAM_ENGLISH_LIFECYCLE_CAPTURE_DIR")};
      CHECK(parent.is_absolute()); CHECK(std::filesystem::is_directory(parent));
      root = parent / (prepared ? "prepared" : distinguish ? "distinguished" : "folded");
      CHECK(std::filesystem::create_directory(root)); // New output only, never overwrite a capture.
    } else root = test::support::temporaryDirectory("english-candidate-lifecycle");
    auto profile = production::draftInventoryPresetProfile(production::DraftInventoryPreset::EnglishFull, "english-lifecycle");
    profile.vowels = {distinguish ? "ay1" : "ay"}; profile.includeKinds = {"sustain"};
    profile.pitchLayers = {69, 70}; profile.requestedMinMidi = 68; profile.requestedMaxMidi = 72; profile.alternateTakes = 1;
    if (prepared) profile.supportedStyles = {"neutral", "soft"};
    if (distinguish) profile.stressPolicy = "DISTINGUISH_LEXICAL_STRESS";
    CHECK(production::createDraftProducerWorkspace(root / "workspace", profile, "english-lifecycle", "producer", "2026-10-15T01:00:00Z"));
    production::ProductionProjectRepository repository{root / "workspace/producer"};
    const auto recovered = repository.recover(); CHECK(recovered); project = recovered.value();
    CHECK(repository.registerReviewer(project, "reviewer", core::sha256Hex(production::encodeProductionProject(project)),
        "producer", "2026-10-15T01:01:00Z"));
    const auto license = root / "fixture-license.txt";
    CHECK(core::durableAtomicWriteTextNew(license, "SYNTHETIC ENGINEERING FIXTURE ONLY. Not a female singer, performer clearance, owner policy or human acceptance."));
    production::SourceStrategyAssessment source{.id = "fixture", .kind = prepared ? production::SourceStrategyKind::HumanRecording : production::SourceStrategyKind::ProceduralSynthesis,
        .rights = production::Feasibility::Pass, .permissions = {true, true, true, true},
        .licenseLocator = license.string(), .licenseSha256 = core::sha256File(license).value()};
    CHECK(repository.registerSource(project, source, core::sha256Hex(production::encodeProductionProject(project)), "producer", "2026-10-15T01:02:00Z"));
    const auto assignments = project.unitAssignments;
    std::vector<std::filesystem::path> tones;
    for (const auto pitch : {69, 70}) {
      const auto path = root / ("tone-" + std::to_string(pitch) + ".wav");
      const auto hz = 440.0 * std::pow(2.0, (static_cast<double>(pitch) - 69.0) / 12.0);
      CHECK(voicebank::writeWav(path, {.sampleRate = 16000U, .channels = 1U, .sampleFormat = voicebank::WavSampleFormat::Pcm16},
          test::support::sineWave(16000U, hz, 0.2, 0.25F)));
      tones.push_back(path);
    }
    std::pair<std::filesystem::path, std::vector<std::string>> packet;
    if (prepared) packet = test::preparedTonePacket(root, tones);
    bool softSourceRegistered = false;
    for (const auto& row : assignments) {
      const auto path = root / ("tone-" + std::to_string(row.pitchLayer) + ".wav");
      const production::RawTakeInput input{.takeId = row.plannedTakeId, .promptId = row.promptId,
          .coverageKey = row.coverageKey, .pitchLayer = row.pitchLayer, .style = row.style};
      const production::ProductionJournalEvent event{"import", row.plannedTakeId, "producer", "2026-10-15T01:03:00Z"};
      if (prepared) {
        if (row.style == "soft" && !softSourceRegistered) {
          source.id = "fixture-soft";
          source.licenseLocator = (root / "fixture-soft-license.txt").string();
          CHECK(core::durableAtomicWriteTextNew(source.licenseLocator, "SECOND SYNTHETIC DECLARATION; no real grant or approval."));
          source.licenseSha256 = core::sha256File(source.licenseLocator).value();
          CHECK(repository.registerSource(project, source, core::sha256Hex(production::encodeProductionProject(project)),
              "producer", "2026-10-15T01:03:00Z"));
          softSourceRegistered = true;
        }
        // The selected source is explicit at capture time; switching sources
        // does not rewrite the earlier takes' declaration snapshots.
        const auto packetSha = core::sha256File(packet.first / "donors.json").value();
        const auto id = packet.second.at(static_cast<std::size_t>(row.pitchLayer - 69));
        auto held = production::loadPreparedPhoneDonor(packet.first, packetSha, id); CHECK(held);
        CHECK(repository.importPreparedDonor(project, std::move(held.value()),
            {core::sha256Hex(production::encodeProductionProject(project)), packetSha, id, project.selectedSourceStrategyId}, input, event));
      } else CHECK(repository.importRaw(project, path, input, event));
    }
    const auto evidence = root / "fixture-quality.txt";
    const std::string decision = "MOCK FIXTURE REVIEW ONLY. No human listening, phonetic quality or singer acceptance.";
    CHECK(core::durableAtomicWriteTextNew(evidence, decision));
    for (const auto& strategy : project.sourceStrategies) {
      const auto inspected = cli({"inspect-source-quality", (root / "workspace/producer").string(), strategy.id});
      CHECK(cli({"record-source-quality", (root / "workspace/producer").string(), strategy.id, inspected.find("projectSha256")->asString(),
        "fixture-quality-" + strategy.id, "reviewer", "2026-10-15T01:04:00Z", "pass", "pass", evidence.string(), core::sha256Hex(decision)}).find("result")->asString() == "SourceQualityAssessmentCommitted");
    }
  }
  ~Fixture() { if (!retained) { std::error_code error; std::filesystem::remove_all(root, error); } }
  J cli(std::vector<std::string> arguments, std::uint32_t expectedExit = 0U) {
    const auto run = authoring::runBoundedHelperProcessObserved({.executable = SEAM_TEST_VOICEBANK_CLI, .arguments = arguments});
    CHECK(run);
    const auto* exited = std::get_if<authoring::HelperExited>(&run.value().termination);
    J::Array argv; for (const auto& argument : arguments) argv.emplace_back(argument);
    const auto receipt = formats::stringifyJson(J::Object{{"argv", std::move(argv)},
        {"exited", exited != nullptr}, {"exitCode", exited ? static_cast<std::int64_t>(exited->code) : std::int64_t{-1}},
        {"stdout", run.value().output.standardOutput}, {"stderr", run.value().output.standardError}}, true);
    CHECK(core::durableAtomicWriteTextNew(root / "commands" / (std::to_string(++commandSequence) + ".json"), receipt));
    if (!exited || exited->code != expectedExit)
      throw test::Failure{"English lifecycle CLI: " + run.value().output.standardOutput + run.value().output.standardError};
    if (expectedExit != 0U) return J{};
    const auto parsed = formats::parseJson(run.value().output.standardOutput); CHECK(parsed); return parsed.value();
  }
};
}

TEST_CASE("Prepared donor candidates retain both license identities through signed installation and refuse resigned evidence omissions") {
  // Every actor, grant, phone, pitch and quality/review decision is a synthetic
  // test fixture. No downloaded voice, factual singer review or Beta approval.
  Fixture fixture{false, true};
  const auto producer = fixture.root / "workspace/producer", editable = fixture.root / "editable/manifest.json";
  fixture.cli({"create-sample-draft", producer.string(), "engineering.prepared.lifecycle", "0.1.0",
      "Synthetic prepared lifecycle", "en", "neutral", (fixture.root / "editable").string()});
  const auto captured = fixture.cli({"prepare-sample-review", producer.string(), editable.string(), (fixture.root / "packet.json").string()});
  const auto reviewed = fixture.cli({"review-sample", producer.string(), (fixture.root / "packet.json").string(),
      captured.find("fileSha256")->asString(), "reviewer", "2026-10-15T01:05:00Z", "accept"});
  std::filesystem::remove_all(fixture.root / "prepared-tones");
  std::filesystem::remove(fixture.root / "fixture-license.txt");
  std::filesystem::remove(fixture.root / "fixture-soft-license.txt");
  const auto candidate = fixture.root / "candidate";
  const auto published = fixture.cli({"publish-sample", producer.string(), editable.string(), reviewed.find("generation")->asString(),
      reviewed.find("projectSha256")->asString(), candidate.string()});
  CHECK(published.find("schemaVersion")->asInt64() == 4);
  const auto verified = production::verifyResourceCandidateDirectory(candidate);
  if (!verified) throw test::Failure{verified.error().message + " / " + verified.error().context};
  const auto descriptor = verified.value().descriptor;
  CHECK(descriptor.schemaVersion == 4); CHECK(descriptor.declaresDependencySet()); CHECK(descriptor.unitBindings.size() == 4U);
  const auto snapshotBytes = core::readTextFileLimited(candidate / "provenance/production.json", 64U * 1024U * 1024U); CHECK(snapshotBytes);
  const auto snapshot = production::decodeProductionProject(snapshotBytes.value()); CHECK(snapshot);
  CHECK(snapshot.value().sourceBindings.size() == 4U); CHECK(snapshot.value().assets.size() == 2U);
  std::vector<production::ResourceCandidateFile> required;
  for (const auto& file : descriptor.evidence)
    if (file.role == "source-license-snapshot" || file.role == "source-donor-packet" || file.role == "source-intake-plan" ||
        file.role == "source-original-license" || file.role == "source-attribution") required.push_back(file);
  CHECK(required.size() == 6U); // Two independent declarations + one shared four-file packet.
  CHECK(std::count_if(required.begin(), required.end(), [](const auto& file) { return file.role == "source-license-snapshot"; }) == 2);
  const auto& binding = snapshot.value().sourceBindings.front(); CHECK(binding.preparedDonor);
  CHECK(binding.strategy.licenseSha256 != binding.preparedDonor->originalLicenseSha256);
  // These owned-reader cases deliberately pass the descriptor, snapshot and
  // listing identities. Ordinary re-signed file damage below stops earlier at
  // readListed and cannot exercise these semantic provenance checks.
  using Owned = std::map<std::string, std::vector<std::byte>, std::less<>>;
  const auto ownedText = [](const std::string& value) {
    const auto bytes = std::as_bytes(std::span{value.data(), value.size()});
    return std::vector<std::byte>{bytes.begin(), bytes.end()};
  };
  const auto rebind = [](production::VoicebankProductionProject& project) {
    for (auto& source : project.sourceBindings) {
      source.id = production::takeSourceBindingId(project.projectId, source);
      const auto take = std::find_if(project.takes.begin(), project.takes.end(),
          [&](const auto& value) { return value.takeId == source.takeId; });
      CHECK(take != project.takes.end()); take->sourceBindingId = source.id;
    }
  };
  const auto exerciseSource = [&](std::string_view name, production::VoicebankProductionProject project,
      production::ResourceCandidateDescriptor changed, Owned owned, std::string_view refusal, std::size_t packetReads) {
    rebind(project);
    const auto sourceJson = production::encodeProductionProject(project);
    CHECK(production::decodeProductionProject(sourceJson));
    owned["provenance/production.json"] = ownedText(sourceJson);
    changed.source->projectSha256 = core::sha256Hex(sourceJson);
    auto listing = std::find_if(changed.evidence.begin(), changed.evidence.end(),
        [](const auto& file) { return file.path == "provenance/production.json"; });
    CHECK(listing != changed.evidence.end());
    listing->sha256 = changed.source->projectSha256; listing->bytes = sourceJson.size();
    std::sort(changed.evidence.begin(), changed.evidence.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
    CHECK(production::validateResourceCandidateDescriptor(changed));
    std::vector<std::string> reads;
    const auto checked = production::verifyResourceCandidateSourceEvidence(changed,
        [&](std::string_view path, std::uint64_t limit) -> core::Result<std::vector<std::byte>> {
          const auto found = owned.find(path);
          auto bytes = found != owned.end() ? core::Result<std::vector<std::byte>>{found->second}
                                           : core::readFileBytesLimited(candidate / path, limit);
          CHECK(bytes); CHECK(bytes.value().size() <= limit);
          const auto file = std::find_if(changed.evidence.begin(), changed.evidence.end(),
              [&](const auto& value) { return value.path == path; });
          CHECK(file != changed.evidence.end()); CHECK(file->bytes == bytes.value().size());
          CHECK(file->sha256 == core::sha256Hex(bytes.value()));
          CHECK(std::find(reads.begin(), reads.end(), path) == reads.end());
          reads.emplace_back(path); return bytes;
        });
    if (refusal.empty()) CHECK(checked);
    else {
      CHECK(!checked);
      if (checked.error().message != refusal)
        throw test::Failure{std::string{name} + ": expected " + std::string{refusal} + ", received " + checked.error().message};
    }
    CHECK(reads.front() == "provenance/production.json");
    CHECK(static_cast<std::size_t>(std::count_if(reads.begin(), reads.end(), [](const auto& path) {
      return path.starts_with("provenance/source-evidence/packets/");
    })) == packetReads);
    std::cout << "[SOURCE-SEMANTICS] " << name << ": " << (checked ? "PASS" : checked.error().message)
              << "; packet-files-read=" << packetReads << '\n';
  };
  exerciseSource("unchanged control", snapshot.value(), descriptor, {}, {}, 4U);
  for (const std::string_view field : {"donor-audio", "absent-donor", "plan", "license", "attribution"}) {
    auto rewritten = snapshot.value(); auto& member = *rewritten.sourceBindings.back().preparedDonor;
    if (field == "donor-audio") {
      const auto other = std::find_if(rewritten.sourceBindings.begin(), rewritten.sourceBindings.end(), [&](const auto& value) {
        return value.rawAssetSha256 != rewritten.sourceBindings.back().rawAssetSha256;
      });
      CHECK(other != rewritten.sourceBindings.end()); CHECK(other->preparedDonor);
      member.donorId = other->preparedDonor->donorId; // Present row, different actual tone.
    } else if (field == "absent-donor") member.donorId = "absent_l00000_n1";
    else if (field == "plan") member.intakePlanSha256 = std::string(64U, '0');
    else if (field == "license") member.originalLicenseSha256 = std::string(64U, '0');
    else member.attributionSha256 = std::string(64U, '0');
    // The first shared member remains valid, so all four packet files and
    // all-row facts pass before this later member reaches its comparison.
    exerciseSource(field, std::move(rewritten), descriptor, {}, "Candidate prepared donor differs from its retained packet", 4U);
  }
  {
    auto rewritten = snapshot.value(); auto changed = descriptor;
    const auto oldDigest = rewritten.sourceBindings.front().preparedDonor->packetSha256;
    const auto oldPrefix = "provenance/source-evidence/packets/" + oldDigest + "/";
    const auto raw = core::readTextFileLimited(candidate / (oldPrefix + "donors.json"), 16U * 1024U * 1024U); CHECK(raw);
    auto packet = formats::parseJson(raw.value()); CHECK(packet);
    packet.value().asObject()["status"] = "APPROVED";
    const auto json = formats::stringifyJson(packet.value()), digest = core::sha256Hex(json);
    const auto prefix = "provenance/source-evidence/packets/" + digest + "/";
    Owned owned;
    for (auto& file : changed.evidence) if (file.path.starts_with(oldPrefix)) {
      const auto name = file.path.substr(oldPrefix.size());
      const auto original = core::readFileBytesLimited(candidate / file.path, 16U * 1024U * 1024U); CHECK(original);
      file.path = prefix + name;
      owned[file.path] = name == "donors.json" ? ownedText(json) : original.value();
      file.sha256 = core::sha256Hex(owned.at(file.path)); file.bytes = owned.at(file.path).size();
    }
    for (auto& source : rewritten.sourceBindings) source.preparedDonor->packetSha256 = digest;
    exerciseSource("packet facts after rehash", std::move(rewritten), std::move(changed), std::move(owned),
        "Expected unapproved prepared donor field: status", 4U);
  }
  {
    auto changed = descriptor;
    const std::string extra = "unreferenced declaration";
    changed.evidence.push_back({"provenance/source-evidence/unreferenced.txt", "source-license-snapshot", core::sha256Hex(extra), extra.size()});
    exerciseSource("unreferenced closure entry", snapshot.value(), std::move(changed), {},
        "Candidate source evidence union contains an unreferenced entry", 4U);
  }
  {
    auto rewritten = snapshot.value();
    for (auto& source : rewritten.sourceBindings) source.preparedDonor.reset();
    exerciseSource("schema four without prepared binding", std::move(rewritten), descriptor, {},
        "Prepared donor provenance requires candidate schema 4, and schema 4 requires a prepared binding", 0U);
  }
  const auto key = distribution::generateSigningKeyPair(); CHECK(key);
  const distribution::VerifySeambankOptions trusted{.trustedPublicKeys = {key.value().publicKey}, .requireTrustedSigner = true};
  const auto package = fixture.root / "prepared.seambank";
  const auto packed = candidate_packaging::packageResourceCandidate(candidate, package, key.value(),
      {.expectedCandidateSha256 = verified.value().candidateSha256}); CHECK(packed);
  CHECK(candidate_packaging::verifyResourceCandidatePackage(package, trusted));
  const auto installed = candidate_packaging::installResourceCandidatePackage(package, fixture.root / "installed",
      {.verification = trusted, .expectedPackageDigest = packed.value().packageDigest}); CHECK(installed);
  const auto audit = [&] { return candidate_packaging::verifyInstalledResourceCandidate(package, packed.value().packageDigest,
      verified.value().candidateSha256, installed.value().installDirectory, trusted); };
  CHECK(audit());
  for (const auto& file : required) {
    const auto path = installed.value().installDirectory / file.path;
    const auto original = core::readFileBytesLimited(path, 16U * 1024U * 1024U); CHECK(original);
    CHECK(core::sha256Hex(original.value()) == file.sha256);
    for (const bool remove : {false, true}) {
      if (remove) CHECK(std::filesystem::remove(path));
      else CHECK(core::durableAtomicWriteText(path, "changed installed evidence"));
      CHECK(!audit());
      CHECK(core::durableAtomicWrite(path, original.value())); CHECK(audit());
    }
  }
  std::size_t variant = 0;
  for (const auto& file : required) {
    for (const bool remove : {false, true}) {
      const auto changedRoot = fixture.root / ("altered-" + std::to_string(variant++));
      std::filesystem::copy(candidate, changedRoot, std::filesystem::copy_options::recursive);
      auto changed = descriptor;
      auto found = std::find_if(changed.evidence.begin(), changed.evidence.end(), [&](const auto& entry) { return entry.path == file.path; });
      CHECK(found != changed.evidence.end());
      if (remove) { std::filesystem::remove(changedRoot / file.path); changed.evidence.erase(found); }
      else {
        const std::string swap = "substituted evidence with a newly authored listing";
        CHECK(core::durableAtomicWriteText(changedRoot / file.path, swap));
        found->bytes = swap.size(); found->sha256 = core::sha256Hex(swap);
      }
      const auto encoded = production::encodeResourceCandidateDescriptor(changed); CHECK(encoded);
      CHECK(core::durableAtomicWriteText(changedRoot / "candidate.json", encoded.value()));
      CHECK(!production::verifyResourceCandidateDirectory(changedRoot));
      const auto output = changedRoot.string() + ".seambank";
      CHECK(distribution::packSeambank(changedRoot, output, key.value()));
      CHECK(distribution::verifySeambank(output, trusted)); // Authentic signature and exact re-authored file table.
      CHECK(!candidate_packaging::verifyResourceCandidatePackage(output, trusted));
      const auto refused = changedRoot.string() + ".typed.seambank";
      CHECK(!candidate_packaging::packageResourceCandidate(changedRoot, refused, key.value(),
          {.expectedCandidateSha256 = core::sha256Hex(encoded.value())})); CHECK(!std::filesystem::exists(refused));
      const auto refusedInstall = changedRoot / "refused-install";
      CHECK(!candidate_packaging::installResourceCandidatePackage(output, refusedInstall,
          {.verification = trusted, .expectedPackageDigest = core::sha256File(output).value()}));
      CHECK(!std::filesystem::exists(refusedInstall));
    }
  }
  const auto downgraded = fixture.root / "downgraded";
  std::filesystem::copy(candidate, downgraded, std::filesystem::copy_options::recursive);
  auto legacy = descriptor; legacy.schemaVersion = 3;
  std::erase_if(legacy.evidence, [&](const auto& file) {
    if (file.path.starts_with("provenance/source-evidence/packets/")) {
      std::filesystem::remove(downgraded / file.path); return true;
    }
    return false;
  });
  const auto legacyJson = production::encodeResourceCandidateDescriptor(legacy); CHECK(legacyJson);
  CHECK(core::durableAtomicWriteText(downgraded / "candidate.json", legacyJson.value()));
  CHECK(!production::verifyResourceCandidateDirectory(downgraded));
  CHECK(distribution::packSeambank(downgraded, fixture.root / "downgraded.seambank", key.value()));
  CHECK(distribution::verifySeambank(fixture.root / "downgraded.seambank", trusted));
  CHECK(!candidate_packaging::verifyResourceCandidatePackage(fixture.root / "downgraded.seambank", trusted));
  auto oversized = descriptor;
  for (auto& file : oversized.evidence) if (file.role == "source-quality-evidence") file.bytes = 64ULL * 1024ULL * 1024ULL + 1U;
  const auto budget = production::verifyResourceCandidateSourceEvidence(oversized,
      [&](std::string_view path, std::uint64_t limit) { return core::readFileBytesLimited(candidate / path, limit); });
  CHECK(!budget); CHECK(budget.error().message.find("64 MiB budget") != std::string::npos);
  CHECK(!published.find("releaseEligible")->asBool());
}

TEST_CASE("English generated candidates preserve declared stress through signed install and fresh dictionary score rendering") {
  std::vector<float> foldedAudio;
  for (const bool distinguish : {false, true}) {
    Fixture fixture{distinguish};
    const auto producer = fixture.root / "workspace/producer", editable = fixture.root / "editable/manifest.json";
    const auto created = fixture.cli({"create-sample-draft", producer.string(), "engineering.english.lifecycle", "0.1.0",
        "Synthetic English lifecycle", "en", "neutral", (fixture.root / "editable").string()});
    CHECK(!created.find("releaseEligible")->asBool());
    CHECK(created.find("missingAssignments")->asArray().empty());
    const auto manifest = voicebank::ManifestJsonCodec{}.load(editable); CHECK(manifest); CHECK(manifest.value().languageContract);
    CHECK(manifest.value().expectedSampleRate == 16000U); CHECK(manifest.value().units.size() == 2U);
    const auto contract = *manifest.value().languageContract;
    const auto captured = fixture.cli({"prepare-sample-review", producer.string(), editable.string(), (fixture.root / "packet.json").string()});
    const auto packetHash = captured.find("fileSha256")->asString();
    fixture.cli({"review-sample", producer.string(), (fixture.root / "packet.json").string(), packetHash,
        "producer", "2026-10-15T01:05:00Z", "accept"}, 2U);
    const auto reviewed = fixture.cli({"review-sample", producer.string(), (fixture.root / "packet.json").string(), packetHash,
        "reviewer", "2026-10-15T01:05:00Z", "accept"});
    CHECK(reviewed.find("candidateAvailable")->asBool()); CHECK(!reviewed.find("releaseEligible")->asBool());
    const auto candidate = fixture.root / "candidate";
    const auto published = fixture.cli({"publish-sample", producer.string(), editable.string(), reviewed.find("generation")->asString(),
        reviewed.find("projectSha256")->asString(), candidate.string()});
    CHECK(!published.find("releaseEligible")->asBool());
    const auto candidateManifest = voicebank::ManifestJsonCodec{}.load(candidate / "manifest.json"); CHECK(candidateManifest);
    CHECK(candidateManifest.value().languageContract == contract);
    const auto key = distribution::generateSigningKeyPair(); CHECK(key);
    CHECK(distribution::savePublicKey(key.value().publicKey, fixture.root / "fixture-public-key.txt"));
    const auto package = fixture.root / "bank.seambank";
    const auto packaged = candidate_packaging::packageResourceCandidate(candidate, package, key.value(),
        {.expectedCandidateSha256 = published.find("candidateSha256")->asString()});
    if (!packaged) throw test::Failure{packaged.error().message + " / " + packaged.error().context};
    CHECK(packaged.value().contentSha256 == published.find("contentSha256")->asString());
    const auto verified = candidate_packaging::verifyResourceCandidatePackage(package,
        {.trustedPublicKeys = {key.value().publicKey}, .requireTrustedSigner = true}); CHECK(verified);
    authoring::VoicebankSession installedBanks({{fixture.root / "installed", voicebank::VoicebankRootKind::Installed}}, false);
    authoring::VoicebankInstallerService installer{installedBanks, fixture.root / "installed"};
    const auto installed = installer.install({.packagePath = package, .trustedPublicKeys = {key.value().publicKey}}); CHECK(installed);
    CHECK(installed.value().candidate.trust == voicebank::VoicebankTrust::TrustedInstalled);
    CHECK(installed.value().contentHash == packaged.value().contentSha256);
    CHECK(installed.value().packageDigest == packaged.value().packageDigest);
    CHECK(installed.value().candidate.manifest.languageContract == contract);
    if (!distinguish) {
      for (const bool remove : {false, true}) {
        const auto altered = fixture.root / (remove ? "removed-contract-candidate" : "altered-candidate");
        std::filesystem::copy(candidate, altered, std::filesystem::copy_options::recursive);
        auto changed = candidateManifest.value();
        if (remove) changed.languageContract.reset();
        else changed.languageContract->stressPolicy = "DISTINGUISH_LEXICAL_STRESS";
        CHECK(changed.validate());
        const auto encoded = voicebank::ManifestJsonCodec{}.encode(changed); CHECK(encoded);
        CHECK(core::durableAtomicWriteText(altered / "manifest.json", encoded.value()));
        CHECK(!std::filesystem::exists(altered / "manifest.json.bak"));
        const auto output = fixture.root / (remove ? "removed-contract.seambank" : "altered.seambank");
        const auto refused = candidate_packaging::packageResourceCandidate(altered, output, key.value(),
            {.expectedCandidateSha256 = published.find("candidateSha256")->asString()});
        CHECK(!refused); CHECK(refused.error().code == core::ErrorCode::Conflict);
        CHECK(refused.error().context == "manifest.json");
        CHECK(refused.error().message == "Candidate file is missing or differs in size" ||
            refused.error().message == "Candidate file differs from its listed digest");
        CHECK(!std::filesystem::exists(output));
      }
    }
    application::ProjectFactory factory{994000U}; auto song = factory.createProject("English dictionary from installed bank");
    const auto track = factory.addVocalTrack(song, "English fixture");
    const auto region = factory.addRegion(song, track, "New melody", time::Tick{0}, time::Tick{1920});
    for (int index = 0; index < 2; ++index) {
      auto [lyric, note] = factory.makeNote(time::Tick{960 * index}, time::Tick{960}, static_cast<std::uint8_t>(69 + index), index == 0 ? U"I" : U"-", domain::Language::English);
      CHECK(!note.phoneticHint); song.findRegion(region)->lyrics.push_back(lyric); song.findRegion(region)->notes.push_back(note);
    }
    song.findVocalTrack(track)->voicebank = {manifest.value().id, manifest.value().version, installed.value().contentHash};
    song.findVocalTrack(track)->styleSelection = {domain::VoiceStyleOrigin::Explicit, "neutral"};
    const auto score = fixture.root / "song.seam"; CHECK(formats::ProjectJsonCodec{}.save(song, score));
    std::filesystem::rename(fixture.root / "workspace", fixture.root / "workspace-unavailable");
    std::filesystem::rename(candidate, fixture.root / "candidate-unavailable");
    for (const auto* name : {"tone-69.wav", "tone-70.wav", "fixture-license.txt", "fixture-quality.txt"})
      std::filesystem::rename(fixture.root / name, fixture.root / (std::string{name} + ".unavailable"));
    const auto reopened = formats::ProjectJsonCodec{}.load(score); CHECK(reopened);
    authoring::VoicebankSession fresh({{fixture.root / "installed", voicebank::VoicebankRootKind::Installed}}, false); CHECK(fresh.refresh());
    const auto resolved = fresh.resolveTrack(reopened.value(), track); CHECK(resolved.resolved());
    const auto& bank = *resolved.candidate; CHECK(bank.manifest.languageContract == contract);
    const auto segments = rendering::PhraseSegmenter{}.segment(*reopened.value().findRegion(region)); CHECK(segments); CHECK(segments.value().size() == 1U);
    const auto snapshot = rendering::RenderSnapshotFactory{}.create(reopened.value(), bank.manifest, track, segments.value().front(), 1U,
        rendering::RenderQuality::Final, bank.bankRoot, 48000U, "neutral"); CHECK(snapshot);
    CHECK(snapshot.value().phonemes->tokens.size() == 2U);
    for (const auto& token : snapshot.value().phonemes->tokens) CHECK(token.symbol == "ay1");
    const std::vector<rendering::TrackVoicebankSource> sources{{track, bank.manifest, bank.bankRoot, bank.contentHash, bank.trust}};
    const auto rendered = rendering::ProductionProjectRenderer{}.render(reopened.value(), sources, track, region, 1U, 48000U, rendering::RenderQuality::Final); CHECK(rendered);
    CHECK(rendered.value().diagnostics.empty()); CHECK(rendered.value().fallbackCount == 0U);
    const auto& audio = rendered.value().interleaved; CHECK(std::all_of(audio.begin(), audio.end(), [](float x) { return std::isfinite(x); }));
    CHECK(rendered.value().channelCount == 2U); CHECK(audio.size() == 96000U);
    const auto facts = voicebank::analyzeAudio(std::span<const float>{audio}); CHECK(facts.rms > 1e-4);
    if (!distinguish) foldedAudio.assign(audio.begin(), audio.end());
    else CHECK(audio == foldedAudio); // Same recordings and pronunciation must retain the same continuation behavior.
    authoring::ExportSettings settings; settings.format = voicebank::WavSampleFormat::Float32; settings.includeStems = true;
    const auto exported = authoring::ExportService{}.exportSet(reopened.value(), sources, track, region, 1U, fixture.root / "export", settings); CHECK(exported);
    CHECK(exported.value().state == authoring::ExportState::Committed); CHECK(exported.value().files.size() == 2U);
    J::Array files; for (const auto& file : exported.value().files) {
      const auto wave = voicebank::readWav(file.path); CHECK(wave); CHECK(wave.value().interleaved == audio);
      CHECK(wave.value().channels == 2U); CHECK(wave.value().sampleRate == 48000U); CHECK(wave.value().frameCount() == 48000U);
      files.emplace_back(J::Object{{"path", file.path.filename().generic_string()}, {"sha256", core::sha256File(file.path).value()}});
    }
    CHECK(core::durableAtomicWriteTextNew(fixture.root / "lifecycle.json", formats::stringifyJson(J::Object{
        {"status", "ENGINEERING_PASS"}, {"stressPolicy", contract.stressPolicy}, {"sourceSampleRate", std::int64_t{16000}},
        {"renderSampleRate", std::int64_t{48000}}, {"resolvedPhones", J::Array{"ay1", "ay1"}}, {"phoneticHints", false},
        {"manifestSha256", packaged.value().manifestSha256}, {"contentSha256", packaged.value().contentSha256},
        {"candidateSha256", packaged.value().candidateSha256}, {"packageSha256", packaged.value().packageDigest},
        {"renderSnapshotSha256", snapshot.value().contentHash}, {"rms", facts.rms}, {"interleavedSamples", static_cast<std::int64_t>(audio.size())},
        {"channels", static_cast<std::int64_t>(rendered.value().channelCount)}, {"frames", std::int64_t{48000}}, {"durationSeconds", 1.0},
        {"exports", std::move(files)}, {"sourceActorsAndQuality", "MOCK_FIXTURE_ONLY"}, {"releaseEligible", false}}, true)));
  }
}
