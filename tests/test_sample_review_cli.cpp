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
#include "seam/voicebank/acoustic_analysis.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/repository.hpp"
#include "seam/voicebank_production/resource_candidate.hpp"
#include "seam/voicebank_production/candidate_publication.hpp"
#include "seam/candidate_packaging/candidate_package.hpp"

#include <map>
#include <set>
#include <stop_token>

TEST_CASE("CLI inspects frozen neural metadata without claiming graph execution") {
#if defined(SEAM_TEST_VOICEBANK_CLI)
  using namespace seam; using J=formats::JsonValue;
  const auto root=test::support::temporaryDirectory("inspect-neural-cli");
  const auto feature=formats::parseJson(R"({"sampleRate":48000,"hopSize":256,"bins":80,"layout":"BTF","amplitudeScale":"ln-amplitude","multiplier":1.0,"offset":0.0,"minimumHz":40.0,"maximumHz":16000.0})"); CHECK(feature);
  const auto configuration=formats::stringifyJson(J{J::Object{{"formatId","com.project-seam.neural-bundle-configuration"},
      {"schemaVersion",std::int64_t{1}},{"maximumFrames",std::int64_t{48000}},
      {"acousticFeatures",feature.value()},{"vocoderFeatures",feature.value()}}});
  // Deliberately not graphs: metadata inspection must never imply execution.
  const std::map<std::string,std::string> files{{"acoustic","not an ONNX graph"},{"configuration",configuration},
      {"vocabulary",R"({"formatId":"com.project-seam.neural-vocabulary","schemaVersion":1,"tokens":["<PAD>","SP","a"]})"},
      {"vocoder","not a vocoder"}};
  J::Array assets;
  for (const auto& [name,bytes]:files) {
    CHECK(core::durableAtomicWriteTextNew(root/name,bytes));
    assets.emplace_back(J::Object{{"name",name},{"role",name},{"sha256",core::sha256Hex(bytes)},
        {"bytes",static_cast<std::int64_t>(bytes.size())}});
  }
  const auto manifest=formats::stringifyJson(J{J::Object{{"formatId","com.project-seam.neural-data-bundle"},
      {"schemaVersion",std::int64_t{1}},{"assets",assets}}});
  const std::vector<std::string> prepareArgs{"prepare-neural-bundle",root.string(),"fixture","1","4096"};
  CHECK(core::durableAtomicWriteText(root/"configuration","{}"));
  CHECK(!authoring::runBoundedHelperProcess({.executable=SEAM_TEST_VOICEBANK_CLI,.arguments=prepareArgs}));
  CHECK(!std::filesystem::exists(root/"manifest.json"));
  CHECK(core::durableAtomicWriteText(root/"configuration",configuration));
  const auto prepared=authoring::runBoundedHelperProcess({.executable=SEAM_TEST_VOICEBANK_CLI,.arguments=prepareArgs}); CHECK(prepared);
  const auto preparedReport=formats::parseJson(prepared.value().standardOutput); CHECK(preparedReport);
  CHECK(preparedReport.value().find("status")->asString()=="DATA_BUNDLE_PREPARED_UNAPPROVED");
  CHECK(preparedReport.value().find("manifestSha256")->asString()==core::sha256Hex(manifest));
  CHECK(core::readTextFileLimited(root/"manifest.json",32768).value()==manifest);
  CHECK(!authoring::runBoundedHelperProcess({.executable=SEAM_TEST_VOICEBANK_CLI,.arguments=prepareArgs}));
  CHECK(core::readTextFileLimited(root/"manifest.json",32768).value()==manifest);
  std::vector<std::string> args{"inspect-neural-bundle",root.string(),"fixture","1",core::sha256Hex(manifest),"4096"};
  const auto run=authoring::runBoundedHelperProcess({.executable=SEAM_TEST_VOICEBANK_CLI,.arguments=args}); CHECK(run);
  const auto report=formats::parseJson(run.value().standardOutput); CHECK(report);
  CHECK(report.value().find("status")->asString()=="METADATA_INSPECTED_ONLY");
  CHECK(!report.value().find("executionAdmitted")->asBool()); CHECK(!report.value().find("releaseEligible")->asBool());
  CHECK(report.value().find("vocabularySize")->asInt64()==3);
  CHECK(report.value().find("manifestSha256")->asString()==args[4]);
  for (const auto* limit:{"0","-1","4096x","536870913"}) {
    auto invalid=args; invalid.back()=limit;
    CHECK(!authoring::runBoundedHelperProcess({.executable=SEAM_TEST_VOICEBANK_CLI,.arguments=invalid}));
  }
  CHECK(core::durableAtomicWriteText(root/"acoustic","modified"));
  CHECK(!authoring::runBoundedHelperProcess({.executable=SEAM_TEST_VOICEBANK_CLI,.arguments=args}));
#endif
}

TEST_CASE("CLI converts hash-bound neural vocabulary without overwriting files or approving it") {
#if defined(SEAM_TEST_VOICEBANK_CLI)
  using namespace seam;
  const auto root=test::support::temporaryDirectory("neural-vocabulary-cli");
  const auto source=root/"export.json",output=root/"vocabulary.json";
  const std::string mapping=R"({"SP":1,"ja/a":2,"ko/a":2})";
  CHECK(core::durableAtomicWriteTextNew(source,mapping));
  std::vector<std::string> args{"convert-neural-vocabulary",source.string(),core::sha256Hex(mapping),output.string()};
  auto wrong=args; wrong[2]=std::string(64,'0');
  CHECK(!authoring::runBoundedHelperProcess({.executable=SEAM_TEST_VOICEBANK_CLI,.arguments=wrong}));
  CHECK(!std::filesystem::exists(output));
  const auto run=authoring::runBoundedHelperProcess({.executable=SEAM_TEST_VOICEBANK_CLI,.arguments=args}); CHECK(run);
  const auto report=formats::parseJson(run.value().standardOutput); CHECK(report);
  CHECK(report.value().find("status")->asString()=="CONVERTED_UNAPPROVED");
  CHECK(!report.value().find("releaseEligible")->asBool());
  const auto converted=core::readTextFileLimited(output,4096); CHECK(converted);
  CHECK(report.value().find("vocabularySha256")->asString()==core::sha256Hex(converted.value()));
  const auto vocabulary=formats::parseJson(converted.value()); CHECK(vocabulary);
  CHECK(vocabulary.value().find("tokens")->asArray().size()==3U);
  CHECK(vocabulary.value().find("aliases")->find("ko/a")->asInt64()==2);
  CHECK(!authoring::runBoundedHelperProcess({.executable=SEAM_TEST_VOICEBANK_CLI,.arguments=args}));
  CHECK(core::readTextFileLimited(output,4096).value()==converted.value());
  CHECK(core::readTextFileLimited(source,4096).value()==mapping);
#endif
}

namespace {
using namespace seam;
namespace production = voicebank_production;
struct CliFixture final {
  std::filesystem::path root{test::support::temporaryDirectory("sample-review-cli")};
  production::ProductionProjectRepository repository{root / "producer"};
  production::VoicebankProductionProject project;
  voicebank::Manifest manifest;
  explicit CliFixture(std::vector<std::string> coverageKeys = {"sustain:a"}) {
    const auto license=root / "fixture-license.txt";
    CHECK(core::durableAtomicWriteTextNew(license,"Synthetic integration fixture, no production singer qualification."));
    const auto hash=core::sha256File(license); CHECK(hash);
    project={.projectId="cli-source", .inventoryId="fixture", .inventorySha256=std::string(64U,'a'),
      .selectedSourceStrategyId="fixture", .licenseLocator=license.string(), .licenseSha256=hash.value()};
    project.sourceStrategies={{.id="fixture", .kind=production::SourceStrategyKind::ProceduralSynthesis,
      .rights=production::Feasibility::Pass, .coverage=production::Feasibility::NotAssessed, .listening=production::Feasibility::NotAssessed,
      .permissions={true,true,true,true}, .licenseLocator=license.string(), .licenseSha256=hash.value(), .evidenceState="SYNTHETIC_TEST_ONLY"}};
    project.operators={{.operatorId="producer",.role="PRODUCER"},{.operatorId="reviewer",.role="REVIEWER"}};
    // One assignment and one take per declared class, so the same lifecycle can run over a
    // single unit or over a bank that covers a consonant, a coda and a vowel sequence.
    std::vector<std::string> takeNames;
    for (std::size_t index = 0U; index < coverageKeys.size(); ++index) {
      const std::string letter(1U, static_cast<char>('a' + static_cast<int>(index)));
      takeNames.push_back("take-" + letter);
      project.unitAssignments.push_back({.coverageKey=coverageKeys[index],.pitchLayer=69,
          .promptId=letter,.plannedTakeId="take-" + letter});
    }
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
    for (std::size_t index = 0U; index < takeNames.size(); ++index) {
      const std::string letter(1U, static_cast<char>('a' + static_cast<int>(index)));
      const auto source = root / ("raw-" + letter + ".wav");
      const auto samples = test::support::sineWave(48000U, 440.0 + 40.0 * static_cast<double>(index), 0.12, 0.25F);
      CHECK(voicebank::writeWav(source,{.sampleRate=48000U,.channels=1U,.sampleFormat=voicebank::WavSampleFormat::Pcm24},samples));
      CHECK(repository.importRaw(project,source,{.takeId=takeNames[index],.promptId=letter,
          .coverageKey=coverageKeys[index],.pitchLayer=69},
        {.action="import",.subjectId=takeNames[index],.operatorId="producer",.occurredAtUtc="2026-09-09T10:01:00Z"}));
      CHECK(!production::requireTakeSourceQualification(project,takeNames[index]));
    }
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
  std::filesystem::rename(fixture.root / "raw-a.wav",fixture.root / "raw-a-unavailable.wav");
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

TEST_CASE("CLI runs the whole lifecycle over a multi-unit bank without producer inputs") {
#if defined(__APPLE__) || defined(__linux__)
  using namespace seam;
  // Four classes rather than the single sustained vowel the original regression used: a
  // consonant onset, a coda, a vowel sequence and a sustain, which is what a bank has to carry
  // before a song that is more than one held vowel can be sung from it.
  CliFixture fixture({"sustain:a", "cv:s:a", "vc:a:s", "vv:a:i"});
  const auto sourceBefore = production::encodeProductionProject(fixture.project);
  const auto captured = fixture.capture();
  const auto packetHash = captured.find("fileSha256")->asString();
  const auto inspection = fixture.success({"inspect-sample-review", (fixture.root / "packet.json").string(), packetHash});
  CHECK(inspection.find("units")->asArray().size() == 4U);
  // Capturing a packet still approves and changes nothing.
  const auto afterCapture = fixture.repository.recover(); CHECK(afterCapture);
  CHECK(production::encodeProductionProject(afterCapture.value()) == sourceBefore);
  CHECK(afterCapture.value().unitAssignments.size() == 4U);
  for (const auto& unit : afterCapture.value().unitAssignments) CHECK(unit.state == production::UnitQueueState::MarkerReview);
  CHECK(afterCapture.value().reviews.empty());
  const auto reviewed = fixture.success(fixture.reviewArgs(packetHash));
  CHECK(reviewed.find("result")->asString() == "ReviewCommitted");
  CHECK(!reviewed.find("releaseEligible")->asBool());
  // The decision covered every unit of the bank, not only the first.
  const auto reviewedProject = fixture.repository.recover(); CHECK(reviewedProject);
  CHECK(reviewedProject.value().unitAssignments.size() == 4U);
  for (const auto& unit : reviewedProject.value().unitAssignments) CHECK(unit.state == production::UnitQueueState::Approved);
  const auto candidate = fixture.root / "candidate";
  const auto published = fixture.success({"publish-sample", (fixture.root / "producer").string(),
      (fixture.root / "editable/manifest.json").string(), reviewed.find("generation")->asString(),
      reviewed.find("projectSha256")->asString(), candidate.string()});
  CHECK(published.find("result")->asString() == "CandidateCommitted");
  CHECK(!published.find("releaseEligible")->asBool());
  const auto candidateManifest = voicebank::ManifestJsonCodec{}.load(candidate / "manifest.json"); CHECK(candidateManifest);
  if (!candidateManifest) return;
  CHECK(candidateManifest.value().units.size() == 4U);
  const auto key = distribution::generateSigningKeyPair(); CHECK(key);
  const auto package = fixture.root / "bank.seambank";
  const auto packed = distribution::packSeambank(candidate, package, key.value());
  if (!packed) throw std::runtime_error(packed.error().message + ": " + packed.error().context);
  authoring::VoicebankSession installedBanks({{fixture.root / "installed", voicebank::VoicebankRootKind::Installed}}, false);
  authoring::VoicebankInstallerService installer{installedBanks, fixture.root / "installed"};
  const auto installed = installer.install({.packagePath = package, .trustedPublicKeys = {key.value().publicKey}}); CHECK(installed);
  if (!installed) return;
  CHECK(installed.value().candidate.trust == voicebank::VoicebankTrust::TrustedInstalled);
  CHECK(installed.value().candidate.manifest.units.size() == 4U);
  CHECK(installed.value().contentHash == published.find("contentSha256")->asString());
  // A song that needs the consonant and the vowel, not just one held vowel.
  application::ProjectFactory factory{991000U};
  auto song = factory.createProject("New melody from a four-class installed candidate");
  const auto track = factory.addVocalTrack(song, "Singer");
  const auto region = factory.addRegion(song, track, "New melody", time::Tick{0}, time::Tick{1920});
  for (int index = 0; index < 2; ++index) {
    auto [lyric, note] = factory.makeNote(time::Tick{960 * index}, time::Tick{960},
        static_cast<std::uint8_t>(69 + 3 * index), index == 0 ? U"あ" : U"さ", domain::Language::Japanese);
    song.findRegion(region)->lyrics.push_back(lyric);
    song.findRegion(region)->notes.push_back(note);
  }
  song.findVocalTrack(track)->voicebank = {fixture.manifest.id, fixture.manifest.version, installed.value().contentHash};
  song.findVocalTrack(track)->styleSelection = {domain::VoiceStyleOrigin::Explicit, "original"};
  CHECK(formats::ProjectJsonCodec{}.save(song, fixture.root / "song.seam"));
  // Nothing the bank was built from may remain reachable by the new song.
  std::filesystem::rename(fixture.root / "producer", fixture.root / "producer-unavailable");
  std::filesystem::rename(candidate, fixture.root / "candidate-unavailable");
  for (const auto* name : {"raw-a.wav", "raw-b.wav", "raw-c.wav", "raw-d.wav"})
    std::filesystem::rename(fixture.root / name, fixture.root / (std::string{name} + ".unavailable"));
  std::filesystem::rename(fixture.root / "quality-evidence.txt", fixture.root / "quality-evidence-unavailable.txt");
  std::filesystem::rename(fixture.root / "fixture-license.txt", fixture.root / "fixture-license-unavailable.txt");
  const auto reopened = formats::ProjectJsonCodec{}.load(fixture.root / "song.seam"); CHECK(reopened);
  if (!reopened) return;
  authoring::VoicebankSession fresh({{fixture.root / "installed", voicebank::VoicebankRootKind::Installed}}, false);
  CHECK(fresh.refresh());
  const auto resolved = fresh.resolveTrack(reopened.value(), track); CHECK(resolved.resolved());
  if (!resolved.resolved()) return;
  CHECK(resolved.candidate->trust == voicebank::VoicebankTrust::TrustedInstalled);
  const auto& bank = *resolved.candidate;
  const std::vector<rendering::TrackVoicebankSource> sources{{track, bank.manifest, bank.bankRoot, bank.contentHash, bank.trust}};
  const auto rendered = rendering::ProductionProjectRenderer{}.render(reopened.value(), sources, track, region, 1U, 48000U, rendering::RenderQuality::Final);
  CHECK(rendered);
  if (!rendered) return;
  CHECK(rendered.value().diagnostics.empty());
  CHECK(rendered.value().fallbackCount == 0U);
  CHECK(voicebank::analyzeAudio(std::span<const float>{rendered.value().interleaved.data(), rendered.value().interleaved.size()}).rms > 1e-4);
  authoring::ExportSettings settings; settings.format = voicebank::WavSampleFormat::Float32; settings.includeStems = true;
  const auto exported = authoring::ExportService{}.exportSet(reopened.value(), sources, track, region, 1U, fixture.root / "export", settings);
  CHECK(exported);
  if (!exported) return;
  CHECK(exported.value().state == authoring::ExportState::Committed);
  CHECK(exported.value().files.size() >= 2U);
  for (const auto& file : exported.value().files) {
    const auto audio = voicebank::readWav(file.path); CHECK(audio);
    if (audio) CHECK(audio.value().interleaved == rendered.value().interleaved);
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

namespace {
std::string readText(const std::filesystem::path& path) {
  const auto text = core::readTextFileLimited(path, 64U * 1024U * 1024U);
  if (!text) throw std::runtime_error(text.error().message + ": " + path.string());
  return text.value();
}
bool replaceOnce(std::string& text, std::string_view from, std::string_view to) {
  const auto at = text.find(from);
  if (at == std::string::npos) return false;
  text.replace(at, from.size(), to);
  return true;
}
std::filesystem::path copyTree(const std::filesystem::path& from, const std::filesystem::path& to) {
  std::filesystem::copy(from, to, std::filesystem::copy_options::recursive);
  return to;
}
std::vector<std::string> publishArgs(const CliFixture& fixture, std::string generation, std::string projectSha256,
                                     const std::filesystem::path& destination) {
  return {"publish-sample", (fixture.root / "producer").string(), (fixture.root / "editable/manifest.json").string(),
      std::move(generation), std::move(projectSha256), destination.string()};
}
std::map<std::string, std::string> treeDigest(const std::filesystem::path& root) {
  std::map<std::string, std::string> files;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
    if (entry.is_regular_file())
      files.emplace(entry.path().lexically_relative(root).generic_string(), core::sha256File(entry.path(), 1ULL << 30U).value());
  return files;
}
std::set<std::string> entriesOf(const std::filesystem::path& directory) {
  std::set<std::string> names;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) names.insert(entry.path().filename().string());
  return names;
}
struct KeyFiles final {
  distribution::SigningKeyPair pair;
  std::string privateKey, publicKey;
};
KeyFiles makeKeys(const std::filesystem::path& root, const std::string& name) {
  const auto pair = distribution::generateSigningKeyPair();
  if (!pair) throw std::runtime_error("key generation failed");
  KeyFiles keys{pair.value(), (root / (name + "-private.json")).string(), (root / (name + "-public.json")).string()};
  if (!distribution::savePrivateKey(keys.pair, keys.privateKey) || !distribution::savePublicKey(keys.pair.publicKey, keys.publicKey))
    throw std::runtime_error("key persistence failed");
  return keys;
}
core::Result<authoring::HelperProcessOutput> runCli(std::vector<std::string> arguments) {
  return authoring::runBoundedHelperProcess({.executable = SEAM_TEST_VOICEBANK_CLI, .arguments = std::move(arguments)});
}
formats::JsonValue cliSuccess(std::vector<std::string> arguments) {
  const auto output = runCli(std::move(arguments));
  if (!output) throw std::runtime_error(output.error().message + ": " + output.error().context);
  const auto json = formats::parseJson(output.value().standardOutput);
  if (!json) throw std::runtime_error("CLI output is not JSON");
  return json.value();
}
std::string field(const formats::JsonValue& object, std::string_view key) { return object.find(key)->asString(); }
}  // namespace

TEST_CASE("CLI publishes a typed schema-2 candidate that lists every embedded file and refuses anything else") {
#if defined(__APPLE__) || defined(__linux__)
  CliFixture fixture;
  const auto captured = fixture.capture();
  const auto reviewed = fixture.success(fixture.reviewArgs(captured.find("fileSha256")->asString()));
  const auto candidate = fixture.root / "candidate";
  const auto published = fixture.success(publishArgs(fixture, reviewed.find("generation")->asString(),
      reviewed.find("projectSha256")->asString(), candidate));
  CHECK(published.find("schemaVersion")->asInt64() == 2);
  CHECK(published.find("qualification")->asString() == "NOT_QUALIFIED");
  CHECK(!published.find("signed")->asBool()); CHECK(!published.find("installed")->asBool());
  const auto verified = production::verifyResourceCandidateDirectory(candidate);
  if (!verified) throw std::runtime_error(verified.error().message + ": " + verified.error().context);
  const auto& descriptor = verified.value().descriptor;
  CHECK(verified.value().candidateSha256 == published.find("candidateSha256")->asString());
  CHECK(descriptor.schemaVersion == 2); CHECK(descriptor.declaresDependencySet());
  CHECK(descriptor.kind == production::ResourceCandidateKind::Sample);
  CHECK(descriptor.status == "REVIEWED_CANDIDATE");
  CHECK((descriptor.languages == std::vector<std::string>{"ja"}));
  CHECK((descriptor.styles == std::vector<std::string>{"original"}));
  CHECK(!descriptor.character.has_value());
  CHECK(descriptor.resourceId == "cli.published.fixture"); CHECK(descriptor.resourceVersion == "0.1.0");
  CHECK(descriptor.source.has_value());
  CHECK(std::to_string(descriptor.source->generation) == reviewed.find("generation")->asString());
  CHECK(descriptor.source->projectSha256 == reviewed.find("projectSha256")->asString());
  CHECK(descriptor.source->projectId == "cli-source");
  CHECK(descriptor.contentSha256 == published.find("contentSha256")->asString());
  CHECK(descriptor.manifestSha256 == published.find("manifestSha256")->asString());
  // Every file in the directory, other than the descriptor itself, is listed exactly once.
  std::set<std::string> listed, present, roles;
  for (const auto* list : {&descriptor.payload, &descriptor.evidence}) {
    for (const auto& file : *list) {
      CHECK(listed.insert(file.path).second);
      roles.insert(file.role);
      CHECK(core::sha256File(candidate / file.path, 64ULL * 1024ULL * 1024ULL).value() == file.sha256);
    }
  }
  for (const auto& entry : std::filesystem::recursive_directory_iterator(candidate))
    if (entry.is_regular_file()) present.insert(entry.path().lexically_relative(candidate).generic_string());
  CHECK(present.erase("candidate.json") == 1U);
  CHECK(present == listed);
  for (const auto role : {"manifest", "sample-audio", "source-license", "production-snapshot", "source-license-snapshot",
           "source-quality-evidence", "history-generation", "history-journal"})
    CHECK(roles.contains(role));
  // The unit's measured acoustic analysis is a listed dependency, so the package identity covers it.
  const auto analysisPath = voicebank::acousticAnalysisSidecarPath(fixture.manifest.units.front().id);
  CHECK(roles.contains("acoustic-analysis"));
  CHECK(listed.contains(analysisPath));
  const auto changedAnalysis = copyTree(candidate, fixture.root / "changed-analysis");
  auto analysisText = readText(changedAnalysis / analysisPath);
  analysisText.insert(analysisText.size() - 1U, " ");
  CHECK(core::durableAtomicWriteText(changedAnalysis / analysisPath, analysisText));
  CHECK(!production::verifyResourceCandidateDirectory(changedAnalysis));
  // A record for a unit the candidate does not bind is refused even when its bytes are listed.
  const auto stray = copyTree(candidate, fixture.root / "stray-analysis");
  const auto strayPath = "analysis/" + std::string(64U, '0') + ".json";
  std::filesystem::rename(stray / analysisPath, stray / strayPath);
  auto strayText = readText(stray / "candidate.json");
  CHECK(replaceOnce(strayText, "\"" + analysisPath + "\"", "\"" + strayPath + "\""));
  CHECK(core::durableAtomicWriteText(stray / "candidate.json", strayText));
  const auto strayVerified = production::verifyResourceCandidateDirectory(stray);
  CHECK(!strayVerified);
  if (!strayVerified) CHECK(strayVerified.error().message.find("no bound unit") != std::string::npos);
  // An unlisted file, changed audio, a missing snapshot or a relabelled claim is refused.
  const auto extra = copyTree(candidate, fixture.root / "extra");
  CHECK(core::durableAtomicWriteTextNew(extra / "notes.txt", "unlisted"));
  CHECK(!production::verifyResourceCandidateDirectory(extra));
  const auto changedAudio = copyTree(candidate, fixture.root / "changed-audio");
  const auto audioPath = changedAudio / ("audio/" + descriptor.unitBindings.front().find("audioSha256")->asString() + ".wav");
  auto audioBytes = core::readFileBytesLimited(audioPath, 64U * 1024U * 1024U).value();
  audioBytes.back() ^= std::byte{1};
  CHECK(core::durableAtomicWrite(audioPath, audioBytes));
  CHECK(!production::verifyResourceCandidateDirectory(changedAudio));
  const auto missing = copyTree(candidate, fixture.root / "missing");
  std::filesystem::rename(missing / "provenance/production.json", fixture.root / "moved-production.json");
  CHECK(!production::verifyResourceCandidateDirectory(missing));
  const auto original = readText(candidate / "candidate.json");
  for (const auto& [from, to] : std::vector<std::pair<std::string, std::string>>{
           {"\"releaseEligible\": false", "\"releaseEligible\": true"},
           {"\"qualification\": \"NOT_QUALIFIED\"", "\"qualification\": \"QUALIFIED\""},
           {"\"evidenceScope\": \"engineering\"", "\"evidenceScope\": \"production\""},
           {"\"status\": \"REVIEWED_CANDIDATE\"", "\"status\": \"DECLARED_CANDIDATE\""},
           {"\"format\":", "\"approved\": true, \"format\":"}}) {
    auto relabelled = original;
    CHECK(replaceOnce(relabelled, from, to));
    CHECK(!production::decodeResourceCandidateDescriptor(relabelled));
  }
  // Schema 1 is still read and checked against the files it names, reported as schema 1, and
  // never re-encoded as schema 2. It cannot prove the absence of unlisted files.
  formats::JsonValue::Object legacy{{"format", "com.project-seam.resource-candidate"}, {"schemaVersion", std::int64_t{1}},
      {"resourceKind", "sample"}, {"status", "REVIEWED_CANDIDATE"}, {"releaseEligible", false},
      {"evidenceScope", "engineering"}, {"sourceProjectSha256", descriptor.source->projectSha256},
      {"sourceGeneration", static_cast<std::int64_t>(descriptor.source->generation)},
      {"inventorySha256", descriptor.source->inventorySha256}, {"licenseSha256", descriptor.source->licenseSha256},
      {"manifestSha256", descriptor.manifestSha256}, {"contentSha256", descriptor.contentSha256},
      {"originHistory", descriptor.originHistory}, {"unitBindings", descriptor.unitBindings}};
  const auto legacyRoot = copyTree(candidate, fixture.root / "legacy");
  CHECK(core::durableAtomicWriteText(legacyRoot / "candidate.json", formats::stringifyJson(formats::JsonValue{legacy}, true) + "\n"));
  const auto legacyRead = production::verifyResourceCandidateDirectory(legacyRoot);
  CHECK(legacyRead);
  CHECK(legacyRead.value().descriptor.schemaVersion == 1);
  CHECK(!legacyRead.value().descriptor.declaresDependencySet());
  CHECK(legacyRead.value().descriptor.payload.empty());
  CHECK(!production::encodeResourceCandidateDescriptor(legacyRead.value().descriptor));
  CHECK(core::durableAtomicWriteTextNew(legacyRoot / "notes.txt", "schema 1 cannot see this"));
  CHECK(production::verifyResourceCandidateDirectory(legacyRoot));
  std::filesystem::rename(legacyRoot / "source-license.txt", fixture.root / "moved-license.txt");
  CHECK(!production::verifyResourceCandidateDirectory(legacyRoot));
#endif
}

TEST_CASE("CLI publishes one pinned generation only while its approvals remain in force") {
#if defined(__APPLE__) || defined(__linux__)
  CliFixture fixture;
  const auto captured = fixture.capture();
  const auto reviewed = fixture.success(fixture.reviewArgs(captured.find("fileSha256")->asString()));
  const auto generation = reviewed.find("generation")->asString();
  const auto reviewedHash = reviewed.find("projectSha256")->asString();
  // The captured generation predates the approval, so nothing of it can pair with the later review.
  CHECK(!fixture.run(publishArgs(fixture, captured.find("generation")->asString(),
      captured.find("projectSha256")->asString(), fixture.root / "early")));
  CHECK(!std::filesystem::exists(fixture.root / "early"));
  CHECK(!fixture.run(publishArgs(fixture, generation, std::string(64U, 'c'), fixture.root / "wrong-hash")));
  CHECK(!std::filesystem::exists(fixture.root / "wrong-hash"));
  // A later generation that does not reach the bound take leaves the pinned approval in force.
  const auto secondLicense = fixture.root / "second-license.txt";
  CHECK(core::durableAtomicWriteTextNew(secondLicense, "Second synthetic source, unrelated to take-a."));
  const auto current = fixture.repository.recover(); CHECK(current);
  const auto registered = fixture.success({"register-source", (fixture.root / "producer").string(),
      core::sha256Hex(production::encodeProductionProject(current.value())), "second-source", "procedural", "pass",
      "yes", "yes", "yes", "yes", secondLicense.string(), core::sha256File(secondLicense).value(), "producer",
      "2026-09-09T10:04:00Z"});
  CHECK(registered.find("result")->asString() == "SourceRegistered");
  const auto advanced = fixture.repository.recover(); CHECK(advanced);
  CHECK(std::to_string(advanced.value().lastDurableGeneration) != generation);
  CHECK(advanced.value().unitAssignments.front().state == production::UnitQueueState::Approved);
  const auto pinned = fixture.success(publishArgs(fixture, generation, reviewedHash, fixture.root / "pinned"));
  CHECK(pinned.find("sourceGeneration")->asString() == generation);
  const auto verified = production::verifyResourceCandidateDirectory(fixture.root / "pinned"); CHECK(verified);
  CHECK(std::to_string(verified.value().descriptor.source->generation) == generation);
  auto generationFile = generation;
  generationFile.insert(0U, 20U - generationFile.size(), '0');
  CHECK(readText(fixture.root / "pinned/provenance/production.json") ==
        readText(fixture.root / "producer/generations" / (generationFile + ".json")));
  // A reassessment of the bound source withdraws the approval: that generation no longer publishes.
  const auto inspected = fixture.success({"inspect-source-quality", (fixture.root / "producer").string(), "fixture"});
  CHECK(fixture.success({"record-source-quality", (fixture.root / "producer").string(), "fixture",
      inspected.find("projectSha256")->asString(), "quality-reassessment", "reviewer", "2026-09-09T10:05:00Z", "pass",
      "blocked", (fixture.root / "quality-evidence.txt").string(),
      core::sha256File(fixture.root / "quality-evidence.txt").value()}).find("result")->asString() ==
      "SourceQualityAssessmentCommitted");
  CHECK(!fixture.run(publishArgs(fixture, generation, reviewedHash, fixture.root / "withdrawn")));
  CHECK(!std::filesystem::exists(fixture.root / "withdrawn"));
  CHECK(production::verifyResourceCandidateDirectory(fixture.root / "pinned"));
#endif
}

TEST_CASE("U57 inputs stay a blocked template that no candidate reader accepts or writes into a candidate") {
#if defined(__APPLE__) || defined(__linux__)
  CliFixture fixture;
  const auto captured = fixture.capture();
  const auto reviewed = fixture.success(fixture.reviewArgs(captured.find("fileSha256")->asString()));
  const auto candidate = fixture.root / "candidate";
  CHECK(fixture.success(publishArgs(fixture, reviewed.find("generation")->asString(),
      reviewed.find("projectSha256")->asString(), candidate)).find("result")->asString() == "CandidateCommitted");
  const auto before = readText(candidate / "candidate.json");
  auto project = fixture.repository.recover().value();
  const auto generationBefore = project.lastDurableGeneration;
  CHECK(!fixture.repository.exportU57Inputs(project, candidate,
      {"candidate-export", project.projectId, "producer", "2026-09-09T10:06:00Z"}));
  CHECK(fixture.repository.recover().value().lastDurableGeneration == generationBefore);
  CHECK(!std::filesystem::exists(candidate / "candidate-template.json"));
  CHECK(readText(candidate / "candidate.json") == before);
  CHECK(production::verifyResourceCandidateDirectory(candidate));
  const auto exported = fixture.repository.exportU57Inputs(project, fixture.root / "u57",
      {"candidate-export", project.projectId, "producer", "2026-09-09T10:06:00Z"});
  CHECK(exported); CHECK(exported.value().status == "SYNTHETIC_READY_REAL_ASSETS_REQUIRED");
  const auto templateText = readText(exported.value().candidateTemplatePath);
  CHECK(!production::decodeResourceCandidateDescriptor(templateText));
  const auto parsed = formats::parseJson(templateText); CHECK(parsed);
  CHECK(parsed.value().find("status")->asString() == "BLOCKED");
  CHECK(parsed.value().find("format")->asString() != "com.project-seam.resource-candidate");
  CHECK(!production::verifyResourceCandidateDirectory(fixture.root / "u57"));
#endif
}

TEST_CASE("Pinned publication rechecks approvals under the producer lock after a concurrent withdrawal") {
#if defined(__APPLE__) || defined(__linux__)
  CliFixture fixture;
  const auto captured = fixture.capture();
  const auto reviewed = fixture.success(fixture.reviewArgs(captured.find("fileSha256")->asString()));
  bool withdrawn = false;
  production::CandidatePublicationOptions options;
  options.faultInjector = [&](production::CandidatePublicationStage stage) -> core::Result<void> {
    if (stage != production::CandidatePublicationStage::BeforeSourceLock || withdrawn) return core::success();
    withdrawn = true;
    const auto inspected = fixture.success({"inspect-source-quality", (fixture.root / "producer").string(), "fixture"});
    fixture.success({"record-source-quality", (fixture.root / "producer").string(), "fixture",
        inspected.find("projectSha256")->asString(), "quality-race", "reviewer", "2026-09-09T10:05:00Z", "pass",
        "blocked", (fixture.root / "quality-evidence.txt").string(),
        core::sha256File(fixture.root / "quality-evidence.txt").value()});
    return core::success();
  };
  const auto published = production::publishSampleCandidateFromGeneration(fixture.root / "producer",
      std::stoull(reviewed.find("generation")->asString()), reviewed.find("projectSha256")->asString(),
      fixture.manifest, fixture.root / "raced", options);
  CHECK(withdrawn);
  CHECK(!published);
  CHECK(!std::filesystem::exists(fixture.root / "raced"));
#endif
}

TEST_CASE("CLI packages and installs exactly the published candidate as distinct steps and a new song renders from it") {
#if defined(__APPLE__) || defined(__linux__)
  CliFixture fixture;
  const auto captured = fixture.capture();
  const auto reviewed = fixture.success(fixture.reviewArgs(field(captured, "fileSha256")));
  const auto candidate = fixture.root / "candidate";
  const auto published = fixture.success(publishArgs(fixture, field(reviewed, "generation"), field(reviewed, "projectSha256"), candidate));
  const auto candidateSha = field(published, "candidateSha256");
  const auto keys = makeKeys(fixture.root, "producer");
  const auto other = makeKeys(fixture.root, "other");
  const auto package = fixture.root / "candidate.seambank";
  // Packaging signs only the candidate whose digest publication reported, into a new file.
  CHECK(!fixture.run({"package-candidate", candidate.string(), std::string(64U, 'd'), package.string(), keys.privateKey}));
  CHECK(!std::filesystem::exists(package));
  const auto packaged = fixture.success({"package-candidate", candidate.string(), candidateSha, package.string(), keys.privateKey});
  CHECK(field(packaged, "result") == "PackageCommitted");
  CHECK(field(packaged, "candidateSha256") == candidateSha);
  CHECK(field(packaged, "contentSha256") == field(published, "contentSha256"));
  CHECK(field(packaged, "resourceKind") == "sample");
  const auto packageDigest = field(packaged, "packageDigest");
  CHECK(core::sha256File(package, 1ULL << 30U).value() == packageDigest);
  CHECK(!packaged.find("installed")->asBool()); CHECK(!packaged.find("releaseEligible")->asBool());
  CHECK(!fixture.run({"package-candidate", candidate.string(), candidateSha, package.string(), keys.privateKey}));
  CHECK(core::sha256File(package, 1ULL << 30U).value() == packageDigest);
  const auto verified = fixture.success({"verify-candidate-package", package.string(), keys.publicKey});
  CHECK(field(verified, "packageDigest") == packageDigest); CHECK(verified.find("signerTrusted")->asBool());
  CHECK(!fixture.run({"verify-candidate-package", package.string(), other.publicKey}));
  // A byte change anywhere in the package is refused by verification and installation alike.
  auto tamperedBytes = core::readFileBytesLimited(package, 1ULL << 30U).value();
  tamperedBytes[tamperedBytes.size() / 2U] ^= std::byte{1};
  const auto tampered = fixture.root / "tampered.seambank";
  CHECK(core::durableAtomicWriteNew(tampered, tamperedBytes));
  CHECK(!fixture.run({"verify-candidate-package", tampered.string(), keys.publicKey}));
  CHECK(!fixture.run({"install-candidate", tampered.string(), core::sha256Hex(tamperedBytes),
      (fixture.root / "installed").string(), keys.publicKey}));
  // Installation requires the exact digest and a trusted signer; refusals install nothing.
  CHECK(!fixture.run({"install-candidate", package.string(), std::string(64U, 'e'), (fixture.root / "installed").string(), keys.publicKey}));
  CHECK(!fixture.run({"install-candidate", package.string(), packageDigest, (fixture.root / "installed").string(), other.publicKey}));
  CHECK(!std::filesystem::exists(fixture.root / "installed/cli.published.fixture"));
  const auto installed = fixture.success({"install-candidate", package.string(), packageDigest,
      (fixture.root / "installed").string(), keys.publicKey});
  CHECK(field(installed, "result") == "InstallCommitted");
  CHECK(field(installed, "contentHash") == field(published, "contentSha256"));
  CHECK(field(installed, "candidateSha256") == candidateSha);
  CHECK(field(installed, "packageDigest") == packageDigest);
  CHECK(!installed.find("replacedExisting")->asBool()); CHECK(installed.find("durabilityConfirmed")->asBool());
  const std::filesystem::path installDirectory{field(installed, "installDirectory")};
  CHECK(core::sha256File(installDirectory / "candidate.json").value() == candidateSha);
  const auto installedTree = treeDigest(installDirectory);
  CHECK(!fixture.run({"install-candidate", package.string(), packageDigest, (fixture.root / "installed").string(), keys.publicKey}));
  CHECK(treeDigest(installDirectory) == installedTree);
  // A new song resolves and renders the installed bank with the producer and candidate unavailable.
  std::filesystem::rename(fixture.root / "producer", fixture.root / "producer-unavailable");
  std::filesystem::rename(candidate, fixture.root / "candidate-unavailable");
  application::ProjectFactory factory{989100U};
  auto song = factory.createProject("New melody from the installed typed candidate");
  const auto track = factory.addVocalTrack(song, "Singer");
  const auto region = factory.addRegion(song, track, "New melody", time::Tick{0}, time::Tick{1920});
  for (int index = 0; index < 2; ++index) {
    auto [lyric, note] = factory.makeNote(time::Tick{960 * index}, time::Tick{960}, static_cast<std::uint8_t>(69 + 3 * index), U"あ",
                                          domain::Language::Japanese);
    song.findRegion(region)->lyrics.push_back(lyric);
    song.findRegion(region)->notes.push_back(note);
  }
  song.findVocalTrack(track)->voicebank = {fixture.manifest.id, fixture.manifest.version, field(installed, "contentHash")};
  song.findVocalTrack(track)->styleSelection = {domain::VoiceStyleOrigin::Explicit, "original"};
  CHECK(formats::ProjectJsonCodec{}.save(song, fixture.root / "song.seam"));
  const auto reopened = formats::ProjectJsonCodec{}.load(fixture.root / "song.seam");
  CHECK(reopened);
  authoring::VoicebankSession session({{fixture.root / "installed", voicebank::VoicebankRootKind::Installed}}, false);
  CHECK(session.refresh());
  const auto resolved = session.resolveTrack(reopened.value(), track);
  CHECK(resolved.resolved());
  if (!resolved.resolved()) return;
  CHECK(resolved.candidate->trust == voicebank::VoicebankTrust::TrustedInstalled);
  const auto& bank = *resolved.candidate;
  const std::vector<rendering::TrackVoicebankSource> sources{{track, bank.manifest, bank.bankRoot, bank.contentHash, bank.trust}};
  const auto rendered = rendering::ProductionProjectRenderer{}.render(reopened.value(), sources, track, region, 1U, 48000U,
                                                                     rendering::RenderQuality::Final);
  CHECK(rendered);
  if (!rendered) return;
  CHECK(rendered.value().diagnostics.empty()); CHECK(rendered.value().fallbackCount == 0U);
  CHECK(voicebank::analyzeAudio(std::span<const float>{rendered.value().interleaved.data(), rendered.value().interleaved.size()}).rms > 1e-4);
#endif
}

TEST_CASE("Typed package verification refuses unlisted entries, schema-1 candidates and content that differs from the descriptor") {
#if defined(__APPLE__) || defined(__linux__)
  CliFixture fixture;
  const auto captured = fixture.capture();
  const auto reviewed = fixture.success(fixture.reviewArgs(field(captured, "fileSha256")));
  const auto candidate = fixture.root / "candidate";
  CHECK(fixture.success(publishArgs(fixture, field(reviewed, "generation"), field(reviewed, "projectSha256"), candidate))
            .find("result")->asString() == "CandidateCommitted");
  const auto keys = makeKeys(fixture.root, "producer");
  const distribution::VerifySeambankOptions trusted{.trustedPublicKeys = {keys.pair.publicKey}, .requireTrustedSigner = true};
  // A package signed over an extra, unlisted file is not this candidate.
  const auto extra = copyTree(candidate, fixture.root / "extra");
  CHECK(core::durableAtomicWriteTextNew(extra / "notes.txt", "unlisted"));
  CHECK(distribution::packSeambank(extra, fixture.root / "extra.seambank", keys.pair));
  CHECK(!candidate_packaging::verifyResourceCandidatePackage(fixture.root / "extra.seambank", trusted));
  CHECK(!candidate_packaging::packageResourceCandidate(extra, fixture.root / "extra-typed.seambank", keys.pair));
  CHECK(!std::filesystem::exists(fixture.root / "extra-typed.seambank"));
  // A descriptor whose content identity differs from the audio it lists never installs.
  const auto relabelled = copyTree(candidate, fixture.root / "relabelled");
  const auto descriptor = production::verifyResourceCandidateDirectory(candidate).value().descriptor;
  auto text = readText(relabelled / "candidate.json");
  CHECK(replaceOnce(text, "\"contentSha256\": \"" + descriptor.contentSha256 + "\"", "\"contentSha256\": \"" + std::string(64U, 'f') + "\""));
  CHECK(core::durableAtomicWriteText(relabelled / "candidate.json", text));
  CHECK(!production::verifyResourceCandidateDirectory(relabelled));
  CHECK(distribution::packSeambank(relabelled, fixture.root / "relabelled.seambank", keys.pair));
  // Its signature and entry listing are valid; only the derived content identity exposes the relabelling.
  CHECK(distribution::verifySeambank(fixture.root / "relabelled.seambank", trusted));
  CHECK(!candidate_packaging::verifyResourceCandidatePackage(fixture.root / "relabelled.seambank", trusted));
  CHECK(!fixture.run({"verify-candidate-package", (fixture.root / "relabelled.seambank").string(), keys.publicKey}));
  candidate_packaging::InstallCandidateOptions install;
  install.verification = trusted;
  install.expectedPackageDigest = core::sha256File(fixture.root / "relabelled.seambank", 1ULL << 30U).value();
  CHECK(!candidate_packaging::installResourceCandidatePackage(fixture.root / "relabelled.seambank", fixture.root / "installed", install));
  CHECK(!std::filesystem::exists(fixture.root / "installed/cli.published.fixture"));
  // Schema 1 cannot be packaged as a typed candidate: it never declared its dependency set.
  const auto legacy = copyTree(candidate, fixture.root / "legacy");
  formats::JsonValue::Object schema1{{"format", "com.project-seam.resource-candidate"}, {"schemaVersion", std::int64_t{1}},
      {"resourceKind", "sample"}, {"status", "REVIEWED_CANDIDATE"}, {"releaseEligible", false},
      {"evidenceScope", "engineering"}, {"sourceProjectSha256", descriptor.source->projectSha256},
      {"sourceGeneration", static_cast<std::int64_t>(descriptor.source->generation)},
      {"inventorySha256", descriptor.source->inventorySha256}, {"licenseSha256", descriptor.source->licenseSha256},
      {"manifestSha256", descriptor.manifestSha256}, {"contentSha256", descriptor.contentSha256},
      {"originHistory", descriptor.originHistory}, {"unitBindings", descriptor.unitBindings}};
  CHECK(core::durableAtomicWriteText(legacy / "candidate.json", formats::stringifyJson(formats::JsonValue{schema1}, true) + "\n"));
  const auto inspected = fixture.success({"inspect-candidate", legacy.string()});
  CHECK(inspected.find("schemaVersion")->asInt64() == 1); CHECK(!inspected.find("dependencySetDeclared")->asBool());
  CHECK(!candidate_packaging::packageResourceCandidate(legacy, fixture.root / "legacy.seambank", keys.pair));
  CHECK(!std::filesystem::exists(fixture.root / "legacy.seambank"));
#endif
}

TEST_CASE("Interrupted publication or packaging leaves the previous candidate and package intact") {
#if defined(__APPLE__) || defined(__linux__)
  CliFixture fixture;
  const auto captured = fixture.capture();
  const auto reviewed = fixture.success(fixture.reviewArgs(field(captured, "fileSha256")));
  const auto generation = std::stoull(field(reviewed, "generation"));
  const auto first = fixture.root / "candidate-a";
  CHECK(fixture.success(publishArgs(fixture, field(reviewed, "generation"), field(reviewed, "projectSha256"), first))
            .find("result")->asString() == "CandidateCommitted");
  const auto keys = makeKeys(fixture.root, "producer");
  const auto firstPackage = candidate_packaging::packageResourceCandidate(first, fixture.root / "a.seambank", keys.pair);
  CHECK(firstPackage);
  const auto candidateBefore = treeDigest(first);
  const auto packageBefore = core::sha256File(fixture.root / "a.seambank", 1ULL << 30U).value();
  const auto parentBefore = entriesOf(fixture.root);
  const auto intact = [&] {
    return treeDigest(first) == candidateBefore && entriesOf(fixture.root) == parentBefore &&
           core::sha256File(fixture.root / "a.seambank", 1ULL << 30U).value() == packageBefore;
  };
  for (const auto stage : {production::CandidatePublicationStage::AudioStaged, production::CandidatePublicationStage::BeforeCommit}) {
    production::CandidatePublicationOptions options;
    options.faultInjector = [stage](production::CandidatePublicationStage current) -> core::Result<void> {
      return current == stage ? core::failure(core::ErrorCode::IoError, "No space left on device (simulated disk exhaustion)")
                              : core::success();
    };
    CHECK(!production::publishSampleCandidateFromGeneration(fixture.root / "producer", generation, field(reviewed, "projectSha256"),
                                                           fixture.manifest, fixture.root / "candidate-b", options));
    CHECK(intact());
  }
  std::stop_source cancel;
  production::CandidatePublicationOptions cancelling;
  cancelling.faultInjector = [&cancel](production::CandidatePublicationStage current) -> core::Result<void> {
    if (current == production::CandidatePublicationStage::AudioStaged) cancel.request_stop();
    return core::success();
  };
  CHECK(!production::publishSampleCandidateFromGeneration(fixture.root / "producer", generation, field(reviewed, "projectSha256"),
                                                         fixture.manifest, fixture.root / "candidate-b", cancelling, cancel.get_token()));
  CHECK(intact());
  std::stop_source stopped;
  stopped.request_stop();
  CHECK(!candidate_packaging::packageResourceCandidate(first, fixture.root / "b.seambank", keys.pair, {}, stopped.get_token()));
  CHECK(!candidate_packaging::packageResourceCandidate(first, fixture.root / "a.seambank", keys.pair));
  CHECK(intact());
#endif
}

TEST_CASE("Recipe and model contract fixtures travel through typed packaging without any qualification claim") {
#if defined(__APPLE__) || defined(__linux__)
  const auto root = test::support::temporaryDirectory("typed-declared-candidates");
  const auto keys = makeKeys(root, "producer");
  // Recipe: the Designer's recipe with its derived procedural manifest; installs as a procedural singer.
  const auto recipe = cliSuccess({"publish-recipe-candidate",
      (std::filesystem::path{SEAM_TEST_SOURCE_DIR} / "assets/pilots/seam-song-01/recipe.json").string(), "1.0.0", "ja",
      (root / "recipe-candidate").string()});
  CHECK(field(recipe, "status") == "DECLARED_CANDIDATE"); CHECK(field(recipe, "qualification") == "NOT_QUALIFIED");
  CHECK(!recipe.find("reviewed")->asBool());
  const auto recipeInspected = cliSuccess({"inspect-candidate", (root / "recipe-candidate").string()});
  CHECK(field(recipeInspected, "resourceKind") == "recipe");
  CHECK(recipeInspected.find("languages")->asArray().front().asString() == "ja");
  CHECK(!recipeInspected.find("sourceGeneration"));
  CHECK(recipeInspected.find("externalDependencies")->asArray().front().find("kind")->asString() == "render-engine");
  const auto recipeDescriptor = production::verifyResourceCandidateDirectory(root / "recipe-candidate");
  CHECK(recipeDescriptor); CHECK(!recipeDescriptor.value().descriptor.source.has_value());
  CHECK(recipeDescriptor.value().descriptor.evidence.empty());
  const auto recipePackage = cliSuccess({"package-candidate", (root / "recipe-candidate").string(), field(recipe, "candidateSha256"),
      (root / "recipe.seamsinger").string(), keys.privateKey});
  CHECK(field(recipePackage, "resourceKind") == "recipe");
  const auto recipeInstalled = cliSuccess({"install-candidate", (root / "recipe.seamsinger").string(),
      field(recipePackage, "packageDigest"), (root / "singers").string(), keys.publicKey});
  CHECK(field(recipeInstalled, "resourceKind") == "recipe");
  const std::filesystem::path singerDirectory{field(recipeInstalled, "installDirectory")};
  CHECK(readText(singerDirectory / "install-receipt.json").find("procedural-singer") != std::string::npos);
  CHECK(core::sha256File(singerDirectory / "candidate.json").value() == field(recipe, "candidateSha256"));
  // A recipe descriptor that relabels the manifest's language is refused at packaging.
  const auto relabelled = copyTree(root / "recipe-candidate", root / "recipe-relabelled");
  auto text = readText(relabelled / "candidate.json");
  CHECK(replaceOnce(text, "\"ja\"", "\"en\""));
  CHECK(core::durableAtomicWriteText(relabelled / "candidate.json", text));
  const auto relabelledSha = core::sha256Hex(text);
  CHECK(!runCli({"package-candidate", relabelled.string(), relabelledSha, (root / "relabelled.seamsinger").string(), keys.privateKey}));
  CHECK(!std::filesystem::exists(root / "relabelled.seamsinger"));
  // Model: a contract payload, not a trained or qualified model. It packages and verifies but never installs.
  const auto payload = root / "model-payload";
  std::filesystem::create_directories(payload / "graphs");
  CHECK(core::durableAtomicWriteTextNew(payload / "manifest.json",
      "{\"modelId\":\"fixture.model\",\"modelVersion\":\"0.0.1\",\"note\":\"contract fixture; not a trained model\"}\n"));
  CHECK(core::durableAtomicWriteTextNew(payload / "graphs/acoustic.bin", std::string(4096U, '\x5a')));
  CHECK(core::durableAtomicWriteTextNew(payload / "vocabulary.json", "{\"phones\":[\"a\",\"i\"]}\n"));
  const auto model = cliSuccess({"publish-model-candidate", payload.string(), "ja,en", "neutral", "seam-neural-worker", "1",
      (root / "model-candidate").string(), "Contract fixture model"});
  CHECK(field(model, "status") == "DECLARED_CANDIDATE"); CHECK(field(model, "resourceKind") == "model");
  const auto modelDescriptor = production::verifyResourceCandidateDirectory(root / "model-candidate");
  CHECK(modelDescriptor);
  if (modelDescriptor) {
    CHECK((modelDescriptor.value().descriptor.languages == std::vector<std::string>{"ja", "en"}));
    CHECK(modelDescriptor.value().descriptor.contentSha256 ==
          production::modelCandidateContentSha256(modelDescriptor.value().descriptor.payload));
  }
  const auto modelPackage = cliSuccess({"package-candidate", (root / "model-candidate").string(), field(model, "candidateSha256"),
      (root / "model.seampkg.bin").string(), keys.privateKey});
  CHECK(field(modelPackage, "resourceKind") == "model");
  CHECK(cliSuccess({"verify-candidate-package", (root / "model.seampkg.bin").string(), keys.publicKey}).find("result")->asString() ==
        "PackageVerified");
  CHECK(!runCli({"install-candidate", (root / "model.seampkg.bin").string(), field(modelPackage, "packageDigest"),
      (root / "models").string(), keys.publicKey}));
  CHECK(!std::filesystem::exists(root / "models/fixture.model"));
  // A declaration can never produce a sample candidate: those come only from a reviewed producer generation.
  production::DeclaredResourceCandidateRequest declaredSample;
  declaredSample.kind = production::ResourceCandidateKind::Sample;
  declaredSample.resourceId = "fixture.model"; declaredSample.resourceVersion = "0.0.1"; declaredSample.displayName = "Declared sample";
  declaredSample.languages = {"ja"}; declaredSample.styles = {"neutral"};
  declaredSample.roles = {{"graphs/acoustic.bin", "sample-audio"}, {"manifest.json", "manifest"}, {"vocabulary.json", "model-data"}};
  CHECK(!production::publishDeclaredResourceCandidate(payload, declaredSample, root / "declared-sample"));
  CHECK(!std::filesystem::exists(root / "declared-sample"));
  // Real model graphs (.onnx) are outside the signed container's asset allowlist, so they are refused.
  CHECK(core::durableAtomicWriteTextNew(payload / "graphs/acoustic.onnx", "not packageable"));
  CHECK(!runCli({"publish-model-candidate", payload.string(), "ja", "neutral", "seam-neural-worker", "1",
      (root / "model-onnx").string()}));
  CHECK(!std::filesystem::exists(root / "model-onnx"));
#endif
}
