#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/authoring/export_service.hpp"
#include "seam/authoring/voicebank_installer_service.hpp"
#include "seam/distribution/seambank.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/authoring/generation_job.hpp"
#include "seam/authoring/inventory_generation.hpp"
#include "seam/authoring/generation_campaign.hpp"
#include "seam/authoring/inventory_preflight.hpp"
#include "seam/authoring/generation_batch_collection.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/rendering/streaming_pcm_source.hpp"
#include "seam/rendering/audio_level_envelope.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/rendering/render_pipeline.hpp"
#include "seam/voice_design/procedural_candidate.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/core/sha256.hpp"
#include "seam/core/exclusive_file_lock.hpp"
#include "seam/voicebank_production/repository.hpp"
#include "seam/voicebank_production/candidate_markers.hpp"
#include "seam/native_ui/voicebank_studio.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/candidate_audition.hpp"
#include "seam/native_ui/candidate_audition_session.hpp"
#include "seam/platform/audio_device.hpp"

#include <filesystem>
#include <fstream>
#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <thread>
#include <limits>
#if defined(SEAM_TEST_VOICEBANK_CLI) && (defined(__APPLE__) || defined(__linux__))
#include <spawn.h>
#include <sys/wait.h>
#include <cerrno>
extern char** environ;
#endif

namespace {
#if defined(SEAM_TEST_VOICEBANK_CLI) && (defined(__APPLE__) || defined(__linux__))
int runProcess(const char* executable, std::vector<std::string> arguments) {
  arguments.insert(arguments.begin(), executable);
  std::vector<char*> argv;
  for (auto& argument : arguments) argv.push_back(argument.data());
  argv.push_back(nullptr);
  pid_t process{};
  if (posix_spawn(&process, executable, nullptr, nullptr, argv.data(), environ) != 0) return -1;
  int status = 0;
  pid_t waited;
  do { waited = waitpid(process, &status, 0); } while (waited < 0 && errno == EINTR);
  if (waited != process) return -1;
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1;
}
int runVoicebankCli(std::vector<std::string> arguments) {
  return runProcess(SEAM_TEST_VOICEBANK_CLI, std::move(arguments));
}
#endif

seam::domain::Project exportProject(const std::filesystem::path& root,
                                    std::uint64_t firstId) {
  const auto media = root / "backing.wav";
  if (!seam::voicebank::writeMonoPcm16Wav(
          media, 48000U,
          seam::test::support::sineWave(48000U, 220.0, 0.05))) {
    throw seam::test::Failure{"unable to create export fixture WAV"};
  }
  auto source = seam::rendering::StreamingPcmSource::open(media, 4096U);
  if (!source) throw seam::test::Failure{"unable to open export fixture WAV"};
  seam::application::ProjectFactory factory{firstId};
  auto project = factory.createProject("Export transaction");
  project.audioTracks().push_back(seam::domain::AudioTrack{
      .id = factory.nextTrackId(),
      .name = "Backing",
      .mediaPath = media.string(),
      .mediaHash = source.value()->info().contentHash,
      .mediaOwnership = seam::domain::MediaOwnership::ExternalReference,
      .originalFilename = media.filename().string(),
      .sourceSampleRate = source.value()->info().sampleRate,
      .sourceChannels = source.value()->info().channels,
      .sourceFrameCount = source.value()->info().frameCount,
      .startTick = seam::time::Tick{0},
      .outputRoute = seam::domain::TrackOutputRoute{
          .bus = seam::domain::BusId{1U},
          .matrix = seam::domain::RoutingMatrix::monoToStereo(),
      },
  });
  if (!project.validate()) {
    throw seam::test::Failure{"invalid export fixture project"};
  }
  return project;
}

seam::authoring::ExportSettings exportSettings(bool stems = true) {
  return seam::authoring::ExportSettings{
      .sampleRate = 48000U,
      .channels = 2U,
      .format = seam::voicebank::WavSampleFormat::Pcm16,
      .includeMaster = true,
      .includeStems = stems,
      .replaceExisting = true,
  };
}

std::int64_t committedRevision(const std::filesystem::path& destination) {
  std::ifstream input{destination / "receipt.json", std::ios::binary};
  const std::string text{std::istreambuf_iterator<char>{input},
                         std::istreambuf_iterator<char>{}};
  const auto parsed = seam::formats::parseJson(text);
  const auto* revision = parsed && parsed.value().isObject()
                             ? parsed.value().find("projectRevision")
                             : nullptr;
  if (revision == nullptr || !revision->isInteger()) {
    throw seam::test::Failure{"export receipt has no committed revision"};
  }
  return revision->asInt64();
}

}

TEST_CASE("released English stop coda bakes with a typed candidate and an inferred closure") {
  using namespace seam;
  for (const bool frication : {false,true}) {
  const auto root=test::support::temporaryDirectory("released-coda-export");
  voice_design::VoiceRecipe recipe; recipe.id="released-coda";
  recipe.poses={{"aa1","neutral",0.0,{{700.0,80.0,0.0},{1200.0,100.0,-3.0},{2600.0,140.0,-6.0}}}};
  if (frication) recipe.frications={{"s","neutral",{.seed=42U}}};
  else recipe.plosives={{"k","neutral",{.seed=42U},10.0}};
  const auto resource=voice_design::freezeVoiceRecipeResource(recipe); CHECK(resource);
  application::ProjectFactory factory{88000U}; auto project=factory.createProject("Released coda");
  const auto track=factory.addVocalTrack(project,"Singer");
  const auto region=factory.addRegion(project,track,"Phrase",time::Tick{0},time::Tick{960});
  auto [lyric,note]=factory.makeNote(time::Tick{0},time::Tick{960},69U,U"ack",domain::Language::English);
  note.phoneticHint=frication?"aa1 s":"aa1 k";
  project.findRegion(region)->lyrics.push_back(lyric); project.findRegion(region)->notes.push_back(note);
  const auto snapshot=rendering::RenderSnapshotFactory{}.createProcedural(project,resource.value(),track,region,1U,rendering::RenderQuality::Final,48000U); CHECK(snapshot);
  CHECK(snapshot.value().phonemes->tokens.back().role==domain::PhonemeRole::Coda);
  const auto expected=rendering::PhraseRenderPipeline{}.render(snapshot.value()); CHECK(expected);
  authoring::ExportSettings settings; settings.includeMaster=false; settings.includeProceduralCandidates=true;
  const std::vector<rendering::TrackSingerSource> sources{rendering::TrackProceduralSource{track,resource.value(),"neutral"}};
  CHECK(authoring::ExportService{}.exportSetWithSources(project,sources,track,region,1U,root/"baked",settings));
  const auto prefix=root/"baked/candidates"/(track.toString()+"-"+region.toString());
  const auto candidate=voice_design::loadProceduralCandidate(prefix.string()+".json",prefix.string()+".wav",resource.value()); CHECK(candidate);
  CHECK(candidate.value().schemaVersion==(frication?2U:4U)); CHECK(candidate.value().markers.size()==2U);
  CHECK(candidate.value().markers.back().kind==(frication?voice_design::ProceduralGestureKind::Frication:voice_design::ProceduralGestureKind::Plosive));
  CHECK(candidate.value().markers.back().ownedSpan.start==21120); CHECK(candidate.value().markers.back().ownedSpan.end==24000);
  CHECK(candidate.value().audio->interleaved==expected.value().rendered.audio.samples);
  const auto& pcm=candidate.value().audio->interleaved;
  if (frication) CHECK(std::any_of(pcm.begin()+21120,pcm.begin()+23520,[](float sample){return sample!=0.0F;}));
  else CHECK(std::all_of(pcm.begin()+21120,pcm.begin()+23520,[](float sample){return sample==0.0F;}));
  CHECK(std::any_of(pcm.begin()+23520,pcm.end(),[](float sample){return sample!=0.0F;}));
  CHECK(pcm.back()==0.0F);
  }
}

TEST_CASE("multilingual vowels and nasal consonants bake and import with their distinct marker contracts") {
  using namespace seam;
  struct Case { domain::Language language; std::u32string lyric; std::string hint,phone; };
  const std::array cases{Case{domain::Language::English,U"a","ah1","ah1"},
      Case{domain::Language::Korean,U"어",{},"eo"},Case{domain::Language::Korean,U"으",{},"eu"},
      Case{domain::Language::Japanese,U"ま",{},"m"},Case{domain::Language::Japanese,U"ん",{},"N"},
      Case{domain::Language::Japanese,U"あん",{},"a"},Case{domain::Language::Japanese,U"か",{},"k"},
      Case{domain::Language::Japanese,U"ば",{},"b"},
      Case{domain::Language::English,U"za","z aa1","z"},
      Case{domain::Language::English,U"az","aa1 z","aa1"}};
  for (const auto& item:cases) {
    const auto root=test::support::temporaryDirectory("multilingual-procedural-bank");
    voice_design::VoiceRecipe recipe; recipe.id="language-bank";
    recipe.poses={{item.phone,"neutral",0.5,{{700.0,80.0,0.0},{1200.0,100.0,-3.0},{2600.0,140.0,-6.0}},voice_design::NasalResonance{}}};
    const bool syllabic=item.phone=="N", coda=item.lyric==U"あん", nasal=item.phone=="m" || syllabic || coda;
    const bool voicedStop=item.phone=="b";
    const bool plosive=item.phone=="k" || voicedStop;
    const bool voicedCoda=item.hint=="aa1 z";
    const bool voicedFrication=item.phone=="z" || voicedCoda;
    if (voicedFrication) {
      recipe.poses.front().nasal.reset(); recipe.poses.front().nasalCoupling=0.0;
      auto other=recipe.poses.front(); other.phone=voicedCoda?"z":"aa1"; recipe.poses.push_back(other);
      recipe.frications={{"z","neutral",{.seed=42U},0.3}};
    }
    if (plosive) {
      recipe.poses.front().phone="a";
      recipe.plosives={{item.phone,"neutral",{.seed=42U},10.0}};
      if (voicedStop) recipe.plosives.front().voicedClosure=voice_design::VoiceRecipe::VoicedClosure{0.2,400.0};
    }
    if (coda) { auto tail=recipe.poses.front(); tail.phone="N"; recipe.poses.push_back(tail); }
    else if (nasal && !syllabic) { auto vowel=recipe.poses.front(); vowel.phone="a"; vowel.nasalCoupling=0.0; recipe.poses.push_back(vowel); }
    const auto coverage=voicedStop?std::string{"cv:ba"}:voicedCoda?std::string{"vc:aa1z"}:voicedFrication?std::string{"cv:za"}:plosive?std::string{"cv:ka"}:syllabic?std::string{"special:N"}:coda?std::string{"vc:aN"}:nasal?std::string{"cv:ma"}:"sustain:"+item.phone;
    const auto resource=voice_design::freezeVoiceRecipeResource(recipe); CHECK(resource);
    application::ProjectFactory factory{87000U}; auto project=factory.createProject("Language bake");
    const auto track=factory.addVocalTrack(project,"Singer");
    const auto region=factory.addRegion(project,track,"Phrase",time::Tick{0},time::Tick{960});
    auto [lyric,note]=factory.makeNote(time::Tick{0},time::Tick{960},69U,item.lyric,item.language);
    if (!item.hint.empty()) note.phoneticHint=item.hint;
    project.findRegion(region)->lyrics.push_back(lyric); project.findRegion(region)->notes.push_back(note);
    const auto snapshot=rendering::RenderSnapshotFactory{}.createProcedural(project,resource.value(),track,region,1U,rendering::RenderQuality::Final,48000U); CHECK(snapshot);
    CHECK(snapshot.value().phonemes->tokens.front().symbol==item.phone);
    CHECK(voice_design::requiresArticulation(snapshot.value().phonemes->tokens)==(nasal||plosive||voicedFrication));
    const auto rendered=rendering::PhraseRenderPipeline{}.render(snapshot.value()); CHECK(rendered);
    authoring::ExportSettings settings; settings.includeMaster=false; settings.includeProceduralCandidates=true;
    const std::vector<rendering::TrackSingerSource> sources{rendering::TrackProceduralSource{track,resource.value(),"neutral"}};
    const auto baked=authoring::ExportService{}.exportSetWithSources(project,sources,track,region,1U,root/"baked",settings); CHECK(baked);
    const auto prefix=root/"baked/candidates"/(track.toString()+"-"+region.toString());
    const auto loaded=voice_design::loadProceduralCandidate(prefix.string()+".json",prefix.string()+".wav",resource.value());
    if (!loaded) throw test::Failure{item.phone+": "+loaded.error().message+" / "+loaded.error().context};
    CHECK(loaded.value().schemaVersion==(voicedStop?6U:voicedFrication?5U:plosive?4U:nasal?3U:1U));
    CHECK(loaded.value().markers.size()==((nasal && !syllabic)||plosive||voicedFrication?2U:1U)); CHECK(loaded.value().markers.front().phone==item.phone);
    if (voicedFrication) {
      const auto& marker=loaded.value().markers[voicedCoda?1U:0U];
      CHECK(marker.kind==voice_design::ProceduralGestureKind::VoicedFrication); CHECK(marker.phone=="z");
      if (voicedCoda) {
        CHECK(marker.ownedSpan.start==21120); CHECK(marker.ownedSpan.end==24000);
        CHECK(loaded.value().markers.front().kind==voice_design::ProceduralGestureKind::OralVowel);
        const auto& samples=loaded.value().audio->interleaved;
        CHECK(std::any_of(samples.begin()+21360,samples.begin()+23760,[](float value){return std::abs(value)>0.00001F;}));
      }
    }
    if (plosive) {
      CHECK(loaded.value().markers.front().kind==(voicedStop?voice_design::ProceduralGestureKind::VoicedPlosive:voice_design::ProceduralGestureKind::Plosive));
      CHECK(loaded.value().markers.front().ownedSpan.end==2880);
      const auto& pcm=loaded.value().audio->interleaved;
      if (voicedStop) CHECK(std::any_of(pcm.begin(),pcm.begin()+2400,[](float sample){return std::abs(sample)>0.00001F;}));
      else CHECK(std::all_of(pcm.begin(),pcm.begin()+2400,[](float sample){return sample==0.0F;}));
      CHECK(std::any_of(pcm.begin()+2400,pcm.begin()+2880,[](float sample){return sample!=0.0F;}));
    }
    if (nasal) {
      CHECK(loaded.value().markers.front().kind==(coda?voice_design::ProceduralGestureKind::OralVowel:voice_design::ProceduralGestureKind::Nasal));
      CHECK(loaded.value().markers.front().ownedSpan.end==(syllabic?24000:coda?21120:2880));
      if (!syllabic) CHECK(loaded.value().markers.back().kind==(coda?voice_design::ProceduralGestureKind::Nasal:voice_design::ProceduralGestureKind::OralVowel));
    }
    CHECK(loaded.value().audio->interleaved==rendered.value().rendered.audio.samples);
    const auto metadata=core::readTextFileLimited(prefix.string()+".json",4U*1024U*1024U); CHECK(metadata);
    if (plosive) {
      CHECK(loaded.value().plosiveRevision==voice_design::PlosiveSource::algorithmRevision);
      auto invalid=formats::parseJson(metadata.value()).value();
      *invalid.find("plosiveRevision")=formats::JsonValue{std::int64_t{0}};
      CHECK(!voice_design::parseProceduralCandidateMetadata(formats::stringifyJson(invalid),resource.value()));
      invalid=formats::parseJson(metadata.value()).value();
      invalid.asObject().erase("plosiveRevision");
      *invalid.find("schemaVersion")=formats::JsonValue{std::int64_t{3}};
      CHECK(!voice_design::parseProceduralCandidateMetadata(formats::stringifyJson(invalid),resource.value()));
      invalid=formats::parseJson(metadata.value()).value();
      *invalid.find("markers")->asArray().front().find("kind")=formats::JsonValue{"frication"};
      CHECK(!voice_design::parseProceduralCandidateMetadata(formats::stringifyJson(invalid),resource.value()));
      invalid=formats::parseJson(metadata.value()).value();
      *invalid.find("markers")->asArray().front().find("endFrame")=formats::JsonValue{std::int64_t{480}};
      CHECK(!voice_design::parseProceduralCandidateMetadata(formats::stringifyJson(invalid),resource.value()));
    }
    if (syllabic) {
      auto split=formats::parseJson(metadata.value()).value();
      auto& entries=split.find("markers")->asArray(); auto second=entries.front();
      const auto key=second.find("key")->asString();
      *second.find("key")=formats::JsonValue{key.substr(0U,key.find(':'))+":1"};
      *second.find("startFrame")=formats::JsonValue{std::int64_t{12000}};
      *entries.front().find("endFrame")=formats::JsonValue{std::int64_t{12000}};
      entries.push_back(std::move(second));
      CHECK(!voice_design::parseProceduralCandidateMetadata(formats::stringifyJson(split),resource.value()));
    }
    auto malformed=formats::parseJson(metadata.value()).value();
    *malformed.find("markers")->asArray().front().find("phone")=formats::JsonValue{"r"};
    CHECK(!voice_design::parseProceduralCandidateMetadata(formats::stringifyJson(malformed),resource.value()));
    if (nasal || plosive) {
      malformed=formats::parseJson(metadata.value()).value(); *malformed.find("schemaVersion")=formats::JsonValue{std::int64_t{2}};
      CHECK(!voice_design::parseProceduralCandidateMetadata(formats::stringifyJson(malformed),resource.value()));
      malformed=formats::parseJson(metadata.value()).value();
      *malformed.find("markers")->asArray()[coda?1U:0U].find("kind")=formats::JsonValue{"oral-vowel"};
      CHECK(!voice_design::parseProceduralCandidateMetadata(formats::stringifyJson(malformed),resource.value()));
    }
    auto unsafe=recipe; unsafe.poses.front().formants.back().frequencyHz=10000.0;
    const auto unsafeResource=voice_design::freezeVoiceRecipeResource(unsafe); CHECK(unsafeResource);
    malformed=formats::parseJson(metadata.value()).value();
    *malformed.find("recipeHash")=formats::JsonValue{unsafeResource.value().identity.contentHash};
    *malformed.find("sampleRate")=formats::JsonValue{std::int64_t{8000}};
    CHECK(!voice_design::parseProceduralCandidateMetadata(formats::stringifyJson(malformed),unsafeResource.value()));
    const auto license=root/"synthetic-license.txt"; CHECK(core::durableAtomicWriteTextNew(license,"Engineering source fixture; not musical qualification."));
    namespace production=voicebank_production;
    production::VoicebankProductionProject producer{.projectId="language-import",.inventoryId="fixture",.inventorySha256=std::string(64U,'a'),
        .selectedSourceStrategyId="fixture",.licenseLocator=license.string(),.licenseSha256=core::sha256File(license).value()};
    producer.sourceStrategies={{.id="fixture",.kind=production::SourceStrategyKind::ProceduralSynthesis,.rights=production::Feasibility::Pass,
        .permissions={true,true,false,false},.licenseLocator=producer.licenseLocator,.licenseSha256=producer.licenseSha256}};
    producer.operators={{"producer","PRODUCER"}};
    producer.unitAssignments={{.coverageKey=coverage,.pitchLayer=69,.promptId="vowel",.plannedTakeId="take"}};
    production::ProductionProjectRepository repository{root/"producer"};
    CHECK(repository.initialize(producer,{"create",producer.projectId,"producer","2026-09-09T00:00:00Z"}));
    CHECK(repository.importProceduralCandidate(producer,prefix.string()+".json",prefix.string()+".wav",resource.value(),
        {.takeId="take",.promptId="vowel",.coverageKey=coverage,.pitchLayer=69},
        {"import-procedural","take","producer","2026-09-09T00:01:00Z"}));
    CHECK(producer.takes.front().state==production::UnitQueueState::MarkerReview); CHECK(producer.reviews.empty());
    const auto recovered=repository.recover(); CHECK(recovered);
    const auto markers=production::resolveCandidateMarkers(recovered.value(),"take"); CHECK(markers);
    native_ui::VoicebankStudioController asyncStudio;
    CHECK(asyncStudio.beginOpenProductionProject(root/"producer",producer.inventorySha256,"producer"));
    for (unsigned poll=0U;asyncStudio.proceduralImportBusy() && poll<2000U;++poll) {
      CHECK(asyncStudio.pollProceduralCandidateImport());
      if (asyncStudio.proceduralImportBusy()) std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    CHECK(!asyncStudio.proceduralImportBusy()); CHECK(asyncStudio.productionProject());
    CHECK(asyncStudio.candidateMarkerPreview());
    CHECK(asyncStudio.candidateMarkerPreview()->markers==markers.value().candidate.markers);
    CHECK(asyncStudio.productionProject()->lastDurableGeneration==recovered.value().lastDurableGeneration);
    CHECK(markers.value().candidate.markers.front().kind==(voicedStop?voice_design::ProceduralGestureKind::VoicedPlosive:voicedFrication&&!voicedCoda?voice_design::ProceduralGestureKind::VoicedFrication:plosive?voice_design::ProceduralGestureKind::Plosive:nasal&&!coda?voice_design::ProceduralGestureKind::Nasal:voice_design::ProceduralGestureKind::OralVowel));
    if (voicedCoda) CHECK(markers.value().candidate.markers.back().kind==voice_design::ProceduralGestureKind::VoicedFrication);
    if (coda) CHECK(markers.value().candidate.markers.back().kind==voice_design::ProceduralGestureKind::Nasal);
  }
}

TEST_CASE("nasal and frication candidates bake and enter production with typed unapproved gestures") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("articulated-export");
  voice_design::VoiceRecipe recipe; recipe.id = "articulated-export";
  recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
  recipe.frications = {{"s", "neutral", {.seed = 42U}}};
  const auto oralResource=voice_design::freezeVoiceRecipeResource(recipe); CHECK(oralResource);
  CHECK(oralResource.value().identity.version=="2");
  recipe.poses.front().nasal=voice_design::NasalResonance{};
  recipe.poses.front().nasalCoupling=0.8;
  const auto resource = voice_design::freezeVoiceRecipeResource(recipe); CHECK(resource);
  CHECK(resource.value().identity.version=="3");
  CHECK(voice_design::decodeVoiceRecipeResource(resource.value()).value()==recipe);
  application::ProjectFactory factory{82000U}; auto project = factory.createProject("Articulated export");
  const auto trackId = factory.addVocalTrack(project, "Singer");
  const auto regionId = factory.addRegion(project, trackId, "Phrase", time::Tick{0}, time::Tick{960});
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{960}, 69U, U"さ", domain::Language::Japanese);
  auto* region = project.findRegion(regionId);
  region->phonemeOverrides = {
      {.key = {note.id, 0U}, .timing = {.startOffset = 0}, .locked = true},
      {.key = {note.id, 1U}, .timing = {.startOffset = 100000}, .locked = true}};
  region->lyrics.push_back(std::move(lyric)); region->notes.push_back(std::move(note));
  const std::vector<rendering::TrackSingerSource> sources{rendering::TrackProceduralSource{trackId, resource.value(), "neutral"}};
  const auto expected = rendering::ProductionProjectRenderer{}.renderWithSources(project, sources,
      trackId, regionId, 1U, 48000U, rendering::RenderQuality::Final); CHECK(expected);
  const std::vector<rendering::TrackSingerSource> oralSources{rendering::TrackProceduralSource{trackId,oralResource.value(),"neutral"}};
  const auto oral=rendering::ProductionProjectRenderer{}.renderWithSources(project,oralSources,
      trackId,regionId,1U,48000U,rendering::RenderQuality::Final); CHECK(oral);
  CHECK(oral.value().interleaved.size()==expected.value().interleaved.size());
  CHECK(oral.value().interleaved!=expected.value().interleaved);
  // The explicit 100 ms onset is unchanged noise; coloration belongs to the vowel.
  CHECK(std::equal(expected.value().interleaved.begin(),expected.value().interleaved.begin()+4800*expected.value().channelCount,
      oral.value().interleaved.begin()));
  authoring::ExportSettings settings; settings.format = voicebank::WavSampleFormat::Float32;
  const auto oralExport=authoring::ExportService{}.exportSetWithSources(project,oralSources,trackId,regionId,
      1U,root/"oral-reference",settings); CHECK(oralExport);
  CHECK(oralExport.value().state==authoring::ExportState::Committed);
  CHECK(voice_design::saveVoiceRecipeFile(root/"nasal-recipe.json",recipe));
  CHECK(voice_design::saveVoiceRecipeFile(root/"oral-recipe.json",voice_design::decodeVoiceRecipeResource(oralResource.value()).value()));
  const auto exported = authoring::ExportService{}.exportSetWithSources(project, sources, trackId, regionId,
      1U, root / "audio", settings); CHECK(exported);
  CHECK(exported.value().state == authoring::ExportState::Committed);
  for (const auto& file : exported.value().files) {
    const auto wav = voicebank::readWav(file.path); CHECK(wav);
    CHECK(wav.value().interleaved == expected.value().interleaved);
  }
  settings.includeProceduralCandidates = true;
  const auto baked = authoring::ExportService{}.exportSetWithSources(project, sources, trackId, regionId,
      1U, root / "baked", settings); CHECK(baked);
  const auto prefix = root / "baked/candidates" / (trackId.toString() + "-" + regionId.toString());
  const auto candidate = voice_design::loadProceduralCandidate(prefix.string() + ".json", prefix.string() + ".wav", resource.value()); CHECK(candidate);
  CHECK(candidate.value().schemaVersion == 2U); CHECK(candidate.value().markers.size() == 2U);
  CHECK(candidate.value().markers[0].kind == voice_design::ProceduralGestureKind::Frication);
  CHECK(candidate.value().markers[1].kind == voice_design::ProceduralGestureKind::OralVowel);
  CHECK(candidate.value().markers[0].ownedSpan.end == 4800);
  CHECK(candidate.value().markers[1].ownedSpan.start == 4800);
  const auto snapshot = rendering::RenderSnapshotFactory{}.createProcedural(project, resource.value(), trackId, regionId,
      1U, rendering::RenderQuality::Final, 48000U); CHECK(snapshot);
  CHECK(candidate.value().audio->interleaved == rendering::PhraseRenderPipeline{}.render(snapshot.value()).value().rendered.audio.samples);
  const auto json = formats::parseJson(candidate.value().metadataJson); CHECK(json);
  CHECK(json.value().find("approval")->asString() == "unapproved");
  CHECK(json.value().find("markerSemantics")->asString() == "planned-articulated-gestures");
  CHECK(candidate.value().articulationPlanRevision == voice_design::ArticulationPlan::algorithmRevision);
  for (unsigned scenario = 0U; scenario < 10U; ++scenario) {
    auto bad = json.value(); auto& onset = bad.find("markers")->asArray().front();
    if (scenario == 0U) *bad.find("schemaVersion") = formats::JsonValue{std::int64_t{1}};
    if (scenario == 1U) *bad.find("markerSemantics") = formats::JsonValue{"planned-vowel-gestures"};
    if (scenario == 2U) *onset.find("kind") = formats::JsonValue{"oral-vowel"};
    if (scenario == 3U) *onset.find("phone") = formats::JsonValue{"k"};
    if (scenario == 4U) onset.asObject().erase("kind");
    if (scenario == 5U) *bad.find("fricationRevision") = formats::JsonValue{std::int64_t{0}};
    if (scenario == 6U) bad.find("markers")->asArray().erase(bad.find("markers")->asArray().begin());
    if (scenario == 7U) *bad.find("style") = formats::JsonValue{"missing"};
    if (scenario == 8U) {
      *bad.find("schemaVersion") = formats::JsonValue{std::int64_t{1}};
      *bad.find("markerSemantics") = formats::JsonValue{"planned-vowel-gestures"};
      for (const auto* field : {"articulationPlanRevision", "fricationRevision", "fricationStreamRevision"}) bad.asObject().erase(field);
      for (auto& marker : bad.find("markers")->asArray()) marker.asObject().erase("kind");
    }
    if (scenario == 9U) *bad.find("sampleRate") = formats::JsonValue{std::int64_t{8000}};
    CHECK(!voice_design::parseProceduralCandidateMetadata(formats::stringifyJson(bad), resource.value()));
  }
  namespace production = voicebank_production;
  const auto license = root / "synthetic-test-license.txt";
  CHECK(core::durableAtomicWriteText(license, "SYNTHETIC TEST FIXTURE ONLY"));
  const auto licenseHash = core::sha256File(license); CHECK(licenseHash);
  production::VoicebankProductionProject producer{.projectId = "articulated-import-test", .inventoryId = "cv-fixture",
      .inventorySha256 = std::string(64U, 'a'), .selectedSourceStrategyId = "procedural-fixture",
      .licenseLocator = license.string(), .licenseSha256 = licenseHash.value(), .immutableAssetRoot = "assets"};
  producer.sourceStrategies = {{.id = "procedural-fixture", .kind = production::SourceStrategyKind::ProceduralSynthesis,
      .rights = production::Feasibility::Pass, .coverage = production::Feasibility::Pass, .listening = production::Feasibility::Pass,
      .permissions = {true, true, true, true}, .licenseLocator = license.string(), .licenseSha256 = licenseHash.value(),
      .evidenceState = "SYNTHETIC_TEST_ONLY"}};
  producer.operators = {{"producer", "PRODUCER"}};
  producer.unitAssignments = {{.coverageKey = "cv:sa", .pitchLayer = 69, .promptId = "prompt-sa", .plannedTakeId = "take-sa"}};
  production::ProductionProjectRepository repository{root / "producer"};
  CHECK(repository.initialize(producer, {.action = "create", .subjectId = producer.projectId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-07T00:00:00Z"}));
  const production::RawTakeInput generatedTake{.takeId = "take-sa", .promptId = "prompt-sa", .coverageKey = "cv:sa", .pitchLayer = 69};
  auto unreadyProducer = producer;
  unreadyProducer.sourceStrategies.front().permissions.sourceUse = false;
  const auto unreadyBefore = production::encodeProductionProject(unreadyProducer);
  const auto unreadyImport = repository.importProceduralCandidate(unreadyProducer, root / "not-read.json", root / "not-read.wav", resource.value(),
      generatedTake, {.action = "import-procedural", .subjectId = generatedTake.takeId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-07T00:00:01Z"});
  CHECK(!unreadyImport); CHECK(unreadyImport.error().code == core::ErrorCode::Conflict);
  CHECK(unreadyImport.error().message == "Source execution requires recorded source-use and transformation authorization");
  CHECK(production::encodeProductionProject(unreadyProducer) == unreadyBefore);
  CHECK(std::filesystem::is_empty(root / "producer/assets"));
  auto nativeProducer = producer; nativeProducer.projectId = "native-generation"; nativeProducer.lastDurableGeneration = 0U;
  nativeProducer.unitAssignments.push_back({.coverageKey = "cv:sa", .pitchLayer = 70, .promptId = "other-row", .plannedTakeId = "other-take"});
  production::ProductionProjectRepository nativeRepository{root / "native-generation"};
  CHECK(nativeRepository.initialize(nativeProducer, {.action = "create", .subjectId = nativeProducer.projectId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-07T00:00:00Z"}));
  const auto nativeJob = authoring::prepareGenerationJob(root / "native-job", "native-job", snapshot.value(), nativeProducer, generatedTake); CHECK(nativeJob);
  const auto nativeReference = authoring::loadGenerationJobReference(root / "native-job/job.seamjob"); CHECK(nativeReference);
  CHECK(nativeReference.value().manifestSha256 == nativeJob.value().manifestSha256);
  CHECK(std::filesystem::equivalent(nativeReference.value().directory, root / "native-job"));
  const auto referenceText = core::readTextFileLimited(root / "native-job/job.seamjob", 8192U); CHECK(referenceText);
  const auto referenceJson = formats::parseJson(referenceText.value()); CHECK(referenceJson);
  for (unsigned scenario = 0U; scenario < 4U; ++scenario) {
    auto bad = referenceJson.value();
    if (scenario == 0U) *bad.find("schemaVersion") = formats::JsonValue{std::int64_t{2}};
    if (scenario == 1U) *bad.find("directory") = formats::JsonValue{"bad\npath"};
    if (scenario == 2U) *bad.find("manifestSha256") = formats::JsonValue{"bad"};
    if (scenario == 3U) bad.asObject().emplace("extra", formats::JsonValue{true});
    const auto path = root / ("invalid-reference-" + std::to_string(scenario) + ".seamjob");
    CHECK(core::durableAtomicWriteTextNew(path, formats::stringifyJson(bad)));
    CHECK(!authoring::loadGenerationJobReference(path));
  }
  native_ui::VoicebankStudioController nativeGeneration;
  CHECK(nativeGeneration.openProductionProject(root / "native-generation", nativeProducer.inventorySha256, "producer"));
  const auto drainGeneration = [&]() -> core::Result<void> {
    for (unsigned attempt = 0U; attempt < 3000U; ++attempt) {
      const auto polled = nativeGeneration.pollProceduralCandidateImport();
      if (!polled) return polled;
      if (!nativeGeneration.proceduralImportBusy()) return core::success();
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return core::failure(core::ErrorCode::Internal, "Generation worker did not finish in the test budget");
  };
  CHECK(nativeGeneration.selectUnit(1U));
  CHECK(nativeGeneration.beginPreparedGenerationJob(nativeReference.value().directory, nativeReference.value().manifestSha256));
  CHECK(nativeGeneration.proceduralImportBusy()); CHECK(!nativeGeneration.selectUnit(0U)); CHECK(!nativeGeneration.save());
  CHECK(!drainGeneration()); CHECK(nativeRepository.recover().value().takes.empty());
  CHECK(!std::filesystem::exists(root / "native-job/output"));
  CHECK(nativeGeneration.selectUnit(0U));
  CHECK(nativeGeneration.beginPreparedGenerationJob(root / "native-job", nativeJob.value().manifestSha256));
  CHECK(drainGeneration());
  CHECK(nativeGeneration.productionProject()->takes.size() == 1U);
  CHECK(nativeGeneration.candidateMarkerPreview());
  CHECK(nativeGeneration.candidateMarkerPreview()->markers == candidate.value().markers);
  CHECK(nativeGeneration.editSelectedCandidateMarker(0, 4700));
  CHECK(nativeGeneration.canUndoCandidateMarkerEdit());
  const auto nativeBeforeRetry = production::encodeProductionProject(*nativeGeneration.productionProject());
  CHECK(nativeGeneration.beginPreparedGenerationJob(root / "native-job", nativeJob.value().manifestSha256));
  CHECK(drainGeneration()); CHECK(nativeGeneration.status() == "GENERATION ALREADY COLLECTED");
  CHECK(production::encodeProductionProject(*nativeGeneration.productionProject()) == nativeBeforeRetry);
  CHECK(nativeGeneration.canUndoCandidateMarkerEdit());
  CHECK(nativeGeneration.candidateMarkerPreview()->markers.front().ownedSpan.end == 4700);
  auto externalNative = nativeRepository.recover(); CHECK(externalNative);
  externalNative.value().unitAssignments.back().plannedTakeId = "externally-changed";
  CHECK(nativeRepository.save(externalNative.value(), {.action = "save", .subjectId = externalNative.value().projectId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-07T00:00:04Z"}));
  CHECK(nativeGeneration.beginPreparedGenerationJob(root / "native-job", nativeJob.value().manifestSha256));
  CHECK(!drainGeneration());
  CHECK(production::encodeProductionProject(*nativeGeneration.productionProject()) == nativeBeforeRetry);
  CHECK(production::encodeProductionProject(nativeRepository.recover().value()) == production::encodeProductionProject(externalNative.value()));
  auto batchProducer = producer; batchProducer.projectId = "batch-producer"; batchProducer.lastDurableGeneration = 0U;
  batchProducer.unitAssignments.push_back({.coverageKey = "cv:sa", .pitchLayer = 70, .promptId = "prompt-sa-70", .plannedTakeId = "take-sa-70"});
  production::ProductionProjectRepository batchRepository{root / "batch-producer"};
  CHECK(batchRepository.initialize(batchProducer, {.action = "create", .subjectId = batchProducer.projectId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-07T00:00:00Z"}));
  auto secondScore = project; secondScore.findRegion(regionId)->notes.front().midiKey = 70U;
  const auto secondSnapshot = rendering::RenderSnapshotFactory{}.createProcedural(secondScore, resource.value(), trackId, regionId,
      0U, rendering::RenderQuality::Final, 48000U); CHECK(secondSnapshot);
  const auto batchA = authoring::prepareGenerationJob(root / "batch-a", "batch-a", snapshot.value(), batchProducer, generatedTake); CHECK(batchA);
  const auto batchB = authoring::prepareGenerationJob(root / "batch-b", "batch-b", secondSnapshot.value(), batchProducer,
      {.takeId = "take-sa-70", .promptId = "prompt-sa-70", .coverageKey = "cv:sa", .pitchLayer = 70}); CHECK(batchB);
  const std::vector<authoring::GenerationJobReference> batch{{root / "batch-a", batchA.value().manifestSha256}, {root / "batch-b", batchB.value().manifestSha256}};
  CHECK(!authoring::saveGenerationBatch(root / "rejected-batch.json", batch, {.maximumFrames = 47999U}));
  auto duplicatePrepared = batch; duplicatePrepared[1] = duplicatePrepared[0];
  CHECK(!authoring::saveGenerationBatch(root / "rejected-batch.json", duplicatePrepared));
  std::stop_source saveBatchStop; saveBatchStop.request_stop();
  CHECK(!authoring::saveGenerationBatch(root / "rejected-batch.json", batch, {}, saveBatchStop.get_token()));
  CHECK(!std::filesystem::exists(root / "rejected-batch.json"));
  CHECK(!authoring::saveGenerationBatch(root / "rejected-batch.json", batch, {}, {}, std::string(64U, '0')));
  CHECK(!std::filesystem::exists(root / "rejected-batch.json"));
  const auto savedBatchHash = authoring::saveGenerationBatch(root / "prepared-batch.json", batch); CHECK(savedBatchHash);
  const auto savedBatch = authoring::loadGenerationBatch(root / "prepared-batch.json", savedBatchHash.value()); CHECK(savedBatch);
  CHECK(savedBatch.value().size() == 2U);
  CHECK(std::filesystem::equivalent(savedBatch.value()[0].directory, batch[0].directory));
  CHECK(savedBatch.value()[1].manifestSha256 == batch[1].manifestSha256);
  CHECK(!authoring::saveGenerationBatch(root / "prepared-batch.json", batch));
  CHECK(core::sha256File(root / "prepared-batch.json").value() == savedBatchHash.value());
  CHECK(!std::filesystem::exists(root / "batch-a/output"));
  CHECK(batchRepository.recover().value().takes.empty());
#if defined(SEAM_TEST_VOICEBANK_CLI) && (defined(__APPLE__) || defined(__linux__))
  const std::vector<std::string> prepareBatchArgs{"prepare-generation-batch", (root / "cli-batch.json").string(),
      (root / "batch-a/job.seamjob").string(), (root / "batch-b/job.seamjob").string()};
  CHECK(runVoicebankCli(prepareBatchArgs) == 0);
  CHECK(runVoicebankCli(prepareBatchArgs) != 0);
  CHECK(authoring::loadGenerationBatch(root / "cli-batch.json", core::sha256File(root / "cli-batch.json").value()));
#endif
  CHECK(!authoring::verifyGenerationJobOutput(root / "batch-a", batchA.value().manifestSha256));
  CHECK(!std::filesystem::exists(root / "batch-a/output"));
  CHECK(!authoring::runGenerationBatch(batch, {.maximumFrames = 47999U}));
  CHECK(!std::filesystem::exists(root / "batch-a/output"));
  auto duplicatedBatch = batch; duplicatedBatch[1] = duplicatedBatch[0];
  CHECK(!authoring::runGenerationBatch(duplicatedBatch));
  CHECK(!std::filesystem::exists(root / "batch-a/output"));
  std::stop_source batchStop;
  CHECK(!authoring::runGenerationBatch(batch, {.maximumFrames = 48000U}, batchStop.get_token(), [&](std::size_t done, std::size_t total) {
    CHECK(total == 2U); CHECK(done == 1U); batchStop.request_stop();
  }));
  CHECK(std::filesystem::exists(root / "batch-a/output")); CHECK(!std::filesystem::exists(root / "batch-b/output"));
  const auto resumedBatch = authoring::runGenerationBatch(batch, {.maximumFrames = 48000U}); CHECK(resumedBatch);
  CHECK(resumedBatch.value().size() == 2U); CHECK(resumedBatch.value()[0].reused); CHECK(!resumedBatch.value()[1].reused);
  CHECK(batchRepository.recover().value().takes.empty());
  const auto batchJson = formats::stringifyJson(formats::JsonValue{formats::JsonValue::Object{
      {"formatId", formats::JsonValue{"com.project-seam.generation-batch"}}, {"schemaVersion", formats::JsonValue{std::int64_t{1}}},
      {"jobs", formats::JsonValue{formats::JsonValue::Array{
        formats::JsonValue{formats::JsonValue::Object{{"directory", formats::JsonValue{"batch-a"}}, {"manifestSha256", formats::JsonValue{batchA.value().manifestSha256}}}},
        formats::JsonValue{formats::JsonValue::Object{{"directory", formats::JsonValue{"batch-b"}}, {"manifestSha256", formats::JsonValue{batchB.value().manifestSha256}}}}}}}}});
  CHECK(core::durableAtomicWriteTextNew(root / "batch.json", batchJson));
  const auto batchHash = core::sha256Hex(batchJson);
  CHECK(!authoring::loadGenerationBatch(root / "batch.json", std::string(64U, '0')));
  const auto loadedBatch = authoring::loadGenerationBatch(root / "batch.json", batchHash); CHECK(loadedBatch);
  const auto reusedBatch = authoring::runGenerationBatch(loadedBatch.value()); CHECK(reusedBatch);
  CHECK(reusedBatch.value()[0].reused && reusedBatch.value()[1].reused);
  const std::vector<production::GeneratedCandidateInput> batchInputs{
      {reusedBatch.value()[0].metadataPath, reusedBatch.value()[0].audioPath, resource.value(), batchA.value().expectation},
      {reusedBatch.value()[1].metadataPath, reusedBatch.value()[1].audioPath, resource.value(), batchB.value().expectation}};
  const production::ProductionJournalEvent batchEvent{.action = "import-generated-batch", .subjectId = batchHash,
      .operatorId = "producer", .occurredAtUtc = "2026-09-07T00:00:02Z"};
  const auto beforeBatchImport = production::encodeProductionProject(batchProducer);
  CHECK(!batchRepository.importGeneratedBatch(batchProducer, batchInputs, batchEvent, 47999U));
  auto brokenBatchInputs = batchInputs; brokenBatchInputs[1].audioPath = root / "missing-batch.wav";
  CHECK(!batchRepository.importGeneratedBatch(batchProducer, brokenBatchInputs, batchEvent, 48000U));
  CHECK(production::encodeProductionProject(batchProducer) == beforeBatchImport);
  CHECK(production::encodeProductionProject(batchRepository.recover().value()) == beforeBatchImport);
  const auto beforeBatchGeneration = batchProducer.lastDurableGeneration;
  auto studioBatchProducer = batchProducer; studioBatchProducer.lastDurableGeneration = 0U;
  production::ProductionProjectRepository studioBatchRepository{root / "studio-batch"};
  CHECK(studioBatchRepository.initialize(studioBatchProducer, {.action = "create", .subjectId = studioBatchProducer.projectId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-07T00:00:00Z"}));
  native_ui::VoicebankStudioController studioBatch;
  CHECK(studioBatch.openProductionProject(root / "studio-batch", studioBatchProducer.inventorySha256, "producer"));
  const auto drainBatch = [&]() -> core::Result<void> {
    for (unsigned attempt = 0U; attempt < 3000U; ++attempt) {
      if (const auto progress = studioBatch.generationBatchProgress()) {
        CHECK(progress->completedOutputs <= progress->totalOutputs);
        CHECK(progress->totalOutputs <= 64U);
        if (progress->phase == native_ui::VoicebankStudioController::GenerationBatchProgress::Phase::Collecting)
          CHECK(progress->completedOutputs == progress->totalOutputs);
      }
      const auto polled = studioBatch.pollProceduralCandidateImport();
      if (!polled) { CHECK(!studioBatch.generationBatchProgress()); return polled; }
      if (!studioBatch.proceduralImportBusy()) {
        CHECK(!studioBatch.generationBatchProgress()); return core::success();
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return core::failure(core::ErrorCode::Internal, "Batch worker exceeded test budget");
  };
  CHECK(!studioBatch.beginPreparedGenerationBatch(root / "batch.json", batchHash, 0U));
  const std::vector<std::filesystem::path> assemblyReferences{root / "batch-a/job.seamjob", root / "batch-b/job.seamjob"};
  CHECK(studioBatch.beginGenerationBatchAssembly(assemblyReferences, root / "studio-assembled.json"));
  CHECK(studioBatch.proceduralImportBusy()); CHECK(!studioBatch.save());
  CHECK(drainBatch()); CHECK(studioBatch.status() == "BATCH PREPARED / NOT GENERATED");
  CHECK(authoring::loadGenerationBatch(root / "studio-assembled.json", core::sha256File(root / "studio-assembled.json").value()));
  CHECK(production::encodeProductionProject(*studioBatch.productionProject()) == beforeBatchImport);
  CHECK(production::encodeProductionProject(studioBatchRepository.recover().value()) == beforeBatchImport);
  CHECK(studioBatch.beginGenerationBatchAssembly({assemblyReferences.front(), assemblyReferences.front()}, root / "bad-studio-assembly.json"));
  CHECK(!drainBatch()); CHECK(!std::filesystem::exists(root / "bad-studio-assembly.json"));
  CHECK(studioBatch.beginPreparedGenerationBatch(root / "batch.json", std::string(64U, '0'), 48000U));
  CHECK(!drainBatch()); CHECK(studioBatchRepository.recover().value().takes.empty());
  CHECK(studioBatch.beginPreparedGenerationBatch(root / "batch.json", batchHash, 47999U));
  CHECK(!drainBatch()); CHECK(studioBatchRepository.recover().value().takes.empty());
  std::filesystem::rename(root / "batch-b/output", root / "batch-b/prior-test-output");
  CHECK(!std::filesystem::exists(root / "batch-b/output"));
  CHECK(studioBatch.beginPreparedGenerationBatch(root / "batch.json", batchHash, 48000U));
  CHECK(studioBatch.generationBatchProgress());
  CHECK(studioBatch.proceduralImportBusy()); CHECK(!studioBatch.selectUnit(1U)); CHECK(!studioBatch.save());
  CHECK(drainBatch());
  CHECK(std::filesystem::exists(root / "batch-b/output"));
  CHECK(authoring::verifyGenerationJobOutput(root / "batch-b", batchB.value().manifestSha256));
  CHECK(studioBatch.productionProject()->takes.size() == 2U);
  CHECK(studioBatch.productionProject()->lastDurableGeneration == beforeBatchGeneration + 1U);
  for (const auto& assignment : studioBatch.productionProject()->unitAssignments)
    CHECK(assignment.state == production::UnitQueueState::MarkerReview);
  CHECK(studioBatch.candidateMarkerPreview());
  CHECK(studioBatch.editSelectedCandidateMarker(0, 4700, "2026-09-07T00:00:03Z"));
  const auto studioBatchCollected = production::encodeProductionProject(*studioBatch.productionProject());
  CHECK(studioBatch.beginPreparedGenerationBatch(root / "batch.json", batchHash, 48000U));
  CHECK(drainBatch()); CHECK(studioBatch.canUndoCandidateMarkerEdit());
  CHECK(studioBatch.status() == "GENERATION BATCH ALREADY COLLECTED");
  CHECK(production::encodeProductionProject(*studioBatch.productionProject()) == studioBatchCollected);
  auto externalBatch = studioBatchRepository.recover().value();
  externalBatch.unitAssignments[1].plannedTakeId = "changed-outside-studio";
  CHECK(studioBatchRepository.save(externalBatch, {.action = "save", .subjectId = externalBatch.projectId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-07T00:00:04Z"}));
  CHECK(studioBatch.beginGenerationBatchAssembly(assemblyReferences, root / "stale-studio-assembly.json"));
  CHECK(!drainBatch()); CHECK(!std::filesystem::exists(root / "stale-studio-assembly.json"));
  CHECK(studioBatch.beginPreparedGenerationBatch(root / "batch.json", batchHash, 48000U));
  CHECK(!drainBatch());
  CHECK(production::encodeProductionProject(*studioBatch.productionProject()) == studioBatchCollected);
  auto studioPartial = batchProducer; studioPartial.lastDurableGeneration = 0U;
  production::ProductionProjectRepository studioPartialRepository{root / "studio-partial-batch"};
  CHECK(studioPartialRepository.initialize(studioPartial, {.action = "create", .subjectId = studioPartial.projectId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-07T00:00:00Z"}));
  CHECK(studioPartialRepository.importProceduralCandidate(studioPartial, batchInputs[0].metadataPath, batchInputs[0].audioPath,
      resource.value(), generatedTake, {.action = "import-procedural", .subjectId = generatedTake.takeId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-07T00:00:01Z"}, {}, &batchA.value().expectation));
  CHECK(studioBatch.openProductionProject(root / "studio-partial-batch", studioPartial.inventorySha256, "producer"));
  CHECK(studioBatch.beginPreparedGenerationBatch(root / "batch.json", batchHash, 48000U));
  const auto partialNativeResult = drainBatch(); CHECK(!partialNativeResult);
  CHECK(partialNativeResult.error().message.find("partially collected") != std::string::npos);
  CHECK(production::encodeProductionProject(*studioBatch.productionProject()) == production::encodeProductionProject(studioPartial));
  CHECK(production::encodeProductionProject(studioPartialRepository.recover().value()) == production::encodeProductionProject(studioPartial));
  CHECK(studioBatch.beginPreparedGenerationBatch(root / "absent-batch.json", batchHash, 48000U));
  CHECK(studioBatch.generationBatchProgress());
  CHECK(!studioBatch.finishProceduralCandidateImport());
  CHECK(!studioBatch.generationBatchProgress()); CHECK(!studioBatch.proceduralImportBusy());
  CHECK(production::encodeProductionProject(*studioBatch.productionProject()) == production::encodeProductionProject(studioPartial));
#if defined(SEAM_TEST_VOICEBANK_CLI) && (defined(__APPLE__) || defined(__linux__))
  const std::vector<std::string> collectBatchArgs{"import-generated-batch", (root / "batch-producer").string(),
      (root / "batch.json").string(), batchHash, "producer", "2026-09-07T00:00:02Z", "48000"};
  auto partialProducer = batchProducer; partialProducer.lastDurableGeneration = 0U;
  production::ProductionProjectRepository partialRepository{root / "partial-batch-producer"};
  CHECK(partialRepository.initialize(partialProducer, {.action = "create", .subjectId = partialProducer.projectId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-07T00:00:00Z"}));
  CHECK(partialRepository.importProceduralCandidate(partialProducer, batchInputs[0].metadataPath, batchInputs[0].audioPath,
      resource.value(), generatedTake, {.action = "import-procedural", .subjectId = generatedTake.takeId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-07T00:00:01Z"}, {}, &batchA.value().expectation));
  const auto partialState = production::encodeProductionProject(partialProducer);
  auto partialArgs = collectBatchArgs; partialArgs[1] = (root / "partial-batch-producer").string();
  CHECK(runVoicebankCli(partialArgs) != 0);
  CHECK(production::encodeProductionProject(partialRepository.recover().value()) == partialState);
  CHECK(runVoicebankCli(collectBatchArgs) == 0);
  batchProducer = batchRepository.recover().value();
#else
  const auto batchImported = batchRepository.importGeneratedBatch(batchProducer, batchInputs, batchEvent, 48000U); CHECK(batchImported);
  CHECK(batchImported.value().assets.size() == 2U);
#endif
  CHECK(batchProducer.lastDurableGeneration == beforeBatchGeneration + 1U);
  const auto collectedBatch = batchRepository.recover(); CHECK(collectedBatch); CHECK(collectedBatch.value().takes.size() == 2U);
  for (const auto& assignment : collectedBatch.value().unitAssignments) {
    CHECK(assignment.state == production::UnitQueueState::MarkerReview);
    CHECK(!assignment.markerReviewed && !assignment.pitchReviewed);
  }
  CHECK(batchRepository.findCollectedGeneration(batchA.value().expectation).value());
  CHECK(batchRepository.findCollectedGeneration(batchB.value().expectation).value());
  const auto afterBatchImport = production::encodeProductionProject(batchProducer);
  CHECK(!batchRepository.importGeneratedBatch(batchProducer, batchInputs, batchEvent, 48000U));
  CHECK(production::encodeProductionProject(batchRepository.recover().value()) == afterBatchImport);
#if defined(SEAM_TEST_VOICEBANK_CLI) && (defined(__APPLE__) || defined(__linux__))
  CHECK(runVoicebankCli(collectBatchArgs) == 0);
  CHECK(production::encodeProductionProject(batchRepository.recover().value()) == afterBatchImport);
  std::filesystem::rename(root / "batch-a/output", root / "batch-a/held-output");
  std::filesystem::rename(root / "batch-b/output", root / "batch-b/held-output");
  CHECK(runVoicebankCli(collectBatchArgs) == 0);
  CHECK(!std::filesystem::exists(root / "batch-a/output")); CHECK(!std::filesystem::exists(root / "batch-b/output"));
  CHECK(production::encodeProductionProject(batchRepository.recover().value()) == afterBatchImport);
  std::filesystem::rename(root / "batch-a/held-output", root / "batch-a/output");
  std::filesystem::rename(root / "batch-b/held-output", root / "batch-b/output");
  CHECK(runVoicebankCli({"run-generation-batch", (root / "batch.json").string(), batchHash, "48000"}) == 0);
  CHECK(runVoicebankCli({"run-generation-batch", (root / "batch.json").string(), batchHash, "47999"}) != 0);
  CHECK(runVoicebankCli({"run-generation-batch", (root / "batch.json").string(), batchHash, "48000junk"}) != 0);
#endif
#if defined(SEAM_TEST_VOICEBANK_CLI) && (defined(__APPLE__) || defined(__linux__))
  auto cliProducer = producer; cliProducer.projectId = "cli-job-producer"; cliProducer.lastDurableGeneration = 0U;
  production::ProductionProjectRepository cliRepository{root / "cli-job-producer"};
  CHECK(cliRepository.initialize(cliProducer, {.action = "create", .subjectId = cliProducer.projectId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-07T00:00:00Z"}));
  CHECK(voice_design::saveVoiceRecipeFile(root / "job-source-recipe.json", recipe));
  auto cliScore = project;
  cliScore.findVocalTrack(trackId)->proceduralRecipe = domain::ProceduralRecipeReference{resource.value().identity, "job-source-recipe.json", "neutral"};
  CHECK(formats::ProjectJsonCodec{}.save(cliScore, root / "job-source.seam"));
  const auto cliJobDirectory = root / "cli-prepared-job";
  std::vector<std::string> prepareArgs{"prepare-generation", (root / "cli-job-producer").string(), (root / "job-source.seam").string(),
      trackId.toString(), regionId.toString(), "take-sa", "cli-job-sa", cliJobDirectory.string()};
  CHECK(runVoicebankCli(prepareArgs) == 0);
  CHECK(runVoicebankCli(prepareArgs) != 0);
  auto badPrepare = prepareArgs; badPrepare[5] = "not-planned"; badPrepare[7] = (root / "bad-cli-job").string();
  CHECK(runVoicebankCli(badPrepare) != 0); CHECK(!std::filesystem::exists(root / "bad-cli-job"));
  const auto cliJobHash = core::sha256File(cliJobDirectory / "job.json"); CHECK(cliJobHash);
  CHECK(runVoicebankCli({"run-generation", cliJobDirectory.string(), cliJobHash.value()}) == 0);
  const auto cliJob = authoring::loadGenerationJob(cliJobDirectory, cliJobHash.value()); CHECK(cliJob);
  const auto cliOutput = cliJobDirectory / "output/candidates" / (trackId.toString() + "-" + regionId.toString());
  const auto cliExpectationHash = core::sha256File(cliJobDirectory / "expectation.json"); CHECK(cliExpectationHash);
  CHECK(runVoicebankCli({"import-generated", (root / "cli-job-producer").string(), cliOutput.string() + ".json", cliOutput.string() + ".wav",
      (cliJobDirectory / "recipe.json").string(), (cliJobDirectory / "expectation.json").string(), cliExpectationHash.value(),
      "producer", "2026-09-07T00:00:01Z"}) == 0);
  const auto cliCollected = cliRepository.recover(); CHECK(cliCollected);
  CHECK(cliCollected.value().takes.size() == 1U);
  CHECK(cliCollected.value().takes.front().state == production::UnitQueueState::MarkerReview);
  CHECK(!cliCollected.value().unitAssignments.front().markerReviewed);
  CHECK(production::resolveCandidateMarkers(cliCollected.value(), "take-sa").value().candidate.markers == candidate.value().markers);
  badPrepare[5] = "take-sa";
  CHECK(runVoicebankCli(badPrepare) != 0); CHECK(!std::filesystem::exists(root / "bad-cli-job"));
#endif
  CHECK(voice_design::saveVoiceRecipeFile(root / "shared-score-recipe.json", recipe));
  auto savedScore = project;
  savedScore.findVocalTrack(trackId)->proceduralRecipe = domain::ProceduralRecipeReference{
      resource.value().identity, "shared-score-recipe.json", "neutral"};
  CHECK(formats::ProjectJsonCodec{}.save(savedScore, root / "shared-score.seam"));
  const auto producerBeforePreparation = production::encodeProductionProject(producer);
  const auto preparedScore = authoring::prepareGenerationJobFromScore(root / "shared-score-job", "shared-score",
      root / "shared-score.seam", trackId, regionId, producer, "take-sa");
  CHECK(preparedScore);
  CHECK(preparedScore.value().expectation.takeId == "take-sa");
  CHECK(preparedScore.value().expectation.recipeHash == resource.value().identity.contentHash);
  CHECK(preparedScore.value().expectation.projectStateSha256 == core::sha256Hex(producerBeforePreparation));
  CHECK(authoring::loadGenerationJob(root / "shared-score-job", preparedScore.value().manifestSha256));
  CHECK(production::encodeProductionProject(producer) == producerBeforePreparation);
  // Preparation restart retains the original expectation, not a newly captured
  // producer state. Simulate interruption before final manifest publication.
  const auto preparedDirectory = root / "shared-score-job";
  std::filesystem::rename(preparedDirectory / "job.json", preparedDirectory / "held-job.json");
  std::filesystem::rename(preparedDirectory / "project.json", preparedDirectory / "held-project.json");
  std::filesystem::rename(preparedDirectory / "expectation.json", preparedDirectory / "held-expectation.json");
  auto changedPreparationProducer = producer; ++changedPreparationProducer.lastDurableGeneration;
  CHECK(!authoring::resumeGenerationJobPreparation(preparedDirectory, "shared-score", preparedScore.value().snapshot,
      changedPreparationProducer, generatedTake));
  CHECK(!std::filesystem::exists(preparedDirectory / "job.json"));
  const auto originalRecipeBytes = core::readTextFileLimited(preparedDirectory / "recipe.json", 1024U * 1024U); CHECK(originalRecipeBytes);
  CHECK(core::durableAtomicWriteText(preparedDirectory / "recipe.json", "{}"));
  CHECK(!authoring::resumeGenerationJobPreparation(preparedDirectory, "shared-score", preparedScore.value().snapshot, producer, generatedTake));
  CHECK(!std::filesystem::exists(preparedDirectory / "job.json"));
  CHECK(!std::filesystem::exists(preparedDirectory / "project.json"));
  CHECK(!std::filesystem::exists(preparedDirectory / "expectation.json"));
  CHECK(core::durableAtomicWriteText(preparedDirectory / "recipe.json", originalRecipeBytes.value()));
  const auto resumedPreparation = authoring::resumeGenerationJobPreparation(preparedDirectory, "shared-score",
      preparedScore.value().snapshot, producer, generatedTake); CHECK(resumedPreparation);
  CHECK(resumedPreparation.value().manifestSha256 == preparedScore.value().manifestSha256);
  CHECK(resumedPreparation.value().expectation == preparedScore.value().expectation);
  CHECK(authoring::resumeGenerationJobPreparation(preparedDirectory, "shared-score", preparedScore.value().snapshot, producer, generatedTake));
  CHECK(!authoring::prepareGenerationJob(preparedDirectory, "shared-score", preparedScore.value().snapshot, producer, generatedTake));
  CHECK(std::filesystem::create_directory(root / "unowned-job-directory"));
  CHECK(!authoring::resumeGenerationJobPreparation(root / "unowned-job-directory", "shared-score", preparedScore.value().snapshot, producer, generatedTake));
  // Exercise the real score -> job -> render -> CLI collection route with
  // explicit ownership, rather than only testing the producer record codec.
  auto styleProducer = producer;
  CHECK(styleProducer.takes.empty());
  styleProducer.projectId = "style-owned-generation";
  styleProducer.schemaVersion = production::kProductionStyleSchemaVersion;
  styleProducer.language = "ja";
  styleProducer.lastDurableGeneration = 0U;
  for (auto& row : styleProducer.unitAssignments) row.style = "neutral";
  auto inventoryProducer = styleProducer;
  inventoryProducer.unitAssignments.front().coverageKey = "cv:s:a";
  production::ProductionProjectRepository inventoryRepository{root / "inventory-producer"};
  CHECK(inventoryRepository.initialize(inventoryProducer, {"create", inventoryProducer.projectId, "producer", "2026-09-13T00:00:00Z"}));
  const auto inventoryBefore = production::encodeProductionProject(inventoryProducer);
  const auto inventoryScore = authoring::buildInventoryGenerationScore(inventoryProducer, "take-sa");
  CHECK(inventoryScore);
  CHECK(inventoryScore.value().project.findRegion(inventoryScore.value().regionId)->notes.front().phoneticHint == "s a");
  CHECK(authoring::buildInventoryGenerationScore(inventoryProducer, "take-sa").value().templateIdentity == inventoryScore.value().templateIdentity);
  const auto inventoryJob = authoring::prepareInventoryGenerationJob(root / "inventory.seam", root / "inventory-job",
      inventoryProducer, "take-sa", {resource.value(), "neutral"});
  if (!inventoryJob) throw std::runtime_error(inventoryJob.error().message);
  CHECK(inventoryJob.value().expectation.coverageKey == "cv:s:a");
  CHECK(inventoryJob.value().expectation.language == "ja");
  CHECK(authoring::runGenerationJob(root / "inventory-job", inventoryJob.value().manifestSha256));
  CHECK(production::encodeProductionProject(inventoryProducer) == inventoryBefore);
  CHECK(!authoring::prepareInventoryGenerationJob(root / "inventory.seam", root / "inventory-job-repeat",
      inventoryProducer, "take-sa", {resource.value(), "neutral"}));
  CHECK(!std::filesystem::exists(root / "inventory-job-repeat"));
  CHECK(!authoring::prepareInventoryGenerationJob(root / "wrong-inventory.seam", root / "wrong-inventory-job",
      inventoryProducer, "take-sa", {resource.value(), "soft"}));
  CHECK(!std::filesystem::exists(root / "wrong-inventory.seam"));
  auto invalidInventory = inventoryProducer;
  invalidInventory.unitAssignments.front().coverageKey = "release:a:R";
  CHECK(!authoring::buildInventoryGenerationScore(invalidInventory, "take-sa"));
  invalidInventory = inventoryProducer; invalidInventory.language = "en";
  CHECK(!authoring::buildInventoryGenerationScore(invalidInventory, "take-sa"));
  invalidInventory = inventoryProducer; invalidInventory.unitAssignments.push_back(invalidInventory.unitAssignments.front());
  CHECK(!authoring::buildInventoryGenerationScore(invalidInventory, "take-sa"));
  auto multiProducer = inventoryProducer;
  multiProducer.projectId = "multi-style-inventory"; multiProducer.lastDurableGeneration = 0U;
  auto softAssignment = multiProducer.unitAssignments.front();
  softAssignment.style = "soft"; softAssignment.plannedTakeId = "take-sa-soft"; softAssignment.promptId = "prompt-sa-soft";
  multiProducer.unitAssignments.push_back(softAssignment);
  production::ProductionProjectRepository multiRepository{root / "multi-style-inventory"};
  CHECK(multiRepository.initialize(multiProducer, {"create", multiProducer.projectId, "producer", "2026-09-13T00:00:00Z"}));
  auto multiRecipe = recipe;
  auto softPose = recipe.poses.front(); softPose.style = "soft"; multiRecipe.poses.push_back(softPose);
  auto softNoise = recipe.frications.front(); softNoise.style = "soft"; multiRecipe.frications.push_back(softNoise);
  const auto multiResource = voice_design::freezeVoiceRecipeResource(multiRecipe); CHECK(multiResource);
  const std::vector<std::string> campaignTakes{"take-sa", "take-sa-soft"};
  const auto beforeCampaign = production::encodeProductionProject(multiProducer);
  const auto campaign = authoring::planGenerationCampaign(multiProducer, campaignTakes, multiResource.value(),
      {.batch = {.maximumJobs = 1U}}); CHECK(campaign);
  const auto campaignJson = formats::parseJson(campaign.value()); CHECK(campaignJson);
  CHECK(campaignJson.value().find("batchCount")->asInt64() == 2);
  CHECK(campaignJson.value().find("totalFrames")->asInt64() == 48000);
  CHECK(campaignJson.value().find("status")->asString() == "PLANNED_UNPREPARED");
  CHECK(campaignJson.value().find("initialProducerSha256")->asString() == core::sha256Hex(beforeCampaign));
  CHECK(campaign.value().find("expectation") == std::string::npos);
  CHECK(authoring::verifyGenerationCampaign(campaign.value(), core::sha256Hex(campaign.value())));
  CHECK(!authoring::verifyGenerationCampaign(campaign.value(), std::string(64U, '0')));
  for (unsigned scenario = 0U; scenario < 4U; ++scenario) {
    auto altered = campaignJson.value();
    if (scenario == 0U) *altered.find("totalFrames") = formats::JsonValue{std::int64_t{1}};
    if (scenario == 1U) *altered.find("jobs")->asArray().front().find("batchIndex") = formats::JsonValue{std::int64_t{1}};
    if (scenario == 2U) altered.asObject().emplace("expectation", formats::JsonValue{"premature"});
    if (scenario == 3U) *altered.find("maximumFrames") = formats::JsonValue{true};
    const auto alteredBytes = formats::stringifyJson(altered, true);
    CHECK(!authoring::verifyGenerationCampaign(alteredBytes, core::sha256Hex(alteredBytes)));
  }
  CHECK(production::encodeProductionProject(multiProducer) == beforeCampaign);
  CHECK(authoring::planGenerationCampaign(multiProducer, std::vector<std::string>{"take-sa-soft", "take-sa"},
      multiResource.value(), {.batch = {.maximumJobs = 1U}}).value() == campaign.value());
  CHECK(!authoring::planGenerationCampaign(multiProducer, campaignTakes, multiResource.value(), {.maximumFrames = 47999U}));
  CHECK(!authoring::planGenerationCampaign(multiProducer, campaignTakes, multiResource.value(), {.maximumEstimatedBytes = 1U}));
  CHECK(!authoring::planGenerationCampaign(multiProducer, campaignTakes, multiResource.value(), {.batch = {.maximumFrames = 23999U}}));
  CHECK(!authoring::planGenerationCampaign(multiProducer, std::vector<std::string>{"take-sa", "take-sa"}, multiResource.value()));
  CHECK(!authoring::planGenerationCampaign(multiProducer, campaignTakes, resource.value()));
  std::stop_source campaignStop; campaignStop.request_stop();
  CHECK(!authoring::planGenerationCampaign(multiProducer, campaignTakes, multiResource.value(), {}, campaignStop.get_token()));
  auto campaignProducer = multiProducer; campaignProducer.projectId = "two-batch-campaign"; campaignProducer.lastDurableGeneration = 0U;
  production::ProductionProjectRepository campaignRepository{root / "two-batch-producer"};
  CHECK(campaignRepository.initialize(campaignProducer, {"create", campaignProducer.projectId, "producer", "2026-09-13T00:00:00Z"}));
  const auto executablePlan = authoring::planGenerationCampaign(campaignProducer, campaignTakes, multiResource.value(),
      {.batch = {.maximumJobs = 1U}}); CHECK(executablePlan);
  const auto executableHash = core::sha256Hex(executablePlan.value());
  auto mutablePlanBytes = executablePlan.value();
  auto admittedCampaign = authoring::VerifiedGenerationCampaign::admit(mutablePlanBytes, executableHash); CHECK(admittedCampaign);
  mutablePlanBytes = "{}";
  CHECK(admittedCampaign.value().sha256() == executableHash);
  const auto sharedCampaign = admittedCampaign.value();
  auto movedCampaign = std::move(admittedCampaign.value());
  CHECK(!admittedCampaign.value().valid());
  CHECK(&sharedCampaign.plan() == &movedCampaign.plan());
  CHECK(!authoring::prepareGenerationCampaignBatch(admittedCampaign.value(), 0U, campaignProducer, root / "empty-admission"));
  CHECK(!std::filesystem::exists(root / "empty-admission"));
  CHECK(!authoring::VerifiedGenerationCampaign::admit(executablePlan.value(), std::string(64U, '0')));
  CHECK(!authoring::prepareGenerationCampaignBatch(executablePlan.value(), executableHash, 1U, campaignProducer, root / "premature-batch"));
  CHECK(!std::filesystem::exists(root / "premature-batch"));
  const auto firstCampaignBatch = authoring::prepareGenerationCampaignBatch(movedCampaign, 0U,
      campaignProducer, root / "campaign-batch-0"); CHECK(firstCampaignBatch);
  CHECK(firstCampaignBatch.value().jobs.size() == 1U);
  const auto retriedCampaignBatch = authoring::prepareGenerationCampaignBatch(sharedCampaign, 0U,
      campaignProducer, root / "campaign-batch-0"); CHECK(retriedCampaignBatch);
  CHECK(retriedCampaignBatch.value().batchSha256 == firstCampaignBatch.value().batchSha256);
  CHECK(authoring::runGenerationBatch(firstCampaignBatch.value().jobs));
  const auto firstCampaignReceipt = authoring::collectGenerationBatchWithReceipt(campaignRepository, campaignProducer,
      firstCampaignBatch.value().jobs, root / "campaign-batch-0/collection.json",
      {"import-generated-batch", firstCampaignBatch.value().batchSha256, "producer", "2026-09-13T00:00:01Z"}); CHECK(firstCampaignReceipt);
  const auto secondCampaignProducer = campaignRepository.recover(); CHECK(secondCampaignProducer);
  const auto secondCampaignBatch = authoring::prepareGenerationCampaignBatch(executablePlan.value(), executableHash, 1U,
      secondCampaignProducer.value(), root / "campaign-batch-1", firstCampaignReceipt.value()); CHECK(secondCampaignBatch);
  const auto secondPreparedJob = authoring::loadGenerationJob(secondCampaignBatch.value().jobs.front().directory,
      secondCampaignBatch.value().jobs.front().manifestSha256); CHECK(secondPreparedJob);
  CHECK(secondPreparedJob.value().expectation.projectStateSha256 == firstCampaignReceipt.value().committedProjectSha256);
  CHECK(secondPreparedJob.value().expectation.projectStateSha256 != core::sha256Hex(production::encodeProductionProject(campaignProducer)));
  CHECK(authoring::runGenerationBatch(secondCampaignBatch.value().jobs));
  CHECK(authoring::collectGenerationBatchWithReceipt(campaignRepository, secondCampaignProducer.value(), secondCampaignBatch.value().jobs,
      root / "campaign-batch-1/collection.json", {"import-generated-batch", secondCampaignBatch.value().batchSha256, "producer", "2026-09-13T00:00:02Z"}));
  CHECK(campaignRepository.recover().value().takes.size() == 2U);
  const auto latestCampaignPointer = core::sha256File(root / "two-batch-producer/project.json"); CHECK(latestCampaignPointer);
  const auto historicalReceipt = authoring::loadVerifiedGenerationBatchReceipt(campaignRepository, campaignProducer,
      firstCampaignBatch.value().jobs, root / "campaign-batch-0/collection.json"); CHECK(historicalReceipt);
  CHECK(historicalReceipt.value().takes.size() == 1U);
  CHECK(historicalReceipt.value().lastDurableGeneration == firstCampaignReceipt.value().committedGeneration);
  CHECK(core::sha256File(root / "two-batch-producer/project.json").value() == latestCampaignPointer.value());
  const auto receiptBytes = core::readTextFileLimited(root / "campaign-batch-0/collection.json", 1024U * 1024U); CHECK(receiptBytes);
  const auto receiptJson = formats::parseJson(receiptBytes.value()); CHECK(receiptJson);
  for (unsigned scenario = 0U; scenario < 3U; ++scenario) {
    auto altered = receiptJson.value();
    if (scenario == 0U) *altered.find("committedProducerSha256") = formats::JsonValue{latestCampaignPointer.value()};
    if (scenario == 1U) *altered.find("takes")->asArray().front().find("audioSha256") = formats::JsonValue{std::string(64U, '0')};
    if (scenario == 2U) altered.asObject().emplace("skipNextBatch", formats::JsonValue{true});
    const auto alteredPath = root / ("altered-receipt-" + std::to_string(scenario) + ".json");
    CHECK(core::durableAtomicWriteTextNew(alteredPath, formats::stringifyJson(altered, true)));
    CHECK(!authoring::loadVerifiedGenerationBatchReceipt(campaignRepository, campaignProducer, firstCampaignBatch.value().jobs, alteredPath));
  }
  CHECK(!campaignRepository.recoverGeneration(firstCampaignReceipt.value().committedGeneration, std::string(64U, '0')));
  CHECK(!campaignRepository.recoverGeneration(999U, firstCampaignReceipt.value().committedProjectSha256));
  auto advancedProducer = multiProducer; advancedProducer.projectId = "advanced-campaign"; advancedProducer.lastDurableGeneration = 0U;
  production::ProductionProjectRepository advancedRepository{root / "advanced-producer"};
  CHECK(advancedRepository.initialize(advancedProducer, {"create", advancedProducer.projectId, "producer", "2026-09-13T00:00:00Z"}));
  const auto advancePlan = authoring::planGenerationCampaign(advancedProducer, campaignTakes, multiResource.value(),
      {.batch = {.maximumJobs = 1U}}); CHECK(advancePlan);
  CHECK(std::filesystem::create_directory(root / "advanced-campaign"));
  const auto advancePath = root / "advanced-campaign/campaign.json";
  CHECK(core::durableAtomicWriteTextNew(advancePath, advancePlan.value()));
  const auto advanceHash = core::sha256Hex(advancePlan.value());
  // A campaign cannot multiply a phone class across the bank before its held-out
  // phrases have rendered audibly; this is that preflight, retained beside it.
  const auto advancePreflight = authoring::runInventoryPreflight(advancePlan.value(), advanceHash,
      root / "advanced-campaign" / "preflight");
  CHECK(advancePreflight);
  if (advancePreflight) CHECK(advancePreflight.value().passed);
  const auto storageCanary = root / "advanced-campaign/storage-canary";
  CHECK(core::durableAtomicWriteTextNew(storageCanary, "x"));
  std::filesystem::resize_file(storageCanary, 8ULL * 1024ULL * 1024ULL * 1024ULL + 1U);
  CHECK(!authoring::advanceGenerationCampaign(advancedRepository, advancePath, advanceHash, "producer", "2026-09-13T00:00:01Z"));
  CHECK(advancedRepository.recover().value().takes.empty());
  CHECK(!std::filesystem::exists(root / "advanced-campaign/batch-0"));
  std::filesystem::rename(storageCanary, root / "held-storage-canary");
  CHECK(std::filesystem::create_directory(root / "storage-scan"));
  CHECK(core::durableAtomicWriteTextNew(root / "storage-scan/a", "abc"));
  CHECK(core::durableAtomicWriteTextNew(root / "storage-scan/b", "de"));
  const auto storageUsage = authoring::inspectCampaignStorage(root / "storage-scan", 5U); CHECK(storageUsage);
  CHECK(storageUsage.value().logicalBytes == 5U); CHECK(storageUsage.value().entries == 2U);
  CHECK(!authoring::inspectCampaignStorage(root / "storage-scan", 4U));
  CHECK(!authoring::inspectCampaignStorage(root / "storage-scan", 5U, 1U));
  CHECK(!authoring::inspectCampaignStorage(root / "storage-scan", 5U, 2U, campaignStop.get_token()));
#if defined(__APPLE__) || defined(__linux__)
  std::filesystem::create_symlink(root / "storage-scan/a", root / "storage-scan/link");
  CHECK(!authoring::inspectCampaignStorage(root / "storage-scan", 100U));
#endif
#if defined(SEAM_TEST_GENERATION_PROBE) && (defined(__APPLE__) || defined(__linux__))
  CHECK(runProcess(SEAM_TEST_GENERATION_PROBE, {"campaign-sigkill", (root / "advanced-producer").string(),
      advancePath.string(), advanceHash, "producer", "2026-09-13T00:00:01Z"}) == 128 + SIGKILL);
#else
  CHECK(!authoring::advanceGenerationCampaign(advancedRepository, advancePath, advanceHash, "producer", "2026-09-13T00:00:01Z",
      {}, [] { return true; }));
#endif
  CHECK(advancedRepository.recover().value().takes.size() == 1U);
  CHECK(!std::filesystem::exists(root / "advanced-campaign/batch-0/collection.json"));
  const auto killedBatchHash = core::sha256File(root / "advanced-campaign/batch-0/batch.json"); CHECK(killedBatchHash);
  const auto killedBatch = authoring::loadGenerationBatch(root / "advanced-campaign/batch-0/batch.json", killedBatchHash.value()); CHECK(killedBatch);
  const auto killedJobDirectory = killedBatch.value().front().directory;
  std::filesystem::rename(killedJobDirectory / "output", killedJobDirectory / "held-output");
  const auto killedProducerBytes = production::encodeProductionProject(advancedRepository.recover().value());
#if defined(SEAM_TEST_VOICEBANK_CLI) && (defined(__APPLE__) || defined(__linux__))
  CHECK(runVoicebankCli({"advance-generation-campaign", (root / "advanced-producer").string(), advancePath.string(),
      advanceHash, "producer", "2026-09-13T00:00:01Z"}) == 0);
#else
  const auto recoveredAdvance = authoring::advanceGenerationCampaign(advancedRepository, advancePath, advanceHash,
      "producer", "2026-09-13T00:00:01Z"); CHECK(recoveredAdvance);
  CHECK(recoveredAdvance.value().completedBatches == 1U); CHECK(!recoveredAdvance.value().complete);
#endif
  CHECK(production::encodeProductionProject(advancedRepository.recover().value()) == killedProducerBytes);
  CHECK(!std::filesystem::exists(killedJobDirectory / "output"));
  CHECK(core::sha256File(root / "advanced-campaign/batch-0/batch.json").value() == killedBatchHash.value());
  CHECK(advancedRepository.recover().value().takes.size() == 1U);
#if defined(SEAM_TEST_VOICEBANK_CLI) && (defined(__APPLE__) || defined(__linux__))
  const std::vector<std::string> advanceArgs{"advance-generation-campaign", (root / "advanced-producer").string(),
      advancePath.string(), advanceHash, "producer", "2026-09-13T00:00:02Z"};
  CHECK(runVoicebankCli(advanceArgs) == 0);
  const auto finishedAdvance = production::encodeProductionProject(advancedRepository.recover().value());
  CHECK(advancedRepository.recover().value().takes.size() == 2U);
  CHECK(runVoicebankCli(advanceArgs) == 0);
  CHECK(production::encodeProductionProject(advancedRepository.recover().value()) == finishedAdvance);
  auto externalAfterAdvance = advancedRepository.recover().value();
  CHECK(advancedRepository.save(externalAfterAdvance, {"save", "external", "producer", "2026-09-13T00:00:03Z"}));
  CHECK(runVoicebankCli(advanceArgs) != 0);
#endif
  CHECK(!authoring::prepareGenerationCampaignBatch(executablePlan.value(), executableHash, 0U,
      secondCampaignProducer.value(), root / "stale-campaign-preparation"));
  CHECK(!std::filesystem::exists(root / "stale-campaign-preparation"));
#if defined(SEAM_TEST_VOICEBANK_CLI) && (defined(__APPLE__) || defined(__linux__))
  CHECK(voice_design::saveVoiceRecipeFile(root / "campaign-recipe.json", multiRecipe));
  const std::vector<std::string> draftCampaignArgs{"draft-generation-campaign", (root / "multi-style-inventory").string(),
      (root / "campaign-recipe.json").string(), (root / "campaign-draft.json").string(), "take-sa", "take-sa-soft"};
  CHECK(runVoicebankCli(draftCampaignArgs) == 0);
  CHECK(runVoicebankCli(draftCampaignArgs) != 0);
  const auto draftCampaignHash = core::sha256File(root / "campaign-draft.json"); CHECK(draftCampaignHash);
  CHECK(runVoicebankCli({"inspect-generation-campaign", (root / "campaign-draft.json").string(), draftCampaignHash.value()}) == 0);
  CHECK(runVoicebankCli({"inspect-generation-campaign", (root / "campaign-draft.json").string(), std::string(64U, '0')}) != 0);
  const std::vector<std::string> publishCampaignArgs{"plan-generation-campaign", (root / "multi-style-inventory").string(),
      (root / "campaign-draft.json").string(), draftCampaignHash.value(), (root / "published-campaign").string()};
  CHECK(runVoicebankCli(publishCampaignArgs) == 0);
  CHECK(core::sha256File(root / "published-campaign/campaign.json").value() == draftCampaignHash.value());
  CHECK(runVoicebankCli(publishCampaignArgs) != 0);
  CHECK(production::encodeProductionProject(multiRepository.recover().value()) == beforeCampaign);
#endif
  const auto neutralJob = authoring::prepareInventoryGenerationJob(root / "multi-neutral.seam", root / "multi-neutral-job",
      multiProducer, "take-sa", {multiResource.value(), "neutral"}); CHECK(neutralJob);
  const auto softJob = authoring::prepareInventoryGenerationJob(root / "multi-soft.seam", root / "multi-soft-job",
      multiProducer, "take-sa-soft", {multiResource.value(), "soft"}); CHECK(softJob);
  const std::vector<authoring::GenerationJobReference> multiJobs{
      {root / "multi-neutral-job", neutralJob.value().manifestSha256}, {root / "multi-soft-job", softJob.value().manifestSha256}};
  const auto admittedStyles = authoring::inspectGenerationBatch(multiJobs); CHECK(admittedStyles);
  CHECK(admittedStyles.value().size() == 2U);
  CHECK(!authoring::inspectGenerationBatch(std::vector<authoring::GenerationJobReference>{multiJobs.front(), multiJobs.front()}));
  CHECK(!authoring::inspectGenerationBatch(multiJobs, {.maximumFrames = 47999U}));
  CHECK(authoring::runGenerationBatch(multiJobs));
  const auto multiHash = authoring::saveGenerationBatch(root / "multi-batch.json", multiJobs); CHECK(multiHash);
  const production::ProductionJournalEvent multiEvent{"import-generated-batch", multiHash.value(), "producer", "2026-09-13T00:00:01Z"};
  const auto receiptPath = root / "multi-collection-receipt.json";
  const auto interruptedCollection = authoring::collectGenerationBatchWithReceipt(multiRepository, multiProducer, multiJobs,
      receiptPath, multiEvent, {}, {}, [] { return true; });
  CHECK(!interruptedCollection);
  CHECK(!std::filesystem::exists(receiptPath));
  const auto committedBeforeReceipt = production::encodeProductionProject(multiRepository.recover().value());
  CHECK(multiRepository.recover().value().takes.size() == 2U);
  const auto committedGeneration = multiRepository.recover().value().lastDurableGeneration;
  CHECK(core::durableAtomicWriteText(root / "multi-style-inventory/project.json", "interrupted-pointer-fixture"));
  CHECK(!multiRepository.reconcileCurrentPointer(committedGeneration, std::string(64U, '0')));
  std::stop_source pointerStop; pointerStop.request_stop();
  CHECK(!multiRepository.reconcileCurrentPointer(committedGeneration, core::sha256Hex(committedBeforeReceipt), pointerStop.get_token()));
  {
    core::ExclusiveFileLock competingWriter;
    CHECK(competingWriter.acquire(root / "multi-style-inventory/.writer.lock"));
    CHECK(!multiRepository.reconcileCurrentPointer(committedGeneration, core::sha256Hex(committedBeforeReceipt)));
  }
  std::filesystem::rename(root / "multi-neutral-job/output", root / "multi-neutral-job/held-output");
  std::filesystem::rename(root / "multi-soft-job/output", root / "multi-soft-job/held-output");
  const auto multiCollected = authoring::collectGenerationBatchWithReceipt(multiRepository, multiProducer, multiJobs,
      receiptPath, multiEvent);
  CHECK(multiCollected);
  CHECK(multiCollected.value().durabilityConfirmed);
  CHECK(core::sha256File(root / "multi-style-inventory/project.json").value() == core::sha256Hex(committedBeforeReceipt));
  CHECK(std::filesystem::is_regular_file(receiptPath));
  CHECK(!std::filesystem::exists(root / "multi-neutral-job/output"));
  CHECK(!std::filesystem::exists(root / "multi-soft-job/output"));
  CHECK(production::encodeProductionProject(multiRepository.recover().value()) == committedBeforeReceipt);
  const auto retainedReceiptHash = core::sha256File(receiptPath); CHECK(retainedReceiptHash);
  CHECK(authoring::collectGenerationBatchWithReceipt(multiRepository, multiProducer, multiJobs, receiptPath, multiEvent));
  CHECK(core::sha256File(receiptPath).value() == retainedReceiptHash.value());
  CHECK(core::durableAtomicWriteTextNew(root / "false-receipt.json", "{}"));
  CHECK(!authoring::collectGenerationBatchWithReceipt(multiRepository, multiProducer, multiJobs, root / "false-receipt.json", multiEvent));
  const auto multiRecovered = multiRepository.recover(); CHECK(multiRecovered);
  CHECK(multiRecovered.value().takes.size() == 2U);
  CHECK(multiRecovered.value().takes[0].style == "neutral");
  CHECK(multiRecovered.value().takes[1].style == "soft");
#if defined(SEAM_TEST_VOICEBANK_CLI) && (defined(__APPLE__) || defined(__linux__))
  auto staleCampaignArgs = publishCampaignArgs;
  staleCampaignArgs.back() = (root / "stale-campaign").string();
  CHECK(runVoicebankCli(staleCampaignArgs) != 0);
  CHECK(!std::filesystem::exists(root / "stale-campaign"));
#endif
  CHECK(std::none_of(multiRecovered.value().unitAssignments.begin(), multiRecovered.value().unitAssignments.end(),
      [](const auto& row) { return row.markerReviewed || row.pitchReviewed; }));
  auto externallyChanged = multiRecovered.value();
  CHECK(multiRepository.save(externallyChanged, {"save", "unrelated-change", "producer", "2026-09-13T00:00:02Z"}));
  CHECK(!authoring::collectGenerationBatchWithReceipt(multiRepository, multiProducer, multiJobs,
      root / "after-external-change.json", multiEvent));
  CHECK(!std::filesystem::exists(root / "after-external-change.json"));
  production::ProductionProjectRepository styleRepository{root / "style-owned-generation"};
  CHECK(styleRepository.initialize(styleProducer, {"create", styleProducer.projectId, "producer", "2026-09-13T00:00:00Z"}));
  const auto styleJobDirectory = root / "style-owned-job";
  const auto styleJob = authoring::prepareGenerationJobFromScore(styleJobDirectory, "style-owned-job",
      root / "shared-score.seam", trackId, regionId, styleProducer, "take-sa");
  CHECK(styleJob);
  CHECK(styleJob.value().expectation.style == "neutral");
  CHECK(styleJob.value().expectation.language == "ja");
  const auto languageBoundJob = authoring::loadGenerationJob(styleJobDirectory, styleJob.value().manifestSha256);
  CHECK(languageBoundJob);
  CHECK(languageBoundJob.value().expectation == styleJob.value().expectation);
  const auto languageJson = formats::parseJson(production::encodeGenerationImportExpectation(styleJob.value().expectation).value());
  CHECK(languageJson);
  for (unsigned scenario = 0; scenario < 4U; ++scenario) {
    auto bad = languageJson.value();
    if (scenario == 0U) bad.asObject().erase("language");
    if (scenario == 1U) *bad.find("language") = formats::JsonValue{""};
    if (scenario == 2U) *bad.find("language") = formats::JsonValue{"xx"};
    if (scenario == 3U) *bad.find("schemaVersion") = formats::JsonValue{std::int64_t{1}};
    const auto bytes = formats::stringifyJson(bad);
    const auto path = root / ("bad-language-expectation-" + std::to_string(scenario) + ".json");
    CHECK(core::durableAtomicWriteTextNew(path, bytes));
    CHECK(!production::loadGenerationImportExpectation(path, core::sha256Hex(bytes)));
  }
  CHECK(authoring::loadGenerationJob(styleJobDirectory, styleJob.value().manifestSha256));
  auto wrongStyleProducer = styleProducer;
  for (auto& row : wrongStyleProducer.unitAssignments) row.style = "soft";
  CHECK(!authoring::prepareGenerationJobFromScore(root / "wrong-style-job", "wrong-style-job",
      root / "shared-score.seam", trackId, regionId, wrongStyleProducer, "take-sa"));
  CHECK(!std::filesystem::exists(root / "wrong-style-job"));
  CHECK(authoring::runGenerationJob(styleJobDirectory, styleJob.value().manifestSha256));
#if defined(SEAM_TEST_VOICEBANK_CLI) && (defined(__APPLE__) || defined(__linux__))
  const auto styleOutput = styleJobDirectory / "output/candidates" / (trackId.toString() + "-" + regionId.toString());
  const auto styleExpectationHash = core::sha256File(styleJobDirectory / "expectation.json"); CHECK(styleExpectationHash);
  const std::vector<std::string> styleCollectArgs{"import-generated", (root / "style-owned-generation").string(),
      styleOutput.string() + ".json", styleOutput.string() + ".wav", (styleJobDirectory / "recipe.json").string(),
      (styleJobDirectory / "expectation.json").string(), styleExpectationHash.value(), "producer", "2026-09-13T00:01:00Z"};
  auto wrongLanguage = styleJob.value().expectation;
  wrongLanguage.language = "en";
  const auto wrongLanguagePath = root / "wrong-language-expectation.json";
  const auto wrongLanguageHash = production::saveGenerationImportExpectation(wrongLanguagePath, wrongLanguage); CHECK(wrongLanguageHash);
  auto wrongLanguageArgs = styleCollectArgs;
  wrongLanguageArgs[5] = wrongLanguagePath.string(); wrongLanguageArgs[6] = wrongLanguageHash.value();
  CHECK(runVoicebankCli(wrongLanguageArgs) != 0);
  CHECK(styleRepository.recover().value().takes.empty());
  CHECK(runVoicebankCli(styleCollectArgs) == 0);
  const auto styleCollected = styleRepository.recover(); CHECK(styleCollected);
  CHECK(styleCollected.value().takes.size() == 1U);
  CHECK(styleCollected.value().takes.front().style == "neutral");
  CHECK(styleCollected.value().takes.front().state == production::UnitQueueState::MarkerReview);
  CHECK(runVoicebankCli(styleCollectArgs) == 0);
  CHECK(production::encodeProductionProject(styleRepository.recover().value()) == production::encodeProductionProject(styleCollected.value()));
#endif
  auto designerRecipe = recipe; designerRecipe.phonation.aspiration = 0.35;
  const auto designerResource = voice_design::freezeVoiceRecipeResource(designerRecipe); CHECK(designerResource);
  const auto scoreBeforeDesigner = core::readTextFileLimited(root / "shared-score.seam", 16U * 1024U * 1024U); CHECK(scoreBeforeDesigner);
  const auto scoreDigest = core::sha256Hex(scoreBeforeDesigner.value());
  const auto designerJob = authoring::prepareGenerationJobFromScore(root / "designer-selected-job", "designer-selected",
      root / "shared-score.seam", trackId, regionId, producer, "take-sa", {}, scoreDigest,
      authoring::GenerationRecipeSelection{designerResource.value(), "neutral"});
  CHECK(designerJob); CHECK(designerJob.value().expectation.recipeHash == designerResource.value().identity.contentHash);
  const auto loadedDesignerJob = authoring::loadGenerationJob(root / "designer-selected-job", designerJob.value().manifestSha256); CHECK(loadedDesignerJob);
  const auto designerRendered = rendering::PhraseRenderPipeline{}.render(loadedDesignerJob.value().snapshot); CHECK(designerRendered);
  CHECK(designerRendered.value().rendered.audio.samples != candidate.value().audio->interleaved);
  CHECK(core::readTextFileLimited(root / "shared-score.seam", 16U * 1024U * 1024U).value() == scoreBeforeDesigner.value());
  CHECK(voice_design::loadVoiceRecipeResource(root / "shared-score-recipe.json").value().identity == resource.value().identity);
  CHECK(production::encodeProductionProject(producer) == producerBeforePreparation);
  CHECK(!authoring::prepareGenerationJobFromScore(root / "invalid-designer-job", "invalid-designer",
      root / "shared-score.seam", trackId, regionId, producer, "take-sa", {}, scoreDigest,
      authoring::GenerationRecipeSelection{designerResource.value(), "missing-style"}));
  CHECK(!authoring::prepareGenerationJobFromScore(root / "invalid-designer-job", "invalid-designer",
      root / "shared-score.seam", trackId, regionId, producer, "take-sa", {}, std::string(64U, '0'),
      authoring::GenerationRecipeSelection{designerResource.value(), "neutral"}));
  CHECK(!std::filesystem::exists(root / "invalid-designer-job"));
  auto plainScore = savedScore; plainScore.findVocalTrack(trackId)->proceduralRecipe.reset();
  CHECK(formats::ProjectJsonCodec{}.save(plainScore, root / "plain-designer-score.seam"));
  CHECK(!authoring::prepareGenerationJobFromScore(root / "plain-designer-job", "plain-designer",
      root / "plain-designer-score.seam", trackId, regionId, producer, "take-sa"));
  CHECK(authoring::prepareGenerationJobFromScore(root / "plain-designer-job", "plain-designer",
      root / "plain-designer-score.seam", trackId, regionId, producer, "take-sa", {}, {},
      authoring::GenerationRecipeSelection{designerResource.value(), "neutral"}));
  auto preparationProducer = producer; preparationProducer.lastDurableGeneration = 0U;
  production::ProductionProjectRepository preparationRepository{root / "studio-preparation"};
  CHECK(preparationRepository.initialize(preparationProducer, {.action = "create", .subjectId = preparationProducer.projectId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-07T00:00:00Z"}));
  native_ui::VoicebankStudioController preparationStudio;
  CHECK(preparationStudio.openProductionProject(root / "studio-preparation", producer.inventorySha256, "producer"));
  for (const auto width:{720.0,1040.0,1600.0}) {
    const auto controls=native_ui::studioGenerationControls(preparationStudio,width,false);
    CHECK(controls.size()==6U);
    for (const auto& control:controls) {
      // Planning only needs planned take ids; a campaign run needs an identity
      // this controller recorded, which does not exist until a campaign is planned.
      CHECK(control.enabled==(control.id!="run-campaign"));
      CHECK(control.bounds.x>=294.0);
      CHECK(control.bounds.x+control.bounds.width<=width-280.0);
      CHECK(control.bounds.y>=268.0); CHECK(control.bounds.y+control.bounds.height<=322.0);
    }
    CHECK(controls[0].bounds.x+controls[0].bounds.width<controls[1].bounds.x);
    CHECK(controls[0].bounds.y+controls[0].bounds.height<=controls[2].bounds.y);
    for (const auto& control:native_ui::studioGenerationControls(preparationStudio,width,true)) CHECK(!control.enabled);
  }
  const auto drainPreparation = [&]() -> core::Result<void> {
    for (unsigned attempt = 0U; attempt < 3000U; ++attempt) {
      const auto polled = preparationStudio.pollProceduralCandidateImport();
      if (!polled) return polled;
      if (!preparationStudio.proceduralImportBusy()) return core::success();
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return core::failure(core::ErrorCode::Internal, "Preparation worker exceeded test budget");
  };
  CHECK(preparationStudio.beginGenerationScoreInspection(root / "shared-score.seam"));
  CHECK(preparationStudio.proceduralImportBusy()); CHECK(!preparationStudio.save());
  const auto busyControls=native_ui::studioGenerationControls(preparationStudio,720.0,false);
  CHECK(busyControls.size()==6U);
  for (const auto& control:busyControls) CHECK(control.enabled==(control.id=="cancel"));
  const auto controlById=[](const auto& controls,std::string_view id) {
    return std::find_if(controls.begin(),controls.end(),[&](const auto& control){return control.id==id;});
  };
  CHECK(controlById(busyControls,"cancel")->label=="Cancel work");
  CHECK(drainPreparation()); CHECK(preparationStudio.status() == "SCORE READY / SHIFT-P PREPARE");
  const auto idleControls=native_ui::studioGenerationControls(preparationStudio,720.0,false);
  CHECK(controlById(idleControls,"batch")->label=="Run batch");
  const auto scoreSelection = preparationStudio.takeGenerationScoreSelection(); CHECK(scoreSelection);
  CHECK(!preparationStudio.takeGenerationScoreSelection());
  const auto beforeCancelledInspection=preparationStudio.status();
  CHECK(preparationStudio.beginGenerationScoreInspection(root/"shared-score.seam"));
  // Observe worker completion without polling/adopting, then cancel: this
  // exercises the late-stop boundary, not merely the worker's early stop checks.
  for (unsigned attempt=0U;attempt<3000U && !preparationStudio.proceduralImportResultReady();++attempt)
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  CHECK(preparationStudio.proceduralImportResultReady());
  preparationStudio.cancelProceduralCandidateImport();
  const auto cancelledInspection=preparationStudio.pollProceduralCandidateImport();
  CHECK(!cancelledInspection); CHECK(cancelledInspection.error().code==core::ErrorCode::Conflict);
  CHECK(!preparationStudio.proceduralImportBusy()); CHECK(!preparationStudio.generationScoreSelection());
  CHECK(preparationStudio.status()==beforeCancelledInspection);
  CHECK(preparationStudio.beginGenerationScoreInspection(root / "plain-designer-score.seam"));
  CHECK(!drainPreparation()); CHECK(!preparationStudio.takeGenerationScoreSelection());
  CHECK(preparationStudio.beginGenerationScoreInspection(root / "plain-designer-score.seam",
      authoring::GenerationRecipeSelection{designerResource.value(), "neutral"}));
  CHECK(drainPreparation()); CHECK(preparationStudio.status() == "DESIGNER SNAPSHOT READY / SHIFT-P");
  CHECK(!preparationStudio.selectUnit(9999U)); CHECK(preparationStudio.generationScoreSelection());
  CHECK(preparationStudio.selectUnit(0U)); CHECK(preparationStudio.generationScoreSelection());
  CHECK(preparationStudio.status()=="DESIGNER SNAPSHOT READY / SHIFT-P");
  const auto draftScoreSelection = preparationStudio.generationScoreSelection(); CHECK(draftScoreSelection);
  // Opening/cancelling either dialog only reads the selection: frozen recipe,
  // score identity and the ready prompt must survive a later retry.
  CHECK(preparationStudio.generationScoreSelection());
  CHECK(preparationStudio.generationScoreSelection()->sha256==draftScoreSelection->sha256);
  CHECK(preparationStudio.status()=="DESIGNER SNAPSHOT READY / SHIFT-P");
  CHECK(draftScoreSelection->selectedRecipe);
  CHECK(draftScoreSelection->selectedRecipe->resource.identity == designerResource.value().identity);
  CHECK(draftScoreSelection->regions.size() == 1U); CHECK(draftScoreSelection->regions.front().regionId == regionId);
  CHECK(preparationStudio.beginGenerationPreparation(draftScoreSelection->path, trackId, regionId,
      root / "studio-draft-selection-job", "studio-draft-selection", draftScoreSelection->sha256, draftScoreSelection->selectedRecipe));
  CHECK(!preparationStudio.generationScoreSelection());
  CHECK(preparationStudio.status()=="PREPARING JOB / ESC CANCEL");
  for (unsigned attempt=0U;attempt<3000U && !preparationStudio.proceduralImportResultReady();++attempt)
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  CHECK(preparationStudio.proceduralImportResultReady());
  // Unlike read-only score inspection, a completed preparation has already
  // published its job. A late cancel must not hide that durable outcome.
  preparationStudio.cancelProceduralCandidateImport();
  CHECK(drainPreparation());
  CHECK(preparationStudio.status()=="JOB PREPARED / NOT GENERATED");
  const auto selectedDraftReference = authoring::loadGenerationJobReference(root / "studio-draft-selection-job/job.seamjob"); CHECK(selectedDraftReference);
  CHECK(authoring::loadGenerationJob(selectedDraftReference.value().directory, selectedDraftReference.value().manifestSha256).value().expectation.recipeHash == designerResource.value().identity.contentHash);
  CHECK(production::encodeProductionProject(*preparationStudio.productionProject()) == producerBeforePreparation);
  // A -> B -> A must not revive a selection from before the assignment change.
  auto selectionProducer=preparationProducer;
  selectionProducer.projectId="selection-retention";
  selectionProducer.unitAssignments.push_back({.coverageKey="cv:sa",.pitchLayer=70,.promptId="second-row",.plannedTakeId="second-take"});
  production::ProductionProjectRepository selectionRepository{root/"selection-retention"};
  CHECK(selectionRepository.initialize(selectionProducer,{.action="create",.subjectId=selectionProducer.projectId,
      .operatorId="producer",.occurredAtUtc="2026-09-07T00:00:00Z"}));
  native_ui::VoicebankStudioController selectionStudio;
  CHECK(selectionStudio.openProductionProject(root/"selection-retention",producer.inventorySha256,"producer"));
  CHECK(selectionStudio.beginGenerationScoreInspection(root/"plain-designer-score.seam",
      authoring::GenerationRecipeSelection{designerResource.value(),"neutral"}));
  for (unsigned attempt=0U;attempt<3000U && selectionStudio.proceduralImportBusy();++attempt) {
    CHECK(selectionStudio.pollProceduralCandidateImport());
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  CHECK(!selectionStudio.proceduralImportBusy()); CHECK(selectionStudio.generationScoreSelection());
  CHECK(selectionStudio.selectUnit(1U)); CHECK(!selectionStudio.generationScoreSelection());
  CHECK(selectionStudio.selectUnit(0U)); CHECK(!selectionStudio.generationScoreSelection());
  CHECK(scoreSelection->regions.size() == 1U);
  CHECK(scoreSelection->regions.front().trackId == trackId);
  CHECK(scoreSelection->regions.front().regionId == regionId);
  CHECK(preparationStudio.validateProductionImportContext(scoreSelection->epoch, scoreSelection->generation, scoreSelection->selectedIndex));
  const auto selectedScoreBytes = core::readTextFileLimited(root / "shared-score.seam", 16U * 1024U * 1024U); CHECK(selectedScoreBytes);
  CHECK(core::durableAtomicWriteText(root / "shared-score.seam", selectedScoreBytes.value() + "\n"));
  CHECK(preparationStudio.beginGenerationPreparation(scoreSelection->path, trackId, regionId,
      root / "changed-score-preparation", "changed-score", scoreSelection->sha256));
  CHECK(!drainPreparation()); CHECK(!std::filesystem::exists(root / "changed-score-preparation"));
  CHECK(core::durableAtomicWriteText(root / "shared-score.seam", selectedScoreBytes.value()));
  CHECK(preparationStudio.beginGenerationPreparation(root / "shared-score.seam", trackId, regionId,
      root / "studio-prepared-job", "studio-prepared", scoreSelection->sha256,
      authoring::GenerationRecipeSelection{designerResource.value(), "neutral"}));
  CHECK(preparationStudio.proceduralImportBusy()); CHECK(!preparationStudio.save());
  CHECK(!preparationStudio.selectUnit(0U));
  CHECK(drainPreparation()); CHECK(preparationStudio.status() == "JOB PREPARED / NOT GENERATED");
  const auto preparedReference = authoring::loadGenerationJobReference(root / "studio-prepared-job/job.seamjob");
  CHECK(preparedReference);
  CHECK(authoring::loadGenerationJob(preparedReference.value().directory, preparedReference.value().manifestSha256));
  CHECK(authoring::loadGenerationJob(preparedReference.value().directory, preparedReference.value().manifestSha256).value().expectation.recipeHash == designerResource.value().identity.contentHash);
  CHECK(!std::filesystem::exists(root / "studio-prepared-job/output"));
  CHECK(production::encodeProductionProject(*preparationStudio.productionProject()) == producerBeforePreparation);
  CHECK(production::encodeProductionProject(preparationRepository.recover().value()) == producerBeforePreparation);
  auto newerPreparation = preparationRepository.recover().value();
  newerPreparation.unitAssignments.front().plannedTakeId = "changed-take";
  CHECK(preparationRepository.save(newerPreparation, {.action = "save", .subjectId = newerPreparation.projectId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-07T00:00:01Z"}));
  CHECK(preparationStudio.beginGenerationPreparation(root / "shared-score.seam", trackId, regionId,
      root / "stale-prepared-job", "stale-prepared"));
  CHECK(!drainPreparation()); CHECK(!std::filesystem::exists(root / "stale-prepared-job"));
  CHECK(production::encodeProductionProject(*preparationStudio.productionProject()) == producerBeforePreparation);
  std::stop_source preparationStop; preparationStop.request_stop();
  CHECK(!authoring::prepareGenerationJobFromScore(root / "cancelled-preparation", "cancelled",
      root / "shared-score.seam", trackId, regionId, producer, "take-sa", preparationStop.get_token()));
  CHECK(!std::filesystem::exists(root / "cancelled-preparation"));
  CHECK(!authoring::prepareGenerationJobFromScore(root / "shared-score-job", "shared-score",
      root / "shared-score.seam", trackId, regionId, producer, "take-sa"));
  CHECK(!authoring::prepareGenerationJobFromScore(root / "bad-score-job", "shared-score",
      root / "shared-score.seam", trackId, domain::RegionId{999U}, producer, "take-sa"));
  CHECK(!authoring::prepareGenerationJobFromScore(root / "bad-score-job", "shared-score",
      root / "shared-score.seam", trackId, regionId, producer, "not-planned"));
  auto ambiguousProducer = producer;
  ambiguousProducer.unitAssignments.push_back(producer.unitAssignments.front());
  CHECK(!authoring::prepareGenerationJobFromScore(root / "bad-score-job", "shared-score",
      root / "shared-score.seam", trackId, regionId, ambiguousProducer, "take-sa"));
  auto mismatchedRecipe = recipe; mismatchedRecipe.id = "changed-recipe";
  CHECK(voice_design::saveVoiceRecipeFile(root / "shared-score-recipe.json", mismatchedRecipe));
  CHECK(!authoring::prepareGenerationJobFromScore(root / "bad-score-job", "shared-score",
      root / "shared-score.seam", trackId, regionId, producer, "take-sa"));
  CHECK(!std::filesystem::exists(root / "bad-score-job"));
  CHECK(production::encodeProductionProject(producer) == producerBeforePreparation);
  // The prepared job owns the original recipe, independent of the subsequently edited source file.
  CHECK(authoring::loadGenerationJob(root / "shared-score-job", preparedScore.value().manifestSha256));
  const auto jobDirectory = root / "generation-job";
  const auto job = authoring::prepareGenerationJob(jobDirectory, "job-sa", snapshot.value(), producer, generatedTake);
  if (!job) throw test::Failure{job.error().message};
  CHECK(job.value().jobId == "job-sa");
  CHECK(job.value().snapshot.contentHash == snapshot.value().contentHash);
  CHECK(job.value().snapshot.sourceProjectId == snapshot.value().sourceProjectId);
  CHECK(!authoring::prepareGenerationJob(jobDirectory, "job-sa", snapshot.value(), producer, generatedTake));
  const auto reloadedJob = authoring::loadGenerationJob(jobDirectory, job.value().manifestSha256); CHECK(reloadedJob);
  CHECK(reloadedJob.value().expectation == job.value().expectation);
  CHECK(rendering::PhraseRenderPipeline{}.render(reloadedJob.value().snapshot).value().rendered.audio.samples == candidate.value().audio->interleaved);
  CHECK(!authoring::loadGenerationJob(jobDirectory, std::string(64U, '0')));
  const auto frozenScore = core::readTextFileLimited(jobDirectory / "project.json", 16U * 1024U * 1024U); CHECK(frozenScore);
  CHECK(core::durableAtomicWriteText(jobDirectory / "project.json", frozenScore.value() + " "));
  CHECK(!authoring::loadGenerationJob(jobDirectory, job.value().manifestSha256));
  CHECK(core::durableAtomicWriteText(jobDirectory / "project.json", frozenScore.value()));
  CHECK(authoring::loadGenerationJob(jobDirectory, job.value().manifestSha256));
  std::filesystem::rename(jobDirectory / "recipe.json", jobDirectory / "recipe-held.json");
  CHECK(!authoring::loadGenerationJob(jobDirectory, job.value().manifestSha256));
  std::filesystem::rename(jobDirectory / "recipe-held.json", jobDirectory / "recipe.json");
  CHECK(authoring::loadGenerationJob(jobDirectory, job.value().manifestSha256));
  std::stop_source cancelledJob; cancelledJob.request_stop();
  CHECK(!authoring::runGenerationJob(jobDirectory, job.value().manifestSha256, cancelledJob.get_token()));
  CHECK(!std::filesystem::exists(jobDirectory / "output"));
  {
    core::ExclusiveFileLock busy;
    CHECK(busy.acquire(jobDirectory / ".worker.lock"));
    CHECK(!authoring::runGenerationJob(jobDirectory, job.value().manifestSha256));
#if defined(SEAM_TEST_VOICEBANK_CLI) && (defined(__APPLE__) || defined(__linux__))
    CHECK(runVoicebankCli({"run-generation", jobDirectory.string(), job.value().manifestSha256}) != 0);
#endif
    CHECK(!std::filesystem::exists(jobDirectory / "output"));
  }
  const auto executed = authoring::runGenerationJob(jobDirectory, job.value().manifestSha256); CHECK(executed);
  CHECK(!executed.value().reused);
  const auto modificationTime = std::filesystem::last_write_time(executed.value().audioPath);
  const auto resumed = authoring::runGenerationJob(jobDirectory, job.value().manifestSha256); CHECK(resumed);
  CHECK(resumed.value().reused); CHECK(resumed.value().audioSha256 == executed.value().audioSha256);
  CHECK(std::filesystem::last_write_time(executed.value().audioPath) == modificationTime);
#if defined(SEAM_TEST_VOICEBANK_CLI) && (defined(__APPLE__) || defined(__linux__))
  CHECK(runVoicebankCli({"run-generation", jobDirectory.string(), job.value().manifestSha256}) == 0);
  CHECK(std::filesystem::last_write_time(executed.value().audioPath) == modificationTime);
#endif
  const auto generatedAudio = core::readFileBytesLimited(executed.value().audioPath, 1024U * 1024U); CHECK(generatedAudio);
  auto alteredAudio = generatedAudio.value(); alteredAudio.push_back(std::byte{0});
  CHECK(core::durableAtomicWrite(executed.value().audioPath, alteredAudio));
  CHECK(!authoring::runGenerationJob(jobDirectory, job.value().manifestSha256));
  CHECK(core::readFileBytesLimited(executed.value().audioPath, 1024U * 1024U).value() == alteredAudio);
  CHECK(core::durableAtomicWrite(executed.value().audioPath, generatedAudio.value()));
  CHECK(authoring::runGenerationJob(jobDirectory, job.value().manifestSha256).value().reused);
  CHECK(repository.recover().value().takes.empty());
  const auto interruptedDirectory = root / "interrupted-return-job";
  const auto interruptedJob = authoring::prepareGenerationJob(interruptedDirectory, "interrupted-return", snapshot.value(), producer, generatedTake); CHECK(interruptedJob);
  CHECK(!authoring::runGenerationJob(interruptedDirectory, interruptedJob.value().manifestSha256, {},
      [](auto phase) { return phase == authoring::ExportPublicationPhase::DestinationPublished; }));
  CHECK(authoring::runGenerationJob(interruptedDirectory, interruptedJob.value().manifestSha256));
#if defined(SEAM_TEST_GENERATION_PROBE) && (defined(__APPLE__) || defined(__linux__))
  for (unsigned phase = 0U; phase < 5U; ++phase) {
    const auto phaseDirectory = root / ("crash-job-" + std::to_string(phase));
    const auto phaseJob = authoring::prepareGenerationJob(phaseDirectory, "crash-job-" + std::to_string(phase), snapshot.value(), producer, generatedTake); CHECK(phaseJob);
    CHECK(runProcess(SEAM_TEST_GENERATION_PROBE, {phaseDirectory.string(), phaseJob.value().manifestSha256, std::to_string(phase)}) == 86);
    const auto resumedOutput = authoring::runGenerationJob(phaseDirectory, phaseJob.value().manifestSha256); CHECK(resumedOutput);
    CHECK(resumedOutput.value().reused == (phase >= 3U));
    CHECK(resumedOutput.value().audioSha256 == executed.value().audioSha256);
    CHECK(authoring::runGenerationJob(phaseDirectory, phaseJob.value().manifestSha256).value().reused);
    CHECK(repository.recover().value().takes.empty());
  }
  for (const auto* signal : {"sigint", "sigterm"}) {
    const auto signalDirectory = root / (std::string{"signal-job-"} + signal);
    const auto signalJob = authoring::prepareGenerationJob(signalDirectory, std::string{"signal-job-"} + signal,
        snapshot.value(), producer, generatedTake); CHECK(signalJob);
    CHECK(runProcess(SEAM_TEST_GENERATION_PROBE, {signalDirectory.string(), signalJob.value().manifestSha256, signal}) == 0);
    const bool duringCommit = std::string_view{signal} == "sigterm";
    CHECK(std::filesystem::exists(signalDirectory / "output") == duringCommit);
    const auto afterSignal = authoring::runGenerationJob(signalDirectory, signalJob.value().manifestSha256); CHECK(afterSignal);
    CHECK(afterSignal.value().reused == duringCommit);
    CHECK(afterSignal.value().audioSha256 == executed.value().audioSha256);
    CHECK(repository.recover().value().takes.empty());
  }
#endif
  auto expectation = production::captureGenerationImportExpectation(producer, generatedTake, resource.value(),
      "neutral", candidate.value().renderContentHash, candidate.value().sampleRate, candidate.value().frameCount); CHECK(expectation);
  const auto requestPath = root / "generation-expectation.json";
  const auto requestHash = production::saveGenerationImportExpectation(requestPath, expectation.value()); CHECK(requestHash);
  CHECK(!production::saveGenerationImportExpectation(requestPath, expectation.value()));
  const auto restored = production::loadGenerationImportExpectation(requestPath, requestHash.value()); CHECK(restored);
  CHECK(restored.value() == expectation.value());
  CHECK(!production::loadGenerationImportExpectation(requestPath, std::string(64U, '0')));
  const auto requestText = production::encodeGenerationImportExpectation(expectation.value()); CHECK(requestText);
  const auto requestJson = formats::parseJson(requestText.value()); CHECK(requestJson);
  for (unsigned scenario = 0U; scenario < 6U; ++scenario) {
    auto bad = requestJson.value();
    if (scenario == 0U) *bad.find("schemaVersion") = formats::JsonValue{std::int64_t{2}};
    if (scenario == 1U) *bad.find("sampleRate") = formats::JsonValue{std::int64_t{0x100000000LL + 48000}};
    if (scenario == 2U) *bad.find("takeId") = formats::JsonValue{"bad\nname"};
    if (scenario == 3U) bad.asObject().erase("style");
    if (scenario == 4U) *bad.find("frameCount") = formats::JsonValue{std::int64_t{0}};
    if (scenario == 5U) bad.asObject().emplace("extra", formats::JsonValue{true});
    const auto bytes = formats::stringifyJson(bad);
    const auto path = root / ("bad-generation-" + std::to_string(scenario) + ".json");
    CHECK(core::durableAtomicWriteTextNew(path, bytes));
    CHECK(!production::loadGenerationImportExpectation(path, core::sha256Hex(bytes)));
  }
  expectation = restored;
  const production::ProductionJournalEvent generatedEvent{.action = "import-procedural", .subjectId = "take-sa",
      .operatorId = "producer", .occurredAtUtc = "2026-09-07T00:00:01Z"};
  const auto beforeGeneration = production::encodeProductionProject(producer);
  for (unsigned scenario = 0U; scenario < 4U; ++scenario) {
    auto wrong = expectation.value();
    if (scenario == 0U) wrong.renderContentHash = std::string(64U, '0');
    if (scenario == 1U) --wrong.frameCount;
    if (scenario == 2U) wrong.style = "other";
    if (scenario == 3U) wrong.takeId = "other-take";
    CHECK(!repository.importProceduralCandidate(producer, prefix.string() + ".json", prefix.string() + ".wav",
        resource.value(), generatedTake, generatedEvent, {}, &wrong));
    CHECK(production::encodeProductionProject(producer) == beforeGeneration);
    CHECK(std::filesystem::is_empty(root / "producer/assets"));
  }
  producer.unitAssignments.front().plannedTakeId = "revised-plan";
  CHECK(!repository.importProceduralCandidate(producer, prefix.string() + ".json", prefix.string() + ".wav",
      resource.value(), generatedTake, generatedEvent, {}, &expectation.value()));
  CHECK(repository.save(producer, {.action = "save", .subjectId = producer.projectId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-07T00:00:01Z"}));
  CHECK(!repository.importProceduralCandidate(producer, prefix.string() + ".json", prefix.string() + ".wav",
      resource.value(), generatedTake, generatedEvent, {}, &expectation.value()));
  CHECK(repository.recover().value().takes.empty());
  // Explicitly capture a new request against the changed workspace.
  expectation = production::captureGenerationImportExpectation(producer, generatedTake, resource.value(),
      "neutral", candidate.value().renderContentHash, candidate.value().sampleRate, candidate.value().frameCount); CHECK(expectation);
#if defined(SEAM_TEST_VOICEBANK_CLI) && (defined(__APPLE__) || defined(__linux__))
  CHECK(voice_design::saveVoiceRecipeFile(root / "generated-recipe.json", recipe));
  std::vector<std::string> collectArgs{"import-generated", (root / "producer").string(), prefix.string() + ".json",
      prefix.string() + ".wav", (root / "generated-recipe.json").string(), requestPath.string(), requestHash.value(),
      "producer", "2026-09-07T00:00:02Z"};
  CHECK(runVoicebankCli(collectArgs) != 0); // Original expectation is stale after the saved assignment change.
  CHECK(repository.recover().value().takes.empty());
  const auto freshPath = root / "fresh-expectation.json";
  const auto freshHash = production::saveGenerationImportExpectation(freshPath, expectation.value()); CHECK(freshHash);
  collectArgs[5] = freshPath.string(); collectArgs[6] = std::string(64U, '0');
  CHECK(runVoicebankCli(collectArgs) != 0); CHECK(repository.recover().value().takes.empty());
  collectArgs[6] = freshHash.value();
  CHECK(runVoicebankCli(collectArgs) == 0);
  const auto collectedState = production::encodeProductionProject(repository.recover().value());
  CHECK(runVoicebankCli(collectArgs) == 0); // Recognize the original committed request without recapturing.
  auto recognizedArgs = collectArgs;
  recognizedArgs[2] = (root / "missing-staged.json").string();
  recognizedArgs[3] = (root / "missing-staged.wav").string();
  recognizedArgs[4] = (root / "missing-staged-recipe.json").string();
  CHECK(runVoicebankCli(recognizedArgs) == 0); // Historical recognition does not depend on disposable staging.
  CHECK(production::encodeProductionProject(repository.recover().value()) == collectedState);
#else
  CHECK(repository.importProceduralCandidate(producer, prefix.string() + ".json", prefix.string() + ".wav", resource.value(),
      generatedTake, generatedEvent, {}, &expectation.value()));
#endif
  const auto recovered = repository.recover(); CHECK(recovered);
  const auto recognized = repository.findCollectedGeneration(expectation.value()); CHECK(recognized); CHECK(recognized.value());
  CHECK(recognized.value()->active); CHECK(recognized.value()->takeId == "take-sa");
  auto wrongRecognition = expectation.value(); wrongRecognition.renderContentHash = std::string(64U, '0');
  CHECK(!repository.findCollectedGeneration(wrongRecognition));
  const auto effective = production::resolveCandidateMarkers(recovered.value(), "take-sa"); CHECK(effective);
  CHECK(effective.value().candidate.markers == candidate.value().markers);
  CHECK(recovered.value().unitAssignments.front().state == production::UnitQueueState::MarkerReview);
  CHECK(!recovered.value().unitAssignments.front().markerReviewed);
  CHECK(!recovered.value().unitAssignments.front().pitchReviewed);
  native_ui::VoicebankStudioController studio;
  CHECK(studio.openProductionProject(root / "producer", producer.inventorySha256, "producer"));
  CHECK(studio.candidateMarkerPreview());
  CHECK(studio.candidateMarkerPreview()->markers.front().kind == voice_design::ProceduralGestureKind::Frication);
  CHECK(studio.editSelectedCandidateMarker(0, 4700));
  const auto edited = repository.recover(); CHECK(edited);
  const auto editedMarkers = production::resolveCandidateMarkers(edited.value(), "take-sa"); CHECK(editedMarkers);
  CHECK(editedMarkers.value().candidate.markers.front().kind == voice_design::ProceduralGestureKind::Frication);
  CHECK(editedMarkers.value().candidate.markers.front().ownedSpan.end == 4700);
  CHECK(editedMarkers.value().candidate.metadataJson == candidate.value().metadataJson);
  CHECK(!edited.value().unitAssignments.front().markerReviewed);
  const auto beforeRecognition = production::encodeProductionProject(edited.value());
  CHECK(repository.findCollectedGeneration(expectation.value()).value());
  CHECK(production::encodeProductionProject(repository.recover().value()) == beforeRecognition);
  auto retakeProject = edited.value();
  CHECK(repository.importProceduralCandidate(retakeProject, prefix.string() + ".json", prefix.string() + ".wav", resource.value(),
      {.takeId = "manual-retake", .promptId = "prompt-sa", .coverageKey = "cv:sa", .pitchLayer = 69, .supersedesTakeId = "take-sa"},
      {.action = "retake", .subjectId = "manual-retake", .operatorId = "producer", .occurredAtUtc = "2026-09-07T00:00:03Z"}));
  const auto beforeRetakeRecognition = production::encodeProductionProject(retakeProject);
  const auto historical = repository.findCollectedGeneration(expectation.value()); CHECK(historical); CHECK(historical.value());
  CHECK(!historical.value()->active); CHECK(historical.value()->state == production::UnitQueueState::Retake);
  CHECK(production::encodeProductionProject(repository.recover().value()) == beforeRetakeRecognition);
  auto automaticProject = project; automaticProject.findRegion(regionId)->phonemeOverrides.clear();
  const auto automaticBake = authoring::ExportService{}.exportSetWithSources(automaticProject, sources, trackId, regionId,
      1U, root / "automatic-bake", settings); CHECK(automaticBake);
  const auto automaticPrefix = root / "automatic-bake/candidates" / (trackId.toString() + "-" + regionId.toString());
  const auto automaticCandidate = voice_design::loadProceduralCandidate(automaticPrefix.string() + ".json",
      automaticPrefix.string() + ".wav", resource.value()); CHECK(automaticCandidate);
  CHECK(automaticCandidate.value().markers.front().ownedSpan.end == 2880);
  CHECK(automaticProject.findRegion(regionId)->phonemeOverrides.empty());
#if defined(SEAM_TEST_VOICEBANK_CLI) && (defined(__APPLE__) || defined(__linux__))
  CHECK(voice_design::saveVoiceRecipeFile(root / "singer.json", recipe));
  automaticProject.findVocalTrack(trackId)->proceduralRecipe = domain::ProceduralRecipeReference{resource.value().identity, "singer.json", "neutral"};
  const auto projectPath = root / "source.seam";
  CHECK(formats::ProjectJsonCodec{}.save(automaticProject, projectPath));
  const auto sourceHash = core::sha256File(projectPath); CHECK(sourceHash);
  const std::vector<std::string> bakeArgs{"bake-project", projectPath.string(), (root / "cli-bake").string(), "48000"};
  CHECK(runVoicebankCli(bakeArgs) == 0);
  const auto cliPrefix = root / "cli-bake/candidates" / (trackId.toString() + "-" + regionId.toString());
  const auto cliCandidate = voice_design::loadProceduralCandidate(cliPrefix.string() + ".json", cliPrefix.string() + ".wav", resource.value()); CHECK(cliCandidate);
  CHECK(cliCandidate.value().markers == automaticCandidate.value().markers);
  CHECK(cliCandidate.value().audio->interleaved == automaticCandidate.value().audio->interleaved);
  CHECK(runVoicebankCli(bakeArgs) != 0); // Existing completed output is preserved, not overwritten.
  CHECK(core::sha256File(projectPath).value() == sourceHash.value());
  const auto cliHash = core::sha256File(cliPrefix.string() + ".wav"); CHECK(cliHash);
  CHECK(cliHash.value() == cliCandidate.value().audioSha256);
  auto invalidArgs = bakeArgs; invalidArgs[2] = (root / "invalid-cli-bake").string(); invalidArgs[3] = "48000junk";
  CHECK(runVoicebankCli(invalidArgs) != 0); CHECK(!std::filesystem::exists(root / "invalid-cli-bake"));
  automaticProject.findVocalTrack(trackId)->proceduralRecipe.reset();
  CHECK(formats::ProjectJsonCodec{}.save(automaticProject, root / "no-recipe.seam"));
  invalidArgs[1] = (root / "no-recipe.seam").string(); invalidArgs[3] = "48000";
  CHECK(runVoicebankCli(invalidArgs) != 0); CHECK(!std::filesystem::exists(root / "invalid-cli-bake"));
#endif
}

TEST_CASE("native vibrato draft changes final audio and survives project reload export and exact undo replay") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("vibrato-native-render");
  voice_design::VoiceRecipe recipe; recipe.id = "vibrato-render-test";
  recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
  CHECK(voice_design::saveVoiceRecipeFile(root / "singer.json", recipe));
  const auto resource = voice_design::freezeVoiceRecipeResource(recipe); CHECK(resource);
  application::ProjectFactory factory{321000U}; auto project = factory.createProject("Native vibrato render");
  const auto track = factory.addVocalTrack(project, "Singer");
  const auto region = factory.addRegion(project, track, "Sustain", time::Tick{0}, time::Tick{1920});
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{1920}, 69U, U"あ", domain::Language::Japanese);
  project.findRegion(region)->lyrics.push_back(lyric); project.findRegion(region)->notes.push_back(note);
  project.findVocalTrack(track)->proceduralRecipe = domain::ProceduralRecipeReference{resource.value().identity, "singer.json", "neutral"};
  application::EditorSession session{project}; session.selection().selectOnly(note.id); unsigned changes = 0U;
  native_ui::NativeEditorController controller{session, factory, region,
      {.beginTextInput = [](const native_ui::TextInputRequest&) {}, .documentChanged = [&] { ++changes; }}};
  const auto render = [&](const domain::Project& score, std::uint64_t revision) {
    const std::vector<rendering::TrackSingerSource> sources{rendering::TrackRecipeFileSource{
        track, *score.findVocalTrack(track)->proceduralRecipe, root}};
    return rendering::ProductionProjectRenderer{}.renderWithSources(score, sources, track, region, revision, 48000U, rendering::RenderQuality::Final);
  };
  const auto original = render(session.project(), session.revision()); CHECK(original); CHECK(original.value().diagnostics.empty());
  CHECK(controller.openVibratoInspector());
  const std::array<std::u32string, 6U> values{U"On", U"0.2", U"0.1", U"0.1", U"100", U"200"};
  for (std::size_t i = 0U; i < values.size(); ++i) { CHECK(controller.openReplacementRow(i)); CHECK(controller.commitTextComposition(values[i])); }
  CHECK(controller.replacementReviewAction(1U)); CHECK(controller.openReplacementRow(0U)); CHECK(controller.commitTextComposition(U"0.25"));
  CHECK(session.project() == project); CHECK(changes == 0U);
  const auto uncommitted = render(session.project(), session.revision()); CHECK(uncommitted);
  CHECK(uncommitted.value().interleaved == original.value().interleaved);
  CHECK(controller.replacementReviewAction(3U)); CHECK(changes == 1U); CHECK(session.revision() == 1U);
  auto expected = project;
  expected.findNote(note.id)->vibrato = {true, 0.2F, 0.1F, 0.1F, 100.0F, 200.0F, 0.25F};
  CHECK(session.project() == expected);
  const auto edited = render(session.project(), session.revision()); CHECK(edited); CHECK(edited.value().diagnostics.empty());
  CHECK(edited.value().interleaved.size() == original.value().interleaved.size());
  CHECK(edited.value().phraseContentHashes != original.value().phraseContentHashes);
  double baselineEnergy = 0.0, differenceEnergy = 0.0;
  for (std::size_t i = 0U; i < edited.value().interleaved.size(); ++i) {
    const auto baseline = static_cast<double>(original.value().interleaved[i]);
    const auto difference = static_cast<double>(edited.value().interleaved[i]) - baseline;
    CHECK(std::isfinite(edited.value().interleaved[i])); baselineEnergy += baseline * baseline; differenceEnergy += difference * difference;
  }
  CHECK(baselineEnergy > 0.0); CHECK(differenceEnergy > baselineEnergy * 0.01); // More than floating-point/cache noise, not a listening-quality metric.
  CHECK(formats::ProjectJsonCodec{}.save(session.project(), root / "vibrato.seam"));
  const auto reopened = formats::ProjectJsonCodec{}.load(root / "vibrato.seam"); CHECK(reopened); CHECK(reopened.value() == expected);
  const auto replay = render(reopened.value(), session.revision()); CHECK(replay); CHECK(replay.value().interleaved == edited.value().interleaved);
  const std::vector<rendering::TrackSingerSource> sources{rendering::TrackRecipeFileSource{track, *reopened.value().findVocalTrack(track)->proceduralRecipe, root}};
  authoring::ExportSettings settings; settings.format = voicebank::WavSampleFormat::Float32; settings.includeStems = true;
  const auto exported = authoring::ExportService{}.exportSetWithSources(reopened.value(), sources, track, region, session.revision(), root / "audio", settings); CHECK(exported);
  CHECK(exported.value().state == authoring::ExportState::Committed); CHECK(exported.value().files.size() == 2U);
  for (const auto& file : exported.value().files) {
    const auto wav = voicebank::readWav(file.path); CHECK(wav); CHECK(wav.value().interleaved == edited.value().interleaved);
  }
  CHECK(session.undo()); CHECK(session.project() == project);
  const auto undone = render(session.project(), session.revision()); CHECK(undone); CHECK(undone.value().interleaved == original.value().interleaved);
  CHECK(session.redo()); CHECK(session.project() == expected);
  const auto redone = render(session.project(), session.revision()); CHECK(redone); CHECK(redone.value().interleaved == edited.value().interleaved);
}

TEST_CASE("native style selection changes installed bank audio and survives reload export undo and redo") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("native-style-installed-audio");
  const auto source = root / "source"; std::filesystem::create_directories(source / "audio");
  const auto originalSamples = test::support::sineWave(48000U, 220.0, 0.24);
  auto softSamples = originalSamples;
  for (auto& sample : softSamples) sample *= 0.25F;
  CHECK(voicebank::writePcm16Wav(source / "audio/original.wav", 48000U, 1U, originalSamples));
  CHECK(voicebank::writePcm16Wav(source / "audio/soft.wav", 48000U, 1U, softSamples));
  auto originalUnit = test::support::makeUnit("original-a", {"a"}, "audio/original.wav", 57, voicebank::UnitKind::Sustain, originalSamples.size());
  auto softUnit = test::support::makeUnit("soft-a", {"a"}, "audio/soft.wav", 57, voicebank::UnitKind::Sustain, softSamples.size());
  softUnit.style = "soft";
  auto manifest = test::support::makeManifest({originalUnit, softUnit}); manifest.id = "native.style.audio.fixture";
  manifest.styles = {"original", "soft"};
  CHECK(voicebank::ManifestJsonCodec{}.save(manifest, source / "manifest.json"));
  CHECK(core::durableAtomicWriteText(source / "license.txt", "Synthetic test signals, not a shipping singer.\n"));
  const auto key = distribution::generateSigningKeyPair(); CHECK(key);
  CHECK(distribution::packSeambank(source, root / "styles.seambank", key.value()));
  authoring::VoicebankSession banks({{root / "installed", voicebank::VoicebankRootKind::Installed}}, false);
  authoring::VoicebankInstallerService installer{banks, root / "installed"};
  const auto installed = installer.install({.packagePath = root / "styles.seambank", .trustedPublicKeys = {key.value().publicKey}}); CHECK(installed);
  const auto& candidate = installed.value().candidate; CHECK(candidate.trust == voicebank::VoicebankTrust::TrustedInstalled);
  application::ProjectFactory factory{392000U}; auto project = factory.createProject("Native style audio");
  const auto track = factory.addVocalTrack(project, "Singer");
  const auto region = factory.addRegion(project, track, "Vowel", time::Tick{0}, time::Tick{960});
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{960}, 57U, U"あ", domain::Language::Japanese);
  project.findRegion(region)->lyrics.push_back(lyric); project.findRegion(region)->notes.push_back(note);
  project.findVocalTrack(track)->voicebank = {manifest.id, manifest.version, candidate.contentHash};
  project.findVocalTrack(track)->styleSelection = {domain::VoiceStyleOrigin::Explicit, "original"};
  application::EditorSession session{project}; unsigned changes = 0U;
  native_ui::NativeEditorController controller{session, factory, region, {.documentChanged = [&] { ++changes; }}};
  controller.setStyleBankSnapshotResolver([&](domain::TrackId id) { return banks.resolveTrackSnapshot(session.project(), id); });
  const auto sourcesFor = [&](const domain::Project& score) {
    const auto resolved = banks.resolveTrack(score, track); CHECK(resolved.resolved());
    const auto& bank = *resolved.candidate;
    return std::vector<rendering::TrackVoicebankSource>{{track, bank.manifest, bank.bankRoot, bank.contentHash, bank.trust}};
  };
  rendering::PcmCache cache{root / "cache"};
  const auto render = [&](const domain::Project& score, rendering::RenderQuality quality) {
    return rendering::ProductionProjectRenderer{}.render(score, sourcesFor(score), track, region, session.revision(), 48000U, quality, {}, &cache);
  };
  const auto original = render(project, rendering::RenderQuality::Final); CHECK(original); CHECK(original.value().diagnostics.empty());
  CHECK(original.value().activeUnitPlan.size() == 1U); CHECK(original.value().activeUnitPlan.front().unitId == "original-a");
  CHECK(controller.openStyleCoverageSheet()); CHECK(controller.openReplacementRow(1U));
  CHECK(session.project() == project); CHECK(changes == 0U);
  const auto draft = render(session.project(), rendering::RenderQuality::Final); CHECK(draft); CHECK(draft.value().interleaved == original.value().interleaved);
  CHECK(draft.value().cacheHits > 0U);
  CHECK(controller.replacementReviewAction(3U)); CHECK(changes == 1U); CHECK(session.revision() == 1U);
  auto expected = project; expected.findVocalTrack(track)->styleSelection = {domain::VoiceStyleOrigin::Explicit, "soft"};
  CHECK(session.project() == expected);
  const auto edited = render(expected, rendering::RenderQuality::Final); CHECK(edited); CHECK(edited.value().diagnostics.empty());
  CHECK(edited.value().activeUnitPlan.size() == 1U); CHECK(edited.value().activeUnitPlan.front().unitId == "soft-a");
  CHECK(edited.value().phraseContentHashes != original.value().phraseContentHashes);
  CHECK(edited.value().interleaved.size() == original.value().interleaved.size());
  double originalEnergy = 0.0, editedEnergy = 0.0;
  for (std::size_t i = 0; i < original.value().interleaved.size(); ++i) {
    const auto a = original.value().interleaved[i], b = edited.value().interleaved[i]; CHECK(std::isfinite(a)); CHECK(std::isfinite(b));
    originalEnergy += static_cast<double>(a) * a; editedEnergy += static_cast<double>(b) * b;
  }
  CHECK(originalEnergy > 0.0); CHECK(editedEnergy > 0.0); CHECK(editedEnergy / originalEnergy < 0.1);
  CHECK(formats::ProjectJsonCodec{}.save(expected, root / "song.seam"));
  const auto reopened = formats::ProjectJsonCodec{}.load(root / "song.seam"); CHECK(reopened); CHECK(reopened.value() == expected);
  authoring::VoicebankSession freshBanks({{root / "installed", voicebank::VoicebankRootKind::Installed}}, false);
  CHECK(freshBanks.refresh()); const auto freshBank = freshBanks.resolveTrack(reopened.value(), track); CHECK(freshBank.resolved());
  CHECK(freshBank.candidate->trust == voicebank::VoicebankTrust::TrustedInstalled);
  const std::vector<rendering::TrackVoicebankSource> freshSources{{track, freshBank.candidate->manifest, freshBank.candidate->bankRoot,
      freshBank.candidate->contentHash, freshBank.candidate->trust}};
  const auto coldReplay = rendering::ProductionProjectRenderer{}.render(reopened.value(), freshSources, track, region,
      session.revision(), 48000U, rendering::RenderQuality::Final); CHECK(coldReplay); CHECK(coldReplay.value().diagnostics.empty());
  CHECK(coldReplay.value().interleaved == edited.value().interleaved);
  const auto replay = render(reopened.value(), rendering::RenderQuality::Final); CHECK(replay); CHECK(replay.value().interleaved == edited.value().interleaved);
  authoring::ExportSettings settings; settings.format = voicebank::WavSampleFormat::Float32; settings.includeStems = true;
  const auto exported = authoring::ExportService{}.exportSet(reopened.value(), sourcesFor(reopened.value()), track, region, session.revision(), root / "exports", settings);
  CHECK(exported); CHECK(exported.value().state == authoring::ExportState::Committed); CHECK(exported.value().files.size() == 2U);
  for (const auto& file : exported.value().files) { const auto wav = voicebank::readWav(file.path); CHECK(wav); CHECK(wav.value().interleaved == edited.value().interleaved); }
  CHECK(session.undo()); CHECK(session.project() == project);
  const auto undone = render(session.project(), rendering::RenderQuality::Final); CHECK(undone); CHECK(undone.value().interleaved == original.value().interleaved);
  const auto originalPreview = render(session.project(), rendering::RenderQuality::Preview); CHECK(originalPreview);
  CHECK(session.redo()); CHECK(session.project() == expected);
  const auto redone = render(session.project(), rendering::RenderQuality::Final); CHECK(redone); CHECK(redone.value().interleaved == edited.value().interleaved);
  const auto editedPreview = render(session.project(), rendering::RenderQuality::Preview); CHECK(editedPreview);
  CHECK(originalPreview.value().diagnostics.empty()); CHECK(editedPreview.value().diagnostics.empty());
  CHECK(originalPreview.value().interleaved != editedPreview.value().interleaved);
}

TEST_CASE("native dynamics curve edits scale final audio and survive reload export undo and redo") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("dynamics-native-render");
  voice_design::VoiceRecipe recipe; recipe.id = "dynamics-render-test";
  recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
  CHECK(voice_design::saveVoiceRecipeFile(root / "singer.json", recipe));
  const auto resource = voice_design::freezeVoiceRecipeResource(recipe); CHECK(resource);
  application::ProjectFactory factory{331000U}; auto project = factory.createProject("Native dynamics render");
  const auto track = factory.addVocalTrack(project, "Singer");
  const auto region = factory.addRegion(project, track, "Sustain", time::Tick{0}, time::Tick{1920});
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{1920}, 69U, U"あ", domain::Language::Japanese);
  note.vibrato.enabled = true;
  project.findRegion(region)->lyrics.push_back(lyric); project.findRegion(region)->notes.push_back(note);
  CHECK(project.findRegion(region)->dynamicsAutomation.upsert({time::Tick{0}, 1.0F}));
  project.findVocalTrack(track)->proceduralRecipe = domain::ProceduralRecipeReference{resource.value().identity, "singer.json", "neutral"};
  application::EditorSession session{project}; unsigned changes = 0U;
  native_ui::NativeEditorController controller{session, factory, region,
      {.beginTextInput = [](const native_ui::TextInputRequest&) {}, .documentChanged = [&] { ++changes; }}};
  controller.resize(480.0, 320.0);
  const auto render = [&](const domain::Project& score, std::uint64_t revision) {
    const std::vector<rendering::TrackSingerSource> sources{rendering::TrackRecipeFileSource{
        track, *score.findVocalTrack(track)->proceduralRecipe, root}};
    return rendering::ProductionProjectRenderer{}.renderWithSources(score, sources, track, region, revision, 48000U, rendering::RenderQuality::Final);
  };
  const auto original = render(session.project(), session.revision()); CHECK(original); CHECK(original.value().diagnostics.empty());
  CHECK(controller.openDynamicsInspector());
  const auto plot = controller.sceneState().replacementReview.dynamicsPlot; CHECK(plot);
  CHECK(controller.pointerDown({.position = plot->handles[0].position, .button = native_ui::PointerButton::Left}));
  const auto y = plot->bounds.y + plot->bounds.height * (1.0 - 0.5 / domain::kMaximumDynamicsGain);
  CHECK(controller.pointerMove({.position = {plot->bounds.x, y}, .button = native_ui::PointerButton::Left}));
  CHECK(controller.pointerUp({.position = {plot->bounds.x, y}, .button = native_ui::PointerButton::Left}));
  CHECK(controller.openReplacementRow(1U)); CHECK(controller.commitTextComposition(U"0.25"));
  CHECK(controller.replacementReviewAction(3U)); // Staged curve only.
  CHECK(session.project() == project); CHECK(changes == 0U);
  const auto draftAudio = render(session.project(), session.revision()); CHECK(draftAudio);
  CHECK(draftAudio.value().interleaved == original.value().interleaved);
  CHECK(controller.replacementReviewAction(3U)); CHECK(changes == 1U); CHECK(session.revision() == 1U);
  auto expected = project; CHECK(expected.findRegion(region)->dynamicsAutomation.upsert({time::Tick{0}, 0.25F}));
  CHECK(session.project() == expected);
  const auto edited = render(session.project(), session.revision()); CHECK(edited); CHECK(edited.value().diagnostics.empty());
  CHECK(edited.value().interleaved.size() == original.value().interleaved.size());
  CHECK(edited.value().phraseContentHashes != original.value().phraseContentHashes);
  double originalEnergy = 0.0, editedEnergy = 0.0;
  for (std::size_t i = 0U; i < original.value().interleaved.size(); ++i) {
    const auto a = original.value().interleaved[i], b = edited.value().interleaved[i];
    CHECK(std::isfinite(a)); CHECK(std::isfinite(b));
    CHECK(std::abs(b - a * 0.25F) < 0.00001F);
    originalEnergy += static_cast<double>(a) * a; editedEnergy += static_cast<double>(b) * b;
  }
  CHECK(originalEnergy > 0.0); CHECK(std::abs(editedEnergy / originalEnergy - 0.0625) < 0.0001);
  const auto measure = [](const rendering::ProjectRenderResult& audio) {
    return rendering::measureAudioLevels({audio.interleaved.data(), audio.interleaved.size()}, audio.channelCount,
        0U, audio.interleaved.size() / audio.channelCount, 128U);
  };
  const auto originalLevels = measure(original.value()), editedLevels = measure(edited.value());
  CHECK(originalLevels); CHECK(editedLevels); CHECK(originalLevels.value().bins.size() == 128U);
  CHECK(editedLevels.value().bins.size() == originalLevels.value().bins.size());
  for (std::size_t i = 0U; i < originalLevels.value().bins.size(); ++i) {
    const auto& a = originalLevels.value().bins[i]; const auto& b = editedLevels.value().bins[i];
    CHECK(a.firstFrame == b.firstFrame); CHECK(a.frameCount == b.frameCount);
    for (std::size_t channel = 0U; channel < a.channels.size(); ++channel) {
      CHECK_NEAR(b.channels[channel].rms, a.channels[channel].rms * 0.25, 0.000001);
      CHECK_NEAR(b.channels[channel].peak, a.channels[channel].peak * 0.25, 0.000001);
    }
  }
  CHECK(formats::ProjectJsonCodec{}.save(session.project(), root / "dynamics.seam"));
  const auto reopened = formats::ProjectJsonCodec{}.load(root / "dynamics.seam"); CHECK(reopened); CHECK(reopened.value() == expected);
  const auto replay = render(reopened.value(), session.revision()); CHECK(replay); CHECK(replay.value().interleaved == edited.value().interleaved);
  const auto replayLevels = measure(replay.value()); CHECK(replayLevels); CHECK(replayLevels.value() == editedLevels.value());
  const std::vector<rendering::TrackSingerSource> sources{rendering::TrackRecipeFileSource{track, *reopened.value().findVocalTrack(track)->proceduralRecipe, root}};
  authoring::ExportSettings settings; settings.format = voicebank::WavSampleFormat::Float32; settings.includeStems = true;
  const auto exported = authoring::ExportService{}.exportSetWithSources(reopened.value(), sources, track, region, session.revision(), root / "audio", settings); CHECK(exported);
  CHECK(exported.value().state == authoring::ExportState::Committed); CHECK(exported.value().files.size() == 2U);
  for (const auto& file : exported.value().files) {
    const auto wav = voicebank::readWav(file.path); CHECK(wav); CHECK(wav.value().interleaved == edited.value().interleaved);
  }
  CHECK(session.undo()); CHECK(session.project() == project);
  const auto undone = render(session.project(), session.revision()); CHECK(undone); CHECK(undone.value().interleaved == original.value().interleaved);
  CHECK(session.redo()); CHECK(session.project() == expected);
  const auto redone = render(session.project(), session.revision()); CHECK(redone); CHECK(redone.value().interleaved == edited.value().interleaved);
}

TEST_CASE("native tempo edit changes final rendered and exported duration with exact undo replay") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("tempo-render-export");
  voice_design::VoiceRecipe recipe;
  recipe.id = "tempo-test";
  recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
  const auto resource = voice_design::freezeVoiceRecipeResource(recipe); CHECK(resource);
  application::ProjectFactory factory{81900U};
  auto project = factory.createProject("Tempo render");
  const auto track = factory.addVocalTrack(project, "Singer");
  const auto region = factory.addRegion(project, track, "Sustain", time::Tick{0}, time::Tick{1920});
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{1920}, 69U, U"あ", domain::Language::Japanese);
  project.findRegion(region)->lyrics.push_back(lyric); project.findRegion(region)->notes.push_back(note);
  const std::vector<rendering::TrackSingerSource> sources{rendering::TrackProceduralSource{track, resource.value(), "neutral"}};
  application::EditorSession session{project};
  unsigned changed = 0U;
  native_ui::NativeEditorController controller{session, factory, region,
      {.beginTextInput = [](const native_ui::TextInputRequest&) {}, .documentChanged = [&] { ++changed; }}};
  const auto render = [&] {
    return rendering::ProductionProjectRenderer{}.renderWithSources(session.project(), sources,
        track, region, session.revision(), 48000U, rendering::RenderQuality::Final);
  };
  const auto original = render(); CHECK(original);
  CHECK(original.value().interleaved.size() / original.value().channelCount == 48000U);
  CHECK(controller.beginTempoEdit(time::Tick{960})); CHECK(controller.commitTextComposition(U"60"));
  CHECK(changed == 1U); CHECK(session.lastImpact().projectWide);
  const auto slowed = render(); CHECK(slowed);
  CHECK(slowed.value().interleaved.size() / slowed.value().channelCount == 72000U);
  CHECK(slowed.value().phraseContentHashes != original.value().phraseContentHashes);
  CHECK(slowed.value().diagnostics.empty());
  CHECK(*session.project().findNote(note.id) == note);
  CHECK(controller.beginMeterEdit(time::Tick{0})); CHECK(controller.commitTextComposition(U"3/4"));
  CHECK(changed == 2U);
  const auto remetered = render(); CHECK(remetered);
  CHECK(remetered.value().interleaved == slowed.value().interleaved);
  CHECK(formats::ProjectJsonCodec{}.save(session.project(), root / "tempo.seam"));
  const auto reopened = formats::ProjectJsonCodec{}.load(root / "tempo.seam"); CHECK(reopened);
  CHECK(reopened.value().tempoMap() == session.project().tempoMap());
  CHECK(reopened.value().meterMap() == session.project().meterMap());
  const auto replay = rendering::ProductionProjectRenderer{}.renderWithSources(reopened.value(), sources,
      track, region, session.revision(), 48000U, rendering::RenderQuality::Final); CHECK(replay);
  CHECK(replay.value().interleaved == slowed.value().interleaved);
  authoring::ExportSettings settings; settings.format = voicebank::WavSampleFormat::Float32; settings.includeStems = true;
  const auto exported = authoring::ExportService{}.exportSetWithSources(session.project(), sources,
      track, region, session.revision(), root / "audio", settings); CHECK(exported);
  CHECK(exported.value().state == authoring::ExportState::Committed);
  CHECK(exported.value().files.size() == 2U);
  for (const auto& file : exported.value().files) {
    const auto wav = voicebank::readWav(file.path); CHECK(wav);
    CHECK(wav.value().frameCount() == 72000U);
    CHECK(wav.value().interleaved == slowed.value().interleaved);
  }
  CHECK(session.undo()); // Meter is independent of this sustained vowel's PCM.
  CHECK(session.undo()); const auto restored = render(); CHECK(restored);
  CHECK(restored.value().interleaved == original.value().interleaved);
  CHECK(session.redo()); const auto redone = render(); CHECK(redone);
  CHECK(redone.value().interleaved == slowed.value().interleaved);
}

TEST_CASE("procedural export commits exact final PCM stems and truthful recipe receipts") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("procedural-export");
  voice_design::VoiceRecipe recipe;
  recipe.id = "export-draft";
  recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
  CHECK(voice_design::saveVoiceRecipeFile(root / "singer.json", recipe));
  const auto resource = voice_design::freezeVoiceRecipeResource(recipe); CHECK(resource);
  application::ProjectFactory factory{81000U};
  auto project = factory.createProject("Recipe export");
  const auto trackId = factory.addVocalTrack(project, "Singer");
  const auto regionId = factory.addRegion(project, trackId, "Vowel", time::Tick{0}, time::Tick{960});
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{960}, 69U, U"あ", domain::Language::Japanese);
  project.findRegion(regionId)->lyrics.push_back(std::move(lyric));
  project.findRegion(regionId)->notes.push_back(std::move(note));
  project.findVocalTrack(trackId)->proceduralRecipe = domain::ProceduralRecipeReference{resource.value().identity, "singer.json", "neutral"};
  const std::vector<rendering::TrackSingerSource> sources{rendering::TrackRecipeFileSource{
      trackId, *project.findVocalTrack(trackId)->proceduralRecipe, root}};
  const auto expected = rendering::ProductionProjectRenderer{}.renderWithSources(project, sources,
      trackId, regionId, 1U, 48000U, rendering::RenderQuality::Final); CHECK(expected);
  authoring::ExportSettings settings; settings.format = voicebank::WavSampleFormat::Float32; settings.includeStems = true;
  const auto exported = authoring::ExportService{}.exportSetWithSources(project, sources, trackId, regionId,
      1U, root / "export", settings); CHECK(exported);
  CHECK(exported.value().state == authoring::ExportState::Committed); CHECK(exported.value().files.size() == 2U);
  for (const auto& file : exported.value().files) {
    const auto wav = voicebank::readWav(file.path); CHECK(wav);
    CHECK(wav.value().interleaved == std::vector<float>(expected.value().interleaved.begin(), expected.value().interleaved.end()));
  }
  const auto receipt = core::readTextFileLimited(exported.value().receiptPath, 1024U * 1024U); CHECK(receipt);
  const auto json = formats::parseJson(receipt.value()); CHECK(json);
  CHECK(json.value().find("voicebanks")->asArray().empty());
  const auto& recorded = json.value().find("proceduralRecipes")->asArray(); CHECK(recorded.size() == 1U);
  CHECK(recorded.front().find("contentHash")->asString() == resource.value().identity.contentHash);
  const auto preserved = receipt.value();
  auto packagedSettings = settings; packagedSettings.includeProjectAndRecipes = true;
  const auto projectBeforePackage = project;
  bool originalMoved = false;
  const auto package = authoring::ExportService{}.exportSetWithSources(project, sources, trackId, regionId,
      1U, root / "packaged", packagedSettings, [&](const authoring::ExportProgress& progress) {
        if (!originalMoved && progress.completedFiles == 1U) {
          std::filesystem::rename(root / "singer.json", root / "source-offline.json");
          originalMoved = true;
        }
      });
  CHECK(package); CHECK(originalMoved); CHECK(project == projectBeforePackage);
  CHECK(package.value().files.size() == 4U);
  const auto packagedProject = formats::ProjectJsonCodec{}.load(root / "packaged/project.seam"); CHECK(packagedProject);
  const auto& reference = *packagedProject.value().findVocalTrack(trackId)->proceduralRecipe;
  CHECK(reference.resource == resource.value().identity); CHECK(std::filesystem::path{reference.path}.is_relative());
  const std::vector<rendering::TrackSingerSource> packagedSources{
      rendering::TrackRecipeFileSource{trackId, reference, root / "packaged"}};
  const auto replay = rendering::ProductionProjectRenderer{}.renderWithSources(packagedProject.value(), packagedSources,
      trackId, regionId, 1U, 48000U, rendering::RenderQuality::Final); CHECK(replay);
  CHECK(replay.value().interleaved == expected.value().interleaved);
  CHECK(authoring::ExportService{}.recoverSet(root / "packaged"));
  std::filesystem::rename(root / "source-offline.json", root / "singer.json");
  authoring::ExportSettings bakeSettings;
  bakeSettings.includeMaster = false; bakeSettings.includeStems = false; bakeSettings.includeProceduralCandidates = true;
  const auto baked = authoring::ExportService{}.exportSetWithSources(project, sources, trackId, regionId,
      1U, root / "baked", bakeSettings); CHECK(baked);
  CHECK(baked.value().state == authoring::ExportState::Committed); CHECK(baked.value().files.size() == 4U);
  const auto prefix = root / "baked/candidates" / (trackId.toString() + "-" + regionId.toString());
  const auto bakedWav = voicebank::readWav(prefix.string() + ".wav"); CHECK(bakedWav); CHECK(bakedWav.value().channels == 1U);
  const auto bakeSnapshot = rendering::RenderSnapshotFactory{}.createProcedural(project, resource.value(), trackId, regionId,
      1U, rendering::RenderQuality::Final, 48000U); CHECK(bakeSnapshot);
  const auto bakeReference = rendering::PhraseRenderPipeline{}.render(bakeSnapshot.value()); CHECK(bakeReference);
  CHECK(bakedWav.value().interleaved == bakeReference.value().rendered.audio.samples);
  const auto bakeText = core::readTextFileLimited(prefix.string() + ".json", 4U * 1024U * 1024U); CHECK(bakeText);
  const auto bakeJson = formats::parseJson(bakeText.value()); CHECK(bakeJson);
  const auto metadataPreview = voice_design::parseProceduralCandidateMetadata(bakeText.value(), resource.value());
  CHECK(metadataPreview);
  CHECK(!metadataPreview.value().audio);
  CHECK(metadataPreview.value().sampleRate == bakedWav.value().sampleRate);
  CHECK(metadataPreview.value().frameCount == static_cast<time::SampleFrame>(bakedWav.value().frameCount()));
  CHECK(bakeJson.value().find("approval")->asString() == "unapproved");
  CHECK(bakeJson.value().find("markerSemantics")->asString() == "planned-vowel-gestures");
  CHECK(bakeJson.value().find("recipeHash")->asString() == resource.value().identity.contentHash);
  CHECK(bakeJson.value().find("renderContentHash")->asString() == bakeSnapshot.value().contentHash);
  CHECK(bakeJson.value().find("audioSha256")->asString() == core::sha256File(prefix.string() + ".wav").value());
  const auto& bakedMarkers = bakeJson.value().find("markers")->asArray(); CHECK(bakedMarkers.size() == 1U);
  CHECK(bakedMarkers[0].find("startFrame")->asInt64() == 0);
  CHECK(bakedMarkers[0].find("endFrame")->asInt64() == static_cast<std::int64_t>(bakedWav.value().frameCount()));
  CHECK(authoring::ExportService{}.recoverSet(root / "baked"));
  const auto ingested = voice_design::loadProceduralCandidate(prefix.string() + ".json", prefix.string() + ".wav", resource.value());
  if (!ingested) throw test::Failure{ingested.error().message};
  CHECK(ingested.value().audio->interleaved == bakedWav.value().interleaved);
  CHECK(ingested.value().markers.size() == 1U); CHECK(ingested.value().recipe.identity == resource.value().identity);
  const auto corruptPath = root / "corrupt-candidate.json";
  for (const auto* field : {"approval", "recipeHash", "audioSha256", "frameCount", "schemaVersion"}) {
    auto corrupt = bakeJson.value();
    if (std::string_view{field} == "frameCount") corrupt.asObject()[field] = formats::JsonValue{std::int64_t{1}};
    else if (std::string_view{field} == "schemaVersion") corrupt.asObject()[field] = formats::JsonValue{1.5};
    else if (std::string_view{field} == "audioSha256" || std::string_view{field} == "recipeHash") corrupt.asObject()[field] = formats::JsonValue{std::string(64U, 'f')};
    else corrupt.asObject()[field] = formats::JsonValue{"invalid"};
    CHECK(core::durableAtomicWriteText(corruptPath, formats::stringifyJson(corrupt)));
    CHECK(!voice_design::loadProceduralCandidate(corruptPath, prefix.string() + ".wav", resource.value()));
  }
  auto badMarker = bakeJson.value();
  badMarker.find("markers")->asArray().front().asObject()["endFrame"] = formats::JsonValue{std::int64_t{99999999}};
  CHECK(core::durableAtomicWriteText(corruptPath, formats::stringifyJson(badMarker)));
  CHECK(!voice_design::loadProceduralCandidate(corruptPath, prefix.string() + ".wav", resource.value()));
  badMarker = bakeJson.value(); badMarker.find("markers")->asArray().push_back(badMarker.find("markers")->asArray().front());
  CHECK(core::durableAtomicWriteText(corruptPath, formats::stringifyJson(badMarker)));
  CHECK(!voice_design::loadProceduralCandidate(corruptPath, prefix.string() + ".wav", resource.value()));
  std::stop_source ingestStop; ingestStop.request_stop();
  auto integerAudio = core::readFileBytesLimited(prefix.string() + ".wav", 1024U * 1024U); CHECK(integerAudio);
  integerAudio.value()[20] = std::byte{1}; integerAudio.value()[21] = std::byte{0};
  const auto integerPath = root / "integer-candidate.wav";
  CHECK(core::durableAtomicWrite(integerPath, integerAudio.value()));
  auto integerMetadata = bakeJson.value();
  integerMetadata.asObject()["audioSha256"] = formats::JsonValue{core::sha256Hex(std::span<const std::byte>{integerAudio.value()})};
  CHECK(core::durableAtomicWriteText(corruptPath, formats::stringifyJson(integerMetadata)));
  CHECK(!voice_design::loadProceduralCandidate(corruptPath, integerPath, resource.value()));
  std::filesystem::create_symlink(prefix.string() + ".wav", root / "linked-candidate.wav");
  CHECK(!voice_design::loadProceduralCandidate(prefix.string() + ".json", root / "linked-candidate.wav", resource.value()));
  CHECK(!voice_design::loadProceduralCandidate(prefix.string() + ".json", prefix.string() + ".wav", resource.value(), ingestStop.get_token()));
  namespace production = voicebank_production;
  const auto licensePath = root / "synthetic-test-license.txt";
  CHECK(core::durableAtomicWriteText(licensePath, "SYNTHETIC TEST FIXTURE ONLY"));
  const auto licenseHash = core::sha256File(licensePath); CHECK(licenseHash);
  production::VoicebankProductionProject producer{.projectId = "candidate-import-test", .inventoryId = "vowel-inventory",
      .inventorySha256 = std::string(64U, 'a'), .selectedSourceStrategyId = "procedural-fixture",
      .licenseLocator = licensePath.string(), .licenseSha256 = licenseHash.value(), .immutableAssetRoot = "assets"};
  producer.sourceStrategies = {{.id = "procedural-fixture", .kind = production::SourceStrategyKind::ProceduralSynthesis,
      .rights = production::Feasibility::Pass, .coverage = production::Feasibility::Pass, .listening = production::Feasibility::Pass,
      .permissions = {true, true, true, true}, .licenseLocator = licensePath.string(), .licenseSha256 = licenseHash.value(),
      .evidenceState = "SYNTHETIC_TEST_ONLY"}};
  producer.operators = {{"producer", "PRODUCER"}};
  producer.unitAssignments = {{.coverageKey = "vowel:a", .pitchLayer = 69, .promptId = "prompt-a", .plannedTakeId = "take-a"}};
  production::ProductionProjectRepository repository{root / "producer"};
  CHECK(repository.initialize(producer, {.action = "create", .subjectId = producer.projectId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-06T00:00:00Z"}));
  const production::ProductionJournalEvent importEvent{.action = "import-procedural", .subjectId = "take-a",
      .operatorId = "producer", .occurredAtUtc = "2026-09-06T00:00:01Z"};
  production::RawTakeInput take{.takeId = "take-a", .promptId = "prompt-a", .coverageKey = "vowel:a", .pitchLayer = 69};
  const auto beforeCancelledImport = production::encodeProductionProject(producer);
  const auto cancelledImport = repository.importProceduralCandidate(
      producer, prefix.string() + ".json", prefix.string() + ".wav",
      resource.value(), take, importEvent, ingestStop.get_token());
  CHECK(!cancelledImport);
  CHECK(cancelledImport.error().code == core::ErrorCode::Conflict);
  CHECK(production::encodeProductionProject(producer) == beforeCancelledImport);
  CHECK(production::encodeProductionProject(repository.recover().value()) == beforeCancelledImport);
  CHECK(std::filesystem::is_empty(root / "producer" / "assets"));
  auto approved = take; approved.initialState = production::UnitQueueState::Approved;
  CHECK(!repository.importProceduralCandidate(producer, prefix.string() + ".json", prefix.string() + ".wav",
      resource.value(), approved, importEvent));
  CHECK(producer.takes.empty()); CHECK(producer.metadataRevisions.empty());
  const auto generation = producer.lastDurableGeneration;
  const auto imported = repository.importProceduralCandidate(producer, prefix.string() + ".json", prefix.string() + ".wav",
      resource.value(), take, importEvent);
  if (!imported) throw test::Failure{imported.error().message + ": " + imported.error().context};
  CHECK(producer.lastDurableGeneration == generation + 1U);
  CHECK(producer.takes.front().state == production::UnitQueueState::MarkerReview); CHECK(producer.reviews.empty());
  CHECK(!producer.unitAssignments.front().markerReviewed); CHECK(!producer.unitAssignments.front().pitchReviewed);
  CHECK(producer.metadataRevisions.size() == 1U);
  const auto& lineage = producer.metadataRevisions.front();
  CHECK(lineage.kind == "procedural-lineage"); CHECK(lineage.rawAssetSha256 == imported.value().sha256);
  CHECK(lineage.values.at("recipeHash") == resource.value().identity.contentHash);
  CHECK(core::sha256Hex(lineage.values.at("recipeJson")) == resource.value().identity.contentHash);
  CHECK(lineage.values.at("candidateMetadata") == bakeText.value());
  CHECK(repository.verify(producer));
  const auto recoveredProducer = repository.recover(); CHECK(recoveredProducer);
  CHECK(recoveredProducer.value().metadataRevisions.front().values == lineage.values);
  auto forgedProducer = producer;
  forgedProducer.metadataRevisions.front().values["recipeJson"] += " ";
  CHECK(!repository.save(forgedProducer, {.action = "save", .subjectId = producer.projectId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-06T00:00:02Z"}));
  CHECK(repository.recover().value().lastDurableGeneration == producer.lastDurableGeneration);
  producer.unitAssignments.front().markerReviewed = true;
  producer.unitAssignments.front().pitchReviewed = true;
  auto retake = take; retake.takeId = "take-b"; retake.supersedesTakeId = "take-a";
  const auto replacedCandidate = repository.importProceduralCandidate(producer, prefix.string() + ".json", prefix.string() + ".wav",
      resource.value(), retake, {.action = "retake", .subjectId = "take-b", .operatorId = "producer", .occurredAtUtc = "2026-09-06T00:00:03Z"});
  CHECK(replacedCandidate);
  CHECK(producer.takes.front().state == production::UnitQueueState::Retake);
  CHECK(producer.takes.back().state == production::UnitQueueState::MarkerReview);
  CHECK(!producer.unitAssignments.front().markerReviewed); CHECK(!producer.unitAssignments.front().pitchReviewed);
  CHECK(producer.metadataRevisions.size() == 2U);
  const auto savedGeneration = producer.lastDurableGeneration;
  retake.takeId = "take-c"; retake.supersedesTakeId = "take-b";
  CHECK(!repository.importProceduralCandidate(producer, prefix.string() + ".json", prefix.string() + ".wav",
      resource.value(), retake, {.action = "retake", .subjectId = "take-c", .operatorId = "unknown", .occurredAtUtc = "2026-09-06T00:00:04Z"}));
  CHECK(producer.lastDurableGeneration == savedGeneration); CHECK(producer.takes.size() == 2U);
  CHECK(producer.metadataRevisions.size() == 2U); CHECK(producer.unitAssignments.front().takeId == "take-b");
  CHECK(repository.recover().value().lastDurableGeneration == savedGeneration);
#if defined(SEAM_TEST_VOICEBANK_CLI) && (defined(__APPLE__) || defined(__linux__))
  std::vector<std::string> cliArguments{"import-procedural", (root / "producer").string(), prefix.string() + ".json",
      prefix.string() + ".wav", (root / "singer.json").string(), "take-cli", "prompt-a", "vowel:a", "69",
      "producer", "2026-09-06T00:00:05Z", "take-b"};
  CHECK(runVoicebankCli(cliArguments) == 0);
  const auto cliProject = repository.recover(); CHECK(cliProject);
  CHECK(cliProject.value().lastDurableGeneration == savedGeneration + 1U);
  CHECK(cliProject.value().takes.back().takeId == "take-cli");
  CHECK(cliProject.value().takes.back().state == production::UnitQueueState::MarkerReview);
  CHECK(cliProject.value().metadataRevisions.back().kind == "procedural-lineage");
  cliArguments[8] = "69garbage";
  CHECK(runVoicebankCli(cliArguments) != 0);
  CHECK(repository.recover().value().lastDurableGeneration == savedGeneration + 1U);
#endif
  CHECK(project == projectBeforePackage);
  native_ui::VoicebankStudioController studio;
  CHECK(!studio.importSelectedProceduralCandidate(prefix.string() + ".json", prefix.string() + ".wav", root / "singer.json"));
  CHECK(studio.openProductionProject(root / "producer", producer.inventorySha256, "producer"));
  CHECK(studio.candidateMarkerPreview());
  CHECK(studio.candidateMarkerError().empty());
  CHECK(studio.candidateMarkerPreview()->markers == metadataPreview.value().markers);
  CHECK(!studio.candidateMarkerPreview()->audio);
  const auto epoch = studio.productionSessionEpoch();
  const auto initialGeneration = studio.productionProject()->lastDurableGeneration;
  CHECK(studio.validateProductionImportContext(epoch, initialGeneration, 0U));
  CHECK(!studio.validateProductionImportContext(epoch, initialGeneration, 1U));
  CHECK(studio.openProductionProject(root / "producer", producer.inventorySha256, "producer"));
  CHECK(studio.productionSessionEpoch() == epoch + 1U);
  CHECK(!studio.validateProductionImportContext(epoch, initialGeneration, 0U));
  CHECK(studio.selectUnit(0U));
  CHECK(studio.inspectSelectedProductionTake(prefix.string() + ".wav")); CHECK(studio.takeInspection());
  const auto nativeGeneration = studio.productionProject()->lastDurableGeneration;
  const auto statusBeforeCancel = studio.status();
  CHECK(!studio.importSelectedProceduralCandidate(prefix.string() + ".json", prefix.string() + ".wav",
      root / "singer.json", "2026-09-06T00:00:06Z", ingestStop.get_token()));
  CHECK(studio.productionProject()->lastDurableGeneration == nativeGeneration);
  CHECK(studio.takeInspection());
  CHECK(studio.status() == statusBeforeCancel);
  CHECK(repository.recover().value().lastDurableGeneration == nativeGeneration);
  CHECK(studio.beginProceduralCandidateImport(prefix.string() + ".json", prefix.string() + ".wav", root / "singer.json",
      "2026-09-06T00:00:06Z"));
  CHECK(studio.proceduralImportBusy());
  CHECK(studio.productionProject()->lastDurableGeneration == nativeGeneration);
  CHECK(!studio.selectUnit(0U));
  CHECK(!studio.save());
  CHECK(!studio.openProductionProject(root / "producer", producer.inventorySha256, "producer"));
  CHECK(!studio.beginProceduralCandidateImport(prefix.string() + ".json", prefix.string() + ".wav", root / "singer.json"));
  const auto drainImport = [&studio]() {
    auto result = core::success();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (studio.proceduralImportBusy() && std::chrono::steady_clock::now() < deadline) {
      result = studio.pollProceduralCandidateImport();
      if (!result) break;
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    CHECK(!studio.proceduralImportBusy());
    return result;
  };
  CHECK(drainImport());
  CHECK(studio.productionProject()->lastDurableGeneration == nativeGeneration + 1U);
  CHECK(!studio.validateProductionImportContext(studio.productionSessionEpoch(), nativeGeneration, 0U));
  CHECK(studio.productionProject()->takes.back().state == production::UnitQueueState::MarkerReview);
  CHECK(studio.productionProject()->metadataRevisions.back().kind == "procedural-lineage");
  CHECK(studio.candidateMarkerPreview());
  CHECK(studio.candidateMarkerPreview()->audioSha256 == studio.productionProject()->takes.back().rawAssetSha256);
  const auto beforeWaveform = production::encodeProductionProject(*studio.productionProject());
  CHECK(studio.beginCandidateWaveformPreview());
  CHECK(studio.proceduralImportBusy());
  CHECK(!studio.selectUnit(0U));
  CHECK(!studio.save());
  CHECK(!studio.beginCandidateWaveformPreview());
  CHECK(drainImport());
  CHECK(studio.candidateWaveform());
  CHECK(studio.candidateWaveformError().empty());
  CHECK(studio.candidateWaveform()->audioSha256 == studio.candidateMarkerPreview()->audioSha256);
  CHECK(studio.candidateWaveform()->frameCount == static_cast<time::SampleFrame>(bakedWav.value().frameCount()));
  CHECK(studio.candidateWaveform()->peaks.size() == 1024U);
  long double sum = 0.0L, squares = 0.0L;
  float measuredPeak = 0.0F;
  std::size_t nearFullScale = 0U;
  for (const auto sample : bakedWav.value().interleaved) {
    sum += sample;
    squares += static_cast<long double>(sample) * sample;
    measuredPeak = std::max(measuredPeak, std::abs(sample));
    if (std::abs(sample) >= 0.9999F) ++nearFullScale;
  }
  const auto sampleCount = static_cast<long double>(bakedWav.value().interleaved.size());
  CHECK(studio.candidateWaveform()->statistics.peak == measuredPeak);
  CHECK_NEAR(studio.candidateWaveform()->statistics.rms, std::sqrt(static_cast<double>(squares / sampleCount)), 1e-12);
  CHECK_NEAR(studio.candidateWaveform()->statistics.dcOffset, static_cast<double>(sum / sampleCount), 1e-12);
  CHECK(studio.candidateWaveform()->statistics.clippedSamples == nearFullScale);
  for (std::size_t index = 0U; index < 1024U; ++index) {
    const auto& samples = bakedWav.value().interleaved;
    const auto begin = samples.begin() + static_cast<std::ptrdiff_t>(index * samples.size() / 1024U);
    const auto end = samples.begin() + static_cast<std::ptrdiff_t>((index + 1U) * samples.size() / 1024U);
    const auto [low, high] = std::minmax_element(begin, end);
    CHECK(studio.candidateWaveform()->peaks[index] == std::pair(*low, *high));
  }
  CHECK(production::encodeProductionProject(repository.recover().value()) == beforeWaveform);
  CHECK(production::encodeProductionProject(*studio.productionProject()) == beforeWaveform);
  for (const auto width : {720U, 1440U}) {
    studio.resize(static_cast<double>(width), 900.0);
    native_ui::PixelSurface surface{width, 900U};
    native_ui::RasterCanvas canvas{surface};
    native_ui::VoicebankStudioScenePainter{}.paint(canvas, studio);
    CHECK(surface.writePpm(root / ("candidate-markers-" + std::to_string(width) + ".ppm")));
  }
  CHECK(!studio.takeInspection()); CHECK(studio.status() == "RAW WAVEFORM / NOT REVIEWED");
  CHECK(studio.productionProject()->reviews.empty());
  CHECK(studio.beginCandidatePitchInspection());
  CHECK(!studio.selectCandidateMarker(0U));
  CHECK(!studio.editSelectedCandidateMarker(1, 23000));
  CHECK(drainImport());
  CHECK(studio.candidatePitchInspection());
  CHECK(studio.candidatePitchView());
  CHECK(studio.toggleCandidatePitchView()); CHECK(!studio.candidatePitchView());
  CHECK(studio.toggleCandidatePitchView()); CHECK(studio.candidatePitchView());
  const auto& measuredPitch = *studio.candidatePitchInspection();
  CHECK(measuredPitch.audioSha256 == studio.candidateMarkerPreview()->audioSha256);
  CHECK(measuredPitch.key == studio.candidateMarkerPreview()->markers.front().key);
  CHECK(measuredPitch.start == 0); CHECK(measuredPitch.end == 24000);
  CHECK(measuredPitch.targetMidi == 69);
  CHECK(measuredPitch.medianHz); CHECK(measuredPitch.centsFromTarget);
  CHECK_NEAR(*measuredPitch.medianHz, 440.0, 10.0);
  const auto referencePitch = voicebank::analyzePitch(bakedWav.value().interleaved, 48000U, measuredPitch.config); CHECK(referencePitch);
  CHECK(measuredPitch.frames.size() == referencePitch.value().size());
  for (std::size_t index = 0U; index < measuredPitch.frames.size(); ++index) {
    CHECK(measuredPitch.frames[index].sourceFrame == referencePitch.value()[index].sourceFrame);
    CHECK(measuredPitch.frames[index].f0Hz == referencePitch.value()[index].f0Hz);
  }
  CHECK(production::encodeProductionProject(*studio.productionProject()) == beforeWaveform);
  CHECK(production::encodeProductionProject(repository.recover().value()) == beforeWaveform);
  studio.resize(720.0, 520.0);
  const auto evidencePath = root / "pitch-inspection.json";
  CHECK(!studio.exportCandidatePitchInspection(evidencePath, "invalid-time"));
  CHECK(!std::filesystem::exists(evidencePath));
  CHECK(studio.exportCandidatePitchInspection(evidencePath, "2026-09-07T02:00:00Z"));
  const auto evidenceBytes = core::readTextFileLimited(evidencePath, 1024U * 1024U); CHECK(evidenceBytes);
  const auto evidence = formats::parseJson(evidenceBytes.value()); CHECK(evidence);
  CHECK(evidence.value().find("status")->asString() == "ESTIMATE_ONLY");
  CHECK(!evidence.value().find("approved")->asBool());
  CHECK(evidence.value().find("audioSha256")->asString() == measuredPitch.audioSha256);
  CHECK(evidence.value().find("boundaryRevisionId")->asString() == measuredPitch.boundaryRevisionId);
  CHECK(evidence.value().find("takeId")->asString() == studio.selectedProductionAssignment()->takeId);
  CHECK(evidence.value().find("sourceGeneration")->asString() == std::to_string(nativeGeneration + 1U));
  CHECK(evidence.value().find("frames")->asArray().size() == measuredPitch.frames.size());
  CHECK(evidence.value().find("correlationMethod")->asString() == "fft");
  CHECK(!studio.exportCandidatePitchInspection(evidencePath, "2026-09-07T02:00:01Z"));
  CHECK(core::readTextFileLimited(evidencePath, 1024U * 1024U).value() == evidenceBytes.value());
  CHECK(production::encodeProductionProject(*studio.productionProject()) == beforeWaveform);
  native_ui::PixelSurface pitchSurface{720U, 520U};
  native_ui::RasterCanvas pitchCanvas{pitchSurface};
  native_ui::VoicebankStudioScenePainter{}.paint(pitchCanvas, studio);
  CHECK(pitchSurface.writePpm(root / "candidate-pitch-720x520.ppm"));
  CHECK(studio.beginCandidatePitchInspection());
  studio.cancelProceduralCandidateImport();
  const auto cancelledPitch = studio.finishProceduralCandidateImport();
  CHECK(!studio.proceduralImportBusy());
  if (!cancelledPitch) CHECK(!studio.candidatePitchInspection());
  CHECK(production::encodeProductionProject(*studio.productionProject()) == beforeWaveform);
  CHECK(repository.recover().value().lastDurableGeneration == nativeGeneration + 1U);
  CHECK(studio.inspectSelectedProductionTake(prefix.string() + ".wav"));
  const auto beforeWorkerFailure = production::encodeProductionProject(*studio.productionProject());
  const auto beforeWorkerStatus = studio.status();
  CHECK(studio.beginProceduralCandidateImport(prefix.string() + ".json", prefix.string() + ".wav", root / "missing-recipe.json"));
  CHECK(!drainImport());
  CHECK(production::encodeProductionProject(*studio.productionProject()) == beforeWorkerFailure);
  CHECK(studio.takeInspection());
  CHECK(studio.status() == beforeWorkerStatus);
  CHECK(studio.selectUnit(0U));
  CHECK(!studio.candidateWaveform());
  CHECK(studio.candidateMarkerPreview());
  const auto rawAsset = std::find_if(studio.productionProject()->assets.begin(), studio.productionProject()->assets.end(),
      [&](const auto& asset) { return asset.sha256 == studio.candidateMarkerPreview()->audioSha256; });
  CHECK(rawAsset != studio.productionProject()->assets.end());
  const auto rawPath = repository.assetPath(*rawAsset);
  const auto rawBytes = core::readFileBytesLimited(rawPath, 4U * 1024U * 1024U); CHECK(rawBytes);
  CHECK(core::durableAtomicWriteText(rawPath, "invalid candidate audio"));
  CHECK(studio.beginCandidateWaveformPreview());
  CHECK(!drainImport());
  CHECK(!studio.candidateWaveform());
  CHECK(!studio.candidateWaveformError().empty());
  CHECK(studio.candidateMarkerPreview());
  CHECK(core::durableAtomicWrite(rawPath, rawBytes.value()));
  CHECK(studio.beginProceduralCandidateImport(prefix.string() + ".json", prefix.string() + ".wav", root / "singer.json"));
  studio.cancelProceduralCandidateImport();
  const auto cancelledWorker = studio.finishProceduralCandidateImport();
  CHECK(!studio.proceduralImportBusy());
  // Cancellation can race the commit boundary: never hide a committed take.
  CHECK(studio.productionProject()->lastDurableGeneration == nativeGeneration + (cancelledWorker ? 2U : 1U));
  CHECK(production::encodeProductionProject(repository.recover().value()) ==
        production::encodeProductionProject(*studio.productionProject()));
  // Synthetic planned subdivisions test navigation; they are not measured
  // acoustic boundaries or a claim that the renderer generated 24 phones.
  auto navigationMetadata = bakeJson.value();
  formats::JsonValue::Array navigationMarkers;
  for (std::uint16_t index = 0U; index < 24U; ++index) {
    auto marker = bakedMarkers.front();
    marker.asObject()["key"] = formats::JsonValue{domain::PhonemeKey{
        ingested.value().markers.front().key.noteId, index}.toString()};
    marker.asObject()["startFrame"] = formats::JsonValue{static_cast<std::int64_t>(index) * 1000};
    marker.asObject()["endFrame"] = formats::JsonValue{(static_cast<std::int64_t>(index) + 1) * 1000};
    navigationMarkers.push_back(std::move(marker));
  }
  navigationMetadata.asObject()["markers"] = formats::JsonValue{std::move(navigationMarkers)};
  const auto navigationPath = root / "navigation-fixture.json";
  CHECK(core::durableAtomicWriteText(navigationPath, formats::stringifyJson(navigationMetadata)));
  CHECK(studio.beginProceduralCandidateImport(navigationPath, prefix.string() + ".wav", root / "singer.json"));
  CHECK(!studio.selectCandidateMarker(1U));
  CHECK(drainImport());
  CHECK(studio.candidateMarkerPreview()->markers.size() == 24U);
  CHECK(!studio.candidatePitchInspection());
  CHECK(!studio.exportCandidatePitchInspection(root / "stale-pitch.json"));
  CHECK(!std::filesystem::exists(root / "stale-pitch.json"));
  const auto beforeNavigation = production::encodeProductionProject(*studio.productionProject());
  CHECK(studio.candidateMarkerWindow(0U) == std::pair(std::size_t{0}, std::size_t{0}));
  for (std::size_t index = 0U; index < 24U; ++index) {
    CHECK(studio.selectCandidateMarker(index));
    const auto [first, count] = studio.candidateMarkerWindow(3U);
    CHECK(count == 3U);
    CHECK(index >= first && index < first + count);
  }
  CHECK(!studio.selectCandidateMarker(24U));
  CHECK(studio.selectedCandidateMarker() == 23U);
  CHECK(studio.moveCandidateMarker(std::numeric_limits<std::int32_t>::max()));
  CHECK(studio.selectedCandidateMarker() == 23U);
  CHECK(studio.moveCandidateMarker(std::numeric_limits<std::int32_t>::min()));
  CHECK(studio.selectedCandidateMarker() == 0U);
  CHECK(studio.selectCandidateMarker(23U));
  CHECK(studio.beginCandidateWaveformPreview());
  CHECK(drainImport());
  CHECK(studio.selectedCandidateMarker() == 23U);
  CHECK(!studio.beginCandidatePitchInspection()); // 1000 frames cannot supply an unpadded window.
  studio.resize(720.0, 520.0);
  native_ui::PixelSurface navigationSurface{720U, 520U};
  native_ui::RasterCanvas navigationCanvas{navigationSurface};
  native_ui::VoicebankStudioScenePainter{}.paint(navigationCanvas, studio);
  CHECK(navigationSurface.writePpm(root / "candidate-navigation-720x520.ppm"));
  CHECK(production::encodeProductionProject(*studio.productionProject()) == beforeNavigation);
  CHECK(production::encodeProductionProject(repository.recover().value()) == beforeNavigation);
  CHECK(studio.selectUnit(0U));
  CHECK(studio.selectedCandidateMarker() == 0U);
  // Synthetic prior review state must be invalidated by a manual boundary edit.
  auto reviewedFixture = repository.recover(); CHECK(reviewedFixture);
  reviewedFixture.value().unitAssignments.front().markerReviewed = true;
  reviewedFixture.value().unitAssignments.front().pitchReviewed = true;
  reviewedFixture.value().unitAssignments.front().state = production::UnitQueueState::Approved;
  reviewedFixture.value().takes.back().state = production::UnitQueueState::Approved;
  CHECK(repository.save(reviewedFixture.value(), {.action = "save", .subjectId = reviewedFixture.value().projectId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-06T01:00:00Z"}));
  CHECK(studio.openProductionProject(root / "producer", producer.inventorySha256, "producer"));
  const auto originalLineage = studio.candidateMarkerPreview()->metadataJson;
  const auto editGeneration = studio.productionProject()->lastDurableGeneration;
  CHECK(studio.beginCandidateWaveformPreview()); CHECK(drainImport());
  CHECK(studio.editSelectedCandidateMarker(100, 900, "2026-09-06T01:00:01Z"));
  CHECK(studio.productionProject()->lastDurableGeneration == editGeneration + 1U);
  CHECK(studio.candidateMarkerPreview()->metadataJson == originalLineage);
  CHECK(studio.candidateMarkerPreview()->markers.front().ownedSpan.start == 100);
  CHECK(studio.candidateMarkerPreview()->markers.front().ownedSpan.end == 900);
  CHECK(studio.candidateMarkersEdited());
  CHECK(studio.candidateWaveform());
  native_ui::VoicebankStudioScenePainter{}.paint(navigationCanvas, studio);
  CHECK(navigationSurface.writePpm(root / "candidate-manual-bounds-720x520.ppm"));
  CHECK(!studio.productionProject()->unitAssignments.front().markerReviewed);
  CHECK(!studio.productionProject()->unitAssignments.front().pitchReviewed);
  CHECK(studio.productionProject()->unitAssignments.front().state == production::UnitQueueState::MarkerReview);
  const auto beforeInvalidEdit = production::encodeProductionProject(*studio.productionProject());
  CHECK(!studio.editSelectedCandidateMarker(-1, 900));
  CHECK(!studio.editSelectedCandidateMarker(100, 1100));
  CHECK(studio.editSelectedCandidateMarker(100, 900)); // No-op does not save.
  CHECK(production::encodeProductionProject(*studio.productionProject()) == beforeInvalidEdit);
  auto editedFixture = repository.recover(); CHECK(editedFixture);
  auto staleEdit = editedFixture.value().metadataRevisions.back();
  staleEdit.revisionId += "-stale";
  // This predecessor is from before the already committed edit.
  CHECK(!repository.recordMetadataRevision(editedFixture.value(), staleEdit,
      {.action = "marker", .subjectId = staleEdit.revisionId, .operatorId = staleEdit.operatorId,
       .occurredAtUtc = staleEdit.performedAtUtc}));
  CHECK(production::encodeProductionProject(editedFixture.value()) == beforeInvalidEdit);
  CHECK(studio.openProductionProject(root / "producer", producer.inventorySha256, "producer"));
  CHECK(studio.candidateMarkerPreview()->markers.front().ownedSpan.start == 100);
  CHECK(studio.candidateMarkerPreview()->metadataJson == originalLineage);
  CHECK(!studio.canUndoCandidateMarkerEdit()); // Session history resets on reopen.
  CHECK(!studio.undoCandidateMarkerEdit());
  const auto historyGeneration = studio.productionProject()->lastDurableGeneration;
  CHECK(studio.editSelectedCandidateMarker(200, 800));
  CHECK(studio.canUndoCandidateMarkerEdit()); CHECK(!studio.canRedoCandidateMarkerEdit());
  CHECK(studio.selectCandidateMarker(1U));
  CHECK(studio.undoCandidateMarkerEdit());
  CHECK(studio.selectedCandidateMarker() == 0U);
  CHECK(studio.candidateMarkerPreview()->markers.front().ownedSpan.start == 100);
  CHECK(studio.productionProject()->lastDurableGeneration == historyGeneration + 2U);
  CHECK(!studio.productionProject()->unitAssignments.front().markerReviewed);
  CHECK(studio.canRedoCandidateMarkerEdit()); CHECK(!studio.canUndoCandidateMarkerEdit());
  CHECK(studio.redoCandidateMarkerEdit());
  CHECK(studio.candidateMarkerPreview()->markers.front().ownedSpan.start == 200);
  CHECK(studio.productionProject()->lastDurableGeneration == historyGeneration + 3U);
  CHECK(studio.undoCandidateMarkerEdit());
  CHECK(!studio.editSelectedCandidateMarker(-1, 900));
  CHECK(studio.editSelectedCandidateMarker(100, 900));
  CHECK(studio.canRedoCandidateMarkerEdit()); // Failure/no-op preserve redo.
  CHECK(studio.editSelectedCandidateMarker(300, 700));
  CHECK(!studio.canRedoCandidateMarkerEdit());
  CHECK(studio.undoCandidateMarkerEdit());
  CHECK(studio.canRedoCandidateMarkerEdit());
  const auto beforeStaleRedo = production::encodeProductionProject(*studio.productionProject());
  auto externalWriter = repository.recover(); CHECK(externalWriter);
  CHECK(repository.save(externalWriter.value(), {.action = "save", .subjectId = externalWriter.value().projectId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-07T00:00:00Z"}));
  CHECK(!studio.redoCandidateMarkerEdit());
  CHECK(studio.canRedoCandidateMarkerEdit());
  CHECK(production::encodeProductionProject(*studio.productionProject()) == beforeStaleRedo);
  CHECK(studio.openProductionProject(root / "producer", producer.inventorySha256, "producer"));
  CHECK(!studio.canRedoCandidateMarkerEdit()); CHECK(!studio.canUndoCandidateMarkerEdit());
  CHECK(studio.candidateMarkerPreview()->markers.front().ownedSpan.start == 100);
  CHECK(studio.candidateMarkerPreview()->metadataJson == originalLineage);
  CHECK(studio.beginCandidateWaveformPreview()); CHECK(drainImport());
  const auto dragBounds = native_ui::VoicebankStudioController::candidateWaveformBounds(studio.logicalWidth(), studio.logicalHeight());
  const auto framePoint = [&](time::SampleFrame frame, bool endHandle = false) {
    return ui::Point{dragBounds.x + static_cast<double>(frame) / static_cast<double>(studio.candidateMarkerPreview()->frameCount) * dragBounds.width,
        endHandle ? dragBounds.bottom() - 4.0 : dragBounds.y + 4.0};
  };
  const auto dragGeneration = studio.productionProject()->lastDurableGeneration;
  CHECK(!studio.beginCandidateMarkerDrag({0.0, 0.0}).value());
  CHECK(studio.beginCandidateMarkerDrag(framePoint(100)).value());
  CHECK(studio.updateCandidateMarkerDrag(framePoint(300)));
  CHECK(studio.updateCandidateMarkerDrag(framePoint(400)));
  CHECK(!studio.save()); CHECK(!studio.selectCandidateMarker(1U));
  CHECK(studio.candidateMarkerPreview()->markers.front().ownedSpan.start == 400);
  CHECK(studio.productionProject()->lastDurableGeneration == dragGeneration);
  native_ui::VoicebankStudioScenePainter{}.paint(navigationCanvas, studio);
  CHECK(navigationSurface.writePpm(root / "candidate-drag-draft-720x520.ppm"));
  CHECK(repository.recover().value().lastDurableGeneration == dragGeneration);
  studio.cancelCandidateMarkerDrag();
  CHECK(studio.candidateMarkerPreview()->markers.front().ownedSpan.start == 100);
  CHECK(!studio.canUndoCandidateMarkerEdit());
  CHECK(studio.beginCandidateMarkerDrag(framePoint(100)).value());
  CHECK(studio.updateCandidateMarkerDrag(framePoint(400)));
  CHECK(studio.finishCandidateMarkerDrag());
  CHECK(studio.productionProject()->lastDurableGeneration == dragGeneration + 1U);
  CHECK(studio.candidateMarkerPreview()->markers.front().ownedSpan.start == 400);
  CHECK(studio.undoCandidateMarkerEdit());
  CHECK(studio.candidateMarkerPreview()->markers.front().ownedSpan.start == 100);
  CHECK(studio.beginCandidateMarkerDrag(framePoint(900, true)).value());
  CHECK(studio.updateCandidateMarkerDrag(framePoint(2000, true)));
  CHECK(studio.candidateMarkerPreview()->markers.front().ownedSpan.end == 1000); // Neighbor clamp.
  CHECK(!studio.updateCandidateMarkerDrag({std::numeric_limits<double>::quiet_NaN(), 0.0}));
  studio.resize(studio.logicalWidth(), studio.logicalHeight());
  CHECK(!studio.candidateMarkerDragging());
  CHECK(studio.candidateMarkerPreview()->markers.front().ownedSpan.end == 900);
  CHECK(studio.beginCandidateMarkerDrag(framePoint(100)).value());
  CHECK(studio.updateCandidateMarkerDrag(framePoint(500)));
  auto dragCompetitor = repository.recover(); CHECK(dragCompetitor);
  CHECK(repository.save(dragCompetitor.value(), {.action = "save", .subjectId = dragCompetitor.value().projectId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-07T00:00:01Z"}));
  CHECK(!studio.finishCandidateMarkerDrag());
  CHECK(!studio.candidateMarkerDragging());
  CHECK(studio.candidateMarkerPreview()->markers.front().ownedSpan.start == 100);
  CHECK(studio.canRedoCandidateMarkerEdit());
  const auto beforeZoom = production::encodeProductionProject(*studio.productionProject());
  for (unsigned index = 0U; index < 4U; ++index) CHECK(studio.zoomCandidateWaveform(true));
  CHECK(studio.candidateWaveformView() == std::pair(time::SampleFrame{0}, time::SampleFrame{1500}));
  CHECK(studio.zoomCandidateWaveform(true));
  CHECK(studio.candidateWaveformView() == std::pair(time::SampleFrame{125}, time::SampleFrame{875}));
  CHECK(!studio.beginCandidateMarkerDrag({dragBounds.x, dragBounds.y + 4.0}).value()); // Clipped edge is not a handle.
  CHECK(studio.panCandidateWaveform(false));
  CHECK(studio.candidateWaveformView() == std::pair(time::SampleFrame{0}, time::SampleFrame{750}));
  const auto& zoomPeaks = studio.candidateWaveformPeaks();
  CHECK(zoomPeaks.size() == 750U);
  for (std::size_t index = 0U; index < zoomPeaks.size(); ++index) {
    CHECK(zoomPeaks[index].first == bakedWav.value().interleaved[index]);
    CHECK(zoomPeaks[index].second == bakedWav.value().interleaved[index]);
  }
  const auto zoomPoint = [&](time::SampleFrame frame) {
    return ui::Point{dragBounds.x + static_cast<double>(frame) / 750.0 * dragBounds.width, dragBounds.y + 4.0};
  };
  CHECK(studio.beginCandidateMarkerDrag(zoomPoint(100)).value());
  CHECK(!studio.zoomCandidateWaveform(true));
  CHECK(studio.updateCandidateMarkerDrag(zoomPoint(150)));
  CHECK(studio.candidateMarkerPreview()->markers.front().ownedSpan.start == 150);
  studio.cancelCandidateMarkerDrag();
  native_ui::VoicebankStudioScenePainter{}.paint(navigationCanvas, studio);
  CHECK(navigationSurface.writePpm(root / "candidate-zoom-720x520.ppm"));
  CHECK(studio.selectCandidateMarker(23U));
  const auto [zoomFirst, zoomLast] = studio.candidateWaveformView();
  CHECK(zoomFirst > 0 && zoomLast <= studio.candidateMarkerPreview()->frameCount);
  for (unsigned index = 0U; index < 10U; ++index) CHECK(studio.zoomCandidateWaveform(true));
  CHECK(studio.candidateWaveformView().second - studio.candidateWaveformView().first == 32);
  for (unsigned index = 0U; index < 16U; ++index) CHECK(studio.zoomCandidateWaveform(false));
  CHECK(studio.candidateWaveformView() == std::pair(time::SampleFrame{0}, time::SampleFrame{24000}));
  CHECK(production::encodeProductionProject(*studio.productionProject()) == beforeZoom);
  const auto single = authoring::ExportService{}.exportProjectWithSources(project, sources, trackId, regionId,
      1U, root / "single.wav", voicebank::WavSampleFormat::Float32); CHECK(single);
  const auto singleWav = voicebank::readWav(root / "single.wav"); CHECK(singleWav);
  CHECK(singleWav.value().interleaved == std::vector<float>(expected.value().interleaved.begin(), expected.value().interleaved.end()));
  auto changed = recipe; changed.seed = 42U;
  CHECK(voice_design::saveVoiceRecipeFile(root / "singer.json", changed));
  settings.replaceExisting = true;
  CHECK(!authoring::ExportService{}.exportSetWithSources(project, sources, trackId, regionId, 2U, root / "export", settings));
  CHECK(core::readTextFileLimited(exported.value().receiptPath, 1024U * 1024U).value() == preserved);
}

TEST_CASE("pitch contour preserves window centers and breaks unvoiced missing or invalid gaps") {
  using namespace seam;
  voicebank::PitchConfig config; config.frameSize = 2048U; config.hopSize = 480U;
  const std::vector<voicebank::PitchFrame> frames{
      {100U, 440.0, 0.9, true}, {580U, 880.0, 0.9, true}, {1060U, 0.0, 0.0, false},
      {1540U, 220.0, 0.9, true}, {2500U, 440.0, 0.9, true},
      {2980U, std::numeric_limits<double>::quiet_NaN(), 0.9, true}, {3460U, 1760.0, 0.9, true}};
  const auto points = native_ui::buildPitchContour(frames, config, 69);
  CHECK(points.size() == 5U);
  CHECK(points[0].sourceFrame == 1124.0); CHECK(points[0].centsFromTarget == 0.0);
  CHECK(!points[0].connectedToPrevious); CHECK(points[1].connectedToPrevious);
  CHECK_NEAR(points[1].centsFromTarget, 1200.0, 1e-9);
  CHECK_NEAR(points[2].centsFromTarget, -1200.0, 1e-9);
  CHECK(!points[2].connectedToPrevious); CHECK(!points[3].connectedToPrevious); CHECK(!points[4].connectedToPrevious);
  CHECK_NEAR(points[4].centsFromTarget, 2400.0, 1e-9); // Model retains outliers; only presentation clips.
  CHECK(native_ui::buildPitchContour(frames, config, -1).empty());
}

TEST_CASE("candidate audition session reports device failures and releases callback state safely") {
  using namespace seam;
  struct State {
    platform::AudioDeviceInfo info;
    platform::AudioDeviceStats stats;
    platform::IAudioProcessor* processor{};
    bool running{false}, failOpen{false}, failStart{false}, destroyed{false};
    unsigned stops{0U};
  };
  class Device final : public platform::IAudioDevice {
  public:
    explicit Device(std::shared_ptr<State> state) : state_(std::move(state)) {}
    ~Device() override { state_->destroyed = true; state_->processor = nullptr; }
    core::Result<void> open(const platform::AudioDeviceConfig& config, platform::IAudioProcessor& processor) override {
      state_->processor = &processor;
      state_->info.sampleRate = config.sampleRate;
      state_->info.outputChannels = config.outputChannels;
      return state_->failOpen ? core::failure(core::ErrorCode::IoError, "fixture open failure") : core::success();
    }
    core::Result<void> start() override {
      if (state_->failStart) return core::failure(core::ErrorCode::IoError, "fixture start failure");
      state_->running = true; return core::success();
    }
    void stop() noexcept override {
      // Device shutdown may still touch the processor; session must retain it.
      if (state_->processor) {
        std::array<float, 1> sample{};
        state_->processor->process({.sampleRate = 8000.0, .frameCount = 1U, .left = sample});
      }
      state_->running = false; ++state_->stops;
    }
    bool running() const noexcept override { return state_->running; }
    platform::AudioDeviceInfo info() const override { return state_->info; }
    platform::AudioDeviceStats stats() const noexcept override { return state_->stats; }
  private:
    std::shared_ptr<State> state_;
  };
  auto audio = std::make_shared<voicebank::AudioBuffer>();
  audio->sampleRate = 8000U; audio->channels = 1U; audio->interleaved.assign(8000U, 0.1F);
  using Session = native_ui::CandidateAuditionSession;
  const auto now = Session::Clock::time_point{};
  for (unsigned scenario = 0U; scenario < 7U; ++scenario) {
    auto state = std::make_shared<State>();
    state->failOpen = scenario == 0U; state->failStart = scenario == 1U;
    Session session;
    const auto started = session.start(std::make_unique<Device>(state), audio, 0U, 8000U, 0.25F, now);
    if (scenario < 2U) {
      CHECK(!started); CHECK(!session.active());
    } else {
      CHECK(started); CHECK(session.active()); CHECK(session.poll(now).value());
      if (scenario == 2U) state->running = false;
      if (scenario == 3U) state->stats.writeFailures = 1U;
      if (scenario == 4U) state->info.sampleRate = 48000U;
      if (scenario == 5U) {
        CHECK(session.poll(now + std::chrono::seconds{2}).value());
        ++state->stats.callbacks;
        CHECK(session.poll(now + std::chrono::seconds{2}).value());
        CHECK(session.poll(now + std::chrono::seconds{4}).value());
      }
      if (scenario == 6U) {
        std::vector<float> output(8000U);
        state->processor->process({.sampleRate = 8000.0, .frameCount = output.size(), .left = output});
        const auto complete = session.poll(now); CHECK(complete); CHECK(!complete.value());
      } else {
        const auto failed = session.poll(scenario == 5U ? now + std::chrono::seconds{5} : now);
        CHECK(!failed);
      }
      CHECK(!session.active()); CHECK(!session.poll(now).value());
    }
    CHECK(state->destroyed); CHECK(state->stops == 1U);
  }
  auto state = std::make_shared<State>();
  {
    Session session;
    CHECK(session.start(std::make_unique<Device>(state), audio, 0U, 8000U, 0.25F, now));
  }
  CHECK(state->destroyed); CHECK(state->stops == 1U);
}

TEST_CASE("candidate audition is bounded block invariant and runs on the output callback") {
  using namespace seam;
  auto audio = std::make_shared<voicebank::AudioBuffer>();
  audio->sampleRate = 8000U;
  audio->channels = 1U;
  audio->bitsPerSample = 32U;
  audio->interleaved.assign(1600U, 0.2F);
  CHECK(!native_ui::CandidateAuditionProcessor::create(audio, 20U, 20U, 0.25F));
  CHECK(!native_ui::CandidateAuditionProcessor::create(audio, 0U, 1601U, 0.25F));
  CHECK(!native_ui::CandidateAuditionProcessor::create(audio, 0U, 1600U, 1.0F));
  auto whole = native_ui::CandidateAuditionProcessor::create(audio, 100U, 1500U, 0.25F); CHECK(whole);
  std::vector<float> left(1500U, 9.0F), right(1500U, 9.0F);
  whole.value()->process({.sampleRate = 8000.0, .frameCount = 1500U, .left = left, .right = right});
  CHECK(whole.value()->finished()); CHECK(!whole.value()->failed());
  CHECK(left == right); CHECK(left.front() == 0.0F); CHECK(left[1399U] == 0.0F);
  CHECK_NEAR(left[500U], 0.05, 1e-7);
  CHECK(std::all_of(left.begin() + 1400, left.end(), [](float value) { return value == 0.0F; }));
  auto chunked = native_ui::CandidateAuditionProcessor::create(audio, 100U, 1500U, 0.25F); CHECK(chunked);
  std::vector<float> output(1500U, 9.0F);
  for (std::size_t offset = 0U; offset < output.size(); offset += 73U) {
    const auto count = std::min<std::size_t>(73U, output.size() - offset);
    chunked.value()->process({.sampleRate = 8000.0, .frameCount = count,
        .left = std::span<float>{output}.subspan(offset, count)});
  }
  CHECK(output == left);
  auto mismatch = native_ui::CandidateAuditionProcessor::create(audio, 0U, 1600U, 0.25F); CHECK(mismatch);
  mismatch.value()->process({.sampleRate = 48000.0, .frameCount = left.size(), .left = left});
  CHECK(mismatch.value()->failed());
  CHECK(std::all_of(left.begin(), left.end(), [](float value) { return value == 0.0F; }));
  auto threaded = native_ui::CandidateAuditionProcessor::create(audio, 0U, 1600U, 0.25F); CHECK(threaded);
  auto device = platform::createThreadedAudioDevice();
  CHECK(device->open({.sampleRate = 8000U, .blockFrames = 64U, .outputChannels = 2U}, *threaded.value()));
  CHECK(device->start());
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
  while (!threaded.value()->finished() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  device->stop();
  CHECK(threaded.value()->finished()); CHECK(!threaded.value()->failed());
  CHECK(device->stats().callbacks > 0U); CHECK(!device->info().physical);
}

TEST_CASE("export service publishes a committed master and receipt atomically") {
  const auto root = seam::test::support::temporaryDirectory("export-service");
  const auto destination = root / "master.wav";
  seam::rendering::ProjectRenderResult rendered{
      .sampleRate = 48000U,
      .channelCount = 2U,
      .interleaved = {0.0F, 0.0F, 0.25F, -0.25F, 0.5F, -0.5F},
  };
  seam::authoring::ExportService service;
  const auto result = service.commitRendered(
      rendered, 42U, destination, seam::voicebank::WavSampleFormat::Pcm24);
  CHECK(result);
  CHECK(result.value().state == seam::authoring::ExportState::Committed);
  CHECK(std::filesystem::exists(destination));
  CHECK(std::filesystem::exists(result.value().receiptPath));
  const auto decoded = seam::voicebank::readWav(destination);
  CHECK(decoded);
  CHECK(decoded.value().channels == 2U);
  CHECK(decoded.value().frameCount() == 3U);
}

TEST_CASE("single-file export removes rollback backups after a successful replacement") {
  const auto root = seam::test::support::temporaryDirectory("export-repeat");
  const auto destination = root / "master.wav";
  seam::rendering::ProjectRenderResult rendered{
      .sampleRate = 48000U,
      .channelCount = 1U,
      .interleaved = {0.1F, 0.2F, 0.3F, 0.4F},
  };
  seam::authoring::ExportService service;
  CHECK(service.commitRendered(
      rendered, 1U, destination, seam::voicebank::WavSampleFormat::Pcm16));
  CHECK(service.commitRendered(
      rendered, 2U, destination, seam::voicebank::WavSampleFormat::Pcm16));
  CHECK(!std::filesystem::exists(destination.string() + ".previous"));
  CHECK(!std::filesystem::exists(destination.string() + ".receipt.json.previous"));
  const auto third = service.commitRendered(
      rendered, 3U, destination, seam::voicebank::WavSampleFormat::Pcm16);
  CHECK(third);
  CHECK(third.value().projectRevision == 3U);
}

TEST_CASE("single-file export preserves an unowned backup collision") {
  const auto root = seam::test::support::temporaryDirectory("export-backup-canary");
  const auto destination = root / "master.wav";
  seam::rendering::ProjectRenderResult rendered{
      .sampleRate = 48000U,
      .channelCount = 1U,
      .interleaved = {0.1F, 0.2F, 0.3F, 0.4F},
  };
  seam::authoring::ExportService service;
  CHECK(service.commitRendered(
      rendered, 1U, destination, seam::voicebank::WavSampleFormat::Pcm16));
  const auto unownedBackup = std::filesystem::path{destination.string() + ".previous"};
  std::ofstream{unownedBackup} << "unrelated-canary";

  const auto replacement = service.commitRendered(
      rendered, 2U, destination, seam::voicebank::WavSampleFormat::Pcm16);

  CHECK(!replacement);
  std::ifstream input{unownedBackup, std::ios::binary};
  const std::string backupText{std::istreambuf_iterator<char>{input},
                               std::istreambuf_iterator<char>{}};
  CHECK(backupText == "unrelated-canary");
  CHECK(std::filesystem::exists(destination));
}

TEST_CASE("single-file export rolls back the master when receipt rotation fails") {
  const auto root = seam::test::support::temporaryDirectory(
      "export-receipt-rollback");
  const auto destination = root / "master.wav";
  seam::rendering::ProjectRenderResult rendered{
      .sampleRate = 48000U,
      .channelCount = 1U,
      .interleaved = {0.1F, 0.2F, 0.3F, 0.4F},
  };
  seam::authoring::ExportService service;
  CHECK(service.commitRendered(
      rendered, 1U, destination, seam::voicebank::WavSampleFormat::Pcm16));

  const auto receipt = destination.string() + ".receipt.json";
  const auto readBytes = [](const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string{std::istreambuf_iterator<char>{input},
                       std::istreambuf_iterator<char>{}};
  };
  const auto previousMaster = readBytes(destination);
  const auto previousReceipt = readBytes(receipt);
  const auto receiptBackup = receipt + ".previous";
  CHECK(std::filesystem::create_directories(receiptBackup));
  std::ofstream{std::filesystem::path{receiptBackup} / "keep.txt"}
      << "keep collision";

  const auto replacement = service.commitRendered(
      rendered, 2U, destination, seam::voicebank::WavSampleFormat::Pcm24);
  CHECK(!replacement);
  CHECK(readBytes(destination) == previousMaster);
  CHECK(readBytes(receipt) == previousReceipt);
  CHECK(!std::filesystem::exists(destination.string() + ".previous"));
  CHECK(std::filesystem::exists(std::filesystem::path{receiptBackup} / "keep.txt"));
}

TEST_CASE("export service cancellation leaves no claimed output") {
  const auto root = seam::test::support::temporaryDirectory("export-cancel");
  const auto destination = root / "master.wav";
  seam::rendering::ProjectRenderResult rendered{
      .sampleRate = 48000U,
      .channelCount = 1U,
      .interleaved = {0.0F, 0.1F, 0.2F},
  };
  std::stop_source source;
  source.request_stop();
  seam::authoring::ExportService service;
  const auto result = service.commitRendered(
      rendered, 1U, destination, seam::voicebank::WavSampleFormat::Pcm16,
      source.get_token());
  CHECK(result);
  CHECK(result.value().state == seam::authoring::ExportState::Cancelled);
  CHECK(!std::filesystem::exists(destination));
}

TEST_CASE("export service publishes master and stems while preserving canaries") {
  const auto root = seam::test::support::temporaryDirectory("export-set");
  const auto media = root / "backing.wav";
  CHECK(seam::voicebank::writeMonoPcm16Wav(
      media, 48000U, seam::test::support::sineWave(48000U, 220.0, 0.05)));
  auto source = seam::rendering::StreamingPcmSource::open(media, 4096U);
  CHECK(source);

  seam::application::ProjectFactory factory{20000U};
  auto project = factory.createProject("Export set");
  project.audioTracks().push_back(seam::domain::AudioTrack{
      .id = seam::domain::TrackId{20001U},
      .name = "Backing",
      .mediaPath = media.string(),
      .mediaHash = source.value()->info().contentHash,
      .mediaOwnership = seam::domain::MediaOwnership::ExternalReference,
      .originalFilename = media.filename().string(),
      .sourceSampleRate = source.value()->info().sampleRate,
      .sourceChannels = source.value()->info().channels,
      .sourceFrameCount = source.value()->info().frameCount,
      .startTick = seam::time::Tick{0},
      .outputRoute = seam::domain::TrackOutputRoute{
          .bus = seam::domain::BusId{1U},
          .matrix = seam::domain::RoutingMatrix::monoToStereo(),
      },
  });
  CHECK(project.validate());

  const auto destination = root / "exports" / "song";
  seam::authoring::ExportService service;
  const auto first = service.exportSet(
      project, {}, {}, {}, 7U, destination,
      seam::authoring::ExportSettings{
          .sampleRate = 48000U,
          .channels = 2U,
          .format = seam::voicebank::WavSampleFormat::Pcm16,
          .includeMaster = true,
          .includeStems = true,
          .replaceExisting = false,
      });
  CHECK(first);
  CHECK(first.value().state == seam::authoring::ExportState::Committed);
  CHECK(first.value().files.size() == 2U);
  CHECK(std::filesystem::exists(destination / "master.wav"));
  CHECK(std::filesystem::exists(destination / "receipt.json"));

  const auto canary = destination / "keep.txt";
  std::ofstream{canary} << "keep";
  const auto second = service.exportSet(
      project, {}, {}, {}, 8U, destination,
      seam::authoring::ExportSettings{
          .sampleRate = 48000U,
          .channels = 2U,
          .format = seam::voicebank::WavSampleFormat::Pcm16,
          .includeMaster = true,
          .includeStems = true,
          .replaceExisting = true,
      });
  CHECK(second);
  CHECK(second.value().state == seam::authoring::ExportState::Committed);
  CHECK(std::filesystem::exists(canary));
  CHECK(second.value().projectRevision == 8U);
}

TEST_CASE("export set failure restoring preserved files rolls back the new set") {
  const auto root = seam::test::support::temporaryDirectory(
      "export-preserved-file-rollback");
  const auto media = root / "backing.wav";
  CHECK(seam::voicebank::writeMonoPcm16Wav(
      media, 48000U, seam::test::support::sineWave(48000U, 220.0, 0.05)));
  auto source = seam::rendering::StreamingPcmSource::open(media, 4096U);
  CHECK(source);

  seam::application::ProjectFactory factory{20000U};
  auto project = factory.createProject("Export rollback");
  project.audioTracks().push_back(seam::domain::AudioTrack{
      .id = factory.nextTrackId(),
      .name = "Backing",
      .mediaPath = media.string(),
      .mediaHash = source.value()->info().contentHash,
      .mediaOwnership = seam::domain::MediaOwnership::ExternalReference,
      .originalFilename = media.filename().string(),
      .sourceSampleRate = source.value()->info().sampleRate,
      .sourceChannels = source.value()->info().channels,
      .sourceFrameCount = source.value()->info().frameCount,
      .startTick = seam::time::Tick{0},
      .outputRoute = seam::domain::TrackOutputRoute{
          .bus = seam::domain::BusId{1U},
          .matrix = seam::domain::RoutingMatrix::monoToStereo(),
      },
  });
  CHECK(project.validate());

  const auto destination = root / "exports" / "song";
  seam::authoring::ExportService service;
  CHECK(service.exportSet(
      project, {}, {}, {}, 7U, destination,
      seam::authoring::ExportSettings{
          .sampleRate = 48000U,
          .channels = 2U,
          .format = seam::voicebank::WavSampleFormat::Pcm16,
          .includeMaster = true,
          .includeStems = true,
          .replaceExisting = false,
      }));

  std::ofstream{destination / "keep.txt"} << "keep this too";
  CHECK(std::filesystem::remove(destination / "receipt.json"));
  CHECK(std::filesystem::create_directories(destination / "receipt.json"));
  std::ofstream{destination / "receipt.json" / "keep.txt"} << "keep";

  const auto replacement = service.exportSet(
      project, {}, {}, {}, 8U, destination,
      seam::authoring::ExportSettings{
          .sampleRate = 48000U,
          .channels = 2U,
          .format = seam::voicebank::WavSampleFormat::Pcm16,
          .includeMaster = true,
          .includeStems = true,
          .replaceExisting = true,
      });
  CHECK(!replacement);
  CHECK(std::filesystem::exists(destination / "master.wav"));
  CHECK(std::filesystem::exists(destination / "keep.txt"));
  CHECK(std::filesystem::exists(destination / "receipt.json" / "keep.txt"));
}

TEST_CASE("export replacement removes files owned only by the previous receipt") {
  const auto root = seam::test::support::temporaryDirectory(
      "export-removed-stem");
  const auto project = exportProject(root, 24000U);
  const auto destination = root / "exports" / "song";
  seam::authoring::ExportService service;
  auto firstSettings = exportSettings(true);
  firstSettings.replaceExisting = false;
  const auto first = service.exportSet(
      project, {}, {}, {}, 1U, destination, firstSettings);
  CHECK(first);
  CHECK(first.value().files.size() == 2U);
  const auto stem = first.value().files.back().path;
  CHECK(std::filesystem::exists(stem));
  std::ofstream{destination / "keep.txt"} << "unowned-canary";

  const auto second = service.exportSet(
      project, {}, {}, {}, 2U, destination, exportSettings(false));
  CHECK(second);
  CHECK(!std::filesystem::exists(stem));
  CHECK(std::filesystem::exists(destination / "keep.txt"));
}

TEST_CASE("export replacement never removes an unowned previous sibling") {
  const auto root = seam::test::support::temporaryDirectory(
      "export-unowned-previous");
  const auto project = exportProject(root, 25000U);
  const auto destination = root / "exports" / "song";
  seam::authoring::ExportService service;
  auto firstSettings = exportSettings(true);
  firstSettings.replaceExisting = false;
  CHECK(service.exportSet(project, {}, {}, {}, 1U, destination, firstSettings));
  const auto unowned = std::filesystem::path{destination.string() + ".previous"};
  CHECK(std::filesystem::create_directories(unowned));
  std::ofstream{unowned / "keep.txt"} << "not export-owned";

  CHECK(service.exportSet(
      project, {}, {}, {}, 2U, destination, exportSettings(true)));
  CHECK(std::filesystem::exists(unowned / "keep.txt"));
}

TEST_CASE("export recovery reconciles every journalled publication phase") {
  const auto root = seam::test::support::temporaryDirectory(
      "export-journal-recovery");
  const auto project = exportProject(root, 26000U);
  seam::authoring::ExportService service;
  const auto phases = std::array{
      seam::authoring::ExportPublicationPhase::JournalPrepared,
      seam::authoring::ExportPublicationPhase::PreviousMoved,
      seam::authoring::ExportPublicationPhase::DestinationPublished,
      seam::authoring::ExportPublicationPhase::ReceiptCommitted,
      seam::authoring::ExportPublicationPhase::BackupRemoved,
  };
  for (std::size_t index = 0U; index < phases.size(); ++index) {
    const auto destination = root / "exports" / ("song-" + std::to_string(index));
    auto firstSettings = exportSettings(true);
    firstSettings.replaceExisting = false;
    CHECK(service.exportSet(
        project, {}, {}, {}, 1U, destination, firstSettings));
    std::ofstream{destination / "keep.txt"} << "unowned-canary";
    auto interruptedSettings = exportSettings(false);
    interruptedSettings.publicationFaultInjector =
        [phase = phases[index]](seam::authoring::ExportPublicationPhase current) {
          return current == phase;
        };
    const auto interrupted = service.exportSet(
        project, {}, {}, {}, 2U, destination, interruptedSettings);
    CHECK(interrupted);
    CHECK(interrupted.value().state ==
          seam::authoring::ExportState::RollbackRequired);

    const auto recovered = service.recoverSet(destination);
    CHECK(recovered);
    CHECK(std::filesystem::exists(destination / "master.wav"));
    CHECK(std::filesystem::exists(destination / "receipt.json"));
    CHECK(std::filesystem::exists(destination / "keep.txt"));
    const auto expectedRevision = index < 3U ? 1 : 2;
    CHECK(committedRevision(destination) == expectedRevision);
    CHECK(std::filesystem::exists(destination / "stems") ==
          (expectedRevision == 1));
    for (const auto& entry : std::filesystem::directory_iterator{
             destination.parent_path()}) {
      const auto name = entry.path().filename().string();
      CHECK(name.find(destination.filename().string() + "-export-") ==
            std::string::npos);
    }
  }
}

TEST_CASE("export rejects symlinked destination files") {
  const auto root = seam::test::support::temporaryDirectory(
      "export-symlink-preserved-file");
  const auto destination = root / "exports" / "master.wav";
  seam::rendering::ProjectRenderResult rendered{
      .sampleRate = 48000U,
      .channelCount = 1U,
      .interleaved = {0.1F, 0.2F, 0.3F},
  };
  seam::authoring::ExportService service;
  const auto outside = root / "outside.wav";
  CHECK(seam::voicebank::writeMonoPcm16Wav(
      outside, 48000U, std::vector<float>{0.2F, 0.3F}));
  std::error_code error;
  std::filesystem::create_symlink(outside, destination, error);
  if (error) return;

  const auto replacement = service.commitRendered(
      rendered, 2U, destination, seam::voicebank::WavSampleFormat::Pcm24);
  CHECK(!replacement);
  CHECK(replacement.error().code == seam::core::ErrorCode::Conflict);
  CHECK(std::filesystem::is_symlink(destination));
  CHECK(std::filesystem::exists(outside));
}

TEST_CASE("export recovery rejects symlinked master files") {
  const auto root = seam::test::support::temporaryDirectory(
      "export-recovery-symlink");
  const auto destination = root / "exports" / "song";
  seam::authoring::ExportService service;
  CHECK(std::filesystem::create_directories(destination));
  const auto outside = root / "outside.wav";
  CHECK(seam::voicebank::writeMonoPcm16Wav(
      outside, 48000U, std::vector<float>{0.2F, 0.3F}));
  const auto master = destination / "master.wav";
  CHECK(seam::voicebank::writeMonoPcm16Wav(
      master, 48000U, std::vector<float>{0.1F, 0.2F}));
  std::error_code error;
  std::filesystem::remove(master, error);
  CHECK(!error);
  std::filesystem::create_symlink(outside, master, error);
  if (error) return;

  const auto recovered = service.recoverSet(destination);
  CHECK(!recovered);
  CHECK(recovered.error().code == seam::core::ErrorCode::Conflict);
  CHECK(std::filesystem::is_symlink(master));
}

TEST_CASE("export receipt records canonical project and render identities") {
  seam::application::ProjectFactory factory{21000U};
  auto project = factory.createProject("Receipt identity");
  const auto trackId = factory.addVocalTrack(project, "Lead");
  auto root = seam::test::support::temporaryDirectory("export-receipt-identity");
  project.findVocalTrack(trackId)->muted = true;
  const auto media = root / "backing.wav";
  CHECK(seam::voicebank::writeMonoPcm16Wav(
      media, 48000U, seam::test::support::sineWave(48000U, 220.0, 0.02)));
  auto source = seam::rendering::StreamingPcmSource::open(media, 4096U);
  CHECK(source);
  project.audioTracks().push_back(seam::domain::AudioTrack{
      .id = factory.nextTrackId(),
      .name = "Backing",
      .mediaPath = media.string(),
      .mediaHash = source.value()->info().contentHash,
      .mediaOwnership = seam::domain::MediaOwnership::ExternalReference,
      .originalFilename = media.filename().string(),
      .sourceSampleRate = source.value()->info().sampleRate,
      .sourceChannels = source.value()->info().channels,
      .sourceFrameCount = source.value()->info().frameCount,
      .startTick = seam::time::Tick{0},
  });
  CHECK(project.validate());
  const auto destination = root / "set";
  seam::authoring::ExportService service;
  const auto result = service.exportSet(
      project, {}, {}, {}, 9U, destination,
      seam::authoring::ExportSettings{
          .sampleRate = 48000U,
          .channels = 2U,
          .format = seam::voicebank::WavSampleFormat::Float32,
          .includeMaster = true,
          .includeStems = false,
          .replaceExisting = false,
      });
  CHECK(result);
  CHECK(result.value().state == seam::authoring::ExportState::Committed);
  std::ifstream receipt(destination / "receipt.json");
  const std::string text{std::istreambuf_iterator<char>{receipt},
                          std::istreambuf_iterator<char>{}};
  CHECK(text.find("\"projectId\": \"" + project.id().toString()) !=
        std::string::npos);
  CHECK(text.find("\"projectSchema\": " + std::to_string(seam::formats::ProjectJsonCodec::kSchemaVersion)) != std::string::npos);
  CHECK(text.find("\"renderQuality\": \"Final\"") != std::string::npos);
  CHECK(text.find("\"renderAbi\": \"") != std::string::npos);
  CHECK(text.find("\"applicationBuildSha\": \"") != std::string::npos);
  CHECK(text.find("\"executionDateUnixMs\": ") != std::string::npos);
}

TEST_CASE("export writer covers the production format matrix") {
  const auto root = seam::test::support::temporaryDirectory("export-matrix");
  seam::authoring::ExportService service;
  for (const auto sampleRate : {44100U, 48000U, 96000U}) {
    for (const auto channels : {std::uint8_t{1U}, std::uint8_t{2U},
                                std::uint8_t{4U}, std::uint8_t{8U}}) {
      for (const auto format : {seam::voicebank::WavSampleFormat::Pcm16,
                                seam::voicebank::WavSampleFormat::Pcm24,
                                seam::voicebank::WavSampleFormat::Float32}) {
        seam::rendering::ProjectRenderResult rendered{
            .sampleRate = sampleRate,
            .channelCount = channels,
            .interleaved = std::vector<float>(
                static_cast<std::size_t>(channels) * 8U, 0.125F),
        };
        const auto destination = root /
            (std::to_string(sampleRate) + "-" +
             std::to_string(static_cast<unsigned>(channels)) + "-" +
             std::to_string(static_cast<int>(format)) + ".wav");
        const auto result = service.commitRendered(
            rendered, static_cast<std::uint64_t>(sampleRate) + channels,
            destination, format);
        CHECK(result);
        CHECK(result.value().state == seam::authoring::ExportState::Committed);
        const auto decoded = seam::voicebank::readWav(destination);
        CHECK(decoded);
        CHECK(decoded.value().sampleRate == sampleRate);
        CHECK(decoded.value().channels == channels);
        CHECK(decoded.value().frameCount() == 8U);
      }
    }
  }
}
