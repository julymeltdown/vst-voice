#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/authoring/export_service.hpp"
#include "seam/authoring/helper_process.hpp"
#include "seam/authoring/voicebank_installer_service.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/distribution/seambank.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/repository.hpp"

namespace {
using namespace seam;
namespace production = voicebank_production;
struct CliFixture final {
  std::filesystem::path root{test::support::temporaryDirectory("sample-review-cli")};
  production::ProductionProjectRepository repository{root / "producer"};
  production::VoicebankProductionProject project;
  voicebank::Manifest manifest;
  CliFixture() {
    const auto license=root / "fixture-license.txt";
    CHECK(core::durableAtomicWriteTextNew(license,"Synthetic integration fixture, no production singer qualification."));
    const auto hash=core::sha256File(license); CHECK(hash);
    project={.projectId="cli-source", .inventoryId="fixture", .inventorySha256=std::string(64U,'a'),
      .selectedSourceStrategyId="fixture", .licenseLocator=license.string(), .licenseSha256=hash.value()};
    project.sourceStrategies={{.id="fixture", .kind=production::SourceStrategyKind::ProceduralSynthesis,
      .rights=production::Feasibility::Pass, .coverage=production::Feasibility::NotAssessed, .listening=production::Feasibility::NotAssessed,
      .permissions={true,true,true,true}, .licenseLocator=license.string(), .licenseSha256=hash.value(), .evidenceState="SYNTHETIC_TEST_ONLY"}};
    project.operators={{.operatorId="producer",.role="PRODUCER"},{.operatorId="reviewer",.role="REVIEWER"}};
    project.unitAssignments={{.coverageKey="sustain:a",.pitchLayer=69,.promptId="a",.plannedTakeId="take-a"}};
    const auto declaredSource = project.sourceStrategies.front();
    project.sourceStrategies.clear(); project.selectedSourceStrategyId.clear();
    project.licenseLocator.clear(); project.licenseSha256.clear();
    const auto definition=production::encodeProductionProject(project);
    CHECK(core::durableAtomicWriteTextNew(root / "draft-definition.json",definition));
    const auto created=success({"init-production",(root / "producer").string(),(root / "draft-definition.json").string(),
        core::sha256Hex(definition),"producer","2026-09-09T10:00:00Z"});
    CHECK(created.find("lifecycle")->asString()=="DRAFT");
    const auto initialized=repository.recover(); CHECK(initialized); project=initialized.value();
    CHECK(project.sourceStrategies.empty());
    const auto registered = success({"register-source",(root/"producer").string(),
        core::sha256Hex(production::encodeProductionProject(project)),declaredSource.id,"procedural","pass",
        "yes","yes","yes","yes",declaredSource.licenseLocator,declaredSource.licenseSha256,"producer","2026-09-09T10:00:30Z"});
    CHECK(registered.find("result")->asString() == "SourceRegistered");
    CHECK(registered.find("coverage")->asString() == "NOT_ASSESSED");
    const auto sourced = repository.recover(); CHECK(sourced); project = sourced.value();
    CHECK(project.sourceQualityAssessments.empty()); CHECK(!production::selectedStrategyReady(project));
    const auto samples=test::support::sineWave(48000U,440.0,0.12,0.25F);
    CHECK(voicebank::writeWav(root / "raw.wav",{.sampleRate=48000U,.channels=1U,.sampleFormat=voicebank::WavSampleFormat::Pcm24},samples));
    CHECK(repository.importRaw(project,root / "raw.wav",{.takeId="take-a",.promptId="a",.coverageKey="sustain:a",.pitchLayer=69},
      {.action="import",.subjectId="take-a",.operatorId="producer",.occurredAtUtc="2026-09-09T10:01:00Z"}));
    CHECK(!production::requireTakeSourceQualification(project,"take-a"));
    const auto inspection = success({"inspect-source-quality",(root/"producer").string(),"fixture"});
    const auto qualityEvidence = root/"quality-evidence.txt";
    const std::string qualityText = "Synthetic reviewer decision for this one-tone fixture; not actual singer qualification.";
    CHECK(core::durableAtomicWriteTextNew(qualityEvidence,qualityText));
    const auto assessed = success({"record-source-quality",(root/"producer").string(),"fixture",inspection.find("projectSha256")->asString(),
        "fixture-quality","reviewer","2026-09-09T10:01:30Z","pass","pass",qualityEvidence.string(),core::sha256Hex(qualityText)});
    CHECK(assessed.find("result")->asString() == "SourceQualityAssessmentCommitted");
    CHECK(assessed.find("sourcePermissions")->asString() == "unchanged");
    const auto assessedProject = repository.recover(); CHECK(assessedProject); project = assessedProject.value();
    CHECK(project.schemaVersion == 3); CHECK(project.sourceQualityAssessments.size() == 1U);
    CHECK(project.reviews.empty()); CHECK(production::requireTakeSourceQualification(project,"take-a"));
    const auto draft = success({"create-sample-draft", (root / "producer").string(), "cli.published.fixture", "0.1.0",
        "Synthetic CLI fixture", "ja", "original", (root / "editable").string()});
    CHECK(draft.find("result")->asString() == "EditableDraftCommitted");
    CHECK(draft.find("approval")->asString() == "unchanged");
    const auto loaded = voicebank::ManifestJsonCodec{}.load(root / "editable/manifest.json"); CHECK(loaded);
    manifest = loaded.value();
    CHECK(manifest.units.front().pitchMarks.size() >= 3U);
    CHECK(std::all_of(manifest.units.front().pitchMarks.begin(), manifest.units.front().pitchMarks.end(),
        [](const auto& mark) { return !mark.locked; }));
  }
  core::Result<authoring::HelperProcessOutput> run(std::vector<std::string> arguments) const {
    return authoring::runBoundedHelperProcess({.executable=SEAM_TEST_VOICEBANK_CLI,.arguments=std::move(arguments)});
  }
  formats::JsonValue success(std::vector<std::string> arguments) const {
    const auto output=run(std::move(arguments));
    if (!output) throw std::runtime_error(output.error().message+": "+output.error().context);
    const auto json=formats::parseJson(output.value().standardOutput); CHECK(json); return json.value();
  }
  formats::JsonValue capture() const {
    return success({"prepare-sample-review",(root / "producer").string(),(root / "editable/manifest.json").string(),(root / "packet.json").string()});
  }
  std::vector<std::string> reviewArgs(std::string hash,std::string actor="reviewer",std::string decision="accept") const {
    return {"review-sample",(root / "producer").string(),(root / "packet.json").string(),std::move(hash),
      std::move(actor),"2026-09-09T10:02:00Z",std::move(decision)};
  }
};
} // namespace

TEST_CASE("CLI initializes and recovers a source-free draft without fabricated feasibility") {
#if defined(__APPLE__) || defined(__linux__)
  using namespace seam;
  const auto root=test::support::temporaryDirectory("source-free-cli-draft");
  production::VoicebankProductionProject draft{.projectId="unfinished-new-voice"};
  draft.operators={{.operatorId="producer",.role="PRODUCER"}};
  const auto text=production::encodeProductionProject(draft);
  CHECK(core::durableAtomicWriteTextNew(root / "draft.json",text));
  const std::vector<std::string> arguments{"init-production",(root / "workspace").string(),(root / "draft.json").string(),
      core::sha256Hex(text),"producer","2026-09-09T10:00:00Z"};
  const auto created=authoring::runBoundedHelperProcess({.executable=SEAM_TEST_VOICEBANK_CLI,.arguments=arguments});
  if (!created) throw std::runtime_error(created.error().message+": "+created.error().context);
  production::ProductionProjectRepository repository{root / "workspace"};
  const auto recovered=repository.recover(); CHECK(recovered);
  CHECK(recovered.value().lifecycle==production::ProductionLifecycle::Draft);
  CHECK(recovered.value().sourceStrategies.empty());
  CHECK(recovered.value().reviews.empty());
  CHECK(recovered.value().takes.empty());
  CHECK(!production::requireSelectedSourceExecution(recovered.value()));
  const auto before=production::encodeProductionProject(recovered.value());
  CHECK(!authoring::runBoundedHelperProcess({.executable=SEAM_TEST_VOICEBANK_CLI,.arguments=arguments}));
  CHECK(production::encodeProductionProject(repository.recover().value())==before);
  auto wrong=arguments; wrong[1]=(root / "wrong-digest").string(); wrong[3]=std::string(64U,'b');
  CHECK(!authoring::runBoundedHelperProcess({.executable=SEAM_TEST_VOICEBANK_CLI,.arguments=wrong}));
  CHECK(!std::filesystem::exists(root / "wrong-digest"));
#endif
}

TEST_CASE("CLI captured review publishes packages installs and exports a reopened new score without producer inputs") {
#if defined(__APPLE__) || defined(__linux__)
  using namespace seam;
  CliFixture fixture;
  const auto sourceBefore=production::encodeProductionProject(fixture.project);
  const auto captured=fixture.capture();
  const auto packetHash=captured.find("fileSha256")->asString();
  CHECK(captured.find("approval")->asString()=="unchanged");
  const auto afterCapture=fixture.repository.recover(); CHECK(afterCapture);
  CHECK(production::encodeProductionProject(afterCapture.value())==sourceBefore);
  CHECK(afterCapture.value().reviews.empty());
  CHECK(afterCapture.value().unitAssignments.front().state==production::UnitQueueState::MarkerReview);
  const auto inspection=fixture.success({"inspect-sample-review",(fixture.root / "packet.json").string(),packetHash});
  CHECK(inspection.find("units")->asArray().size()==1U);
  CHECK(inspection.find("units")->asArray().front().find("originOperatorId")->asString()=="producer");
  CHECK(!fixture.run(fixture.reviewArgs(packetHash,"producer")));
  CHECK(!fixture.run(fixture.reviewArgs(std::string(64U,'b'))));
  const auto reviewed=fixture.success(fixture.reviewArgs(packetHash));
  CHECK(reviewed.find("result")->asString()=="ReviewCommitted");
  CHECK(reviewed.find("candidateAvailable")->asBool());
  CHECK(reviewed.find("durabilityConfirmed")->asBool());
  CHECK(!reviewed.find("releaseEligible")->asBool());
  CHECK(!fixture.run(fixture.reviewArgs(packetHash))); // The captured generation is consumed.
  const auto candidate=fixture.root / "candidate";
  auto publishArgs=std::vector<std::string>{"publish-sample",(fixture.root / "producer").string(),
      (fixture.root / "editable/manifest.json").string(),reviewed.find("generation")->asString(),reviewed.find("projectSha256")->asString(),candidate.string()};
  auto stale=publishArgs; stale[3]=captured.find("generation")->asString();
  CHECK(!fixture.run(stale)); CHECK(!std::filesystem::exists(candidate));
  const auto published=fixture.success(publishArgs);
  CHECK(published.find("result")->asString()=="CandidateCommitted");
  const auto& assessment = fixture.project.sourceQualityAssessments.front();
  CHECK(core::sha256File(candidate/"provenance/source-evidence"/(assessment.evidenceSha256+".quality.txt")).value() == assessment.evidenceSha256);
  CHECK(!published.find("releaseEligible")->asBool());
  CHECK(!fixture.run(publishArgs)); // Create-new, never overwrite an existing candidate.
  const auto key=distribution::generateSigningKeyPair(); CHECK(key);
  const auto package=fixture.root / "candidate.seambank";
  const auto packed = distribution::packSeambank(candidate,package,key.value());
  if (!packed) throw std::runtime_error(packed.error().message + ": " + packed.error().context);
  authoring::VoicebankSession installedBanks({{fixture.root / "installed",voicebank::VoicebankRootKind::Installed}},false);
  authoring::VoicebankInstallerService installer{installedBanks,fixture.root / "installed"};
  const auto installed=installer.install({.packagePath=package,.trustedPublicKeys={key.value().publicKey}}); CHECK(installed);
  CHECK(installed.value().candidate.trust==voicebank::VoicebankTrust::TrustedInstalled);
  CHECK(installed.value().contentHash==published.find("contentSha256")->asString());

  application::ProjectFactory factory{989000U};
  auto song=factory.createProject("New melody from the installed reviewed candidate");
  const auto track=factory.addVocalTrack(song,"Singer");
  const auto region=factory.addRegion(song,track,"New melody",time::Tick{0},time::Tick{1920});
  for (int i=0;i<2;++i) {
    auto [lyric,note]=factory.makeNote(time::Tick{960*i},time::Tick{960},static_cast<std::uint8_t>(69+3*i),U"あ",domain::Language::Japanese);
    song.findRegion(region)->lyrics.push_back(lyric); song.findRegion(region)->notes.push_back(note);
  }
  song.findVocalTrack(track)->voicebank={fixture.manifest.id,fixture.manifest.version,installed.value().contentHash};
  song.findVocalTrack(track)->styleSelection={domain::VoiceStyleOrigin::Explicit,"original"};
  CHECK(formats::ProjectJsonCodec{}.save(song,fixture.root / "song.seam"));
  std::filesystem::rename(fixture.root / "producer",fixture.root / "producer-unavailable");
  std::filesystem::rename(candidate,fixture.root / "candidate-unavailable");
  std::filesystem::rename(fixture.root / "raw.wav",fixture.root / "raw-unavailable.wav");
  std::filesystem::rename(fixture.root / "quality-evidence.txt",fixture.root / "quality-evidence-unavailable.txt");
  std::filesystem::rename(fixture.root / "fixture-license.txt",fixture.root / "fixture-license-unavailable.txt");
  const auto reopened=formats::ProjectJsonCodec{}.load(fixture.root / "song.seam"); CHECK(reopened);
  authoring::VoicebankSession fresh({{fixture.root / "installed",voicebank::VoicebankRootKind::Installed}},false);
  CHECK(fresh.refresh());
  const auto resolved=fresh.resolveTrack(reopened.value(),track); CHECK(resolved.resolved());
  CHECK(resolved.candidate->trust==voicebank::VoicebankTrust::TrustedInstalled);
  const auto& bank=*resolved.candidate;
  const std::vector<rendering::TrackVoicebankSource> sources{{track,bank.manifest,bank.bankRoot,bank.contentHash,bank.trust}};
  const auto rendered=rendering::ProductionProjectRenderer{}.render(reopened.value(),sources,track,region,1U,48000U,rendering::RenderQuality::Final);
  CHECK(rendered); CHECK(rendered.value().diagnostics.empty()); CHECK(rendered.value().fallbackCount==0U);
  CHECK(voicebank::analyzeAudio(std::span<const float>{rendered.value().interleaved.data(),rendered.value().interleaved.size()}).rms>1e-4);
  authoring::ExportSettings settings; settings.format=voicebank::WavSampleFormat::Float32; settings.includeStems=true;
  const auto exported=authoring::ExportService{}.exportSet(reopened.value(),sources,track,region,1U,fixture.root / "export",settings);
  CHECK(exported); CHECK(exported.value().state==authoring::ExportState::Committed); CHECK(exported.value().files.size()==2U);
  for (const auto& file:exported.value().files) {
    const auto audio=voicebank::readWav(file.path); CHECK(audio);
    CHECK(audio.value().interleaved==rendered.value().interleaved);
  }
#endif
}

TEST_CASE("CLI source reassessment invalidates affected unit approval without erasing review history or source rights") {
#if defined(__APPLE__) || defined(__linux__)
  CliFixture fixture;
  const auto captured=fixture.capture();
  CHECK(fixture.success(fixture.reviewArgs(captured.find("fileSha256")->asString())).find("candidateAvailable")->asBool());
  const auto approved=fixture.repository.recover(); CHECK(approved); CHECK(approved.value().unitAssignments.front().state==production::UnitQueueState::Approved);
  const auto current=fixture.success({"inspect-source-quality",(fixture.root/"producer").string(),"fixture"});
  const auto decision=fixture.success({"record-source-quality",(fixture.root/"producer").string(),"fixture",current.find("projectSha256")->asString(),
      "quality-reassessment","reviewer","2026-09-09T10:03:00Z","pass","blocked",(fixture.root/"quality-evidence.txt").string(),
      core::sha256File(fixture.root/"quality-evidence.txt").value()});
  CHECK(!decision.find("releaseEligible")->asBool());
  const auto changed=fixture.repository.recover(); CHECK(changed);
  CHECK(changed.value().sourceQualityAssessments.size()==2U);
  CHECK(changed.value().reviews.size()==approved.value().reviews.size());
  CHECK(changed.value().sourceBindings==approved.value().sourceBindings);
  CHECK(changed.value().unitAssignments.front().state==production::UnitQueueState::MarkerReview);
  CHECK(!changed.value().unitAssignments.front().markerReviewed); CHECK(!changed.value().unitAssignments.front().pitchReviewed);
  CHECK(production::requireTakeSourceExecution(changed.value(),"take-a"));
  CHECK(!production::requireTakeSourceQualification(changed.value(),"take-a"));
#endif
}

TEST_CASE("CLI review capture preserves occupied files and rejected or changed manifest cannot publish") {
#if defined(__APPLE__) || defined(__linux__)
  CliFixture fixture;
  const auto captured=fixture.capture();
  const auto before=core::readTextFileLimited(fixture.root / "packet.json",1024U*1024U); CHECK(before);
  CHECK(!fixture.run({"prepare-sample-review",(fixture.root / "producer").string(),(fixture.root / "editable/manifest.json").string(),(fixture.root / "packet.json").string()}));
  CHECK(core::readTextFileLimited(fixture.root / "packet.json",1024U*1024U).value()==before.value());
  const auto reviewed=fixture.success(fixture.reviewArgs(captured.find("fileSha256")->asString(),"reviewer","reject"));
  CHECK(!reviewed.find("candidateAvailable")->asBool());
  CHECK(!fixture.run({"publish-sample",(fixture.root / "producer").string(),(fixture.root / "editable/manifest.json").string(),
      reviewed.find("generation")->asString(),reviewed.find("projectSha256")->asString(),(fixture.root / "candidate").string()}));
  CHECK(!std::filesystem::exists(fixture.root / "candidate"));
  CliFixture changed;
  const auto newCapture=changed.capture();
  const auto accepted=changed.success(changed.reviewArgs(newCapture.find("fileSha256")->asString()));
  changed.manifest.units.front().gainDb=-6.0F;
  CHECK(voicebank::ManifestJsonCodec{}.save(changed.manifest,changed.root / "editable/manifest.json"));
  CHECK(!changed.run({"publish-sample",(changed.root / "producer").string(),(changed.root / "editable/manifest.json").string(),
      accepted.find("generation")->asString(),accepted.find("projectSha256")->asString(),(changed.root / "candidate").string()}));
  CHECK(!std::filesystem::exists(changed.root / "candidate"));
#endif
}
