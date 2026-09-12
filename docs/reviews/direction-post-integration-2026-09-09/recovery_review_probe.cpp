#include "test_support.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank_production/candidate_publication.hpp"
#include "seam/voicebank_production/repository.hpp"
#include <iostream>
#include <stdexcept>

namespace production = seam::voicebank_production;
namespace core = seam::core;

template <class T> void require(const core::Result<T>& result, const char* action) {
  if (!result) throw std::runtime_error(std::string{action} + ": " + result.error().message + " / " + result.error().context);
}

bool run(const std::filesystem::path& root, bool torn) {
  std::filesystem::create_directory(root);
  const auto notice = root / "synthetic-notice.txt";
  require(core::durableAtomicWriteTextNew(notice,
      "SYNTHETIC PRIVATE ENGINEERING PROBE ONLY. No production singer or rights qualification."), "notice");
  const auto noticeHash = core::sha256File(notice); require(noticeHash, "notice hash");
  production::VoicebankProductionProject project{
      .projectId = torn ? "gap-probe" : "control-probe", .inventoryId = "synthetic-inventory",
      .inventorySha256 = std::string(64U, 'a'), .selectedSourceStrategyId = "synthetic",
      .licenseLocator = notice.string(), .licenseSha256 = noticeHash.value()};
  project.sourceStrategies = {{.id = "synthetic", .kind = production::SourceStrategyKind::ProceduralSynthesis,
      .rights = production::Feasibility::Pass, .coverage = production::Feasibility::Pass,
      .listening = production::Feasibility::Pass, .permissions = {true, true, true, true},
      .licenseLocator = notice.string(), .licenseSha256 = noticeHash.value(), .evidenceState = "SYNTHETIC_TEST_ONLY"}};
  project.operators = {{"producer", "PRODUCER"}, {"reviewer", "REVIEWER"}};
  project.unitAssignments = {{.coverageKey = "sustain:a", .pitchLayer = 69,
      .promptId = "prompt-a", .plannedTakeId = "take-a"}};
  production::ProductionProjectRepository repository{root / "workspace"};
  require(repository.initialize(project, {.action = "create", .subjectId = project.projectId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-09T10:00:00Z"}), "initialize");
  std::cout << project.projectId << " initialize=PASS generation=" << project.lastDurableGeneration << '\n';
  if (torn) {
    require(core::durableAtomicWriteTextNew(root / "workspace/journal/00000000000000000002.json", "interrupted"), "torn journal");
    const auto recovered = repository.recover(); require(recovered, "recover after torn journal");
    std::cout << project.projectId << " recovery=PASS generation=" << recovered.value().lastDurableGeneration << '\n';
  }
  require(repository.save(project, {.action = "save", .subjectId = project.projectId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-09T10:01:00Z"}), "normal save");
  require(repository.verify(project), "verify after normal save");
  std::cout << project.projectId << " save=PASS verify=PASS generation=" << project.lastDurableGeneration << '\n';
  const auto audio = root / "raw.wav";
  require(seam::voicebank::writeWav(audio, {.sampleRate = 48000U, .channels = 1U,
      .sampleFormat = seam::voicebank::WavSampleFormat::Pcm24},
      seam::test::support::sineWave(48000U, 440.0, 0.12, 0.25F)), "WAV generation");
  const auto imported = repository.importRaw(project, audio,
      {.takeId = "take-a", .promptId = "prompt-a", .coverageKey = "sustain:a", .pitchLayer = 69},
      {.action = "import", .subjectId = "take-a", .operatorId = "producer", .occurredAtUtc = "2026-09-09T10:02:00Z"});
  require(imported, "import beyond gap"); require(repository.verify(project), "verify after import");
  std::cout << project.projectId << " import=PASS verify=PASS generation=" << project.lastDurableGeneration << '\n';
  auto manifest = seam::test::support::makeManifest({seam::test::support::makeUnit(
      "a-69", {"a"}, "audio/placeholder.wav", 69, seam::voicebank::UnitKind::Sustain, 5760U)});
  const auto packet = production::prepareSampleCandidateReview(root / "workspace", project, manifest);
  std::cout << project.projectId << " prepareReview=" << (packet ? "PASS" : "FAIL") << '\n';
  if (!packet) std::cout << "errorCode=" << static_cast<int>(packet.error().code)
      << " isConflict=" << (packet.error().code == core::ErrorCode::Conflict)
      << " message=" << packet.error().message << " context=" << packet.error().context << '\n';
  const bool expected = torn ? (!packet && packet.error().code == core::ErrorCode::Conflict &&
      packet.error().context == "Missing generation 2") : static_cast<bool>(packet);
  std::cout << project.projectId << " reviewRecords=" << project.reviews.size() << '\n';
  return expected;
}

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    const auto root = std::filesystem::canonical(argv[1]);
    const bool control = run(root / "control", false);
    const bool gap = run(root / "gap", true);
    std::cout << "recovery_review_conflict_reproduced=" << (control && gap) << '\n';
    return control && gap ? 0 : 1;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
