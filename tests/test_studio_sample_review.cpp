#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/native_ui/voicebank_studio.hpp"
#include "seam/platform/file_dialog.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/take_inspection_receipt.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <thread>

namespace {
using namespace seam;
namespace production = voicebank_production;
using Controller = native_ui::VoicebankStudioController;
using Decision = production::SampleCandidateReviewDecision;

core::Result<void> collect(Controller& controller) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
  while (controller.proceduralImportBusy()) {
    auto result = controller.pollProceduralCandidateImport();
    if (!result) return result;
    if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("Studio review worker did not finish");
    if (controller.proceduralImportBusy()) std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  return core::success();
}

struct Fixture final {
  std::filesystem::path root{test::support::temporaryDirectory("studio-sample-review")};
  production::ProductionProjectRepository repository{root / "producer"};
  production::VoicebankProductionProject project;
  voicebank::Manifest manifest;
  Controller controller;
  explicit Fixture(bool secondMissing = false, bool openManifest = true) {
    const auto license = root / "fixture-license.txt";
    CHECK(core::durableAtomicWriteTextNew(license, "Synthetic native workflow test only; no singer or Beta qualification."));
    const auto licenseHash = core::sha256File(license); CHECK(licenseHash);
    project = {.projectId = "native-review", .inventoryId = "fixture", .inventorySha256 = std::string(64U, 'a'),
        .selectedSourceStrategyId = "fixture", .licenseLocator = license.string(), .licenseSha256 = licenseHash.value()};
    project.sourceStrategies = {{.id = "fixture", .kind = production::SourceStrategyKind::ProceduralSynthesis,
        .rights = production::Feasibility::Pass, .coverage = production::Feasibility::Pass, .listening = production::Feasibility::Pass,
        .permissions = {true,true,true,true}, .licenseLocator = license.string(), .licenseSha256 = licenseHash.value(),
        .evidenceState = "SYNTHETIC_TEST_ONLY"}};
    project.operators = {{"producer", "PRODUCER"}, {"reviewer", "REVIEWER"}, {"reviewer-two", "REVIEWER"}};
    project.unitAssignments = {{.coverageKey = "sustain:a", .pitchLayer = 69, .promptId = "a", .plannedTakeId = "take-a"}};
    if (secondMissing) project.unitAssignments.push_back({.coverageKey = "sustain:i", .pitchLayer = 69, .promptId = "i", .plannedTakeId = "take-i"});
    CHECK(repository.initialize(project, {.action = "create", .subjectId = project.projectId, .operatorId = "producer", .occurredAtUtc = "2026-09-09T10:00:00Z"}));
    const auto samples = test::support::sineWave(48000U, 440.0, 0.12, 0.25F);
    CHECK(voicebank::writeWav(root / "raw.wav", {.sampleRate = 48000U, .channels = 1U, .sampleFormat = voicebank::WavSampleFormat::Pcm24}, samples));
    // The repository admits the take with its own QC receipt for the stored bytes.
    CHECK(repository.importRaw(project, root / "raw.wav", {.takeId = "take-a", .promptId = "a", .coverageKey = "sustain:a", .pitchLayer = 69},
        {.action = "import", .subjectId = "take-a", .operatorId = "producer", .occurredAtUtc = "2026-09-09T10:01:00Z"}));
    auto unit = test::support::makeUnit("a-69", {"a"}, "raw.wav", 69, voicebank::UnitKind::Sustain, samples.size());
    unit.renderer = voicebank::RendererHint::ClassicPsola;
    for (time::SampleFrame frame = 109; frame < unit.markers.audioEnd; frame += 109)
      unit.pitchMarks.push_back({.frame = frame, .confidence = 1.0F, .locked = true});
    manifest = test::support::makeManifest({unit});
    if (secondMissing) { unit.id = "i-69"; unit.phones = {"i"}; manifest.units.push_back(unit); }
    CHECK(voicebank::ManifestJsonCodec{}.save(manifest, root / "manifest.json"));
    if (openManifest) CHECK(controller.openManifest(root / "manifest.json"));
    CHECK(controller.openProductionProject(root / "producer", project.inventorySha256, "producer"));
  }
  void capture() {
    CHECK(controller.beginSelectedSampleReview());
    const auto collected = collect(controller);
    if (!collected) throw test::Failure{"review capture: " + collected.error().message};
    CHECK(controller.sampleReviewInspection());
  }
  void choose() { CHECK(controller.selectSampleReviewer(controller.sampleReviewInspection()->context, "reviewer")); }
  void decide(Decision decision) {
    const auto context = controller.sampleReviewInspection()->context;
    CHECK(controller.beginSampleReviewDecision(context, "reviewer", decision, "2026-09-09T10:02:00Z")); CHECK(collect(controller));
  }
};

struct Dialog final : platform::IFileDialog {
  std::function<void()> callback;
  std::optional<std::string> reviewer;
  std::optional<std::filesystem::path> path;
  bool confirm{false};
  std::string summary;
  std::vector<std::string> choices;
  std::optional<platform::FileDialogPurpose> purpose;
  core::Result<std::optional<std::filesystem::path>> choose(const platform::FileDialogRequest& request) override {
    purpose = request.purpose; if (callback) callback(); return path;
  }
  core::Result<std::optional<std::string>> chooseSampleReviewer(const std::vector<std::string>& options) override {
    choices = options; if (callback) callback(); return reviewer;
  }
  core::Result<bool> confirmSampleReview(std::string_view text, bool) override {
    summary = text; if (callback) callback(); return confirm;
  }
};
} // namespace

TEST_CASE("Studio refuses an unsupported newest workspace without replacing the open project") {
  Fixture fixture;
  auto name=std::to_string(fixture.project.lastDurableGeneration);name.insert(0U,20U-name.size(),'0');name+=".json";
  const auto path=fixture.root/"producer"/"journal"/name;
  auto journal=formats::parseJson(core::readTextFileLimited(path,1024U*1024U).value());CHECK(journal);
  journal.value().asObject().insert_or_assign("reviewInspectionPolicyVersion",formats::JsonValue{std::int64_t{4}});
  CHECK(core::durableAtomicWriteText(path,formats::stringifyJson(journal.value(),true)+"\n"));
  const auto journalHash=core::sha256File(path).value();
  const auto pointerHash=core::sha256File(fixture.root/"producer"/"project.json").value();
  const auto before=production::encodeProductionProject(*fixture.controller.productionProject());
  const auto epoch=fixture.controller.productionSessionEpoch();
  const auto refused=fixture.controller.openProductionProject(fixture.root/"producer",fixture.project.inventorySha256,"producer");
  CHECK(!refused);CHECK(refused.error().code==core::ErrorCode::Unsupported);
  CHECK(refused.error().message=="Unsupported production review inspection policy");
  CHECK(fixture.controller.productionSessionEpoch()==epoch);
  CHECK(production::encodeProductionProject(*fixture.controller.productionProject())==before);
  Controller fresh;
  CHECK(!fresh.openProductionProject(fixture.root/"producer",fixture.project.inventorySha256,"producer"));
  CHECK(!fresh.productionProject());
  CHECK(core::sha256File(path).value()==journalHash);
  CHECK(core::sha256File(fixture.root/"producer"/"project.json").value()==pointerHash);
}

TEST_CASE("native review shows current derivative inspection instead of a raw-only warning") {
  Fixture fixture;
  const auto staged = fixture.repository.stageOperation(fixture.project, "take-a", "",
      {.kind=production::OperationKind::NormalizeGain, .targetPeak=0.15F}, "review-normalized");
  CHECK(staged);
  CHECK(fixture.repository.commitStaged(fixture.project, staged.value(), "normalized-revision", "producer",
      "2026-09-09T10:02:00Z", "take-a", ""));
  CHECK(fixture.controller.openProductionProject(fixture.root / "producer", fixture.project.inventorySha256, "producer"));
  fixture.capture();
  const auto& inspection = *fixture.controller.sampleReviewInspection();
  CHECK(inspection.packet.units.front().audioSha256 == staged.value().outputSha256);
  const auto& details = inspection.details;
  CHECK(std::any_of(details.begin(), details.end(), [](const auto& value) {
    return value.find("AUTOMATED SIGNAL CHECKS SIGNAL_CHECKS_PASSED") != std::string::npos;
  }));
  CHECK(std::count_if(details.begin(), details.end(), [](const auto& value) { return value.starts_with("AUTOMATED CHECK "); }) == 8);
  CHECK(std::none_of(details.begin(), details.end(), [](const auto& value) { return value.find("COVER THE RAW TAKE") != std::string::npos; }));
  CHECK(fixture.project.reviews.empty());
  CHECK(!fixture.controller.sampleReviewReceipt());
  CHECK(fixture.controller.sampleReviewerId().empty());
}

TEST_CASE("Studio human sample review exposes matching automated signal checks without treating them as approval") {
  Fixture fixture(false, true);
  fixture.capture();
  const auto& details = fixture.controller.sampleReviewInspection()->details;
  CHECK(std::any_of(details.begin(), details.end(), [](const auto& value) {
    return value.find("AUTOMATED SIGNAL CHECKS SIGNAL_CHECKS_PASSED") != std::string::npos &&
        value.find("seam.take-inspector v3") != std::string::npos &&
        value.find("VOICED QC POLICY v1") != std::string::npos &&
        value.find("HUMAN REVIEW STILL REQUIRED") != std::string::npos;
  }));
  CHECK(std::any_of(details.begin(), details.end(), [](const auto& value) {
    return value.starts_with("TAKE METRICS PEAK ");
  }));
  CHECK(std::any_of(details.begin(), details.end(), [](const auto& value) {
    return value == "INSPECTED WAV FORMAT 48000 HZ / 1 CH / 24 BIT";
  }));
  CHECK(std::any_of(details.begin(), details.end(), [](const auto& value) {
    return value == "ROOT-PITCH CHECK EXPECTED MIDI 69 / ANALYZED 69";
  }));
  CHECK(std::count_if(details.begin(), details.end(), [](const auto& value) {
    return value.starts_with("AUTOMATED CHECK ") && value.ends_with(" / PASS");
  }) == 6U);
  // Breath and pause checks are shown as not applicable, never as passed.
  CHECK(std::count_if(details.begin(), details.end(), [](const auto& value) {
    return value.starts_with("AUTOMATED CHECK ") && value.ends_with(" / NOT APPLICABLE TO VOICED");
  }) == 2U);
  CHECK(std::none_of(details.begin(), details.end(), [](const auto& value) {
    return value.starts_with("LEGACY VOICED-ONLY");
  }));
  CHECK(std::none_of(details.begin(), details.end(), [](const auto& value) {
    return value.find("HUMAN REVIEW PASS") != std::string::npos;
  }));
  CHECK(fixture.project.reviews.empty());
  CHECK(fixture.controller.sampleReviewerId().empty());
}

TEST_CASE("Studio shows inspector two as historical evidence requiring reinspection") {
  Fixture fixture;
  auto& receipt = fixture.project.metadataRevisions.back();
  CHECK(receipt.kind == production::kTakeInspectionRevisionKind);
  auto evidence = production::decodeTakeInspectionEvidence(receipt.values.at("evidenceJson")); CHECK(evidence);
  evidence.value().inspectorVersion = "2";
  receipt.values.at("evidenceJson") = production::encodeTakeInspectionEvidence(evidence.value());
  receipt.values.at("evidenceSha256") = core::sha256Hex(receipt.values.at("evidenceJson"));
  receipt.revisionId = "take-inspection-" + receipt.values.at("evidenceSha256").substr(0U, 32U);
  // A declared historical fixture, not a new write through the current inspector.
  const auto bytes = production::encodeProductionProject(fixture.project);
  auto name = std::to_string(fixture.project.lastDurableGeneration);
  name.insert(0U, 20U - name.size(), '0'); name += ".json";
  const auto journalPath = fixture.root / "producer/journal" / name;
  auto journal = formats::parseJson(core::readTextFileLimited(journalPath, 1024U*1024U).value()); CHECK(journal);
  journal.value().asObject().insert_or_assign("projectSha256", formats::JsonValue{core::sha256Hex(bytes)});
  journal.value().asObject().insert_or_assign("reviewInspectionPolicyVersion", formats::JsonValue{std::int64_t{2}});
  CHECK(core::durableAtomicWriteText(fixture.root / "producer/generations" / name, bytes));
  CHECK(core::durableAtomicWriteText(fixture.root / "producer/project.json", bytes));
  CHECK(core::durableAtomicWriteText(journalPath, formats::stringifyJson(journal.value(), true) + "\n"));
  CHECK(fixture.controller.openProductionProject(fixture.root / "producer", fixture.project.inventorySha256, "producer"));
  fixture.capture();
  const auto& details = fixture.controller.sampleReviewInspection()->details;
  CHECK(std::any_of(details.begin(), details.end(), [](const auto& value) {
    return value == "HISTORICAL AUTOMATED CHECKS / seam.take-inspector v2 / REINSPECT THIS AUDIO BEFORE APPROVAL";
  }));
  CHECK(std::none_of(details.begin(), details.end(), [](const auto& value) {
    return value.starts_with("AUTOMATED SIGNAL CHECKS ");
  }));
}

TEST_CASE("legacy edited audio always warns when current diagnostics are missing") {
  Fixture fixture;
  const auto staged = fixture.repository.stageOperation(fixture.project, "take-a", "",
      {.kind=production::OperationKind::NormalizeGain, .targetPeak=0.15F}, "legacy-stage");
  CHECK(staged);
  // Reproduce the pre-diagnostic edit writer through generic save, without rewriting history.
  production::ImmutableAssetStore assets{fixture.root / "producer" / "assets"};
  const auto asset = assets.importFile(staged.value().path, production::AssetKind::Derived); CHECK(asset);
  fixture.project.assets.push_back(asset.value());
  fixture.project.derivedRevisions.push_back({.revisionId="legacy-edit", .inputSha256=staged.value().inputSha256,
      .outputSha256=staged.value().outputSha256, .operation=production::OperationKind::NormalizeGain,
      .operationVersion="seam-pcm-ops-1", .parameters={{"targetPeak", "0.15"}},
      .operatorId="producer", .performedAtUtc="2026-09-09T10:02:00Z"});
  fixture.project.takes.front().derivedRevisionIds.push_back("legacy-edit");
  CHECK(fixture.repository.save(fixture.project, {"transform", "legacy-edit", "producer", "2026-09-09T10:02:00Z"}));
  CHECK(fixture.repository.verify(fixture.project));
  CHECK(production::currentTakeInspection(fixture.project, "take-a"));
  CHECK(!production::currentActiveTakeInspection(fixture.project, "take-a"));
  CHECK(fixture.controller.openProductionProject(fixture.root / "producer", fixture.project.inventorySha256, "producer"));
  fixture.capture();
  const auto& details = fixture.controller.sampleReviewInspection()->details;
  CHECK(std::any_of(details.begin(), details.end(), [](const auto& value) {
    return value == "NO CURRENT AUTOMATED CHECKS FOR THIS AUDIO / HUMAN REVIEW STILL REQUIRED";
  }));
  CHECK(std::none_of(details.begin(), details.end(), [](const auto& value) {
    return value.starts_with("AUTOMATED CHECK ") || value.find("COVER THE RAW TAKE") != std::string::npos;
  }));
  CHECK(!fixture.controller.sampleReviewReceipt());
  CHECK(fixture.controller.sampleReviewerId().empty());
}

TEST_CASE("Studio selected sample capture exposes exact material without assigning a reviewer or approving") {
  Fixture fixture;
  const auto before = production::encodeProductionProject(fixture.project);
  fixture.controller.manifest().units.front().markers.stableStart += 3;
  fixture.capture();
  const auto& inspection = *fixture.controller.sampleReviewInspection();
  CHECK(fixture.controller.sampleReviewerId().empty());
  CHECK(!fixture.controller.sampleReviewReceipt());
  CHECK(inspection.packet.manifest.units.front().markers == fixture.controller.manifest().units.front().markers);
  CHECK(inspection.packet.units.front().audioSha256 == fixture.project.takes.front().rawAssetSha256);
  CHECK(inspection.packet.units.front().originOperatorId == "producer");
  CHECK(inspection.audio && inspection.audio->frameCount() == inspection.packet.units.front().frameCount);
  CHECK(!inspection.peaks.empty());
  CHECK(std::any_of(inspection.details.begin(), inspection.details.end(), [](const auto& value) { return value.starts_with("SOURCE BINDING "); }));
  CHECK(std::any_of(inspection.details.begin(), inspection.details.end(), [](const auto& value) { return value.starts_with("PITCH "); }));
  CHECK(!fixture.controller.beginSampleReviewDecision(inspection.context, "", Decision::Accept));
  CHECK(!fixture.controller.selectSampleReviewer(inspection.context, "producer"));
  const auto recovered = fixture.repository.recover(); CHECK(recovered);
  CHECK(production::encodeProductionProject(recovered.value()) == before);
}

TEST_CASE("Studio review requires explicit modal identity and rejects edits across all modal boundaries") {
  Fixture fixture;
  fixture.capture();
  Dialog dialog;
  CHECK(native_ui::chooseStudioSampleReviewer(fixture.controller, dialog));
  CHECK(fixture.controller.sampleReviewerId().empty());
  dialog.reviewer = "reviewer";
  dialog.callback = [&] { fixture.controller.manifest().units.front().gainDb = -1.0F; };
  CHECK(!native_ui::chooseStudioSampleReviewer(fixture.controller, dialog));
  CHECK(fixture.controller.sampleReviewerId().empty());
  fixture.capture(); dialog.callback = {};
  CHECK(native_ui::chooseStudioSampleReviewer(fixture.controller, dialog));
  CHECK(fixture.controller.sampleReviewerId() == "reviewer");
  CHECK(native_ui::confirmStudioSampleReview(fixture.controller, dialog, Decision::Accept));
  CHECK(!fixture.controller.proceduralImportBusy()); // Cancel means no worker.
  dialog.confirm = true;
  dialog.callback = [&] { fixture.controller.manifest().units.front().pitchMarks.front().frame += 1; };
  CHECK(!native_ui::confirmStudioSampleReview(fixture.controller, dialog, Decision::Accept));
  CHECK(dialog.summary.find("reviewer") != std::string::npos);
  CHECK(dialog.summary.find(fixture.project.takes.front().rawAssetSha256) != std::string::npos);
  CHECK(!fixture.controller.proceduralImportBusy());
  dialog.path = fixture.root / "stale-candidate";
  dialog.callback = [&] { fixture.controller.manifest().units.front().markers.stableStart += 1; };
  CHECK(!native_ui::publishStudioSampleCandidate(fixture.controller, dialog));
  CHECK(!std::filesystem::exists(*dialog.path));
  const auto recovered = fixture.repository.recover(); CHECK(recovered); CHECK(recovered.value().reviews.empty());
}

TEST_CASE("Studio review selection round trips and reviewer changes cannot reuse a modal decision") {
  Fixture fixture;
  fixture.capture(); fixture.choose();
  const auto before = fixture.controller.sampleReviewInspection()->context;
  Dialog dialog; dialog.confirm = true;
  dialog.callback = [&] { CHECK(fixture.controller.selectSampleReviewer(before, "reviewer-two")); };
  CHECK(!native_ui::confirmStudioSampleReview(fixture.controller, dialog, Decision::Accept));
  CHECK(!fixture.controller.proceduralImportBusy());
  CHECK(fixture.controller.selectUnit(0U));
  CHECK(!fixture.controller.validateSampleReviewContext(before));
  CHECK(!fixture.controller.sampleReviewInspection());
  CHECK(fixture.controller.sampleReviewerId().empty());
}

TEST_CASE("Studio review capture cancels or discards stale asynchronous results without approval") {
  Fixture fixture;
  CHECK(fixture.controller.beginSelectedSampleReview());
  CHECK(!fixture.controller.selectUnit(0U));
  fixture.controller.cancelProceduralCandidateImport();
  CHECK(!collect(fixture.controller));
  CHECK(!fixture.controller.sampleReviewInspection());
  CHECK(!fixture.controller.proceduralImportBusy());
  CHECK(fixture.controller.beginSelectedSampleReview());
  fixture.controller.manifest().units.front().gainDb = -2.0F;
  CHECK(!collect(fixture.controller));
  CHECK(!fixture.controller.sampleReviewInspection());
  const auto recovered = fixture.repository.recover(); CHECK(recovered); CHECK(recovered.value().reviews.empty());
  fixture.capture(); // Failed/cancelled futures are consumed, not a permanent busy latch.
}

TEST_CASE("Studio explicit acceptance publishes an actual new candidate and preserves producer input") {
  Fixture fixture;
  fixture.capture(); fixture.choose(); fixture.decide(Decision::Accept);
  CHECK(fixture.controller.sampleReviewReceipt());
  CHECK(fixture.controller.sampleReviewReceipt()->reviews.front().result == "PASS");
  CHECK(!fixture.controller.sampleReviewInspection()); // Generation-bound packet consumed.
  const auto before = fixture.repository.recover(); CHECK(before);
  const auto originalAudio = core::sha256File(fixture.root / "raw.wav"); CHECK(originalAudio);
  Dialog dialog; dialog.path = fixture.root / "candidate";
  CHECK(native_ui::publishStudioSampleCandidate(fixture.controller, dialog));
  CHECK(dialog.purpose == platform::FileDialogPurpose::PublishSampleCandidate);
  CHECK(collect(fixture.controller));
  CHECK(fixture.controller.publishedSampleCandidate());
  const auto& result = *fixture.controller.publishedSampleCandidate();
  CHECK(result.root == std::filesystem::canonical(dialog.path->parent_path()) / dialog.path->filename());
  CHECK(!result.releaseEligible);
  CHECK(result.durabilityConfirmed);
  CHECK(std::filesystem::exists(result.root / "manifest.json"));
  CHECK(!result.contentSha256.empty()); CHECK(!result.candidateSha256.empty());
  const auto after = fixture.repository.recover(); CHECK(after);
  CHECK(production::encodeProductionProject(after.value()) == production::encodeProductionProject(before.value()));
  CHECK(core::sha256File(fixture.root / "raw.wav").value() == originalAudio.value());
  CHECK(native_ui::publishStudioSampleCandidate(fixture.controller, dialog));
  CHECK(!collect(fixture.controller)); // Occupied destination is never overwritten.
  CHECK(fixture.controller.publishedSampleCandidate()->candidateSha256 == result.candidateSha256);
}

TEST_CASE("Studio rejection and incomplete selected-unit review cannot publish a complete candidate") {
  Fixture fixture(true);
  fixture.capture(); fixture.choose(); fixture.decide(Decision::Accept);
  CHECK(fixture.controller.sampleReviewReceipt());
  CHECK(!fixture.controller.sampleReviewReceipt()->candidate);
  const auto context = fixture.controller.captureSampleReviewContext(); CHECK(context);
  CHECK(fixture.controller.beginSampleCandidatePublication(context.value(), fixture.root / "partial"));
  CHECK(!collect(fixture.controller));
  CHECK(!std::filesystem::exists(fixture.root / "partial"));
  fixture.capture(); fixture.choose(); fixture.decide(Decision::Reject);
  CHECK(fixture.controller.sampleReviewReceipt()->reviews.front().result != "PASS");
  CHECK(fixture.controller.productionProject()->unitAssignments.front().state == production::UnitQueueState::Rejected);
  CHECK(!fixture.controller.publishedSampleCandidate());
}

TEST_CASE("Studio loads an editable manifest asynchronously and rejects stale manifest picker results") {
  Fixture fixture(false, false);
  Dialog dialog; dialog.path = fixture.root / "manifest.json";
  CHECK(native_ui::openStudioSampleManifest(fixture.controller, dialog));
  CHECK(dialog.purpose == platform::FileDialogPurpose::OpenSampleManifest);
  CHECK(collect(fixture.controller)); CHECK(fixture.controller.selectedUnit());
  CHECK(fixture.controller.manifest() == fixture.manifest);
  fixture.capture();
  dialog.callback = [&] { fixture.controller.manifest().units.front().gainDb = -3.0F; };
  CHECK(!native_ui::openStudioSampleManifest(fixture.controller, dialog));
  CHECK(fixture.controller.manifest().units.front().gainDb == -3.0F);
  CHECK(fixture.controller.beginSampleReviewUnitSelection(0U));
  CHECK(collect(fixture.controller));
  CHECK(fixture.controller.manifest().units.front().gainDb == -3.0F);
  CHECK(!fixture.controller.sampleReviewInspection());
}

TEST_CASE("Studio shutdown collects the actual decision outcome even when cancellation races commit") {
  Fixture fixture;
  fixture.capture(); fixture.choose();
  CHECK(fixture.controller.beginSampleReviewDecision(fixture.controller.sampleReviewInspection()->context, "reviewer", Decision::Accept));
  const auto finished = fixture.controller.finishProceduralCandidateImport();
  const auto persisted = fixture.repository.recover(); CHECK(persisted);
  if (persisted.value().reviews.empty()) { CHECK(!finished); CHECK(!fixture.controller.sampleReviewReceipt()); }
  else {
    CHECK(finished); CHECK(fixture.controller.sampleReviewReceipt());
    CHECK(fixture.controller.sampleReviewReceipt()->committedGeneration == persisted.value().lastDurableGeneration);
    CHECK(fixture.controller.sampleReviewReceipt()->reviews.front().reviewId == persisted.value().reviews.back().reviewId);
  }
  CHECK(!fixture.controller.proceduralImportBusy());
}

TEST_CASE("Studio review controls and paged evidence remain bounded at the minimum viewport") {
  Fixture fixture; fixture.capture();
  const auto controls = native_ui::studioSampleReviewControls(fixture.controller, 720.0);
  CHECK(controls.size() == 22U);
  CHECK(std::any_of(controls.begin(), controls.end(), [](const auto& control) {
    return control.id == "register-reviewer" && control.enabled;
  }));
  // The signed-bank row is present but disabled until a candidate has actually been published.
  CHECK(std::any_of(controls.begin(), controls.end(), [](const auto& control) {
    return control.id == "sign-bank" && !control.enabled;
  }));
  CHECK(std::any_of(controls.begin(), controls.end(), [](const auto& control) {
    return control.id == "install-bank" && !control.enabled;
  }));
  CHECK(std::any_of(controls.begin(), controls.end(), [](const auto& control) {
    return control.id == "open-bank-in-song-editor" && !control.enabled;
  }));
  for (const auto& control : controls) {
    CHECK(control.bounds.x >= 0.0); CHECK(control.bounds.right() <= 720.0);
    CHECK(control.bounds.y >= 0.0); CHECK(control.bounds.bottom() < 186.0);
    if (control.id == "accept" || control.id == "reject") CHECK(!control.enabled);
  }
  const auto lines = native_ui::studioSampleReviewDetailLines(fixture.controller, 720.0);
  CHECK(lines.size() > native_ui::studioSampleReviewVisibleLines(520.0));
  for (const auto& line : lines) CHECK(line.size() <= 55U);
  std::string reconstructed;
  for (const auto& line : lines) reconstructed += line;
  CHECK(reconstructed.find("Capture is read-only. Choose the actual registered reviewer, inspect/listen, then explicitly accept or reject.") != std::string::npos);
  CHECK(std::find(lines.begin(), lines.end(), "Capture is read-only. Choose the actual registered ") != lines.end());
  CHECK(std::any_of(lines.begin(), lines.end(), [](const auto& line) { return line.find("AUDIO SHA256") != std::string::npos; }));
}
