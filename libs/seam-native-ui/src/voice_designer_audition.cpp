#include "seam/native_ui/voice_designer_audition.hpp"
#include "seam/voice_design/phonation_source.hpp"
#include "seam/voice_design/vocal_tract.hpp"
#include "seam/voice_design/frication_source.hpp"
#include "seam/voice_design/plosive_source.hpp"
#include "seam/voice_design/articulated_stream.hpp"
#include "seam/phonemizer/phonemizer.hpp"
#include "seam/domain/project.hpp"
#include <algorithm>
#include <cmath>

namespace seam::native_ui {
namespace {
core::Result<voicebank::AudioBuffer> finishAudition(std::vector<float> samples, std::stop_token stopToken) {
  using Output = voicebank::AudioBuffer;
  float peak = 0.0F;
  for (const auto sample : samples) {
    if (!std::isfinite(sample)) return core::failure<Output>(core::ErrorCode::InvalidState, "Designer preview produced non-finite audio");
    peak = std::max(peak, std::abs(sample));
  }
  const auto gain = peak > 0.9F ? 0.9F / peak : 1.0F;
  for (std::size_t index = 0U; index < samples.size(); ++index) {
    const auto edge = std::min(index, samples.size()-1U-index);
    samples[index] *= gain * std::min(1.0F,static_cast<float>(edge)/240.0F);
  }
  if (stopToken.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict,"Designer audition cancelled");
  return Output{48000U,1U,32U,std::move(samples)};
}
core::Result<voicebank::AudioBuffer> renderNoisePhraseAudition(const synthesis::ProceduralSingerResource& resource,
    std::size_t index,std::size_t vowelPoseIndex,std::uint8_t midiKey,std::stop_token stopToken, bool plosive, bool coda) {
  using Output=voicebank::AudioBuffer;
  const auto recipe=voice_design::decodeVoiceRecipeResource(resource,stopToken);
  if (!recipe) return core::Result<Output>{recipe.error()};
  if (index>=(plosive?recipe.value().plosives.size():recipe.value().frications.size()) || vowelPoseIndex>=recipe.value().poses.size() || midiKey<36U || midiKey>96U)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,"Select a noise source, vowel pose and supported audition pitch");
  const auto& phone=plosive?recipe.value().plosives[index].phone:recipe.value().frications[index].phone;
  const auto& style=plosive?recipe.value().plosives[index].style:recipe.value().frications[index].style;
  const auto& vowel=recipe.value().poses[vowelPoseIndex];
  const bool sourceVoiced=!plosive && recipe.value().frications[index].voicingGain.has_value();
  if (style!=vowel.style || !phonemizer::isVowelSymbol(vowel.phone))
    return core::failure<Output>(core::ErrorCode::Unsupported,"Noise phrase audition requires a vowel pose in the source style");
  domain::Project project{domain::ProjectId{1U},"Designer consonant/vowel audition"};
  domain::VocalRegion region{.id=domain::RegionId{3U},.name="Consonant and vowel",.durationTick=time::Tick{1920},
      .lyrics={{domain::LyricTokenId{4U},U"preview",domain::Language::English}},
      .notes={{.id=domain::NoteId{5U},.durationTick=time::Tick{1920},.midiKey=midiKey,.lyricTokenId=domain::LyricTokenId{4U}}}};
  // The scratch lyric satisfies score ownership; explicit tokens below, not
  // lyric phonemization, define the selected source/vowel pair.
  const auto nucleus=plosive?static_cast<std::int64_t>(std::llround((50.0+recipe.value().plosives[index].burstMilliseconds)*1000.0)):150000;
  std::vector<domain::PhonemeToken> phones{
      {.key={domain::NoteId{5U},0U},.symbol=phone,.role=domain::PhonemeRole::Onset,.voiced=sourceVoiced,.timing={.startOffset=0}},
      {.key={domain::NoteId{5U},1U},.symbol=vowel.phone,.role=domain::PhonemeRole::Nucleus,.voiced=true,.timing={.startOffset=nucleus}}};
  if (coda) {
    const auto codaStart=1000000-nucleus;
    phones={
        {.key={domain::NoteId{5U},0U},.symbol=vowel.phone,.role=domain::PhonemeRole::Nucleus,.voiced=true,.timing={.startOffset=0,.endOffset=codaStart}},
        {.key={domain::NoteId{5U},1U},.symbol=phone,.role=domain::PhonemeRole::Coda,.voiced=sourceVoiced,.timing={.startOffset=codaStart}}};
  }
  auto performance=synthesis::compileScorePerformance(project,region,48000U,phones);
  if (!performance) return core::Result<Output>{performance.error()};
  auto stream=voice_design::ArticulatedStream::createFromRecipe(resource,std::move(performance.value()),phones,vowel.style,512U,stopToken);
  if (!stream) return core::Result<Output>{stream.error()};
  auto rendered=stream.value().renderOwned({0,48000},stopToken);
  if (!rendered) return core::Result<Output>{rendered.error()};
  return finishAudition(std::move(rendered.value().samples),stopToken);
}
}
core::Result<voicebank::AudioBuffer> renderDesignerPlosivePhraseAudition(const synthesis::ProceduralSingerResource& resource,
    std::size_t index,std::size_t vowelPoseIndex,std::uint8_t midiKey,std::stop_token stopToken,PlosiveAuditionMode mode) {
  if (mode!=PlosiveAuditionMode::StopVowel && mode!=PlosiveAuditionMode::VowelStop)
    return core::failure<voicebank::AudioBuffer>(core::ErrorCode::InvalidArgument,"Choose stop-vowel or vowel-stop phrase audition");
  return renderNoisePhraseAudition(resource,index,vowelPoseIndex,midiKey,stopToken,true,mode==PlosiveAuditionMode::VowelStop);
}
core::Result<voicebank::AudioBuffer> renderDesignerFricationPhraseAudition(const synthesis::ProceduralSingerResource& resource,
    std::size_t index,std::size_t vowelPoseIndex,std::uint8_t midiKey,std::stop_token stopToken,FricationAuditionMode mode) {
  if (mode!=FricationAuditionMode::FricationVowel && mode!=FricationAuditionMode::VowelFrication)
    return core::failure<voicebank::AudioBuffer>(core::ErrorCode::InvalidArgument,"Choose frication-vowel or vowel-frication phrase audition");
  return renderNoisePhraseAudition(resource,index,vowelPoseIndex,midiKey,stopToken,false,mode==FricationAuditionMode::VowelFrication);
}
core::Result<voicebank::AudioBuffer> renderDesignerPlosiveAudition(const synthesis::ProceduralSingerResource& resource,
    std::size_t index, std::stop_token stopToken) {
  using Output=voicebank::AudioBuffer;
  const auto recipe=voice_design::decodeVoiceRecipeResource(resource,stopToken);
  if (!recipe) return core::Result<Output>{recipe.error()};
  if (index>=recipe.value().plosives.size()) return core::failure<Output>(core::ErrorCode::InvalidArgument,"Select a valid plosive source");
  const auto& pose=recipe.value().plosives[index];
  const auto burst=static_cast<std::uint32_t>(std::llround(pose.burstMilliseconds*48.0));
  auto source=voice_design::PlosiveSource::create({pose.source,2400U,burst},48000U,0);
  if (!source) return core::Result<Output>{source.error()};
  auto rendered=source.value().render(2400U+burst,stopToken);
  if (!rendered) return core::Result<Output>{rendered.error()};
  rendered.value().samples.resize(24000U,0.0F);
  return finishAudition(std::move(rendered.value().samples),stopToken);
}
core::Result<voicebank::AudioBuffer> renderDesignerFricationAudition(const synthesis::ProceduralSingerResource& resource,
    std::size_t index, std::stop_token stopToken) {
  using Output = voicebank::AudioBuffer;
  const auto recipe = voice_design::decodeVoiceRecipeResource(resource,stopToken);
  if (!recipe) return core::Result<Output>{recipe.error()};
  if (index >= recipe.value().frications.size()) return core::failure<Output>(core::ErrorCode::InvalidArgument,"Select a valid frication source");
  if (recipe.value().frications[index].voicingGain)
    return core::failure<Output>(core::ErrorCode::Unsupported,"Voiced frication requires CV or VC audition; isolated noise would omit its voiced component");
  auto source = voice_design::FricationSource::create(recipe.value().frications[index].source,48000U,0);
  if (!source) return core::Result<Output>{source.error()};
  auto rendered = source.value().render(48000U,stopToken);
  if (!rendered) return core::Result<Output>{rendered.error()};
  return finishAudition(std::move(rendered.value().samples),stopToken);
}
core::Result<voicebank::AudioBuffer> renderDesignerAudition(const synthesis::ProceduralSingerResource& resource,
    std::size_t poseIndex, std::uint8_t midiKey, std::stop_token stopToken) {
  using Output = voicebank::AudioBuffer;
  auto recipe = voice_design::decodeVoiceRecipeResource(resource, stopToken);
  if (!recipe) return core::Result<Output>{recipe.error()};
  if (poseIndex >= recipe.value().poses.size() || midiKey < 36U || midiKey > 96U)
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Select a recipe pose and an audition pitch from MIDI 36 to 96");
  domain::Project project{domain::ProjectId{1U}, "Designer audition"};
  domain::VocalRegion region{.id = domain::RegionId{3U}, .name = "Sustained pose", .durationTick = time::Tick{1920},
      .lyrics = {{domain::LyricTokenId{4U}, U"あ", domain::Language::Japanese}},
      .notes = {{.id = domain::NoteId{5U}, .durationTick = time::Tick{1920}, .midiKey = midiKey, .lyricTokenId = domain::LyricTokenId{4U}}}};
  project.vocalTracks().push_back({.id = domain::TrackId{2U}, .name = "Draft voice", .regions = {region}});
  const auto performance = synthesis::compileScorePerformance(project, region, 48000U);
  if (!performance) return core::Result<Output>{performance.error()};
  auto source = voice_design::PhonationSource::create(recipe.value(), performance.value(), 0);
  if (!source) return core::Result<Output>{source.error()};
  const auto& pose = recipe.value().poses[poseIndex];
  auto tract = voice_design::VocalTract::create(recipe.value(), pose.phone, pose.style, 48000U);
  if (!tract) return core::Result<Output>{tract.error()};
  auto excitation = source.value().render(48000U, stopToken);
  if (!excitation) return core::Result<Output>{excitation.error()};
  auto filtered = tract.value().process(excitation.value().samples, stopToken);
  if (!filtered) return core::Result<Output>{filtered.error()};
  return finishAudition(std::move(filtered.value()), stopToken);
}
}
