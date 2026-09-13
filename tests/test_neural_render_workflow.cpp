// Coordinator-level neural workflow coverage: two simultaneous neural tracks,
// cancellation without stale publication, and cache provenance.
//
// The executing runner is the transport fixture probe, which returns silence and
// performs no inference. Everything here proves routing, publication identity and
// cache behaviour, never musical output or model quality.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/authoring/neural_phrase_runner.hpp"
#include "seam/authoring/render_coordinator.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/neural_synthesis/bundle_metadata.hpp"
#include "seam/phonemizer/language_resolver.hpp"
#include "seam/rendering/project_renderer.hpp"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <vector>

#ifndef SEAM_NEURAL_BUNDLE_TRANSPORT_PROBE
#error SEAM_NEURAL_BUNDLE_TRANSPORT_PROBE is required for neural workflow tests
#endif

namespace {

using seam::authoring::AuthoringNeuralPhraseRunner;
using seam::neural_synthesis::AdmittedNeuralBundle;
using seam::rendering::NeuralRenderProvenance;
using seam::rendering::TrackNeuralSource;
using seam::rendering::TrackSingerSource;

std::string configuration() {
  const auto features=R"({"sampleRate":48000,"hopSize":256,"bins":80,"layout":"BTF",)"
      R"("amplitudeScale":"ln-amplitude","multiplier":1.0,"offset":0.0,"minimumHz":40.0,)"
      R"("maximumHz":16000.0,"fftSize":2048,"windowSize":1024,"melFrequencyScale":"slaney"})";
  return std::string{R"({"formatId":"com.project-seam.neural-bundle-configuration","schemaVersion":3,)"}+
      R"("maximumFrames":48000,"acousticFeatures":)"+features+R"(,"vocoderFeatures":)"+features+
      R"(,"stepsLayout":"scalar","vocoderOutput":"audio"})";
}

std::string vocabularyJson(const std::set<std::string>& symbols) {
  std::string tokens=R"("<PAD>","SP")";
  for (const auto& symbol:symbols) tokens+=",\""+symbol+"\"";
  return std::string{R"({"formatId":"com.project-seam.neural-vocabulary","schemaVersion":1,"tokens":[)"}+
      tokens+"]}";
}

seam::core::Result<AdmittedNeuralBundle> writeBundle(const std::filesystem::path& directory,
    std::string_view vocabulary,std::string_view graph,std::string_view modelId,
    std::string_view modelVersion) {
  using namespace seam::synthesis;
  const std::string declaration=configuration();
  const auto asset=[&](NeuralAssetRole role,const char* name,std::string_view value) {
    return NeuralBundleAssetInput{role,name,std::as_bytes(std::span{value.data(),value.size()}),seam::core::sha256Hex(value)};
  };
  const std::array assets{asset(NeuralAssetRole::Acoustic,"acoustic",graph),
      asset(NeuralAssetRole::Vocoder,"vocoder",graph),asset(NeuralAssetRole::Vocabulary,"vocabulary",vocabulary),
      asset(NeuralAssetRole::Configuration,"configuration",declaration)};
  const auto manifest=FrozenNeuralBundle::manifest(assets,1024U*1024U);
  if (!manifest) return seam::core::Result<AdmittedNeuralBundle>{manifest.error()};
  auto frozen=FrozenNeuralBundle::freeze({seam::domain::SingerResourceKind::Neural,
      std::string{modelId},std::string{modelVersion},seam::core::sha256Hex(manifest.value())},assets,1024U*1024U);
  if (!frozen) return seam::core::Result<AdmittedNeuralBundle>{frozen.error()};
  if (!seam::core::durableAtomicWriteNew(directory/"manifest.json",frozen.value().manifestData().bytes()))
    return seam::core::failure<AdmittedNeuralBundle>(seam::core::ErrorCode::IoError,"manifest write failed");
  for (const auto& entry:frozen.value().assets())
    if (!seam::core::durableAtomicWriteNew(directory/entry.name,entry.data->bytes()))
      return seam::core::failure<AdmittedNeuralBundle>(seam::core::ErrorCode::IoError,"asset write failed");
  return AdmittedNeuralBundle::admit(frozen.value(),65536U,10);
}

struct Track final {
  seam::domain::TrackId track{};
  seam::domain::RegionId region{};
  std::shared_ptr<const AdmittedNeuralBundle> bundle;
  std::shared_ptr<const AuthoringNeuralPhraseRunner> runner;
};

// The runner is created from the bundle directory, exactly as the application
// does, so the test cannot hand the renderer a bundle the worker never loaded.
Track addNeuralTrack(seam::domain::Project& project,seam::application::ProjectFactory& factory,
    const std::filesystem::path& directory,std::string_view lyric,std::string_view modelId,
    std::string_view modelVersion,seam::time::Tick start) {
  std::filesystem::create_directories(directory);
  Track result{};
  result.track=factory.addVocalTrack(project,"Neural "+std::string{modelId});
  result.region=factory.addRegion(project,result.track,"Phrase",start,start+seam::time::Tick{1920});
  auto* region=project.findRegion(result.region);
  if (region==nullptr) throw seam::test::Failure{"neural workflow region is missing"};
  for (std::size_t index=0U;index<2U;++index) {
    auto [token,note]=factory.makeNote(start+seam::time::Tick{960*static_cast<std::int64_t>(index)},
        seam::time::Tick{960},69U,std::u32string{lyric.begin(),lyric.end()},seam::domain::Language::English);
    region->lyrics.push_back(std::move(token));
    region->notes.push_back(std::move(note));
  }
  region->sortNotes();
  const auto pronunciation=seam::phonemizer::resolvePronunciation(*region);
  if (!pronunciation) throw seam::test::Failure{"phonemizer fixture failed: "+pronunciation.error().message};
  std::set<std::string> symbols;
  for (const auto& token:pronunciation.value().pronunciation.tokens) symbols.insert(token.symbol);
  const auto admitted=writeBundle(directory,vocabularyJson(symbols),"workflow graph fixture "+std::string{modelId},
      modelId,modelVersion);
  if (!admitted) throw seam::test::Failure{"bundle fixture failed: "+admitted.error().message};
  result.bundle=std::make_shared<const AdmittedNeuralBundle>(std::move(admitted).value());
  seam::authoring::NeuralPhraseRunnerOptions options{};
  options.bundleDirectory=std::filesystem::canonical(directory);
  options.maximumBundleBytes=1024U*1024U;
  options.worker=seam::neural_synthesis::NeuralWorkerRunOptions{
      .helper=SEAM_NEURAL_BUNDLE_TRANSPORT_PROBE,
      .helperContentHash=seam::core::sha256File(SEAM_NEURAL_BUNDLE_TRANSPORT_PROBE).value(),
      .timeout=std::chrono::seconds{20},.maximumResidentBytes=256U*1024U*1024U,
      .maximumCpuTime=std::chrono::seconds{5},.protocolVersion=2U};
  const auto runner=AuthoringNeuralPhraseRunner::create(options);
  if (!runner) throw seam::test::Failure{"neural runner fixture failed: "+runner.error().message};
  result.runner=std::make_shared<const AuthoringNeuralPhraseRunner>(std::move(runner).value());
  return result;
}

NeuralRenderProvenance provenance(std::string worker="seam-neural-worker-1") {
  return NeuralRenderProvenance{.workerVersion=std::move(worker),
      .runtimeVersion="onnxruntime-fixture",.provider="CPUExecutionProvider"};
}

std::vector<TrackSingerSource> sources(const std::vector<Track>& tracks,
    const NeuralRenderProvenance& value) {
  std::vector<TrackSingerSource> result;
  for (const auto& track:tracks)
    result.push_back(TrackNeuralSource{track.track,track.bundle,value,track.runner});
  return result;
}

seam::authoring::RenderProgress waitForTerminal(seam::authoring::AuthoringRenderCoordinator& coordinator,
    std::uint64_t revision,std::chrono::milliseconds timeout=std::chrono::milliseconds{20000}) {
  const auto deadline=std::chrono::steady_clock::now()+timeout;
  while (std::chrono::steady_clock::now()<deadline) {
    const auto progress=coordinator.progress();
    if (progress.requestedRevision==revision &&
        (progress.state==seam::authoring::RenderState::Ready ||
         progress.state==seam::authoring::RenderState::Cancelled ||
         progress.state==seam::authoring::RenderState::Failed)) return progress;
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  return coordinator.progress();
}

}  // namespace

TEST_CASE("two simultaneous neural tracks publish distinct admitted identities") {
  const auto root=seam::test::support::temporaryDirectory("neural-workflow-multi");
  seam::application::ProjectFactory factory{9300U};
  auto project=factory.createProject("Two neural singers");
  const auto first=addNeuralTrack(project,factory,root/"voice-a","ak","seam.voice.a","1.0.0",seam::time::Tick{0});
  const auto second=addNeuralTrack(project,factory,root/"voice-b","ak","seam.voice.b","2.0.0",seam::time::Tick{0});
  const auto tracks=std::vector<Track>{first,second};
  const auto selected=sources(tracks,provenance());
  seam::authoring::AuthoringRenderCoordinator coordinator{root/"cache"};
  coordinator.submitWithSources(project,selected,first.track,first.region,1U,48000U,
      seam::rendering::RenderQuality::Preview,true);
  const auto progress=waitForTerminal(coordinator,1U);
  if (progress.state!=seam::authoring::RenderState::Ready)
    throw seam::test::Failure{"multi-track neural render did not complete: "+progress.diagnostic};
  CHECK(progress.totalPhrases==2U);
  CHECK(progress.completedPhrases==2U);
  const auto published=coordinator.latest(); CHECK(published);
  CHECK(published->result.trackCount==2U);
  CHECK(published->result.regionCount==2U);
  CHECK(published->result.phraseCount==2U);
  // Two models are in flight, so the identity must name each admitted execution
  // rather than one voicebank, and the renderer must not be reported as the
  // source-filter path that never ran.
  CHECK(published->neuralIdentities.size()==2U);
  CHECK(published->neuralIdentities[0].trackId==first.track);
  CHECK(published->neuralIdentities[1].trackId==second.track);
  CHECK(published->neuralIdentities[0].modelId=="seam.voice.a");
  CHECK(published->neuralIdentities[1].modelId=="seam.voice.b");
  CHECK(published->neuralIdentities[0].modelVersion=="1.0.0");
  CHECK(published->neuralIdentities[1].modelVersion=="2.0.0");
  CHECK(published->neuralIdentities[0].inferenceSteps==10);
  CHECK(published->neuralIdentities[0].runtimeVersion=="onnxruntime-fixture");
  CHECK(published->neuralIdentities[0].bundleContentHash.size()==64U);
  CHECK(published->neuralIdentities[0].bundleContentHash!=published->neuralIdentities[1].bundleContentHash);
  CHECK(published->activeRenderer=="seam.neural-worker.v1");
  CHECK(published->activeVoicebankId.empty());
  CHECK(published->activeVoicebankContentHash.empty());
  CHECK(published->result.activeUnitPlan.empty());
  // The two phrases keep separate content identities, so one track's audio can
  // never be served for the other from the shared cache.
  CHECK(published->result.phraseContentHashes.size()==2U);
  CHECK(published->result.phraseContentHashes[0]!=published->result.phraseContentHashes[1]);
}

TEST_CASE("a cancelled neural preview publishes nothing and a retry completes") {
  const auto root=seam::test::support::temporaryDirectory("neural-workflow-cancel");
  seam::application::ProjectFactory factory{9100U};
  auto project=factory.createProject("Cancel neural preview");
  const auto track=addNeuralTrack(project,factory,root/"voice","ak","seam.voice.cancel","1.0.0",seam::time::Tick{0});
  const auto selected=sources(std::vector<Track>{track},provenance());
  std::mutex gateMutex;
  std::condition_variable_any gateCondition;
  bool entered=false;
  bool release=false;
  bool exited=false;
  seam::authoring::RenderCoordinatorHooks hooks;
  hooks.beforePublication=[&](std::uint64_t revision,std::stop_token) {
    if (revision!=41U) return;
    std::unique_lock lock{gateMutex};
    entered=true;
    gateCondition.notify_all();
    static_cast<void>(gateCondition.wait(lock,[&] { return release; }));
    exited=true;
    gateCondition.notify_all();
  };
  seam::authoring::AuthoringRenderCoordinator coordinator{root/"cache",std::move(hooks)};
  coordinator.submitWithSources(project,selected,track.track,track.region,41U,48000U,
      seam::rendering::RenderQuality::Preview,true);
  {
    std::unique_lock lock{gateMutex};
    CHECK(gateCondition.wait_for(lock,std::chrono::seconds{5},[&] { return entered; }));
  }
  coordinator.cancel();
  {
    std::lock_guard lock{gateMutex};
    release=true;
    gateCondition.notify_all();
  }
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{20};
  while (std::chrono::steady_clock::now()<deadline) {
    bool finished=false;
    {
      std::lock_guard lock{gateMutex};
      finished=exited;
    }
    if (finished) break;
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  CHECK(coordinator.stats().completed==0U);
  CHECK(coordinator.progress().state==seam::authoring::RenderState::Cancelled);
  // A cancelled render must not publish: the retained slot stays idle and keeps
  // no neural identity.
  {
    const auto retained=coordinator.acquire();
    if (retained) {
      CHECK(retained->state==seam::authoring::RenderState::Idle);
      CHECK(retained->neuralIdentities.empty());
    }
  }
  // Retry on the same coordinator: the cancelled request must not become the
  // published identity, and no neural identity may be reported for it.
  coordinator.submitWithSources(project,selected,track.track,track.region,42U,48000U,
      seam::rendering::RenderQuality::Preview,true);
  CHECK(waitForTerminal(coordinator,42U).state==seam::authoring::RenderState::Ready);
  const auto published=coordinator.latest(); CHECK(published);
  CHECK(published->projectRevision==42U);
  CHECK(published->neuralIdentities.size()==1U);
  CHECK(published->neuralIdentities.front().modelId=="seam.voice.cancel");
  CHECK(coordinator.stats().completed==1U);
}

TEST_CASE("neural cache provenance separates executions that share a bundle") {
  const auto root=seam::test::support::temporaryDirectory("neural-workflow-cache");
  seam::application::ProjectFactory factory{9200U};
  auto project=factory.createProject("Neural cache provenance");
  const auto track=addNeuralTrack(project,factory,root/"voice","ak","seam.voice.cache","1.0.0",seam::time::Tick{0});
  const auto tracks=std::vector<Track>{track};
  seam::authoring::AuthoringRenderCoordinator coordinator{root/"cache"};
  const auto render=[&](std::uint64_t revision,const NeuralRenderProvenance& value) {
    coordinator.submitWithSources(project,sources(tracks,value),track.track,track.region,revision,48000U,
        seam::rendering::RenderQuality::Final,true);
    const auto progress=waitForTerminal(coordinator,revision);
    if (progress.state!=seam::authoring::RenderState::Ready)
      throw seam::test::Failure{"neural cache render did not complete: "+progress.diagnostic};
    const auto published=coordinator.latest(); CHECK(published);
    return *published;
  };
  const auto first=render(51U,provenance());
  CHECK(first.result.cacheHits==0U);
  const auto repeated=render(52U,provenance());
  CHECK(repeated.result.cacheHits>=1U);
  CHECK(repeated.result.phraseContentHashes==first.result.phraseContentHashes);
  CHECK(repeated.neuralIdentities.front().bundleContentHash==first.neuralIdentities.front().bundleContentHash);
  // A different provider and runtime describe different audio, so the cached
  // phrase cannot be reused even though the admitted bundle is identical.
  auto other=provenance("seam-neural-worker-2");
  other.runtimeVersion="onnxruntime-1.30.0";
  const auto changed=render(53U,other);
  CHECK(changed.result.cacheHits==0U);
  CHECK(changed.neuralIdentities.front().bundleContentHash==first.neuralIdentities.front().bundleContentHash);
  CHECK(changed.neuralIdentities.front().workerVersion=="seam-neural-worker-2");
  CHECK(changed.result.phraseContentHashes!=first.result.phraseContentHashes);
}
