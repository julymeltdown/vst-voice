#include "test_framework.hpp"
#include "seam/neural_synthesis/bundle_metadata.hpp"
#include "seam/formats/json_value.hpp"
#include "test_support.hpp"
#include "seam/core/file_io.hpp"
#include "seam/platform/application_paths.hpp"

#include "seam/neural_synthesis/worker_protocol.hpp"
#include "seam/neural_synthesis/model_contract.hpp"
#include "seam/neural_synthesis/neural_phrase_backend.hpp"
#include "seam/neural_synthesis/deployment_descriptor.hpp"
#include "seam/neural_synthesis/diffsinger_inputs.hpp"
#include "seam/core/sha256.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
#include <thread>
#include <fstream>
#if !defined(_WIN32)
#include <cerrno>
#include <signal.h>
#endif

namespace {

seam::neural_synthesis::NeuralRequest request() {
  return seam::neural_synthesis::NeuralRequest{
      .requestId = 42U,
      .modelId = "seam.test.neural",
      .modelVersion = "1.0.0",
      .modelContentHash = std::string(64U, 'a'),
      .pronunciationHash = std::string(64U, 'b'),
      .sampleRate = 48000U,
      .channels = 1U,
      .frameCount = 4U,
      .f0Hz = {220.0F, 220.5F, 221.0F, 221.5F},
      .dynamics = {0.5F, 0.6F, 0.7F, 0.8F},
  };
}

}  // namespace

TEST_CASE("neural vocabulary loads exact model-bound bytes and preserves explicit token order") {
  using namespace seam::neural_synthesis;
  const std::string json=R"({"formatId":"com.project-seam.neural-vocabulary","schemaVersion":1,"tokens":["SP","z","aa1","あ"]})";
  ModelContract model{.modelId="vocabulary-test",.modelVersion="1",.modelContentHash=std::string(64U,'a'),
      .vocabularyHash=seam::core::sha256Hex(json)};
  const auto vocabulary=NeuralVocabulary::decode(json,model); CHECK(vocabulary);
  CHECK(vocabulary.value().size()==4U); CHECK(vocabulary.value().contentHash()==model.vocabularyHash);
  CHECK(vocabulary.value().tokenId("SP").value()==0U); CHECK(vocabulary.value().tokenId("z").value()==1U);
  CHECK(vocabulary.value().tokenId("aa1").value()==2U); CHECK(vocabulary.value().tokenId("あ").value()==3U);
  CHECK(!vocabulary.value().tokenId("a")); CHECK(!vocabulary.value().tokenId("Z"));
  CHECK(!NeuralVocabulary::decode(json+" ",model));
  for (const auto* tokens:{"[]","[\"z\",\"z\"]","[\"\"]","[1]","[\"z\\n\"]"}) {
    const auto bad=std::string{"{\"formatId\":\"com.project-seam.neural-vocabulary\",\"schemaVersion\":1,\"tokens\":"}+tokens+"}";
    model.vocabularyHash=seam::core::sha256Hex(bad); CHECK(!NeuralVocabulary::decode(bad,model));
  }
  const std::string oversized(4U*1024U*1024U+1U,' ');
  model.vocabularyHash=seam::core::sha256Hex(oversized); CHECK(!NeuralVocabulary::decode(oversized,model));
}

TEST_CASE("neural score conditioning uses compiled timing and explicit silence without phone substitution") {
  using namespace seam;
  using namespace seam::neural_synthesis;
  const std::string json=R"({"formatId":"com.project-seam.neural-vocabulary","schemaVersion":1,"tokens":["SP","z","a"]})";
  ModelContract model{.modelId="score-test",.modelVersion="1",.modelContentHash=std::string(64U,'a'),.vocabularyHash=core::sha256Hex(json)};
  const auto vocabulary=NeuralVocabulary::decode(json,model); CHECK(vocabulary);
  domain::Project project{domain::ProjectId{1U},"Neural conditioning"};
  domain::VocalRegion region{.id=domain::RegionId{3U},.durationTick=time::Tick{480},
      .lyrics={{domain::LyricTokenId{4U},U"za",domain::Language::English}},
      .notes={{.id=domain::NoteId{5U},.durationTick=time::Tick{480},.midiKey=69U,.lyricTokenId=domain::LyricTokenId{4U}}}};
  std::vector<domain::PhonemeToken> phones{
      {.key={domain::NoteId{5U},0U},.symbol="z",.role=domain::PhonemeRole::Onset,.voiced=true,.timing={.startOffset=10000}},
      {.key={domain::NoteId{5U},1U},.symbol="a",.role=domain::PhonemeRole::Nucleus,.voiced=true,.timing={.startOffset=100000}}};
  const auto timing=synthesis::compilePhonemeTimingPlan(project,region,phones,48000U); CHECK(timing);
  const auto conditioned=vocabulary.value().conditionScore(phones,timing.value(),0,12010,"SP"); CHECK(conditioned);
  const std::vector<NeuralPhonemeSpan> expected{{0U,0U,480U},{1U,480U,4800U},{2U,4800U,12000U},{0U,12000U,12010U}};
  CHECK(conditioned.value().spans==expected); CHECK(conditioned.value().vocabularyHash==model.vocabularyHash);
  auto unknown=phones; unknown[0].symbol="v"; CHECK(!vocabulary.value().conditionScore(unknown,timing.value(),0,12010,"SP"));
  auto unresolved=timing.value(); unresolved[0].explicitStartFrame.reset();
  CHECK(!vocabulary.value().conditionScore(phones,unresolved,0,12010,"SP"));
  CHECK(!vocabulary.value().conditionScore(phones,timing.value(),500,12010,"SP"));
  CHECK(!vocabulary.value().conditionScore(phones,timing.value(),0,12010,"missing"));
  auto overlap=timing.value(); overlap[0].endExplicit=true; overlap[0].endFrame=5000;
  CHECK(!vocabulary.value().conditionScore(phones,overlap,0,12010,"SP"));
  auto lateOnset=timing.value(); lateOnset[0].explicitStartFrame=6000; lateOnset[0].endExplicit=true;
  lateOnset[0].endFrame=7000; lateOnset[1].endFrame=6000;
  CHECK(!vocabulary.value().conditionScore(phones,lateOnset,0,12010,"SP"));
  auto earlyCodaPhones=phones; earlyCodaPhones[0].role=domain::PhonemeRole::Coda;
  auto earlyCoda=timing.value(); earlyCoda[0].endFrame=2000; earlyCoda[0].endExplicit=true;
  CHECK(!vocabulary.value().conditionScore(earlyCodaPhones,earlyCoda,0,12010,"SP"));
  std::vector<domain::PhonemeToken> codaPhones{
      {.key={domain::NoteId{5U},0U},.symbol="a",.role=domain::PhonemeRole::Nucleus,.voiced=true,.timing={.startOffset=0,.endOffset=150000}},
      {.key={domain::NoteId{5U},1U},.symbol="z",.role=domain::PhonemeRole::Coda,.voiced=true,.timing={.startOffset=150000}}};
  const auto codaTiming=synthesis::compilePhonemeTimingPlan(project,region,codaPhones,48000U); CHECK(codaTiming);
  const auto codaConditioning=vocabulary.value().conditionScore(codaPhones,codaTiming.value(),0,12000,"SP"); CHECK(codaConditioning);
  const std::vector<NeuralPhonemeSpan> expectedCoda{{2U,0U,7200U},{1U,7200U,12000U}};
  CHECK(codaConditioning.value().spans==expectedCoda);
  const auto performance=synthesis::compileScorePerformance(project,region,48000U,phones); CHECK(performance);
  const auto prepared=prepareNeuralScoreRequest(101U,model,vocabulary.value(),performance.value(),phones,std::string(64U,'b'),0,12010,"SP"); CHECK(prepared);
  CHECK(prepared.value().conditioning->spans==expected);
  CHECK(prepared.value().f0Hz[0]==0.0F); CHECK(prepared.value().dynamics[0]==0.0F);
  CHECK(std::abs(prepared.value().f0Hz[500]-440.0F)<0.001F);
  CHECK(prepared.value().dynamics[500]==performance.value().at(500).dynamicsGain*performance.value().at(500).articulationGain);
  CHECK(prepared.value().f0Hz[12000]==0.0F); CHECK(prepared.value().dynamics[12000]==0.0F);
  const auto wire=encodeRequest(prepared.value()); CHECK(wire); CHECK(decodeRequest(wire.value()).value()==prepared.value());
  auto unvoiced=phones; unvoiced[0].voiced=false;
  const auto unvoicedPerformance=synthesis::compileScorePerformance(project,region,48000U,unvoiced); CHECK(unvoicedPerformance);
  const auto unvoicedRequest=prepareNeuralScoreRequest(102U,model,vocabulary.value(),unvoicedPerformance.value(),unvoiced,std::string(64U,'b'),0,12010,"SP"); CHECK(unvoicedRequest);
  CHECK(unvoicedRequest.value().f0Hz[500]==0.0F); CHECK(unvoicedRequest.value().dynamics[500]>0.0F);
  CHECK(unvoicedRequest.value().f0Hz[5000]>0.0F);
  auto delayed=region; delayed.durationTick=time::Tick{1920}; delayed.notes[0].startTick=time::Tick{480};
  std::vector<domain::PhonemeToken> extendedPhones{
      {.key={domain::NoteId{5U},0U},.symbol="z",.role=domain::PhonemeRole::Onset,.voiced=true,.timing={.startOffset=-100000}},
      {.key={domain::NoteId{5U},1U},.symbol="a",.role=domain::PhonemeRole::Nucleus,.voiced=true,.timing={.startOffset=0,.endOffset=200000}},
      {.key={domain::NoteId{5U},2U},.symbol="z",.role=domain::PhonemeRole::Coda,.voiced=true,.timing={.startOffset=200000,.endOffset=300000}}};
  const auto extendedPerformance=synthesis::compileScorePerformance(project,delayed,48000U,extendedPhones); CHECK(extendedPerformance);
  const auto extendedRequest=prepareNeuralScoreRequest(104U,model,vocabulary.value(),extendedPerformance.value(),extendedPhones,std::string(64U,'b'),0,26410,"SP"); CHECK(extendedRequest);
  CHECK(extendedRequest.value().f0Hz[0]==0.0F); CHECK(extendedRequest.value().dynamics[0]==0.0F);
  CHECK(std::abs(extendedRequest.value().f0Hz[8000]-440.0F)<0.001F);
  CHECK(extendedRequest.value().dynamics[8000]==extendedPerformance.value().at(12000).dynamicsGain);
  CHECK(std::abs(extendedRequest.value().f0Hz[25000]-440.0F)<0.001F);
  CHECK(extendedRequest.value().dynamics[25000]==extendedPerformance.value().at(23999).dynamicsGain);
  CHECK(extendedRequest.value().f0Hz[26400]==0.0F); CHECK(extendedRequest.value().dynamics[26400]==0.0F);
  std::stop_source stop; stop.request_stop();
  CHECK(!prepareNeuralScoreRequest(103U,model,vocabulary.value(),performance.value(),phones,std::string(64U,'b'),0,12010,"SP",{},stop.get_token()));
#if defined(SEAM_NEURAL_WORKER_PROBE)
  const auto helperHash=core::sha256File(SEAM_NEURAL_WORKER_PROBE); CHECK(helperHash);
  const auto output=runNeuralWorker(prepared.value(),model,NeuralWorkerRunOptions{.helper=SEAM_NEURAL_WORKER_PROBE,.helperContentHash=helperHash.value(),.vocabulary=vocabulary.value()});
  CHECK(output); CHECK(output.value().response.frameCount==12010U); CHECK(!output.value().response.requestContentHash.empty());
#endif
}

TEST_CASE("neural vocabulary aliases preserve trained IDs without increasing embedding size") {
  using namespace seam; using namespace seam::neural_synthesis;
  using J=formats::JsonValue;
  const auto decode=[](const J::Object& aliases) {
    const auto json=formats::stringifyJson(J{J::Object{{"formatId","com.project-seam.neural-vocabulary"},
        {"schemaVersion",std::int64_t{2}},{"tokens",J::Array{J{"<PAD>"},J{"SP"},J{"ja/a"}}},{"aliases",aliases}}});
    ModelContract model{.modelId="aliases",.modelVersion="1",.modelContentHash=std::string(64,'a'),.vocabularyHash=core::sha256Hex(json)};
    return NeuralVocabulary::decode(json,model);
  };
  const auto vocabulary=decode({{"en/aa",std::int64_t{2}},{"ko/a",std::int64_t{2}}}); CHECK(vocabulary);
  CHECK(vocabulary.value().size()==3U);
  CHECK(vocabulary.value().tokenId("ja/a").value()==2U);
  CHECK(vocabulary.value().tokenId("en/aa").value()==2U);
  CHECK(vocabulary.value().tokenId("ko/a").value()==2U);
  CHECK(!vocabulary.value().tokenId("a"));
  for (const auto& aliases:std::vector<J::Object>{{{"alias",std::int64_t{0}}},{{"alias",std::int64_t{3}}},
      {{"alias",std::int64_t{-1}}},{{"alias",2.5}},{{"ja/a",std::int64_t{2}}},{{"bad\n",std::int64_t{2}}}})
    CHECK(!decode(aliases));
}

TEST_CASE("native DiffSinger vocabulary conversion preserves source IDs and aliases") {
  using namespace seam; using namespace seam::neural_synthesis;
  const auto converted=convertDiffSingerVocabulary(R"({"SP":1,"ja/a":3,"ko/a":3,"AP":2,"en/aa":3})"); CHECK(converted);
  const auto reordered=convertDiffSingerVocabulary(R"({"en/aa":3,"AP":2,"ko/a":3,"ja/a":3,"SP":1})"); CHECK(reordered);
  CHECK(converted.value()==reordered.value());
  ModelContract model{.modelId="import",.modelVersion="1",.modelContentHash=std::string(64,'a'),
      .vocabularyHash=core::sha256Hex(converted.value())};
  const auto vocabulary=NeuralVocabulary::decode(converted.value(),model); CHECK(vocabulary);
  CHECK(vocabulary.value().size()==4U);
  CHECK(vocabulary.value().tokenId("SP").value()==1U);
  CHECK(vocabulary.value().tokenId("AP").value()==2U);
  for (const auto* phone:{"ja/a","ko/a","en/aa"}) CHECK(vocabulary.value().tokenId(phone).value()==3U);
  for (const auto* invalid:{R"({"a":2})",R"({"a":0})",R"({"a":-1})",R"({"a":true})",R"({"a":1.5})",
      R"({"a":65536})",R"({"<PAD>":1})",R"({"bad\n":1})",R"({"a":1,"a":2})","{}","[]"})
    CHECK(!convertDiffSingerVocabulary(invalid));
}

TEST_CASE("neural phonetic conditioning requires complete explicit vocabulary bound spans") {
  using namespace seam::neural_synthesis;
  PhoneticConditioning value{std::string(64U,'c'),{{1U,0U,40U},{0U,40U,50U},{2U,50U,100U}}};
  CHECK(value.validate(100U,3U));
  for (unsigned scenario=0U;scenario<8U;++scenario) {
    auto bad=value;
    if (scenario==0U) bad.vocabularyHash="invalid";
    if (scenario==1U) bad.spans[0].tokenId=3U;
    if (scenario==2U) bad.spans[1].startFrame=39U;
    if (scenario==3U) bad.spans[1].startFrame=41U;
    if (scenario==4U) bad.spans[1].endFrame=40U;
    if (scenario==5U) bad.spans.back().endFrame=99U;
    if (scenario==6U) bad.spans.back().endFrame=101U;
    if (scenario==7U) bad.spans.resize(4097U);
    CHECK(!bad.validate(100U,3U));
  }
  CHECK(!value.validate(100U,0U)); CHECK(!value.validate(100U,65537U));
  CHECK(!value.validate(0U,3U));
  ModelContract model{.modelId="conditioning-test",.modelVersion="1",.modelContentHash=std::string(64U,'a'),
      .vocabularyHash=value.vocabularyHash};
  CHECK(model.validatePhoneticConditioning(value,100U,3U));
  model.vocabularyHash=std::string(64U,'d'); CHECK(!model.validatePhoneticConditioning(value,100U,3U));
  model.vocabularyHash=value.vocabularyHash; model.maximumFrames=99U;
  CHECK(!model.validatePhoneticConditioning(value,100U,3U));
}

TEST_CASE("neural request v2 carries bounded phoneme spans without silent v1 downgrade") {
  using namespace seam::neural_synthesis;
  auto input=request(); input.vocabularySize=3U;
  input.conditioning=PhoneticConditioning{std::string(64U,'c'),{{1U,0U,2U},{2U,2U,4U}}};
  const auto encoded=encodeRequest(input); CHECK(encoded);
  const auto decoded=decodeRequest(encoded.value()); CHECK(decoded); CHECK(decoded.value()==input);
  const std::string wire{reinterpret_cast<const char*>(encoded.value().data()),encoded.value().size()};
  const auto downgrade=wire.find("seam-neural-request-v2"); CHECK(downgrade!=std::string::npos);
  auto bad=encoded.value(); bad[downgrade+20U]=std::byte{'1'}; CHECK(!decodeRequest(bad));
  const auto span=wire.find("\"endFrame\":2"); CHECK(span!=std::string::npos);
  bad=encoded.value(); bad[span+11U]=std::byte{'1'}; CHECK(!decodeRequest(bad));
  WorkerProtocolLimits tiny; tiny.maximumMetadataBytes=32U;
  CHECK(!encodeRequest(input,tiny)); CHECK(!decodeRequest(encoded.value(),tiny));
  ModelContract model{.modelId=input.modelId,.modelVersion=input.modelVersion,.modelContentHash=input.modelContentHash,
      .vocabularyHash=input.conditioning->vocabularyHash};
  CHECK(model.validateRequest(input)); model.vocabularyHash=std::string(64U,'d'); CHECK(!model.validateRequest(input));
  input.conditioning.reset(); CHECK(!encodeRequest(input)); // No stray vocabulary declaration in v1.
}

TEST_CASE("neural bundle v3 preserves explicit identity and rejects legacy downgrade") {
  using namespace seam::neural_synthesis;
  auto input=request(); input.vocabularySize=3U;
  input.conditioning=PhoneticConditioning{std::string(64U,'c'),{{1U,0U,4U}}};
  input.bundleContentHash=input.modelContentHash;
  const auto encoded=encodeRequest(input); CHECK(encoded);
  CHECK(decodeRequest(encoded.value()).value()==input);
  const std::string wire{reinterpret_cast<const char*>(encoded.value().data()),encoded.value().size()};
  const auto kind=wire.find("seam-neural-request-v3"); CHECK(kind!=std::string::npos);
  auto bad=encoded.value(); bad[kind+20U]=std::byte{'2'}; CHECK(!decodeRequest(bad));
  ModelContract model{.modelId=input.modelId,.modelVersion=input.modelVersion,
      .modelContentHash=input.modelContentHash,.vocabularyHash=input.conditioning->vocabularyHash};
  CHECK(!runNeuralWorker(input,model,{}));
  const std::vector<float> padded(256U,0.25F);
  const auto response=finalizeDiffSingerResponse(input,model,padded,"fixture"); CHECK(response);
  CHECK(response.value().bundleContentHash==input.bundleContentHash);
  const auto output=encodeResponse(response.value()); CHECK(output);
  CHECK(decodeResponse(output.value()).value()==response.value());
  const std::string responseWire{reinterpret_cast<const char*>(output.value().data()),output.value().size()};
  const auto responseKind=responseWire.find("seam-neural-response-v3"); CHECK(responseKind!=std::string::npos);
  bad=output.value(); bad[responseKind+21U]=std::byte{'2'}; CHECK(!decodeResponse(bad));
  auto invalid=response.value(); invalid.requestContentHash.clear(); CHECK(!encodeResponse(invalid));
  invalid=response.value(); invalid.bundleContentHash=std::string(64U,'d'); CHECK(!encodeResponse(invalid));
  input.bundleContentHash=std::string(64U,'d'); CHECK(!encodeRequest(input));
  input.bundleContentHash=input.modelContentHash; input.conditioning.reset(); CHECK(!encodeRequest(input));
}

TEST_CASE("neural worker request and response frames round-trip with bounded metadata") {
  using namespace seam::neural_synthesis;
  const auto encoded = encodeRequest(request());
  CHECK(encoded);
  CHECK(encoded.value().size() > 20U);
  const auto decoded = decodeRequest(encoded.value());
  CHECK(decoded);
  CHECK(decoded.value() == request());

  NeuralResponse response{
      .requestId = decoded.value().requestId,
      .backendId = "seam.test.neural.cpu",
      .modelContentHash = decoded.value().modelContentHash,
      .sampleRate = decoded.value().sampleRate,
      .channels = 2U,
      .frameCount = decoded.value().frameCount,
      .pcm = {0.0F, 0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F, 0.7F},
  };
  const auto encodedResponse = encodeResponse(response);
  CHECK(encodedResponse);
  const auto decodedResponse = decodeResponse(encodedResponse.value());
  CHECK(decodedResponse);
  CHECK(decodedResponse.value() == response);
  response.requestContentHash=seam::core::sha256Hex(std::span<const std::byte>{encoded.value()});
  const auto bound=encodeResponse(response); CHECK(bound);
  CHECK(decodeResponse(bound.value()).value()==response);
  response.requestContentHash="invalid"; CHECK(!encodeResponse(response));
}

TEST_CASE("neural worker protocol rejects stale, malformed and non-finite frames before allocation") {
  using namespace seam::neural_synthesis;
  auto encoded = encodeRequest(request());
  CHECK(encoded);
  auto malformed = encoded.value();
  malformed[0] = std::byte{'X'};
  CHECK(!decodeRequest(malformed));

  malformed = encoded.value();
  malformed[8] = std::byte{0xff};
  malformed[9] = std::byte{0xff};
  malformed[10] = std::byte{0xff};
  malformed[11] = std::byte{0x7f};
  CHECK(!decodeRequest(malformed));

  auto invalid = request();
  invalid.f0Hz.front() = std::numeric_limits<float>::infinity();
  CHECK(!encodeRequest(invalid));
  invalid = request();
  invalid.dynamics.pop_back();
  CHECK(!encodeRequest(invalid));

  WorkerProtocolLimits limits;
  limits.maximumFrameBytes = 32U;
  CHECK(!encodeRequest(request(), limits));
  limits = {};
  limits.maximumMetadataBytes = 8U;
  CHECK(!decodeRequest(encoded.value(), limits));
}

TEST_CASE("neural worker protocol enforces response shape, identity and PCM range") {
  using namespace seam::neural_synthesis;
  auto response = NeuralResponse{
      .requestId = 1U,
      .backendId = "backend",
      .modelContentHash = std::string(64U, 'c'),
      .sampleRate = 48000U,
      .channels = 1U,
      .frameCount = 2U,
      .pcm = {0.0F, 1.0F},
  };
  CHECK(response.validate());
  response.pcm.front() = 1.1F;
  CHECK(!response.validate());
  response.pcm = {0.0F};
  CHECK(!response.validate());
  response.pcm = {0.0F, 0.0F};
  response.modelContentHash = "not-a-digest";
  CHECK(!response.validate());
}

TEST_CASE("neural model contract binds model, vocabulary, sample rate and output shape") {
  using namespace seam::neural_synthesis;
  ModelContract contract{
      .modelId = "seam.test.neural",
      .modelVersion = "1.0.0",
      .modelContentHash = std::string(64U, 'a'),
      .vocabularyHash = std::string(64U, 'b'),
      .sampleRate = 48000U,
      .hopSize = 256U,
      .outputChannels = 1U,
      .maximumFrames = 16U,
      .maximumModelBytes = 1024U,
  };
  CHECK(contract.validate());
  auto matching = request();
  CHECK(contract.validateRequest(matching));
  matching.sampleRate = 44100U;
  CHECK(!contract.validateRequest(matching));
  matching = request();
  matching.modelContentHash.front() = 'c';
  CHECK(!contract.validateRequest(matching));
  contract.maximumFrames = 2U;
  CHECK(!contract.validateRequest(request()));
  contract = {};
  CHECK(!contract.validate());
}

TEST_CASE("neural worker runner uses an explicit helper and validates the response") {
#if defined(SEAM_NEURAL_WORKER_PROBE)
  using namespace seam::neural_synthesis;
  const auto helperHash=seam::core::sha256File(SEAM_NEURAL_WORKER_PROBE); CHECK(helperHash);
  const auto input = request();
  ModelContract contract{
      .modelId = input.modelId,
      .modelVersion = input.modelVersion,
      .modelContentHash = input.modelContentHash,
      .vocabularyHash = std::string(64U, 'b'),
      .sampleRate = input.sampleRate,
      .hopSize = 256U,
      .outputChannels = input.channels,
      .maximumFrames = input.frameCount,
      .maximumModelBytes = 1024U,
  };
  // This is a protocol success-path test, not a two-second cold-launch
  // qualification. Use the existing supported request budget; separate
  // helper-process tests retain their short deadline/cancellation assertions.
  const auto result = runNeuralWorker(input, contract,
      NeuralWorkerRunOptions{.helper = SEAM_NEURAL_WORKER_PROBE,.helperContentHash=helperHash.value()});
  if (!result) throw seam::test::Failure{"Neural worker execution failed: " + result.error().message + " " + result.error().context};
  CHECK(result);
  CHECK(result.value().response.requestId == input.requestId);
  CHECK(result.value().response.pcm.size() == input.frameCount);
  CHECK(!result.value().diagnostic.empty());
  const std::string vocabularyJson=R"({"formatId":"com.project-seam.neural-vocabulary","schemaVersion":1,"tokens":["SP","z","a"]})";
  contract.vocabularyHash=seam::core::sha256Hex(vocabularyJson);
  const auto verifiedVocabulary=NeuralVocabulary::decode(vocabularyJson,contract); CHECK(verifiedVocabulary);
  auto conditioned=input; conditioned.requestId=43U; conditioned.vocabularySize=verifiedVocabulary.value().size();
  conditioned.conditioning=PhoneticConditioning{contract.vocabularyHash,{{1U,0U,2U},{2U,2U,4U}}};
  CHECK(!runNeuralWorker(conditioned,contract,NeuralWorkerRunOptions{.helper=SEAM_NEURAL_WORKER_PROBE,.helperContentHash=helperHash.value()}));
  auto wrongCount=conditioned; wrongCount.vocabularySize=4U;
  CHECK(!runNeuralWorker(wrongCount,contract,NeuralWorkerRunOptions{.helper=SEAM_NEURAL_WORKER_PROBE,.helperContentHash=helperHash.value(),.vocabulary=verifiedVocabulary.value()}));
  const auto conditionedRun=runNeuralWorker(conditioned,contract,NeuralWorkerRunOptions{.helper=SEAM_NEURAL_WORKER_PROBE,.helperContentHash=helperHash.value(),.vocabulary=verifiedVocabulary.value()});
  CHECK(conditionedRun); CHECK(conditionedRun.value().response.requestId==43U);
  CHECK(conditionedRun.value().response.pcm.size()==input.frameCount);
  const auto originalFrame=encodeRequest(conditioned); CHECK(originalFrame);
  CHECK(conditionedRun.value().response.requestContentHash==seam::core::sha256Hex(std::span<const std::byte>{originalFrame.value()}));
  auto changed=conditioned; changed.conditioning->spans[0].tokenId=2U;
  CHECK(encodeRequest(changed).value()!=originalFrame.value());
  changed=conditioned; changed.f0Hz[0]+=1.0F; CHECK(encodeRequest(changed).value()!=originalFrame.value());
  for (const auto id:{44U,45U}) {
    auto faulty=conditioned; faulty.requestId=id;
    const auto rejected=runNeuralWorker(faulty,contract,NeuralWorkerRunOptions{.helper=SEAM_NEURAL_WORKER_PROBE,.helperContentHash=helperHash.value(),.vocabulary=verifiedVocabulary.value()});
    CHECK(!rejected); CHECK(rejected.error().code==seam::core::ErrorCode::Conflict);
    CHECK(rejected.error().message.find("complete request")!=std::string::npos);
  }
  CHECK(!runNeuralWorker(input, contract,
      NeuralWorkerRunOptions{.helper = "relative-neural-worker"}));
  for (const auto& digest: {std::string{},std::string(64U,'A'),std::string(64U,'0')}) {
    const auto rejected=runNeuralWorker(input,contract,
        NeuralWorkerRunOptions{.helper=SEAM_NEURAL_WORKER_PROBE,.helperContentHash=digest});
    CHECK(!rejected);
    CHECK(rejected.error().message.find("helper")!=std::string::npos);
  }
  CHECK(!runNeuralWorker(input,contract,NeuralWorkerRunOptions{
      .helper=SEAM_NEURAL_WORKER_PROBE,.helperContentHash=helperHash.value(),.maximumHelperBytes=1U}));
  CHECK(!runNeuralWorker(input,contract,NeuralWorkerRunOptions{
      .helper=SEAM_NEURAL_WORKER_PROBE,.helperContentHash=helperHash.value(),.maximumHelperBytes=0U}));
  auto memoryProbe=input; memoryProbe.requestId=46U;
  const auto memoryRejected=runNeuralWorker(memoryProbe,contract,NeuralWorkerRunOptions{
      .helper=SEAM_NEURAL_WORKER_PROBE,.helperContentHash=helperHash.value(),.maximumResidentBytes=1U});
  CHECK(!memoryRejected);
  CHECK(memoryRejected.error().message.find("resident-memory limit")!=std::string::npos);
  auto cpuProbe=input; cpuProbe.requestId=47U;
  const auto cpuRejected=runNeuralWorker(cpuProbe,contract,NeuralWorkerRunOptions{
      .helper=SEAM_NEURAL_WORKER_PROBE,.helperContentHash=helperHash.value(),.maximumCpuTime=std::chrono::milliseconds{1}});
  CHECK(!cpuRejected);
  CHECK(cpuRejected.error().message.find("CPU-time limit")!=std::string::npos);
  const auto invalidCpu=runNeuralWorker(input,contract,NeuralWorkerRunOptions{
      .helper=SEAM_NEURAL_WORKER_PROBE,.helperContentHash=helperHash.value(),.maximumCpuTime=std::chrono::milliseconds{-1}});
  CHECK(!invalidCpu);
  std::stop_source cancelled; cancelled.request_stop();
  CHECK(!runNeuralWorker(input,contract,NeuralWorkerRunOptions{
      .helper=SEAM_NEURAL_WORKER_PROBE,.helperContentHash=helperHash.value()},cancelled.get_token()));
#endif
}

TEST_CASE("neural package resolution binds module build helper and dependency files") {
#if defined(SEAM_NEURAL_WORKER_PROBE)
  using namespace seam; using namespace seam::neural_synthesis;
  const auto root=test::support::temporaryDirectory("neural-package");
  CHECK(core::durableAtomicWriteText(root/"module.bin","module-fixture"));
  CHECK(core::durableAtomicWriteText(root/"runtime.bin","runtime-fixture"));
  CHECK(std::filesystem::copy_file(SEAM_NEURAL_WORKER_PROBE,root/"helper"));
  const auto digest=core::sha256File(root/"helper"); CHECK(digest);
  NeuralHelperPackage package{.buildId="test-build",
      .module={"module.bin",core::sha256Hex("module-fixture")},
      .helper={"helper",digest.value()},
      .dependencies={{"runtime.bin",core::sha256Hex("runtime-fixture")}}};
  const auto manifestFile=[](std::string_view path,std::string_view hash) {
    return "{\"path\":\""+std::string{path}+"\",\"sha256\":\""+std::string{hash}+"\",\"maximumBytes\":268435456}";
  };
  const std::string manifest="{\"formatId\":\"com.project-seam.neural-helper-package\",\"schemaVersion\":1,\"buildId\":\"test-build\",\"protocolVersion\":1,\"module\":"+
      manifestFile("module.bin",package.module.contentHash)+",\"helper\":"+manifestFile("helper",digest.value())+
      ",\"dependencies\":["+manifestFile("runtime.bin",package.dependencies[0].contentHash)+"]}";
  const auto decoded=NeuralHelperPackage::decode(manifest,core::sha256Hex(manifest)); CHECK(decoded);
  package=decoded.value();
  CHECK(!NeuralHelperPackage::decode(manifest+" ",core::sha256Hex(manifest)));
  const auto rejectReplacement=[&](std::string_view from,std::string_view to) {
    auto changed=manifest; const auto at=changed.find(from); CHECK(at!=std::string::npos);
    changed.replace(at,from.size(),to);
    CHECK(!NeuralHelperPackage::decode(changed,core::sha256Hex(changed)));
  };
  rejectReplacement("\"schemaVersion\":1","\"schemaVersion\":2");
  rejectReplacement("\"protocolVersion\":1","\"protocolVersion\":1.5");
  rejectReplacement("module.bin","../module.bin");
  rejectReplacement("module.bin","/module.bin");
  rejectReplacement("module.bin","C:module.bin");
  rejectReplacement("module.bin","a//module.bin");
  rejectReplacement("module.bin","a/./module.bin");
  rejectReplacement("module.bin","a\\u0000module.bin");
  rejectReplacement("runtime.bin","module.bin");
  rejectReplacement("268435456","0");
  rejectReplacement("268435456","268435457");
  rejectReplacement("\"buildId\":\"test-build\"","\"buildId\":\"test-build\",\"unknown\":true");
  const std::string oversized(256U*1024U+1U,' ');
  CHECK(!NeuralHelperPackage::decode(oversized,core::sha256Hex(oversized)));
  const auto resolve=[&](const NeuralHelperPackage& value) {
    return resolveNeuralHelperPackage(root,root/"module.bin",value,"test-build");
  };
  const auto valid=resolve(package); CHECK(valid);
  CHECK(valid.value().helper==std::filesystem::canonical(root/"helper"));
  CHECK(valid.value().helperContentHash==digest.value());
  auto bad=package; bad.buildId="other-build"; CHECK(!resolve(bad));
  bad=package; bad.protocolVersion=3U; CHECK(!resolve(bad));
  auto bundleManifest=manifest;
  bundleManifest.replace(bundleManifest.find("\"schemaVersion\":1"),17U,"\"schemaVersion\":2");
  bundleManifest.replace(bundleManifest.find("\"protocolVersion\":1"),19U,"\"protocolVersion\":2");
  const auto bundlePackage=NeuralHelperPackage::decode(bundleManifest,core::sha256Hex(bundleManifest)); CHECK(bundlePackage);
  CHECK(bundlePackage.value().protocolVersion==2U);
  const auto bundleOptions=resolve(bundlePackage.value()); CHECK(bundleOptions);
  CHECK(bundleOptions.value().protocolVersion==2U);
  CHECK(!runNeuralWorker(request(),ModelContract{},bundleOptions.value()));
  bad=package; bad.helper.relativePath="../helper"; CHECK(!resolve(bad));
  bad=package; bad.helper.relativePath=root/"helper"; CHECK(!resolve(bad));
  bad=package; bad.helper.relativePath="missing"; CHECK(!resolve(bad));
  bad=package; bad.dependencies.push_back(bad.helper); CHECK(!resolve(bad));
  bad=package; bad.dependencies[0].contentHash=std::string(64U,'0'); CHECK(!resolve(bad));
  bad=package; bad.dependencies.resize(65U,bad.dependencies.front()); CHECK(!resolve(bad));
  CHECK(!resolveNeuralHelperPackage(root,root/"runtime.bin",package,"test-build"));
  CHECK(!resolveNeuralHelperPackage("relative-root",root/"module.bin",package,"test-build"));
  std::stop_source stop; stop.request_stop();
  CHECK(!resolveNeuralHelperPackage(root,root/"module.bin",package,"test-build",stop.get_token()));
#if !defined(_WIN32)
  std::filesystem::create_symlink(root/"helper",root/"redirect");
  bad=package; bad.helper.relativePath="redirect"; CHECK(!resolve(bad));
#endif
  CHECK(core::durableAtomicWriteText(root/"runtime.bin","changed-runtime"));
  CHECK(!resolve(package));
#endif
}

TEST_CASE("neural helper resolves from a loaded module address and executes its sibling probe") {
#if defined(SEAM_NEURAL_WORKER_PROBE)
  using namespace seam; using namespace seam::neural_synthesis;
  static const char moduleAnchor=0;
  const auto module=platform::loadedModulePath(&moduleAnchor); CHECK(module);
  CHECK(module.value().is_absolute()); CHECK(!platform::loadedModulePath(nullptr));
  const auto helper=std::filesystem::canonical(SEAM_NEURAL_WORKER_PROBE);
  CHECK(helper.parent_path()==module.value().parent_path());
  const auto moduleHash=core::sha256File(module.value()); CHECK(moduleHash);
  const auto helperHash=core::sha256File(helper); CHECK(helperHash);
  const NeuralHelperPackage package{.buildId="address-fixture",
      .module={module.value().filename(),moduleHash.value()},
      .helper={helper.filename(),helperHash.value()},.dependencies={}};
  const auto options=resolveNeuralHelperForModule(&moduleAnchor,package,"address-fixture"); CHECK(options);
  CHECK(options.value().helper==helper);
  auto wrong=package; wrong.module.relativePath="wrong-module";
  CHECK(!resolveNeuralHelperForModule(&moduleAnchor,wrong,"address-fixture"));
  CHECK(!resolveNeuralHelperForModule(nullptr,package,"address-fixture"));
  const auto input=request();
  const ModelContract model{.modelId=input.modelId,.modelVersion=input.modelVersion,
      .modelContentHash=input.modelContentHash,.vocabularyHash=std::string(64U,'b')};
  const auto run=runNeuralWorker(input,model,options.value()); CHECK(run);
  CHECK(run.value().response.requestId==input.requestId);
#endif
}

TEST_CASE("signed neural deployment binds exact descriptor bytes and the loaded surface identity") {
  using namespace seam; using namespace seam::neural_synthesis;
  const auto key=distribution::generateSigningKeyPair(); CHECK(key);
  const auto stranger=distribution::generateSigningKeyPair(); CHECK(stranger);
  const std::string json=R"({"formatId":"com.project-seam.neural-deployment","schemaVersion":1,"buildId":"signed-build","platform":"macos-arm64","surface":"clap","modulePath":"Contents/MacOS/SEAM","manifestPath":"Contents/Resources/neural-helper-package.json","manifestSha256":")"+
      std::string(64U,'a')+"\"}";
  const NeuralDeploymentTarget target{"signed-build","macos-arm64","clap"};
  const auto sign=[&](const std::string& bytes) {
    return distribution::signEd25519(std::as_bytes(std::span{bytes.data(),bytes.size()}),key.value().privateKey);
  };
  const auto signature=sign(json); CHECK(signature);
  auto v2=json;
  v2.replace(v2.find("\"schemaVersion\":1"),17U,"\"schemaVersion\":2,\"protocolVersion\":2");
  const auto v2Signature=sign(v2); CHECK(v2Signature);
  auto v2Target=target; v2Target.protocolVersion=2U;
  CHECK(VerifiedNeuralDeployment::verify(v2,v2Signature.value(),key.value().publicKey,v2Target));
  CHECK(!VerifiedNeuralDeployment::verify(v2,v2Signature.value(),key.value().publicKey,target));
  CHECK(!VerifiedNeuralDeployment::verify(json,signature.value(),key.value().publicKey,v2Target));
  const auto verified=VerifiedNeuralDeployment::verify(json,signature.value(),key.value().publicKey,target); CHECK(verified);
  CHECK(verified.value().contentHash()==core::sha256Hex(json));
  CHECK(!verified.value().load(nullptr));
  CHECK(!VerifiedNeuralDeployment::verify(json+" ",signature.value(),key.value().publicKey,target));
  CHECK(!VerifiedNeuralDeployment::verify(json,signature.value(),stranger.value().publicKey,target));
  for (const auto& wrong: {NeuralDeploymentTarget{"other-build","macos-arm64","clap"},
      NeuralDeploymentTarget{"signed-build","windows-x64","clap"},
      NeuralDeploymentTarget{"signed-build","macos-arm64","vst3"}})
    CHECK(!VerifiedNeuralDeployment::verify(json,signature.value(),key.value().publicKey,wrong));
  const auto rejectSigned=[&](std::string_view from,std::string_view to) {
    auto changed=json; const auto at=changed.find(from); CHECK(at!=std::string::npos);
    changed.replace(at,from.size(),to); const auto signedChange=sign(changed); CHECK(signedChange);
    CHECK(!VerifiedNeuralDeployment::verify(changed,signedChange.value(),key.value().publicKey,target));
  };
  rejectSigned("\"schemaVersion\":1","\"schemaVersion\":2");
  rejectSigned("Contents/MacOS/SEAM","../SEAM");
  rejectSigned("Contents/MacOS/SEAM","C:SEAM");
  rejectSigned("Contents/MacOS/SEAM","Contents//SEAM");
  rejectSigned("Contents/MacOS/SEAM","Contents/\\u0000SEAM");
  rejectSigned(std::string(64U,'a'),std::string(64U,'A'));
  rejectSigned("\"schemaVersion\":1","\"schemaVersion\":1,\"unexpected\":true");
  const std::string oversized(16U*1024U+1U,' '); const auto oversizedSignature=sign(oversized); CHECK(oversizedSignature);
  CHECK(!VerifiedNeuralDeployment::verify(oversized,oversizedSignature.value(),key.value().publicKey,target));
  // The full-product contract spells the Windows target "windows-x86_64"; the
  // deployment descriptor spells it "windows-x64". The two names are not
  // interchangeable, so a correctly signed descriptor carrying the contract
  // spelling must be refused instead of accepted by string coincidence. Only the
  // explicit mapping owner may translate between the namespaces.
  const auto swapPlatform=[&](std::string_view platform) {
    auto bytes=json; const auto at=bytes.find("\"macos-arm64\""); CHECK(at!=std::string::npos);
    bytes.replace(at,13U,std::string{"\""}+std::string{platform}+"\""); return bytes;
  };
  const auto windowsBytes=swapPlatform("windows-x64");
  const auto windowsSignature=sign(windowsBytes); CHECK(windowsSignature);
  CHECK(VerifiedNeuralDeployment::verify(windowsBytes,windowsSignature.value(),key.value().publicKey,
      NeuralDeploymentTarget{"signed-build","windows-x64","clap"}));
  const auto contractBytes=swapPlatform("windows-x86_64");
  const auto contractSignature=sign(contractBytes); CHECK(contractSignature);
  CHECK(!VerifiedNeuralDeployment::verify(contractBytes,contractSignature.value(),key.value().publicKey,
      NeuralDeploymentTarget{"signed-build","windows-x86_64","clap"}));
  CHECK(!VerifiedNeuralDeployment::verify(contractBytes,contractSignature.value(),key.value().publicKey,
      NeuralDeploymentTarget{"signed-build","windows-x64","clap"}));
}

TEST_CASE("DiffSinger inputs conserve hop durations and retain phone ownership at rounded boundaries") {
  using namespace seam; using namespace seam::neural_synthesis;
  const std::string vocabularyJson=R"({"formatId":"com.project-seam.neural-vocabulary","schemaVersion":1,"tokens":["<PAD>","SP","a","z"]})";
  auto input=request();
  input.frameCount=11U;
  input.f0Hz={0,0,440,440,440,440,0,0,0,0,0};
  input.dynamics.assign(11U,1.0F);
  input.vocabularySize=4U;
  input.conditioning=PhoneticConditioning{core::sha256Hex(vocabularyJson),{{1U,0U,2U},{2U,2U,6U},{3U,6U,11U}}};
  ModelContract model{.modelId=input.modelId,.modelVersion=input.modelVersion,.modelContentHash=input.modelContentHash,
      .vocabularyHash=input.conditioning->vocabularyHash,.hopSize=4U};
  const auto vocabulary=NeuralVocabulary::decode(vocabularyJson,model); CHECK(vocabulary);
  const auto prepared=prepareDiffSingerAcousticInputs(input,model,vocabulary.value(),20); CHECK(prepared);
  CHECK(prepared.value().tokens==(std::vector<std::int64_t>{1,2,3}));
  CHECK(prepared.value().durations==(std::vector<std::int64_t>{1,1,1}));
  CHECK(prepared.value().f0Hz==(std::vector<float>{0,440,0}));
  CHECK(prepared.value().outputSampleFrames==11U); CHECK(prepared.value().paddedSampleFrames==12U);
  CHECK(prepared.value().steps==20);
  auto changed=input; changed.conditioning->spans[0].tokenId=0;
  const auto padding=prepareDiffSingerAcousticInputs(changed,model,vocabulary.value(),20);
  CHECK(!padding); CHECK(padding.error().code==core::ErrorCode::Unsupported);
  changed=input; changed.conditioning->spans[0].endFrame=1; changed.conditioning->spans[1].startFrame=1;
  const auto erased=prepareDiffSingerAcousticInputs(changed,model,vocabulary.value(),20);
  CHECK(!erased); CHECK(erased.error().message.find("erase")!=std::string::npos);
  changed=input; changed.conditioning.reset(); changed.vocabularySize=0;
  CHECK(!prepareDiffSingerAcousticInputs(changed,model,vocabulary.value(),20));
  for (const auto steps:{0,1001}) CHECK(!prepareDiffSingerAcousticInputs(input,model,vocabulary.value(),steps));
  changed=input; changed.vocabularySize=5;
  CHECK(!prepareDiffSingerAcousticInputs(changed,model,vocabulary.value(),20));
  changed=input; changed.f0Hz[0]=std::numeric_limits<float>::quiet_NaN();
  CHECK(!prepareDiffSingerAcousticInputs(changed,model,vocabulary.value(),20));
  std::stop_source stop; stop.request_stop();
  CHECK(!prepareDiffSingerAcousticInputs(input,model,vocabulary.value(),20,stop.get_token()));
  // A long sequence retains the exact total: rounding each duration separately
  // would drift when boundaries are not aligned to the acoustic hop.
  input.frameCount=101U; input.f0Hz.assign(101U,220.0F); input.dynamics.assign(101U,1.0F);
  input.conditioning->spans={{2U,0U,21U},{2U,21U,42U},{2U,42U,63U},{2U,63U,84U},{2U,84U,101U}};
  const auto sequence=prepareDiffSingerAcousticInputs(input,model,vocabulary.value(),5); CHECK(sequence);
  CHECK(sequence.value().durations==(std::vector<std::int64_t>{5,6,5,5,5}));
  CHECK(sequence.value().f0Hz.size()==26U); CHECK(sequence.value().paddedSampleFrames==104U);
}

TEST_CASE("DiffSinger finalization validates padded output before trimming and applying dynamics") {
  using namespace seam::neural_synthesis;
  auto input=request();
  input.frameCount=5; input.f0Hz.assign(5,220.0F); input.dynamics={0.0F,0.5F,1.0F,2.0F,0.25F};
  ModelContract model{.modelId=input.modelId,.modelVersion=input.modelVersion,.modelContentHash=input.modelContentHash,
      .vocabularyHash=std::string(64,'a'),.hopSize=4};
  std::vector<float> pcm(8,0.25F);
  const auto result=finalizeDiffSingerAudio(input,model,pcm); CHECK(result);
  CHECK(result.value()==(std::vector<float>{0.0F,0.125F,0.25F,0.5F,0.0625F}));
  CHECK(pcm==std::vector<float>(8,0.25F));
  const auto response=finalizeDiffSingerResponse(input,model,pcm,"test-vocoder"); CHECK(response);
  CHECK(response.value().pcm==result.value());
  CHECK(response.value().frameCount==input.frameCount);
  const auto requestFrame=encodeRequest(input); CHECK(requestFrame);
  CHECK(response.value().requestContentHash==seam::core::sha256Hex(std::span<const std::byte>{requestFrame.value()}));
  const auto encodedResponse=encodeResponse(response.value()); CHECK(encodedResponse);
  const auto decodedResponse=decodeResponse(encodedResponse.value()); CHECK(decodedResponse);
  CHECK(decodedResponse.value()==response.value());
  auto changedGain=input; changedGain.dynamics[1]=0.25F;
  const auto changedResponse=finalizeDiffSingerResponse(changedGain,model,pcm,"test-vocoder"); CHECK(changedResponse);
  CHECK(changedResponse.value().requestContentHash!=response.value().requestContentHash);
  CHECK(changedResponse.value().pcm[1]==0.0625F);
  CHECK(!finalizeDiffSingerResponse(input,model,pcm,""));
  CHECK(!finalizeDiffSingerAudio(input,model,std::span{pcm}.first(5)));
  for (const float bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),1.1F,-1.1F}) {
    auto invalid=pcm; invalid.back()=bad;
    CHECK(!finalizeDiffSingerAudio(input,model,invalid));
  }
  auto clipping=pcm; clipping[3]=0.75F;
  CHECK(!finalizeDiffSingerAudio(input,model,clipping));
  auto wrong=input; wrong.modelContentHash=std::string(64,'b');
  CHECK(!finalizeDiffSingerAudio(wrong,model,pcm));
  std::stop_source cancel; cancel.request_stop();
  CHECK(!finalizeDiffSingerAudio(input,model,pcm,cancel.get_token()));
}

TEST_CASE("frozen bundle metadata binds vocabulary and rejects incompatible acoustic vocoder declarations") {
  using namespace seam;
  using J=formats::JsonValue;
  const J feature{J::Object{{"sampleRate",std::int64_t{48000}},{"hopSize",std::int64_t{256}},{"bins",std::int64_t{80}},
      {"layout","BTF"},{"amplitudeScale","ln-amplitude"},{"multiplier",1.0},{"offset",0.0},{"minimumHz",40.0},{"maximumHz",16000.0}}};
  J configuration{J::Object{{"formatId","com.project-seam.neural-bundle-configuration"},{"schemaVersion",std::int64_t{1}},
      {"maximumFrames",std::int64_t{48000}},{"acousticFeatures",feature},{"vocoderFeatures",feature}}};
  const std::string vocabulary=R"({"formatId":"com.project-seam.neural-vocabulary","schemaVersion":1,"tokens":["<PAD>","SP","a"]})";
  const auto freeze=[&](const J& config,const std::string& tokens) {
    const std::string json=formats::stringifyJson(config), graph="uninspected graph fixture";
    const auto input=[](synthesis::NeuralAssetRole role,const char* name,const std::string& value) {
      return synthesis::NeuralBundleAssetInput{role,name,std::as_bytes(std::span{value.data(),value.size()}),core::sha256Hex(value)};
    };
    const std::array assets{input(synthesis::NeuralAssetRole::Acoustic,"acoustic",graph),input(synthesis::NeuralAssetRole::Vocoder,"vocoder",graph),
        input(synthesis::NeuralAssetRole::Vocabulary,"vocabulary",tokens),input(synthesis::NeuralAssetRole::Configuration,"configuration",json)};
    const auto manifest=synthesis::FrozenNeuralBundle::manifest(assets,4096U); CHECK(manifest);
    return synthesis::FrozenNeuralBundle::freeze({domain::SingerResourceKind::Neural,"bundle","1",core::sha256Hex(manifest.value())},assets,4096U);
  };
  const auto bundle=freeze(configuration,vocabulary); CHECK(bundle);
  const auto directory=test::support::temporaryDirectory("neural-bundle-directory");
  CHECK(core::durableAtomicWriteNew(directory/"manifest.json",bundle.value().manifestData().bytes()));
  for (const auto& asset:bundle.value().assets()) CHECK(core::durableAtomicWriteNew(directory/asset.name,asset.data->bytes()));
  const auto loaded=neural_synthesis::loadNeuralBundleDirectory(directory,bundle.value().identity(),4096U); CHECK(loaded);
  CHECK(loaded.value().identity().contentHash==bundle.value().identity().contentHash);
  CHECK(neural_synthesis::inspectNeuralBundleMetadata(loaded.value()));
#if defined(SEAM_NEURAL_BUNDLE_TRANSPORT_PROBE) && !defined(_WIN32)
  auto input=request(); input.modelId="bundle"; input.modelVersion="1";
  input.modelContentHash=bundle.value().identity().contentHash;
  input.bundleContentHash=input.modelContentHash; input.vocabularySize=3U;
  input.conditioning=neural_synthesis::PhoneticConditioning{core::sha256Hex(vocabulary),{{2U,0U,4U}}};
  neural_synthesis::NeuralWorkerRunOptions launch{
      .helper=SEAM_NEURAL_BUNDLE_TRANSPORT_PROBE,
      .helperContentHash=core::sha256File(SEAM_NEURAL_BUNDLE_TRANSPORT_PROBE).value(),
      .maximumResidentBytes=256U*1024U*1024U,.maximumCpuTime=std::chrono::seconds{2},.protocolVersion=2U};
  const auto canonical=std::filesystem::canonical(directory);
  const auto run=neural_synthesis::runNeuralBundleWorker(input,canonical,4096U,launch); CHECK(run);
  CHECK(run.value().response.bundleContentHash==input.bundleContentHash);
  auto stale=input; stale.requestId=92U;
  CHECK(!neural_synthesis::runNeuralBundleWorker(stale,canonical,4096U,launch));
  auto unbounded=launch; unbounded.maximumResidentBytes=0U;
  CHECK(!neural_synthesis::runNeuralBundleWorker(input,canonical,4096U,unbounded));
  unbounded=launch; unbounded.maximumCpuTime=std::chrono::milliseconds{0};
  CHECK(!neural_synthesis::runNeuralBundleWorker(input,canonical,4096U,unbounded));
  CHECK(!neural_synthesis::runNeuralBundleWorker(input,canonical,1U,launch));
  // Invalid process budgets must fail before touching a missing model directory.
  for (int scenario=0;scenario<5;++scenario) {
    auto invalidBudget=launch;
    if (scenario==0) invalidBudget.maximumCpuTime=std::chrono::milliseconds{60001};
    if (scenario==1) invalidBudget.timeout=std::chrono::milliseconds{60001};
    if (scenario==2) invalidBudget.maximumResidentBytes=4ULL*1024ULL*1024ULL*1024ULL+1U;
    if (scenario==3) invalidBudget.limits.maximumFrameBytes=64U*1024U*1024U+1U;
    if (scenario==4) invalidBudget.limits.maximumMetadataBytes=1024U*1024U+1U;
    const auto rejected=neural_synthesis::runNeuralBundleWorker(input,canonical/"missing",4096U,invalidBudget);
    CHECK(!rejected); CHECK(rejected.error().code==core::ErrorCode::InvalidArgument);
  }
  auto legacy=launch; legacy.protocolVersion=1U;
  CHECK(!neural_synthesis::runNeuralBundleWorker(input,canonical,4096U,legacy));
  legacy=launch; legacy.helper=SEAM_NEURAL_WORKER_PROBE;
  legacy.helperContentHash=core::sha256File(SEAM_NEURAL_WORKER_PROBE).value();
  CHECK(!neural_synthesis::runNeuralBundleWorker(input,canonical,4096U,legacy));
  std::stop_source cancelledLaunch; cancelledLaunch.request_stop();
  CHECK(!neural_synthesis::runNeuralBundleWorker(input,canonical,4096U,launch,cancelledLaunch.get_token()));
  std::stop_source inFlight;
  auto delayed=input; delayed.requestId=93U;
  const auto marker=canonical/"child-93";
  {
    std::jthread cancelWhenReady([&](std::stop_token token) {
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{3};
      while (!token.stop_requested() && std::chrono::steady_clock::now()<deadline) {
        if (std::filesystem::exists(marker)) {inFlight.request_stop(); return;}
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
      }
    });
    const auto cancelled=neural_synthesis::runNeuralBundleWorker(delayed,canonical,4096U,launch,inFlight.get_token());
    CHECK(!cancelled); CHECK(inFlight.stop_requested());
  }
  const auto childGone=[](const std::filesystem::path& ready) {
    std::ifstream stream{ready}; int pid{}; stream>>pid;
    CHECK(stream); CHECK(pid>0);
    errno=0; CHECK(kill(pid,0)==-1); CHECK(errno==ESRCH);
  };
  childGone(marker);
  delayed.requestId=94U;
  auto deadlineLaunch=launch; deadlineLaunch.timeout=std::chrono::milliseconds{1000};
  CHECK(!neural_synthesis::runNeuralBundleWorker(delayed,canonical,4096U,deadlineLaunch));
  childGone(canonical/"child-94");
  // A fresh request still succeeds after both termination paths.
  CHECK(neural_synthesis::runNeuralBundleWorker(input,canonical,4096U,launch));
#endif
  CHECK(!neural_synthesis::loadNeuralBundleDirectory(directory,bundle.value().identity(),1U));
  auto wrongIdentity=bundle.value().identity(); wrongIdentity.contentHash=std::string(64,'0');
  CHECK(!neural_synthesis::loadNeuralBundleDirectory(directory,wrongIdentity,4096U));
  CHECK(core::durableAtomicWriteText(directory/"vocabulary","{}"));
#if defined(SEAM_NEURAL_BUNDLE_TRANSPORT_PROBE) && !defined(_WIN32)
  CHECK(!neural_synthesis::runNeuralBundleWorker(input,canonical,4096U,launch));
#endif
  CHECK(!neural_synthesis::loadNeuralBundleDirectory(directory,bundle.value().identity(),4096U));
  CHECK(core::durableAtomicWriteText(directory/"vocabulary",std::string(vocabulary.size(),'x')));
  CHECK(!neural_synthesis::loadNeuralBundleDirectory(directory,bundle.value().identity(),4096U));
  CHECK(neural_synthesis::inspectNeuralBundleMetadata(loaded.value()));
  std::stop_source stoppedLoad; stoppedLoad.request_stop();
  CHECK(!neural_synthesis::loadNeuralBundleDirectory(directory,bundle.value().identity(),4096U,stoppedLoad.get_token()));
  const auto metadata=neural_synthesis::inspectNeuralBundleMetadata(bundle.value()); CHECK(metadata);
  CHECK(metadata.value().model.modelContentHash==bundle.value().identity().contentHash);
  CHECK(metadata.value().model.vocabularyHash==core::sha256Hex(vocabulary));
  CHECK(metadata.value().vocabulary.tokenId("a").value()==2U);
  CHECK(metadata.value().features.bins==80U);
  CHECK(metadata.value().configurationVersion==1U);
  CHECK(metadata.value().features.fftSize==0U);
  auto extended=configuration;
  extended.asObject()["schemaVersion"]=std::int64_t{2};
  extended.asObject()["stepsLayout"]="vector1";
  for (const auto* role:{"acousticFeatures","vocoderFeatures"}) {
    auto& spec=extended.asObject()[role].asObject();
    spec["fftSize"]=std::int64_t{2048}; spec["windowSize"]=std::int64_t{1024}; spec["melFrequencyScale"]="slaney";
  }
  const auto extendedBundle=freeze(extended,vocabulary); CHECK(extendedBundle);
  const auto extendedMetadata=neural_synthesis::inspectNeuralBundleMetadata(extendedBundle.value()); CHECK(extendedMetadata);
  CHECK(extendedMetadata.value().configurationVersion==2U);
  CHECK(extendedMetadata.value().features.fftSize==2048U);
  CHECK(extendedMetadata.value().stepsLayout=="vector1");
  auto outputBound=extended; outputBound.asObject()["schemaVersion"]=std::int64_t{3};
  outputBound.asObject()["vocoderOutput"]="waveform";
  const auto outputMetadata=neural_synthesis::inspectNeuralBundleMetadata(freeze(outputBound,vocabulary).value()); CHECK(outputMetadata);
  CHECK(outputMetadata.value().configurationVersion==3U);
  CHECK(outputMetadata.value().vocoderOutput=="waveform");
  outputBound.asObject()["vocoderOutput"]="guessed";
  CHECK(!neural_synthesis::inspectNeuralBundleMetadata(freeze(outputBound,vocabulary).value()));
  for (const auto* key:{"fftSize","windowSize","melFrequencyScale"}) {
    auto mismatch=extended;
    auto& value=mismatch.asObject()["vocoderFeatures"].asObject()[key];
    value=value.isString()?J{"htk"}:J{value.asInt64()+1};
    CHECK(!neural_synthesis::inspectNeuralBundleMetadata(freeze(mismatch,vocabulary).value()));
  }
  auto badWindow=extended;
  for (const auto* role:{"acousticFeatures","vocoderFeatures"}) badWindow.asObject()[role].asObject()["windowSize"]=std::int64_t{4096};
  CHECK(!neural_synthesis::inspectNeuralBundleMetadata(freeze(badWindow,vocabulary).value()));
  auto badSteps=extended; badSteps.asObject()["stepsLayout"]="guess";
  CHECK(!neural_synthesis::inspectNeuralBundleMetadata(freeze(badSteps,vocabulary).value()));
  for (const auto* field:{"sampleRate","hopSize","bins","layout","amplitudeScale","multiplier","offset","minimumHz","maximumHz"}) {
    auto mismatch=configuration;
    auto& value=mismatch.asObject()["vocoderFeatures"].asObject()[field];
    if (value.isString()) value=std::string(field)=="layout"?"BFT":"log10-amplitude";
    else if (value.isInteger()) value=value.asInt64()+1;
    else value=value.asNumber()+1.0;
    const auto changed=freeze(mismatch,vocabulary); CHECK(changed);
    CHECK(!neural_synthesis::inspectNeuralBundleMetadata(changed.value()));
  }
  auto extra=configuration; extra.asObject()["executable"]="helper";
  CHECK(!neural_synthesis::inspectNeuralBundleMetadata(freeze(extra,vocabulary).value()));
  // Matching declarations are insufficient: reject an invalid convention even
  // when both graphs claim the same values.
  const std::vector<std::pair<std::string,J>> invalidFeatures{
      {"sampleRate",std::int64_t{7999}}, {"sampleRate",std::int64_t{384001}},
      {"sampleRate",48000.5}, {"hopSize",std::int64_t{0}},
      {"hopSize",std::int64_t{8193}}, {"bins",std::int64_t{0}},
      {"bins",std::int64_t{513}}, {"layout","TF"},
      {"amplitudeScale","unspecified"}, {"multiplier",0.0},
      {"multiplier",1000.1}, {"offset",1000.1}, {"offset",-1000.1},
      {"minimumHz",-1.0}, {"maximumHz",40.0}, {"maximumHz",24001.0}};
  for (const auto& [field,value]:invalidFeatures) {
    auto invalid=configuration;
    for (const auto* role:{"acousticFeatures","vocoderFeatures"})
      invalid.asObject()[role].asObject()[field]=value;
    const auto changed=freeze(invalid,vocabulary); CHECK(changed);
    CHECK(!neural_synthesis::inspectNeuralBundleMetadata(changed.value()));
  }
  for (const auto bound:{std::int64_t{0},std::int64_t{4194305}}) {
    auto invalid=configuration; invalid.asObject()["maximumFrames"]=bound;
    const auto changed=freeze(invalid,vocabulary); CHECK(changed);
    CHECK(!neural_synthesis::inspectNeuralBundleMetadata(changed.value()));
  }
  const auto badVocabulary=freeze(configuration,"{}"); CHECK(badVocabulary);
  CHECK(!neural_synthesis::inspectNeuralBundleMetadata(badVocabulary.value()));
  for (const auto& token:std::vector<std::string>{std::string(129,'a'),"phone\n",std::string{"phone\x7f"},std::string(1,'\0')}) {
    const auto json=formats::stringifyJson(J{J::Object{{"formatId","com.project-seam.neural-vocabulary"},
        {"schemaVersion",std::int64_t{1}},{"tokens",J::Array{J{"<PAD>"},J{token}}}}});
    const auto invalid=freeze(configuration,json); CHECK(invalid);
    CHECK(!neural_synthesis::inspectNeuralBundleMetadata(invalid.value()));
  }
  const auto boundaryJson=formats::stringifyJson(J{J::Object{{"formatId","com.project-seam.neural-vocabulary"},
      {"schemaVersion",std::int64_t{1}},{"tokens",J::Array{J{"<PAD>"},J{std::string(128,'a')}}}}});
  const auto boundaryBundle=freeze(configuration,boundaryJson); CHECK(boundaryBundle);
  CHECK(neural_synthesis::inspectNeuralBundleMetadata(boundaryBundle.value()));
  std::stop_source cancel; cancel.request_stop();
  CHECK(!neural_synthesis::inspectNeuralBundleMetadata(bundle.value(),cancel.get_token()));
}
