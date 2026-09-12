#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/core/file_io.hpp"
#include "seam/platform/application_paths.hpp"

#include "seam/neural_synthesis/worker_protocol.hpp"
#include "seam/neural_synthesis/model_contract.hpp"
#include "seam/neural_synthesis/neural_phrase_backend.hpp"
#include "seam/neural_synthesis/deployment_descriptor.hpp"
#include "seam/core/sha256.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

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
  bad=package; bad.protocolVersion=2U; CHECK(!resolve(bad));
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
}
