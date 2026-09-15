#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/native_ui/voice_designer_model.hpp"
#include "seam/native_ui/voice_designer_session.hpp"
#include "seam/native_ui/voice_designer_layout.hpp"
#include "seam/native_ui/voice_designer_source_selection.hpp"
#include "seam/core/exclusive_file_lock.hpp"
#include "seam/core/sha256.hpp"
#include "seam/voice_design/frication_source.hpp"
#include <thread>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
seam::core::Result<void> drainDesigner(seam::native_ui::VoiceDesignerSession& session) {
  for (unsigned index = 0U; index < 3000U; ++index) {
    auto result = session.poll();
    if (!result || (!session.busy() && !session.auditionBusy())) return result;
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  return seam::core::failure(seam::core::ErrorCode::Internal, "Designer file worker exceeded test budget");
}
seam::voice_design::VoiceRecipe designerFixture() {
  seam::voice_design::VoiceRecipe recipe;
  recipe.id = "designer-fixture";
  recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
  return recipe;
}
}

TEST_CASE("Designer source selection resolves style filtered rows without changing recipes") {
  using namespace seam;
  auto recipe=designerFixture();
  recipe.poses.push_back(recipe.poses[0]); recipe.poses.back().style="bright";
  recipe.poses.push_back(recipe.poses[0]); recipe.poses.back().phone="i";
  recipe.frications={{"s","bright",{}},{"s","neutral",{}},{"sh","neutral",{}}};
  recipe.plosives={{"k","bright",{},10.0},{"t","neutral",{},10.0},{"k","neutral",{},10.0}};
  const auto original=recipe;
  auto selected=native_ui::designerSourceSelection(recipe,2U,2U,false); CHECK(selected);
  CHECK(selected->pose==2U); CHECK(selected->control==21U);
  selected=native_ui::designerSourceSelection(recipe,2U,0U,false); CHECK(selected);
  CHECK(selected->pose==1U); CHECK(selected->control==17U);
  selected=native_ui::designerSourceSelection(recipe,0U,2U,true); CHECK(selected);
  CHECK(selected->pose==0U); CHECK(selected->control==29U);
  selected=native_ui::designerSourceSelection(recipe,999U,0U,true); CHECK(selected);
  CHECK(selected->pose==1U); CHECK(selected->control==21U);
  CHECK(!native_ui::designerSourceSelection(recipe,0U,3U,true));
  CHECK(!native_ui::designerSourceSelection(recipe,0U,3U,false));
  CHECK(recipe==original);
  recipe.poses.clear(); CHECK(!native_ui::designerSourceSelection(recipe,0U,0U,false));
}

TEST_CASE("Designer source creation retains matching vowel and rejection retains selection") {
  using namespace seam;
  auto recipe=designerFixture(); recipe.poses.push_back(recipe.poses[0]); recipe.poses.back().phone="i";
  recipe.poses.push_back(recipe.poses[0]); recipe.poses.back().style="bright";
  native_ui::VoiceDesignerSession session; CHECK(session.create(recipe));
  CHECK(session.selectAudition(session.epoch(),session.model()->revision(),1U,72U));
  CHECK(session.addFrication(session.epoch(),session.model()->revision(),"s","neutral"));
  CHECK(session.auditionPose()==1U); CHECK(session.auditionPitch()==72U);
  CHECK(session.addPlosive(session.epoch(),session.model()->revision(),"k","neutral"));
  CHECK(session.auditionPose()==1U);
  const auto revision=session.model()->revision();
  CHECK(!session.addPlosive(session.epoch(),revision,"k","neutral"));
  CHECK(session.model()->revision()==revision); CHECK(session.auditionPose()==1U);
  CHECK(session.addPlosive(session.epoch(),revision,"t","bright")); CHECK(session.auditionPose()==2U);
  CHECK(session.auditionPitch()==72U);
}

TEST_CASE("Designer history preserves plosive bindings while unrelated voice parameters change") {
  using namespace seam;
  auto model=native_ui::VoiceDesignerModel::create(designerFixture()); CHECK(model);
  const auto original=model.value().resource();
  auto recipe=model.value().recipe(); recipe.plosives={{"t","neutral",{.seed=42U},10.0}};
  CHECK(model.value().edit(model.value().revision(),recipe)); CHECK(model.value().resource().identity.version=="4");
  const auto bindings=model.value().recipe().plosives;
  auto changed=model.value().recipe(); changed.phonation.aspiration=0.15;
  CHECK(model.value().edit(model.value().revision(),changed)); CHECK(model.value().recipe().plosives==bindings);
  CHECK(model.value().undo(model.value().revision())); CHECK(model.value().recipe()==recipe);
  CHECK(model.value().undo(model.value().revision())); CHECK(model.value().resource().identity==original.identity);
  CHECK(model.value().redo(model.value().revision())); CHECK(model.value().recipe().plosives==bindings);
  const auto root=test::support::temporaryDirectory("designer-plosive-persistence");
  CHECK(voice_design::saveVoiceRecipeFile(root/"voice.json",model.value().recipe()));
  const auto loaded=voice_design::loadVoiceRecipeResource(root/"voice.json",model.value().resource().identity); CHECK(loaded);
  CHECK(voice_design::decodeVoiceRecipeResource(loaded.value()).value().plosives==bindings);
}

TEST_CASE("Designer stop vowel audition uses the selected pose pitch and distinct preview mode") {
  using namespace seam;
  auto recipe=designerFixture();
  recipe.plosives={{"k","neutral",{.seed=42U},10.0}};
  auto other=recipe.poses.front(); other.phone="i"; other.formants.front().frequencyHz=300.0; recipe.poses.push_back(other);
  const auto resource=voice_design::freezeVoiceRecipeResource(recipe); CHECK(resource);
  const auto ka=native_ui::renderDesignerPlosivePhraseAudition(resource.value(),0U,0U);
  if (!ka) throw test::Failure{ka.error().message+" / "+ka.error().context};
  const auto ki=native_ui::renderDesignerPlosivePhraseAudition(resource.value(),0U,1U); CHECK(ki);
  CHECK(ka.value().interleaved.size()==48000U); CHECK(ka.value().interleaved!=ki.value().interleaved);
  const auto& pcm=ka.value().interleaved;
  CHECK(std::all_of(pcm.begin(),pcm.begin()+2400,[](float sample){return sample==0.0F;}));
  CHECK(std::any_of(pcm.begin()+2400,pcm.begin()+2880,[](float sample){return sample!=0.0F;}));
  CHECK(std::any_of(pcm.begin()+2880,pcm.end(),[](float sample){return sample!=0.0F;}));
  CHECK(native_ui::renderDesignerPlosivePhraseAudition(resource.value(),0U,0U).value().interleaved==pcm);
  CHECK(native_ui::renderDesignerPlosivePhraseAudition(resource.value(),0U,0U,72U).value().interleaved!=pcm);
  CHECK(!native_ui::renderDesignerPlosivePhraseAudition(resource.value(),1U,0U));
  CHECK(!native_ui::renderDesignerPlosivePhraseAudition(resource.value(),0U,2U));
  CHECK(!native_ui::renderDesignerPlosivePhraseAudition(resource.value(),0U,0U,20U));
  std::stop_source stop; stop.request_stop();
  CHECK(!native_ui::renderDesignerPlosivePhraseAudition(resource.value(),0U,0U,69U,stop.get_token()));
  auto wrong=recipe; wrong.poses[1].style="other";
  const auto mismatched=voice_design::freezeVoiceRecipeResource(wrong); CHECK(mismatched);
  CHECK(!native_ui::renderDesignerPlosivePhraseAudition(mismatched.value(),0U,1U));
  native_ui::VoiceDesignerSession session; CHECK(session.create(recipe));
  CHECK(session.beginPlosiveAudition(0U,true)); CHECK(drainDesigner(session));
  CHECK(session.plosiveAudioIsPhrase()); CHECK(session.plosiveAudio()->interleaved==pcm);
  CHECK(!session.auditionAudio());
  CHECK(session.beginPlosiveAudition(0U)); CHECK(drainDesigner(session));
  CHECK(!session.plosiveAudioIsPhrase()); CHECK(session.plosiveAudio()->frameCount()==24000U);
  CHECK(session.beginPlosiveAudition(0U,true));
  CHECK(session.selectAudition(session.epoch(),session.model()->revision(),1U,69U));
  CHECK(drainDesigner(session)); CHECK(!session.plosiveAudio()); CHECK(!session.plosiveAudioIsPhrase());
  CHECK(session.beginPlosiveAudition(0U,true)); CHECK(drainDesigner(session));
  CHECK(session.plosiveAudio()->interleaved==ki.value().interleaved);
  using Mode=native_ui::PlosiveAuditionMode;
  const auto coda=native_ui::renderDesignerPlosivePhraseAudition(resource.value(),0U,0U,69U,{},Mode::VowelStop); CHECK(coda);
  const auto& vc=coda.value().interleaved; CHECK(vc.size()==48000U); CHECK(vc!=pcm);
  CHECK(std::any_of(vc.begin(),vc.begin()+45120,[](float sample){return sample!=0.0F;}));
  CHECK(std::all_of(vc.begin()+45120,vc.begin()+47520,[](float sample){return sample==0.0F;}));
  CHECK(std::any_of(vc.begin()+47520,vc.end(),[](float sample){return sample!=0.0F;}));
  CHECK(vc.back()==0.0F);
  CHECK(native_ui::renderDesignerPlosivePhraseAudition(resource.value(),0U,0U,69U,{},Mode::VowelStop).value().interleaved==vc);
  CHECK(!native_ui::renderDesignerPlosivePhraseAudition(resource.value(),0U,0U,69U,{},Mode::Source));
  CHECK(session.selectAudition(session.epoch(),session.model()->revision(),0U,69U));
  CHECK(session.beginPlosiveAudition(0U,Mode::VowelStop)); CHECK(drainDesigner(session));
  CHECK(session.plosiveAudioIsCoda()); CHECK(session.plosiveAudioIsPhrase()); CHECK(session.plosiveAudioMode()==Mode::VowelStop);
  CHECK(session.plosiveAudio()->interleaved==vc);
  const auto retained=session.plosiveAudio();
  CHECK(!session.beginPlosiveAudition(0U,static_cast<Mode>(99))); CHECK(session.plosiveAudio()==retained);
  CHECK(session.beginPlosiveAudition(0U,Mode::StopVowel)); CHECK(drainDesigner(session));
  CHECK(!session.plosiveAudioIsCoda()); CHECK(session.plosiveAudio()->interleaved==pcm);
}

TEST_CASE("Designer plosive audition is finite reproducible separate and invalidated by edits") {
  using namespace seam;
  native_ui::VoiceDesignerSession session; CHECK(session.create(designerFixture()));
  CHECK(session.addPlosive(session.epoch(),session.model()->revision(),"k","neutral"));
  CHECK(session.addFrication(session.epoch(),session.model()->revision(),"s","neutral"));
  const auto resource=session.model()->resource();
  const auto direct=native_ui::renderDesignerPlosiveAudition(resource,0U); CHECK(direct);
  CHECK(direct.value().sampleRate==48000U); CHECK(direct.value().channels==1U);
  const auto& pcm=direct.value().interleaved; CHECK(pcm.size()==24000U);
  CHECK(std::all_of(pcm.begin(),pcm.begin()+2400,[](float sample){return sample==0.0F;}));
  CHECK(std::any_of(pcm.begin()+2400,pcm.begin()+2880,[](float sample){return sample!=0.0F;}));
  CHECK(std::all_of(pcm.begin()+2880,pcm.end(),[](float sample){return sample==0.0F;}));
  CHECK(std::all_of(pcm.begin(),pcm.end(),[](float sample){return std::isfinite(sample) && std::abs(sample)<=0.9F;}));
  CHECK(native_ui::renderDesignerPlosiveAudition(resource,0U).value().interleaved==pcm);
  CHECK(!native_ui::renderDesignerPlosiveAudition(resource,1U));
  std::stop_source stop; stop.request_stop();
  CHECK(!native_ui::renderDesignerPlosiveAudition(resource,0U,stop.get_token()));
  CHECK(session.beginAudition()); CHECK(drainDesigner(session)); const auto vowel=session.auditionAudio();
  CHECK(session.beginPlosiveAudition(0U)); CHECK(drainDesigner(session));
  CHECK(session.plosiveAudio()); CHECK(session.plosiveAudioIndex()==0U);
  CHECK(session.plosiveAudio()->interleaved==pcm); CHECK(session.auditionAudio()==vowel); CHECK(!session.fricationAudio());
  auto changed=session.model()->recipe(); changed.plosives.front().burstMilliseconds=25.0;
  CHECK(session.edit(session.epoch(),session.model()->revision(),changed));
  CHECK(!session.plosiveAudio()); CHECK(!session.plosiveAudioIndex());
  CHECK(session.beginPlosiveAudition(0U)); CHECK(drainDesigner(session));
  CHECK(session.plosiveAudio()->interleaved!=pcm);
  CHECK(session.beginFricationAudition(0U)); CHECK(!session.plosiveAudio()); CHECK(drainDesigner(session));
  CHECK(session.fricationAudio()); CHECK(!session.plosiveAudio());
  CHECK(session.beginPlosiveAudition(0U));
  CHECK(session.setPlosiveSeed(session.epoch(),session.model()->revision(),0U,"43"));
  CHECK(drainDesigner(session)); CHECK(!session.plosiveAudio());
  CHECK(session.beginPlosiveAudition(0U)); session.cancelAudition(); CHECK(drainDesigner(session)); CHECK(!session.plosiveAudio());
  CHECK(session.beginPlosiveAudition(0U)); CHECK(drainDesigner(session));
  CHECK(session.removePlosive(session.epoch(),session.model()->revision(),0U)); CHECK(!session.plosiveAudio());
  CHECK(!session.beginPlosiveAudition(0U));
}

TEST_CASE("Designer plosive creation seed edits removal and save are validated undoable operations") {
  using namespace seam;
  native_ui::VoiceDesignerSession session; CHECK(session.create(designerFixture()));
  CHECK(session.setSeed(session.epoch(),session.model()->revision(),"123"));
  CHECK(session.addFrication(session.epoch(),session.model()->revision(),"s","neutral"));
  const auto initial=session.model()->recipe(); const auto oldRevision=session.model()->revision();
  CHECK(!session.addPlosive(session.epoch(),oldRevision,"s","neutral"));
  CHECK(!session.addPlosive(session.epoch(),oldRevision,"k","missing"));
  CHECK(session.addPlosive(session.epoch(),oldRevision,"k","neutral"));
  CHECK(session.model()->resource().identity.version=="4");
  CHECK(session.model()->recipe().plosives.front().source.seed==123U);
  CHECK(!session.addPlosive(session.epoch(),oldRevision,"t","neutral"));
  CHECK(!session.addPlosive(session.epoch(),session.model()->revision(),"k","neutral"));
  CHECK(session.setPlosiveSeed(session.epoch(),session.model()->revision(),0U,"18446744073709551615"));
  CHECK(session.model()->recipe().plosives.front().source.seed==std::numeric_limits<std::uint64_t>::max());
  CHECK(session.model()->recipe().seed==initial.seed); CHECK(session.model()->recipe().frications==initial.frications);
  for (const auto* seed : {"01","-1","18446744073709551616"})
    CHECK(!session.setPlosiveSeed(session.epoch(),session.model()->revision(),0U,seed));
  const auto seeded=session.model()->recipe();
  auto desired=seeded; desired.plosives.front().burstMilliseconds=22.0; desired.plosives.front().source.centerHz=3200.0;
  CHECK(session.edit(session.epoch(),session.model()->revision(),desired));
  auto invalid=desired; invalid.plosives.front().burstMilliseconds=0.0;
  CHECK(!session.edit(session.epoch(),session.model()->revision(),invalid)); CHECK(session.model()->recipe()==desired);
  CHECK(!session.removePlosive(session.epoch()+1U,session.model()->revision(),0U));
  CHECK(!session.removePlosive(session.epoch(),session.model()->revision(),1U));
  CHECK(session.removePlosive(session.epoch(),session.model()->revision(),0U));
  CHECK(session.model()->recipe()==initial); CHECK(session.model()->resource().identity.version=="2");
  CHECK(session.undo(session.epoch(),session.model()->revision())); CHECK(session.model()->recipe()==desired);
  CHECK(session.undo(session.epoch(),session.model()->revision())); CHECK(session.model()->recipe()==seeded);
  CHECK(session.redo(session.epoch(),session.model()->revision())); CHECK(session.model()->recipe()==desired);
  const auto root=test::support::temporaryDirectory("designer-plosive-controls");
  CHECK(session.beginSave(root/"voice.json")); CHECK(drainDesigner(session));
  native_ui::VoiceDesignerSession reopened; CHECK(reopened.beginOpen(root/"voice.json")); CHECK(drainDesigner(reopened));
  CHECK(reopened.model()->recipe()==desired);
}

TEST_CASE("Designer pose duplication rejects selection changes with an unchanged recipe revision") {
  using namespace seam;
  auto recipe = designerFixture();
  auto second = recipe.poses.front(); second.phone = "i"; second.formants.front().frequencyHz = 300.0;
  recipe.poses.push_back(second);
  native_ui::VoiceDesignerSession session; CHECK(session.create(recipe));
  const auto epoch = session.epoch(); const auto revision = session.model()->revision();
  CHECK(session.selectAudition(epoch, revision, 1U, 69U));
  CHECK(session.model()->revision() == revision);
  CHECK(!session.duplicatePose(epoch, revision, "u", "neutral", 0U));
  CHECK(session.model()->recipe() == recipe); CHECK(!session.model()->canUndo());
  CHECK(session.duplicatePose(epoch, revision, "u", "neutral", 1U));
  CHECK(session.model()->recipe().poses.back().formants == second.formants);
  CHECK(session.undo(epoch, session.model()->revision())); CHECK(session.model()->recipe() == recipe);
}

TEST_CASE("Designer nasal edits audition save reopen and undo without changing the previous resource") {
  using namespace seam;
  native_ui::VoiceDesignerSession session; CHECK(session.create(designerFixture()));
  const auto original=session.model()->resource();
  CHECK(session.beginAudition()); CHECK(drainDesigner(session)); const auto dry=session.auditionAudio(); CHECK(dry);
  auto desired=session.model()->recipe(); desired.poses[0].nasal=voice_design::NasalResonance{}; desired.poses[0].nasalCoupling=0.8;
  CHECK(session.edit(session.epoch(),session.model()->revision(),desired)); CHECK(!session.auditionAudio());
  CHECK(session.model()->resource().identity.version=="3");
  CHECK(session.beginAudition()); CHECK(drainDesigner(session)); const auto wet=session.auditionAudio(); CHECK(wet);
  CHECK(wet->interleaved!=dry->interleaved); CHECK(voice_design::decodeVoiceRecipeResource(original).value()==designerFixture());
  CHECK(session.undo(session.epoch(),session.model()->revision())); CHECK(session.model()->resource().identity==original.identity);
  CHECK(session.beginAudition()); CHECK(drainDesigner(session)); CHECK(session.auditionAudio()->interleaved==dry->interleaved);
  CHECK(session.redo(session.epoch(),session.model()->revision()));
  const auto root=test::support::temporaryDirectory("designer-nasal");
  CHECK(voice_design::saveVoiceRecipeFile(root/"nasal.json",session.model()->recipe()));
  native_ui::VoiceDesignerSession reopened; CHECK(reopened.beginOpen(root/"nasal.json")); CHECK(drainDesigner(reopened));
  CHECK(reopened.beginAudition()); CHECK(drainDesigner(reopened)); CHECK(reopened.auditionAudio()->interleaved==wet->interleaved);
}

TEST_CASE("Designer compact layout keeps selection status and errors inside the minimum window") {
  using namespace seam::native_ui;
  for (const auto height : {320.0,340.0,400.0,520.0,599.0,600.0,700.0,900.0}) {
    const auto layout = voiceDesignerLayout(height);
    CHECK(layout.visibleRows >= 1U && layout.visibleRows <= 256U);
    CHECK(layout.summaryY >= layout.controlsTop+static_cast<double>(layout.visibleRows)*layout.rowHeight);
    CHECK(layout.statusY+24.0 <= height); CHECK(layout.helpY+24.0 <= height); CHECK(layout.errorY+24.0 <= height);
    if (!layout.compact) CHECK(layout.referenceY+24.0 <= height);
    else CHECK(layout.referenceY < 0.0);
    if (height >= 400.0) {
      CHECK(layout.prepareY >= layout.helpY+24.0);
      CHECK(layout.fricationY >= layout.prepareY+24.0);
      CHECK(layout.fricationY+24.0 <= height);
    } else { CHECK(layout.prepareY < 0.0); CHECK(layout.fricationY < 0.0); }
    for (const auto count : {1U,17U,32U,224U}) for (std::size_t selected = 0U; selected < count; ++selected) {
      const auto first = layout.firstRow(count,selected);
      CHECK(first <= selected); CHECK(selected < first+layout.visibleRows);
      CHECK(first < count);
    }
  }
  CHECK(voiceDesignerLayout(std::numeric_limits<double>::quiet_NaN()).visibleRows == voiceDesignerLayout(320.0).visibleRows);
  CHECK(voiceDesignerLayout(520.0).visibleRows==6U);
  CHECK(voiceDesignerLayout(900.0).visibleRows==16U);
  CHECK(voiceDesignerLayout(1200.0).visibleRows>voiceDesignerLayout(900.0).visibleRows);
  CHECK(VoiceDesignerLayout::textSize>=15.0);
}

TEST_CASE("Designer source action bars remain reachable throughout supported resize range") {
  using namespace seam::native_ui;
  for (int height=520; height<=1600; ++height) {
    const auto layout=voiceDesignerLayout(static_cast<double>(height));
    CHECK(layout.summaryY>=layout.controlsTop+static_cast<double>(layout.visibleRows)*layout.rowHeight);
    CHECK(layout.statusY>=layout.summaryY+24.0);
    CHECK(layout.helpY>=layout.statusY+24.0);
    CHECK(layout.prepareY>=layout.helpY+24.0);
    CHECK(layout.fricationY>=layout.prepareY+24.0);
    CHECK(layout.fricationY+24.0<=height);
    CHECK(layout.errorY+24.0<=height);
  }
}

TEST_CASE("Designer noise preview availability follows voicing and selected vowel") {
  using namespace seam;
  auto recipe=designerFixture(); recipe.poses.push_back(recipe.poses[0]); recipe.poses.back().phone="z";
  recipe.poses.push_back(recipe.poses[0]); recipe.poses.back().style="bright";
  recipe.frications={{"z","neutral",{.seed=42U},0.3},{"s","neutral",{}}};
  recipe.plosives={{"k","neutral",{},10.0}};
  auto available=native_ui::designerNoisePreviewAvailability(recipe,0U,0U,false);
  CHECK(!available.source); CHECK(available.context);
  available=native_ui::designerNoisePreviewAvailability(recipe,1U,0U,false);
  CHECK(!available.source); CHECK(!available.context);
  available=native_ui::designerNoisePreviewAvailability(recipe,0U,1U,false);
  CHECK(available.source); CHECK(available.context);
  available=native_ui::designerNoisePreviewAvailability(recipe,1U,0U,true);
  CHECK(available.source); CHECK(!available.context);
  available=native_ui::designerNoisePreviewAvailability(recipe,0U,0U,true);
  CHECK(available.source); CHECK(available.context);
  available=native_ui::designerNoisePreviewAvailability(recipe,2U,0U,false);
  CHECK(!available.source); CHECK(!available.context);
  CHECK(!native_ui::designerNoisePreviewAvailability(recipe,99U,0U,false).source);
  CHECK(!native_ui::designerNoisePreviewAvailability(recipe,0U,99U,true).context);
}

TEST_CASE("Designer voicing gain edit preserves validation history and saved identity") {
  using namespace seam;
  auto recipe=designerFixture(); recipe.frications={{"z","neutral",{.seed=42U}}};
  native_ui::VoiceDesignerSession session; CHECK(session.create(recipe));
  auto desired=recipe; native_ui::setDesignerFricationVoicing(desired.frications[0],0.3);
  CHECK(!session.edit(session.epoch(),session.model()->revision(),desired));
  CHECK(session.model()->recipe()==recipe); // Explicit resonance still required.
  CHECK(session.duplicatePose(session.epoch(),session.model()->revision(),"z","neutral",0U));
  const auto unvoiced=session.model()->resource();
  desired=session.model()->recipe(); native_ui::setDesignerFricationVoicing(desired.frications[0],0.3);
  CHECK(session.edit(session.epoch(),session.model()->revision(),desired));
  const auto voiced=session.model()->resource(); CHECK(voiced.identity.version=="5");
  auto invalid=desired; native_ui::setDesignerFricationVoicing(invalid.frications[0],1.01);
  CHECK(!session.edit(session.epoch(),session.model()->revision(),invalid)); CHECK(session.model()->resource().identity==voiced.identity);
  CHECK(session.undo(session.epoch(),session.model()->revision())); CHECK(session.model()->resource().identity==unvoiced.identity);
  CHECK(session.redo(session.epoch(),session.model()->revision())); CHECK(session.model()->resource().identity==voiced.identity);
  const auto root=test::support::temporaryDirectory("designer-voicing-control");
  CHECK(session.beginSave(root/"voice.json")); CHECK(drainDesigner(session));
  native_ui::VoiceDesignerSession reopened; CHECK(reopened.beginOpen(root/"voice.json")); CHECK(drainDesigner(reopened));
  CHECK(reopened.model()->resource().identity==voiced.identity);
  desired=session.model()->recipe(); native_ui::setDesignerFricationVoicing(desired.frications[0],0.0);
  CHECK(session.edit(session.epoch(),session.model()->revision(),desired)); CHECK(session.model()->resource().identity==unvoiced.identity);
}

TEST_CASE("Designer voiced frication uses mixed context instead of dropping voicing") {
  using namespace seam;
  auto recipe=designerFixture(); recipe.poses.push_back(recipe.poses[0]); recipe.poses.back().phone="z";
  recipe.frications={{"z","neutral",{.seed=42U},0.3}};
  native_ui::VoiceDesignerSession session; CHECK(session.create(recipe));
  const auto resource=session.model()->resource();
  CHECK(!native_ui::renderDesignerFricationAudition(resource,0U));
  CHECK(session.beginFricationAudition(0U,native_ui::FricationAuditionMode::FricationVowel)); CHECK(drainDesigner(session));
  const auto cv=session.fricationAudio(); CHECK(cv); CHECK(cv->frameCount()==48000U);
  CHECK(session.beginFricationAudition(0U,native_ui::FricationAuditionMode::VowelFrication)); CHECK(drainDesigner(session));
  const auto vc=session.fricationAudio(); CHECK(vc); CHECK(vc->frameCount()==48000U); CHECK(vc->interleaved!=cv->interleaved);
  CHECK(session.model()->resource().identity==resource.identity);
}

TEST_CASE("Designer frication context previews preserve mode pitch and draft ownership") {
  using namespace seam;
  using Mode=native_ui::FricationAuditionMode;
  native_ui::VoiceDesignerSession session; CHECK(session.create(designerFixture()));
  CHECK(session.addFrication(session.epoch(),session.model()->revision(),"s","neutral"));
  const auto resource=session.model()->resource();
  const auto cv=native_ui::renderDesignerFricationPhraseAudition(resource,0U,0U);
  const auto vc=native_ui::renderDesignerFricationPhraseAudition(resource,0U,0U,69U,{},Mode::VowelFrication);
  CHECK(cv); CHECK(vc); CHECK(cv.value().frameCount()==48000U); CHECK(vc.value().frameCount()==48000U);
  CHECK(cv.value().interleaved!=vc.value().interleaved);
  CHECK(native_ui::renderDesignerFricationPhraseAudition(resource,0U,0U).value().interleaved==cv.value().interleaved);
  CHECK(native_ui::renderDesignerFricationPhraseAudition(resource,0U,0U,72U).value().interleaved!=cv.value().interleaved);
  for (const auto* audio:{&cv.value(),&vc.value()}) {
    CHECK(audio->interleaved.front()==0.0F); CHECK(audio->interleaved.back()==0.0F);
    CHECK(std::all_of(audio->interleaved.begin(),audio->interleaved.end(),[](float sample){return std::isfinite(sample)&&std::abs(sample)<=0.90001F;}));
    // VC frication begins at 850 ms: sustained noise, not a stop's silent closure.
    CHECK(std::any_of(audio->interleaved.begin()+41000,audio->interleaved.begin()+45000,[](float sample){return std::abs(sample)>0.00001F;}));
  }
  CHECK(!native_ui::renderDesignerFricationPhraseAudition(resource,1U,0U));
  CHECK(!native_ui::renderDesignerFricationPhraseAudition(resource,0U,1U));
  CHECK(!native_ui::renderDesignerFricationPhraseAudition(resource,0U,0U,35U));
  CHECK(!native_ui::renderDesignerFricationPhraseAudition(resource,0U,0U,69U,{},Mode::Source));
  auto other=designerFixture(); other.poses.push_back(other.poses[0]); other.poses.back().style="bright";
  other.frications=session.model()->recipe().frications;
  native_ui::VoiceDesignerSession mismatch; CHECK(mismatch.create(other));
  CHECK(!native_ui::renderDesignerFricationPhraseAudition(mismatch.model()->resource(),0U,1U));
  std::stop_source cancel; cancel.request_stop();
  CHECK(!native_ui::renderDesignerFricationPhraseAudition(resource,0U,0U,69U,cancel.get_token()));
  CHECK(session.beginFricationAudition(0U,Mode::FricationVowel)); CHECK(drainDesigner(session));
  CHECK(session.fricationAudioMode()==Mode::FricationVowel); CHECK(session.fricationAudio()->interleaved==cv.value().interleaved);
  CHECK(session.beginFricationAudition(0U,Mode::VowelFrication)); CHECK(!session.fricationAudio()); CHECK(drainDesigner(session));
  CHECK(session.fricationAudioMode()==Mode::VowelFrication); CHECK(session.fricationAudio()->interleaved==vc.value().interleaved);
  CHECK(session.model()->resource().identity==resource.identity);
  CHECK(session.beginFricationAudition(0U,Mode::FricationVowel));
  auto changed=session.model()->recipe(); changed.frications[0].source.gain=0.1;
  CHECK(session.edit(session.epoch(),session.model()->revision(),changed)); CHECK(drainDesigner(session)); CHECK(!session.fricationAudio());
  CHECK(session.undo(session.epoch(),session.model()->revision())); CHECK(session.model()->resource().identity==resource.identity);
  CHECK(session.beginFricationAudition(0U)); CHECK(drainDesigner(session)); CHECK(session.fricationAudioMode()==Mode::Source);
  CHECK(session.fricationAudio()->interleaved==native_ui::renderDesignerFricationAudition(resource,0U).value().interleaved);
}

TEST_CASE("Designer frication audition is bounded reproducible and separate from vowel comparison") {
  using namespace seam;
  native_ui::VoiceDesignerSession session; CHECK(session.create(designerFixture()));
  CHECK(session.addFrication(session.epoch(),session.model()->revision(),"s","neutral"));
  const auto resource = session.model()->resource();
  const auto first = native_ui::renderDesignerFricationAudition(resource,0U); CHECK(first);
  const auto repeat = native_ui::renderDesignerFricationAudition(resource,0U); CHECK(repeat);
  CHECK(first.value().frameCount() == 48000U); CHECK(first.value().interleaved == repeat.value().interleaved);
  CHECK(first.value().interleaved.front() == 0.0F); CHECK(first.value().interleaved.back() == 0.0F);
  CHECK(std::all_of(first.value().interleaved.begin(),first.value().interleaved.end(),[](float x) { return std::isfinite(x) && std::abs(x) <= 0.90001F; }));
  CHECK(std::any_of(first.value().interleaved.begin(),first.value().interleaved.end(),[](float x) { return std::abs(x) > 0.00001F; }));
  CHECK(!native_ui::renderDesignerFricationAudition(resource,1U));
  CHECK(session.beginAudition()); CHECK(drainDesigner(session)); const auto vowel = session.auditionAudio();
  CHECK(session.pinAuditionReference(session.epoch(),session.model()->revision()));
  CHECK(session.beginFricationAudition(0U)); CHECK(drainDesigner(session)); CHECK(session.fricationAudioIndex() == 0U);
  CHECK(session.fricationAudio()->interleaved == first.value().interleaved);
  CHECK(session.auditionAudio() == vowel); CHECK(session.auditionReference()->audio == vowel);
  CHECK(session.beginFricationAudition(0U));
  CHECK(session.setFricationSeed(session.epoch(),session.model()->revision(),0U,"42"));
  CHECK(drainDesigner(session)); CHECK(!session.fricationAudio()); CHECK(!session.auditionAudio());
  CHECK(session.auditionReference()->audio == vowel);
  CHECK(session.beginFricationAudition(0U)); CHECK(drainDesigner(session));
  CHECK(session.fricationAudio()->interleaved != first.value().interleaved);
  CHECK(session.beginFricationAudition(0U)); CHECK(session.finish()); CHECK(!session.auditionBusy());
}

TEST_CASE("Designer voiced source removal preserves its resonance and rejects dependent pose removal") {
  using namespace seam;
  auto recipe=designerFixture(); recipe.poses.push_back(recipe.poses[0]); recipe.poses.back().phone="z";
  recipe.frications={{"z","neutral",{.seed=42U},0.3}};
  native_ui::VoiceDesignerSession session; CHECK(session.create(recipe));
  CHECK(session.beginFricationAudition(0U,native_ui::FricationAuditionMode::FricationVowel)); CHECK(drainDesigner(session));
  CHECK(session.fricationAudio());
  CHECK(session.selectAudition(session.epoch(),session.model()->revision(),1U,69U));
  const auto original=session.model()->resource(); const auto revision=session.model()->revision();
  const auto rejected=session.removePose(session.epoch(),revision);
  CHECK(!rejected); CHECK(rejected.error().code==core::ErrorCode::Conflict);
  CHECK(rejected.error().message.find("disable its voicing")!=std::string::npos);
  CHECK(session.model()->revision()==revision); CHECK(session.model()->resource().identity==original.identity);
  CHECK(session.selectAudition(session.epoch(),revision,0U,69U));
  CHECK(session.beginFricationAudition(0U,native_ui::FricationAuditionMode::FricationVowel)); CHECK(drainDesigner(session));
  const auto pcm=session.fricationAudio()->interleaved;
  CHECK(session.removeFrication(session.epoch(),session.model()->revision(),0U));
  CHECK(!session.fricationAudio()); CHECK(session.model()->recipe().poses==recipe.poses);
  CHECK(session.model()->recipe().frications.empty());
  CHECK(session.undo(session.epoch(),session.model()->revision())); CHECK(session.model()->resource().identity==original.identity);
  CHECK(!session.fricationAudio());
  CHECK(session.beginFricationAudition(0U,native_ui::FricationAuditionMode::FricationVowel)); CHECK(drainDesigner(session));
  CHECK(session.fricationAudio()->interleaved==pcm);
}

TEST_CASE("Designer frication seed and removal edits preserve other sources and undo exactly") {
  using namespace seam;
  native_ui::VoiceDesignerSession session; CHECK(session.create(designerFixture()));
  CHECK(session.addFrication(session.epoch(), session.model()->revision(), "s", "neutral"));
  CHECK(session.addFrication(session.epoch(), session.model()->revision(), "sh", "neutral"));
  const auto original = session.model()->recipe();
  CHECK(session.setFricationSeed(session.epoch(), session.model()->revision(), 1U, "18446744073709551615"));
  CHECK(session.model()->recipe().frications[1].source.seed == std::numeric_limits<std::uint64_t>::max());
  CHECK(session.model()->recipe().seed == original.seed);
  CHECK(session.model()->recipe().frications[0] == original.frications[0]);
  const auto seeded = session.model()->recipe(); const auto revision = session.model()->revision();
  CHECK(!session.setFricationSeed(session.epoch(), revision, 1U, "18446744073709551616"));
  CHECK(!session.removeFrication(session.epoch(), revision, 2U));
  CHECK(!session.removeFrication(session.epoch()+1U, revision, 0U));
  CHECK(session.model()->recipe() == seeded); CHECK(session.model()->revision() == revision);
  CHECK(session.removeFrication(session.epoch(), revision, 0U));
  CHECK(session.model()->recipe().frications.size() == 1U);
  CHECK(session.model()->recipe().frications.front() == seeded.frications[1]);
  CHECK(session.undo(session.epoch(), session.model()->revision())); CHECK(session.model()->recipe() == seeded);
  CHECK(session.undo(session.epoch(), session.model()->revision())); CHECK(session.model()->recipe() == original);
}

TEST_CASE("Designer frication creation and filter edits retain validated reproducible recipe state") {
  using namespace seam;
  native_ui::VoiceDesignerSession session; CHECK(session.create(designerFixture()));
  CHECK(session.setSeed(session.epoch(), session.model()->revision(), "123"));
  const auto before = session.model()->recipe(); const auto revision = session.model()->revision();
  CHECK(!session.addFrication(session.epoch(), revision, "s", "missing-style"));
  CHECK(session.model()->recipe() == before); CHECK(session.model()->revision() == revision);
  CHECK(session.addFrication(session.epoch(), revision, "s", "neutral"));
  CHECK(session.model()->resource().identity.version == "2");
  CHECK(session.model()->recipe().frications.front().source.seed == 123U);
  CHECK(!session.addFrication(session.epoch(), session.model()->revision(), "s", "neutral"));
  const auto original = session.model()->recipe();
  auto edited = original; edited.frications.front().source.centerHz = 6000.0;
  CHECK(session.edit(session.epoch(), session.model()->revision(), edited));
  auto originalNoise = voice_design::FricationSource::create(original.frications.front().source, 48000U, 0); CHECK(originalNoise);
  auto editedNoise = voice_design::FricationSource::create(edited.frications.front().source, 48000U, 0); CHECK(editedNoise);
  const auto originalPcm = originalNoise.value().render(2400U); CHECK(originalPcm);
  const auto editedPcm = editedNoise.value().render(2400U); CHECK(editedPcm);
  CHECK(originalPcm.value().samples != editedPcm.value().samples);
  auto invalid = edited; invalid.frications.front().source.gain = 0.3;
  const auto editRevision = session.model()->revision();
  CHECK(!session.edit(session.epoch(), editRevision, invalid)); CHECK(session.model()->revision() == editRevision);
  CHECK(session.undo(session.epoch(), editRevision)); CHECK(session.model()->recipe() == original);
  CHECK(session.undo(session.epoch(), session.model()->revision())); CHECK(session.model()->recipe() == before);
  CHECK(session.model()->resource().identity.version == "1");
}

TEST_CASE("Designer seed edits retain full unsigned precision and invalidate reproducible audio") {
  using namespace seam;
  native_ui::VoiceDesignerSession session; CHECK(session.create(designerFixture()));
  CHECK(session.beginAudition()); CHECK(drainDesigner(session)); const auto original = session.auditionAudio();
  CHECK(session.setSeed(session.epoch(), session.model()->revision(), "18446744073709551615"));
  CHECK(session.model()->recipe().seed == std::numeric_limits<std::uint64_t>::max()); CHECK(!session.auditionAudio());
  CHECK(session.beginAudition()); CHECK(drainDesigner(session)); CHECK(session.auditionAudio()->interleaved != original->interleaved);
  const auto revision = session.model()->revision(); const auto retained = session.auditionAudio();
  CHECK(session.setSeed(session.epoch(), revision, "18446744073709551615"));
  CHECK(session.model()->revision() == revision); CHECK(session.auditionAudio() == retained);
  for (const auto* invalid : {"", "-1", "+1", "01", "1.0", " 1", "18446744073709551616"}) {
    CHECK(!session.setSeed(session.epoch(), revision, invalid)); CHECK(session.model()->revision() == revision);
    CHECK(session.auditionAudio() == retained);
  }
  CHECK(!session.setSeed(session.epoch()+1U, revision, "1"));
  CHECK(session.undo(session.epoch(), revision)); CHECK(session.model()->recipe().seed == 0U);
  CHECK(session.beginAudition()); CHECK(drainDesigner(session)); CHECK(session.auditionAudio()->interleaved == original->interleaved);
}

TEST_CASE("Designer modulation controls produce periodic audio changes and zero-rate neutrality") {
  using namespace seam;
  native_ui::VoiceDesignerSession session; CHECK(session.create(designerFixture()));
  CHECK(session.beginAudition()); CHECK(drainDesigner(session)); const auto original = session.auditionAudio(); CHECK(original);
  for (unsigned control = 0U; control < 2U; ++control) {
    auto changed = session.model()->recipe(); changed.modulation.rateHz = 5.0;
    if (control == 0U) changed.modulation.jitterCents = 20.0;
    else changed.modulation.shimmerAmount = 0.3;
    CHECK(session.edit(session.epoch(), session.model()->revision(), changed)); CHECK(!session.auditionAudio());
    CHECK(session.beginAudition()); CHECK(drainDesigner(session));
    CHECK(session.auditionAudio()->interleaved != original->interleaved);
    CHECK(session.undo(session.epoch(), session.model()->revision()));
    CHECK(session.beginAudition()); CHECK(drainDesigner(session));
    CHECK(session.auditionAudio()->interleaved == original->interleaved);
  }
  auto disabled = session.model()->recipe(); disabled.modulation = {20.0, 0.3, 0.0};
  CHECK(session.edit(session.epoch(), session.model()->revision(), disabled));
  CHECK(session.beginAudition()); CHECK(drainDesigner(session)); CHECK(session.auditionAudio()->interleaved == original->interleaved);
  auto invalid = disabled; invalid.modulation.rateHz = 21.0;
  const auto revision = session.model()->revision(); const auto audio = session.auditionAudio();
  CHECK(!session.edit(session.epoch(), revision, invalid)); CHECK(session.model()->revision() == revision);
  CHECK(session.auditionAudio() == audio);
}

TEST_CASE("Designer preview cancellation cannot cancel an independent recipe save") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("designer-preview-cancel");
  native_ui::VoiceDesignerSession session; CHECK(session.create(designerFixture()));
  CHECK(session.beginAudition()); CHECK(session.beginSave(root / "voice.json"));
  session.cancelAudition();
  CHECK(drainDesigner(session)); CHECK(!session.busy()); CHECK(!session.auditionBusy());
  CHECK(!session.auditionAudio()); CHECK(!session.model()->dirty());
  const auto saved = voice_design::loadVoiceRecipeResource(root / "voice.json"); CHECK(saved);
  CHECK(saved.value().identity == session.model()->resource().identity);
}

TEST_CASE("Designer pose duplication and removal are validated undoable draft edits") {
  using namespace seam;
  native_ui::VoiceDesignerSession session; CHECK(session.create(designerFixture()));
  const auto epoch = session.epoch();
  CHECK(!session.removePose(epoch, session.model()->revision()));
  CHECK(!session.duplicatePose(epoch, session.model()->revision(), "a", "neutral"));
  CHECK(session.model()->revision() == 0U);
  CHECK(session.duplicatePose(epoch, 0U, "i", "soft"));
  CHECK(session.model()->recipe().poses.size() == 2U); CHECK(session.auditionPose() == 1U);
  CHECK(session.model()->recipe().poses[1].formants == designerFixture().poses[0].formants);
  CHECK(!session.duplicatePose(epoch, 0U, "u", "neutral"));
  CHECK(session.beginAudition()); CHECK(drainDesigner(session)); CHECK(session.auditionAudio());
  CHECK(session.removePose(epoch, session.model()->revision()));
  CHECK(session.model()->recipe() == designerFixture()); CHECK(!session.auditionAudio()); CHECK(session.auditionPose() == 0U);
  CHECK(session.undo(epoch, session.model()->revision())); CHECK(session.model()->recipe().poses.size() == 2U);
  CHECK(session.undo(epoch, session.model()->revision())); CHECK(session.model()->recipe() == designerFixture());
  CHECK(session.redo(epoch, session.model()->revision())); CHECK(session.model()->recipe().poses.size() == 2U);
  auto linked = session.model()->recipe(); linked.frications = {{"s", "soft", {.seed = 42U}}};
  CHECK(session.edit(epoch, session.model()->revision(), linked));
  CHECK(session.selectAudition(epoch, session.model()->revision(), 1U, 69U));
  const auto revision = session.model()->revision();
  CHECK(!session.removePose(epoch, revision));
  CHECK(session.model()->recipe() == linked); CHECK(session.model()->revision() == revision);
}

TEST_CASE("Designer session gestures invalidate preview and commit one undo") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("designer-session-gesture");
  native_ui::VoiceDesignerSession session; CHECK(session.create(designerFixture()));
  const auto epoch = session.epoch();
  CHECK(session.beginAudition()); CHECK(drainDesigner(session)); CHECK(session.auditionAudio());
  CHECK(session.beginGesture(epoch, session.model()->revision())); CHECK(!session.auditionAudio());
  CHECK(!session.beginSave(root / "during-gesture.json"));
  CHECK(!session.create(designerFixture(), true));
  for (unsigned index = 1U; index <= 10U; ++index) {
    auto preview = session.model()->recipe(); preview.phonation.aspiration = 0.05 + static_cast<double>(index) * 0.01;
    CHECK(session.updateGesture(epoch, session.model()->revision(), preview));
  }
  CHECK(!session.endGesture(epoch + 1U, session.model()->revision(), true)); CHECK(session.model()->gestureActive());
  CHECK(session.endGesture(epoch, session.model()->revision(), true)); CHECK(session.model()->canUndo());
  CHECK(session.undo(epoch, session.model()->revision())); CHECK(session.model()->recipe() == designerFixture());
  CHECK(!session.model()->canUndo());
  CHECK(session.beginGesture(epoch, session.model()->revision()));
  auto preview = session.model()->recipe(); preview.phonation.aspiration = 0.2;
  CHECK(session.updateGesture(epoch, session.model()->revision(), preview));
  CHECK(session.beginAudition());
  CHECK(session.endGesture(epoch, session.model()->revision(), false)); CHECK(drainDesigner(session));
  CHECK(!session.auditionAudio()); CHECK(session.model()->recipe() == designerFixture());
  CHECK(session.model()->canRedo()); CHECK(!std::filesystem::exists(root / "during-gesture.json"));
}

TEST_CASE("Designer A/B reference retains original recipe and PCM across edits") {
  using namespace seam;
  native_ui::VoiceDesignerSession session; CHECK(session.create(designerFixture()));
  CHECK(!session.pinAuditionReference(session.epoch(), session.model()->revision()));
  CHECK(session.beginAudition()); CHECK(drainDesigner(session));
  CHECK(session.pinAuditionReference(session.epoch(), session.model()->revision()));
  CHECK(session.referenceMatchesSelection());
  const auto reference = *session.auditionReference();
  auto edited = session.model()->recipe(); edited.phonation.aspiration = 0.3;
  CHECK(session.edit(session.epoch(), session.model()->revision(), edited));
  CHECK(session.auditionReference()->audio == reference.audio); CHECK(!session.auditionAudio());
  CHECK(session.referenceMatchesSelection());
  CHECK(!session.pinAuditionReference(session.epoch(), session.model()->revision()));
  CHECK(session.beginAudition()); CHECK(drainDesigner(session));
  CHECK(session.auditionAudio()->interleaved != reference.audio->interleaved);
  CHECK(voice_design::decodeVoiceRecipeResource(session.auditionReference()->resource).value() == designerFixture());
  CHECK(session.selectAudition(session.epoch(), session.model()->revision(), 0U, 72U));
  CHECK(!session.referenceMatchesSelection()); CHECK(session.auditionReference()->audio == reference.audio);
  CHECK(session.selectAudition(session.epoch(), session.model()->revision(), 0U, 69U)); CHECK(session.referenceMatchesSelection());
  CHECK(session.create(designerFixture(), true)); CHECK(!session.auditionReference());
  CHECK(reference.audio->frameCount() == 48000U);
  CHECK(session.beginAudition()); CHECK(drainDesigner(session));
  CHECK(session.pinAuditionReference(session.epoch(), session.model()->revision()));
  const auto hash = session.model()->resource().identity.contentHash; const auto revision = session.model()->revision();
  session.clearAuditionReference(); CHECK(!session.auditionReference());
  CHECK(session.model()->resource().identity.contentHash == hash); CHECK(session.model()->revision() == revision);
}

TEST_CASE("Designer resonance edits affect audition and undo restores exact audio") {
  using namespace seam;
  native_ui::VoiceDesignerSession session; CHECK(session.create(designerFixture()));
  CHECK(session.beginAudition()); CHECK(drainDesigner(session)); const auto original = session.auditionAudio(); CHECK(original);
  for (unsigned control = 0U; control < 3U; ++control) {
    auto edited = session.model()->recipe();
    auto& band = edited.poses.front().formants.front();
    if (control == 0U) band.frequencyHz += 100.0;
    if (control == 1U) band.bandwidthHz += 50.0;
    if (control == 2U) band.gainDb -= 6.0;
    CHECK(session.edit(session.epoch(), session.model()->revision(), edited)); CHECK(!session.auditionAudio());
    CHECK(session.beginAudition()); CHECK(drainDesigner(session));
    CHECK(session.auditionAudio()->interleaved != original->interleaved);
    CHECK(session.undo(session.epoch(), session.model()->revision()));
    CHECK(session.beginAudition()); CHECK(drainDesigner(session));
    CHECK(session.auditionAudio()->interleaved == original->interleaved);
  }
  auto crossing = session.model()->recipe(); crossing.poses.front().formants.front().frequencyHz = 1200.0;
  const auto revision = session.model()->revision(); const auto preserved = session.auditionAudio();
  CHECK(!session.edit(session.epoch(), revision, crossing));
  CHECK(session.model()->revision() == revision); CHECK(session.auditionAudio() == preserved);
}

TEST_CASE("Designer audition selection invalidates old output without dirtying the recipe") {
  using namespace seam;
  auto recipe = designerFixture();
  recipe.poses.push_back({"i", "soft", 0.0, {{300.0, 80.0, 0.0}, {2200.0, 100.0, -3.0}, {3000.0, 140.0, -6.0}}});
  native_ui::VoiceDesignerSession session; CHECK(session.create(recipe));
  const auto epoch = session.epoch(); const auto revision = session.model()->revision();
  const auto hash = session.model()->resource().identity.contentHash;
  CHECK(session.beginAudition());
  CHECK(session.selectAudition(epoch, revision, 1U, 72U));
  CHECK(drainDesigner(session)); CHECK(!session.auditionAudio());
  CHECK(session.model()->revision() == revision); CHECK(session.model()->resource().identity.contentHash == hash);
  CHECK(session.beginAudition()); CHECK(drainDesigner(session)); CHECK(session.auditionAudio());
  const auto selectedAudio = session.auditionAudio();
  const auto expected = native_ui::renderDesignerAudition(session.model()->resource(), 1U, 72U); CHECK(expected);
  CHECK(selectedAudio->interleaved == expected.value().interleaved);
  CHECK(session.selectAudition(epoch, revision, 1U, 72U)); CHECK(session.auditionAudio() == selectedAudio);
  CHECK(!session.selectAudition(epoch, revision, 2U, 72U)); CHECK(session.auditionAudio() == selectedAudio);
  CHECK(!session.selectAudition(epoch + 1U, revision, 0U, 69U));
  CHECK(session.selectAudition(epoch, revision, 1U, 69U)); CHECK(!session.auditionAudio());
  CHECK(session.beginAudition()); CHECK(drainDesigner(session));
  CHECK(session.auditionAudio()->interleaved != selectedAudio->interleaved);
  CHECK(session.edit(epoch, revision, designerFixture())); CHECK(session.auditionPose() == 0U);
}

TEST_CASE("Designer audition renders deterministic bounded PCM and follows phonation edits") {
  using namespace seam;
  const auto resource = voice_design::freezeVoiceRecipeResource(designerFixture()); CHECK(resource);
  const auto first = native_ui::renderDesignerAudition(resource.value(), 0U); CHECK(first);
  const auto repeat = native_ui::renderDesignerAudition(resource.value(), 0U); CHECK(repeat);
  CHECK(first.value().sampleRate == 48000U); CHECK(first.value().channels == 1U); CHECK(first.value().frameCount() == 48000U);
  CHECK(first.value().interleaved == repeat.value().interleaved);
  CHECK(first.value().interleaved.front() == 0.0F); CHECK(first.value().interleaved.back() == 0.0F);
  CHECK(std::all_of(first.value().interleaved.begin(), first.value().interleaved.end(), [](float sample) { return std::isfinite(sample) && std::abs(sample) <= 0.90001F; }));
  CHECK(std::any_of(first.value().interleaved.begin(), first.value().interleaved.end(), [](float sample) { return std::abs(sample) > 0.00001F; }));
  auto changed = designerFixture(); changed.phonation.aspiration = 0.4;
  const auto changedResource = voice_design::freezeVoiceRecipeResource(changed); CHECK(changedResource);
  const auto renderedChange = native_ui::renderDesignerAudition(changedResource.value(), 0U); CHECK(renderedChange);
  CHECK(renderedChange.value().interleaved != first.value().interleaved);
  CHECK(!native_ui::renderDesignerAudition(resource.value(), 1U));
  CHECK(!native_ui::renderDesignerAudition(resource.value(), 0U, 0U));
  std::stop_source stop; stop.request_stop();
  CHECK(!native_ui::renderDesignerAudition(resource.value(), 0U, 69U, stop.get_token()));
}

TEST_CASE("Designer audition worker discards old voice revisions without blocking editing") {
  using namespace seam;
  native_ui::VoiceDesignerSession session; CHECK(session.create(designerFixture()));
  CHECK(session.beginAudition()); CHECK(session.auditionBusy()); CHECK(!session.beginAudition());
  auto changed = session.model()->recipe(); changed.phonation.aspiration = 0.4;
  CHECK(session.edit(session.epoch(), session.model()->revision(), changed));
  CHECK(drainDesigner(session)); CHECK(!session.auditionAudio());
  CHECK(session.beginAudition()); CHECK(drainDesigner(session)); CHECK(session.auditionAudio());
  const auto retained = session.auditionAudio(); CHECK(retained->frameCount() == 48000U);
  CHECK(session.undo(session.epoch(), session.model()->revision())); CHECK(!session.auditionAudio());
  CHECK(retained->frameCount() == 48000U);
  CHECK(session.beginAudition()); CHECK(session.finish()); CHECK(!session.auditionBusy()); CHECK(!session.auditionAudio());
}

TEST_CASE("Designer session saves reopens and rejects old document actions") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("designer-session");
  native_ui::VoiceDesignerSession session;
  CHECK(session.create(designerFixture())); CHECK(session.model()->dirty());
  CHECK(!session.beginOpen(root / "missing.json"));
  const auto epoch = session.epoch();
  CHECK(session.beginSave(root / "voice.json")); CHECK(session.busy());
  CHECK(!session.edit(epoch, 0U, designerFixture()));
  CHECK(drainDesigner(session)); CHECK(!session.model()->dirty());
  auto changed = session.model()->recipe(); changed.phonation.aspiration = 0.25;
  CHECK(session.edit(epoch, session.model()->revision(), changed)); CHECK(session.model()->dirty());
  CHECK(session.beginSave(root / "voice.json")); CHECK(drainDesigner(session));
  CHECK(!session.model()->dirty()); CHECK(session.model()->canUndo());
  CHECK(voice_design::decodeVoiceRecipeResource(voice_design::loadVoiceRecipeResource(root / "voice.json").value()).value() == changed);
  CHECK(session.beginOpen(root / "voice.json")); CHECK(drainDesigner(session));
  CHECK(session.epoch() != epoch); CHECK(session.model()->revision() == 0U);
  CHECK(!session.edit(epoch, 0U, designerFixture())); CHECK(session.model()->recipe() == changed);
  CHECK(session.beginOpen(root / "missing.json")); CHECK(!drainDesigner(session));
  CHECK(session.model()->recipe() == changed); CHECK(!session.model()->dirty());
}

TEST_CASE("Designer session protects edited external files and preserves failed-save state") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("designer-save-conflict");
  native_ui::VoiceDesignerSession session;
  CHECK(session.create(designerFixture())); CHECK(session.beginSave(root / "voice.json")); CHECK(drainDesigner(session));
  auto edited = session.model()->recipe(); edited.seed = 42U;
  CHECK(session.edit(session.epoch(), session.model()->revision(), edited));
  const auto expectedRevision = session.model()->revision();
  auto outside = designerFixture(); outside.seed = 9U;
  CHECK(voice_design::saveVoiceRecipeFile(root / "voice.json", outside));
  CHECK(session.beginSave(root / "voice.json")); CHECK(!drainDesigner(session));
  CHECK(session.model()->dirty()); CHECK(session.model()->revision() == expectedRevision);
  CHECK(session.model()->recipe() == edited); CHECK(session.model()->canUndo());
  CHECK(voice_design::decodeVoiceRecipeResource(voice_design::loadVoiceRecipeResource(root / "voice.json").value()).value() == outside);
  CHECK(voice_design::saveVoiceRecipeFile(root / "existing.json", outside));
  CHECK(session.beginSave(root / "existing.json")); CHECK(!drainDesigner(session)); CHECK(session.model()->dirty());
  {
    core::ExclusiveFileLock lock; CHECK(lock.acquire(root / "new.json.designer.lock"));
    CHECK(session.beginSave(root / "new.json")); CHECK(!drainDesigner(session));
    CHECK(!std::filesystem::exists(root / "new.json"));
  }
  CHECK(session.beginSave(root / "new.json")); CHECK(drainDesigner(session)); CHECK(!session.model()->dirty());
  CHECK(session.path() == root / "new.json");
  CHECK(session.beginOpen(root / "absent.json")); CHECK(!session.finish());
  CHECK(!session.busy()); CHECK(session.model()->recipe() == edited);
}

TEST_CASE("Designer discard authorization replaces only after successful validation or loading") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("designer-discard");
  native_ui::VoiceDesignerSession session;
  CHECK(session.create(designerFixture()));
  auto edited = session.model()->recipe(); edited.seed = 987U;
  CHECK(session.edit(session.epoch(), session.model()->revision(), edited));
  const auto epoch = session.epoch();
  CHECK(!session.create(designerFixture()));
  CHECK(session.beginOpen(root / "missing.json", true)); CHECK(!drainDesigner(session));
  CHECK(session.epoch() == epoch); CHECK(session.model()->recipe() == edited); CHECK(session.model()->dirty());
  auto invalid = designerFixture(); invalid.poses.clear();
  CHECK(!session.create(invalid, true)); CHECK(session.model()->recipe() == edited);
  CHECK(session.create(designerFixture(), true));
  CHECK(session.epoch() != epoch); CHECK(session.model()->recipe() == designerFixture());
  CHECK(session.model()->dirty()); CHECK(!session.model()->canUndo());
}

TEST_CASE("Designer gesture previews collapse to one undo and reject stale completion") {
  using namespace seam;
  auto created = native_ui::VoiceDesignerModel::create(designerFixture()); CHECK(created);
  auto& model = created.value(); const auto original = model.recipe();
  CHECK(model.acknowledgeSave(model.revision(), model.resource().identity.contentHash));
  CHECK(model.beginGesture(0U)); CHECK(model.gestureActive());
  CHECK(!model.beginGesture(model.revision()));
  CHECK(!model.edit(model.revision(), original));
  CHECK(!model.canUndo()); CHECK(!model.undo(model.revision()));
  for (unsigned index = 1U; index <= 100U; ++index) {
    auto preview = model.recipe(); preview.phonation.aspiration = static_cast<double>(index) / 200.0;
    CHECK(model.updateGesture(model.revision(), preview));
  }
  const auto previewResource = model.resource(); const auto previewRevision = model.revision();
  CHECK(!model.acknowledgeSave(model.revision(), previewResource.identity.contentHash));
  CHECK(!model.commitGesture(1U)); CHECK(model.gestureActive());
  CHECK(model.commitGesture(previewRevision)); CHECK(!model.gestureActive());
  CHECK(model.canUndo()); CHECK(model.dirty()); CHECK(model.revision() > previewRevision);
  CHECK(!model.updateGesture(previewRevision, original));
  CHECK(model.undo(model.revision())); CHECK(model.recipe() == original); CHECK(!model.canUndo());
  CHECK(!model.dirty()); CHECK(model.canRedo());
  CHECK(voice_design::decodeVoiceRecipeResource(previewResource).value().phonation.aspiration == 0.5);
  CHECK(model.redo(model.revision())); CHECK(model.recipe().phonation.aspiration == 0.5);
}

TEST_CASE("Designer cancelled and neutral gestures preserve prior redo and saved identity") {
  using namespace seam;
  auto created = native_ui::VoiceDesignerModel::create(designerFixture()); CHECK(created);
  auto& model = created.value(); const auto initial = model.recipe();
  CHECK(model.acknowledgeSave(model.revision(), model.resource().identity.contentHash));
  auto edited = initial; edited.seed = 123U;
  CHECK(model.edit(model.revision(), edited)); CHECK(model.undo(model.revision()));
  CHECK(model.beginGesture(model.revision()));
  CHECK(model.updateGesture(model.revision(), edited));
  auto invalid = edited; invalid.phonation.openQuotient = 2.0;
  const auto beforeFailure = model.revision();
  CHECK(!model.updateGesture(beforeFailure, invalid)); CHECK(model.revision() == beforeFailure);
  CHECK(model.recipe() == edited); CHECK(model.gestureActive());
  CHECK(model.cancelGesture(model.revision())); CHECK(model.recipe() == initial);
  CHECK(!model.dirty()); CHECK(model.canRedo());
  CHECK(model.beginGesture(model.revision()));
  CHECK(model.updateGesture(model.revision(), edited));
  CHECK(model.updateGesture(model.revision(), initial));
  CHECK(model.commitGesture(model.revision())); CHECK(!model.canUndo()); CHECK(model.canRedo());
  CHECK(!model.dirty());
  CHECK(!model.cancelGesture(model.revision())); CHECK(!model.commitGesture(model.revision()));
  CHECK(model.redo(model.revision())); CHECK(model.recipe() == edited);
}

TEST_CASE("Voice Designer frozen edits undo without reviving stale actions") {
  using namespace seam;
  const auto initial = designerFixture();
  auto created = native_ui::VoiceDesignerModel::create(initial); CHECK(created);
  auto& model = created.value();
  const auto originalResource = model.resource();
  CHECK(model.dirty()); CHECK(!model.canUndo());
  CHECK(model.acknowledgeSave(0U, originalResource.identity.contentHash)); CHECK(!model.dirty());
  auto edited = model.recipe();
  edited.phonation.aspiration = 0.2;
  edited.poses.front().formants.front().frequencyHz = 800.0;
  CHECK(model.edit(0U, edited)); CHECK(model.revision() == 1U); CHECK(model.dirty());
  CHECK(model.resource().identity.contentHash != originalResource.identity.contentHash);
  CHECK(voice_design::decodeVoiceRecipeResource(originalResource).value() == initial);
  const auto editedHash = model.resource().identity.contentHash;
  CHECK(!model.edit(0U, initial)); CHECK(model.recipe() == edited);
  CHECK(!model.acknowledgeSave(0U, originalResource.identity.contentHash)); CHECK(model.dirty());
  CHECK(model.undo(1U)); CHECK(model.recipe() == initial); CHECK(!model.dirty());
  CHECK(model.revision() == 2U); CHECK(!model.edit(0U, edited));
  CHECK(model.edit(2U, initial)); CHECK(model.revision() == 2U); CHECK(model.canRedo());
  CHECK(model.redo(2U)); CHECK(model.recipe() == edited); CHECK(model.revision() == 3U);
  CHECK(model.acknowledgeSave(3U, editedHash)); CHECK(!model.dirty());
  CHECK(model.undo(3U)); CHECK(model.dirty());
  auto branched = model.recipe(); branched.seed = 91U;
  CHECK(model.edit(4U, branched)); CHECK(!model.canRedo());
}

TEST_CASE("Voice Designer rejects invalid source and resonance edits atomically") {
  using namespace seam;
  auto created = native_ui::VoiceDesignerModel::create(designerFixture()); CHECK(created);
  auto& model = created.value();
  const auto original = model.recipe();
  const auto hash = model.resource().identity.contentHash;
  for (unsigned scenario = 0U; scenario < 4U; ++scenario) {
    auto invalid = original;
    if (scenario == 0U) invalid.phonation.aspiration = std::numeric_limits<double>::quiet_NaN();
    if (scenario == 1U) invalid.poses.front().formants.front().frequencyHz = 1300.0;
    if (scenario == 2U) invalid.id = "different-voice";
    if (scenario == 3U) invalid.engineId = "different-engine";
    CHECK(!model.edit(0U, invalid));
    CHECK(model.recipe() == original); CHECK(model.resource().identity.contentHash == hash);
    CHECK(model.revision() == 0U); CHECK(!model.canUndo());
  }
  CHECK(!model.undo(0U)); CHECK(!model.redo(0U));
  CHECK(!model.acknowledgeSave(0U, std::string(64U, '0'))); CHECK(model.dirty());
}

TEST_CASE("Voice Designer keeps a bounded undo history across recipe schema changes") {
  using namespace seam;
  auto created = native_ui::VoiceDesignerModel::create(designerFixture()); CHECK(created);
  auto& model = created.value();
  auto articulated = model.recipe(); articulated.frications = {{"s", "neutral", {.seed = 42U}}};
  CHECK(model.edit(model.revision(), articulated)); CHECK(model.resource().identity.version == "2");
  CHECK(model.undo(model.revision())); CHECK(model.resource().identity.version == "1");
  for (std::uint64_t index = 1U; index <= 140U; ++index) {
    auto next = model.recipe(); next.seed = index;
    CHECK(model.edit(model.revision(), next));
  }
  unsigned undos = 0U;
  while (model.canUndo()) { CHECK(model.undo(model.revision())); ++undos; }
  CHECK(undos == 128U); CHECK(model.recipe().seed == 12U);
  unsigned redos = 0U;
  while (model.canRedo()) { CHECK(model.redo(model.revision())); ++redos; }
  CHECK(redos == 128U); CHECK(model.recipe().seed == 140U);
}

// An installed singer is signed and immutable. The Designer must not be able to save a draft over
// one, whether the caller declared a protected root or not, because the installed layout is
// self-describing.
TEST_CASE("The Designer refuses to save a draft over an installed singer") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("designer-protected-roots");
  // An installed-looking resource: a manifest and receipt beside the recipe it would overwrite.
  const auto installed = root / "singers" / "some.singer" / "1.0.0";
  std::filesystem::create_directories(installed);
  std::ofstream(installed / "manifest.json") << "{}";
  std::ofstream(installed / "install-receipt.json") << "{}";
  CHECK(voice_design::saveVoiceRecipeFile(installed / "recipe.json", designerFixture()));
  const auto installedBefore = core::sha256File(installed / "recipe.json", 1024U * 1024U);
  CHECK(installedBefore.hasValue());
  if (!installedBefore) return;

  native_ui::VoiceDesignerSession session;
  CHECK(session.create(designerFixture()));
  CHECK(session.model() != nullptr);
  // The installation is detected from its own layout, so a caller that declares nothing is still
  // protected, and the recipe that is already there is untouched.
  CHECK(!session.beginSave(installed / "recipe.json"));
  CHECK(!session.busy());
  const auto installedAfter = core::sha256File(installed / "recipe.json", 1024U * 1024U);
  CHECK(installedAfter.hasValue());
  if (!installedAfter) return;
  CHECK(installedAfter.value() == installedBefore.value());

  // A declared protected root refuses a nested save even where no marker exists yet, and a draft
  // outside every root still saves normally.
  session.setProtectedRoots({root / "singers"});
  CHECK(!session.beginSave(root / "singers" / "new.singer" / "1.0.0" / "recipe.json"));
  CHECK(!session.busy());
  std::filesystem::create_directories(root / "drafts");
  const auto draft = root / "drafts" / "my-singer.json";
  CHECK(session.beginSave(draft));
  CHECK(drainDesigner(session));
  CHECK(std::filesystem::exists(draft));
}

// A sibling file in the installed directory must be refused too, not only the recipe entry, because
// any write into signed content risks corrupting a verified installation.
TEST_CASE("The Designer refuses any save inside a protected root and leaves prior saves working") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("designer-protected-sibling");
  native_ui::VoiceDesignerSession session;
  session.setProtectedRoots({root / "installed"});
  CHECK(session.create(designerFixture()));
  CHECK(!session.beginSave(root / "installed" / "sibling.json"));
  CHECK(!session.busy());
  // The refusal is about location, not about the session: a normal save still works afterwards.
  std::filesystem::create_directories(root / "drafts");
  const auto allowed = root / "drafts" / "fine.json";
  CHECK(session.beginSave(allowed));
  CHECK(drainDesigner(session));
  CHECK(!session.path().empty());
  // A second save to the same draft path is still allowed, so the guard did not break normal saves.
  CHECK(session.beginSave(allowed));
  CHECK(drainDesigner(session));
}
