#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank_production/repository.hpp"
#include "test_support.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

// Read-only audit of product source. This executable mutates only a fresh
// caller-supplied temporary workspace with explicitly synthetic test material.
void checked(const auto& result) {
  if (!result) throw std::runtime_error(result.error().message);
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  namespace p = seam::voicebank_production;
  const std::filesystem::path root{argv[1]};
  const auto license = root / "synthetic-notice.txt";
  checked(seam::core::durableAtomicWriteTextNew(license, "Synthetic audit fixture only; no singer or rights acceptance."));
  const auto digest = seam::core::sha256File(license); checked(digest);
  p::VoicebankProductionProject project{
    .projectId="ownership-audit", .inventoryId="audit", .inventorySha256=std::string(64, 'a'),
    .selectedSourceStrategyId="synthetic", .licenseLocator=license.string(),
    .licenseSha256=digest.value(), .immutableAssetRoot="assets"};
  project.sourceStrategies.push_back({.id="synthetic", .kind=p::SourceStrategyKind::ProceduralSynthesis,
    .rights=p::Feasibility::Pass, .coverage=p::Feasibility::Pass, .listening=p::Feasibility::Pass,
    .permissions={.sourceUse=true, .transformation=true, .singingBankRedistribution=true, .commercialRenders=true},
    .licenseLocator=license.string(), .licenseSha256=digest.value(), .evidenceState="SYNTHETIC_AUDIT_ONLY"});
  project.operators={{.operatorId="producer", .role="PRODUCER"}};
  project.unitAssignments={
    {.coverageKey="sustain:a", .pitchLayer=69, .promptId="prompt-a", .plannedTakeId="take-a"},
    {.coverageKey="sustain:i", .pitchLayer=69, .promptId="prompt-b", .plannedTakeId="take-b"}};
  p::ProductionProjectRepository repo{root / "workspace"};
  checked(repo.initialize(project, {.action="create", .subjectId=project.projectId, .operatorId="producer", .occurredAtUtc="2026-09-09T10:00:00Z"}));
  const auto wav = root / "shared.wav";
  checked(seam::voicebank::writeWav(wav, {.sampleRate=48000, .channels=1, .sampleFormat=seam::voicebank::WavSampleFormat::Pcm24},
    seam::test::support::sineWave(48000, 440, 0.12, 0.25F)));
  const auto a = repo.importRaw(project, wav, {.takeId="take-a", .promptId="prompt-a", .coverageKey="sustain:a", .pitchLayer=69},
    {.action="import", .subjectId="take-a", .operatorId="producer", .occurredAtUtc="2026-09-09T10:01:00Z"}); checked(a);
  const auto b = repo.importRaw(project, wav, {.takeId="take-b", .promptId="prompt-b", .coverageKey="sustain:i", .pitchLayer=69},
    {.action="import", .subjectId="take-b", .operatorId="producer", .occurredAtUtc="2026-09-09T10:02:00Z"}); checked(b);
  const auto staged = repo.stageOperation(b.value(), {.kind=p::OperationKind::NormalizeGain, .targetPeak=0.15F}, "edit-selected-b"); checked(staged);
  const auto revision = repo.commitStaged(project, staged.value(), "revision-intended-for-b", "producer", "2026-09-09T10:03:00Z"); checked(revision);
  std::cout << "same_audio=" << (a.value().sha256 == b.value().sha256) << '\n'
    << "intended_take=take-b\n"
    << "take_a_revisions=" << project.takes[0].derivedRevisionIds.size() << '\n'
    << "take_b_revisions=" << project.takes[1].derivedRevisionIds.size() << '\n';
  const auto durable = repo.recover(); checked(durable);
  std::cout << "durable_take_a_revisions=" << durable.value().takes[0].derivedRevisionIds.size() << '\n'
    << "durable_take_b_revisions=" << durable.value().takes[1].derivedRevisionIds.size() << '\n';
  return 0;
}
