#include "test_framework.hpp"
#include "test_onnx_fixture.hpp"
#include "seam/core/sha256.hpp"
#include "seam/neural_synthesis/model_bundle.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace {

using seam::neural_synthesis::AdmittedNeuralBundle;

std::string configuration(std::uint32_t version,std::uint64_t maximumFrames,
    std::string_view conditioningDefaults={}) {
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
  if (version>=4U) result+=R"(,"conditioningDefaults":)"+std::string{conditioningDefaults};
  return result+"}";
}

seam::core::Result<seam::synthesis::FrozenNeuralBundle> fixture(std::string_view acoustic,
    std::string_view vocoder,std::uint32_t version,std::uint64_t maximumFrames,
    std::string_view vocabulary=R"({"formatId":"com.project-seam.neural-vocabulary","schemaVersion":1,"tokens":["<PAD>","SP","a"]})",
    std::string_view conditioningDefaults={}) {
  using namespace seam::synthesis;
  const std::string declaration=configuration(version,maximumFrames,conditioningDefaults);
  const auto input=[&](NeuralAssetRole role,const char* name,std::string_view value) {
    return NeuralBundleAssetInput{role,name,std::as_bytes(std::span{value.data(),value.size()}),seam::core::sha256Hex(value)};
  };
  const std::array assets{input(NeuralAssetRole::Acoustic,"acoustic",acoustic),
      input(NeuralAssetRole::Vocoder,"vocoder",vocoder),input(NeuralAssetRole::Vocabulary,"vocabulary",vocabulary),
      input(NeuralAssetRole::Configuration,"configuration",declaration)};
  const auto manifest=FrozenNeuralBundle::manifest(assets,1024U*1024U);
  if (!manifest) return seam::core::Result<FrozenNeuralBundle>{manifest.error()};
  return FrozenNeuralBundle::freeze({seam::domain::SingerResourceKind::Neural,"bundle-test","1",
      seam::core::sha256Hex(manifest.value())},assets,1024U*1024U);
}

}  // namespace

namespace {

// The configured declaration names its vocoder output "audio" and its mel layout BTF with 80 bins,
// so the fixtures below declare exactly that and the tests vary one thing at a time from there.
std::string acousticGraph() { return seam::test::onnx::onnxAcousticGraph(); }
std::string vocoderGraph() { return seam::test::onnx::onnxVocoderGraph(80U, 1U, "audio"); }

}  // namespace

TEST_CASE("admitted neural bundle binds execution identity and shares frozen assets") {
  using namespace seam::neural_synthesis;
  const auto bundle=fixture(acousticGraph(),vocoderGraph(),3U,48000U);
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
  CHECK(copy.acoustic()->sha256()==seam::core::sha256Hex(acousticGraph()));
  // The handle retains what each graph file declares, not just the configuration's description.
  CHECK(admitted.value().acousticGraph().opset==17U);
  CHECK(admitted.value().acousticGraph().producer=="seam-test");
  CHECK(admitted.value().acousticGraph().findOutput("mel")!=nullptr);
  CHECK(admitted.value().vocoderGraph().findInput("mel")!=nullptr);
  CHECK(admitted.value().vocoderGraph().findOutput("audio")!=nullptr);
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
  const auto legacy=fixture(acousticGraph(),vocoderGraph(),1U,48000U);
  CHECK(legacy);
  const auto refusedLegacy=AdmittedNeuralBundle::admit(legacy.value(),65536U,10);
  CHECK(!refusedLegacy);
  const auto declared=fixture(acousticGraph(),vocoderGraph(),3U,48000U);
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
  const auto other=fixture(acousticGraph(),vocoderGraph(),3U,48000U,vocabulary);
  CHECK(other);
  const auto admitted=AdmittedNeuralBundle::admit(other.value(),65536U,10);
  CHECK(admitted);
  CHECK(admitted.value().metadata().vocabulary.size()==2U);
  std::stop_source cancellation;
  cancellation.request_stop();
  CHECK(!AdmittedNeuralBundle::admit(declared.value(),65536U,10,cancellation.get_token()));
  // Freezing is byte binding only; a vocabulary that cannot be decoded is
  // refused at admission, before any phrase is prepared.
  const auto undecodable=fixture(acousticGraph(),vocoderGraph(),3U,48000U,"{}");
  CHECK(undecodable);
  CHECK(!AdmittedNeuralBundle::admit(undecodable.value(),65536U,10));
}

TEST_CASE("schema four conditioning defaults bind to the vocabulary and the graph") {
  using namespace seam::neural_synthesis;
  const std::string defaults=R"({"breathiness":{"z":0.4,"a":0.0}})";
  const std::string vocabulary=R"({"formatId":"com.project-seam.neural-vocabulary","schemaVersion":1,"tokens":["<PAD>","SP","a","z"]})";
  const auto conditioned=seam::test::onnx::onnxAcousticGraph(80U,1U,{},"seam-test",9U,17U,true);
  // Defaults pair with a graph that declares the channel: admission succeeds
  // and the measured values reach the model contract.
  const auto bundle=fixture(conditioned,vocoderGraph(),4U,48000U,vocabulary,defaults);
  CHECK(bundle);
  const auto admitted=AdmittedNeuralBundle::admit(bundle.value(),65536U,10);
  CHECK(admitted);
  if (admitted) {
    const auto& model=admitted.value().metadata().model;
    CHECK(admitted.value().metadata().configurationVersion==4U);
    CHECK(model.breathinessDefaults.size()==2U);
    CHECK(std::abs(model.breathinessDefaults.at("z")-0.4F)<1e-6F);
    CHECK(model.breathinessDefaults.at("a")==0.0F);
  }
  // The same defaults against an unconditioned graph are refused: the bundle
  // would send a control the worker cannot deliver.
  const auto mismatched=fixture(acousticGraph(),vocoderGraph(),4U,48000U,vocabulary,defaults);
  CHECK(mismatched);
  CHECK(!AdmittedNeuralBundle::admit(mismatched.value(),65536U,10));
  // A default naming a symbol outside the vocabulary is refused at inspection.
  const auto unknown=fixture(conditioned,vocoderGraph(),4U,48000U,vocabulary,
      R"({"breathiness":{"q":0.4}})");
  CHECK(unknown);
  CHECK(!AdmittedNeuralBundle::admit(unknown.value(),65536U,10));
  // Out-of-range and malformed defaults fail closed validation.
  for (const auto* bad:{R"({"breathiness":{"z":1.5}})",R"({"breathiness":{"z":-0.1}})",
       R"({"breathiness":{"z":"high"}})",R"({"energy":{"z":0.4}})",R"({"breathiness":[]})",
       R"({"breathiness":{"z":0.4},"extra":{}})"}) {
    const auto invalid=fixture(conditioned,vocoderGraph(),4U,48000U,vocabulary,bad);
    CHECK(invalid);
    CHECK(!AdmittedNeuralBundle::admit(invalid.value(),65536U,10));
  }
  // A conditioned graph without defaults stays admissible: the channel is
  // optional and renders with zeros when nothing supplies it.
  const auto bare=fixture(conditioned,vocoderGraph(),4U,48000U,vocabulary,R"({"breathiness":{}})");
  CHECK(bare);
  CHECK(AdmittedNeuralBundle::admit(bare.value(),65536U,10));
}

TEST_CASE("graph inspection reports what a graph file declares") {
  using namespace seam::neural_synthesis;
  const auto acoustic=acousticGraph();
  const auto graph=inspectNeuralGraph(std::as_bytes(std::span{acoustic.data(),acoustic.size()}));
  CHECK(graph);
  if (!graph) return;
  CHECK(graph.value().irVersion==9U);
  CHECK(graph.value().opset==17U);
  CHECK(graph.value().producer=="seam-test");
  // The operator set is what the file uses, unique and sorted, not what the file claims elsewhere.
  CHECK(graph.value().operators==std::vector<std::string>{"MatMul"});
  CHECK(graph.value().nodeCount==1U);
  CHECK(graph.value().initializerCount==1U);
  CHECK(graph.value().initializerBytes==4ULL*80ULL);
  CHECK(graph.value().inputs.size()==1U);
  CHECK(graph.value().inputs.front().name=="phones");
  CHECK(graph.value().inputs.front().elementType==7U);
  CHECK(graph.value().outputs.size()==1U);
  const auto* mel=graph.value().findOutput("mel");
  CHECK(mel!=nullptr);
  if (!mel) return;
  // A symbolic time axis is recorded as unbounded while the rank and the feature axis stay known.
  CHECK((mel->dimensions==std::vector<std::int64_t>{1,-1,80}));
  CHECK(mel->dynamicDimensions()==1U);
  CHECK(isFloatTensorElementType(mel->elementType));
  CHECK(!isFloatTensorElementType(7U));
  CHECK(tensorElementTypeName(mel->elementType)=="float32");
  CHECK(tensorElementTypeName(7U)=="int64");
  CHECK(isAdmittedOperator("ConvTranspose"));
  // Owned DiffSinger exports require inspected If/Loop bodies; Scan remains outside the profile.
  CHECK(isAdmittedOperator("Loop"));
  CHECK(isAdmittedOperator("If"));
  CHECK(!isAdmittedOperator("Scan"));
  CHECK(!isAdmittedOperator("SeamCustomOp"));
}

TEST_CASE("graph inspection recursively validates control-flow bodies and lexical captures") {
  using namespace seam::neural_synthesis;
  using namespace seam::test::onnx;
  const auto branch=[&](std::string_view op) {
    return onnxGraph({}, {onnxValueInfo("branch_output",1U,{"1","T","80"})},
                     {onnxNode(op,{"mel"},{"branch_output"})});
  };
  const auto makeAttribute=[&](std::string_view name,std::string_view graph) {
    std::string attribute;
    protoBytesField(attribute,1U,name);
    protoBytesField(attribute,6U,graph);
    return attribute;
  };
  const auto makeIf=[&](std::string_view bodyOperator) {
    std::string node;
    protoBytesField(node,1U,"condition");
    protoBytesField(node,2U,"audio");
    protoBytesField(node,4U,"If");
    protoBytesField(node,5U,makeAttribute("then_branch",branch(bodyOperator)));
    protoBytesField(node,5U,makeAttribute("else_branch",branch("Identity")));
    return onnxModel(onnxGraph(
        {onnxValueInfo("mel",1U,{"1","T","80"}),onnxValueInfo("condition",9U,{})},
        {onnxValueInfo("audio",1U,{"1","T","80"})},{node}));
  };
  const auto valid=makeIf("Identity");
  const auto contract=inspectNeuralGraph(std::as_bytes(std::span{valid.data(),valid.size()}));
  CHECK(contract);
  if (!contract) return;
  CHECK((contract.value().operators==std::vector<std::string>{"Identity","If"}));
  CHECK(contract.value().nodeCount==3U);
  CHECK(contract.value().captures.empty());
  CHECK((contract.value().nodes.front().inputs==std::vector<std::string>{"condition","mel"}));
  const auto invalid=makeIf("SeamCustomOp");
  CHECK(!inspectNeuralGraph(std::as_bytes(std::span{invalid.data(),invalid.size()})));
}

TEST_CASE("graph inspection refuses bytes no admitted export family produces") {
  using namespace seam::neural_synthesis;
  using namespace seam::test::onnx;
  const auto inspect=[&](const std::string& bytes) {
    return inspectNeuralGraph(std::as_bytes(std::span{bytes.data(),bytes.size()}));
  };
  // A model whose meta declares a training record, a function or any other field is refused rather
  // than skipped, so a second representation cannot ride along unnoticed.
  auto unknownModel=onnxModel(onnxGraph({onnxValueInfo("mel",1U,{"1","T","80"})},
      {onnxValueInfo("audio",1U,{"1","samples"})},{onnxNode("Identity",{"mel"},{"audio"})}));
  protoVarintField(unknownModel,20U,1U);
  CHECK(!inspect(unknownModel));
  // An explicit custom operator domain is refused by name, and so is an operator outside the set.
  const auto customDomain=onnxModel(onnxGraph({onnxValueInfo("mel",1U,{"1","T","80"})},
      {onnxValueInfo("audio",1U,{"1","samples"})},
      {onnxNode("SeamLookup",{"mel"},{"audio"},"com.example.case")}));
  CHECK(!inspect(customDomain));
  const auto unknownOperator=onnxModel(onnxGraph({onnxValueInfo("mel",1U,{"1","T","80"})},
      {onnxValueInfo("audio",1U,{"1","samples"})},
      {onnxNode("FancyNeuralOp",{"mel"},{"audio"})}));
  CHECK(!inspect(unknownOperator));
  // A malformed subgraph payload is refused rather than skipped.
  std::string attribute;
  protoBytesField(attribute,1U,"body");
  protoBytesField(attribute,6U,"subgraph bytes");
  std::string node;
  protoBytesField(node,1U,"mel");
  protoBytesField(node,2U,"audio");
  protoBytesField(node,4U,"Identity");
  protoBytesField(node,5U,attribute);
  const auto subgraph=onnxModel(onnxGraph({onnxValueInfo("mel",1U,{"1","T","80"})},
      {onnxValueInfo("audio",1U,{"1","samples"})},{node}));
  CHECK(!inspect(subgraph));
  // An initializer that reaches outside the file for its bytes is refused outright.
  std::string external;
  protoVarintField(external,1U,80U);
  protoVarintField(external,2U,1U);
  protoBytesField(external,8U,"weights");
  protoVarintField(external,14U,1U);
  const auto externalData=onnxModel(onnxGraph({onnxValueInfo("mel",1U,{"1","T","80"})},
      {onnxValueInfo("audio",1U,{"1","samples"})},
      {onnxNode("Identity",{"mel"},{"audio"})},{external}));
  CHECK(!inspect(externalData));
  // Revisions outside the admitted window, and a second operator set, are refused.
  CHECK(!inspect(onnxAcousticGraph(80U,1U,{},"seam-test",9U,12U)));
  CHECK(!inspect(onnxAcousticGraph(80U,1U,{},"seam-test",9U,22U)));
  CHECK(!inspect(onnxAcousticGraph(80U,1U,{},"seam-test",11U,17U)));
  auto twoOpsets=acousticGraph();
  std::string extraOpset;
  protoVarintField(extraOpset,2U,17U);
  protoBytesField(twoOpsets,8U,extraOpset);
  CHECK(!inspect(twoOpsets));
  // Two tensors may not share one name, and a graph may not declare nothing.
  const auto duplicate=onnxModel(onnxGraph({onnxValueInfo("mel",1U,{"1","T","80"}),
      onnxValueInfo("mel",1U,{"1","T","80"})},{onnxValueInfo("audio",1U,{"1","samples"})},
      {onnxNode("Identity",{"mel"},{"audio"})}));
  CHECK(!inspect(duplicate));
  CHECK(!inspect(onnxModel(onnxGraph({},{onnxValueInfo("audio",1U,{"1","samples"})},{onnxNode("Identity",{},{})}))));
  // Truncated, non-protobuf and oversized payloads are refused before anything is bound.
  auto truncated=acousticGraph();
  truncated.resize(truncated.size()-1U);
  CHECK(!inspect(truncated));
  CHECK(!inspect(std::string{"\xff\xff\xfe not a graph"}));
  const auto oversized=acousticGraph();
  CHECK(!inspectNeuralGraph(std::as_bytes(std::span{oversized.data(),oversized.size()}),
      GraphInspectionLimits{.maximumBytes=16U}));
  GraphInspectionLimits tiny;
  tiny.maximumInitializerBytes=16U;
  CHECK(!inspectNeuralGraph(std::as_bytes(std::span{oversized.data(),oversized.size()}),tiny));
}

TEST_CASE("admission refuses a pair of individually valid graphs that disagree") {
  using namespace seam::neural_synthesis;
  const auto refused=[&](const std::string& acoustic,const std::string& vocoder) {
    const auto bundle=fixture(acoustic,vocoder,3U,48000U);
    CHECK(bundle);
    return !bundle || !AdmittedNeuralBundle::admit(bundle.value(),65536U,10);
  };
  // The vocoder consumes a different number of mel bins than the acoustic graph emits.
  CHECK(refused(seam::test::onnx::onnxAcousticGraph(80U),seam::test::onnx::onnxVocoderGraph(64U,1U,"audio")));
  // The acoustic graph leaves the feature axis unbounded, so nothing proves what it would emit.
  CHECK(refused(seam::test::onnx::onnxAcousticGraph(80U,1U,"?"),seam::test::onnx::onnxVocoderGraph(80U,1U,"audio")));
  // The two graphs disagree about the mel element type.
  CHECK(refused(seam::test::onnx::onnxAcousticGraph(80U,1U,{}),seam::test::onnx::onnxVocoderGraph(80U,10U,"audio")));
  // The configured vocoder output name is not a tensor the vocoder graph declares.
  CHECK(refused(acousticGraph(),seam::test::onnx::onnxVocoderGraph(80U,1U,"waveform")));
  // A vocoder that declares two output channels is refused; the model contract is mono.
  CHECK(refused(acousticGraph(),seam::test::onnx::onnxVocoderGraph(80U,1U,"audio","2")));
  // The pair the configuration describes is admitted, so the refusals above are the difference.
  CHECK(!refused(acousticGraph(),vocoderGraph()));
}

TEST_CASE("graph admission validates and binds declared conditioning controls") {
  using namespace seam::neural_synthesis;
  const auto inspect = [](const std::string& graph) {
    return inspectNeuralGraph(std::as_bytes(std::span{graph.data(), graph.size()}), {});
  };
  // Valid breathiness input [1, T] float32 is admitted and declared
  const auto valid = seam::test::onnx::onnxAcousticGraph(80U, 1U, {}, "seam-test", 9U, 17U, true, 1U, {"1", "T"});
  const auto inspected = inspect(valid);
  CHECK(inspected.hasValue());
  CHECK(inspected.value().supportsConditioningControl("breathiness"));
  const auto* control = inspected.value().findConditioningControl("breathiness");
  CHECK(control != nullptr);
  CHECK(control->name == "breathiness");
  CHECK(control->type == "float32");
  CHECK(control->unit == "normalized-periodic-aperiodic-balance");
  CHECK(control->minimumValue == 0.0F);
  CHECK(control->maximumValue == 1.0F);
  CHECK(control->defaultValue == 0.0F);

  // Invalid element type: int64 (7U) is refused
  const auto badType = seam::test::onnx::onnxAcousticGraph(80U, 1U, {}, "seam-test", 9U, 17U, true, 7U, {"1", "T"});
  const auto badTypeResult = inspect(badType);
  CHECK(!badTypeResult.hasValue());
  CHECK(badTypeResult.error().code == seam::core::ErrorCode::Unsupported);
  CHECK(badTypeResult.error().message.find("must be float32") != std::string::npos);

  // Invalid rank: 3D [1, T, 80] is refused
  const auto badRank = seam::test::onnx::onnxAcousticGraph(80U, 1U, {}, "seam-test", 9U, 17U, true, 1U, {"1", "T", "80"});
  const auto badRankResult = inspect(badRank);
  CHECK(!badRankResult.hasValue());
  CHECK(badRankResult.error().code == seam::core::ErrorCode::Unsupported);
  CHECK(badRankResult.error().message.find("dynamic 2D") != std::string::npos);

  // Invalid batch: batch 2 is refused
  const auto badBatch = seam::test::onnx::onnxAcousticGraph(80U, 1U, {}, "seam-test", 9U, 17U, true, 1U, {"2", "T"});
  const auto badBatchResult = inspect(badBatch);
  CHECK(!badBatchResult.hasValue());
  CHECK(badBatchResult.error().code == seam::core::ErrorCode::Unsupported);
  CHECK(badBatchResult.error().message.find("dynamic 2D") != std::string::npos);

  const auto fixedTime = inspect(seam::test::onnx::onnxAcousticGraph(
      80U, 1U, {}, "seam-test", 9U, 17U, true, 1U, {"1", "32"}));
  CHECK(!fixedTime);
  CHECK(fixedTime.error().message.find("dynamic 2D") != std::string::npos);

  const auto wrongUnit = inspect(seam::test::onnx::onnxAcousticGraph(
      80U, 1U, {}, "seam-test", 9U, 17U, true, 1U, {"1", "T"}, "bipolar"));
  CHECK(!wrongUnit);
  CHECK(wrongUnit.error().message.find("metadata") != std::string::npos);

  const auto ignored = inspect(seam::test::onnx::onnxAcousticGraph(
      80U, 1U, {}, "seam-test", 9U, 17U, true, 1U, {"1", "T"},
      "normalized-periodic-aperiodic-balance", false));
  CHECK(!ignored);
  CHECK(ignored.error().message.find("does not affect") != std::string::npos);
}
