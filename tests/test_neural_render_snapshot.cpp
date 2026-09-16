#include "test_framework.hpp"
#include "test_onnx_fixture.hpp"
#include "seam/build/version.hpp"
#include "seam/core/sha256.hpp"
#include "seam/neural_synthesis/model_bundle.hpp"
#include "seam/rendering/render_pipeline.hpp"
#include "seam/rendering/render_snapshot.hpp"
#include "seam/application/project_factory.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace {

using seam::neural_synthesis::AdmittedNeuralBundle;

std::string configuration(std::uint32_t version,std::uint32_t sampleRate,std::uint64_t maximumFrames) {
  const auto features=[&] {
    return std::string{R"({"sampleRate":)"}+std::to_string(sampleRate)+
        R"(,"hopSize":256,"bins":80,"layout":"BTF","amplitudeScale":"ln-amplitude",)"+
        R"("multiplier":1.0,"offset":0.0,"minimumHz":40.0,"maximumHz":16000.0,)"+
        R"("fftSize":2048,"windowSize":1024,"melFrequencyScale":"slaney"})";
  }();
  std::string result=R"({"formatId":"com.project-seam.neural-bundle-configuration","schemaVersion":)"+
      std::to_string(version)+R"(,"maximumFrames":)"+std::to_string(maximumFrames)+R"(,"acousticFeatures":)"+features+
      R"(,"vocoderFeatures":)"+features+R"(,"stepsLayout":"scalar")";
  if (version>=3U) result+=R"(,"vocoderOutput":"audio")";
  return result+"}";
}

seam::core::Result<seam::synthesis::FrozenNeuralBundle> freezeBundle(std::uint32_t sampleRate,
    std::uint64_t maximumFrames=48000U,std::uint32_t version=3U,bool includeBreathiness=false) {
  using namespace seam::synthesis;
  const std::string acoustic=seam::test::onnx::onnxAcousticGraph(80U,1U,{},"seam-test",9U,17U,includeBreathiness);
  const std::string vocoder=seam::test::onnx::onnxVocoderGraph(80U,1U,"audio");
  const std::string declaration=configuration(version,sampleRate,maximumFrames);
  const std::string vocabulary=R"({"formatId":"com.project-seam.neural-vocabulary","schemaVersion":1,"tokens":["<PAD>","SP","aa1","k"]})";
  const auto input=[&](NeuralAssetRole role,const char* name,const std::string& value) {
    return NeuralBundleAssetInput{role,name,std::as_bytes(std::span{value.data(),value.size()}),seam::core::sha256Hex(value)};
  };
  const std::array assets{input(NeuralAssetRole::Acoustic,"acoustic",acoustic),
      input(NeuralAssetRole::Vocoder,"vocoder",vocoder),input(NeuralAssetRole::Vocabulary,"vocabulary",vocabulary),
      input(NeuralAssetRole::Configuration,"configuration",declaration)};
  const auto manifest=FrozenNeuralBundle::manifest(assets,1024U*1024U);
  if (!manifest) return seam::core::Result<FrozenNeuralBundle>{manifest.error()};
  return FrozenNeuralBundle::freeze({seam::domain::SingerResourceKind::Neural,"neural-test-bank","1",
      seam::core::sha256Hex(manifest.value())},assets,1024U*1024U);
}

struct Score final {
  seam::domain::Project project;
  seam::domain::TrackId track{};
  seam::domain::RegionId region{};
};

Score score(std::u32string lyric=U"ak",seam::domain::Language language=seam::domain::Language::English) {
  seam::application::ProjectFactory factory{9100U};
  Score result{};
  result.project=factory.createProject("Neural snapshot");
  result.track=factory.addVocalTrack(result.project,"Singer");
  result.region=factory.addRegion(result.project,result.track,"Phrase",seam::time::Tick{0},seam::time::Tick{1920});
  auto [value,note]=factory.makeNote(seam::time::Tick{0},seam::time::Tick{1920},69U,std::move(lyric),language);
  auto* region=result.project.findRegion(result.region);
  region->lyrics.push_back(std::move(value));
  region->notes.push_back(std::move(note));
  region->sortNotes();
  return result;
}

}  // namespace

namespace {

// Test-only runner. It never selects a helper or a runtime; the production
// runner is the application's, and the real admission and execution path is
// verified separately by seam_neural_production_worker.
class FixtureRunner final : public seam::rendering::NeuralPhraseRunner {
public:
  explicit FixtureRunner(std::size_t overlap=0U,std::size_t placementCount=0U)
      : overlap_(overlap),placementCount_(placementCount) {}
  [[nodiscard]] seam::core::Result<seam::synthesis::PhraseRenderResult> render(
      const seam::rendering::RenderSnapshot& snapshot,std::stop_token stopToken) const override {
    if (stopToken.stop_requested()) return seam::core::failure<seam::synthesis::PhraseRenderResult>(
        seam::core::ErrorCode::Conflict,"Fixture neural rendering cancelled");
    // Absent ownership means the full compiled extent, matching the other
    // families' "publish the complete rendered extent" rule.
    const auto fallback=snapshot.compiledPerformance!=nullptr
        ? seam::synthesis::PhraseFrameRange{snapshot.compiledPerformance->notes().front().startFrame,
                                            snapshot.compiledPerformance->notes().back().endFrame}
        : seam::synthesis::PhraseFrameRange{0,0};
    const auto range=snapshot.ownedFrames.value_or(fallback);
    const auto count=static_cast<std::size_t>(range.end-range.start)+overlap_;
    seam::synthesis::PhraseRenderResult result{.audio={range.start,std::vector<float>(count,0.25F)},.placements={}};
    for (std::size_t index=0U;index<placementCount_;++index)
      result.placements.push_back(seam::synthesis::RenderedPlacementInfo{});
    return result;
  }
private:
  std::size_t overlap_{0U};
  std::size_t placementCount_{0U};
};

}  // namespace

TEST_CASE("neural pipeline requires a selected worker runner and verifies the owned window") {
  using namespace seam::rendering;
  const auto frozen=freezeBundle(48000U); CHECK(frozen);
  const auto admitted=AdmittedNeuralBundle::admit(frozen.value(),65536U,10); CHECK(admitted);
  const NeuralRenderProvenance provenance{.workerVersion="seam-neural-worker-1",
      .runtimeVersion="onnxruntime-1.30.0",.provider="CPUExecutionProvider"};
  auto music=score();
  const auto snapshot=RenderSnapshotFactory{}.createNeural(music.project,admitted.value(),provenance,
      music.track,music.region,1U,RenderQuality::Final,48000U,"original"); CHECK(snapshot);
  // Without a selected runner the pipeline refuses instead of falling back.
  CHECK(PhraseRenderPipeline{}.render(snapshot.value()).error().code==seam::core::ErrorCode::Unsupported);
  const auto selected=std::make_shared<const FixtureRunner>();
  const auto rendered=PhraseRenderPipeline{selected}.render(snapshot.value()); CHECK(rendered);
  CHECK(rendered.value().resourceKind==seam::domain::SingerResourceKind::Neural);
  CHECK(rendered.value().proceduralMarkers.empty());
  CHECK(rendered.value().unitPlan.entries.empty());
  CHECK(rendered.value().rendered.placements.empty());
  CHECK(!rendered.value().rendered.audio.samples.empty());
  // The declared owned window is enforced exactly, not trimmed or padded later.
  const auto* performance=snapshot.value().compiledPerformance.get();
  CHECK(performance!=nullptr);
  const seam::synthesis::PhraseFrameRange window{performance->notes().front().startFrame,
      performance->notes().front().startFrame+512};
  const auto narrowed=RenderSnapshotFactory{}.createNeural(music.project,admitted.value(),provenance,
      music.track,music.region,1U,RenderQuality::Final,48000U,"original",window); CHECK(narrowed);
  const auto exact=PhraseRenderPipeline{selected}.render(narrowed.value()); CHECK(exact);
  CHECK(exact.value().rendered.audio.startFrame==window.start);
  CHECK(exact.value().rendered.audio.samples.size()==512U);
  const auto overlap=PhraseRenderPipeline{std::make_shared<const FixtureRunner>(1U)}.render(narrowed.value());
  CHECK(overlap.error().code==seam::core::ErrorCode::Conflict);
  const auto fabricated=PhraseRenderPipeline{std::make_shared<const FixtureRunner>(0U,1U)}.render(narrowed.value());
  CHECK(fabricated.error().code==seam::core::ErrorCode::InvariantViolation);
  // Cancellation reaches the runner instead of publishing stale audio.
  std::stop_source cancellation;
  cancellation.request_stop();
  CHECK(PhraseRenderPipeline{selected}.render(narrowed.value(),cancellation.get_token()).error().code==
      seam::core::ErrorCode::Conflict);
}

TEST_CASE("neural snapshot binds an admitted bundle to pronunciation and score identity") {
  using namespace seam::rendering;
  const auto frozen=freezeBundle(48000U); CHECK(frozen);
  const auto admitted=AdmittedNeuralBundle::admit(frozen.value(),65536U,10); CHECK(admitted);
  const NeuralRenderProvenance provenance{.workerVersion="seam-neural-worker-1",
      .runtimeVersion="onnxruntime-1.30.0",.provider="CPUExecutionProvider"};
  auto music=score();
  const auto snapshot=RenderSnapshotFactory{}.createNeural(music.project,admitted.value(),provenance,
      music.track,music.region,7U,RenderQuality::Final,48000U,"original"); CHECK(snapshot);
  CHECK(snapshot.value().contentHash.size()==64U);
  CHECK(snapshot.value().revision==7U);
  CHECK(snapshot.value().renderAbiId==std::string{seam::build::kRenderAbiId});
  CHECK(snapshot.value().sourceProjectId==music.project.id());
  CHECK(snapshot.value().neuralExecution!=nullptr);
  CHECK(snapshot.value().neuralExecution->execution()==admitted.value().execution());
  CHECK(renderResourceFamily(snapshot.value())==RenderResourceFamily::Neural);
  CHECK(snapshot.value().pronunciationIdentity.has_value());
  CHECK(snapshot.value().phonemes!=nullptr && !snapshot.value().phonemes->tokens.empty());
  CHECK(snapshot.value().compiledPerformance!=nullptr);
  // The score-owned context bounds the published window.
  const auto& performance=*snapshot.value().compiledPerformance;
  CHECK(!snapshot.value().ownedFrames.has_value()||
      (snapshot.value().ownedFrames->start>=performance.notes().front().startFrame&&
       snapshot.value().ownedFrames->end<=performance.notes().back().endFrame));
  // An explicit owned window participates in identity instead of being ignored.
  const seam::synthesis::PhraseFrameRange window{performance.notes().front().startFrame,
      performance.notes().front().startFrame+256};
  const auto narrowed=RenderSnapshotFactory{}.createNeural(music.project,admitted.value(),provenance,
      music.track,music.region,7U,RenderQuality::Final,48000U,"original",window); CHECK(narrowed);
  CHECK(narrowed.value().contentHash!=snapshot.value().contentHash);
  CHECK(narrowed.value().ownedFrames==window);
  CHECK(RenderSnapshotFactory{}.splitOwnedOutput(narrowed.value(),window,128U).error().code==seam::core::ErrorCode::Unsupported);
  // Identity is per execution and per music, never shareable across either.
  const auto otherSteps=AdmittedNeuralBundle::admit(frozen.value(),65536U,4); CHECK(otherSteps);
  const auto changed=RenderSnapshotFactory{}.createNeural(music.project,otherSteps.value(),provenance,
      music.track,music.region,7U,RenderQuality::Final,48000U,"original"); CHECK(changed);
  CHECK(changed.value().contentHash!=snapshot.value().contentHash);
  const NeuralRenderProvenance otherRuntime{.workerVersion="seam-neural-worker-1",
      .runtimeVersion="onnxruntime-1.31.0",.provider="CPUExecutionProvider"};
  const auto reruntime=RenderSnapshotFactory{}.createNeural(music.project,admitted.value(),otherRuntime,
      music.track,music.region,7U,RenderQuality::Final,48000U,"original"); CHECK(reruntime);
  CHECK(reruntime.value().contentHash!=snapshot.value().contentHash);
  const auto preview=RenderSnapshotFactory{}.createNeural(music.project,admitted.value(),provenance,
      music.track,music.region,7U,RenderQuality::Preview,48000U,"original"); CHECK(preview);
  CHECK(preview.value().contentHash!=snapshot.value().contentHash);
  // A neural snapshot must not answer sample or procedural queries.
  CHECK(snapshot.value().findSample()==nullptr);
  CHECK(snapshot.value().findProcedural()==nullptr);
  CHECK(PhraseRenderPipeline{}.render(snapshot.value()).error().code==seam::core::ErrorCode::Unsupported);
}

TEST_CASE("neural snapshot refuses unbounded, mismatched, foreign and stale preparation") {
  using namespace seam::rendering;
  const auto frozen=freezeBundle(48000U); CHECK(frozen);
  const auto admitted=AdmittedNeuralBundle::admit(frozen.value(),65536U,10); CHECK(admitted);
  const NeuralRenderProvenance provenance{.workerVersion="seam-neural-worker-1",
      .runtimeVersion="onnxruntime-1.30.0",.provider="CPUExecutionProvider"};
  auto music=score();
  const auto factory=RenderSnapshotFactory{};
  // Provenance is required and bounded.
  for (const NeuralRenderProvenance& broken:{NeuralRenderProvenance{},
      NeuralRenderProvenance{.workerVersion="w",.runtimeVersion="r",.provider=""},
      NeuralRenderProvenance{.workerVersion=std::string(257U,'w'),.runtimeVersion="r",.provider="p"},
      NeuralRenderProvenance{.workerVersion="w\n",.runtimeVersion="r",.provider="p"}}) {
    CHECK(!factory.createNeural(music.project,admitted.value(),broken,music.track,music.region,1U,
        RenderQuality::Final,48000U));
  }
  // The admitted model rate and the snapshot rate must agree; resampling is not
  // silently allowed to change the model contract.
  CHECK(!factory.createNeural(music.project,admitted.value(),provenance,music.track,music.region,1U,
      RenderQuality::Final,44100U));
  // An unknown track or region cannot borrow another score's identity.
  CHECK(!factory.createNeural(music.project,admitted.value(),provenance,seam::domain::TrackId{999U},music.region,1U,
      RenderQuality::Final,48000U));
  CHECK(!factory.createNeural(music.project,admitted.value(),provenance,music.track,seam::domain::RegionId{999U},1U,
      RenderQuality::Final,48000U));
  // An owned window outside the score context is refused, not clamped.
  const auto outside=RenderSnapshotFactory{}.createNeural(music.project,admitted.value(),provenance,
      music.track,music.region,1U,RenderQuality::Final,48000U,"original",
      seam::synthesis::PhraseFrameRange{-4096,4096});
  CHECK(!outside);
  // A track already bound to a procedural recipe cannot host a neural bundle.
  auto proceduralMusic=score();
  proceduralMusic.project.findVocalTrack(proceduralMusic.track)->proceduralRecipe=
      seam::domain::ProceduralRecipeReference{.path="recipe.json",.style="neutral"};
  CHECK(!factory.createNeural(proceduralMusic.project,admitted.value(),provenance,proceduralMusic.track,
      proceduralMusic.region,1U,RenderQuality::Final,48000U));
  // A region with no singable notes cannot produce an executable request.
  auto empty=score();
  empty.project.findRegion(empty.region)->notes.clear();
  CHECK(!factory.createNeural(empty.project,admitted.value(),provenance,empty.track,empty.region,1U,
      RenderQuality::Final,48000U));
}

TEST_CASE("a saved neural selection binds the snapshot to exactly that bundle") {
  using namespace seam::rendering;
  const auto frozen=freezeBundle(48000U); CHECK(frozen);
  const auto admitted=AdmittedNeuralBundle::admit(frozen.value(),65536U,10); CHECK(admitted);
  const NeuralRenderProvenance provenance{.workerVersion="seam-neural-worker-1",
      .runtimeVersion="onnxruntime-1.30.0",.provider="CPUExecutionProvider"};
  auto music=score();
  const auto factory=RenderSnapshotFactory{};
  // The persistence layer stores the same identity the snapshot admits.
  auto* track=music.project.findVocalTrack(music.track);
  track->neuralResource=seam::domain::NeuralResourceReference{seam::domain::SingerResourceIdentity{
      seam::domain::SingerResourceKind::Neural,admitted.value().execution().modelId,
      admitted.value().execution().modelVersion,admitted.value().execution().bundleContentHash}};
  CHECK(music.project.validate());
  const auto bound=factory.createNeural(music.project,admitted.value(),provenance,music.track,
      music.region,1U,RenderQuality::Final,48000U,"original"); CHECK(bound);
  CHECK(track->neuralResource->resource.contentHash==bound.value().neuralExecution->execution().bundleContentHash);
  // A different bundle, version or digest may not satisfy a saved selection.
  for (int field=0;field<3;++field) {
    auto changed=*track->neuralResource;
    if (field==0) changed.resource.id="another-bank";
    if (field==1) changed.resource.version="2";
    if (field==2) changed.resource.contentHash=std::string(64U,'b');
    track->neuralResource=changed;
    CHECK(factory.createNeural(music.project,admitted.value(),provenance,music.track,music.region,1U,
        RenderQuality::Final,48000U,"original").error().code==seam::core::ErrorCode::Conflict);
  }
  // An unbound track still previews, so selecting a voice is not a precondition
  // for trying one.
  track->neuralResource.reset();
  CHECK(factory.createNeural(music.project,admitted.value(),provenance,music.track,music.region,1U,
      RenderQuality::Final,48000U,"original"));
}

TEST_CASE("unadmitted and legacy neural resources stay non-executable for rendering") {
  using namespace seam::rendering;
  // A legacy opaque model resource is a family tag, never an execution carrier.
  RenderSnapshot legacy{};
  legacy.resource=seam::synthesis::NeuralSingerResource{};
  CHECK(renderResourceFamily(legacy)==RenderResourceFamily::Neural);
  CHECK(legacy.neuralExecution==nullptr);
  CHECK(PhraseRenderPipeline{}.render(legacy).error().code==seam::core::ErrorCode::Unsupported);
  // Sample and procedural families are unchanged by the neural carrier.
  RenderSnapshot sample{};
  sample.resource=seam::synthesis::SampleSingerResource{};
  CHECK(renderResourceFamily(sample)==RenderResourceFamily::Sample);
}

TEST_CASE("neural snapshot refuses breathiness control unless admitted graph declares it") {
  using namespace seam::rendering;
  const auto frozenDefault = freezeBundle(48000U, 48000U, 3U, false); CHECK(frozenDefault);
  const auto admittedDefault = AdmittedNeuralBundle::admit(frozenDefault.value(), 65536U, 10); CHECK(admittedDefault);
  CHECK(!admittedDefault.value().acousticGraph().supportsConditioningControl("breathiness"));

  const auto frozenWithBreath = freezeBundle(48000U, 48000U, 3U, true); CHECK(frozenWithBreath);
  const auto admittedWithBreath = AdmittedNeuralBundle::admit(frozenWithBreath.value(), 65536U, 10); CHECK(admittedWithBreath);
  CHECK(admittedWithBreath.value().acousticGraph().supportsConditioningControl("breathiness"));

  const NeuralRenderProvenance provenance{.workerVersion="seam-neural-worker-1",
      .runtimeVersion="onnxruntime-1.30.0",.provider="CPUExecutionProvider"};

  auto music = score();
  auto* region = music.project.findRegion(music.region);
  CHECK(region != nullptr);
  CHECK(region->breathinessAutomation.upsert(seam::domain::BreathinessAutomationPoint{seam::time::Tick{0}, 0.5F}).hasValue());

  const auto refused = RenderSnapshotFactory{}.createNeural(music.project, admittedDefault.value(), provenance,
      music.track, music.region, 1U, RenderQuality::Final, 48000U, "original");
  CHECK(!refused);
  CHECK(refused.error().code == seam::core::ErrorCode::Unsupported);
  CHECK(refused.error().message.find("breathiness") != std::string::npos);

  const auto accepted = RenderSnapshotFactory{}.createNeural(music.project, admittedWithBreath.value(), provenance,
      music.track, music.region, 1U, RenderQuality::Final, 48000U, "original");
  CHECK(accepted);
  CHECK(accepted.value().neuralExecution->acousticGraph().supportsConditioningControl("breathiness"));
}
