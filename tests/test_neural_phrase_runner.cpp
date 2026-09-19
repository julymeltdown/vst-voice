#include "test_framework.hpp"
#include "test_support.hpp"
#include "test_onnx_fixture.hpp"
#include "seam/authoring/neural_phrase_runner.hpp"
#include "seam/authoring/neural_resource_registry.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/neural_synthesis/model_bundle.hpp"
#include "seam/phonemizer/language_resolver.hpp"
#include "seam/rendering/render_pipeline.hpp"
#include "seam/rendering/render_snapshot.hpp"
#include "seam/rendering/project_renderer.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <set>
#include <map>
#include <string>

namespace {

using seam::authoring::AuthoringNeuralPhraseRunner;
using seam::authoring::NeuralPhraseRunnerOptions;
using seam::neural_synthesis::AdmittedNeuralBundle;

std::string configuration() {
  const auto features=R"({"sampleRate":48000,"hopSize":256,"bins":80,"layout":"BTF",)"
      R"("amplitudeScale":"ln-amplitude","multiplier":1.0,"offset":0.0,"minimumHz":40.0,)"
      R"("maximumHz":16000.0,"fftSize":2048,"windowSize":1024,"melFrequencyScale":"slaney"})";
  return std::string{R"({"formatId":"com.project-seam.neural-bundle-configuration","schemaVersion":3,)"}+
      R"("maximumFrames":48000,"acousticFeatures":)"+features+R"(,"vocoderFeatures":)"+features+
      R"(,"stepsLayout":"scalar","vocoderOutput":"audio"})";
}

// The vocabulary must contain the symbols the phonemizer actually produces for
// this region, so it is derived from the resolved pronunciation rather than
// guessed. SP is the explicit silence symbol the runner declares.
std::string vocabularyJson(const std::set<std::string>& symbols,bool includeSilence) {
  std::string tokens=R"("<PAD>")";
  if (includeSilence) tokens+=R"(,"SP")";
  for (const auto& symbol:symbols) tokens+=",\""+symbol+"\"";
  return std::string{R"({"formatId":"com.project-seam.neural-vocabulary","schemaVersion":1,"tokens":[)"}+
      tokens+"]}";
}

seam::core::Result<AdmittedNeuralBundle> writeBundle(const std::filesystem::path& directory,
    std::string_view vocabulary,std::string_view producer="runner-fixture") {
  using namespace seam::synthesis;
  const std::string declaration=configuration();
  // Real graph bytes, not a placeholder: admission reads the two files and binds the acoustic mel
  // output to the vocoder mel input, so a fixture that is not a graph would be refused.
  const std::string acoustic=seam::test::onnx::onnxAcousticGraph(80U,1U,{},producer);
  const std::string vocoder=seam::test::onnx::onnxVocoderGraph(80U,1U,"audio","1",producer);
  const auto asset=[&](NeuralAssetRole role,const char* name,std::string_view value) {
    return NeuralBundleAssetInput{role,name,std::as_bytes(std::span{value.data(),value.size()}),seam::core::sha256Hex(value)};
  };
  const std::array assets{asset(NeuralAssetRole::Acoustic,"acoustic",acoustic),
      asset(NeuralAssetRole::Vocoder,"vocoder",vocoder),asset(NeuralAssetRole::Vocabulary,"vocabulary",vocabulary),
      asset(NeuralAssetRole::Configuration,"configuration",declaration)};
  const auto manifest=FrozenNeuralBundle::manifest(assets,1024U*1024U);
  if (!manifest) return seam::core::Result<AdmittedNeuralBundle>{manifest.error()};
  auto frozen=FrozenNeuralBundle::freeze({seam::domain::SingerResourceKind::Neural,"runner-bank","1",
      seam::core::sha256Hex(manifest.value())},assets,1024U*1024U);
  if (!frozen) return seam::core::Result<AdmittedNeuralBundle>{frozen.error()};
  if (!seam::core::durableAtomicWriteNew(directory/"manifest.json",frozen.value().manifestData().bytes()))
    return seam::core::failure<AdmittedNeuralBundle>(seam::core::ErrorCode::IoError,"manifest write failed");
  for (const auto& entry:frozen.value().assets())
    if (!seam::core::durableAtomicWriteNew(directory/entry.name,entry.data->bytes()))
      return seam::core::failure<AdmittedNeuralBundle>(seam::core::ErrorCode::IoError,"asset write failed");
  return AdmittedNeuralBundle::admit(frozen.value(),65536U,10);
}

struct Prepared final {
  seam::domain::Project project;
  seam::domain::TrackId track{};
  seam::domain::RegionId region{};
  std::set<std::string> symbols;
  std::shared_ptr<const AdmittedNeuralBundle> admitted;
  seam::rendering::RenderSnapshot snapshot;
};

Prepared prepareWithGraph(const std::filesystem::path& directory,const std::string& lyric,
    bool includeSilence,std::string_view producer);

Prepared prepare(const std::filesystem::path& directory,const std::string& lyric,bool includeSilence=true) {
  return prepareWithGraph(directory,lyric,includeSilence,"runner-fixture");
}
Prepared prepareWithGraph(const std::filesystem::path& directory,const std::string& lyric,
    bool includeSilence,std::string_view producer) {
  seam::application::ProjectFactory factory{9300U};
  Prepared result{};
  result.project=factory.createProject("Runner");
  result.track=factory.addVocalTrack(result.project,"Singer");
  result.region=factory.addRegion(result.project,result.track,"Phrase",seam::time::Tick{0},seam::time::Tick{1920});
  auto* region=result.project.findRegion(result.region);
  // Two identical syllables give a second note whose complete phoneme spans form
  // a genuine sub-window of the phrase.
  for (std::size_t index=0U;index<2U;++index) {
    auto [lyricToken,note]=factory.makeNote(seam::time::Tick{960*static_cast<std::int64_t>(index)},
        seam::time::Tick{960},69U,std::u32string{lyric.begin(),lyric.end()},seam::domain::Language::English);
    region->lyrics.push_back(std::move(lyricToken));
    region->notes.push_back(std::move(note));
  }
  region->sortNotes();
  const auto pronunciation=seam::phonemizer::resolvePronunciation(*region);
  if (!pronunciation) throw seam::test::Failure{"phonemizer fixture failed: "+pronunciation.error().message};
  for (const auto& token:pronunciation.value().pronunciation.tokens) result.symbols.insert(token.symbol);
  const auto admitted=writeBundle(directory,vocabularyJson(result.symbols,includeSilence),producer);
  if (!admitted) throw seam::test::Failure{"bundle fixture failed: "+admitted.error().message};
  result.admitted=std::make_shared<const AdmittedNeuralBundle>(std::move(admitted).value());
  const seam::rendering::NeuralRenderProvenance provenance{.workerVersion="seam-neural-worker-1",
      .runtimeVersion="onnxruntime-1.30.0",.provider="CPUExecutionProvider"};
  const auto snapshot=seam::rendering::RenderSnapshotFactory{}.createNeural(result.project,*result.admitted,
      provenance,result.track,result.region,1U,seam::rendering::RenderQuality::Final,48000U,"original");
  if (!snapshot) throw seam::test::Failure{"snapshot fixture failed: "+snapshot.error().message};
  result.snapshot=snapshot.value();
  return result;
}

NeuralPhraseRunnerOptions options(const std::filesystem::path& directory) {
  NeuralPhraseRunnerOptions result{};
  result.bundleDirectory=std::filesystem::canonical(directory);
  result.maximumBundleBytes=1024U*1024U;
  result.worker=seam::neural_synthesis::NeuralWorkerRunOptions{
      .helper=SEAM_NEURAL_BUNDLE_TRANSPORT_PROBE,
      .helperContentHash=seam::core::sha256File(SEAM_NEURAL_BUNDLE_TRANSPORT_PROBE).value(),
      .timeout=std::chrono::seconds{20},.maximumResidentBytes=256U*1024U*1024U,
      .maximumCpuTime=std::chrono::seconds{5},.protocolVersion=2U};
  return result;
}

seam::rendering::RenderSnapshot boundedSnapshot(const Prepared& prepared,
    seam::synthesis::PhraseFrameRange window) {
  const seam::rendering::NeuralRenderProvenance provenance{.workerVersion="seam-neural-worker-1",
      .runtimeVersion="onnxruntime-1.30.0",.provider="CPUExecutionProvider"};
  const auto snapshot=seam::rendering::RenderSnapshotFactory{}.createNeural(prepared.project,
      *prepared.admitted,provenance,prepared.track,prepared.region,1U,
      seam::rendering::RenderQuality::Final,48000U,"original",window);
  if (!snapshot) throw seam::test::Failure{"bounded snapshot fixture failed: "+snapshot.error().message};
  return snapshot.value();
}

}  // namespace

namespace {

// Writes one bundle directory that also carries the resource record an installed
// bundle needs so a saved identity can be resolved back to these bytes.
seam::core::Result<std::filesystem::path> writeInstalledBundle(const std::filesystem::path& root,
    std::string_view name,std::string_view id,std::string_view version,std::string_view producer) {
  const auto directory=root/std::string{name};
  std::error_code error;
  std::filesystem::create_directories(directory,error);
  if (error) return seam::core::failure<std::filesystem::path>(seam::core::ErrorCode::IoError,
      "install directory creation failed");
  const auto admitted=writeBundle(directory,
      R"({"formatId":"com.project-seam.neural-vocabulary","schemaVersion":1,"tokens":["<PAD>","SP","aa1","k"]})",producer);
  if (!admitted) return seam::core::Result<std::filesystem::path>{admitted.error()};
  const auto manifest=seam::core::readFileBytesLimited(directory/"manifest.json",32768U);
  if (!manifest) return seam::core::Result<std::filesystem::path>{manifest.error()};
  const auto record=std::string{R"({"contentHash":")"}+
      seam::core::sha256Hex(manifest.value())+
      R"(","formatId":"com.project-seam.neural-resource","id":")"+std::string{id}+
      R"(","schemaVersion":1,"version":")"+std::string{version}+"\"}";
  if (!seam::core::durableAtomicWriteNew(directory/"resource.json",
      std::as_bytes(std::span{record.data(),record.size()})))
    return seam::core::failure<std::filesystem::path>(seam::core::ErrorCode::IoError,"record write failed");
  return seam::core::success(directory);
}

seam::domain::NeuralResourceReference selection(std::string id,std::string version,
    const std::filesystem::path& directory) {
  const auto manifest=seam::core::readFileBytesLimited(directory/"manifest.json",32768U);
  return seam::domain::NeuralResourceReference{{seam::domain::SingerResourceKind::Neural,
      std::move(id),std::move(version),seam::core::sha256Hex(manifest.value())}};
}

}  // namespace

TEST_CASE("project rendering routes a neural track through the selected runner") {
  namespace rendering=seam::rendering;
  const auto directory=seam::test::support::temporaryDirectory("neural-coordinator");
  auto prepared=prepare(directory,"ak");
  const auto runner=AuthoringNeuralPhraseRunner::create(options(directory)); CHECK(runner);
  const auto selected=std::make_shared<const AuthoringNeuralPhraseRunner>(std::move(runner).value());
  const auto* performance=prepared.snapshot.compiledPerformance.get(); CHECK(performance!=nullptr);
  const auto extent=std::size_t(performance->notes().back().endFrame-performance->notes().front().startFrame);
  const rendering::NeuralRenderProvenance provenance{.workerVersion="seam-neural-worker-1",
      .runtimeVersion="onnxruntime-1.30.0",.provider="CPUExecutionProvider"};
  const auto render=[&](std::shared_ptr<const AuthoringNeuralPhraseRunner> phraseRunner) {
    const std::vector<rendering::TrackSingerSource> sources{rendering::TrackNeuralSource{
        prepared.track,prepared.admitted,provenance,std::move(phraseRunner)}};
    return rendering::ProductionProjectRenderer{}.renderWithSources(prepared.project,sources,
        prepared.track,prepared.region,1U,48000U);
  };
  const auto rendered=render(selected);
  if (!rendered) throw seam::test::Failure{"neural project render failed: "+rendered.error().message+" | "+rendered.error().context};
  CHECK(rendered.value().trackCount==1U);
  CHECK(rendered.value().regionCount==1U);
  CHECK(rendered.value().phraseCount==1U);
  CHECK(rendered.value().channelCount==2U);
  // The model owns no sample units, so no unit plan or diagnostics are invented.
  CHECK(rendered.value().activeUnitPlan.empty());
  CHECK(rendered.value().diagnostics.empty());
  CHECK(rendered.value().phraseContentHashes.size()==1U);
  CHECK(rendered.value().phraseContentHashes.front().size()==64U);
  // Mono model audio is routed to the stereo device output with silence on the
  // right channel, at the phrase's absolute start frame.
  const auto& snapshot=prepared.snapshot;
  const auto start=static_cast<std::size_t>(snapshot.compiledPerformance->notes().front().startFrame);
  CHECK(rendered.value().interleaved.size()==2U*(start+extent));
  CHECK_NEAR(rendered.value().interleaved[start*2U],0.0F,0.000001F);
  CHECK_NEAR(rendered.value().interleaved[start*2U+1U],0.0F,0.000001F);
  // A saved selection must match the resolved bundle exactly, and a missing
  // runner is refused rather than silently rendering something else.
  auto* track=prepared.project.findVocalTrack(prepared.track);
  track->neuralResource=seam::domain::NeuralResourceReference{
      {seam::domain::SingerResourceKind::Neural,prepared.admitted->execution().modelId,
       prepared.admitted->execution().modelVersion,prepared.admitted->execution().bundleContentHash}};
  CHECK(render(selected));
  track->neuralResource->resource.contentHash=std::string(64U,'c');
  CHECK(render(selected).error().code==seam::core::ErrorCode::Conflict);
  track->neuralResource.reset();
  CHECK(render(nullptr).error().code==seam::core::ErrorCode::InvalidArgument);
}

TEST_CASE("installed neural resources resolve saved identities or refuse them") {
  using namespace seam::authoring;
  const auto root=seam::test::support::temporaryDirectory("neural-install-root");
  const auto first=writeInstalledBundle(root,"bank-a","seam.voice.a","1.0.0","fixture-a"); CHECK(first);
  const auto second=writeInstalledBundle(root,"bank-b","seam.voice.b","2.0.0","fixture-b"); CHECK(second);
  const auto registry=NeuralResourceRegistry::scan(root,16U,1024U*1024U,4U*1024U*1024U);
  if (!registry) throw seam::test::Failure{"registry scan failed: "+registry.error().message};
  CHECK(registry.value().resources().size()==2U);
  const auto saved=selection("seam.voice.b","2.0.0",second.value());
  const auto resolved=registry.value().resolve(saved); CHECK(resolved);
  CHECK(resolved.value()==std::filesystem::canonical(second.value()));
  // A saved identity is exact: version, digest and kind all participate.
  CHECK(!registry.value().resolve(selection("seam.voice.b","1.0.0",second.value())));
  CHECK(!registry.value().resolve(selection("seam.voice.b","2.0.0",first.value())));
  CHECK(!registry.value().resolve(seam::domain::NeuralResourceReference{
      {seam::domain::SingerResourceKind::Neural,"seam.voice.c","1.0.0",
       selection("seam.voice.b","2.0.0",second.value()).resource.contentHash}}));
  CHECK(!registry.value().resolve(seam::domain::NeuralResourceReference{
      {seam::domain::SingerResourceKind::Procedural,"seam.voice.b","2.0.0",
       selection("seam.voice.b","2.0.0",second.value()).resource.contentHash}}));
  // A resource that claims a digest its manifest does not have is refused, as is
  // one whose assets were changed after the manifest was published.
  const auto tampered=seam::test::support::temporaryDirectory("neural-install-tampered");
  const auto damaged=writeInstalledBundle(tampered,"bank-c","seam.voice.c","1.0.0","fixture-c"); CHECK(damaged);
  constexpr std::string_view differentBytes{"different graph bytes"};
  std::error_code removeError;
  std::filesystem::remove(tampered/"bank-c"/"acoustic",removeError); CHECK(!removeError);
  CHECK(seam::core::durableAtomicWriteNew(tampered/"bank-c"/"acoustic",
      std::as_bytes(std::span{differentBytes.data(),differentBytes.size()})));
  CHECK(!NeuralResourceRegistry::scan(tampered,16U,1024U*1024U,4U*1024U*1024U));
  const auto missingRecord=seam::test::support::temporaryDirectory("neural-install-norecord");
  const auto recordless=missingRecord/"bank-g";
  std::filesystem::create_directories(recordless);
  const auto admitted=writeBundle(recordless,
      R"({"formatId":"com.project-seam.neural-vocabulary","schemaVersion":1,"tokens":["<PAD>","SP","aa1","k"]})",
      "fixture-d"); CHECK(admitted);
  CHECK(!NeuralResourceRegistry::scan(missingRecord,16U,1024U*1024U,4U*1024U*1024U));
  // An installation root with no bundles is a valid empty catalog, and every
  // selection into it is refused rather than substituted.
  const auto emptyRoot=seam::test::support::temporaryDirectory("neural-install-empty");
  const auto emptyRegistry=NeuralResourceRegistry::scan(emptyRoot,16U,1024U*1024U,4U*1024U*1024U);
  CHECK(emptyRegistry);
  CHECK(emptyRegistry.value().resources().empty());
  CHECK(!emptyRegistry.value().resolve(saved));
  // Duplicate, unbounded and cancelled scans are refused instead of guessed.
  const auto duplicate=seam::test::support::temporaryDirectory("neural-install-duplicate");
  const auto original=writeInstalledBundle(duplicate,"bank-e","seam.voice.e","1.0.0","fixture-e"); CHECK(original);
  std::error_code error;
  std::filesystem::copy(original.value(),duplicate/"bank-f",
                        std::filesystem::copy_options::recursive,error); CHECK(!error);
  CHECK(!NeuralResourceRegistry::scan(duplicate,16U,1024U*1024U,4U*1024U*1024U));
  CHECK(!NeuralResourceRegistry::scan(root,0U,1024U*1024U,4U*1024U*1024U));
  CHECK(!NeuralResourceRegistry::scan(root,16U,8U,4U*1024U*1024U));
  CHECK(!NeuralResourceRegistry::scan(std::filesystem::path{"relative"},16U,1024U*1024U,4U*1024U*1024U));
  std::stop_source cancellation;
  cancellation.request_stop();
  CHECK(!NeuralResourceRegistry::scan(root,16U,1024U*1024U,4U*1024U*1024U,cancellation.get_token()));
}

TEST_CASE("neural phrase runner prepares a bound request and returns window-exact audio") {
  const auto directory=seam::test::support::temporaryDirectory("neural-runner");
  const auto prepared=prepare(directory,"ak");
  const auto runner=AuthoringNeuralPhraseRunner::create(options(directory)); CHECK(runner);
  CHECK(runner.value().bundleContentHash()==prepared.snapshot.neuralExecution->execution().bundleContentHash);
  const auto selected=std::make_shared<const AuthoringNeuralPhraseRunner>(std::move(runner).value());
  const auto* performance=prepared.snapshot.compiledPerformance.get(); CHECK(performance!=nullptr);
  CHECK(performance->notes().size()==2U);
  const seam::synthesis::PhraseFrameRange extent{performance->notes().front().startFrame,
      performance->notes().back().endFrame};
  const auto rendered=seam::rendering::PhraseRenderPipeline{selected}.render(prepared.snapshot);
  if (!rendered) throw seam::test::Failure{"neural runner render failed: "+rendered.error().message+" | "+rendered.error().context};
  CHECK(rendered.value().rendered.audio.startFrame==extent.start);
  CHECK(rendered.value().rendered.audio.samples.size()==static_cast<std::size_t>(extent.end-extent.start));
  CHECK(rendered.value().resourceKind==seam::domain::SingerResourceKind::Neural);
  CHECK(rendered.value().rendered.placements.empty());
  CHECK(rendered.value().unitPlan.entries.empty());
  CHECK(rendered.value().phonemes.tokens.size()==prepared.snapshot.phonemes->tokens.size());
  // The selected helper is a transport fixture: it returns silence by contract.
  // This proves admission, request binding and response validation, not synthesis.
  CHECK(rendered.value().rendered.audio.samples.front()==0.0F);
  // A second request on the same runner gets its own request identity.
  const auto again=seam::rendering::PhraseRenderPipeline{selected}.render(prepared.snapshot); CHECK(again);
  // Output ownership keeps complete conditioning and crops only returned PCM.
  const seam::synthesis::PhraseFrameRange second{performance->notes().back().startFrame,
      performance->notes().back().endFrame};
  const auto windowed=boundedSnapshot(prepared,second); CHECK(windowed.ownedFrames==second);
  const auto secondAudio=seam::rendering::PhraseRenderPipeline{selected}.render(windowed); CHECK(secondAudio);
  CHECK(secondAudio.value().rendered.audio.startFrame==second.start);
  CHECK(secondAudio.value().rendered.audio.samples.size()==static_cast<std::size_t>(second.end-second.start));
  const seam::synthesis::PhraseFrameRange cut{second.start,
      second.start+static_cast<seam::time::SampleFrame>((second.end-second.start)/2)};
  const auto cutAudio=seam::rendering::PhraseRenderPipeline{selected}.render(boundedSnapshot(prepared,cut)); CHECK(cutAudio);
  CHECK(cutAudio.value().rendered.audio.startFrame==cut.start);
  CHECK(cutAudio.value().rendered.audio.samples.size()==static_cast<std::size_t>(cut.end-cut.start));
}

TEST_CASE("neural phrase runner refuses unsafe options, foreign bundles and cancellation") {
  const auto directory=seam::test::support::temporaryDirectory("neural-runner-reject");
  const auto prepared=prepare(directory,"ak");
  const auto good=options(directory);
  CHECK(AuthoringNeuralPhraseRunner::create(good));
  // Every option below is an execution-selection fault, not a music fault.
  auto relative=good; relative.bundleDirectory=std::filesystem::path{"relative"};
  CHECK(!AuthoringNeuralPhraseRunner::create(relative));
  auto unbounded=good; unbounded.maximumBundleBytes=0U;
  CHECK(!AuthoringNeuralPhraseRunner::create(unbounded));
  auto legacy=good; legacy.worker.protocolVersion=1U;
  CHECK(!AuthoringNeuralPhraseRunner::create(legacy));
  auto unbudgeted=good; unbudgeted.worker.maximumResidentBytes=0U;
  CHECK(!AuthoringNeuralPhraseRunner::create(unbudgeted));
  auto noDigest=good; noDigest.worker.helperContentHash.clear();
  CHECK(!AuthoringNeuralPhraseRunner::create(noDigest));
  auto relativeHelper=good; relativeHelper.worker.helper=std::filesystem::path{"helper"};
  CHECK(!AuthoringNeuralPhraseRunner::create(relativeHelper));
  auto blankSilence=good; blankSilence.silencePhone.clear();
  CHECK(!AuthoringNeuralPhraseRunner::create(blankSilence));
  auto absent=good; absent.bundleDirectory=std::filesystem::canonical(
      seam::test::support::temporaryDirectory("neural-runner-missing"));
  CHECK(!AuthoringNeuralPhraseRunner::create(absent));
  std::stop_source cancellation;
  cancellation.request_stop();
  CHECK(!AuthoringNeuralPhraseRunner::create(good,cancellation.get_token()));
  // A second, individually valid bundle cannot serve the first snapshot.
  const auto other=seam::test::support::temporaryDirectory("neural-runner-other");
  const auto otherPrepared=prepareWithGraph(other,"ak",true,"fixture-other");
  CHECK(otherPrepared.admitted->execution().bundleContentHash!=
      prepared.admitted->execution().bundleContentHash);
  const auto foreign=AuthoringNeuralPhraseRunner::create(options(other)); CHECK(foreign);
  const auto selected=std::make_shared<const AuthoringNeuralPhraseRunner>(std::move(foreign).value());
  CHECK(seam::rendering::PhraseRenderPipeline{selected}.render(prepared.snapshot).error().code==
      seam::core::ErrorCode::Conflict);
  // A vocabulary without the declared silence symbol is refused, never guessed.
  const auto noSilence=seam::test::support::temporaryDirectory("neural-runner-silence");
  const auto quietPrepared=prepare(noSilence,"ak",false);
  const auto runner=AuthoringNeuralPhraseRunner::create(options(noSilence)); CHECK(runner);
  const auto silenceSelected=std::make_shared<const AuthoringNeuralPhraseRunner>(std::move(runner).value());
  CHECK(seam::rendering::PhraseRenderPipeline{silenceSelected}.render(quietPrepared.snapshot).error().code==
      seam::core::ErrorCode::Unsupported);
}
