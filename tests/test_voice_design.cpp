#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/voice_design/voice_recipe.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voice_design/vocal_tract.hpp"
#include "seam/voice_design/phonation_source.hpp"
#include "seam/voice_design/frication_source.hpp"
#include "seam/voice_design/plosive_source.hpp"
#include "seam/voice_design/articulation_plan.hpp"
#include "seam/voice_design/frication_gesture_stream.hpp"
#include "seam/voice_design/articulated_stream.hpp"
#include "seam/voice_design/procedural_renderer.hpp"
#include "seam/voice_design/procedural_candidate.hpp"
#include "seam/voicebank/pitch.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"
#include <limits>
#include <cmath>
#include <numbers>
#include <algorithm>

namespace {
seam::voice_design::VoiceRecipe nasalFixture() {
  seam::voice_design::VoiceRecipe recipe; recipe.id="nasal-model-test"; recipe.seed=42U;
  recipe.phonation.aspiration=0.0;
  recipe.poses={{"a","neutral",1.0,{{700.0,80.0,0.0},{1200.0,100.0,-3.0},{2600.0,140.0,-6.0}},
      seam::voice_design::NasalResonance{}}};
  return recipe;
}

TEST_CASE("voiced note reattacks taper boundaries while melisma remains continuous") {
  using namespace seam;
  for (const auto rate : {22050U, 44100U, 48000U, 96000U})
  for (const auto pitch : {36U, 61U, 84U})
  for (const bool continuation : {false, true}) {
    const auto boundary=static_cast<time::SampleFrame>(rate/2U);
    const auto end=static_cast<time::SampleFrame>(rate);
    domain::Project project{domain::ProjectId{1U},"Voiced boundaries"};
    domain::VocalRegion region{.id=domain::RegionId{3U},.durationTick=time::Tick{1920},
        .lyrics={{domain::LyricTokenId{4U},U"あ",domain::Language::Japanese},
                 {domain::LyricTokenId{6U},continuation ? U"ー" : U"あ",domain::Language::Japanese}},
        .notes={{.id=domain::NoteId{5U},.durationTick=time::Tick{960},.midiKey=static_cast<std::uint8_t>(pitch),.lyricTokenId=domain::LyricTokenId{4U}},
                {.id=domain::NoteId{7U},.startTick=time::Tick{960},.durationTick=time::Tick{960},.midiKey=static_cast<std::uint8_t>(pitch+4U),.lyricTokenId=domain::LyricTokenId{6U}}}};
    const auto phones=phonemizer::resolveJapanesePronunciation(region); CHECK(phones);
    const auto performance=synthesis::compileScorePerformance(project,region,rate,phones.value().pronunciation.tokens,synthesis::PhonemeTimingPolicy::ProceduralInNote); CHECK(performance);
    CHECK(performance.value().at(boundary).reattack==!continuation);
    const auto resource=voice_design::freezeVoiceRecipeResource(nasalFixture()); CHECK(resource);
    auto whole=voice_design::ArticulatedStream::createFromRecipe(resource.value(),performance.value(),phones.value().pronunciation.tokens,"neutral",127U); CHECK(whole);
    const auto audio=whole.value().renderOwned({0,end}); CHECK(audio);
    if (!continuation) {
      CHECK(audio.value().samples[static_cast<std::size_t>(boundary-1)]==0.0F);
      CHECK(audio.value().samples[static_cast<std::size_t>(boundary)]==0.0F);
    } else {
      CHECK(std::abs(audio.value().samples[static_cast<std::size_t>(boundary-1)])+std::abs(audio.value().samples[static_cast<std::size_t>(boundary)])>0.000001F);
    }
    auto chunks=voice_design::ArticulatedStream::createFromRecipe(resource.value(),performance.value(),phones.value().pronunciation.tokens,"neutral",257U); CHECK(chunks);
    auto first=chunks.value().renderOwned({0,boundary-1}); CHECK(first);
    auto checkpoint=chunks.value();
    std::stop_source stop; stop.request_stop();
    CHECK(!chunks.value().renderOwned({boundary-1,end},stop.get_token()));
    const auto last=chunks.value().renderOwned({boundary-1,end}); CHECK(last);
    CHECK(checkpoint.renderOwned({boundary-1,end}).value().samples==last.value().samples);
    first.value().samples.insert(first.value().samples.end(),last.value().samples.begin(),last.value().samples.end());
    CHECK(first.value().samples==audio.value().samples);
    whole.value().reset();
    CHECK(whole.value().renderOwned({0,end}).value().samples==audio.value().samples);
  }
}

TEST_CASE("missing tract coverage identifies the requested phone style and recipe") {
  const auto recipe=nasalFixture();
  const auto missingPhone=seam::voice_design::VocalTract::create(recipe,"i","neutral",48000U);
  CHECK(!missingPhone);
  CHECK(missingPhone.error().code==seam::core::ErrorCode::NotFound);
  CHECK(missingPhone.error().message.find("phone 'i'")!=std::string::npos);
  CHECK(missingPhone.error().message.find("style 'neutral'")!=std::string::npos);
  CHECK(missingPhone.error().message.find(recipe.id)!=std::string::npos);
  const auto missingStyle=seam::voice_design::VocalTract::create(recipe,"a","soft",48000U);
  CHECK(!missingStyle);
  CHECK(missingStyle.error().message.find("style 'soft'")!=std::string::npos);
}

TEST_CASE("mixed voiced frication remains replayable across rates pitches and gain endpoints") {
  using namespace seam;
  for (const auto rate:{22050U,44100U,48000U,96000U}) for (const auto pitch:{36U,69U,96U})
    for (const bool coda:{false,true}) for (const double gain:{0.01,1.0}) {
      auto recipe=nasalFixture(); recipe.poses[0].nasal.reset(); recipe.poses[0].nasalCoupling=0.0;
      recipe.poses.push_back(recipe.poses[0]); recipe.poses.back().phone="z";
      recipe.frications={{"z","neutral",{.seed=42U,.gain=0.25},gain}};
      const auto resource=voice_design::freezeVoiceRecipeResource(recipe); CHECK(resource);
      domain::Project project{domain::ProjectId{1U},"Mixed source matrix"};
      domain::VocalRegion region{.id=domain::RegionId{3U},.durationTick=time::Tick{480},
          .lyrics={{domain::LyricTokenId{4U},U"test",domain::Language::English}},
          .notes={{.id=domain::NoteId{5U},.durationTick=time::Tick{480},.midiKey=static_cast<std::uint8_t>(pitch),.lyricTokenId=domain::LyricTokenId{4U}}}};
      std::vector<domain::PhonemeToken> phones{
          {.key={domain::NoteId{5U},0U},.symbol="z",.role=domain::PhonemeRole::Onset,.voiced=true,.timing={.startOffset=0}},
          {.key={domain::NoteId{5U},1U},.symbol="a",.role=domain::PhonemeRole::Nucleus,.voiced=true,.timing={.startOffset=100000}}};
      if (coda) phones={
          {.key={domain::NoteId{5U},0U},.symbol="a",.role=domain::PhonemeRole::Nucleus,.voiced=true,.timing={.startOffset=0,.endOffset=150000}},
          {.key={domain::NoteId{5U},1U},.symbol="z",.role=domain::PhonemeRole::Coda,.voiced=true,.timing={.startOffset=150000}}};
      const auto performance=synthesis::compileScorePerformance(project,region,rate,phones); CHECK(performance);
      const synthesis::PhraseFrameRange context{0,performance.value().notes().back().endFrame};
      auto whole=voice_design::ArticulatedStream::createFromRecipe(resource.value(),performance.value(),phones,"neutral",512U); CHECK(whole);
      const auto audio=whole.value().renderOwned(context); CHECK(audio);
      // Internal DSP safety bound, not a mastering/loudness acceptance target.
      CHECK(std::all_of(audio.value().samples.begin(),audio.value().samples.end(),[](float sample){return std::isfinite(sample)&&std::abs(sample)<=9.0F;}));
      CHECK(std::any_of(audio.value().samples.begin(),audio.value().samples.end(),[](float sample){return std::abs(sample)>0.00001F;}));
      auto chunked=voice_design::ArticulatedStream::createFromRecipe(resource.value(),performance.value(),phones,"neutral",127U); CHECK(chunked);
      const auto split=context.end/2;
      auto first=chunked.value().renderOwned({0,split}); CHECK(first);
      const auto tail=chunked.value().renderOwned({split,context.end}); CHECK(tail);
      first.value().samples.insert(first.value().samples.end(),tail.value().samples.begin(),tail.value().samples.end());
      CHECK(first.value().samples==audio.value().samples);
      whole.value().reset(); const auto cropped=whole.value().renderOwned({context.end-1000,context.end-1}); CHECK(cropped);
      CHECK(std::equal(cropped.value().samples.begin(),cropped.value().samples.end(),audio.value().samples.end()-1000));
      const auto lowRate=synthesis::compileScorePerformance(project,region,8000U,phones); CHECK(lowRate);
      CHECK(!voice_design::ArticulatedStream::createFromRecipe(resource.value(),lowRate.value(),phones,"neutral"));
    }
}

TEST_CASE("voiced frication candidate metadata preserves recipe voicing and rejects relabeling") {
  using namespace seam;
  using J=formats::JsonValue;
  auto recipe=nasalFixture(); recipe.poses[0].nasal.reset(); recipe.poses[0].nasalCoupling=0.0;
  recipe.poses.push_back(recipe.poses[0]); recipe.poses.back().phone="z";
  recipe.frications={{"z","neutral",{.seed=42U},0.3}};
  const auto resource=voice_design::freezeVoiceRecipeResource(recipe); CHECK(resource);
  const auto marker=[](std::uint16_t ordinal,const char* phone,const char* kind,std::int64_t start,std::int64_t end) {
    return J{J::Object{{"key",domain::PhonemeKey{domain::NoteId{1U},ordinal}.toString()},
        {"phone",phone},{"kind",kind},{"startFrame",start},{"endFrame",end}}};
  };
  J metadata{J::Object{{"formatId","com.project-seam.procedural-candidate"},{"schemaVersion",std::int64_t{5}},
      {"approval","unapproved"},{"markerSemantics","planned-articulated-gestures"},
      {"audioSha256",std::string(64U,'a')},{"renderContentHash",std::string(64U,'b')},{"renderAbi","test-abi"},
      {"recipeId",resource.value().identity.id},{"recipeVersion","5"},{"recipeHash",resource.value().identity.contentHash},
      {"style","neutral"},{"sampleRate",std::int64_t{48000}},{"frameCount",std::int64_t{24000}},
      {"scoreOriginFrame",std::int64_t{0}},{"proceduralRevision",std::int64_t{8}},{"compilerRevision",std::int64_t{9}},
      {"articulationPlanRevision",std::int64_t{8}},{"fricationRevision",std::int64_t{1}},
      {"fricationStreamRevision",std::int64_t{3}},{"plosiveRevision",std::int64_t{1}},
      {"markers",J::Array{marker(0U,"z","voiced-frication",0,4800),marker(1U,"a","oral-vowel",4800,24000)}}}};
  const auto parse=[&](const J& value){return voice_design::parseProceduralCandidateMetadata(formats::stringifyJson(value),resource.value());};
  const auto valid=parse(metadata); CHECK(valid); CHECK(valid.value().schemaVersion==5U);
  CHECK(valid.value().markers[0].kind==voice_design::ProceduralGestureKind::VoicedFrication);
  for (unsigned scenario=0U;scenario<7U;++scenario) {
    auto bad=metadata;
    if (scenario==0U) bad.asObject()["schemaVersion"]=std::int64_t{4};
    if (scenario==1U) bad.asObject()["approval"]="approved";
    if (scenario==2U) bad.asObject()["proceduralRevision"]=std::int64_t{7};
    if (scenario==3U) bad.asObject()["markers"].asArray()[0].asObject()["kind"]="frication";
    if (scenario==4U) bad.asObject()["markers"].asArray()[0].asObject()["phone"]="v";
    if (scenario==5U) bad.asObject()["markers"].asArray()[0].asObject()["endFrame"]=std::int64_t{25000};
    if (scenario==6U) bad.asObject()["markers"].asArray().erase(bad.asObject()["markers"].asArray().begin());
    CHECK(!parse(bad));
  }
  auto invalidRecipe=recipe; invalidRecipe.frications[0].phone="a"; CHECK(!invalidRecipe.validate());
  invalidRecipe=recipe; invalidRecipe.frications[0].phone="n"; invalidRecipe.poses.back().phone="n"; CHECK(!invalidRecipe.validate());
}

TEST_CASE("voiced frication recipe contract is explicit versioned and cannot downgrade") {
  using namespace seam;
  auto recipe=nasalFixture(); recipe.poses[0].nasal.reset(); recipe.poses[0].nasalCoupling=0.0;
  recipe.frications={{"z","neutral",{.seed=42U}}};
  const auto legacy=voice_design::encodeVoiceRecipe(recipe); CHECK(legacy);
  CHECK(voice_design::voiceRecipeSchemaVersion(recipe)==2);
  recipe.frications[0].voicingGain=0.3;
  CHECK(!recipe.validate()); // No implicit borrowing of the neighboring vowel tract.
  recipe.poses.push_back(recipe.poses.front()); recipe.poses.back().phone="z";
  CHECK(recipe.validate()); CHECK(voice_design::voiceRecipeSchemaVersion(recipe)==5);
  recipe.frications.push_back({"s","neutral",{.seed=7U}});
  const auto bytes=voice_design::encodeVoiceRecipe(recipe); CHECK(bytes);
  const auto decoded=voice_design::decodeVoiceRecipe(bytes.value()); CHECK(decoded); CHECK(decoded.value()==recipe);
  CHECK(voice_design::encodeVoiceRecipe(decoded.value()).value()==bytes.value());
  auto malformed=formats::parseJson(bytes.value()).value();
  malformed.asObject()["schemaVersion"]=formats::JsonValue{std::int64_t{4}};
  CHECK(!voice_design::decodeVoiceRecipe(formats::stringifyJson(malformed)));
  for (const auto gain:{0.0,-0.1,1.01,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
    auto bad=recipe; bad.frications[0].voicingGain=gain; CHECK(!bad.validate());
  }
  auto missing=recipe; missing.poses.back().style="other"; CHECK(!missing.validate());
  const auto resource=voice_design::freezeVoiceRecipeResource(recipe); CHECK(resource);
  CHECK(resource.value().identity.version=="5");
  CHECK(voice_design::decodeVoiceRecipeResource(resource.value()));
  CHECK(!voice_design::decodeVoiceRecipeResource(resource.value(),{},false));
  recipe.frications.pop_back(); recipe.frications[0].voicingGain.reset(); recipe.poses.pop_back();
  CHECK(voice_design::encodeVoiceRecipe(recipe).value()==legacy.value());
}
double measuredTone(const std::vector<float>& audio,double frequency,std::uint32_t rate=48000U) {
  double real=0.0,imaginary=0.0;
  // Measure a settled, integer-cycle window, excluding the test helper's
  // 128-frame fade-out (which deliberately creates transient spectral energy).
  for (std::size_t i=audio.size()/2U;i<audio.size()*9U/10U;++i) {
    const auto phase=2.0*std::numbers::pi*frequency*static_cast<double>(i)/rate;
    real+=audio[i]*std::cos(phase); imaginary+=audio[i]*std::sin(phase);
  }
  return std::hypot(real,imaginary);
}
}

TEST_CASE("plosive articulation renders exact closure burst and vowel with transactional seeking") {
  using namespace seam;
  domain::Project project{domain::ProjectId{1U},"Stop articulation"};
  domain::VocalRegion region{.id=domain::RegionId{3U},.name="Note",.durationTick=time::Tick{960},
      .lyrics={{domain::LyricTokenId{4U},U"か",domain::Language::Japanese}},
      .notes={{.id=domain::NoteId{5U},.durationTick=time::Tick{960},.midiKey=69U,.lyricTokenId=domain::LyricTokenId{4U}}}};
  std::vector<domain::PhonemeToken> phones{
      {.key={domain::NoteId{5U},0U},.symbol="k",.role=domain::PhonemeRole::Onset,.voiced=false,.timing={.startOffset=0}},
      {.key={domain::NoteId{5U},1U},.symbol="a",.role=domain::PhonemeRole::Nucleus,.voiced=true,.timing={.startOffset=100000}}};
  auto recipe=nasalFixture(); recipe.plosives={{"k","neutral",{.seed=42U,.centerHz=2000.0,.bandwidthHz=1000.0},10.0}};
  const auto resource=voice_design::freezeVoiceRecipeResource(recipe); CHECK(resource);
  for (const auto rate : {8000U,48000U,96000U}) {
    const auto performance=synthesis::compileScorePerformance(project,region,rate,phones); CHECK(performance);
    const auto plan=voice_design::ArticulationPlan::compileRecipe(resource.value(),performance.value(),phones,"neutral"); CHECK(plan);
    for (const auto* symbol : {"p","t"}) {
      auto alternatePhones=phones; alternatePhones.front().symbol=symbol;
      auto alternateRecipe=recipe; alternateRecipe.plosives.front().phone=symbol;
      const auto alternateResource=voice_design::freezeVoiceRecipeResource(alternateRecipe); CHECK(alternateResource);
      const auto alternate=voice_design::ArticulationPlan::compileRecipe(alternateResource.value(),performance.value(),alternatePhones,"neutral"); CHECK(alternate);
      CHECK(alternate.value().gestures().front().kind==voice_design::ArticulationGestureKind::Plosive);
    }
    const auto& gesture=plan.value().gestures().front();
    CHECK(gesture.kind==voice_design::ArticulationGestureKind::Plosive); CHECK(gesture.plosive); CHECK(!gesture.frication);
    CHECK(gesture.plosive->closureFrames==rate*9U/100U); CHECK(gesture.plosive->burstFrames==rate/100U);
    const auto context=plan.value().context();
    auto stream=voice_design::ArticulatedStream::createFromRecipe(resource.value(),performance.value(),phones,"neutral",257U); CHECK(stream);
    const auto whole=stream.value().renderOwned(context); CHECK(whole);
    const auto closure=gesture.plosive->closureFrames;
    CHECK(std::all_of(whole.value().samples.begin(),whole.value().samples.begin()+closure,[](float sample){return sample==0.0F;}));
    CHECK(std::any_of(whole.value().samples.begin()+closure,whole.value().samples.begin()+rate/10U,[](float sample){return sample!=0.0F;}));
    CHECK(std::any_of(whole.value().samples.begin()+rate/10U,whole.value().samples.end(),[](float sample){return sample!=0.0F;}));
    stream.value().reset();
    const auto split=static_cast<time::SampleFrame>(closure+17U);
    const auto prefix=stream.value().renderOwned({context.start,split}); CHECK(prefix);
    auto checkpoint=stream.value();
    std::stop_source stop; stop.request_stop();
    CHECK(!stream.value().renderOwned({split,context.end},stop.get_token())); CHECK(stream.value().position()==split);
    const auto tail=stream.value().renderOwned({split,context.end}); CHECK(tail);
    CHECK(tail.value().samples==checkpoint.renderOwned({split,context.end}).value().samples);
    CHECK(std::equal(tail.value().samples.begin(),tail.value().samples.end(),whole.value().samples.begin()+split));
    stream.value().reset();
    CHECK(stream.value().renderOwned({split,context.end}).value().samples==tail.value().samples);
    auto changed=recipe; changed.plosives.front().burstMilliseconds=100.0;
    const auto tooLong=voice_design::freezeVoiceRecipeResource(changed); CHECK(tooLong);
    CHECK(!voice_design::ArticulationPlan::compileRecipe(tooLong.value(),performance.value(),phones,"neutral"));
    changed=recipe; changed.plosives.front().source.seed=43U;
    const auto other=voice_design::freezeVoiceRecipeResource(changed); CHECK(other);
    CHECK(!voice_design::ArticulatedStream::create(other.value(),performance.value(),plan.value(),"neutral"));
    const std::vector<voice_design::FricationBinding> fric{{"k",{.seed=42U}}};
    const std::vector<voice_design::PlosiveBinding> stops{{"k",{.seed=42U},10.0}};
    CHECK(!voice_design::ArticulationPlan::compile(phones,performance.value().phonemeTiming(),fric,rate,context,{},stops));
  }
}

TEST_CASE("plosive recipe bindings roundtrip and preserve existing recipe identities") {
  using namespace seam;
  auto recipe=nasalFixture();
  recipe.frications={{"s","neutral",{.seed=17U}}};
  const auto before=voice_design::encodeVoiceRecipe(recipe); CHECK(before);
  const auto original=voice_design::freezeVoiceRecipeResource(recipe); CHECK(original);
  recipe.plosives={{"k","neutral",{.seed=std::numeric_limits<std::uint64_t>::max(),.centerHz=3000.0,.bandwidthHz=1800.0,.gain=0.2},12.5}};
  const auto encoded=voice_design::encodeVoiceRecipe(recipe); CHECK(encoded);
  CHECK(voice_design::decodeVoiceRecipe(encoded.value()).value()==recipe);
  const auto frozen=voice_design::freezeVoiceRecipeResource(recipe); CHECK(frozen);
  CHECK(frozen.value().identity.version=="4"); CHECK(frozen.value().identity.contentHash!=original.value().identity.contentHash);
  CHECK(voice_design::decodeVoiceRecipeResource(frozen.value()).value()==recipe);
  const auto root=test::support::temporaryDirectory("plosive-recipe");
  CHECK(voice_design::saveVoiceRecipeFile(root/"recipe.json",recipe));
  CHECK(voice_design::loadVoiceRecipeResource(root/"recipe.json",frozen.value().identity));
  recipe.plosives.front().burstMilliseconds=20.0;
  CHECK(voice_design::freezeVoiceRecipeResource(recipe).value().identity!=frozen.value().identity);
  CHECK(voice_design::decodeVoiceRecipeResource(frozen.value()).value().plosives.front().burstMilliseconds==12.5);
  recipe.plosives.clear(); CHECK(voice_design::encodeVoiceRecipe(recipe).value()==before.value());
  auto wrong=frozen.value(); wrong.identity.version="3"; CHECK(!voice_design::decodeVoiceRecipeResource(wrong));
}

TEST_CASE("noise codas close voicing and preserve their distinct source envelopes at the phrase end") {
  using namespace seam;
  domain::Project project{domain::ProjectId{1U},"Released coda"};
  domain::VocalRegion region{.id=domain::RegionId{3U},.name="Coda",.durationTick=time::Tick{960},
      .lyrics={{domain::LyricTokenId{4U},U"a",domain::Language::Japanese}},
      .notes={{.id=domain::NoteId{5U},.durationTick=time::Tick{960},.midiKey=69U,.lyricTokenId=domain::LyricTokenId{4U}}}};
  for (const auto* symbol : {"p","t","k","s"}) {
    const bool frication=std::string_view{symbol}=="s";
    const std::vector<domain::PhonemeToken> phones{
        {.key={domain::NoteId{5U},0U},.symbol="a",.role=domain::PhonemeRole::Nucleus,.voiced=true,.timing={.startOffset=0,.endOffset=300000}},
        {.key={domain::NoteId{5U},1U},.symbol=symbol,.role=domain::PhonemeRole::Coda,.voiced=false,.timing={.startOffset=300000}}};
    auto recipe=nasalFixture();
    if (frication) recipe.frications={{symbol,"neutral",{.seed=42U}}};
    else recipe.plosives={{symbol,"neutral",{.seed=42U},10.0}};
    const auto resource=voice_design::freezeVoiceRecipeResource(recipe); CHECK(resource);
    const auto performance=synthesis::compileScorePerformance(project,region,48000U,phones); CHECK(performance);
    const auto plan=voice_design::ArticulationPlan::compileRecipe(resource.value(),performance.value(),phones,"neutral"); CHECK(plan);
    CHECK(plan.value().gestures().back().kind==(frication?voice_design::ArticulationGestureKind::Frication:voice_design::ArticulationGestureKind::Plosive));
    CHECK(plan.value().gestures().back().span.start==14400); CHECK(plan.value().gestures().back().span.end==24000);
    auto stream=voice_design::ArticulatedStream::createFromRecipe(resource.value(),performance.value(),phones,"neutral",257U); CHECK(stream);
    const auto whole=stream.value().renderOwned({0,24000}); CHECK(whole);
    if (frication) CHECK(std::any_of(whole.value().samples.begin()+14400,whole.value().samples.begin()+23520,[](float sample){return sample!=0.0F;}));
    else CHECK(std::all_of(whole.value().samples.begin()+14400,whole.value().samples.begin()+23520,[](float sample){return sample==0.0F;}));
    CHECK(std::any_of(whole.value().samples.begin()+23520,whole.value().samples.end(),[](float sample){return sample!=0.0F;}));
    CHECK(whole.value().samples.back()==0.0F);
    stream.value().reset(); const auto tail=stream.value().renderOwned({23600,24000}); CHECK(tail);
    CHECK(std::equal(tail.value().samples.begin(),tail.value().samples.end(),whole.value().samples.begin()+23600));
    auto unsupported=phones; unsupported.back().symbol="kcl";
    CHECK(!voice_design::ArticulationPlan::compileRecipe(resource.value(),performance.value(),unsupported,"neutral"));
    auto overlap=phones; overlap.front().timing.endOffset.reset();
    const auto badPerformance=synthesis::compileScorePerformance(project,region,48000U,overlap); CHECK(badPerformance);
    CHECK(!voice_design::ArticulationPlan::compileRecipe(resource.value(),badPerformance.value(),overlap,"neutral"));
  }
}

TEST_CASE("plosive recipes reject ambiguous bindings and malformed durations without silently downgrading") {
  using namespace seam;
  auto recipe=nasalFixture(); recipe.plosives={{"p","neutral",{.seed=42U},10.0}};
  const auto json=voice_design::encodeVoiceRecipe(recipe); CHECK(json);
  const auto original=formats::parseJson(json.value()).value();
  for (unsigned scenario=0U;scenario<6U;++scenario) {
    auto invalid=original;
    auto& pose=invalid.find("plosives")->asArray().front();
    if (scenario==0U) pose.asObject().erase("burstMilliseconds");
    if (scenario==1U) *pose.find("burstMilliseconds")=formats::JsonValue{0.0};
    if (scenario==2U) *pose.find("seed")=formats::JsonValue{42.0};
    if (scenario==3U) *pose.find("phone")=formats::JsonValue{"a"};
    if (scenario==4U) *invalid.find("schemaVersion")=formats::JsonValue{std::int64_t{3}};
    if (scenario==5U) invalid.find("plosives")->asArray().clear();
    CHECK(!voice_design::decodeVoiceRecipe(formats::stringifyJson(invalid)));
  }
  recipe.frications={{"p","neutral",{.seed=1U}}}; CHECK(!recipe.validate());
  recipe.frications.clear(); recipe.plosives.push_back(recipe.plosives.front()); CHECK(!recipe.validate());
  recipe.plosives.pop_back(); recipe.plosives.front().style="missing"; CHECK(!recipe.validate());
}

TEST_CASE("plosive source has exact closure and a finite decaying burst rather than sustained frication") {
  using namespace seam::voice_design;
  const PlosiveConfig config{{.seed=17U,.centerHz=3000.0,.bandwidthHz=1800.0,.gain=0.2},257U,480U};
  auto source=PlosiveSource::create(config,48000U,100); CHECK(source);
  CHECK(source.value().burstStart()==357); CHECK(source.value().end()==837);
  const auto audio=source.value().render(737U); CHECK(audio); CHECK(audio.value().startFrame==100);
  CHECK(std::all_of(audio.value().samples.begin(),audio.value().samples.begin()+258,[](float value) { return value==0.0F; }));
  CHECK(audio.value().samples.back()==0.0F);
  CHECK(std::any_of(audio.value().samples.begin()+258,audio.value().samples.end(),[](float value) { return std::abs(value)>0.000001F; }));
  CHECK(std::all_of(audio.value().samples.begin(),audio.value().samples.end(),[](float value) { return std::isfinite(value) && std::abs(value)<=1.0F; }));
  const auto noise=FricationSource::create(config.burst,48000U,357).value().render(480U); CHECK(noise);
  CHECK(std::vector<float>(audio.value().samples.begin()+257,audio.value().samples.end())!=noise.value().samples);
  double early=0.0,late=0.0;
  for (std::size_t i=48U;i<120U;++i) early+=audio.value().samples[257U+i]*audio.value().samples[257U+i];
  for (std::size_t i=360U;i<432U;++i) late+=audio.value().samples[257U+i]*audio.value().samples[257U+i];
  CHECK(early>late*100.0); CHECK(!source.value().render(1U));
}

TEST_CASE("plosive source chunks checkpoints reset and cancellation preserve exact source state") {
  using namespace seam::voice_design;
  const PlosiveConfig config{{.seed=42U},257U,480U};
  auto source=PlosiveSource::create(config,48000U,0).value();
  const auto whole=source.render(737U); CHECK(whole); source.reset();
  const auto closure=source.render(200U); CHECK(closure); auto checkpoint=source;
  std::vector<float> joined=closure.value().samples;
  while (source.position()<source.end()) {
    const auto block=source.render(static_cast<std::size_t>(std::min<seam::time::SampleFrame>(37,source.end()-source.position()))); CHECK(block);
    joined.insert(joined.end(),block.value().samples.begin(),block.value().samples.end());
  }
  CHECK(joined==whole.value().samples);
  std::stop_source stop; stop.request_stop(); CHECK(!checkpoint.render(537U,stop.get_token())); CHECK(checkpoint.position()==200);
  CHECK(!checkpoint.render(538U)); CHECK(checkpoint.position()==200);
  const auto tail=checkpoint.render(537U); CHECK(tail);
  CHECK(tail.value().samples==std::vector<float>(whole.value().samples.begin()+200,whole.value().samples.end()));
  source.reset(); CHECK(source.render(737U).value().samples==whole.value().samples);
}

TEST_CASE("plosive source rejects invalid spans spectra and clocks before rendering") {
  using namespace seam::voice_design;
  PlosiveConfig config{{.seed=42U,.centerHz=2000.0,.bandwidthHz=1000.0},10U,3U};
  for (const auto rate:{8000U,48000U,384000U}) {
    auto source=PlosiveSource::create(config,rate,0); CHECK(source); CHECK(source.value().render(13U));
  }
  CHECK(!PlosiveSource::create(config,0U,0)); CHECK(!PlosiveSource::create(config,48000U,-1));
  CHECK(!PlosiveSource::create(config,48000U,(seam::time::SampleFrame{1}<<52)-12));
  auto invalid=config; invalid.closureFrames=0U; CHECK(!PlosiveSource::create(invalid,48000U,0));
  invalid=config; invalid.burstFrames=2U; CHECK(!PlosiveSource::create(invalid,48000U,0));
  invalid=config; invalid.closureFrames=96000U; CHECK(!PlosiveSource::create(invalid,48000U,0));
  invalid=config; invalid.burst.centerHz=24000.0; CHECK(!PlosiveSource::create(invalid,48000U,0));
  invalid=config; invalid.burst.gain=std::numeric_limits<double>::quiet_NaN(); CHECK(!PlosiveSource::create(invalid,48000U,0));
  CHECK(!PlosiveSource::create(config,48000U,0).value().render(0U));
}

TEST_CASE("explicit nasal onset gestures render voiced audio with exact chunk and checkpoint continuity") {
  using namespace seam;
  domain::Project project{domain::ProjectId{1U},"Voiced onset timing"};
  domain::VocalRegion region{.id=domain::RegionId{3U},.name="Note",.durationTick=time::Tick{960},
      .lyrics={{domain::LyricTokenId{4U},U"ま",domain::Language::Japanese}},
      .notes={{.id=domain::NoteId{5U},.durationTick=time::Tick{960},.midiKey=69U,.lyricTokenId=domain::LyricTokenId{4U}}}};
  project.vocalTracks().push_back({.id=domain::TrackId{2U},.name="Singer",.regions={region}});
  const auto resolved=phonemizer::resolveJapanesePronunciation(region); CHECK(resolved);
  const auto& phones=resolved.value().pronunciation.tokens;
  CHECK(phones.size()==2U); CHECK(phones.front().symbol=="m"); CHECK(phones.front().voiced);
  const auto performance=synthesis::compileScorePerformance(project,region,48000U,phones,synthesis::PhonemeTimingPolicy::ProceduralInNote); CHECK(performance);
  CHECK(performance.value().phonemeTiming().front().inferredStartFrame==std::optional<time::SampleFrame>{0});
  CHECK(performance.value().phonemeTiming().back().nucleusFrame==2880);
  auto recipe=nasalFixture(); auto consonantPose=recipe.poses.front(); consonantPose.phone="m"; recipe.poses.push_back(consonantPose);
  const auto resource=voice_design::freezeVoiceRecipeResource(recipe); CHECK(resource);
  const auto plan=voice_design::ArticulationPlan::compileRecipe(resource.value(),performance.value(),phones,"neutral");
  CHECK(plan); CHECK(plan.value().gestures().front().kind==voice_design::ArticulationGestureKind::Nasal);
  CHECK(plan.value().gestures().front().span.end==2880);
  auto whole=voice_design::ArticulatedStream::createFromRecipe(resource.value(),performance.value(),phones,"neutral",127U); CHECK(whole);
  const auto rendered=whole.value().renderOwned({0,24000}); CHECK(rendered);
  CHECK(std::any_of(rendered.value().samples.begin()+240,rendered.value().samples.begin()+2640,[](float value) { return std::abs(value)>0.000001F; }));
  auto chunked=voice_design::ArticulatedStream::createFromRecipe(resource.value(),performance.value(),phones,"neutral",257U); CHECK(chunked);
  auto first=chunked.value().renderOwned({0,2980}); CHECK(first); auto checkpoint=chunked.value();
  const auto last=chunked.value().renderOwned({2980,24000}); CHECK(last);
  auto joined=first.value().samples; joined.insert(joined.end(),last.value().samples.begin(),last.value().samples.end()); CHECK(joined==rendered.value().samples);
  std::stop_source stop; stop.request_stop(); CHECK(!checkpoint.renderOwned({2980,24000},stop.get_token()));
  CHECK(checkpoint.renderOwned({2980,24000}).value().samples==last.value().samples);
  whole.value().reset(); CHECK(whole.value().renderOwned({0,24000}).value().samples==rendered.value().samples);
  recipe.poses.back().nasal.reset();
  CHECK(!voice_design::ArticulationPlan::compileRecipe(voice_design::freezeVoiceRecipeResource(recipe).value(),performance.value(),phones,"neutral"));
}

TEST_CASE("nasal consonant tract excludes oral output rather than relabeling a colored vowel") {
  using namespace seam::voice_design;
  auto recipe=nasalFixture(); auto nasal=recipe.poses.front(); nasal.phone="m"; recipe.poses.push_back(nasal);
  const auto input=seam::test::support::sineWave(48000U,220.0,0.1,0.2F);
  const auto voiced=VocalTract::create(recipe,"m","neutral",48000U).value().process(input); CHECK(voiced);
  const auto vowel=VocalTract::create(recipe,"a","neutral",48000U).value().process(input); CHECK(vowel);
  CHECK(voiced.value()!=vowel.value());
  recipe.poses.back().formants={{400.0,100.0,-10.0},{1800.0,200.0,4.0},{3200.0,300.0,-8.0}};
  CHECK(VocalTract::create(recipe,"m","neutral",48000U).value().process(input).value()==voiced.value());
  recipe.poses.back().nasalCoupling=0.0; CHECK(!VocalTract::create(recipe,"m","neutral",48000U));
}

TEST_CASE("nasal codas use inferred timing and preserve explicit nonoverlapping timing") {
  using namespace seam;
  domain::Project project{domain::ProjectId{1U},"Nasal coda"};
  domain::VocalRegion region{.id=domain::RegionId{3U},.name="Note",.durationTick=time::Tick{960},
      .lyrics={{domain::LyricTokenId{4U},U"あん",domain::Language::Japanese}},
      .notes={{.id=domain::NoteId{5U},.durationTick=time::Tick{960},.midiKey=69U,.lyricTokenId=domain::LyricTokenId{4U}}}};
  project.vocalTracks().push_back({.id=domain::TrackId{2U},.name="Singer",.regions={region}});
  auto recipe=nasalFixture(); auto pose=recipe.poses.front(); pose.phone="N"; recipe.poses.push_back(pose);
  const auto resource=voice_design::freezeVoiceRecipeResource(recipe); CHECK(resource);
  const auto prepare=[&] {
    const auto phones=phonemizer::resolveJapanesePronunciation(region); CHECK(phones);
    const auto performance=synthesis::compileScorePerformance(project,region,48000U,phones.value().pronunciation.tokens,synthesis::PhonemeTimingPolicy::ProceduralInNote); CHECK(performance);
    return voice_design::ArticulatedStream::createFromRecipe(resource.value(),performance.value(),phones.value().pronunciation.tokens,"neutral");
  };
  auto automatic=prepare(); CHECK(automatic); CHECK(automatic.value().renderOwned({0,24000}));
  region.phonemeOverrides={{.key={domain::NoteId{5U},0U},.timing={.endOffset=400000},.locked=true},
      {.key={domain::NoteId{5U},1U},.timing={.startOffset=400000},.locked=true}};
  auto stream=prepare(); CHECK(stream); const auto audio=stream.value().renderOwned({0,24000}); CHECK(audio);
  CHECK(std::any_of(audio.value().samples.begin()+20500,audio.value().samples.begin()+23500,[](float value) { return std::abs(value)>0.000001F; }));
  region.phonemeOverrides.front().timing.endOffset=450000; CHECK(!prepare());
  region.phonemeOverrides.clear(); region.lyrics.front().surface=U"ん";
  auto syllabic=prepare(); CHECK(syllabic);
  const auto held=syllabic.value().renderOwned({0,24000}); CHECK(held);
  CHECK(std::any_of(held.value().samples.begin(),held.value().samples.end(),[](float value) { return std::abs(value)>0.000001F; }));
  // Multiple nucleus-free tokens in one note still need an explicit allocation
  // contract; do not silently assign an arbitrary mora sequence here.
  region.lyrics.front().surface=U"んん"; CHECK(!prepare());
}

TEST_CASE("explicit nasal models roundtrip as schema three without relabeling legacy recipes") {
  using namespace seam::voice_design;
  auto recipe=nasalFixture();
  const auto encoded=encodeVoiceRecipe(recipe); CHECK(encoded);
  CHECK(decodeVoiceRecipe(encoded.value()).value()==recipe);
  const auto resource=freezeVoiceRecipeResource(recipe); CHECK(resource); CHECK(resource.value().identity.version=="3");
  CHECK(decodeVoiceRecipeResource(resource.value()).value()==recipe);
  const auto root=seam::test::support::temporaryDirectory("nasal-recipe");
  CHECK(saveVoiceRecipeFile(root/"recipe.json",recipe)); CHECK(loadVoiceRecipeResource(root/"recipe.json",resource.value().identity));
  recipe.poses[0].nasal->antiresonanceHz+=50.0;
  CHECK(freezeVoiceRecipeResource(recipe).value().identity!=resource.value().identity);
  recipe.poses[0].nasal.reset();
  CHECK(voiceRecipeSchemaVersion(recipe)==1); CHECK(!VocalTract::create(recipe,"a","neutral",48000U));
  recipe.poses[0].nasalCoupling=0.0;
  const auto old=encodeVoiceRecipe(recipe); CHECK(old); CHECK(old.value().find("\"nasal\"")==std::string::npos);
  CHECK(encodeVoiceRecipe(decodeVoiceRecipe(old.value()).value()).value()==old.value());
  auto malformed=seam::formats::parseJson(encoded.value()).value();
  malformed.asObject()["schemaVersion"]=std::int64_t{2}; CHECK(!decodeVoiceRecipe(seam::formats::stringifyJson(malformed)));
  recipe=nasalFixture(); recipe.poses[0].nasal->resonanceBandwidthHz=0.0; CHECK(!recipe.validate());
  recipe=nasalFixture(); recipe.poses[0].nasal->antiresonanceHz=std::numeric_limits<double>::quiet_NaN(); CHECK(!recipe.validate());
}

TEST_CASE("nasal tract creates measurable resonance and antiresonance with unchanged periodic pitch") {
  using namespace seam::voice_design;
  auto recipe=nasalFixture(); auto oral=recipe; oral.poses[0].nasalCoupling=0.0; oral.poses[0].nasal.reset();
  const auto tones=seam::test::support::sineWave(48000U,250.0,1.0,0.1F);
  auto input=tones; const auto upper=seam::test::support::sineWave(48000U,1000.0,1.0,0.1F);
  for (std::size_t i=0U;i<input.size();++i) input[i]+=upper[i];
  auto dry=VocalTract::create(oral,"a","neutral",48000U); CHECK(dry);
  auto wet=VocalTract::create(recipe,"a","neutral",48000U); CHECK(wet);
  const auto dryAudio=dry.value().process(input), wetAudio=wet.value().process(input); CHECK(dryAudio); CHECK(wetAudio);
  CHECK(measuredTone(wetAudio.value(),250.0)>measuredTone(dryAudio.value(),250.0)*2.0);
  CHECK(measuredTone(wetAudio.value(),1000.0)<measuredTone(dryAudio.value(),1000.0)*0.001);
  auto half=recipe; half.poses[0].nasalCoupling=0.5;
  const auto halfAudio=VocalTract::create(half,"a","neutral",48000U).value().process(input); CHECK(halfAudio);
  for (std::size_t i=0U;i<input.size();++i) CHECK_NEAR(halfAudio.value()[i],0.5*(dryAudio.value()[i]+wetAudio.value()[i]),0.0000001);
  auto bypass=recipe; bypass.poses[0].nasalCoupling=0.0;
  CHECK(VocalTract::create(bypass,"a","neutral",48000U).value().process(input).value()==dryAudio.value());
  const auto pure=seam::test::support::sineWave(48000U,200.0,1.0,0.2F);
  wet.value().reset(); const auto pitched=wet.value().process(pure); CHECK(pitched);
  std::size_t crossings=0U;
  for (std::size_t i=24001U;i<pitched.value().size();++i) if (pitched.value()[i-1U]<=0.0F && pitched.value()[i]>0.0F) ++crossings;
  CHECK(crossings>=99U && crossings<=101U);
}

TEST_CASE("nasal banks preserve transitions checkpoints reset and failed-block rollback") {
  using namespace seam::voice_design;
  auto recipe=nasalFixture(); auto second=recipe.poses.front(); second.phone="i";
  second.nasal->resonanceHz=350.0; second.nasal->antiresonanceHz=1600.0; second.nasalCoupling=0.4;
  recipe.poses.push_back(second);
  const auto input=seam::test::support::sineWave(48000U,230.0,0.1,0.2F);
  auto stream=VocalTract::create(recipe,"a","neutral",48000U).value();
  CHECK(stream.transitionTo(recipe,"i","neutral",511U)); auto blocks=stream;
  const auto whole=stream.process(input); CHECK(whole);
  std::vector<float> joined;
  for (std::size_t offset=0U;offset<input.size();offset+=37U) {
    const auto part=blocks.process(std::span<const float>{input}.subspan(offset,std::min<std::size_t>(37U,input.size()-offset)));
    CHECK(part); joined.insert(joined.end(),part.value().begin(),part.value().end());
  }
  CHECK(joined==whole.value()); CHECK(blocks.transitionFramesRemaining()==0U);
  blocks.reset(); CHECK(blocks.process(input).value()==VocalTract::create(recipe,"i","neutral",48000U).value().process(input).value());
  CHECK(blocks.transitionTo(recipe,"a","neutral",100U)); auto preserved=blocks;
  auto invalid=input; invalid.back()=std::numeric_limits<float>::infinity(); CHECK(!blocks.process(invalid));
  std::stop_source stop; stop.request_stop(); CHECK(!blocks.process(input,stop.get_token()));
  CHECK(blocks.process(input).value()==preserved.process(input).value());
  for (const auto rate : {8000U,44100U,192000U,384000U}) for (const auto width : {10.0,5000.0}) {
    auto extreme=recipe; extreme.poses[0].nasal->resonanceBandwidthHz=width; extreme.poses[0].nasal->antiresonanceBandwidthHz=width;
    auto tract=VocalTract::create(extreme,"a","neutral",rate); CHECK(tract);
    std::vector<float> impulse(16384U,0.0F); impulse[0]=1.0F;
    const auto response=tract.value().process(impulse); CHECK(response);
    CHECK(std::all_of(response.value().begin(),response.value().end(),[](float sample) { return std::isfinite(sample) && std::abs(sample)<=8.0F; }));
  }
  recipe.poses[0].nasal->antiresonanceHz=4000.0; CHECK(!VocalTract::create(recipe,"a","neutral",8000U));
}

TEST_CASE("nasal sustained rendering survives file reload and owned-window reconstruction") {
  using namespace seam;
  domain::Project project{domain::ProjectId{1U},"Nasal audition"};
  domain::VocalRegion region{.id=domain::RegionId{3U},.name="Note",.durationTick=time::Tick{960},
      .lyrics={{domain::LyricTokenId{4U},U"あ",domain::Language::Japanese}},
      .notes={{.id=domain::NoteId{5U},.durationTick=time::Tick{960},.midiKey=69U,.lyricTokenId=domain::LyricTokenId{4U}}}};
  project.vocalTracks().push_back({.id=domain::TrackId{2U},.name="Singer",.regions={region}});
  auto recipe=nasalFixture();
  const auto root=test::support::temporaryDirectory("nasal-audition"); CHECK(voice_design::saveVoiceRecipeFile(root/"recipe.json",recipe));
  const auto resource=voice_design::loadVoiceRecipeResource(root/"recipe.json"); CHECK(resource);
  const auto score=synthesis::compileScorePerformance(project,region,48000U); CHECK(score);
  const synthesis::PhraseOutputContract contract{48000U,{0,24000},{0,24000}};
  const auto whole=voice_design::renderSustainedPose(resource.value(),score.value(),"a","neutral",contract,127U); CHECK(whole);
  auto stream=voice_design::SustainedPoseStream::create(resource.value(),score.value(),"a","neutral",contract.context,257U); CHECK(stream);
  const auto first=stream.value().renderOwned({0,12345}); CHECK(first); auto checkpoint=stream.value();
  const auto last=stream.value().renderOwned({12345,24000}); CHECK(last);
  auto joined=first.value().audio.samples; joined.insert(joined.end(),last.value().audio.samples.begin(),last.value().audio.samples.end());
  CHECK(joined==whole.value().audio.samples); CHECK(checkpoint.renderOwned({12345,24000}).value().audio.samples==last.value().audio.samples);
  recipe.poses[0].nasalCoupling=0.0;
  const auto dry=voice_design::renderSustainedPose(voice_design::freezeVoiceRecipeResource(recipe).value(),score.value(),"a","neutral",contract); CHECK(dry);
  CHECK(dry.value().audio.samples!=whole.value().audio.samples);
}

TEST_CASE("frication recipes roundtrip in schema two while preserving canonical schema one identity") {
  using namespace seam;
  voice_design::VoiceRecipe recipe;
  recipe.id = "export-draft"; recipe.seed = 42U;
  recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
  const auto legacy = voice_design::freezeVoiceRecipeResource(recipe); CHECK(legacy);
  // Captured from the pre-schema-2 export fixture, not computed from a second new writer.
  CHECK(legacy.value().identity.contentHash == "37db127d94902b90880aa9643a4eefb2d816c309564b193650a42c47b830f171");
  CHECK(legacy.value().identity.version == "1");
  recipe.frications = {{"s", "neutral", {.seed = std::numeric_limits<std::uint64_t>::max()}}};
  const auto encoded = voice_design::encodeVoiceRecipe(recipe); CHECK(encoded);
  const auto decoded = voice_design::decodeVoiceRecipe(encoded.value()); CHECK(decoded); CHECK(decoded.value() == recipe);
  const auto frozen = voice_design::freezeVoiceRecipeResource(recipe); CHECK(frozen);
  CHECK(frozen.value().identity.version == "2"); CHECK(frozen.value().identity.contentHash != legacy.value().identity.contentHash);
  CHECK(voice_design::decodeVoiceRecipeResource(frozen.value()));
  auto wrongVersion = frozen.value(); wrongVersion.identity.version = "1";
  CHECK(!voice_design::decodeVoiceRecipeResource(wrongVersion));
  auto changed = recipe; changed.frications.front().source.gain = 0.1;
  CHECK(voice_design::freezeVoiceRecipeResource(changed).value().identity.contentHash != frozen.value().identity.contentHash);
  const auto root = test::support::temporaryDirectory("frication-recipe");
  CHECK(voice_design::saveVoiceRecipeFile(root / "recipe.json", recipe));
  CHECK(voice_design::loadVoiceRecipeResource(root / "recipe.json", frozen.value().identity));
  for (unsigned scenario = 0U; scenario < 6U; ++scenario) {
    auto json = formats::parseJson(encoded.value()).value();
    auto& entries = json.find("frications")->asArray();
    if (scenario == 0U) *json.find("schemaVersion") = formats::JsonValue{std::int64_t{1}};
    if (scenario == 1U) entries.clear();
    if (scenario == 2U) entries.push_back(entries.front());
    if (scenario == 3U) *entries.front().find("seed") = formats::JsonValue{"01"};
    if (scenario == 4U) *entries.front().find("style") = formats::JsonValue{"missing"};
    if (scenario == 5U) entries.front().asObject().erase("gain");
    CHECK(!voice_design::decodeVoiceRecipe(formats::stringifyJson(json)));
  }
  recipe.frications.clear();
  CHECK(voice_design::freezeVoiceRecipeResource(recipe).value().identity == legacy.value().identity);
}

TEST_CASE("recipe articulation prepares resolved score edits without caller supplied frication settings") {
  using namespace seam;
  domain::Project project{domain::ProjectId{1U}, "Recipe articulation"};
  domain::VocalRegion region{.id = domain::RegionId{3U}, .name = "Note", .durationTick = time::Tick{960},
      .lyrics = {{domain::LyricTokenId{4U}, U"さ", domain::Language::Japanese}},
      .notes = {{.id = domain::NoteId{5U}, .durationTick = time::Tick{960}, .midiKey = 69U,
                 .lyricTokenId = domain::LyricTokenId{4U}}}};
  voice_design::VoiceRecipe recipe;
  recipe.id = "prepared-articulation";
  recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
  recipe.frications = {{"s", "neutral", {.seed = 42U}}};
  const auto resource = voice_design::freezeVoiceRecipeResource(recipe); CHECK(resource);
  const auto prepare = [&](const voice_design::VoiceRecipe& inputRecipe, std::string style = "neutral") {
    const auto resolved = phonemizer::resolveJapanesePronunciation(region); CHECK(resolved);
    const auto performance = synthesis::compileScorePerformance(project, region, 48000U, resolved.value().pronunciation.tokens); CHECK(performance);
    const auto frozen = voice_design::freezeVoiceRecipeResource(inputRecipe); CHECK(frozen);
    return voice_design::ArticulationPlan::compileRecipe(frozen.value(), performance.value(), resolved.value().pronunciation.tokens, style);
  };
  CHECK(!prepare(recipe)); // No automatic onset policy has been qualified.
  region.phonemeOverrides = {
      {.key = {domain::NoteId{5U}, 0U}, .timing = {.startOffset = 0}, .locked = true},
      {.key = {domain::NoteId{5U}, 1U}, .timing = {.startOffset = 100000}, .locked = true}};
  const auto plan = prepare(recipe); CHECK(plan);
  CHECK(plan.value().gestures().size() == 2U);
  CHECK(plan.value().gestures()[0].span.start == 0); CHECK(plan.value().gestures()[0].span.end == 4800);
  CHECK(plan.value().gestures()[0].frication == std::optional{recipe.frications.front().source});
  const auto resolved = phonemizer::resolveJapanesePronunciation(region); CHECK(resolved);
  const auto& phones = resolved.value().pronunciation.tokens;
  const auto performance = synthesis::compileScorePerformance(project, region, 48000U, phones); CHECK(performance);
  auto prepared = voice_design::ArticulatedStream::createFromRecipe(resource.value(), performance.value(), phones, "neutral"); CHECK(prepared);
  auto explicitStream = voice_design::ArticulatedStream::create(resource.value(), performance.value(), plan.value(), "neutral"); CHECK(explicitStream);
  CHECK(prepared.value().renderOwned(plan.value().context()).value().samples ==
      explicitStream.value().renderOwned(plan.value().context()).value().samples);
  std::stop_source stop; stop.request_stop();
  CHECK(!voice_design::ArticulationPlan::compileRecipe(resource.value(), performance.value(), phones, "neutral", stop.get_token()));
  CHECK(!voice_design::ArticulatedStream::createFromRecipe(resource.value(), performance.value(), phones, "neutral", 512U, stop.get_token()));
  CHECK(!prepare(recipe, "missing"));
  auto missingBinding = recipe; missingBinding.frications.clear(); CHECK(!prepare(missingBinding));
  auto otherStyle = recipe; otherStyle.poses.push_back(otherStyle.poses.front()); otherStyle.poses.back().style = "soft";
  otherStyle.frications.front().style = "soft";
  CHECK(!prepare(otherStyle)); CHECK(prepare(otherStyle, "soft"));
  auto missingVowel = recipe; missingVowel.poses.front().phone = "i"; CHECK(!prepare(missingVowel));
  auto corrupt = resource.value(); corrupt.identity.contentHash = std::string(64U, '0');
  CHECK(!voice_design::ArticulationPlan::compileRecipe(corrupt, performance.value(), phones, "neutral"));
  // Recipe changes, not hidden caller settings, select the generated onset.
  auto changed = recipe; changed.frications.front().source.seed = 43U;
  CHECK(prepare(changed).value().gestures()[0].frication->seed == 43U);
  // A second score note without pronunciation must not silently disappear.
  auto second = region.notes.front(); second.id = domain::NoteId{6U}; second.startTick = time::Tick{1920};
  region.durationTick = time::Tick{2880}; region.notes.push_back(second);
  const auto partial = synthesis::compileScorePerformance(project, region, 48000U, phones); CHECK(partial);
  CHECK(!voice_design::ArticulationPlan::compileRecipe(resource.value(), partial.value(), phones, "neutral"));
  // An onset in a score gap cannot borrow the next note's pitch context.
  auto nextPhones = phones;
  for (auto& phone : nextPhones) phone.key.noteId = second.id;
  nextPhones[0].timing.startOffset = -10000;
  auto extendedPhones = phones; extendedPhones.insert(extendedPhones.end(), nextPhones.begin(), nextPhones.end());
  const auto extended = synthesis::compileScorePerformance(project, region, 48000U, extendedPhones); CHECK(extended);
  const std::vector<voice_design::FricationBinding> explicitBindings{{"s", recipe.frications.front().source}};
  CHECK(voice_design::ArticulationPlan::compile(extendedPhones, extended.value().phonemeTiming(), explicitBindings,
      48000U, {extended.value().notes().front().startFrame, extended.value().notes().back().endFrame}));
  CHECK(!voice_design::ArticulationPlan::compileRecipe(resource.value(), extended.value(), extendedPhones, "neutral"));
  // Unused frication presets do not impose their Nyquist requirements on vowels.
  region.notes.resize(1U); region.phonemeOverrides.clear(); region.lyrics.front().surface = U"あ";
  const auto vowel = phonemizer::resolveJapanesePronunciation(region); CHECK(vowel);
  const auto lowRate = synthesis::compileScorePerformance(project, region, 8000U, vowel.value().pronunciation.tokens); CHECK(lowRate);
  CHECK(voice_design::ArticulationPlan::compileRecipe(resource.value(), lowRate.value(), vowel.value().pronunciation.tokens, "neutral"));
}

TEST_CASE("articulation plans bind explicit frication to shared nucleus timing without guessing phones") {
  using namespace seam;
  domain::Project project{domain::ProjectId{1U}, "Articulation"};
  domain::VocalRegion region{.id = domain::RegionId{3U}, .name = "Note", .durationTick = time::Tick{960},
      .lyrics = {{domain::LyricTokenId{4U}, U"さ", domain::Language::Japanese}},
      .notes = {{.id = domain::NoteId{5U}, .durationTick = time::Tick{960}, .midiKey = 69U,
                 .lyricTokenId = domain::LyricTokenId{4U}}}};
  std::vector<domain::PhonemeToken> phones{
      {.key = {domain::NoteId{5U}, 0U}, .symbol = "s", .role = domain::PhonemeRole::Onset, .voiced = false,
       .timing = {.startOffset = 0}},
      {.key = {domain::NoteId{5U}, 1U}, .symbol = "a", .role = domain::PhonemeRole::Nucleus, .voiced = true,
       .timing = {.startOffset = 100000}}};
  const auto timing = synthesis::compilePhonemeTimingPlan(project, region, phones, 48000U); CHECK(timing);
  const synthesis::PhraseFrameRange context{0, timing.value().back().endFrame};
  const std::vector<voice_design::FricationBinding> bindings{{"s", {.seed = 42U}}};
  const auto plan = voice_design::ArticulationPlan::compile(phones, timing.value(), bindings, 48000U, context); CHECK(plan);
  CHECK(plan.value().gestures().size() == 2U);
  const auto& onset = plan.value().gestures()[0];
  CHECK(onset.kind == voice_design::ArticulationGestureKind::Frication);
  CHECK(onset.key == phones.front().key); CHECK(onset.span.start == 0); CHECK(onset.span.end == 4800);
  CHECK(onset.frication);
  CHECK(plan.value().gestures()[1].kind == voice_design::ArticulationGestureKind::OralVowel);
  CHECK(plan.value().gestures()[1].span.start == 4800);
  voice_design::VoiceRecipe recipe;
  recipe.id = "articulation-fixture";
  recipe.frications = {{"s", "neutral", {.seed = 42U}}};
  recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
  const auto resource = voice_design::freezeVoiceRecipeResource(recipe); CHECK(resource);
  const auto performance = synthesis::compileScorePerformance(project, region, 48000U, phones); CHECK(performance);
  auto mixed = voice_design::ArticulatedStream::create(resource.value(), performance.value(), plan.value(), "neutral", 257U); CHECK(mixed);
  auto differentRecipe = recipe; differentRecipe.frications.front().source.seed = 43U;
  const auto otherResource = voice_design::freezeVoiceRecipeResource(differentRecipe); CHECK(otherResource);
  CHECK(!voice_design::ArticulatedStream::create(otherResource.value(), performance.value(), plan.value(), "neutral"));
  const auto mixedWhole = mixed.value().renderOwned(context); CHECK(mixedWhole);
  CHECK(std::any_of(mixedWhole.value().samples.begin() + 6000, mixedWhole.value().samples.end(), [](float value) { return value != 0.0F; }));
  CHECK(mixedWhole.value().samples[4799U] == 0.0F); CHECK(mixedWhole.value().samples[4800U] == 0.0F);
  const auto vowelPitch = voicebank::analyzePitch(std::span<const float>{mixedWhole.value().samples}.subspan(8000U, 12000U),
      48000U, {.correlationMethod = voicebank::PitchCorrelationMethod::Fft}); CHECK(vowelPitch);
  CHECK_NEAR(voicebank::medianVoicedPitch(vowelPitch.value()), 440.0, 10.0);
  const auto diagnosticRoot = test::support::temporaryDirectory("articulated-stream");
  CHECK(voicebank::writeWav(diagnosticRoot / "explicit-frication-vowel.wav",
      {.sampleRate = 48000U, .channels = 1U, .sampleFormat = voicebank::WavSampleFormat::Float32}, mixedWhole.value().samples));
  for (const auto block : {1U, 512U, 4096U}) {
    auto renderer = voice_design::ArticulatedStream::create(resource.value(), performance.value(), plan.value(), "neutral", block); CHECK(renderer);
    const auto first = renderer.value().renderOwned({0, 4701}); CHECK(first);
    auto checkpoint = renderer.value();
    const auto last = renderer.value().renderOwned({4701, context.end}); CHECK(last);
    CHECK(checkpoint.renderOwned({4701, context.end}).value().samples == last.value().samples);
    auto joined = first.value().samples; joined.insert(joined.end(), last.value().samples.begin(), last.value().samples.end());
    CHECK(joined == mixedWhole.value().samples);
    renderer.value().reset();
    CHECK(renderer.value().renderOwned({4790, 5000}).value().samples == std::vector<float>(mixedWhole.value().samples.begin() + 4790, mixedWhole.value().samples.begin() + 5000));
    std::stop_source stop; stop.request_stop();
    CHECK(!renderer.value().renderOwned({5000, 6000}, stop.get_token())); CHECK(renderer.value().position() == 5000);
  }
  auto differentTiming = phones; differentTiming.back().timing.startOffset = 120000;
  const auto mismatched = synthesis::compileScorePerformance(project, region, 48000U, differentTiming); CHECK(mismatched);
  CHECK(!voice_design::ArticulatedStream::create(resource.value(), mismatched.value(), plan.value(), "neutral"));
  CHECK(region.dynamicsAutomation.replacePoints({{time::Tick{0}, 0.5F}}));
  const auto quietPerformance = synthesis::compileScorePerformance(project, region, 48000U, phones); CHECK(quietPerformance);
  auto quiet = voice_design::ArticulatedStream::create(resource.value(), quietPerformance.value(), plan.value(), "neutral"); CHECK(quiet);
  const auto quietAudio = quiet.value().renderOwned(context); CHECK(quietAudio);
  for (std::size_t index = 0U; index < mixedWhole.value().samples.size(); ++index) CHECK(quietAudio.value().samples[index] == mixedWhole.value().samples[index] * 0.5F);
  auto source = voice_design::FricationSource::create(*onset.frication, 48000U, onset.span.start); CHECK(source);
  const auto output = source.value().render(static_cast<std::size_t>(onset.span.end - onset.span.start)); CHECK(output);
  CHECK(output.value().startFrame == onset.span.start); CHECK(output.value().samples.size() == 4800U);
  auto stream = voice_design::FricationGestureStream::create(plan.value(), 257U); CHECK(stream);
  const auto entire = stream.value().renderOwned(context); CHECK(entire);
  CHECK(std::equal(entire.value().samples.begin(), entire.value().samples.begin() + 4800, mixedWhole.value().samples.begin()));
  CHECK(entire.value().samples.front() == 0.0F); CHECK(entire.value().samples[4799U] == 0.0F);
  CHECK(std::all_of(entire.value().samples.begin() + 4800, entire.value().samples.end(), [](float value) { return value == 0.0F; }));
  CHECK(std::any_of(entire.value().samples.begin() + 240, entire.value().samples.begin() + 4560, [](float value) { return value != 0.0F; }));
  for (const auto blockSize : {1U, 512U, 4096U}) {
    auto chunks = voice_design::FricationGestureStream::create(plan.value(), blockSize); CHECK(chunks);
    const auto a = chunks.value().renderOwned({0, 101}); CHECK(a);
    auto checkpoint = chunks.value();
    const auto b = chunks.value().renderOwned({101, 4701}); CHECK(b);
    CHECK(checkpoint.renderOwned({101, 4701}).value().samples == b.value().samples);
    const auto c = chunks.value().renderOwned({4701, context.end}); CHECK(c);
    auto combined = a.value().samples;
    combined.insert(combined.end(), b.value().samples.begin(), b.value().samples.end());
    combined.insert(combined.end(), c.value().samples.begin(), c.value().samples.end());
    CHECK(combined == entire.value().samples);
    chunks.value().reset();
    const auto tail = chunks.value().renderOwned({100, 300}); CHECK(tail);
    CHECK(tail.value().samples == std::vector<float>(entire.value().samples.begin() + 100, entire.value().samples.begin() + 300));
    std::stop_source stop; stop.request_stop();
    CHECK(!chunks.value().renderOwned({300, 400}, stop.get_token())); CHECK(chunks.value().position() == 300);
    CHECK(!chunks.value().renderOwned({0, 1})); CHECK(chunks.value().position() == 300);
  }
  const auto missingSource=voice_design::ArticulationPlan::compile(phones,timing.value(),{},48000U,context);
  CHECK(!missingSource); CHECK(missingSource.error().code==core::ErrorCode::Unsupported);
  CHECK(missingSource.error().message.find("Phone 's'")!=std::string::npos);
  CHECK(missingSource.error().message.find("selected style")!=std::string::npos);
  auto voicedPhones=phones; voicedPhones[0].symbol="z"; voicedPhones[0].voiced=true;
  const auto voicedTiming=synthesis::compilePhonemeTimingPlan(project,region,voicedPhones,48000U); CHECK(voicedTiming);
  const std::vector<voice_design::FricationBinding> mislabeledNoise{{"z",{.seed=42U}}};
  const auto unsupportedVoice=voice_design::ArticulationPlan::compile(voicedPhones,voicedTiming.value(),mislabeledNoise,48000U,context);
  CHECK(!unsupportedVoice); CHECK(unsupportedVoice.error().code==core::ErrorCode::Unsupported);
  CHECK(unsupportedVoice.error().message.find("Phone 'z'")!=std::string::npos);
  CHECK(unsupportedVoice.error().message.find("voiced articulation model")!=std::string::npos);
  const std::vector<voice_design::FricationBinding> voicedBinding{{"z",{.seed=42U},0.3}};
  const auto mixedPlan=voice_design::ArticulationPlan::compile(voicedPhones,voicedTiming.value(),voicedBinding,48000U,context);
  CHECK(mixedPlan); CHECK(mixedPlan.value().gestures()[0].kind==voice_design::ArticulationGestureKind::VoicedFrication);
  CHECK(mixedPlan.value().gestures()[0].voicingGain==std::optional<double>{0.3});
  auto voicedRecipe=nasalFixture(); voicedRecipe.poses[0].nasal.reset(); voicedRecipe.poses[0].nasalCoupling=0.0;
  voicedRecipe.poses.push_back(voicedRecipe.poses[0]); voicedRecipe.poses.back().phone="z";
  voicedRecipe.frications={{"z","neutral",{.seed=42U},0.3}};
  const auto mixedResource=voice_design::freezeVoiceRecipeResource(voicedRecipe); CHECK(mixedResource);
  const auto mixedPerformance=synthesis::compileScorePerformance(project,region,48000U,voicedPhones); CHECK(mixedPerformance);
  CHECK(voice_design::ArticulatedStream::create(mixedResource.value(),mixedPerformance.value(),mixedPlan.value(),"neutral"));
  auto mixedStream=voice_design::ArticulatedStream::create(mixedResource.value(),mixedPerformance.value(),mixedPlan.value(),"neutral",127U,true); CHECK(mixedStream);
  const auto mixedAudio=mixedStream.value().renderOwned(context); CHECK(mixedAudio);
  CHECK(std::all_of(mixedAudio.value().samples.begin(),mixedAudio.value().samples.end(),[](float sample){return std::isfinite(sample);}));
  mixedStream.value().reset();
  const auto mixedSeek=mixedStream.value().renderOwned({100,1000}); CHECK(mixedSeek);
  CHECK(std::equal(mixedSeek.value().samples.begin(),mixedSeek.value().samples.end(),mixedAudio.value().samples.begin()+100));
  const auto checkpoint=mixedStream.value();
  std::stop_source mixedCancel; mixedCancel.request_stop();
  CHECK(!mixedStream.value().renderOwned({1000,1500},mixedCancel.get_token())); CHECK(mixedStream.value().position()==1000);
  auto checkpointCopy=checkpoint;
  CHECK(mixedStream.value().renderOwned({1000,1500}).value().samples==checkpointCopy.renderOwned({1000,1500}).value().samples);
  auto otherBlocks=voice_design::ArticulatedStream::create(mixedResource.value(),mixedPerformance.value(),mixedPlan.value(),"neutral",31U,true); CHECK(otherBlocks);
  CHECK(otherBlocks.value().renderOwned(context).value().samples==mixedAudio.value().samples);
  auto changedVoicing=voicedRecipe; changedVoicing.frications[0].voicingGain=0.6;
  const auto changedResource=voice_design::freezeVoiceRecipeResource(changedVoicing); CHECK(changedResource);
  CHECK(!voice_design::ArticulatedStream::create(changedResource.value(),mixedPerformance.value(),mixedPlan.value(),"neutral",127U,true));
  const std::vector<voice_design::FricationBinding> louderBinding{{"z",{.seed=42U},0.6}};
  const auto louderPlan=voice_design::ArticulationPlan::compile(voicedPhones,voicedTiming.value(),louderBinding,48000U,context); CHECK(louderPlan);
  auto louderStream=voice_design::ArticulatedStream::create(changedResource.value(),mixedPerformance.value(),louderPlan.value(),"neutral",127U,true); CHECK(louderStream);
  const auto louderAudio=louderStream.value().renderOwned(context); CHECK(louderAudio);
  CHECK(!std::equal(louderAudio.value().samples.begin()+300,louderAudio.value().samples.begin()+4000,mixedAudio.value().samples.begin()+300));
  auto recipeStream=voice_design::ArticulatedStream::createFromRecipe(mixedResource.value(),mixedPerformance.value(),voicedPhones,"neutral",127U,{},true); CHECK(recipeStream);
  CHECK(recipeStream.value().renderOwned(context).value().samples==mixedAudio.value().samples);
  CHECK(voice_design::ArticulatedStream::createFromRecipe(mixedResource.value(),mixedPerformance.value(),voicedPhones,"neutral"));
  CHECK(!voice_design::ArticulatedStream::createFromRecipe(mixedResource.value(),mixedPerformance.value(),voicedPhones,"missing",127U,{},true));
  CHECK(!voice_design::ArticulatedStream::createFromRecipe(mixedResource.value(),mixedPerformance.value(),voicedPhones,"neutral",127U,mixedCancel.get_token(),true));
  std::vector<domain::PhonemeToken> voicedCoda{
      {.key={domain::NoteId{5U},0U},.symbol="a",.role=domain::PhonemeRole::Nucleus,.voiced=true,.timing={.startOffset=0,.endOffset=400000}},
      {.key={domain::NoteId{5U},1U},.symbol="z",.role=domain::PhonemeRole::Coda,.voiced=true,.timing={.startOffset=400000}}};
  const auto codaPerformance=synthesis::compileScorePerformance(project,region,48000U,voicedCoda); CHECK(codaPerformance);
  auto codaStream=voice_design::ArticulatedStream::createFromRecipe(mixedResource.value(),codaPerformance.value(),voicedCoda,"neutral",127U,{},true); CHECK(codaStream);
  const auto codaAudio=codaStream.value().renderOwned(context); CHECK(codaAudio);
  CHECK(codaAudio.value().samples!=mixedAudio.value().samples);
  codaStream.value().reset();
  const auto codaCrop=codaStream.value().renderOwned({19500,23000}); CHECK(codaCrop);
  CHECK(std::equal(codaCrop.value().samples.begin(),codaCrop.value().samples.end(),codaAudio.value().samples.begin()+19500));
  auto changedNoiseRecipe=voicedRecipe; changedNoiseRecipe.frications[0].source.seed=17U;
  const auto changedNoiseResource=voice_design::freezeVoiceRecipeResource(changedNoiseRecipe); CHECK(changedNoiseResource);
  auto changedNoiseStream=voice_design::ArticulatedStream::createFromRecipe(changedNoiseResource.value(),codaPerformance.value(),voicedCoda,"neutral",31U,{},true); CHECK(changedNoiseStream);
  const auto changedNoiseAudio=changedNoiseStream.value().renderOwned(context); CHECK(changedNoiseAudio);
  CHECK(std::equal(changedNoiseAudio.value().samples.begin(),changedNoiseAudio.value().samples.begin()+19200,codaAudio.value().samples.begin()));
  CHECK(!std::equal(changedNoiseAudio.value().samples.begin()+19500,changedNoiseAudio.value().samples.end(),codaAudio.value().samples.begin()+19500));
  CHECK(voice_design::isVoicedGesture(mixedPlan.value().gestures()[0].kind));
  CHECK(voice_design::isNoiseGesture(mixedPlan.value().gestures()[0].kind));
  auto mixedNoise=voice_design::FricationGestureStream::create(mixedPlan.value(),127U); CHECK(mixedNoise);
  const auto mixedPcm=mixedNoise.value().renderOwned(context); CHECK(mixedPcm);
  auto plainNoise=voice_design::FricationGestureStream::create(plan.value(),512U); CHECK(plainNoise);
  const auto plainPcm=plainNoise.value().renderOwned(context); CHECK(plainPcm);
  CHECK(mixedPcm.value().samples==plainPcm.value().samples); // Same noise, not omitted because voiced.
  auto seekNoise=voice_design::FricationGestureStream::create(mixedPlan.value(),31U); CHECK(seekNoise);
  const auto seekPcm=seekNoise.value().renderOwned({100,1000}); CHECK(seekPcm);
  CHECK(std::equal(seekPcm.value().samples.begin(),seekPcm.value().samples.end(),mixedPcm.value().samples.begin()+100));
  auto unvoicedZ=voicedPhones; unvoicedZ[0].voiced=false;
  const auto unvoicedTiming=synthesis::compilePhonemeTimingPlan(project,region,unvoicedZ,48000U); CHECK(unvoicedTiming);
  CHECK(!voice_design::ArticulationPlan::compile(unvoicedZ,unvoicedTiming.value(),voicedBinding,48000U,context));
  auto badVoicing=voicedBinding; badVoicing[0].voicingGain=0.0;
  CHECK(!voice_design::ArticulationPlan::compile(voicedPhones,voicedTiming.value(),badVoicing,48000U,context));
  for (unsigned scenario = 0U; scenario < 6U; ++scenario) {
    auto badTiming = timing.value(); auto badPhones = phones;
    if (scenario == 0U) badTiming[0].explicitStartFrame.reset();
    if (scenario == 1U) { badTiming[0].endExplicit = true; badTiming[0].endFrame = 6000; }
    if (scenario == 2U) badTiming[1] = badTiming[0];
    if (scenario == 3U) badTiming[1].voiced = false;
    if (scenario == 4U) badPhones[0].symbol = "k";
    if (scenario == 5U) badTiming[0].nucleusKey.reset();
    CHECK(!voice_design::ArticulationPlan::compile(badPhones, badTiming, bindings, 48000U, context));
  }
  auto duplicates = bindings; duplicates.push_back(bindings.front());
  CHECK(!voice_design::ArticulationPlan::compile(phones, timing.value(), duplicates, 48000U, context));
  // Synthetic anchor stress case: tiny ownership must not hide huge prefix work.
  auto longTiming = timing.value();
  for (auto& anchor : longTiming) anchor.nucleusFrame = 100000000;
  longTiming.back().explicitStartFrame = 100000000;
  longTiming.back().endFrame = 100000001;
  const auto longPlan = voice_design::ArticulationPlan::compile(phones, longTiming, bindings, 48000U, {0, 100000001}); CHECK(longPlan);
  auto longStream = voice_design::FricationGestureStream::create(longPlan.value()); CHECK(longStream);
  CHECK(!longStream.value().renderOwned({99999999, 100000001}));
  CHECK(longStream.value().position() == 0);
}

TEST_CASE("frication excitation is deterministic chunk invariant and cancellation safe") {
  using namespace seam::voice_design;
  auto source = FricationSource::create({.seed = 42U}, 48000U, 123); CHECK(source);
  auto chunked = source.value();
  const auto whole = source.value().render(48000U); CHECK(whole);
  std::vector<float> joined;
  for (std::size_t offset = 0U; offset < 48000U; offset += 257U) {
    const auto block = chunked.render(std::min<std::size_t>(257U, 48000U - offset)); CHECK(block);
    CHECK(block.value().startFrame == 123 + static_cast<seam::time::SampleFrame>(offset));
    joined.insert(joined.end(), block.value().samples.begin(), block.value().samples.end());
  }
  CHECK(joined == whole.value().samples);
  source.value().reset(); CHECK(source.value().render(48000U).value().samples == joined);
  auto checkpoint = chunked;
  CHECK(chunked.render(333U).value().samples == checkpoint.render(333U).value().samples);
  const auto position = chunked.position();
  std::stop_source stop; stop.request_stop();
  CHECK(!chunked.render(1000U, stop.get_token())); CHECK(chunked.position() == position);
  CHECK(!chunked.render(0U)); CHECK(chunked.position() == position);
  const auto other = FricationSource::create({.seed = 43U}, 48000U, 123); CHECK(other);
  auto different = other.value(); CHECK(different.render(48000U).value().samples != joined);
  const auto stats = seam::voicebank::analyzeAudio(joined);
  CHECK(stats.rms > 1e-4); CHECK(stats.peak < 1.0F);
  const auto bandEnergy = [&](double center) {
    double energy = 0.0;
    for (const auto offset : {-150.0, -50.0, 50.0, 150.0}) {
      double real = 0.0, imaginary = 0.0;
      for (std::size_t index = 0U; index < joined.size(); ++index) {
        const auto phase = 2.0 * std::numbers::pi * (center + offset) * static_cast<double>(index) / 48000.0;
        real += joined[index] * std::cos(phase); imaginary += joined[index] * std::sin(phase);
      }
      energy += real * real + imaginary * imaginary;
    }
    return energy;
  };
  CHECK(bandEnergy(5000.0) > 5.0 * bandEnergy(400.0));
  const auto outputRoot = seam::test::support::temporaryDirectory("frication-source");
  CHECK(seam::voicebank::writeWav(outputRoot / "aperiodic-source.wav",
      {.sampleRate = 48000U, .channels = 1U, .sampleFormat = seam::voicebank::WavSampleFormat::Float32}, joined));
}

TEST_CASE("frication excitation rejects unsafe configurations and stays finite at supported extremes") {
  using namespace seam::voice_design;
  CHECK(!FricationSource::create({.centerHz = std::numeric_limits<double>::quiet_NaN()}, 48000U, 0));
  CHECK(!FricationSource::create({.gain = 1.0}, 48000U, 0));
  CHECK(!FricationSource::create({}, 8000U, 0)); // No Nyquist clamping disguised as requested timbre.
  CHECK(!FricationSource::create({}, 48000U, -1));
  for (const auto rate : {8000U, 48000U, 192000U}) {
    for (const auto q : {0.25, 20.0}) {
      const auto center = static_cast<double>(rate) * 0.1;
      auto source = FricationSource::create({.seed = 7U, .centerHz = center, .bandwidthHz = center / q, .gain = 0.25}, rate, 0);
      CHECK(source);
      const auto rendered = source.value().render(16384U); CHECK(rendered);
      CHECK(std::all_of(rendered.value().samples.begin(), rendered.value().samples.end(), [](float value) {
        return std::isfinite(value) && std::abs(value) <= 1.0F;
      }));
    }
  }
}

TEST_CASE("recipe files save atomically and reload only the requested immutable identity") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("recipe-files");
  const auto path = root / "singer.voice-recipe.json";
  voice_design::VoiceRecipe recipe;
  recipe.id = "saved-draft"; recipe.seed = std::numeric_limits<std::uint64_t>::max();
  recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
  CHECK(voice_design::saveVoiceRecipeFile(path, recipe));
  const auto frozen = voice_design::freezeVoiceRecipeResource(recipe); CHECK(frozen);
  const auto loaded = voice_design::loadVoiceRecipeResource(path, frozen.value().identity); CHECK(loaded);
  CHECK(voice_design::decodeVoiceRecipeResource(loaded.value()).value() == recipe);
  const auto original = core::readTextFileLimited(path, 512U * 1024U); CHECK(original);
  auto changed = recipe; ++changed.phonation.openQuotient;
  CHECK(!voice_design::saveVoiceRecipeFile(path, changed));
  CHECK(core::readTextFileLimited(path, 512U * 1024U).value() == original.value());
  changed = recipe; changed.seed = 42U;
  const core::AtomicWriteOptions fault{.backupPath = {}, .maximumBackupBytes = 512U * 1024U,
      .faultInjector = [](core::AtomicWriteStage stage) {
        return stage == core::AtomicWriteStage::BeforeReplace ? core::failure(core::ErrorCode::IoError, "Injected recipe save failure") : core::success();
      }};
  CHECK(!voice_design::saveVoiceRecipeFile(path, changed, fault));
  CHECK(core::readTextFileLimited(path, 512U * 1024U).value() == original.value());
  std::stop_source stop; stop.request_stop();
  CHECK(!voice_design::saveVoiceRecipeFile(path, changed, {}, stop.get_token()));
  CHECK(!voice_design::loadVoiceRecipeResource(path, {}, stop.get_token()));
  CHECK(core::readTextFileLimited(path, 512U * 1024U).value() == original.value());
  CHECK(voice_design::saveVoiceRecipeFile(path, changed));
  CHECK(!voice_design::loadVoiceRecipeResource(path, frozen.value().identity));
  CHECK(voice_design::decodeVoiceRecipeResource(loaded.value()).value() == recipe);
  CHECK(voice_design::decodeVoiceRecipeResource(voice_design::loadVoiceRecipeResource(path).value()).value() == changed);
  CHECK(core::durableAtomicWriteText(path, "\n" + original.value() + "\n"));
  CHECK(voice_design::loadVoiceRecipeResource(path, frozen.value().identity));
  CHECK(core::durableAtomicWriteText(path, std::string(512U * 1024U + 1U, ' ')));
  CHECK(!voice_design::loadVoiceRecipeResource(path));
  CHECK(core::durableAtomicWriteText(path, "{\"schemaVersion\":999}"));
  CHECK(!voice_design::loadVoiceRecipeResource(path));
  CHECK(core::readTextFileLimited(path, 512U * 1024U).value() == "{\"schemaVersion\":999}");
  std::filesystem::create_symlink(path, root / "link.json");
  CHECK(!voice_design::loadVoiceRecipeResource(root / "link.json"));
  CHECK(!voice_design::saveVoiceRecipeFile(root / "link.json", recipe));
}

TEST_CASE("sustained procedural audition composes source tract gains and owned windows") {
  using namespace seam;
  domain::Project project{domain::ProjectId{1U}, "Sustained pose"};
  domain::VocalRegion region{.id = domain::RegionId{3U}, .name = "Note", .durationTick = time::Tick{960},
      .lyrics = {{domain::LyricTokenId{4U}, U"あ", domain::Language::Japanese}},
      .notes = {{.id = domain::NoteId{5U}, .durationTick = time::Tick{960}, .midiKey = 69U,
                 .lyricTokenId = domain::LyricTokenId{4U}}}};
  project.vocalTracks().push_back({.id = domain::TrackId{2U}, .name = "Singer", .regions = {region}});
  voice_design::VoiceRecipe recipe;
  recipe.id = "audition"; recipe.seed = 42U;
  recipe.modulation = {5.0, 0.1, 4.0};
  recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
  const auto frozen = voice_design::freezeVoiceRecipeResource(recipe); CHECK(frozen);
  const auto score = synthesis::compileScorePerformance(project, region, 48000U); CHECK(score);
  const synthesis::PhraseOutputContract output{48000U, {0, 24000}, {0, 24000}};
  const auto full = voice_design::renderSustainedPose(frozen.value(), score.value(), "a", "neutral", output, 127U);
  CHECK(full); CHECK(full.value().audio.samples.size() == 24000U);
  CHECK(full.value().processedFrames == 24000U);
  CHECK(full.value().resource == frozen.value().identity);
  CHECK(full.value().posePhone == "a"); CHECK(full.value().style == "neutral");
  const auto large = voice_design::renderSustainedPose(frozen.value(), score.value(), "a", "neutral", output, 4096U);
  CHECK(large); CHECK(large.value().audio.samples == full.value().audio.samples);
  auto releasedRegion = region;
  releasedRegion.performance.takes = {{.id = "release", .sourceRegionId = region.id,
      .resource = frozen.value().identity,
      .pronunciation = {domain::Language::Japanese, "fixture", "1", std::string(64U, 'b'), std::string(64U, 'c'), std::string(64U, 'd')},
      .generatorId = "fixture", .generatorVersion = "1", .range = {time::Tick{0}, time::Tick{960}},
      .lanes = {{domain::PerformanceChannel::Release, {{time::Tick{0}, 100.0}}}}}};
  releasedRegion.performance.accepted = {{"release", domain::PerformanceChannel::Release, region.notes.front().id, time::Tick{0}}};
  const auto releasedScore = synthesis::compileScorePerformance(project, releasedRegion, 48000U); CHECK(releasedScore);
  const auto released = voice_design::renderSustainedPose(frozen.value(), releasedScore.value(), "a", "neutral", output); CHECK(released);
  for (std::size_t i = 0; i < full.value().audio.samples.size(); ++i) {
    const auto gain = releasedScore.value().at(static_cast<time::SampleFrame>(i)).articulationGain;
    CHECK(released.value().audio.samples[i] == static_cast<float>(static_cast<double>(full.value().audio.samples[i]) * gain));
  }
  auto stream = voice_design::SustainedPoseStream::create(frozen.value(), score.value(), "a", "neutral", output.context, 257U);
  CHECK(stream);
  const auto firstChunk = stream.value().renderOwned({0, 12000}); CHECK(firstChunk);
  CHECK(firstChunk.value().processedFrames == 12000U); CHECK(stream.value().position() == 12000);
  auto checkpoint = stream.value();
  auto skipped = checkpoint;
  const auto afterGap = skipped.renderOwned({18000, 21000}); CHECK(afterGap);
  CHECK(afterGap.value().processedFrames == 9000U);
  CHECK(afterGap.value().audio.startFrame == 18000); CHECK(skipped.position() == 21000);
  CHECK(afterGap.value().audio.samples == std::vector<float>(full.value().audio.samples.begin() + 18000,
      full.value().audio.samples.begin() + 21000));
  CHECK(checkpoint.position() == 12000);
  CHECK(!skipped.renderOwned({21000, 24001})); CHECK(skipped.position() == 21000);
  const auto afterGapTail = skipped.renderOwned({21000, 24000}); CHECK(afterGapTail);
  CHECK(afterGapTail.value().processedFrames == 3000U);
  CHECK(afterGapTail.value().audio.samples == std::vector<float>(full.value().audio.samples.begin() + 21000,
      full.value().audio.samples.end()));
  CHECK(!stream.value().renderOwned({1000, 2000})); CHECK(stream.value().position() == 12000);
  std::stop_source streamStop; streamStop.request_stop();
  CHECK(!stream.value().renderOwned({12000, 24000}, streamStop.get_token()));
  CHECK(stream.value().position() == 12000);
  const auto secondChunk = stream.value().renderOwned({12000, 24000}); CHECK(secondChunk);
  CHECK(secondChunk.value().processedFrames == 12000U); CHECK(stream.value().position() == 24000);
  auto combined = firstChunk.value().audio.samples;
  combined.insert(combined.end(), secondChunk.value().audio.samples.begin(), secondChunk.value().audio.samples.end());
  CHECK(combined == full.value().audio.samples);
  CHECK(checkpoint.renderOwned({12000, 24000}).value().audio.samples == secondChunk.value().audio.samples);
  stream.value().reset(); CHECK(stream.value().position() == 0);
  CHECK(stream.value().renderOwned({0, 24000}).value().audio.samples == full.value().audio.samples);
  auto window = output; window.owned = {4000, 9000};
  const auto slice = voice_design::renderSustainedPose(frozen.value(), score.value(), "a", "neutral", window);
  CHECK(slice); CHECK(slice.value().audio.startFrame == 4000);
  CHECK(slice.value().processedFrames == 9000U);
  CHECK(slice.value().audio.samples == std::vector<float>(full.value().audio.samples.begin() + 4000,
      full.value().audio.samples.begin() + 9000));
  for (const auto blockSize : {1U, 257U, 4096U}) {
    auto tailWindow = output; tailWindow.owned = {23990, 24000};
    const auto tail = voice_design::renderSustainedPose(frozen.value(), score.value(), "a", "neutral", tailWindow, blockSize);
    CHECK(tail); CHECK(tail.value().processedFrames == 24000U);
    CHECK(tail.value().audio.samples == std::vector<float>(full.value().audio.samples.end() - 10, full.value().audio.samples.end()));
  }
  CHECK(region.dynamicsAutomation.replacePoints({{time::Tick{0}, 0.5F}}));
  const auto quietScore = synthesis::compileScorePerformance(project, region, 48000U); CHECK(quietScore);
  const auto quiet = voice_design::renderSustainedPose(frozen.value(), quietScore.value(), "a", "neutral", output);
  CHECK(quiet);
  for (std::size_t i = 0; i < full.value().audio.samples.size(); ++i) CHECK(quiet.value().audio.samples[i] == full.value().audio.samples[i] * 0.5F);
  region.notes[0].articulation = domain::NoteArticulation::Staccato;
  const auto shortScore = synthesis::compileScorePerformance(project, region, 48000U); CHECK(shortScore);
  const auto shortAudio = voice_design::renderSustainedPose(frozen.value(), shortScore.value(), "a", "neutral", output);
  CHECK(shortAudio);
  CHECK(std::all_of(shortAudio.value().audio.samples.begin() + 12000, shortAudio.value().audio.samples.end(),
      [](float value) { return value == 0.0F; }));
  CHECK(!voice_design::renderSustainedPose(frozen.value(), score.value(), "missing", "neutral", output));
  CHECK(!voice_design::renderSustainedPose(frozen.value(), score.value(), "a", "neutral", output, 0U));
  auto wrongRate = output; wrongRate.sampleRate = 44100U;
  CHECK(!voice_design::renderSustainedPose(frozen.value(), score.value(), "a", "neutral", wrongRate));
  std::stop_source stop; stop.request_stop();
  CHECK(!voice_design::renderSustainedPose(frozen.value(), score.value(), "a", "neutral", output, 512U, stop.get_token()));
  auto failingRegion = region;
  failingRegion.notes[0].articulation = domain::NoteArticulation::Normal;
  auto highNote = failingRegion.notes[0]; highNote.id = domain::NoteId{6U};
  highNote.startTick = time::Tick{960}; highNote.midiKey = 127U;
  failingRegion.notes.push_back(highNote); failingRegion.durationTick = time::Tick{1920};
  const auto failureScore = synthesis::compileScorePerformance(project, failingRegion, 8000U); CHECK(failureScore);
  auto failedStream = voice_design::SustainedPoseStream::create(frozen.value(), failureScore.value(), "a", "neutral", {0, 8000});
  CHECK(failedStream); CHECK(!failedStream.value().renderOwned({0, 8000}));
  CHECK(failedStream.value().position() == 0);
  const auto prefix = failedStream.value().renderOwned({0, 2000}); CHECK(prefix);
  const auto freshPrefix = voice_design::renderSustainedPose(frozen.value(), failureScore.value(), "a", "neutral",
      {8000U, {0, 8000}, {0, 2000}});
  CHECK(freshPrefix); CHECK(prefix.value().audio.samples == freshPrefix.value().audio.samples);
  auto prefixCheckpoint = failedStream.value();
  CHECK(!failedStream.value().renderOwned({2000, 8000}));
  CHECK(failedStream.value().position() == 2000);
  const auto recovered = failedStream.value().renderOwned({2000, 3500}); CHECK(recovered);
  const auto replayed = prefixCheckpoint.renderOwned({2000, 3500}); CHECK(replayed);
  CHECK(recovered.value().processedFrames == 1500U);
  CHECK(recovered.value().audio.samples == replayed.value().audio.samples);
}

TEST_CASE("phonation follows compiled melody with deterministic block-continuous excitation") {
  using namespace seam;
  domain::Project project{domain::ProjectId{1U}, "Phonation"};
  domain::VocalRegion region{.id = domain::RegionId{3U}, .name = "Melody", .durationTick = time::Tick{1920},
      .lyrics = {{domain::LyricTokenId{4U}, U"あ", domain::Language::Japanese}},
      .notes = {{.id = domain::NoteId{5U}, .durationTick = time::Tick{960}, .midiKey = 60U, .lyricTokenId = domain::LyricTokenId{4U}},
                {.id = domain::NoteId{6U}, .startTick = time::Tick{960}, .durationTick = time::Tick{960}, .midiKey = 67U,
                 .lyricTokenId = domain::LyricTokenId{4U}}}};
  project.vocalTracks().push_back({.id = domain::TrackId{2U}, .name = "Singer", .regions = {region}});
  const auto score = synthesis::compileScorePerformance(project, region, 48000U); CHECK(score);
  voice_design::VoiceRecipe recipe;
  recipe.id = "source-fixture"; recipe.phonation.aspiration = 0.0;
  recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
  auto source = voice_design::PhonationSource::create(recipe, score.value(), 0); CHECK(source);
  const auto whole = source.value().render(48000U); CHECK(whole);
  CHECK(whole.value().startFrame == 0); CHECK(source.value().position() == 48000);
  for (const auto [offset, expected] : {std::pair{4800U, 261.625565}, std::pair{28800U, 391.995436}}) {
    const auto measured = voicebank::analyzePitch(std::span<const float>{whole.value().samples}.subspan(offset, 12000U), 48000U);
    CHECK(measured); CHECK_NEAR(voicebank::medianVoicedPitch(measured.value()), expected, 4.0);
  }
  recipe.phonation.aspiration = 0.3; recipe.modulation = {10.0, 0.1, 5.0}; recipe.seed = 123U;
  auto noisy = voice_design::PhonationSource::create(recipe, score.value(), 0); CHECK(noisy);
  auto blocked = noisy.value();
  const auto noisyWhole = noisy.value().render(4800U); CHECK(noisyWhole);
  std::vector<float> joined;
  while (joined.size() < 4800U) {
    const auto next = blocked.render(std::min<std::size_t>(127U, 4800U - joined.size()));
    CHECK(next); CHECK(next.value().startFrame == static_cast<time::SampleFrame>(joined.size()));
    joined.insert(joined.end(), next.value().samples.begin(), next.value().samples.end());
  }
  CHECK(joined == noisyWhole.value().samples);
  noisy.value().reset(); CHECK(noisy.value().render(4800U).value().samples == joined);
  ++recipe.seed;
  auto changed = voice_design::PhonationSource::create(recipe, score.value(), 0); CHECK(changed);
  CHECK(changed.value().render(4800U).value().samples != joined);
  const auto before = blocked.position();
  CHECK(!blocked.render(0U));
  std::stop_source stop; stop.request_stop(); CHECK(!blocked.render(100U, stop.get_token()));
  CHECK(blocked.position() == before);
  const auto silence = source.value().render(128U); CHECK(silence);
  CHECK(std::all_of(silence.value().samples.begin(), silence.value().samples.end(), [](float value) { return value == 0.0F; }));
  auto toneRegion = region; toneRegion.notes.resize(1U); toneRegion.notes[0].midiKey = 69U;
  CHECK(toneRegion.pitchAutomation.upsert({.tick = time::Tick{0},
      .cents = static_cast<float>(1200.0 * std::log2(2700.0 / 440.0))}));
  const auto toneScore = synthesis::compileScorePerformance(project, toneRegion, 8000U); CHECK(toneScore);
  auto pureRecipe = recipe; pureRecipe.phonation.aspiration = 0.0; pureRecipe.modulation = {};
  auto bandlimited = voice_design::PhonationSource::create(pureRecipe, toneScore.value(), 0); CHECK(bandlimited);
  const auto tone = bandlimited.value().render(2000U); CHECK(tone);
  const auto component = [&](double frequency) {
    double real = 0.0, imaginary = 0.0;
    for (std::size_t i = 0; i < tone.value().samples.size(); ++i) {
      const auto phase = 2.0 * std::numbers::pi * frequency * static_cast<double>(i) / 8000.0;
      real += tone.value().samples[i] * std::cos(phase);
      imaginary += tone.value().samples[i] * std::sin(phase);
    }
    return std::hypot(real, imaginary);
  };
  // A 5400 Hz second harmonic would fold to 2600 Hz at this sample rate.
  CHECK(component(2700.0) > 1000.0 * component(2600.0));
  region.notes[0].midiKey = 127U;
  const auto high = synthesis::compileScorePerformance(project, region, 8000U); CHECK(high);
  auto unsupported = voice_design::PhonationSource::create(recipe, high.value(), 0); CHECK(unsupported);
  CHECK(!unsupported.value().render(100U)); CHECK(unsupported.value().position() == 0);
}

TEST_CASE("oral pose crossfades preserve block state rollback and settle at the target bank") {
  using namespace seam::voice_design;
  VoiceRecipe recipe;
  recipe.id = "transition";
  recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}},
                 {"i", "neutral", 0.0, {{300.0, 60.0, 0.0}, {2200.0, 90.0, -3.0}, {3000.0, 120.0, -6.0}, {4000.0, 200.0, -9.0}}}};
  auto tract = VocalTract::create(recipe, "a", "neutral", 48000U); CHECK(tract);
  const auto input = seam::test::support::sineWave(48000U, 220.0, 0.05);
  CHECK(tract.value().process(std::span<const float>{input}.first(300U)));
  auto old = tract.value();
  auto target = VocalTract::create(recipe, "i", "neutral", 48000U); CHECK(target);
  CHECK(!tract.value().transitionTo(recipe, "missing", "neutral", 511U));
  CHECK(!tract.value().transitionTo(recipe, "i", "neutral", 0U));
  CHECK(tract.value().transitionFramesRemaining() == 0U);
  CHECK(tract.value().transitionTo(recipe, "i", "neutral", 511U));
  CHECK(!tract.value().transitionTo(recipe, "a", "neutral", 200U));
  CHECK(tract.value().transitionFramesRemaining() == 511U);
  auto blocked = tract.value();
  std::stop_source stop; stop.request_stop();
  CHECK(!blocked.process(input, stop.get_token()));
  auto invalid = input; invalid.back() = std::numeric_limits<float>::quiet_NaN();
  CHECK(!blocked.process(invalid)); CHECK(blocked.transitionFramesRemaining() == 511U);
  const auto whole = tract.value().process(input); CHECK(whole);
  const auto oldOutput = old.process(input); CHECK(oldOutput);
  const auto targetOutput = target.value().process(input); CHECK(targetOutput);
  std::vector<float> joined;
  for (std::size_t offset = 0U; offset < input.size(); offset += 37U) {
    const auto part = blocked.process(std::span<const float>{input}.subspan(offset, std::min<std::size_t>(37U, input.size() - offset)));
    CHECK(part); joined.insert(joined.end(), part.value().begin(), part.value().end());
  }
  CHECK(joined == whole.value()); CHECK(blocked.transitionFramesRemaining() == 0U);
  for (std::size_t i = 0U; i < input.size(); ++i) {
    auto weight = std::min(1.0, static_cast<double>(i + 1U) / 511.0);
    weight = weight * weight * (3.0 - 2.0 * weight);
    CHECK_NEAR(whole.value()[i], oldOutput.value()[i] * (1.0 - weight) + targetOutput.value()[i] * weight, 0.000001);
    if (i >= 510U) CHECK(whole.value()[i] == targetOutput.value()[i]);
  }
  CHECK(tract.value().transitionTo(recipe, "a", "neutral", 100U));
  CHECK(tract.value().process(std::span<const float>{input}.first(20U)));
  tract.value().reset(); CHECK(tract.value().transitionFramesRemaining() == 0U);
  auto freshTarget = VocalTract::create(recipe, "i", "neutral", 48000U); CHECK(freshTarget);
  CHECK(tract.value().process(input).value() == freshTarget.value().process(input).value());
}

TEST_CASE("oral resonance changes timbre without changing a tone frequency and is block invariant") {
  using namespace seam::voice_design;
  VoiceRecipe recipe;
  recipe.id = "tract-fixture";
  recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 12.0}, {1500.0, 100.0, -24.0}, {2600.0, 140.0, -24.0}}}};
  auto shifted = recipe; shifted.poses[0].formants[0].frequencyHz = 1000.0;
  std::vector<float> input(48000U);
  for (std::size_t i = 0; i < input.size(); ++i) input[i] = static_cast<float>(0.2 * std::sin(2.0 * std::numbers::pi * 200.0 * static_cast<double>(i) / 48000.0));
  auto a = VocalTract::create(recipe, "a", "neutral", 48000U);
  auto b = VocalTract::create(shifted, "a", "neutral", 48000U);
  CHECK(a); CHECK(b);
  auto blocked = a.value();
  const auto whole = a.value().process(input);
  const auto changed = b.value().process(input);
  CHECK(whole); CHECK(changed); CHECK(whole.value() != changed.value());
  for (const auto* samples : {&whole.value(), &changed.value()}) {
    int crossings = 0;
    for (std::size_t i = 9601U; i < samples->size(); ++i) if ((*samples)[i - 1U] <= 0.0F && (*samples)[i] > 0.0F) ++crossings;
    CHECK(std::abs(crossings - 160) <= 1);
  }
  std::vector<float> joined;
  for (std::size_t offset = 0; offset < input.size(); offset += 127U) {
    const auto part = blocked.process(std::span<const float>{input}.subspan(offset, std::min<std::size_t>(127U, input.size() - offset)));
    CHECK(part); joined.insert(joined.end(), part.value().begin(), part.value().end());
  }
  CHECK(joined == whole.value());
  std::vector<float> twoTone(48000U);
  for (std::size_t i = 0; i < twoTone.size(); ++i) {
    const auto t = static_cast<double>(i) / 48000.0;
    twoTone[i] = static_cast<float>(0.1 * (std::sin(2.0 * std::numbers::pi * 700.0 * t) +
        std::sin(2.0 * std::numbers::pi * 1000.0 * t)));
  }
  a.value().reset(); b.value().reset();
  const auto spectrumA = a.value().process(twoTone);
  const auto spectrumB = b.value().process(twoTone);
  CHECK(spectrumA); CHECK(spectrumB);
  const auto magnitude = [](const auto& samples, double frequency) {
    double real = 0.0, imaginary = 0.0;
    for (std::size_t i = 24000U; i < samples.size(); ++i) {
      const auto phase = 2.0 * std::numbers::pi * frequency * static_cast<double>(i) / 48000.0;
      real += samples[i] * std::cos(phase); imaginary += samples[i] * std::sin(phase);
    }
    return std::hypot(real, imaginary);
  };
  CHECK(magnitude(spectrumA.value(), 700.0) > 2.0 * magnitude(spectrumA.value(), 1000.0));
  CHECK(magnitude(spectrumB.value(), 1000.0) > 2.0 * magnitude(spectrumB.value(), 700.0));
  blocked.reset(); CHECK(blocked.process(input).value() == whole.value());
  auto preserved = blocked;
  auto invalid = input; invalid.back() = std::numeric_limits<float>::quiet_NaN();
  CHECK(!blocked.process(invalid));
  std::stop_source stop; stop.request_stop(); CHECK(!blocked.process(input, stop.get_token()));
  CHECK(blocked.process(input).value() == preserved.process(input).value());
  CHECK(!blocked.process({}));
  CHECK(!VocalTract::create(recipe, "unknown", "neutral", 48000U));
  CHECK(!VocalTract::create(recipe, "a", "neutral", 0U));
  auto nasal = recipe; nasal.poses[0].nasalCoupling = 0.1;
  CHECK(!VocalTract::create(nasal, "a", "neutral", 48000U));
  auto high = recipe; high.poses[0].formants[2].frequencyHz = 4000.0;
  CHECK(!VocalTract::create(high, "a", "neutral", 8000U));
  for (const auto rate : {8000U, 44100U, 192000U, 384000U}) for (const auto width : {10.0, 5000.0}) {
    auto extreme = recipe;
    for (auto& band : extreme.poses[0].formants) { band.bandwidthHz = width; band.gainDb = 24.0; }
    auto filter = VocalTract::create(extreme, "a", "neutral", rate); CHECK(filter);
    std::vector<float> impulse(16384U, 0.0F); impulse[0] = 1.0F;
    const auto response = filter.value().process(impulse); CHECK(response);
    CHECK(std::all_of(response.value().begin(), response.value().end(), [](float value) { return std::isfinite(value) && std::abs(value) <= 8.0F; }));
  }
}

TEST_CASE("procedural recipe resources bind immutable canonical data to exact identity") {
  seam::voice_design::VoiceRecipe recipe;
  recipe.id = "draft-resource";
  recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
  const auto original = recipe;
  const auto first = seam::voice_design::freezeVoiceRecipeResource(recipe);
  CHECK(first); CHECK(first.value().validate());
  const auto encoded = seam::voice_design::encodeVoiceRecipe(recipe); CHECK(encoded);
  CHECK(first.value().identity.contentHash == seam::core::sha256Hex(encoded.value()));
  CHECK(seam::voice_design::freezeVoiceRecipeResource(recipe).value().identity == first.value().identity);
  recipe.phonation.aspiration = 0.4;
  const auto edited = seam::voice_design::freezeVoiceRecipeResource(recipe);
  CHECK(edited); CHECK(edited.value().identity.contentHash != first.value().identity.contentHash);
  const auto decoded = seam::voice_design::decodeVoiceRecipeResource(first.value());
  CHECK(decoded); CHECK(decoded.value() == original);
  CHECK(seam::voice_design::decodeVoiceRecipeResource(edited.value()).value() == recipe);
  auto wrong = first.value(); wrong.identity.id = "another-recipe";
  CHECK(!seam::voice_design::decodeVoiceRecipeResource(wrong));
  wrong = first.value(); wrong.identity.version = "2";
  CHECK(!seam::voice_design::decodeVoiceRecipeResource(wrong));
  const std::string arbitrary = "{}";
  auto identity = first.value().identity; identity.contentHash = seam::core::sha256Hex(arbitrary);
  const auto opaque = seam::synthesis::freezeProceduralResource(identity,
      std::as_bytes(std::span<const char>{arbitrary.data(), arbitrary.size()}));
  CHECK(opaque); CHECK(opaque.value().validate());
  CHECK(!seam::voice_design::decodeVoiceRecipeResource(opaque.value()));
  std::stop_source stop; stop.request_stop();
  CHECK(!seam::voice_design::freezeVoiceRecipeResource(recipe, stop.get_token()));
  CHECK(!seam::voice_design::decodeVoiceRecipeResource(first.value(), stop.get_token()));
}

TEST_CASE("voice recipes roundtrip exact draft controls poses and full width seeds") {
  seam::voice_design::VoiceRecipe recipe;
  recipe.id = "original-draft";
  recipe.seed = std::numeric_limits<std::uint64_t>::max();
  recipe.modulation = {8.0, 0.1, 4.0};
  recipe.poses = {{"a", "neutral", 0.1, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
  auto soft = recipe.poses.front(); soft.style = "soft"; soft.formants[0].frequencyHz = 600.0;
  recipe.poses.push_back(soft);
  const auto original = recipe;
  const auto encoded = seam::voice_design::encodeVoiceRecipe(recipe);
  CHECK(encoded);
  const auto decoded = seam::voice_design::decodeVoiceRecipe(encoded.value());
  CHECK(decoded); CHECK(decoded.value() == original); CHECK(recipe == original);
  CHECK(seam::voice_design::encodeVoiceRecipe(decoded.value()).value() == encoded.value());
  for (int invalid = 0; invalid < 8; ++invalid) {
    auto changed = recipe;
    if (invalid == 0) changed.phonation.openQuotient = 1.0;
    if (invalid == 1) changed.poses[0].formants[0].bandwidthHz = 0.0;
    if (invalid == 2) changed.poses[0].formants[1].frequencyHz = 500.0;
    if (invalid == 3) changed.modulation.jitterCents = std::numeric_limits<double>::quiet_NaN();
    if (invalid == 4) changed.poses.push_back(changed.poses.front());
    if (invalid == 5) changed.engineId = "unavailable-engine";
    if (invalid == 6) changed.poses.clear();
    if (invalid == 7) changed.id = std::string(1U, static_cast<char>(0xff));
    CHECK(!seam::voice_design::encodeVoiceRecipe(changed));
  }
  const auto parsed = seam::formats::parseJson(encoded.value()); CHECK(parsed);
  for (int invalid = 0; invalid < 5; ++invalid) {
    auto value = parsed.value();
    if (invalid == 0) *value.find("schemaVersion") = seam::formats::JsonValue{std::int64_t{2}};
    if (invalid == 1) value.asObject().emplace("script", seam::formats::JsonValue{"not executable"});
    if (invalid == 2) *value.find("seed") = seam::formats::JsonValue{"18446744073709551616"};
    if (invalid == 3) *value.find("seed") = seam::formats::JsonValue{123.0};
    if (invalid == 4) *value.find("seed") = seam::formats::JsonValue{"01"};
    CHECK(!seam::voice_design::decodeVoiceRecipe(seam::formats::stringifyJson(value)));
  }
  CHECK(!seam::voice_design::decodeVoiceRecipe(std::string(512U * 1024U + 1U, ' ')));
}
