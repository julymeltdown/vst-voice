
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
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/neural_synthesis/bundle_metadata.hpp"
#include "seam/neural_synthesis/model_bundle.hpp"
#include "seam/phonemizer/language_resolver.hpp"
#include "seam/rendering/project_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <fstream>
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
  Phrase result{};
  result.track = factory.addVocalTrack(project, "Neural fixture");
  result.region = factory.addRegion(project, result.track, "Phrase", seam::time::Tick{0},
                                    seam::time::Tick{1920});
  auto* region = project.findRegion(result.region);
  if (region == nullptr) throw seam::test::Failure{"production render region is missing"};
  for (std::size_t index = 0U; index < 2U; ++index) {
    auto note = factory.makeNote(
        seam::time::Tick{960 * static_cast<std::int64_t>(index)}, seam::time::Tick{960},
        69U, U"a", seam::domain::Language::English);
    region->lyrics.push_back(std::move(note.first));
    region->notes.push_back(std::move(note.second));
  }
  region->sortNotes();
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
  const auto phrase = buildPhrase(factory, project);
  if (vocabularyOut != nullptr) {
    writeVocabulary(phrase.symbols, std::filesystem::path{vocabularyOut});
    return;
  }

  const std::filesystem::path directory{bundleDirectory};
  const std::string manifestSha256 = requireEnvironment("SEAM_NEURAL_PRODUCTION_MANIFEST_SHA256");
  const auto maximumBytes = static_cast<std::size_t>(
      std::stoull(requireEnvironment("SEAM_NEURAL_PRODUCTION_MAXIMUM_BYTES")));
  const auto identity = seam::domain::SingerResourceIdentity{
      seam::domain::SingerResourceKind::Neural, "fixture", "1", manifestSha256};
  const auto frozen = seam::neural_synthesis::loadNeuralBundleDirectory(directory, identity, maximumBytes);
  if (!frozen) throw seam::test::Failure{"bundle load failed: " + frozen.error().message};
  const auto admitted = AdmittedNeuralBundle::admit(frozen.value(), 65536U, 10);
  if (!admitted) throw seam::test::Failure{"bundle admission failed: " + admitted.error().message};
  auto bundle = std::make_shared<const AdmittedNeuralBundle>(std::move(admitted).value());

  const std::string helper = SEAM_NEURAL_PRODUCTION_WORKER;
  const auto helperHash = seam::core::sha256File(helper);
  if (!helperHash) throw seam::test::Failure{"worker hashing failed"};
  seam::authoring::NeuralPhraseRunnerOptions options{};
  options.bundleDirectory = std::filesystem::canonical(directory);
  options.maximumBundleBytes = maximumBytes;
  options.worker = seam::neural_synthesis::NeuralWorkerRunOptions{
      .helper = helper,
      .helperContentHash = helperHash.value(),
      .timeout = std::chrono::seconds{60},
      .maximumResidentBytes = 256U * 1024U * 1024U,
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
  CHECK(published->neuralIdentities.front().modelId == "fixture");
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
  // State what actually happened, so a caller cannot mistake a skipped phase for a
  // completed render.
  std::cout << "production worker rendered " << published->result.interleaved.size()
            << " interleaved samples through " << published->activeRenderer << " with "
            << nonzero << " nonzero samples" << std::endl;

  // A wrong captured manifest identity must be refused before any graph runs.
  const auto wrong = seam::neural_synthesis::loadNeuralBundleDirectory(directory,
      seam::domain::SingerResourceIdentity{seam::domain::SingerResourceKind::Neural,
          "fixture", "1", std::string(64U, '0')}, maximumBytes);
  CHECK(!wrong);
}
