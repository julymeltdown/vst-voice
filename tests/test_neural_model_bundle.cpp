#include "test_framework.hpp"
#include "seam/core/sha256.hpp"
#include "seam/neural_synthesis/model_bundle.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace {

using seam::neural_synthesis::AdmittedNeuralBundle;

std::string configuration(std::uint32_t version,std::uint64_t maximumFrames) {
  const std::string basic=R"({"sampleRate":48000,"hopSize":256,"bins":80,"layout":"BTF",)"
      R"("amplitudeScale":"ln-amplitude","multiplier":1.0,"offset":0.0,"minimumHz":40.0,"maximumHz":16000.0})";
  const std::string extended=R"({"sampleRate":48000,"hopSize":256,"bins":80,"layout":"BTF",)"
      R"("amplitudeScale":"ln-amplitude","multiplier":1.0,"offset":0.0,"minimumHz":40.0,"maximumHz":16000.0,)"
      R"("fftSize":2048,"windowSize":1024,"melFrequencyScale":"slaney"})";
  const std::string features=version>=2U?extended:basic;
  std::string result=R"({"formatId":"com.project-seam.neural-bundle-configuration","schemaVersion":)"+
      std::to_string(version)+R"(,"maximumFrames":)"+std::to_string(maximumFrames)+R"(,"acousticFeatures":)"+features+
      R"(,"vocoderFeatures":)"+features;
  if (version>=2U) result+=R"(,"stepsLayout":"scalar")";
  if (version>=3U) result+=R"(,"vocoderOutput":"audio")";
  return result+"}";
}

seam::core::Result<seam::synthesis::FrozenNeuralBundle> fixture(std::string_view graph,
    std::uint32_t version,std::uint64_t maximumFrames,
    std::string_view vocabulary=R"({"formatId":"com.project-seam.neural-vocabulary","schemaVersion":1,"tokens":["<PAD>","SP","a"]})") {
  using namespace seam::synthesis;
  const std::string declaration=configuration(version,maximumFrames);
  const auto input=[&](NeuralAssetRole role,const char* name,std::string_view value) {
    return NeuralBundleAssetInput{role,name,std::as_bytes(std::span{value.data(),value.size()}),seam::core::sha256Hex(value)};
  };
  const std::array assets{input(NeuralAssetRole::Acoustic,"acoustic",graph),
      input(NeuralAssetRole::Vocoder,"vocoder",graph),input(NeuralAssetRole::Vocabulary,"vocabulary",vocabulary),
      input(NeuralAssetRole::Configuration,"configuration",declaration)};
  const auto manifest=FrozenNeuralBundle::manifest(assets,1024U*1024U);
  if (!manifest) return seam::core::Result<FrozenNeuralBundle>{manifest.error()};
  return FrozenNeuralBundle::freeze({seam::domain::SingerResourceKind::Neural,"bundle-test","1",
      seam::core::sha256Hex(manifest.value())},assets,1024U*1024U);
}

}  // namespace

TEST_CASE("admitted neural bundle binds execution identity and shares frozen assets") {
  using namespace seam::neural_synthesis;
  const auto bundle=fixture("admissible graph fixture",3U,48000U);
  CHECK(bundle);
  const auto admitted=AdmittedNeuralBundle::admit(bundle.value(),65536U,10);
  CHECK(admitted);
  const auto& identity=admitted.value().execution();
  CHECK(identity.modelId=="bundle-test");
  CHECK(identity.modelVersion=="1");
  CHECK(identity.bundleContentHash==bundle.value().identity().contentHash);
  CHECK(identity.configurationVersion==3U);
  CHECK(identity.inferenceSteps==10);
  CHECK(admitted.value().metadata().stepsLayout=="scalar");
  CHECK(admitted.value().metadata().vocoderOutput=="audio");
  CHECK(admitted.value().metadata().features.bins==80U);
  // A copied handle shares the admitted bytes instead of duplicating a model.
  const auto copy=admitted.value();
  CHECK(copy.acoustic().get()==admitted.value().acoustic().get());
  CHECK(copy.vocoder().get()==admitted.value().vocoder().get());
  CHECK(copy.vocabulary().get()==admitted.value().vocabulary().get());
  CHECK(copy.acoustic()->sha256()==seam::core::sha256Hex("admissible graph fixture"));
  // A different steps value is a different execution identity.
  const auto other=AdmittedNeuralBundle::admit(bundle.value(),65536U,20);
  CHECK(other);
  CHECK(other.value().execution()!=identity);
  CHECK(other.value().execution().inferenceSteps==20);
  CHECK(admitted.value().execution()==identity);
}

TEST_CASE("admitted neural bundle refuses legacy, over-budget, mismatched and cancelled preparation") {
  using namespace seam::neural_synthesis;
  // Schema v1 declares neither a steps layout nor an output name.
  const auto legacy=fixture("admissible graph fixture",1U,48000U);
  CHECK(legacy);
  const auto refusedLegacy=AdmittedNeuralBundle::admit(legacy.value(),65536U,10);
  CHECK(!refusedLegacy);
  const auto declared=fixture("admissible graph fixture",3U,48000U);
  CHECK(declared);
  const auto overBudget=AdmittedNeuralBundle::admit(declared.value(),4096U,10);
  CHECK(!overBudget);
  CHECK(!AdmittedNeuralBundle::admit(declared.value(),65536U,0));
  CHECK(!AdmittedNeuralBundle::admit(declared.value(),65536U,1001));
  CHECK(!AdmittedNeuralBundle::admit(declared.value(),0U,10));
  CHECK(!AdmittedNeuralBundle::admit(declared.value(),5U*1024U*1024U,10));
  // A moved-from handle is the reachable empty state; admission refuses it.
  auto moved=declared.value();
  const auto retained=std::move(moved);
  CHECK(retained.valid() && !moved.valid());
  CHECK(!AdmittedNeuralBundle::admit(std::move(moved),65536U,10));
  const auto vocabulary=R"({"formatId":"com.project-seam.neural-vocabulary","schemaVersion":1,"tokens":["<PAD>","SP"]})";
  const auto other=fixture("admissible graph fixture",3U,48000U,vocabulary);
  CHECK(other);
  const auto admitted=AdmittedNeuralBundle::admit(other.value(),65536U,10);
  CHECK(admitted);
  CHECK(admitted.value().metadata().vocabulary.size()==2U);
  std::stop_source cancellation;
  cancellation.request_stop();
  CHECK(!AdmittedNeuralBundle::admit(declared.value(),65536U,10,cancellation.get_token()));
  // Freezing is byte binding only; a vocabulary that cannot be decoded is
  // refused at admission, before any phrase is prepared.
  const auto undecodable=fixture("admissible graph fixture",3U,48000U,"{}");
  CHECK(undecodable);
  CHECK(!AdmittedNeuralBundle::admit(undecodable.value(),65536U,10));
}
