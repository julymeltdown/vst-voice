
// Production-worker render coverage: the normal authoring render path must be able to
// publish audio that a real admitted ONNX bundle produced through the shipped worker.
//
// The graphs are arithmetic fixtures, so this proves execution and routing, not
// musical quality. The transport-probe workflow test covers routing alone; this one
// replaces the probe with the production worker and requires non-silent output.
//
// A Python check drives the test, because an admitted bundle needs real ONNX graph
// bytes. Two phases share one deterministic project: setting
// SEAM_NEURAL_PRODUCTION_VOCABULARY_OUT writes the phone mapping the phrase needs, and
// setting SEAM_NEURAL_PRODUCTION_BUNDLE renders it. With neither variable set the test
// does nothing, so a suite run without ONNX stays honest.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/authoring/neural_phrase_runner.hpp"
#include "seam/authoring/render_coordinator.hpp"
#include "seam/authoring/export_service.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/neural_synthesis/bundle_metadata.hpp"
#include "seam/neural_synthesis/model_bundle.hpp"
#include "seam/neural_synthesis/diffsinger_inputs.hpp"
#include "seam/phonemizer/language_resolver.hpp"
#include "seam/rendering/project_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#ifndef SEAM_NEURAL_PRODUCTION_WORKER
#error SEAM_NEURAL_PRODUCTION_WORKER is required for production render coverage
#endif

namespace {

using seam::authoring::AuthoringNeuralPhraseRunner;
using seam::neural_synthesis::AdmittedNeuralBundle;
using seam::rendering::TrackNeuralSource;
using seam::rendering::TrackSingerSource;

struct Phrase final {
  seam::domain::TrackId track{};
  seam::domain::RegionId region{};
  std::set<std::string> symbols;
};

// One deterministic project in both phases: the vocabulary the worker loads has to
// cover exactly the phones this phrase asks for.
Phrase buildPhrase(seam::application::ProjectFactory& factory, seam::domain::Project& project) {
  const bool nasalFixture = std::getenv("SEAM_TEST_MORAIC_NASAL") != nullptr;
  Phrase result{};
  result.track = factory.addVocalTrack(project, "Neural fixture");
  result.region = factory.addRegion(project, result.track, "Phrase", seam::time::Tick{0},
                                    seam::time::Tick{1920});
  auto* region = project.findRegion(result.region);
  if (region == nullptr) throw seam::test::Failure{"production render region is missing"};
  for (std::size_t index = 0U; index < 2U; ++index) {
    auto note = factory.makeNote(
        seam::time::Tick{960 * static_cast<std::int64_t>(index)}, seam::time::Tick{960},
        69U, nasalFixture ? (index == 0U ? U"あ" : U"ん") : U"a",
        nasalFixture ? seam::domain::Language::Japanese : seam::domain::Language::English);
    region->lyrics.push_back(std::move(note.first));
    region->notes.push_back(std::move(note.second));
  }
  region->sortNotes();
  // The companion nasal fixture keeps the original English coverage and adds a
  // whole-note N through the real worker without a vowel or authored override.
  const auto pronunciation = seam::phonemizer::resolvePronunciation(*region);
  if (!pronunciation) throw seam::test::Failure{"phonemizer fixture failed: " + pronunciation.error().message};
  for (const auto& token : pronunciation.value().pronunciation.tokens) {
    result.symbols.insert(token.symbol);
  }
  return result;
}

void writeVocabulary(const std::set<std::string>& symbols, const std::filesystem::path& path) {
  const char quote = '"';
  std::string document = "{";
  document += quote;
  document += "SP";
  document += quote;
  document += ":1";
  int identifier = 2;
  for (const auto& symbol : symbols) {
    if (symbol == "SP") continue;
    document += ",";
    document += quote;
    document += symbol;
    document += quote;
    document += ":";
    document += std::to_string(identifier++);
  }
  document += "}";
  document += static_cast<char>(10);
  const auto bytes = std::as_bytes(std::span{document});
  if (!seam::core::durableAtomicWriteNew(path, bytes))
    throw seam::test::Failure{"could not write the required vocabulary"};
}

std::string requireEnvironment(const char* name) {
  const auto* value = std::getenv(name);
  if (value == nullptr || *value == 0) throw seam::test::Failure{std::string{name} + " is unset"};
  return value;
}

seam::authoring::RenderProgress waitForTerminal(
    seam::authoring::AuthoringRenderCoordinator& coordinator, std::uint64_t revision,
    std::chrono::milliseconds timeout = std::chrono::milliseconds{30000}) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto progress = coordinator.progress();
    if (progress.requestedRevision == revision &&
        (progress.state == seam::authoring::RenderState::Ready ||
         progress.state == seam::authoring::RenderState::Cancelled ||
         progress.state == seam::authoring::RenderState::Failed)) {
      return progress;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  return coordinator.progress();
}

}  // namespace

TEST_CASE("an admitted bundle renders non-silent audio through the production worker") {
  const auto* vocabularyOut = std::getenv("SEAM_NEURAL_PRODUCTION_VOCABULARY_OUT");
  const auto* bundleDirectory = std::getenv("SEAM_NEURAL_PRODUCTION_BUNDLE");
  if (vocabularyOut == nullptr && bundleDirectory == nullptr) return;

  const auto root = seam::test::support::temporaryDirectory("neural-production-render");
  seam::application::ProjectFactory factory{9400U};
  auto project = factory.createProject("Production neural render");
  auto phrase = buildPhrase(factory, project);
  if (vocabularyOut != nullptr) {
    writeVocabulary(phrase.symbols, std::filesystem::path{vocabularyOut});
    if (const auto* projectOut=std::getenv("SEAM_NEURAL_PRODUCTION_PROJECT_OUT")) {
      const auto json=seam::formats::ProjectJsonCodec{}.encode(project); CHECK(json);
      CHECK(seam::core::durableAtomicWriteNew(projectOut,std::as_bytes(std::span{json.value()})));
    }
    return;
  }

  const auto* projectInput=std::getenv("SEAM_NEURAL_PRODUCTION_PROJECT");
  if (projectInput!=nullptr) {
    const auto json=seam::core::readTextFileLimited(projectInput,4U*1024U*1024U); CHECK(json);
    CHECK(seam::core::sha256Hex(json.value())==requireEnvironment("SEAM_NEURAL_PRODUCTION_PROJECT_SHA256"));
    auto decoded=seam::formats::ProjectJsonCodec{}.decode(json.value()); CHECK(decoded);
    project=std::move(decoded).value();
    CHECK(project.vocalTracks().size()==1U);
    CHECK(project.audioTracks().empty());
    auto& track=project.vocalTracks().front();
    CHECK(track.regions.size()==1U && !track.regions.front().notes.empty());
    phrase.track=track.id; phrase.region=track.regions.front().id;
    // Explicit experiment selection in memory; never overwrite the input song.
    track.proceduralRecipe.reset();
    track.voicebank={};
  }

  const std::filesystem::path directory{bundleDirectory};
  const std::string manifestSha256 = requireEnvironment("SEAM_NEURAL_PRODUCTION_MANIFEST_SHA256");
  const auto maximumBytes = static_cast<std::size_t>(
      std::stoull(requireEnvironment("SEAM_NEURAL_PRODUCTION_MAXIMUM_BYTES")));
  const auto* selectedId=std::getenv("SEAM_NEURAL_PRODUCTION_MODEL_ID");
  const auto* selectedVersion=std::getenv("SEAM_NEURAL_PRODUCTION_MODEL_VERSION");
  const auto identity = seam::domain::SingerResourceIdentity{
      seam::domain::SingerResourceKind::Neural, selectedId ? selectedId : "fixture",
      selectedVersion ? selectedVersion : "1", manifestSha256};
  if (projectInput!=nullptr)
    project.vocalTracks().front().neuralResource=seam::domain::NeuralResourceReference{identity};
  const auto frozen = seam::neural_synthesis::loadNeuralBundleDirectory(directory, identity, maximumBytes);
  if (!frozen) throw seam::test::Failure{"bundle load failed: " + frozen.error().message};
  const auto admitted = AdmittedNeuralBundle::admit(frozen.value(), 4U*1024U*1024U, 10);
  if (!admitted) throw seam::test::Failure{"bundle admission failed: " + admitted.error().message};
  auto bundle = std::make_shared<const AdmittedNeuralBundle>(std::move(admitted).value());

  const std::string helper = SEAM_NEURAL_PRODUCTION_WORKER;
  const auto helperHash = seam::core::sha256File(helper);
  if (!helperHash) throw seam::test::Failure{"worker hashing failed"};
  seam::authoring::NeuralPhraseRunnerOptions options{};
  options.bundleDirectory = std::filesystem::canonical(directory);
  options.maximumBundleBytes = maximumBytes;
  if (const auto* silence=std::getenv("SEAM_NEURAL_PRODUCTION_SILENCE_PHONE"))
    options.silencePhone=silence;
  options.worker = seam::neural_synthesis::NeuralWorkerRunOptions{
      .helper = helper,
      .helperContentHash = helperHash.value(),
      .timeout = std::chrono::seconds{60},
      .maximumResidentBytes = (projectInput ? 1024U : 256U) * 1024U * 1024U,
      .maximumCpuTime = std::chrono::seconds{30},
      .protocolVersion = 2U};
  const auto runner = AuthoringNeuralPhraseRunner::create(options);
  if (!runner) throw seam::test::Failure{"production runner failed: " + runner.error().message};
  const auto provenance = seam::rendering::NeuralRenderProvenance{
      .workerVersion = "seam-neural-worker-1",
      .runtimeVersion = "onnxruntime-1.30.0",
      .provider = "CPUExecutionProvider"};
  const std::vector<TrackSingerSource> sources{TrackNeuralSource{
      phrase.track, bundle, provenance,
      std::make_shared<const AuthoringNeuralPhraseRunner>(std::move(runner).value())}};
  seam::authoring::AuthoringRenderCoordinator coordinator{root / "cache"};
  coordinator.submitWithSources(project, sources, phrase.track, phrase.region, 7U, 48000U,
      seam::rendering::RenderQuality::Preview, true);
  const auto progress = waitForTerminal(coordinator, 7U);
  if (progress.state != seam::authoring::RenderState::Ready)
    throw seam::test::Failure{"production neural render did not complete: " + progress.diagnostic};
  const auto published = coordinator.latest();
  CHECK(published);
  if (!published) return;
  // The renderer identity, admitted model and worker runtime must all be visible, so a
  // published phrase can never be attributed to the probe or to another bundle.
  CHECK(published->activeRenderer == "seam.neural-worker.v1");
  CHECK(published->neuralIdentities.size() == 1U);
  CHECK(published->neuralIdentities.front().modelId == identity.id);
  CHECK(published->neuralIdentities.front().bundleContentHash == manifestSha256);
  CHECK(published->neuralIdentities.front().provider == "CPUExecutionProvider");
  CHECK(published->result.activeUnitPlan.empty());
  CHECK(published->result.interleaved.size() > 0U);
  const auto samples = published->result.interleaved.data();
  std::size_t nonzero = 0U;
  bool finite = true;
  for (std::size_t index = 0U; index < published->result.interleaved.size(); ++index) {
    const float sample = samples[index];
    finite = finite && std::isfinite(sample) && std::abs(sample) <= 1.0F;
    if (sample != 0.0F) ++nonzero;
  }
  CHECK(finite);
  // The transport probe publishes silence; a real execution must not.
  CHECK(nonzero > 0U);
  CHECK(nonzero * 2U >= published->result.interleaved.size());
  if (projectInput!=nullptr) {
    seam::authoring::ExportSettings settings{};
    settings.includeMaster=true; settings.includeStems=true; settings.includeProjectAndRecipes=true;
    const auto exported=seam::authoring::ExportService{}.exportSetWithSources(project,sources,
        phrase.track,phrase.region,8U,requireEnvironment("SEAM_NEURAL_PRODUCTION_EXPORT"),settings);
    if (!exported) throw seam::test::Failure{"candidate export failed: "+exported.error().message};
    CHECK(exported.value().state==seam::authoring::ExportState::Committed);
    CHECK(!exported.value().masterSha256.empty());
    const auto restored=seam::formats::ProjectJsonCodec{}.load(
        std::filesystem::path{requireEnvironment("SEAM_NEURAL_PRODUCTION_EXPORT")} / "project.seam");
    CHECK(restored);
    CHECK(restored.value().vocalTracks().front().neuralResource);
    CHECK(restored.value().vocalTracks().front().neuralResource->resource==identity);
    std::cout << "candidate application export committed with master SHA256 "
              << exported.value().masterSha256 << "; singerQualified=false" << std::endl;
  }
  // Explicit test-harness replay inputs, not a production worker capture hook.
  // Reconstruct from the same native snapshot/score conversion as the runner.
  // A downstream replay must prove output parity before claiming worker tensors.
  if (const auto* capture=std::getenv("SEAM_NEURAL_PRODUCTION_INPUTS_OUT")) {
    const auto snapshot=seam::rendering::RenderSnapshotFactory{}.createNeural(project,*bundle,provenance,
        phrase.track,phrase.region,8U,seam::rendering::RenderQuality::Final,48000U,"original"); CHECK(snapshot);
    const auto& value=snapshot.value();
    const auto& metadata=value.neuralExecution->metadata();
    const auto& performance=*value.compiledPerformance;
    const auto request=seam::neural_synthesis::prepareNeuralScoreRequest(1U,metadata.model,
        metadata.vocabulary,performance,value.phonemes->tokens,value.pronunciationIdentity->sequenceHash,
        performance.notes().front().startFrame,performance.notes().back().endFrame,options.silencePhone);
    CHECK(request);
    const auto inputs=seam::neural_synthesis::prepareDiffSingerAcousticInputs(
        request.value(),metadata.model,metadata.vocabulary,10); CHECK(inputs);
    std::ostringstream json;
    json << std::setprecision(17)
         << "{\"formatId\":\"com.project-seam.native-input-replay\",\"schemaVersion\":1,"
         << "\"workerTensorCapture\":false,\"singerQualified\":false,\"releaseEligible\":false,"
         << "\"manifestSha256\":\"" << manifestSha256 << "\",\"workerSha256\":\"" << helperHash.value()
         << "\",\"inputRevision\":" << seam::neural_synthesis::kDiffSingerInputRevision
         << ",\"steps\":" << inputs.value().steps
         << ",\"outputSampleFrames\":" << inputs.value().outputSampleFrames
         << ",\"paddedSampleFrames\":" << inputs.value().paddedSampleFrames;
    if (projectInput) json << ",\"projectSha256\":\""
        << requireEnvironment("SEAM_NEURAL_PRODUCTION_PROJECT_SHA256") << '"';
    const auto append=[&](const char* name,const auto& values) {
      json << ",\"" << name << "\":[";
      for (std::size_t i=0;i<values.size();++i) { if (i) json << ','; json << values[i]; }
      json << ']';
    };
    append("tokens",inputs.value().tokens); append("durations",inputs.value().durations);
    append("f0",inputs.value().f0Hz); append("breathiness",inputs.value().breathiness);
    // Finalization gains are sample-domain, not acoustic F0 or score-rest masks.
    // Retain exact runs so replay need not guess note-envelope behavior.
    json << ",\"dynamicsRuns\":[";
    const auto& dynamics=request.value().dynamics;
    for (std::size_t start=0;start<dynamics.size();) {
      auto end=start+1U;
      while (end<dynamics.size() && dynamics[end]==dynamics[start]) ++end;
      if (start) json << ',';
      json << '[' << end << ',' << dynamics[start] << ']';
      CHECK(json.tellp()<=static_cast<std::streamoff>(8U*1024U*1024U));
      start=end;
    }
    json << ']';
    json << '}';
    CHECK(json.str().size()<=8U*1024U*1024U);
    CHECK(seam::core::durableAtomicWriteTextNew(capture,json.str()));
  }
  // Ownership splits must preserve full model context, even at non-hop-aligned
  // boundaries. This uses the real ONNX worker, not the silent transport probe.
  if (projectInput==nullptr) {
  const auto snapshot=seam::rendering::RenderSnapshotFactory{}.createNeural(project,*bundle,provenance,
      phrase.track,phrase.region,8U,seam::rendering::RenderQuality::Final,48000U,"original"); CHECK(snapshot);
  const auto& selected=*std::get<TrackNeuralSource>(sources.front()).runner;
  const auto whole=selected.render(snapshot.value(),{}); CHECK(whole);
  const auto& notes=snapshot.value().compiledPerformance->notes();
  const seam::synthesis::PhraseFrameRange context{notes.front().startFrame,notes.back().endFrame};
  const auto chunks=seam::rendering::RenderSnapshotFactory{}.splitOwnedOutput(snapshot.value(),context,17003U); CHECK(chunks);
  std::vector<float> stitched;
  for (const auto& chunk:chunks.value()) {
    const auto audio=selected.render(chunk,{}); CHECK(audio);
    CHECK(audio.value().audio.startFrame==context.start+static_cast<seam::time::SampleFrame>(stitched.size()));
    stitched.insert(stitched.end(),audio.value().audio.samples.begin(),audio.value().audio.samples.end());
  }
  CHECK(stitched==whole.value().audio.samples);
  }
  // State what actually happened, so a caller cannot mistake a skipped phase for a
  // completed render.
  std::cout << "production worker rendered " << published->result.interleaved.size()
            << " interleaved samples through " << published->activeRenderer << " with "
            << nonzero << " nonzero samples; workerSha256=" << helperHash.value() << std::endl;

  // A wrong captured manifest identity must be refused before any graph runs.
  const auto wrong = seam::neural_synthesis::loadNeuralBundleDirectory(directory,
      seam::domain::SingerResourceIdentity{seam::domain::SingerResourceKind::Neural,
          "fixture", "1", std::string(64U, '0')}, maximumBytes);
  CHECK(!wrong);
}
