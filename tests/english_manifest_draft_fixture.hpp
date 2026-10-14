#pragma once

#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank_production/draft_inventory.hpp"
#include "seam/voicebank_production/manifest_draft.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/repository.hpp"

namespace seam::test::support {
struct EnglishManifestDraftFixture final {
  std::filesystem::path root{temporaryDirectory("english-manifest-draft")};
  std::filesystem::path producerRoot{root / "workspace" / "producer"};
  voicebank_production::ProductionProjectRepository repository{producerRoot};
  voicebank_production::VoicebankProductionProject project;
  voicebank_production::DraftInventoryProfile profile;
  voicebank_production::SampleManifestDraftIdentity identity{
      "engineering.english", "0.1.0", "Synthetic English draft", domain::Language::English, "neutral"};
  explicit EnglishManifestDraftFixture(bool distinguish = false) {
    namespace production = voicebank_production;
    profile = production::draftInventoryPresetProfile(production::DraftInventoryPreset::EnglishFull, "english-fixture");
    profile.vowels = {distinguish ? "iy1" : "iy"}; profile.includeKinds = {"sustain"};
    profile.pitchLayers = {69, 70}; profile.requestedMinMidi = 68; profile.requestedMaxMidi = 70;
    profile.alternateTakes = 1;
    if (distinguish) profile.stressPolicy = "DISTINGUISH_LEXICAL_STRESS";
    const auto created = production::createDraftProducerWorkspace(std::filesystem::absolute(root / "workspace"),
        profile, "english-fixture", "producer", "2026-10-15T00:00:00Z");
    if (!created) throw Failure{"English fixture workspace: " + created.error().message};
    const auto recovered = repository.recover(); CHECK(recovered); project = recovered.value();
    const auto license = root / "synthetic-license.txt";
    CHECK(core::durableAtomicWriteTextNew(license, "SYNTHETIC TEST ONLY. No singer, rights or musical acceptance."));
    production::SourceStrategyAssessment source{.id = "fixture", .kind = production::SourceStrategyKind::ProceduralSynthesis,
        .rights = production::Feasibility::Pass, .permissions = {true, true, false, false},
        .licenseLocator = license.string(), .licenseSha256 = core::sha256File(license).value()};
    CHECK(repository.registerSource(project, source, core::sha256Hex(production::encodeProductionProject(project)),
        "producer", "2026-10-15T00:01:00Z"));
    CHECK(voicebank::writeWav(root / "tone.wav", {.sampleRate = 16000U, .channels = 1U, .sampleFormat = voicebank::WavSampleFormat::Pcm16},
        sineWave(16000U, 440.0, 0.2, 0.25F)));
    const auto row = project.unitAssignments.front();
    CHECK(repository.importRaw(project, root / "tone.wav", {.takeId = row.plannedTakeId, .promptId = row.promptId,
        .coverageKey = row.coverageKey, .pitchLayer = row.pitchLayer, .style = row.style},
        {"import", row.plannedTakeId, "producer", "2026-10-15T00:02:00Z"}));
  }
  ~EnglishManifestDraftFixture() { std::error_code error; std::filesystem::remove_all(root, error); }
  auto create(std::string name, voicebank_production::SampleManifestDraftOptions options = {}) {
    return voicebank_production::createSampleManifestDraft(producerRoot, project, identity,
        std::filesystem::absolute(root / name), options);
  }
};
} // namespace seam::test::support
