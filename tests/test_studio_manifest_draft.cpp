#include "test_framework.hpp"
#include "test_support.hpp"
#include "prepared_phone_donor_fixture.hpp"
#include "english_manifest_draft_fixture.hpp"
#include "seam/native_ui/voicebank_studio.hpp"
#include "seam/platform/file_dialog.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/voicebank_production/project_codec.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace {
struct DonorDialog final : seam::platform::IFileDialog {
  std::optional<std::filesystem::path> path;
  std::optional<seam::platform::PreparedDonorSelectionInput> input;
  bool confirm{false};
  std::string summary;
  std::vector<std::string> assignments;
  std::function<void()> duringChoice, duringConfirmation;
  seam::core::Result<std::optional<std::filesystem::path>> choose(const seam::platform::FileDialogRequest& request) override {
    CHECK(request.purpose==seam::platform::FileDialogPurpose::PreparedDonorPacket); return path;
  }
  seam::core::Result<std::optional<seam::platform::PreparedDonorSelectionInput>> choosePreparedDonor(
      std::string_view, const std::vector<std::string>& choices) override {
    assignments=choices; if (duringChoice) duringChoice(); return input;
  }
  seam::core::Result<bool> confirmPreparedDonorImport(std::string_view text) override {
    summary=text; if (duringConfirmation) duringConfirmation(); return confirm;
  }
};
}

TEST_CASE("Studio captures original donor notices and imports held bytes only after explicit confirmation") {
  using namespace seam;
  test::support::PreparedDonorWorkspaceFixture fixture;
  native_ui::VoicebankStudioController controller;
  CHECK(controller.openProductionProject(fixture.workspace,fixture.project.inventorySha256,"producer"));
  DonorDialog dialog; dialog.path=fixture.prepared.root/"donors.json";
  dialog.input=platform::PreparedDonorSelectionInput{fixture.prepared.id,1U};
  const auto before=voicebank_production::encodeProductionProject(fixture.project);
  const auto settle=[&] {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{10};
    while (controller.proceduralImportBusy()) {
      CHECK(controller.pollProceduralCandidateImport());
      CHECK(std::chrono::steady_clock::now()<deadline);
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
  };
  CHECK(native_ui::captureStudioPreparedDonor(controller,dialog)); settle();
  CHECK(dialog.assignments.size()==2U); CHECK(controller.preparedDonorInspection());
  const auto capture=*controller.preparedDonorInspection();
  CHECK(capture.intake->take.style=="soft"); CHECK(capture.intake->take.pitchLayer==60);
  CHECK(capture.details.find("SYNTHETIC TEST FIXTURE ONLY")!=std::string::npos);
  CHECK(capture.details.find("Synthetic fixture author; original PCM copied")!=std::string::npos);
  CHECK(capture.details.find("SYNTHETIC PRODUCER DECLARATION")!=std::string::npos);
  CHECK(capture.details.find("SOURCE PHONES (AUTOMATIC, UNREVIEWED) [\"aa\"]")!=std::string::npos);
  CHECK(voicebank_production::encodeProductionProject(fixture.repository.recover().value())==before);
  CHECK(native_ui::importStudioPreparedDonor(controller,dialog)); CHECK(!controller.proceduralImportBusy());
  CHECK(!controller.preparedDonorReceipt());
  CHECK(dialog.summary.find(capture.captureSha256)!=std::string::npos);
  CHECK(dialog.summary.find("can reopen earlier take reviews")!=std::string::npos);
  // The importer must consume captured material even if preparation disappears.
  for (const auto name : {"donors.json","intake-plan.json","license-source.txt","attribution.txt"})
    CHECK(std::filesystem::remove(fixture.prepared.root/name));
  CHECK(std::filesystem::remove(fixture.prepared.root/(fixture.prepared.id+".wav")));
  dialog.confirm=true;
  CHECK(native_ui::importStudioPreparedDonor(controller,dialog)); settle();
  CHECK(controller.preparedDonorReceipt()); CHECK(!controller.preparedDonorInspection());
  auto recovered=fixture.repository.recover(); CHECK(recovered); CHECK(fixture.repository.verify(recovered.value()));
  CHECK(recovered.value().lastDurableGeneration==fixture.project.lastDurableGeneration+1U);
  CHECK(recovered.value().takes.size()==1U); CHECK(recovered.value().takes.front().style=="soft");
  CHECK(recovered.value().takes.front().state==voicebank_production::UnitQueueState::MarkerReview);
  CHECK(recovered.value().sourceBindings.front().preparedDonor->packetSha256==fixture.prepared.packetSha);
  CHECK(recovered.value().reviews.empty()); CHECK(recovered.value().sourceQualityAssessments.empty());
  CHECK(!voicebank_production::selectedStrategyReady(recovered.value()));
  CHECK(!controller.beginPreparedDonorImport(capture.context,capture.captureSha256));
  const auto lines=native_ui::studioSampleReviewDetailLines(controller,720);
  CHECK(std::any_of(lines.begin(),lines.end(),[](const auto& line) { return line.find("LAST DONOR IMPORT")!=std::string::npos; }));
}

TEST_CASE("Studio donor capture rejects invalid choices, cancelled work and stale modal contexts without imports") {
  using namespace seam;
  test::support::PreparedDonorWorkspaceFixture fixture;
  native_ui::VoicebankStudioController controller;
  CHECK(controller.openProductionProject(fixture.workspace,fixture.project.inventorySha256,"producer"));
  const auto before=voicebank_production::encodeProductionProject(fixture.project);
  DonorDialog dialog;
  CHECK(native_ui::captureStudioPreparedDonor(controller,dialog)); CHECK(!controller.proceduralImportBusy());
  dialog.path=fixture.prepared.root/"donors.json";
  CHECK(native_ui::captureStudioPreparedDonor(controller,dialog)); CHECK(!controller.proceduralImportBusy());
  const auto context=controller.captureSampleReviewContext().value();
  CHECK(!controller.beginPreparedDonorCapture(context,*dialog.path,fixture.prepared.id,99U));
  CHECK(!controller.beginPreparedDonorCapture(context,fixture.prepared.root/"other.json",fixture.prepared.id,0U));
  CHECK(!controller.beginPreparedDonorImport(context,"uncaptured"));
  dialog.input=platform::PreparedDonorSelectionInput{fixture.prepared.id,0U};
  CHECK(native_ui::captureStudioPreparedDonor(controller,dialog));
  controller.cancelProceduralCandidateImport();
  bool refused=false;
  while (controller.proceduralImportBusy()) {
    if (!controller.pollProceduralCandidateImport()) refused=true;
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  CHECK(refused); CHECK(!controller.preparedDonorInspection());
  dialog.duringChoice=[&] { CHECK(controller.openProductionProject(fixture.workspace,fixture.project.inventorySha256,"producer")); };
  CHECK(!native_ui::captureStudioPreparedDonor(controller,dialog)); CHECK(!controller.proceduralImportBusy());
  CHECK(!controller.preparedDonorReceipt());
  CHECK(voicebank_production::encodeProductionProject(fixture.repository.recover().value())==before);
}

TEST_CASE("Studio donor confirmation cannot substitute a recaptured assignment under the same context") {
  using namespace seam;
  test::support::PreparedDonorWorkspaceFixture fixture;
  native_ui::VoicebankStudioController controller;
  CHECK(controller.openProductionProject(fixture.workspace,fixture.project.inventorySha256,"producer"));
  const auto context=controller.captureSampleReviewContext().value();
  const auto settle=[&] {
    while (controller.proceduralImportBusy()) {
      CHECK(controller.pollProceduralCandidateImport()); std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
  };
  CHECK(controller.beginPreparedDonorCapture(context,fixture.prepared.root/"donors.json",fixture.prepared.id,0U)); settle();
  DonorDialog dialog; dialog.confirm=true;
  dialog.duringConfirmation=[&] {
    CHECK(controller.beginPreparedDonorCapture(context,fixture.prepared.root/"donors.json",fixture.prepared.id,1U)); settle();
  };
  CHECK(!native_ui::importStudioPreparedDonor(controller,dialog));
  CHECK(controller.preparedDonorInspection()->intake->take.style=="soft"); CHECK(!controller.preparedDonorReceipt());
  CHECK(fixture.repository.recover().value().takes.empty());
}

TEST_CASE("Shared donor intake revalidates caller-mutated snapshots before displaying source notices") {
  using namespace seam;
  test::support::PreparedDonorWorkspaceFixture fixture;
  auto held=fixture.prepared.load(); held.evidence.proposalJson="{}"; held.frames=1U;
  auto captured=voicebank_production::capturePreparedDonorIntake(fixture.workspace,fixture.project,held,
      fixture.prepared.packetSha,fixture.prepared.id,fixture.project.selectedSourceStrategyId,"sustain:aa",60,"neutral");
  CHECK(captured); CHECK(captured.value().snapshot.frames==80U);
  held.evidence.packet.bytes.clear();
  CHECK(!voicebank_production::capturePreparedDonorIntake(fixture.workspace,fixture.project,std::move(held),
      fixture.prepared.packetSha,fixture.prepared.id,fixture.project.selectedSourceStrategyId,"sustain:aa",60,"neutral"));
  CHECK(fixture.repository.recover().value().takes.empty());
}

namespace {
using namespace seam;
namespace production = voicebank_production;
using Controller = native_ui::VoicebankStudioController;

core::Result<void> collectDraft(Controller& controller) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
  while (controller.proceduralImportBusy()) {
    const auto result = controller.pollProceduralCandidateImport(); if (!result) return result;
    if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("Studio draft worker exceeded test deadline");
    if (controller.proceduralImportBusy()) std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  return core::success();
}

struct Fixture final {
  std::filesystem::path root{test::support::temporaryDirectory("studio-manifest-draft")};
  production::ProductionProjectRepository repository{root / "producer"};
  production::VoicebankProductionProject project;
  production::SampleManifestDraftIdentity identity{"native.draft", "0.1.0", "Synthetic native draft", domain::Language::Korean, "warm"};
  Controller controller;
  explicit Fixture(bool missing = false, bool redistributable = false, bool sourceFree = false, bool styleOwned = false) {
    const auto license = root / "synthetic-license.txt";
    CHECK(core::durableAtomicWriteTextNew(license, "SYNTHETIC NATIVE DRAFT TEST ONLY. No singer qualification."));
    const auto hash = core::sha256File(license); CHECK(hash);
    project = {.projectId = "native-draft", .inventoryId = "fixture", .inventorySha256 = std::string(64U,'a'),
        .selectedSourceStrategyId = "synthetic", .licenseLocator = license.string(), .licenseSha256 = hash.value()};
    project.sourceStrategies = {{.id = "synthetic", .kind = production::SourceStrategyKind::ProceduralSynthesis,
        .rights = production::Feasibility::Pass, .coverage = production::Feasibility::NotAssessed, .listening = production::Feasibility::NotAssessed,
        .permissions = {true,true,redistributable,redistributable}, .licenseLocator = license.string(), .licenseSha256 = hash.value(), .evidenceState = "SYNTHETIC_TEST_ONLY"}};
    project.operators = {{"producer", "PRODUCER"}, {"reviewer", "REVIEWER"}};
    project.unitAssignments = {{.coverageKey = "sustain:a", .pitchLayer = 69, .promptId = "a", .plannedTakeId = "take-a"}};
    if (missing) project.unitAssignments.push_back({.coverageKey = "sustain:i", .pitchLayer = 69, .promptId = "i", .plannedTakeId = "take-i"});
    if (styleOwned) {
      project.schemaVersion = production::kProductionStyleSchemaVersion; project.language = "ko";
      project.declaredPitchLayers = {69};
      for (auto& row : project.unitAssignments) row.style = "warm";
      project.unitAssignments.push_back({.coverageKey = "sustain:a", .pitchLayer = 69, .promptId = "soft-a", .plannedTakeId = "soft-a", .style = "soft"});
    }
    if (sourceFree) {
      project.sourceStrategies.clear(); project.selectedSourceStrategyId.clear(); project.licenseLocator.clear(); project.licenseSha256.clear();
    }
    CHECK(repository.initialize(project, {.action = "create", .subjectId = project.projectId, .operatorId = "producer", .occurredAtUtc = "2026-09-09T10:00:00Z"}));
    CHECK(voicebank::writeWav(root / "raw.wav", {.sampleRate = 48000U, .channels = 1U, .sampleFormat = voicebank::WavSampleFormat::Pcm24},
        test::support::sineWave(48000U,440.0,0.2,0.25F)));
    if (!sourceFree) CHECK(repository.importRaw(project, root / "raw.wav", {.takeId = "take-a", .promptId = "a", .coverageKey = "sustain:a", .pitchLayer = 69, .style = styleOwned ? "warm" : ""},
        {.action = "import", .subjectId = "take-a", .operatorId = "producer", .occurredAtUtc = "2026-09-09T10:01:00Z"}));
    if (styleOwned && !sourceFree) CHECK(repository.importRaw(project, root / "raw.wav",
        {.takeId = "soft-a", .promptId = "soft-a", .coverageKey = "sustain:a", .pitchLayer = 69, .style = "soft"},
        {"import", "soft-a", "producer", "2026-09-13T00:00:00Z"}));
    CHECK(controller.openProductionProject(root / "producer", project.inventorySha256, "producer"));
    CHECK(controller.manifest().units.empty());
  }
  void begin(std::string name = "draft", production::SampleManifestDraftOptions options = {}) {
    const auto context = controller.captureSampleReviewContext(); CHECK(context);
    CHECK(controller.beginSampleManifestDraftCreation(context.value(), identity, root / name, std::move(options)));
  }
  void createLoaded() { begin(); CHECK(collectDraft(controller)); CHECK(controller.createdSampleManifestDraft()); CHECK(controller.selectedUnit()); }
  void selectStyle(std::string_view style) {
    const auto& units = controller.manifest().units;
    const auto found = std::find_if(units.begin(), units.end(),
        [&](const auto& unit) { return unit.style == style; });
    CHECK(found != units.end());
    CHECK(controller.selectUnit(static_cast<std::size_t>(found - units.begin())));
  }
  void sourceUnchanged() const {
    const auto recovered = repository.recover(); CHECK(recovered);
    CHECK(production::encodeProductionProject(recovered.value()) == production::encodeProductionProject(project));
    CHECK(production::encodeProductionProject(*controller.productionProject()) == production::encodeProductionProject(project));
    CHECK(controller.productionProject()->reviews.empty());
    CHECK(!production::requireTakeSourceQualification(*controller.productionProject(), "take-a"));
  }
};

struct IdentityDialog final : platform::IFileDialog {
  std::optional<platform::SampleManifestDraftIdentityInput> identity;
  std::optional<std::filesystem::path> destination;
  std::function<void()> duringIdentity, duringDestination;
  std::size_t identityCalls{0U}, destinationCalls{0U};
  std::optional<platform::FileDialogPurpose> purpose;
  core::Result<std::optional<platform::SampleManifestDraftIdentityInput>> chooseSampleManifestDraftIdentity() override {
    ++identityCalls; if (duringIdentity) duringIdentity(); return identity;
  }
  core::Result<std::optional<std::filesystem::path>> choose(const platform::FileDialogRequest& request) override {
    ++destinationCalls; purpose = request.purpose; if (duringDestination) duringDestination(); return destination;
  }
};

// Own-thread test control; the worker captures only shared synchronization,
// never a controller pointer. Always release before the fixture joins workers.
struct StageGate final {
  struct State final { std::mutex mutex; std::condition_variable changed; bool arrived{false}, released{false}; };
  std::shared_ptr<State> state{std::make_shared<State>()};
  ~StageGate() { release(); }
  void release() { std::lock_guard lock(state->mutex); state->released = true; state->changed.notify_all(); }
  void wait() {
    std::unique_lock lock(state->mutex);
    if (!state->changed.wait_for(lock, std::chrono::seconds{10}, [&] { return state->arrived; }))
      throw std::runtime_error("Draft worker did not reach test checkpoint");
  }
  production::SampleManifestDraftOptions pauseAt(production::ManifestDraftStage target) const {
    return {.faultInjector = [shared = state, target](production::ManifestDraftStage stage) -> core::Result<void> {
      if (stage == target) {
        std::unique_lock lock(shared->mutex); shared->arrived = true; shared->changed.notify_all();
        shared->changed.wait(lock, [&] { return shared->released; });
      }
      return core::success();
    }};
  }
  production::PreparedDonorImportOptions pauseAt(production::PreparedDonorImportStage target) const {
    return {.faultInjector = [shared = state, target](production::PreparedDonorImportStage stage) -> core::Result<void> {
      if (stage == target) {
        std::unique_lock lock(shared->mutex); shared->arrived = true; shared->changed.notify_all();
        shared->changed.wait(lock, [&] { return shared->released; });
      }
      return core::success();
    }};
  }
};

struct DonorReceiptFixture final {
  test::support::PreparedDonorWorkspaceFixture source;
  Controller controller;
  Controller::PreparedDonorInspection capture;
  DonorReceiptFixture() {
    CHECK(controller.openProductionProject(source.workspace, source.project.inventorySha256, "producer"));
    const auto context = controller.captureSampleReviewContext(); CHECK(context);
    CHECK(controller.beginPreparedDonorCapture(context.value(), source.prepared.root / "donors.json", source.prepared.id, 0U));
    CHECK(collectDraft(controller)); CHECK(controller.preparedDonorInspection());
    capture = *controller.preparedDonorInspection();
  }
  void begin(production::PreparedDonorImportOptions options = {}) {
    CHECK(controller.beginPreparedDonorImport(capture.context, capture.captureSha256, std::move(options)));
  }
  production::VoicebankProductionProject committed() {
    const auto recovered = source.repository.recover(); CHECK(recovered);
    const auto& receipt = controller.preparedDonorReceipt(); CHECK(receipt);
    CHECK(receipt->committedGeneration == source.project.lastDurableGeneration + 1U);
    CHECK(receipt->committedGeneration == recovered.value().lastDurableGeneration);
    CHECK(receipt->committedProjectSha256 == core::sha256Hex(production::encodeProductionProject(recovered.value())));
    CHECK(recovered.value().takes.size() == 1U);
    CHECK(recovered.value().takes.front().takeId == capture.intake->take.takeId);
    CHECK(recovered.value().takes.front().state == production::UnitQueueState::MarkerReview);
    CHECK(recovered.value().reviews.empty()); CHECK(recovered.value().sourceQualityAssessments.empty());
    CHECK(!controller.preparedDonorInspection());
    return recovered.value();
  }
};

struct VisualSnapshot final {
  voicebank::Manifest manifest;
  std::filesystem::path path;
  std::size_t selected;
  std::vector<ui::WaveformColumn> waveform;
  voicebank::Spectrogram spectrogram;
  explicit VisualSnapshot(const Controller& controller)
      : manifest(controller.manifest()), path(controller.manifestPath()), selected(controller.selectedIndex()),
        waveform(controller.microscope().waveform()), spectrogram(controller.microscope().spectrogram()) {}
  void check(const Controller& controller, bool geometryUnchanged = true) const {
    CHECK(controller.manifest() == manifest); CHECK(controller.manifestPath() == path);
    CHECK(controller.selectedIndex() == selected); CHECK(controller.microscope().unit() == controller.selectedUnit());
    const auto& current = controller.microscope().waveform(); CHECK(current.size() == waveform.size());
    for (std::size_t i = 0U; i < current.size(); ++i) {
      CHECK(current[i].minimum == waveform[i].minimum); CHECK(current[i].maximum == waveform[i].maximum);
      CHECK(current[i].rms == waveform[i].rms); if (geometryUnchanged) CHECK(current[i].x == waveform[i].x);
    }
    CHECK(controller.microscope().spectrogram().decibels == spectrogram.decibels);
    CHECK(controller.microscope().spectrogram().columns == spectrogram.columns);
    CHECK(controller.microscope().spectrogram().bins == spectrogram.bins);
  }
};
} // namespace

TEST_CASE("Studio donor import cancellation before commit preserves inspection and permits an explicit retry") {
  DonorReceiptFixture fixture; StageGate gate;
  const auto before = production::encodeProductionProject(fixture.source.project);
  const auto pointerSha = core::sha256File(fixture.source.workspace / "project.json"); CHECK(pointerSha);
  fixture.begin(gate.pauseAt(production::PreparedDonorImportStage::NoticesRetained)); gate.wait();
  fixture.controller.cancelProceduralCandidateImport(); gate.release();
  const auto refused = collectDraft(fixture.controller); CHECK(!refused);
  CHECK(refused.error().code == core::ErrorCode::Conflict);
  CHECK(refused.error().message == "Prepared donor import cancelled before commit");
  CHECK(!fixture.controller.preparedDonorReceipt()); CHECK(fixture.controller.preparedDonorInspection());
  CHECK(fixture.controller.preparedDonorInspection()->captureSha256 == fixture.capture.captureSha256);
  CHECK(production::encodeProductionProject(fixture.source.repository.recover().value()) == before);
  CHECK(production::encodeProductionProject(*fixture.controller.productionProject()) == before);
  CHECK(core::sha256File(fixture.source.workspace / "project.json").value() == pointerSha.value());
  // Notice/audio staging is allowed on refusal; no generation is committed.
  fixture.begin(); CHECK(collectDraft(fixture.controller));
  const auto recovered = fixture.committed(); CHECK(fixture.source.repository.verify(recovered));
  CHECK(fixture.controller.preparedDonorReceipt()->durabilityConfirmed);
  std::cout << "[DONOR-RECEIPT] PRECOMMIT_CANCEL_NO_COMMIT / INSPECTION_RETRY_COMMITTED\n";
}

TEST_CASE("Studio donor receipt survives cancellation after repository publication before UI adoption") {
  DonorReceiptFixture fixture; fixture.begin();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
  for (;;) {
    const auto recovered = fixture.source.repository.recover();
    if (recovered && recovered.value().lastDurableGeneration > fixture.source.project.lastDurableGeneration) break;
    CHECK(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  fixture.controller.cancelProceduralCandidateImport(); CHECK(collectDraft(fixture.controller));
  const auto recovered = fixture.committed(); CHECK(fixture.source.repository.verify(recovered));
  CHECK(fixture.controller.preparedDonorReceipt()->durabilityConfirmed);
  CHECK(production::encodeProductionProject(*fixture.controller.productionProject()) == production::encodeProductionProject(recovered));
  CHECK(fixture.controller.sampleReviewStatus().find("DONOR IMPORTED / UNAPPROVED") != std::string::npos);
  std::cout << "[DONOR-RECEIPT] LATE_CANCEL_COMMIT_ADOPTED\n";
}

TEST_CASE("Studio donor receipt preserves a newer editor context and reports unconfirmed pointer durability") {
  for (const bool obstructPointer : {false, true}) {
    DonorReceiptFixture fixture; StageGate gate;
    auto options = gate.pauseAt(production::PreparedDonorImportStage::NoticesRetained);
    const auto pause = options.faultInjector;
    options.faultInjector = [pause, root = fixture.source.workspace, obstructPointer](auto stage) {
      const auto paused = pause(stage); if (!paused) return paused;
      if (obstructPointer && stage == production::PreparedDonorImportStage::NoticesRetained) {
        CHECK(std::filesystem::remove(root / "project.json"));
        CHECK(std::filesystem::create_directory(root / "project.json"));
      }
      return core::success();
    };
    fixture.begin(std::move(options)); gate.wait();
    // Mutable editor state may change independently of the worker. Keep a
    // valid new model, not an invalid manifest that merely fails validation.
    const auto newer = test::support::makeManifest({test::support::makeUnit("new-editor-unit", {"a"}, "editor.wav")});
    CHECK(newer.validate()); fixture.controller.manifest() = newer;
    CHECK(!fixture.controller.validateSampleReviewContext(fixture.capture.context));
    const auto originalProject = production::encodeProductionProject(*fixture.controller.productionProject());
    gate.release(); CHECK(collectDraft(fixture.controller));
    const auto recovered = fixture.committed();
    CHECK(fixture.controller.manifest() == newer);
    CHECK(production::encodeProductionProject(*fixture.controller.productionProject()) == originalProject);
    CHECK(fixture.controller.sampleReviewStatus().find("CONTEXT CHANGED: REOPEN PRODUCER") != std::string::npos);
    const auto& receipt = *fixture.controller.preparedDonorReceipt();
    CHECK(receipt.durabilityConfirmed == !obstructPointer);
    if (obstructPointer) {
      CHECK(!receipt.diagnostic.empty());
      CHECK(fixture.controller.sampleReviewStatus().find("DURABILITY UNCONFIRMED: " + receipt.diagnostic) != std::string::npos);
      CHECK(!fixture.source.repository.verify(recovered));
      CHECK(std::filesystem::remove(fixture.source.workspace / "project.json"));
      CHECK(fixture.source.repository.reconcileCurrentPointer(receipt.committedGeneration, receipt.committedProjectSha256));
    }
    CHECK(fixture.source.repository.verify(recovered));
    CHECK(!fixture.controller.beginPreparedDonorImport(fixture.capture.context, fixture.capture.captureSha256));
    CHECK(production::encodeProductionProject(fixture.source.repository.recover().value()) == production::encodeProductionProject(recovered));
    std::cout << "[DONOR-RECEIPT] STALE_EDITOR_PRESERVED / "
              << (obstructPointer ? "UNCONFIRMED_POINTER_DIAGNOSTIC" : "DURABLE_COMMIT") << '\n';
  }
}

TEST_CASE("Studio reconciles a confirmed donor generation without repeating import or changing its historical receipt") {
  DonorReceiptFixture fixture;
  fixture.begin({.faultInjector=[&](production::PreparedDonorImportStage stage) {
    if (stage == production::PreparedDonorImportStage::NoticesRetained) {
      CHECK(std::filesystem::remove(fixture.source.workspace / "project.json"));
      CHECK(std::filesystem::create_directory(fixture.source.workspace / "project.json"));
    }
    return core::success();
  }});
  CHECK(collectDraft(fixture.controller)); const auto recovered = fixture.committed();
  const auto original = *fixture.controller.preparedDonorReceipt(); CHECK(!original.durabilityConfirmed);
  const auto bytes = production::encodeProductionProject(recovered);
  CHECK(fixture.controller.beginProductionPointerReconciliation());
  CHECK(!collectDraft(fixture.controller));
  CHECK(std::filesystem::is_directory(fixture.source.workspace / "project.json"));
  CHECK(!fixture.controller.reconciledProductionPointer());
  CHECK(production::encodeProductionProject(fixture.source.repository.recover().value()) == bytes);
  CHECK(std::filesystem::remove(fixture.source.workspace / "project.json"));
  CHECK(fixture.controller.beginProductionPointerReconciliation());
  // The pointer can be durable before UI polling. Late stop and a valid newer
  // editor model must not hide the result or overwrite that model.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
  while (!fixture.source.repository.verify(recovered)) {
    CHECK(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  const auto newer = test::support::makeManifest({test::support::makeUnit("pointer-editor", {"a"}, "editor.wav")});
  CHECK(newer.validate()); fixture.controller.manifest() = newer;
  fixture.controller.cancelProceduralCandidateImport(); CHECK(collectDraft(fixture.controller));
  const auto& receipt = fixture.controller.reconciledProductionPointer(); CHECK(receipt);
  CHECK(receipt->workspaceRoot == std::filesystem::absolute(fixture.source.workspace).lexically_normal());
  CHECK(receipt->projectId == recovered.projectId); CHECK(receipt->generation == original.committedGeneration);
  CHECK(receipt->projectSha256 == original.committedProjectSha256);
  CHECK(fixture.controller.manifest() == newer);
  CHECK(fixture.controller.sampleReviewStatus().find("CONTEXT CHANGED: REOPEN PRODUCER") != std::string::npos);
  CHECK(!fixture.controller.preparedDonorReceipt()->durabilityConfirmed);
  CHECK(fixture.controller.preparedDonorReceipt()->diagnostic == original.diagnostic);
  CHECK(production::encodeProductionProject(*fixture.controller.productionProject()) == bytes);
  CHECK(production::encodeProductionProject(fixture.source.repository.recover().value()) == bytes);
  CHECK(fixture.source.repository.verify(recovered));
  CHECK(fixture.controller.beginProductionPointerReconciliation()); CHECK(collectDraft(fixture.controller));
  CHECK(production::encodeProductionProject(fixture.source.repository.recover().value()) == bytes);
  std::cout << "[POINTER-RECONCILIATION] OBSTRUCTION_REFUSED / EXACT_DURABLE_REPAIR / LATE_CANCEL_EDITOR_PRESERVED\n";
}

TEST_CASE("Studio refuses to reconcile its older producer snapshot over newer durable work") {
  DonorReceiptFixture fixture; Controller older;
  CHECK(older.openProductionProject(fixture.source.workspace,fixture.source.project.inventorySha256,"producer"));
  const auto before = production::encodeProductionProject(*older.productionProject());
  fixture.begin(); CHECK(collectDraft(fixture.controller)); const auto recovered = fixture.committed();
  CHECK(older.beginProductionPointerReconciliation()); CHECK(!collectDraft(older));
  CHECK(older.sampleReviewStatus().find("newer or incomplete work") != std::string::npos);
  CHECK(!older.reconciledProductionPointer());
  CHECK(production::encodeProductionProject(*older.productionProject()) == before);
  CHECK(fixture.source.repository.verify(recovered));
  CHECK(production::encodeProductionProject(fixture.source.repository.recover().value()) == production::encodeProductionProject(recovered));
}

TEST_CASE("Studio English generated producer draft retains stress contract through creation save and reopen") {
  test::support::EnglishManifestDraftFixture fixture;
  Controller controller;
  CHECK(controller.openProductionProject(fixture.producerRoot, fixture.project.inventorySha256, "producer"));
  const auto context = controller.captureSampleReviewContext(); CHECK(context);
  CHECK(controller.beginSampleManifestDraftCreation(context.value(), fixture.identity, fixture.root / "editable"));
  CHECK(collectDraft(controller)); CHECK(controller.createdSampleManifestDraft());
  CHECK(controller.manifest().languageContract); CHECK(controller.manifest().expectedSampleRate == 16000U);
  CHECK(controller.manifest().matchedPhone("iy1") == "iy");
  const auto original = controller.manifest();
  CHECK(controller.save());
  Controller reopened; CHECK(reopened.openManifest(controller.manifestPath()));
  CHECK(reopened.manifest() == original); CHECK(reopened.manifest().matchedPhone("iy2") == "iy");
  CHECK(fixture.repository.recover().value().reviews.empty());
}

TEST_CASE("Studio interactive workspace opening requires the expected inventory and registered producer") {
  Fixture f;
  const auto epoch=f.controller.productionSessionEpoch();
  const auto original=*f.controller.productionProject();
  CHECK(!f.controller.openProductionProject(f.root/"producer",f.project.inventorySha256,"unknown",true));
  CHECK(!f.controller.openProductionProject(f.root/"producer",f.project.inventorySha256,"reviewer",true));
  CHECK(f.controller.productionSessionEpoch()==epoch);
  CHECK(production::encodeProductionProject(*f.controller.productionProject())==production::encodeProductionProject(original));
  CHECK(!f.controller.openProductionProject(f.root/"producer",std::string(64U,'b'),"producer",true));
  CHECK(f.controller.productionSessionEpoch()==epoch);
  CHECK(f.controller.openProductionProject(f.root/"producer",f.project.inventorySha256,"producer",true));
  CHECK(f.controller.productionSessionEpoch()==epoch+1U);
  CHECK(production::encodeProductionProject(*f.controller.productionProject())==production::encodeProductionProject(original));
}

TEST_CASE("Studio asynchronously creates and opens every assignment-owned style") {
  Fixture fixture{false, false, false, true};
  fixture.createLoaded();
  CHECK(fixture.controller.manifest().styles == (std::vector<std::string>{"soft", "warm"}));
  CHECK(fixture.controller.manifest().units.size() == 2U);
  CHECK(fixture.controller.manifest().units[0].style != fixture.controller.manifest().units[1].style);
  CHECK(fixture.controller.createdSampleManifestDraft()->missingAssignments.empty());
  fixture.sourceUnchanged();
}

TEST_CASE("Studio queue state follows the owned style") {
  Fixture fixture{false, false, false, true};
  fixture.createLoaded();
  fixture.project.takes[0].state = production::UnitQueueState::Rejected;
  fixture.project.unitAssignments[0].state = production::UnitQueueState::Rejected;
  CHECK(fixture.repository.save(fixture.project,
      {.action = "save", .subjectId = "take-a", .operatorId = "producer",
       .occurredAtUtc = "2026-10-15T04:00:00Z"}));
  CHECK(fixture.controller.openProductionProject(fixture.root / "producer", fixture.project.inventorySha256, "producer"));
  fixture.selectStyle("warm");
  CHECK(fixture.controller.productionStateForUnit(*fixture.controller.selectedUnit()) == production::UnitQueueState::Rejected);
  fixture.selectStyle("soft");
  CHECK(fixture.controller.productionStateForUnit(*fixture.controller.selectedUnit()) == production::UnitQueueState::MarkerReview);
  fixture.sourceUnchanged();
}

TEST_CASE("Studio queue state refuses an unowned style") {
  Fixture fixture{false, false, false, true};
  fixture.createLoaded();
  auto unit = *fixture.controller.selectedUnit();
  unit.style = "unowned";
  CHECK(!fixture.controller.productionStateForUnit(unit));
  fixture.sourceUnchanged();
}

TEST_CASE("Studio queue state preserves legacy style matching") {
  Fixture fixture;
  fixture.createLoaded();
  auto unit = *fixture.controller.selectedUnit();
  unit.style = "legacy-manifest-style";
  CHECK(fixture.controller.productionStateForUnit(unit) == production::UnitQueueState::MarkerReview);
  fixture.sourceUnchanged();
}

TEST_CASE("Studio style-owned bank selection retakes only the selected style") {
  Fixture fixture{false, false, false, true};
  fixture.createLoaded(); fixture.selectStyle("soft");
  CHECK(fixture.controller.inspectSelectedProductionTake(fixture.root / "raw.wav"));
  CHECK(fixture.controller.importSelectedTake(fixture.root / "raw.wav", "2026-10-15T03:00:00Z"));
  const auto durable = fixture.repository.recover(); CHECK(durable);
  CHECK(durable.value().takes.size() == 3U);
  const auto& imported = durable.value().takes.back();
  if (imported.style != "soft")
    throw test::Failure{"Selected soft unit retake was stored in style " + imported.style +
        ", superseding " + imported.supersedesTakeId};
  CHECK(imported.supersedesTakeId == "soft-a");
  CHECK(durable.value().unitAssignments[0].takeId == "take-a");
  CHECK(durable.value().unitAssignments[1].takeId == imported.takeId);
  CHECK(imported.state == production::UnitQueueState::MarkerReview || imported.state == production::UnitQueueState::Rejected);
  CHECK(durable.value().reviews.empty());
  CHECK(production::encodeProductionProject(durable.value()) == production::encodeProductionProject(*fixture.controller.productionProject()));
}

TEST_CASE("Studio style-owned marker save targets only the selected style") {
  Fixture fixture{false, false, false, true};
  fixture.createLoaded(); fixture.selectStyle("soft");
  const auto frame = fixture.controller.selectedUnit()->markers.stableStart + 8;
  CHECK(fixture.controller.moveSelectedMarker(ui::AcousticMarkerKind::StableStart,
      fixture.controller.microscope().frameToPixel(frame)));
  CHECK(fixture.controller.save());
  const auto durable = fixture.repository.recover(); CHECK(durable);
  CHECK(durable.value().metadataRevisions.size() == fixture.project.metadataRevisions.size() + 1U);
  const auto& metadata = durable.value().metadataRevisions.back();
  CHECK(metadata.kind == "MARKERS_AND_PITCH");
  if (metadata.takeId != "soft-a")
    throw test::Failure{"Selected soft unit markers were stored on take " + metadata.takeId};
  CHECK(durable.value().takes.size() == fixture.project.takes.size());
  for (std::size_t i = 0U; i < fixture.project.takes.size(); ++i) {
    CHECK(durable.value().takes[i].takeId == fixture.project.takes[i].takeId);
    CHECK(durable.value().takes[i].style == fixture.project.takes[i].style);
    CHECK(durable.value().takes[i].rawAssetSha256 == fixture.project.takes[i].rawAssetSha256);
  }
  CHECK(durable.value().sourceBindings == fixture.project.sourceBindings);
  CHECK(durable.value().unitAssignments[0].takeId == "take-a");
  CHECK(durable.value().unitAssignments[1].takeId == "soft-a");
  CHECK(durable.value().reviews.empty());
}

TEST_CASE("Studio style-owned bank refuses an unowned selected style") {
  Fixture fixture{false, false, false, true};
  auto unit = test::support::makeUnit("unowned-a", {"a"}, "raw.wav", 69,
      voicebank::UnitKind::Sustain, 9600U);
  unit.style = "unowned";
  auto manifest = test::support::makeManifest({unit});
  manifest.language = domain::Language::Korean; manifest.styles = {"unowned"};
  const auto path = fixture.root / "unowned.json";
  CHECK(voicebank::ManifestJsonCodec{}.save(manifest, path));
  CHECK(fixture.controller.openManifest(path));
  CHECK(fixture.controller.openProductionProject(fixture.root / "producer", fixture.project.inventorySha256, "producer"));
  CHECK(!fixture.controller.inspectSelectedProductionTake(fixture.root / "raw.wav"));
  CHECK(!fixture.controller.importSelectedTake(fixture.root / "raw.wav", "2026-10-15T03:00:00Z"));
  const auto durable = fixture.repository.recover(); CHECK(durable);
  CHECK(production::encodeProductionProject(durable.value()) == production::encodeProductionProject(fixture.project));
  CHECK(production::encodeProductionProject(*fixture.controller.productionProject()) == production::encodeProductionProject(fixture.project));
}

TEST_CASE("Studio asynchronous workspace entry defers adoption and cancellation retains the empty context") {
  Fixture f;
  Controller cancelled;
  CHECK(cancelled.beginOpenProductionProject(f.root/"producer",f.project.inventorySha256,"producer"));
  CHECK(cancelled.proceduralImportBusy()); CHECK(cancelled.workspaceOpening()); CHECK(!cancelled.productionProject());
  CHECK(!cancelled.beginOpenProductionProject(f.root/"producer",f.project.inventorySha256,"producer"));
  CHECK(!cancelled.openProductionProject(f.root/"producer",f.project.inventorySha256,"producer"));
  cancelled.cancelProceduralCandidateImport(); CHECK(collectDraft(cancelled));
  CHECK(!cancelled.workspaceOpening());
  CHECK(!cancelled.productionProject()); CHECK(cancelled.productionSessionEpoch()==0U);
  CHECK(cancelled.beginOpenProductionProject(f.root/"producer",f.project.inventorySha256,"producer"));
  CHECK(collectDraft(cancelled)); CHECK(cancelled.productionProject());
  CHECK(cancelled.productionSessionEpoch()==1U);
  CHECK(production::encodeProductionProject(*cancelled.productionProject())==production::encodeProductionProject(*f.controller.productionProject()));
  CHECK(!cancelled.beginOpenProductionProject(f.root/"producer",f.project.inventorySha256,"producer"));
  Controller invalid;
  CHECK(invalid.beginOpenProductionProject(f.root/"producer",f.project.inventorySha256,"reviewer"));
  CHECK(!collectDraft(invalid)); CHECK(!invalid.productionProject()); CHECK(!invalid.proceduralImportBusy());
  CHECK(!invalid.workspaceOpening());
  CHECK(invalid.productionSessionEpoch()==0U);
  CHECK(invalid.beginOpenProductionProject(f.root/"producer",f.project.inventorySha256,"producer"));
  CHECK(invalid.finishProceduralCandidateImport()); CHECK(!invalid.productionProject());
}

TEST_CASE("Studio creates and opens an editable draft without a preexisting manifest or approval") {
  for (const auto& [code, language] : std::vector<std::pair<std::string, domain::Language>>{
      {"ja",domain::Language::Japanese},{"en",domain::Language::English},{"ko",domain::Language::Korean}}) {
    Fixture fixture;
    IdentityDialog dialog;
    dialog.identity = platform::SampleManifestDraftIdentityInput{fixture.identity.id, fixture.identity.version, fixture.identity.displayName, code, fixture.identity.style};
    dialog.destination = fixture.root / "draft";
    CHECK(native_ui::createStudioSampleManifestDraft(fixture.controller, dialog));
    CHECK(dialog.purpose == platform::FileDialogPurpose::CreateSampleManifestDraft);
    CHECK(collectDraft(fixture.controller));
    CHECK(fixture.controller.createdSampleManifestDraft()); CHECK(fixture.controller.selectedUnit());
    const auto& receipt = *fixture.controller.createdSampleManifestDraft();
    CHECK(!receipt.releaseEligible); CHECK(receipt.durabilityConfirmed);
    CHECK(receipt.root == std::filesystem::canonical(dialog.destination->parent_path()) / dialog.destination->filename());
    CHECK(fixture.controller.manifestPath() == receipt.root / "manifest.json");
    CHECK(fixture.controller.manifest().id == fixture.identity.id);
    CHECK(fixture.controller.manifest().version == fixture.identity.version);
    CHECK(fixture.controller.manifest().displayName == fixture.identity.displayName);
    CHECK(fixture.controller.manifest().language == language);
    CHECK(fixture.controller.manifest().styles == std::vector<std::string>{"warm"});
    CHECK(fixture.controller.selectedUnit()->pitchMarks.size() >= 6U);
    CHECK(std::all_of(fixture.controller.selectedUnit()->pitchMarks.begin(), fixture.controller.selectedUnit()->pitchMarks.end(),
        [](const auto& mark) { return !mark.locked; }));
    CHECK(fixture.controller.sampleReviewerId().empty()); CHECK(!fixture.controller.sampleReviewReceipt());
    CHECK(!fixture.controller.publishedSampleCandidate()); CHECK(!fixture.controller.dirty());
    CHECK(receipt.sourceProjectSha256 == core::sha256Hex(production::encodeProductionProject(fixture.project)));
    CHECK(!receipt.diagnostics.empty()); fixture.sourceUnchanged();
  }
}

TEST_CASE("Studio draft identity requires explicit language style and all bank fields before destination") {
  Fixture fixture;
  for (std::size_t empty = 0U; empty < 5U; ++empty) {
    IdentityDialog dialog; dialog.identity = platform::SampleManifestDraftIdentityInput{"bank", "0.1", "Display", "en", "warm"};
    auto& value = *dialog.identity;
    std::array<std::string*,5U> fields{&value.id,&value.version,&value.displayName,&value.language,&value.style};
    fields[empty]->clear(); dialog.destination = fixture.root / "invalid";
    CHECK(!native_ui::createStudioSampleManifestDraft(fixture.controller, dialog));
    CHECK(dialog.destinationCalls == 0U); CHECK(!fixture.controller.proceduralImportBusy());
  }
  IdentityDialog invalid; invalid.identity = platform::SampleManifestDraftIdentityInput{"bank", "0.1", "Display", "auto", "warm"};
  CHECK(!native_ui::createStudioSampleManifestDraft(fixture.controller, invalid));
  CHECK(invalid.destinationCalls == 0U); fixture.sourceUnchanged();
}

TEST_CASE("Studio cancelling either draft modal performs no worker or publication") {
  Fixture fixture;
  IdentityDialog dialog; dialog.destination = fixture.root / "cancelled";
  CHECK(native_ui::createStudioSampleManifestDraft(fixture.controller, dialog));
  CHECK(dialog.destinationCalls == 0U);
  dialog.identity = platform::SampleManifestDraftIdentityInput{"bank", "0.1", "Display", "ko", "warm"}; dialog.destination.reset();
  CHECK(native_ui::createStudioSampleManifestDraft(fixture.controller, dialog));
  CHECK(dialog.destinationCalls == 1U);
  CHECK(!fixture.controller.proceduralImportBusy()); CHECK(!fixture.controller.createdSampleManifestDraft());
  CHECK(!std::filesystem::exists(fixture.root / "cancelled")); fixture.sourceUnchanged();
}

TEST_CASE("Studio draft modal context is rechecked after identity and destination selection") {
  Fixture fixture;
  IdentityDialog dialog; dialog.identity = platform::SampleManifestDraftIdentityInput{"bank", "0.1", "Display", "en", "warm"}; dialog.destination = fixture.root / "stale";
  dialog.duringIdentity = [&] { CHECK(fixture.controller.selectUnit(0U)); };
  CHECK(!native_ui::createStudioSampleManifestDraft(fixture.controller, dialog));
  CHECK(dialog.destinationCalls == 0U);
  dialog.duringIdentity = {}; dialog.duringDestination = [&] { CHECK(fixture.controller.selectUnit(0U)); };
  CHECK(!native_ui::createStudioSampleManifestDraft(fixture.controller, dialog));
  CHECK(dialog.destinationCalls == 1U); CHECK(!fixture.controller.proceduralImportBusy());
  CHECK(!std::filesystem::exists(*dialog.destination)); fixture.sourceUnchanged();
}

TEST_CASE("Studio draft refuses unsaved marker edits and retains them without showing replacement modals") {
  Fixture fixture; fixture.createLoaded();
  const auto old = fixture.controller.selectedUnit()->markers.stableStart;
  CHECK(fixture.controller.moveSelectedMarker(ui::AcousticMarkerKind::StableStart, fixture.controller.microscope().frameToPixel(old+8)));
  CHECK(fixture.controller.dirty());
  const auto before = fixture.controller.manifest();
  IdentityDialog dialog; dialog.identity = platform::SampleManifestDraftIdentityInput{"another", "0.1", "Other", "ja", "other"}; dialog.destination = fixture.root / "other";
  CHECK(!native_ui::createStudioSampleManifestDraft(fixture.controller, dialog)); CHECK(dialog.identityCalls == 0U);
  const auto context = fixture.controller.captureSampleReviewContext(); CHECK(context);
  CHECK(!fixture.controller.beginSampleManifestDraftCreation(context.value(), fixture.identity, *dialog.destination));
  CHECK(fixture.controller.manifest() == before); CHECK(fixture.controller.dirty()); fixture.sourceUnchanged();
}

TEST_CASE("Studio late-cancelled draft retains its commit receipt without auto-opening") {
  Fixture fixture; StageGate gate;
  fixture.begin("late-cancel", gate.pauseAt(production::ManifestDraftStage::AfterCommitBeforeParentSync)); gate.wait();
  fixture.controller.cancelProceduralCandidateImport(); gate.release();
  CHECK(collectDraft(fixture.controller)); CHECK(fixture.controller.createdSampleManifestDraft());
  CHECK(fixture.controller.manifest().units.empty());
  CHECK(std::filesystem::exists(fixture.controller.createdSampleManifestDraft()->root / "manifest.json"));
  CHECK(!fixture.controller.sampleManifestDraftLoadDiagnostic().empty());
  CHECK(fixture.controller.sampleReviewStatus().find("NOT OPENED") != std::string::npos); fixture.sourceUnchanged();
}

TEST_CASE("Studio precommit cancellation does not create a draft or replace its current model") {
  Fixture fixture; StageGate gate;
  fixture.begin("cancel-before-commit", gate.pauseAt(production::ManifestDraftStage::BeforeCommit)); gate.wait();
  fixture.controller.cancelProceduralCandidateImport(); gate.release();
  CHECK(!collectDraft(fixture.controller)); CHECK(!fixture.controller.createdSampleManifestDraft());
  CHECK(fixture.controller.manifest().units.empty()); CHECK(!std::filesystem::exists(fixture.root / "cancel-before-commit"));
  fixture.sourceUnchanged();
}

TEST_CASE("Studio draft stale worker result preserves newer editor state and still reports the commit") {
  Fixture fixture; fixture.createLoaded(); StageGate gate;
  fixture.begin("second-draft", gate.pauseAt(production::ManifestDraftStage::AfterCommitBeforeParentSync)); gate.wait();
  fixture.controller.manifest().displayName = "NEW LOCAL EDIT";
  const auto edited = fixture.controller.manifest(); const auto previousPath = fixture.controller.manifestPath();
  gate.release(); CHECK(collectDraft(fixture.controller));
  CHECK(fixture.controller.manifest() == edited); CHECK(fixture.controller.manifestPath() == previousPath);
  CHECK(fixture.controller.createdSampleManifestDraft()->root.filename() == "second-draft");
  CHECK(fixture.controller.sampleReviewStatus().find("NOT OPENED") != std::string::npos); fixture.sourceUnchanged();
}

TEST_CASE("Studio draft loading failure cannot erase a committed directory receipt") {
  Fixture fixture; const auto destination = fixture.root / "load-failure";
  fixture.begin("load-failure", {.faultInjector = [destination](production::ManifestDraftStage stage) {
    if (stage == production::ManifestDraftStage::AfterCommitBeforeParentSync) {
      std::error_code error; std::filesystem::remove(destination / "manifest.json", error);
      if (error) return core::failure(core::ErrorCode::IoError, "Synthetic load-failure setup failed");
    }
    return core::success();
  }});
  CHECK(collectDraft(fixture.controller)); CHECK(fixture.controller.createdSampleManifestDraft());
  CHECK(std::filesystem::exists(destination / "draft.json")); CHECK(fixture.controller.manifest().units.empty());
  CHECK(!fixture.controller.sampleManifestDraftLoadDiagnostic().empty());
  CHECK(fixture.controller.sampleReviewStatus().find("NOT OPENED") != std::string::npos); fixture.sourceUnchanged();
}

TEST_CASE("Studio draft shows missing assignments and unconfirmed durability without inventing approval") {
  Fixture fixture(true);
  fixture.begin("partial", {.faultInjector = [](production::ManifestDraftStage stage) {
    return stage == production::ManifestDraftStage::AfterCommitBeforeParentSync
        ? core::failure(core::ErrorCode::IoError, "Synthetic parent-sync failure") : core::success();
  }});
  CHECK(collectDraft(fixture.controller)); CHECK(fixture.controller.createdSampleManifestDraft()); CHECK(fixture.controller.selectedUnit());
  CHECK(!fixture.controller.createdSampleManifestDraft()->durabilityConfirmed);
  CHECK(fixture.controller.createdSampleManifestDraft()->missingAssignments == std::vector<std::string>{"sustain:i@69"});
  CHECK(fixture.controller.manifest().units.size() == 1U);
  const auto lines = native_ui::studioSampleReviewDetailLines(fixture.controller, 720.0);
  CHECK(std::any_of(lines.begin(),lines.end(),[](const auto& text) { return text.find("MISSING ASSIGNMENT") != std::string::npos; }));
  CHECK(std::any_of(lines.begin(),lines.end(),[](const auto& text) { return text.find("ESTIMATED") != std::string::npos; }));
  CHECK(fixture.controller.sampleReviewStatus().find("DURABILITY UNCONFIRMED") != std::string::npos); fixture.sourceUnchanged();
}

TEST_CASE("Studio draft postcommit oversized copied audio retains receipt and previous visual model") {
  Fixture fixture; fixture.createLoaded(); const VisualSnapshot before{fixture.controller}; StageGate gate;
  fixture.begin("oversized", gate.pauseAt(production::ManifestDraftStage::AfterCommitBeforeParentSync)); gate.wait();
  const auto manifest = voicebank::ManifestJsonCodec{}.load(fixture.root / "oversized" / "manifest.json"); CHECK(manifest);
  // Sparse oversized input exercises encoded-byte admission without allocating
  // the advertised payload or allowing WAV decode to allocate PCM first.
  std::filesystem::resize_file(fixture.root / "oversized" / manifest.value().units.front().audioPath, 256ULL*1024ULL*1024ULL+1ULL);
  gate.release(); CHECK(collectDraft(fixture.controller)); before.check(fixture.controller);
  CHECK(fixture.controller.createdSampleManifestDraft()->root.filename() == "oversized");
  CHECK(!fixture.controller.sampleManifestDraftLoadDiagnostic().empty());
  CHECK(fixture.controller.sampleReviewStatus().find("NOT OPENED") != std::string::npos); fixture.sourceUnchanged();
}

TEST_CASE("Studio draft replaced audio is rejected before decode across selection and reopen") {
  Fixture fixture; fixture.createLoaded(); const VisualSnapshot before{fixture.controller};
  const auto receiptHash = fixture.controller.createdSampleManifestDraft()->draftSha256;
  const auto audioPath = fixture.controller.manifestPath().parent_path() / fixture.controller.selectedUnit()->audioPath;
  CHECK(voicebank::writeWav(audioPath, {.sampleRate=48000U,.channels=1U,.sampleFormat=voicebank::WavSampleFormat::Pcm24},
      test::support::sineWave(48000U,220.0,0.2,0.1F)));
  const auto selected = fixture.controller.selectUnit(0U); CHECK(!selected);
  CHECK(selected.error().message.find("retained unit binding") != std::string::npos); before.check(fixture.controller);
  CHECK(fixture.controller.beginSampleReviewUnitSelection(0U)); CHECK(!collectDraft(fixture.controller)); before.check(fixture.controller);
  CHECK(!fixture.controller.openManifest(before.path)); before.check(fixture.controller);
  const auto context = fixture.controller.captureSampleReviewContext(); CHECK(context);
  CHECK(fixture.controller.beginSampleManifestOpen(context.value(), before.path)); CHECK(!collectDraft(fixture.controller)); before.check(fixture.controller);
  Controller reopened; CHECK(!reopened.openManifest(before.path)); CHECK(reopened.manifest().units.empty());
  CHECK(fixture.controller.createdSampleManifestDraft()->draftSha256 == receiptHash); fixture.sourceUnchanged();
}

TEST_CASE("Studio draft resize keeps pinned waveform and spectrum after copied audio disappears") {
  Fixture fixture; fixture.createLoaded(); const VisualSnapshot before{fixture.controller};
  const auto oldWidth = fixture.controller.microscope().waveformBounds().width;
  CHECK(std::filesystem::remove(fixture.controller.manifestPath().parent_path() / fixture.controller.selectedUnit()->audioPath));
  fixture.controller.resize(1450.0,900.0); before.check(fixture.controller,false);
  CHECK(fixture.controller.microscope().waveformBounds().width != oldWidth);
  CHECK(fixture.controller.microscope().waveform().back().x != before.waveform.back().x);
  const auto frame = fixture.controller.selectedUnit()->markers.stableStart + 8;
  CHECK(fixture.controller.moveSelectedMarker(ui::AcousticMarkerKind::StableStart, fixture.controller.microscope().frameToPixel(frame)));
  CHECK(fixture.controller.selectedUnit()->markers.stableStart == frame); CHECK(fixture.controller.dirty()); fixture.sourceUnchanged();
}

TEST_CASE("Studio draft binding survives edited manifest reopen and cannot downgrade through removed metadata") {
  Fixture fixture; fixture.createLoaded();
  auto edited = fixture.controller.manifest(); edited.units.front().markers.stableStart += 8;
  CHECK(voicebank::ManifestJsonCodec{}.save(edited, fixture.controller.manifestPath()));
  CHECK(fixture.controller.openManifest(fixture.controller.manifestPath())); CHECK(fixture.controller.manifest() == edited);
  const VisualSnapshot before{fixture.controller};
  const auto descriptorPath = fixture.controller.createdSampleManifestDraft()->root / "draft.json";
  const auto original = core::readTextFileLimited(descriptorPath,32ULL*1024ULL*1024ULL); CHECK(original);
  CHECK(std::filesystem::remove(descriptorPath));
  CHECK(!fixture.controller.openManifest(before.path)); before.check(fixture.controller);
  CHECK(core::durableAtomicWriteTextNew(descriptorPath,"{\"format\":\"unrelated\"}"));
  const auto context = fixture.controller.captureSampleReviewContext(); CHECK(context);
  CHECK(fixture.controller.beginSampleManifestOpen(context.value(),before.path)); CHECK(!collectDraft(fixture.controller)); before.check(fixture.controller);
  CHECK(core::durableAtomicWriteText(descriptorPath,original.value()));
  CHECK(fixture.controller.openManifest(before.path)); before.check(fixture.controller); fixture.sourceUnchanged();
}

TEST_CASE("Studio ordinary external manifest does not infer digest binding from an arbitrary filename") {
  Fixture fixture; fixture.createLoaded();
  auto legacy = fixture.controller.manifest(); legacy.units.front().audioPath = "raw.wav";
  const auto path = fixture.root / "legacy.json";
  CHECK(voicebank::ManifestJsonCodec{}.save(legacy,path));
  Controller controller; CHECK(controller.openManifest(path)); CHECK(controller.manifestPath().parent_path() / controller.selectedUnit()->audioPath == fixture.root / "raw.wav");
  CHECK(controller.selectUnit(0U)); CHECK(controller.microscope().unit() == controller.selectedUnit());
  fixture.sourceUnchanged();
}

TEST_CASE("Studio source qualification label does not misreport unassessed listening as blocked source rights") {
  Fixture fixture;
  CHECK(fixture.project.sourceStrategies.front().rights == production::Feasibility::Pass);
  CHECK(fixture.project.sourceStrategies.front().permissions.sourceUse);
  CHECK(fixture.project.sourceStrategies.front().permissions.transformation);
  const auto lines = native_ui::studioSampleReviewDetailLines(fixture.controller,960.0);
  CHECK(std::any_of(lines.begin(),lines.end(),[](const auto& line) { return line == "SOURCE QUALIFICATION PENDING"; }));
  CHECK(std::none_of(lines.begin(),lines.end(),[](const auto& line) { return line.find("RIGHTS BLOCKED") != std::string::npos; }));
  fixture.sourceUnchanged();
}

namespace {
struct SampleCloseDialog final : seam::platform::IFileDialog {
  seam::platform::UnsavedSampleDecision decision{seam::platform::UnsavedSampleDecision::Cancel};
  std::function<void()> duringConfirmation;
  std::size_t calls{};
  seam::core::Result<seam::platform::UnsavedSampleDecision> confirmUnsavedSampleChanges() override {
    ++calls; if (duringConfirmation) duringConfirmation(); return decision;
  }
  seam::core::Result<std::optional<std::filesystem::path>> choose(const seam::platform::FileDialogRequest&) override {
    return std::optional<std::filesystem::path>{};
  }
};
void editSample(Controller& controller) {
  const auto frame = controller.selectedUnit()->markers.stableStart + 8;
  CHECK(controller.moveSelectedMarker(seam::ui::AcousticMarkerKind::StableStart,controller.microscope().frameToPixel(frame)));
  CHECK(controller.dirty());
}
}

TEST_CASE("Studio sample close cancellation and discard preserve saved files and in-memory edits") {
  Fixture fixture; fixture.createLoaded(); SampleCloseDialog dialog;
  CHECK(fixture.controller.confirmSampleClose(dialog).value()); CHECK(dialog.calls == 0U);
  const auto before = core::readTextFileLimited(fixture.controller.manifestPath(),32U*1024U*1024U); CHECK(before);
  editSample(fixture.controller); const auto edited = fixture.controller.manifest();
  CHECK(!fixture.controller.confirmSampleClose(dialog).value()); CHECK(dialog.calls == 1U);
  CHECK(fixture.controller.dirty()); CHECK(fixture.controller.manifest() == edited);
  dialog.decision = platform::UnsavedSampleDecision::Discard;
  CHECK(fixture.controller.confirmSampleClose(dialog).value()); CHECK(fixture.controller.dirty());
  CHECK(fixture.controller.manifest() == edited);
  CHECK(core::readTextFileLimited(fixture.controller.manifestPath(),32U*1024U*1024U).value() == before.value());
  fixture.sourceUnchanged();
}

TEST_CASE("Studio sample close save persists edited markers across reopening") {
  Fixture fixture; fixture.createLoaded(); editSample(fixture.controller);
  const auto edited = fixture.controller.manifest(); SampleCloseDialog dialog; dialog.decision = platform::UnsavedSampleDecision::Save;
  const auto close = fixture.controller.confirmSampleClose(dialog); CHECK(close); CHECK(close.value()); CHECK(!fixture.controller.dirty());
  Controller reopened; CHECK(reopened.openManifest(fixture.controller.manifestPath())); CHECK(reopened.manifest() == edited);
}

TEST_CASE("Studio sample close rejects a modal-time edit without saving or discarding it") {
  Fixture fixture; fixture.createLoaded(); editSample(fixture.controller);
  const auto before = core::readTextFileLimited(fixture.controller.manifestPath(),32U*1024U*1024U); CHECK(before);
  SampleCloseDialog dialog; dialog.decision = platform::UnsavedSampleDecision::Save;
  dialog.duringConfirmation = [&] { editSample(fixture.controller); };
  const auto close = fixture.controller.confirmSampleClose(dialog); CHECK(!close);
  CHECK(close.error().message.find("changed during close") != std::string::npos); CHECK(fixture.controller.dirty());
  CHECK(core::readTextFileLimited(fixture.controller.manifestPath(),32U*1024U*1024U).value() == before.value());
  fixture.sourceUnchanged();
}

TEST_CASE("Studio sample close does not authorize closing after save failure or during background work") {
  Fixture fixture; fixture.createLoaded(); editSample(fixture.controller);
  const auto path = fixture.controller.manifestPath(); std::filesystem::rename(path,path.string()+".before");
  CHECK(std::filesystem::create_directory(path)); SampleCloseDialog dialog; dialog.decision = platform::UnsavedSampleDecision::Save;
  CHECK(!fixture.controller.confirmSampleClose(dialog)); CHECK(fixture.controller.dirty()); fixture.sourceUnchanged();
  Fixture busy; StageGate gate; busy.begin("busy-close",gate.pauseAt(production::ManifestDraftStage::AudioStaged)); gate.wait();
  SampleCloseDialog unused;
  const auto result = busy.controller.confirmSampleClose(unused);
  gate.release(); CHECK(!result); CHECK(unused.calls == 0U); CHECK(collectDraft(busy.controller));
}

namespace {
std::filesystem::path externalTwoUnitManifest(Fixture& fixture) {
  fixture.createLoaded();
  auto manifest = fixture.controller.manifest();
  manifest.units.front().audioPath = "raw.wav";
  auto second = manifest.units.front(); second.id = "external-second";
  manifest.units.push_back(std::move(second));
  const auto path = fixture.root / "external.json";
  CHECK(seam::voicebank::ManifestJsonCodec{}.save(manifest,path));
  return path;
}
}

TEST_CASE("Studio background selection works without a producer and retains unsaved edits and current geometry") {
  Fixture fixture; const auto path = externalTwoUnitManifest(fixture);
  Controller controller; CHECK(controller.openManifest(path)); CHECK(!controller.productionProject()); editSample(controller);
  const auto edited = controller.manifest(); const auto before = core::readTextFileLimited(path,32U*1024U*1024U); CHECK(before);
  CHECK(controller.beginEditableUnitSelection(1U)); CHECK(controller.proceduralImportBusy()); CHECK(controller.selectedIndex() == 0U);
  CHECK(!controller.beginEditableUnitSelection(0U));
  controller.resize(1800.0,1000.0); const auto width = controller.microscope().waveformBounds().width;
  CHECK(collectDraft(controller)); CHECK(controller.selectedIndex() == 1U); CHECK(controller.dirty());
  CHECK(controller.manifest() == edited); CHECK(controller.microscope().unit() == controller.selectedUnit());
  CHECK(controller.microscope().waveformBounds().width == width);
  CHECK(core::readTextFileLimited(path,32U*1024U*1024U).value() == before.value()); fixture.sourceUnchanged();
}

TEST_CASE("Studio cancelled background selection preserves the current editable unit and can restart") {
  Fixture fixture; Controller controller; CHECK(controller.openManifest(externalTwoUnitManifest(fixture))); editSample(controller);
  const VisualSnapshot before{controller};
  CHECK(controller.beginEditableUnitSelection(1U)); controller.cancelProceduralCandidateImport();
  CHECK(!collectDraft(controller)); before.check(controller);
  CHECK(controller.beginEditableUnitSelection(1U)); CHECK(collectDraft(controller)); CHECK(controller.selectedIndex() == 1U);
  CHECK(controller.dirty()); fixture.sourceUnchanged();
}

TEST_CASE("Studio stale background selection cannot overwrite changed manifest material") {
  Fixture fixture; Controller controller; CHECK(controller.openManifest(externalTwoUnitManifest(fixture))); editSample(controller);
  CHECK(controller.beginEditableUnitSelection(1U));
  // Simulate an external owner changing the public editable model before
  // collection; UI commands themselves remain blocked while work is active.
  controller.selectedUnit()->gainDb = -3.0F;
  const auto changed = controller.manifest();
  CHECK(!collectDraft(controller)); CHECK(controller.selectedIndex() == 0U);
  CHECK(controller.manifest() == changed); CHECK(controller.dirty()); fixture.sourceUnchanged();
}

TEST_CASE("Studio background selection preserves the visible model on audio failure and joins on shutdown") {
  Fixture fixture; const auto path = externalTwoUnitManifest(fixture);
  Controller controller; CHECK(controller.openManifest(path)); const VisualSnapshot before{controller};
  std::filesystem::rename(fixture.root / "raw.wav",fixture.root / "raw-preserved.wav");
  CHECK(controller.beginEditableUnitSelection(1U)); CHECK(!collectDraft(controller)); before.check(controller);
  std::filesystem::rename(fixture.root / "raw-preserved.wav",fixture.root / "raw.wav");
  CHECK(controller.beginEditableUnitSelection(1U));
  CHECK(!controller.finishProceduralCandidateImport()); before.check(controller);
  CHECK(!controller.proceduralImportBusy()); fixture.sourceUnchanged();
}

namespace {
struct QualityDialog final : seam::platform::IFileDialog {
  std::optional<std::filesystem::path> evidence;
  std::optional<seam::platform::SourceQualityDecisionInput> decision;
  std::function<void()> duringChoose, duringDecision;
  std::string summary;
  std::vector<std::string> reviewers;
  seam::core::Result<std::optional<std::filesystem::path>> choose(const seam::platform::FileDialogRequest& request) override {
    CHECK(request.purpose == seam::platform::FileDialogPurpose::SourceQualityEvidence);
    if (duringChoose) duringChoose();
    return evidence;
  }
  seam::core::Result<std::optional<seam::platform::SourceQualityDecisionInput>> chooseSourceQualityDecision(
      std::string_view text,const std::vector<std::string>& choices) override {
    summary=text; reviewers=choices; if (duringDecision) duringDecision(); return decision;
  }
};
std::filesystem::path qualityEvidence(Fixture& fixture) {
  const auto path=fixture.root/"native-quality-evidence.txt";
  CHECK(seam::core::durableAtomicWriteTextNew(path,"SYNTHETIC NATIVE REVIEW EVIDENCE; no real singer qualification."));
  return path;
}
}

TEST_CASE("Studio source evidence capture requires no manifest and only explicit independent decisions commit") {
  Fixture fixture(false,true); QualityDialog dialog;
  const auto before=production::encodeProductionProject(*fixture.controller.productionProject());
  CHECK(native_ui::captureStudioSourceQuality(fixture.controller,dialog)); CHECK(!fixture.controller.proceduralImportBusy());
  dialog.evidence=qualityEvidence(fixture);
  CHECK(native_ui::captureStudioSourceQuality(fixture.controller,dialog)); CHECK(collectDraft(fixture.controller));
  CHECK(fixture.controller.manifest().units.empty()); CHECK(fixture.controller.sourceQualityInspection());
  CHECK(production::encodeProductionProject(*fixture.controller.productionProject()) == before);
  CHECK(!fixture.controller.sourceQualityReceipt());
  CHECK(native_ui::confirmStudioSourceQuality(fixture.controller,dialog)); CHECK(!fixture.controller.proceduralImportBusy());
  CHECK(dialog.reviewers == std::vector<std::string>{"reviewer"}); CHECK(dialog.summary.find("EVIDENCE SHA256") != std::string::npos);
  dialog.decision=platform::SourceQualityDecisionInput{"native-quality-1","reviewer","pass","pass"};
  CHECK(native_ui::confirmStudioSourceQuality(fixture.controller,dialog)); CHECK(collectDraft(fixture.controller));
  CHECK(fixture.controller.sourceQualityReceipt()); CHECK(!fixture.controller.sourceQualityInspection());
  const auto& project=*fixture.controller.productionProject();
  CHECK(project.schemaVersion==3); CHECK(project.sourceQualityAssessments.size()==1U);
  CHECK(project.sourceBindings==fixture.project.sourceBindings); CHECK(project.reviews.empty());
  CHECK(project.unitAssignments.front().state==production::UnitQueueState::MarkerReview);
  CHECK(production::requireTakeSourceQualification(project,"take-a"));
  CHECK(fixture.controller.sourceQualityReceipt()->committedGeneration==project.lastDurableGeneration);
  const auto recovered=fixture.repository.recover(); CHECK(recovered);
  CHECK(production::encodeProductionProject(recovered.value())==production::encodeProductionProject(project));
}

TEST_CASE("Studio source quality rejects unlisted reviewer and changed captured evidence without a commit") {
  Fixture fixture; QualityDialog dialog; dialog.evidence=qualityEvidence(fixture);
  CHECK(native_ui::captureStudioSourceQuality(fixture.controller,dialog)); CHECK(collectDraft(fixture.controller));
  const auto before=production::encodeProductionProject(*fixture.controller.productionProject());
  dialog.decision=platform::SourceQualityDecisionInput{"native-quality-1","producer","pass","pass"};
  CHECK(!native_ui::confirmStudioSourceQuality(fixture.controller,dialog)); CHECK(!fixture.controller.proceduralImportBusy());
  dialog.decision->reviewerId="reviewer";
  CHECK(core::durableAtomicWriteText(*dialog.evidence,"changed after capture"));
  CHECK(native_ui::confirmStudioSourceQuality(fixture.controller,dialog)); CHECK(!collectDraft(fixture.controller));
  CHECK(!fixture.controller.sourceQualityReceipt());
  CHECK(production::encodeProductionProject(*fixture.controller.productionProject())==before); fixture.sourceUnchanged();
}

TEST_CASE("Studio source quality rejects stale file and decision modals while preserving unsaved edits") {
  Fixture fixture; fixture.createLoaded(); QualityDialog dialog; dialog.evidence=qualityEvidence(fixture);
  dialog.duringChoose=[&] { editSample(fixture.controller); };
  CHECK(!native_ui::captureStudioSourceQuality(fixture.controller,dialog)); CHECK(!fixture.controller.proceduralImportBusy());
  dialog.duringChoose={};
  CHECK(native_ui::captureStudioSourceQuality(fixture.controller,dialog)); CHECK(collectDraft(fixture.controller));
  dialog.decision=platform::SourceQualityDecisionInput{"native-quality-1","reviewer","pass","pass"};
  dialog.duringDecision=[&] { editSample(fixture.controller); };
  CHECK(!native_ui::confirmStudioSourceQuality(fixture.controller,dialog)); CHECK(!fixture.controller.proceduralImportBusy());
  CHECK(fixture.controller.dirty()); CHECK(!fixture.controller.sourceQualityReceipt()); fixture.sourceUnchanged();
}

TEST_CASE("Studio source quality capture cancellation preserves source history and controls do not overlap") {
  Fixture fixture; const auto context=fixture.controller.captureSampleReviewContext(); CHECK(context);
  CHECK(fixture.controller.beginSourceQualityEvidenceCapture(context.value(),qualityEvidence(fixture)));
  fixture.controller.cancelProceduralCandidateImport(); CHECK(!collectDraft(fixture.controller));
  CHECK(!fixture.controller.sourceQualityInspection()); CHECK(!fixture.controller.sourceQualityReceipt()); fixture.sourceUnchanged();
  for (double width : {720.0,960.0,1440.0}) {
    const auto controls=native_ui::studioSampleReviewControls(fixture.controller,width); CHECK(controls.size()==25U);
    for (std::size_t i=0U;i<controls.size();++i) for (std::size_t j=i+1U;j<controls.size();++j) {
      const auto& a=controls[i].bounds; const auto& b=controls[j].bounds;
      CHECK(a.right()<=b.x || b.right()<=a.x || a.bottom()<=b.y || b.bottom()<=a.y);
    }
  }
}

TEST_CASE("Studio source decision cannot adopt evidence recaptured under the same project context during its modal") {
  Fixture fixture; QualityDialog dialog; dialog.evidence=qualityEvidence(fixture);
  CHECK(native_ui::captureStudioSourceQuality(fixture.controller,dialog)); CHECK(collectDraft(fixture.controller));
  const auto original=*fixture.controller.sourceQualityInspection();
  const auto other=fixture.root/"other-quality.txt"; CHECK(core::durableAtomicWriteTextNew(other,"Different synthetic reviewer evidence"));
  dialog.decision=platform::SourceQualityDecisionInput{"native-quality-1","reviewer","pass","pass"};
  dialog.duringDecision=[&] {
    CHECK(fixture.controller.beginSourceQualityEvidenceCapture(original.context,other)); CHECK(collectDraft(fixture.controller));
  };
  CHECK(!native_ui::confirmStudioSourceQuality(fixture.controller,dialog)); CHECK(!fixture.controller.proceduralImportBusy());
  CHECK(!fixture.controller.sourceQualityReceipt());
  CHECK(fixture.controller.sourceQualityInspection()->assessment.evidenceSha256 != original.assessment.evidenceSha256);
  fixture.sourceUnchanged();
}

TEST_CASE("Studio preserves the actual source quality receipt after late cancellation") {
  Fixture fixture; QualityDialog dialog; dialog.evidence=qualityEvidence(fixture);
  CHECK(native_ui::captureStudioSourceQuality(fixture.controller,dialog)); CHECK(collectDraft(fixture.controller));
  const auto generation=fixture.controller.productionProject()->lastDurableGeneration;
  dialog.decision=platform::SourceQualityDecisionInput{"native-quality-late-cancel","reviewer","pass","pass"};
  CHECK(native_ui::confirmStudioSourceQuality(fixture.controller,dialog));
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{10};
  for (;;) {
    const auto recovered=fixture.repository.recover();
    if (recovered && recovered.value().lastDurableGeneration>generation) break;
    CHECK(std::chrono::steady_clock::now()<deadline); std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  fixture.controller.cancelProceduralCandidateImport(); CHECK(collectDraft(fixture.controller));
  CHECK(fixture.controller.sourceQualityReceipt()); CHECK(fixture.controller.sourceQualityReceipt()->committedGeneration>generation);
  CHECK(fixture.controller.productionProject()->sourceQualityAssessments.size()==1U);
  CHECK(!production::requireTakeSourceQualification(*fixture.controller.productionProject(),"take-a"));
  CHECK(fixture.controller.productionProject()->sourceBindings==fixture.project.sourceBindings);
}

namespace {
struct RegistrationDialog final : platform::IFileDialog {
  std::optional<std::filesystem::path> evidence;
  std::optional<platform::SourceRegistrationInput> input;
  std::function<void()> duringChoose, duringForm;
  std::string summary;
  core::Result<std::optional<std::filesystem::path>> choose(const platform::FileDialogRequest& request) override {
    CHECK(request.purpose==platform::FileDialogPurpose::SourceLicenseEvidence);
    if (duringChoose) duringChoose();
    return evidence;
  }
  core::Result<std::optional<platform::SourceRegistrationInput>> chooseSourceRegistration(std::string_view text) override {
    summary=text; if (duringForm) duringForm(); return input;
  }
};
platform::SourceRegistrationInput unassessedSource() {
  return {"native-new-source","procedural","not-assessed",{"no","no","no","no"}};
}
}

TEST_CASE("Studio registers a source-free planned Draft only after explicit source declarations") {
  Fixture fixture(false,false,true); RegistrationDialog dialog;
  CHECK(native_ui::captureStudioSourceLicense(fixture.controller,dialog)); CHECK(!fixture.controller.proceduralImportBusy());
  dialog.evidence=qualityEvidence(fixture);
  CHECK(native_ui::captureStudioSourceLicense(fixture.controller,dialog)); CHECK(collectDraft(fixture.controller));
  CHECK(fixture.controller.sourceRegistrationInspection()); fixture.sourceUnchanged();
  CHECK(native_ui::registerStudioSource(fixture.controller,dialog)); fixture.sourceUnchanged();
  CHECK(dialog.summary.find(fixture.controller.sourceRegistrationInspection()->evidenceSha256)!=std::string::npos);
  dialog.input=unassessedSource();
  CHECK(native_ui::registerStudioSource(fixture.controller,dialog)); CHECK(collectDraft(fixture.controller));
  CHECK(fixture.controller.sourceRegistrationReceipt()); CHECK(!fixture.controller.sourceRegistrationInspection());
  const auto& project=*fixture.controller.productionProject();
  CHECK(project.sourceStrategies.size()==1U); CHECK(project.takes.empty()); CHECK(project.reviews.empty());
  CHECK(project.selectedSourceStrategyId=="native-new-source"); CHECK(!production::requireSelectedSourceExecution(project));
  CHECK(project.sourceStrategies.front().rights==production::Feasibility::NotAssessed);
  CHECK(project.sourceStrategies.front().coverage==production::Feasibility::NotAssessed);
  CHECK(production::encodeProductionProject(fixture.repository.recover().value())==production::encodeProductionProject(project));
}

TEST_CASE("Studio source registration rejects missing declarations changed evidence and stale modals") {
  Fixture fixture; fixture.createLoaded(); RegistrationDialog dialog; dialog.evidence=qualityEvidence(fixture);
  CHECK(native_ui::captureStudioSourceLicense(fixture.controller,dialog)); CHECK(collectDraft(fixture.controller));
  dialog.input=unassessedSource(); dialog.input->permissions[2]="";
  CHECK(!native_ui::registerStudioSource(fixture.controller,dialog)); CHECK(!fixture.controller.proceduralImportBusy());
  dialog.input=unassessedSource();
  CHECK(core::durableAtomicWriteText(*dialog.evidence,"Changed after license capture"));
  CHECK(native_ui::registerStudioSource(fixture.controller,dialog)); CHECK(!collectDraft(fixture.controller)); fixture.sourceUnchanged();
  CHECK(native_ui::captureStudioSourceLicense(fixture.controller,dialog)); CHECK(collectDraft(fixture.controller));
  dialog.duringForm=[&] { editSample(fixture.controller); };
  CHECK(!native_ui::registerStudioSource(fixture.controller,dialog)); CHECK(fixture.controller.dirty()); fixture.sourceUnchanged();
  CHECK(!fixture.controller.sourceRegistrationReceipt());
}

TEST_CASE("Studio registration cannot adopt replacement evidence during its modal and capture can cancel") {
  Fixture fixture; RegistrationDialog dialog; dialog.evidence=qualityEvidence(fixture);
  CHECK(native_ui::captureStudioSourceLicense(fixture.controller,dialog));
  fixture.controller.cancelProceduralCandidateImport(); CHECK(!collectDraft(fixture.controller));
  CHECK(!fixture.controller.sourceRegistrationInspection()); fixture.sourceUnchanged();
  CHECK(native_ui::captureStudioSourceLicense(fixture.controller,dialog)); CHECK(collectDraft(fixture.controller));
  const auto replacement=fixture.root/"replacement-license.txt";
  CHECK(core::durableAtomicWriteTextNew(replacement,"Different synthetic authorization evidence"));
  dialog.input=unassessedSource();
  dialog.duringForm=[&] {
    const auto context=fixture.controller.captureSampleReviewContext(); CHECK(context);
    CHECK(fixture.controller.beginSourceLicenseCapture(context.value(),replacement)); CHECK(collectDraft(fixture.controller));
  };
  CHECK(!native_ui::registerStudioSource(fixture.controller,dialog)); fixture.sourceUnchanged();
  CHECK(fixture.controller.sourceRegistrationInspection()->evidencePath==replacement);
}

TEST_CASE("Studio retains source registration receipt after late cancellation without changing prior provenance") {
  Fixture fixture; RegistrationDialog dialog; dialog.evidence=qualityEvidence(fixture);
  CHECK(native_ui::captureStudioSourceLicense(fixture.controller,dialog)); CHECK(collectDraft(fixture.controller));
  const auto generation=fixture.controller.productionProject()->lastDurableGeneration;
  dialog.input=unassessedSource();
  CHECK(native_ui::registerStudioSource(fixture.controller,dialog));
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{10};
  for (;;) {
    const auto recovered=fixture.repository.recover();
    if (recovered && recovered.value().lastDurableGeneration>generation) break;
    CHECK(std::chrono::steady_clock::now()<deadline); std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  fixture.controller.cancelProceduralCandidateImport(); CHECK(collectDraft(fixture.controller));
  CHECK(fixture.controller.sourceRegistrationReceipt());
  CHECK(fixture.controller.productionProject()->sourceBindings==fixture.project.sourceBindings);
  CHECK(fixture.controller.productionProject()->sourceStrategies.front()==fixture.project.sourceStrategies.front());
  CHECK(fixture.controller.productionProject()->reviews.empty());
}
