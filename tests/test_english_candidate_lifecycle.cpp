#include "test_framework.hpp"
#include "test_support.hpp"
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

namespace {
using namespace seam;
namespace production = voicebank_production;
using J = formats::JsonValue;

struct Fixture final {
  bool retained{std::getenv("SEAM_ENGLISH_LIFECYCLE_CAPTURE_DIR") != nullptr};
  std::filesystem::path root;
  production::VoicebankProductionProject project;
  std::size_t commandSequence{0};
  explicit Fixture(bool distinguish) {
    if (retained) {
      const std::filesystem::path parent{std::getenv("SEAM_ENGLISH_LIFECYCLE_CAPTURE_DIR")};
      CHECK(parent.is_absolute()); CHECK(std::filesystem::is_directory(parent));
      root = parent / (distinguish ? "distinguished" : "folded");
      CHECK(std::filesystem::create_directory(root)); // New output only, never overwrite a capture.
    } else root = test::support::temporaryDirectory("english-candidate-lifecycle");
    auto profile = production::draftInventoryPresetProfile(production::DraftInventoryPreset::EnglishFull, "english-lifecycle");
    profile.vowels = {distinguish ? "ay1" : "ay"}; profile.includeKinds = {"sustain"};
    profile.pitchLayers = {69, 70}; profile.requestedMinMidi = 68; profile.requestedMaxMidi = 72; profile.alternateTakes = 1;
    if (distinguish) profile.stressPolicy = "DISTINGUISH_LEXICAL_STRESS";
    CHECK(production::createDraftProducerWorkspace(root / "workspace", profile, "english-lifecycle", "producer", "2026-10-15T01:00:00Z"));
    production::ProductionProjectRepository repository{root / "workspace/producer"};
    const auto recovered = repository.recover(); CHECK(recovered); project = recovered.value();
    CHECK(repository.registerReviewer(project, "reviewer", core::sha256Hex(production::encodeProductionProject(project)),
        "producer", "2026-10-15T01:01:00Z"));
    const auto license = root / "fixture-license.txt";
    CHECK(core::durableAtomicWriteTextNew(license, "SYNTHETIC ENGINEERING FIXTURE ONLY. Not a female singer, performer clearance, owner policy or human acceptance."));
    production::SourceStrategyAssessment source{.id = "fixture", .kind = production::SourceStrategyKind::ProceduralSynthesis,
        .rights = production::Feasibility::Pass, .permissions = {true, true, true, true},
        .licenseLocator = license.string(), .licenseSha256 = core::sha256File(license).value()};
    CHECK(repository.registerSource(project, source, core::sha256Hex(production::encodeProductionProject(project)), "producer", "2026-10-15T01:02:00Z"));
    const auto assignments = project.unitAssignments;
    for (const auto& row : assignments) {
      const auto path = root / ("tone-" + std::to_string(row.pitchLayer) + ".wav");
      const auto hz = 440.0 * std::pow(2.0, (static_cast<double>(row.pitchLayer) - 69.0) / 12.0);
      CHECK(voicebank::writeWav(path, {.sampleRate = 16000U, .channels = 1U, .sampleFormat = voicebank::WavSampleFormat::Pcm16},
          test::support::sineWave(16000U, hz, 0.2, 0.25F)));
      CHECK(repository.importRaw(project, path, {.takeId = row.plannedTakeId, .promptId = row.promptId,
          .coverageKey = row.coverageKey, .pitchLayer = row.pitchLayer, .style = row.style},
          {"import", row.plannedTakeId, "producer", "2026-10-15T01:03:00Z"}));
    }
    const auto inspected = cli({"inspect-source-quality", (root / "workspace/producer").string(), "fixture"});
    const auto evidence = root / "fixture-quality.txt";
    const std::string decision = "MOCK FIXTURE REVIEW ONLY. No human listening, phonetic quality or singer acceptance.";
    CHECK(core::durableAtomicWriteTextNew(evidence, decision));
    CHECK(cli({"record-source-quality", (root / "workspace/producer").string(), "fixture", inspected.find("projectSha256")->asString(),
        "fixture-quality", "reviewer", "2026-10-15T01:04:00Z", "pass", "pass", evidence.string(), core::sha256Hex(decision)}).find("result")->asString() == "SourceQualityAssessmentCommitted");
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
