#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/voicebank/take_inspection.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank_production/asset_store.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/repository.hpp"
#include "seam/voicebank_production/take_inspection_receipt.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace {
namespace production = seam::voicebank_production;
namespace voicebank = seam::voicebank;
using voicebank::TakeCheck;
using voicebank::TakeCheckOutcome;
using voicebank::TakeQcPolicy;

std::vector<float> noise(std::size_t frames, float amplitude, std::uint32_t seed) {
  std::vector<float> samples(frames, 0.0F);
  auto state = seed;
  for (auto& sample : samples) {
    state = state * 1664525U + 1013904223U;
    const auto unit = static_cast<float>(state >> 8U) / static_cast<float>(1U << 24U);
    sample = amplitude * (2.0F * unit - 1.0F);
  }
  return samples;
}

void writeTake(const std::filesystem::path& path, const std::vector<float>& samples) {
  CHECK(voicebank::writeWav(path, {.sampleRate = 48000U, .channels = 1U,
      .sampleFormat = voicebank::WavSampleFormat::Pcm24}, samples));
}

void writeBytes(const std::filesystem::path& path, std::string_view bytes) {
  std::ofstream stream{path, std::ios::binary | std::ios::trunc};
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::vector<float> breathTake() { return noise(24000U, 0.08F, 7U); }
std::vector<float> pauseTake() { return noise(24000U, 0.0008F, 11U); }
std::vector<float> closureTake() { return std::vector<float>(7200U, 0.0F); }
std::vector<float> voicedTake(double hz = 261.63) {
  return seam::test::support::sineWave(48000U, hz, 0.5, 0.3F);
}

voicebank::TakeInspection inspect(const std::filesystem::path& path, TakeQcPolicy policy,
                                  std::optional<std::int32_t> root = std::nullopt) {
  const auto inspected = voicebank::inspectTake(path, {.policy = policy, .expectedRootMidi = root});
  CHECK(inspected);
  if (!inspected) throw std::runtime_error(inspected.error().message);
  return inspected.value();
}

struct AdmissionFixture final {
  std::filesystem::path root{seam::test::support::temporaryDirectory("take-inspection-admission")};
  production::ProductionProjectRepository repository{root / "workspace"};
  production::VoicebankProductionProject project;

  AdmissionFixture() {
    project.schemaVersion = production::kProductionStyleSchemaVersion;
    project.projectId = "take-qc";
    project.language = "ja";
    project.inventoryId = "take-qc-inventory";
    project.inventorySha256 = std::string(64U, 'a');
    project.operators = {{"producer", "PRODUCER"}};
    const auto slot = [](std::string key, std::string take) {
      return production::UnitAssignment{.coverageKey = std::move(key), .pitchLayer = 60,
          .promptId = take + "-prompt", .plannedTakeId = take, .style = "neutral"};
    };
    project.unitAssignments = {slot("sustain:a", "take-a"), slot("breath:br", "take-br"),
        slot("special:pau", "take-pau"), slot("special:cl", "take-cl")};
    const auto notice = root / "notice.txt";
    CHECK(seam::core::durableAtomicWriteText(notice, "GENERATED TEST FIXTURE ONLY; no singer qualification"));
    const auto licenseHash = seam::core::sha256File(notice);
    CHECK(licenseHash);
    project.selectedSourceStrategyId = "test-source";
    project.licenseLocator = notice.string();
    project.licenseSha256 = licenseHash.value();
    project.sourceStrategies.push_back({.id = "test-source",
        .kind = production::SourceStrategyKind::ProceduralSynthesis,
        .rights = production::Feasibility::Pass,
        .permissions = {.sourceUse = true, .transformation = true},
        .licenseLocator = notice.string(), .licenseSha256 = licenseHash.value(),
        .evidenceState = "SYNTHETIC_TEST_ONLY"});
    CHECK(repository.initialize(project, {"create", project.projectId, "producer", "2026-09-28T00:00:00Z"}));
  }

  static production::RawTakeInput take(std::string takeId, std::string coverageKey) {
    return {.takeId = takeId, .promptId = takeId + "-prompt", .coverageKey = std::move(coverageKey),
            .pitchLayer = 60, .style = "neutral"};
  }
  static production::ProductionJournalEvent event(std::string takeId) {
    return {"import", std::move(takeId), "producer", "2026-09-28T00:01:00Z"};
  }
  production::MetadataRevision receiptFor(const std::filesystem::path& path,
                                          const production::RawTakeInput& input) const {
    const auto request = production::takeInspectionRequestFor(input.coverageKey, input.pitchLayer);
    CHECK(request);
    const auto inspected = voicebank::inspectTake(path, request.value());
    CHECK(inspected);
    return production::makeTakeInspectionRevision(
        {{input.takeId, input.promptId, input.coverageKey, input.pitchLayer}, inspected.value()},
        "producer", "2026-09-28T00:01:00Z");
  }
  void assertUnchanged(const std::string& before) {
    CHECK(production::encodeProductionProject(project) == before);
    const auto recovered = repository.recover();
    CHECK(recovered);
    CHECK(production::encodeProductionProject(recovered.value()) == before);
  }
};

production::MetadataRevision reissue(const production::MetadataRevision& original,
                                     const production::TakeInspectionEvidence& evidence) {
  return production::makeTakeInspectionRevision(evidence, original.operatorId, original.performedAtUtc);
}

bool validAfterReplacing(production::VoicebankProductionProject project,
                         const production::MetadataRevision& replacement) {
  for (auto& revision : project.metadataRevisions) {
    if (revision.takeId == replacement.takeId && revision.kind == production::kTakeInspectionRevisionKind)
      revision = replacement;
  }
  return static_cast<bool>(production::validateProductionProject(project));
}
}  // namespace

TEST_CASE("take QC policy follows the unit's coverage key and refuses ambiguous keys") {
  for (const auto* key : {"sustain:a", "cv:k:a", "vc:a:k", "vv:a:i", "release:a:R",
                          "glottal-attack:glottal:a", "special:N", "vowel:a"}) {
    const auto policy = voicebank::takeQcPolicyForCoverageKey(key);
    CHECK(policy);
    CHECK(policy && policy.value() == TakeQcPolicy::Voiced);
  }
  for (const auto* key : {"breath:br", "special:br"}) {
    const auto policy = voicebank::takeQcPolicyForCoverageKey(key);
    CHECK(policy && policy.value() == TakeQcPolicy::Breath);
  }
  for (const auto* key : {"special:pau", "special:sil"}) {
    const auto policy = voicebank::takeQcPolicyForCoverageKey(key);
    CHECK(policy && policy.value() == TakeQcPolicy::Pause);
  }
  for (const auto* key : {"special:cl", "special:R", "special:glottal"}) {
    const auto policy = voicebank::takeQcPolicyForCoverageKey(key);
    CHECK(policy && policy.value() == TakeQcPolicy::Closure);
  }
  const std::string oversized = "cv:" + std::string(5000U, 'k');
  const std::string longPhone = "cv:" + std::string(129U, 'k') + ":a";
  for (const std::string& key : {std::string{}, std::string{"sustain"}, std::string{":a"},
                                std::string{"sustain:"}, std::string{"cv::a"}, std::string{"cv:k a"},
                                std::string{"special:pau:br"}, std::string{"special:cl:a"},
                                oversized, longPhone}) {
    const auto policy = voicebank::takeQcPolicyForCoverageKey(key);
    CHECK(!policy);
    CHECK(!policy && policy.error().message.size() <= 256U);
  }
  // Explicit applicability, never an implied pass.
  CHECK(voicebank::takeCheckApplies(TakeQcPolicy::Voiced, TakeCheck::RootPitch));
  CHECK(!voicebank::takeCheckApplies(TakeQcPolicy::Voiced, TakeCheck::Quiet));
  CHECK(!voicebank::takeCheckApplies(TakeQcPolicy::Breath, TakeCheck::RootPitch));
  CHECK(voicebank::takeCheckApplies(TakeQcPolicy::Breath, TakeCheck::Unvoiced));
  CHECK(!voicebank::takeCheckApplies(TakeQcPolicy::Pause, TakeCheck::SignalPresent));
  CHECK(voicebank::takeCheckApplies(TakeQcPolicy::Closure, TakeCheck::Quiet));
  for (const auto policy : {TakeQcPolicy::Voiced, TakeQcPolicy::Breath, TakeQcPolicy::Closure, TakeQcPolicy::Pause})
    for (const auto check : {TakeCheck::Format, TakeCheck::Finite, TakeCheck::Clipping, TakeCheck::DcOffset})
      CHECK(voicebank::takeCheckApplies(policy, check));
}

TEST_CASE("genuine breath pause and closure material pass their own policy while bad voiced pitch fails") {
  const auto root = seam::test::support::temporaryDirectory("take-inspection-policies");
  const auto breath = root / "breath.wav";
  const auto pause = root / "pause.wav";
  const auto closure = root / "closure.wav";
  const auto inTune = root / "in-tune.wav";
  const auto sharp = root / "sharp.wav";
  writeTake(breath, breathTake());
  writeTake(pause, pauseTake());
  writeTake(closure, closureTake());
  writeTake(inTune, voicedTake());
  writeTake(sharp, voicedTake(440.0));

  const auto breathAsBreath = inspect(breath, TakeQcPolicy::Breath);
  CHECK(breathAsBreath.accepted());
  CHECK(breathAsBreath.outcome(TakeCheck::SignalPresent) == TakeCheckOutcome::Pass);
  CHECK(breathAsBreath.outcome(TakeCheck::Unvoiced) == TakeCheckOutcome::Pass);
  CHECK(breathAsBreath.outcome(TakeCheck::RootPitch) == TakeCheckOutcome::Inapplicable);
  CHECK(breathAsBreath.outcome(TakeCheck::Quiet) == TakeCheckOutcome::Inapplicable);
  CHECK(breathAsBreath.measurements.voicedShare.has_value());
  CHECK(breathAsBreath.measurements.voicedShare.value_or(1.0) <= voicebank::kTakeMaximumVoicedShare);
  CHECK(!breathAsBreath.measurements.expectedRootMidi && !breathAsBreath.measurements.analyzedRootMidi);
  const auto breathAsVoiced = inspect(breath, TakeQcPolicy::Voiced, 60);
  CHECK(!breathAsVoiced.accepted());
  CHECK(breathAsVoiced.outcome(TakeCheck::RootPitch) == TakeCheckOutcome::Fail);

  const auto pauseAsPause = inspect(pause, TakeQcPolicy::Pause);
  CHECK(pauseAsPause.accepted());
  CHECK(pauseAsPause.outcome(TakeCheck::Quiet) == TakeCheckOutcome::Pass);
  CHECK(pauseAsPause.outcome(TakeCheck::SignalPresent) == TakeCheckOutcome::Inapplicable);
  CHECK(pauseAsPause.outcome(TakeCheck::Unvoiced) == TakeCheckOutcome::Inapplicable);
  CHECK(pauseAsPause.outcome(TakeCheck::RootPitch) == TakeCheckOutcome::Inapplicable);
  CHECK(!pauseAsPause.measurements.voicedShare);
  CHECK(!inspect(pause, TakeQcPolicy::Voiced, 60).accepted());

  const auto closureAsClosure = inspect(closure, TakeQcPolicy::Closure);
  CHECK(closureAsClosure.accepted());
  CHECK(closureAsClosure.outcome(TakeCheck::Quiet) == TakeCheckOutcome::Pass);
  const auto closureAsVoiced = inspect(closure, TakeQcPolicy::Voiced, 60);
  CHECK(closureAsVoiced.outcome(TakeCheck::SignalPresent) == TakeCheckOutcome::Fail);
  CHECK(!closureAsVoiced.accepted());

  const auto voiced = inspect(inTune, TakeQcPolicy::Voiced, 60);
  CHECK(voiced.accepted());
  CHECK(voiced.measurements.analyzedRootMidi == std::optional<std::int32_t>{60});
  CHECK(voiced.measurements.rootPitchDeviationCents.has_value());
  CHECK(std::abs(voiced.measurements.rootPitchDeviationCents.value_or(1000.0)) < 20.0);
  const auto offPitch = inspect(sharp, TakeQcPolicy::Voiced, 60);
  CHECK(!offPitch.accepted());
  CHECK(offPitch.outcome(TakeCheck::RootPitch) == TakeCheckOutcome::Fail);
  CHECK(offPitch.measurements.analyzedRootMidi == std::optional<std::int32_t>{69});

  // Material in the wrong slot is caught by the slot's own policy.
  const auto sungBreath = inspect(inTune, TakeQcPolicy::Breath);
  CHECK(sungBreath.outcome(TakeCheck::Unvoiced) == TakeCheckOutcome::Fail);
  CHECK(!sungBreath.accepted());
  const auto loudPause = inspect(inTune, TakeQcPolicy::Pause);
  CHECK(loudPause.outcome(TakeCheck::Quiet) == TakeCheckOutcome::Fail);
  CHECK(!loudPause.accepted());

  // The outcomes are a pure function of the recorded measurements.
  for (const auto& inspection : {breathAsBreath, pauseAsPause, closureAsClosure, voiced, offPitch})
    CHECK(voicebank::evaluateTakeChecks(inspection.policy, inspection.measurements) == inspection.checks);
  CHECK(!voicebank::inspectTake(inTune, {.policy = TakeQcPolicy::Voiced}));
  CHECK(!voicebank::inspectTake(breath, {.policy = TakeQcPolicy::Breath, .expectedRootMidi = 60}));
  CHECK(!voicebank::inspectTake(inTune, {.policy = TakeQcPolicy::Voiced, .expectedRootMidi = 128}));
}

TEST_CASE("null empty malformed and oversized take audio yields bounded diagnostics and admits nothing") {
  const auto root = seam::test::support::temporaryDirectory("take-inspection-bounds");
  const auto empty = root / "empty.wav";
  const auto garbage = root / "garbage.wav";
  const auto truncated = root / "truncated.wav";
  const auto large = root / "large.wav";
  writeBytes(empty, "");
  writeBytes(garbage, "RIFF\x10\x00\x00\x00WAVEnot a real chunk layout at all");
  writeTake(truncated, voicedTake());
  std::filesystem::resize_file(truncated, std::filesystem::file_size(truncated) / 2U);
  writeTake(large, voicedTake());
  const auto missing = root / std::string(200U, 'x') / std::string(200U, 'y') /
                       std::string(200U, 'z') / "missing.wav";
  struct Case final {
    std::filesystem::path path;
    voicebank::TakeInspectionRequest request;
  };
  const voicebank::TakeInspectionRequest voiced{.policy = TakeQcPolicy::Voiced, .expectedRootMidi = 60};
  const std::vector<Case> cases{
      {{}, voiced}, {missing, voiced}, {root, voiced}, {empty, voiced}, {garbage, voiced},
      {truncated, voiced},
      {large, {.policy = TakeQcPolicy::Voiced, .expectedRootMidi = 60, .maximumBytes = 1024U}},
      {large, {.policy = TakeQcPolicy::Voiced, .expectedRootMidi = 60, .maximumBytes = 0U}}};
  for (const auto& item : cases) {
    const auto inspected = voicebank::inspectTake(item.path, item.request);
    CHECK(!inspected);
    if (inspected) continue;
    CHECK(!inspected.error().message.empty());
    CHECK(inspected.error().message.size() <= 256U);
    CHECK(inspected.error().context.size() <= 512U);
  }
  CHECK(voicebank::inspectTake(missing, voiced).error().code == seam::core::ErrorCode::NotFound);
  CHECK(voicebank::inspectTake(empty, voiced).error().code == seam::core::ErrorCode::ParseError);

  AdmissionFixture fixture;
  const auto before = production::encodeProductionProject(fixture.project);
  for (const auto& path : {empty, garbage, truncated, missing}) {
    CHECK(!fixture.repository.importRaw(fixture.project, path,
        AdmissionFixture::take("take-a", "sustain:a"), AdmissionFixture::event("take-a")));
  }
  fixture.assertUnchanged(before);
  CHECK(fixture.project.takes.empty() && fixture.project.assets.empty());
  CHECK(fixture.project.reviews.empty());
}

TEST_CASE("inspecting one take and importing another requires reinspection") {
  AdmissionFixture fixture;
  const auto first = fixture.root / "first.wav";
  const auto second = fixture.root / "second.wav";
  writeTake(first, voicedTake());
  writeTake(second, voicedTake(262.5));
  auto input = AdmissionFixture::take("take-a", "sustain:a");
  const auto firstReceipt = fixture.receiptFor(first, input);
  const auto before = production::encodeProductionProject(fixture.project);

  input.technicalInspection = firstReceipt;
  const auto swapped = fixture.repository.importRaw(fixture.project, second, input, AdmissionFixture::event("take-a"));
  CHECK(!swapped);
  CHECK(!swapped && swapped.error().code == seam::core::ErrorCode::Conflict);
  fixture.assertUnchanged(before);

  // The inspected file itself is replaced before import.
  const auto replaced = fixture.root / "replaced.wav";
  writeTake(replaced, voicedTake());
  input.technicalInspection = fixture.receiptFor(replaced, input);
  writeTake(replaced, voicedTake(262.5));
  CHECK(!fixture.repository.importRaw(fixture.project, replaced, input, AdmissionFixture::event("take-a")));
  fixture.assertUnchanged(before);

  // A receipt made for another slot cannot admit this one, even for equal bytes.
  auto pauseSlot = AdmissionFixture::take("take-a", "special:pau");
  input.technicalInspection = fixture.receiptFor(first, pauseSlot);
  CHECK(!fixture.repository.importRaw(fixture.project, first, input, AdmissionFixture::event("take-a")));
  // A voiced-only v1 receipt is history, not admission evidence.
  auto legacy = firstReceipt;
  legacy.kind = std::string{production::kLegacyDryTakeInspectionRevisionKind};
  input.technicalInspection = legacy;
  CHECK(!fixture.repository.importRaw(fixture.project, first, input, AdmissionFixture::event("take-a")));
  fixture.assertUnchanged(before);

  input.technicalInspection = firstReceipt;
  const auto admitted = fixture.repository.importRaw(fixture.project, first, input, AdmissionFixture::event("take-a"));
  CHECK(admitted);
  if (!admitted) return;
  CHECK(fixture.project.takes.size() == 1U);
  CHECK(fixture.project.takes.front().state == production::UnitQueueState::MarkerReview);
  CHECK(fixture.project.reviews.empty());
  const auto stored = std::find_if(fixture.project.metadataRevisions.begin(), fixture.project.metadataRevisions.end(),
      [](const auto& value) { return value.kind == production::kTakeInspectionRevisionKind; });
  CHECK(stored != fixture.project.metadataRevisions.end());
  CHECK(stored != fixture.project.metadataRevisions.end() && production::sameMetadataRevision(*stored, firstReceipt));
  const auto current = production::currentTakeInspection(fixture.project, "take-a");
  CHECK(current.has_value());
  const auto storedDigest = seam::core::sha256File(fixture.repository.assetPath(admitted.value()));
  CHECK(storedDigest);
  CHECK(current && storedDigest && current->inspection.sourceSha256 == storedDigest.value());
  // A take cannot be admitted already approved; QC is never an approval.
  auto approved = AdmissionFixture::take("take-br", "breath:br");
  approved.initialState = production::UnitQueueState::Approved;
  const auto breath = fixture.root / "breath.wav";
  writeTake(breath, breathTake());
  CHECK(!fixture.repository.importRaw(fixture.project, breath, approved, AdmissionFixture::event("take-br")));
}

TEST_CASE("every admitted take carries a current receipt of its stored bytes under its unit's policy") {
  AdmissionFixture fixture;
  struct Slot final {
    const char* takeId;
    const char* coverageKey;
    std::vector<float> samples;
    TakeQcPolicy policy;
  };
  const std::vector<Slot> slots{{"take-a", "sustain:a", voicedTake(), TakeQcPolicy::Voiced},
      {"take-br", "breath:br", breathTake(), TakeQcPolicy::Breath},
      {"take-pau", "special:pau", pauseTake(), TakeQcPolicy::Pause},
      {"take-cl", "special:cl", closureTake(), TakeQcPolicy::Closure}};
  for (const auto& slot : slots) {
    const auto path = fixture.root / (std::string{slot.takeId} + ".wav");
    writeTake(path, slot.samples);
    CHECK(fixture.repository.importRaw(fixture.project, path,
        AdmissionFixture::take(slot.takeId, slot.coverageKey), AdmissionFixture::event(slot.takeId)));
  }
  CHECK(fixture.project.takes.size() == slots.size());
  CHECK(production::validateProductionProject(fixture.project));
  for (const auto& slot : slots) {
    const auto receipt = production::currentTakeInspection(fixture.project, slot.takeId);
    CHECK(receipt.has_value());
    if (!receipt) continue;
    CHECK(receipt->inspection.policy == slot.policy);
    CHECK(receipt->inspection.accepted());
    const auto take = std::find_if(fixture.project.takes.begin(), fixture.project.takes.end(),
        [&](const auto& value) { return value.takeId == slot.takeId; });
    const auto asset = std::find_if(fixture.project.assets.begin(), fixture.project.assets.end(),
        [&](const auto& value) { return value.sha256 == take->rawAssetSha256; });
    CHECK(asset != fixture.project.assets.end());
    if (asset == fixture.project.assets.end()) continue;
    const auto stored = seam::core::sha256File(fixture.repository.assetPath(*asset));
    CHECK(stored && stored.value() == receipt->inspection.sourceSha256);
    CHECK(asset->byteSize == receipt->inspection.byteSize);
  }
  const auto recovered = fixture.repository.recover();
  CHECK(recovered);
  CHECK(recovered && production::encodeProductionProject(recovered.value()) ==
                         production::encodeProductionProject(fixture.project));

  // Forged receipts do not validate, even with recomputed digests.
  const auto original = *std::find_if(fixture.project.metadataRevisions.begin(), fixture.project.metadataRevisions.end(),
      [](const auto& value) { return value.takeId == "take-a" && value.kind == production::kTakeInspectionRevisionKind; });
  const auto evidence = production::decodeTakeInspectionEvidence(original.values.at("evidenceJson"));
  CHECK(evidence);
  if (!evidence) return;
  CHECK(validAfterReplacing(fixture.project, reissue(original, evidence.value())));
  auto otherPolicy = evidence.value();
  otherPolicy.inspection.policy = TakeQcPolicy::Breath;
  otherPolicy.inspection.measurements.expectedRootMidi.reset();
  otherPolicy.inspection.measurements.analyzedRootMidi.reset();
  otherPolicy.inspection.measurements.rootPitchDeviationCents.reset();
  otherPolicy.inspection.measurements.voicedShare = 0.0;
  otherPolicy.inspection.checks = voicebank::evaluateTakeChecks(TakeQcPolicy::Breath, otherPolicy.inspection.measurements);
  CHECK(!validAfterReplacing(fixture.project, reissue(original, otherPolicy)));
  auto forgedOutcome = evidence.value();
  forgedOutcome.inspection.checks[static_cast<std::size_t>(TakeCheck::RootPitch)] = TakeCheckOutcome::Fail;
  CHECK(!validAfterReplacing(fixture.project, reissue(original, forgedOutcome)));
  auto otherLayer = evidence.value();
  otherLayer.binding.pitchLayer = 62;
  CHECK(!validAfterReplacing(fixture.project, reissue(original, otherLayer)));
  auto otherSize = evidence.value();
  otherSize.inspection.byteSize += 1U;
  CHECK(!validAfterReplacing(fixture.project, reissue(original, otherSize)));
  auto otherRoot = evidence.value();
  otherRoot.inspection.measurements.expectedRootMidi = 62;
  otherRoot.inspection.checks = voicebank::evaluateTakeChecks(TakeQcPolicy::Voiced, otherRoot.inspection.measurements);
  CHECK(!validAfterReplacing(fixture.project, reissue(original, otherRoot)));
  auto spaced = original;
  spaced.values["evidenceJson"] = " " + original.values.at("evidenceJson");
  spaced.values["evidenceSha256"] = seam::core::sha256Hex(spaced.values.at("evidenceJson"));
  spaced.revisionId = "take-inspection-" + spaced.values.at("evidenceSha256").substr(0U, 32U);
  CHECK(!validAfterReplacing(fixture.project, spaced));
}

TEST_CASE("the asset store publishes only verified copies and repairs a corrupt content address") {
  const auto root = seam::test::support::temporaryDirectory("take-inspection-asset-store");
  const production::ImmutableAssetStore store{root / "assets"};
  const auto source = root / "take.wav";
  writeTake(source, voicedTake());
  const auto first = store.importFile(source, production::AssetKind::Raw);
  CHECK(first);
  if (!first) return;
  CHECK(store.verify(first.value()));
  const auto stored = store.pathFor(first.value());
  writeBytes(stored, "corrupted bytes at a content address");
  CHECK(!store.verify(first.value()));
  const auto repaired = store.importFile(source, production::AssetKind::Raw);
  CHECK(repaired);
  CHECK(store.verify(first.value()));

  // A copy truncated by an earlier crash does not block the digest either.
  const auto other = root / "other.wav";
  writeTake(other, breathTake());
  const auto digest = seam::core::sha256File(other);
  CHECK(digest);
  if (!digest) return;
  const auto address = root / "assets" / "raw" / digest.value().substr(0U, 2U) / (digest.value() + ".wav");
  std::filesystem::create_directories(address.parent_path());
  std::filesystem::copy_file(other, address);
  std::filesystem::resize_file(address, std::filesystem::file_size(address) / 3U);
  const auto recovered = store.importFile(other, production::AssetKind::Raw);
  CHECK(recovered);
  CHECK(recovered && store.verify(recovered.value()));
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root / "assets"))
    CHECK(entry.path().extension() != ".partial");
}
