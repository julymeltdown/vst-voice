#include "seam/voice_design/articulation_plan.hpp"
#include "seam/voice_design/vocal_tract.hpp"
#include "seam/phonemizer/phonemizer.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <cmath>

namespace seam::voice_design {
core::Result<ArticulationPlan> ArticulationPlan::compileRecipe(
    const synthesis::ProceduralSingerResource& resource,
    const synthesis::CompiledScorePerformance& performance,
    std::span<const domain::PhonemeToken> phones, std::string_view style, std::stop_token stop, bool allowVoicedFrication, bool allowVoicedStops) {
  const auto cancelled = [] { return core::failure<ArticulationPlan>(core::ErrorCode::Conflict, "Articulation preparation cancelled"); };
  if (stop.stop_requested()) return cancelled();
  if (performance.notes().empty() || performance.notes().size() > 4096U || phones.empty() || phones.size() > 16384U)
    return core::failure<ArticulationPlan>(core::ErrorCode::InvalidArgument, "Recipe articulation score coverage exceeds bounds");
  const auto recipe = decodeVoiceRecipeResource(resource, stop, allowVoicedFrication, allowVoicedStops);
  if (!recipe) return core::Result<ArticulationPlan>{recipe.error()};
  std::set<std::string> requestedFrication;
  std::set<std::string> requestedStops;
  for (const auto& phone : phones) if (
      (phone.role==domain::PhonemeRole::Onset || phone.role==domain::PhonemeRole::Coda))
    requestedFrication.insert(phone.symbol);
  for (const auto& phone : phones) if (
      (phone.role==domain::PhonemeRole::Onset || phone.role==domain::PhonemeRole::Coda)) requestedStops.insert(phone.symbol);
  std::vector<FricationBinding> bindings;
  std::vector<PlosiveBinding> plosives;
  for (const auto& pose : recipe.value().plosives) if (pose.style == style && requestedStops.contains(pose.phone))
    plosives.push_back({pose.phone, pose.source, pose.burstMilliseconds, pose.voicedClosure});
  for (const auto& pose : recipe.value().frications) if (pose.style == style && requestedFrication.contains(pose.phone))
    bindings.push_back({pose.phone, pose.source, pose.voicingGain});
  const auto notes = performance.notes();
  std::vector<std::string> nasals;
  for (const auto& pose:recipe.value().poses) if (pose.style==style && pose.nasal && pose.nasalCoupling>0.0 && phonemizer::isNasalSymbol(pose.phone))
    nasals.push_back(pose.phone);
  const synthesis::PhraseFrameRange context{notes.front().startFrame, notes.back().endFrame};
  auto plan = compile(phones, performance.phonemeTiming(), bindings, performance.sampleRate(), context,nasals,plosives);
  if (!plan) return core::failure<ArticulationPlan>(plan.error().code,
      "Recipe '" + recipe.value().id + "', style '" + std::string(style) + "': " + plan.error().message);
  std::map<domain::NoteId, const synthesis::ScoreNoteSpan*> scoreNotes;
  for (const auto& note : notes) scoreNotes.emplace(note.id, &note);
  std::set<domain::NoteId> covered;
  std::set<std::string> checkedVowels;
  for (const auto& gesture : plan.value().gestures()) {
    if (stop.stop_requested()) return cancelled();
    const auto found = scoreNotes.find(gesture.key.noteId);
    if (found == scoreNotes.end() || gesture.span.start < found->second->startFrame || gesture.span.end > found->second->endFrame)
      return core::failure<ArticulationPlan>(core::ErrorCode::Unsupported, "Articulation outside its score note requires extended phonation context");
    covered.insert(gesture.key.noteId);
    if (isVoicedGesture(gesture.kind) && gesture.kind != ArticulationGestureKind::VoicedPlosive && checkedVowels.insert(gesture.phone).second) {
      const auto tract = VocalTract::create(recipe.value(), gesture.phone, style, performance.sampleRate());
      if (!tract) return core::Result<ArticulationPlan>{tract.error()};
    }
  }
  if (checkedVowels.empty() || covered.size() != notes.size()) return core::failure<ArticulationPlan>(
      core::ErrorCode::Unsupported, "Recipe articulation requires voiced poses and complete score-note coverage");
  if (stop.stop_requested()) return cancelled();
  return plan;
}

core::Result<ArticulationPlan> ArticulationPlan::compile(
    std::span<const domain::PhonemeToken> phones,
    std::span<const synthesis::PhonemeTimingAnchor> timing,
    std::span<const FricationBinding> bindings, std::uint32_t sampleRate,
    synthesis::PhraseFrameRange context, std::span<const std::string> nasalBindings,
    std::span<const PlosiveBinding> plosiveBindings) {
  const auto invalid = [](const char* message) { return core::failure<ArticulationPlan>(core::ErrorCode::InvalidArgument, message); };
  if (phones.empty() || phones.size() > 16384U || phones.size() != timing.size() || bindings.size() > 64U || nasalBindings.size()>64U || plosiveBindings.size()>64U ||
      sampleRate < 8000U || sampleRate > 384000U || context.start < 0 || context.end <= context.start || context.end > (time::SampleFrame{1} << 52))
    return invalid("Articulation input or context exceeds bounds");
  std::map<std::string, FricationBinding, std::less<>> sources;
  std::set<std::string,std::less<>> nasals;
  for (const auto& phone:nasalBindings) if (!phonemizer::isNasalSymbol(phone) || !nasals.insert(phone).second)
    return invalid("Nasal binding is unsupported or duplicated");
  for (const auto& binding : bindings) {
    if (binding.phone.empty() || binding.phone.size() > 128U ||
        (binding.voicingGain && (!std::isfinite(*binding.voicingGain) || *binding.voicingGain<=0.0 || *binding.voicingGain>1.0 ||
            phonemizer::isVowelSymbol(binding.phone) || phonemizer::isNasalSymbol(binding.phone))) ||
        !sources.emplace(binding.phone, binding).second)
      return invalid("Frication binding is invalid or duplicated");
    const auto source = FricationSource::create(binding.source, sampleRate, context.start);
    if (!source) return core::Result<ArticulationPlan>{source.error()};
  }
  std::map<domain::PhonemeKey, const synthesis::PhonemeTimingAnchor*> anchors;
  std::map<std::string, PlosiveBinding, std::less<>> stops;
  for (const auto& binding : plosiveBindings) {
    const bool validPhone=binding.voicedClosure ? (binding.phone=="b" || binding.phone=="d" || binding.phone=="g") :
        (binding.phone=="p" || binding.phone=="t" || binding.phone=="k");
    if (!validPhone ||
        (binding.voicedClosure && (!std::isfinite(binding.voicedClosure->gain) || binding.voicedClosure->gain<=0.0 || binding.voicedClosure->gain>0.5 ||
            !std::isfinite(binding.voicedClosure->lowpassHz) || binding.voicedClosure->lowpassHz<40.0 || binding.voicedClosure->lowpassHz>2000.0)) ||
        !std::isfinite(binding.burstMilliseconds) || binding.burstMilliseconds < 1.0 || binding.burstMilliseconds > 100.0 ||
        sources.contains(binding.phone) || !stops.emplace(binding.phone, binding).second)
      return invalid("Plosive binding is unsupported, ambiguous or duplicated");
    const auto source = FricationSource::create(binding.source, sampleRate, context.start);
    if (!source) return core::Result<ArticulationPlan>{source.error()};
  }
  for (const auto& anchor : timing) if (!anchors.emplace(anchor.key, &anchor).second) return invalid("Articulation timing keys are duplicated");
  std::set<domain::PhonemeKey> seen;
  std::map<domain::NoteId,std::size_t> tokenCounts;
  for (const auto& phone:phones) ++tokenCounts[phone.key.noteId];
  ArticulationPlan result;
  result.sampleRate_ = sampleRate;
  result.context_ = context;
  for (const auto& phone : phones) {
    const auto valid = phone.validate();
    if (!valid) return core::Result<ArticulationPlan>{valid.error()};
    const auto found = anchors.find(phone.key);
    if (found == anchors.end() || !seen.insert(phone.key).second) return invalid("Articulation token/timing coverage differs");
    const auto& anchor = *found->second;
    if (!anchor.voiced || *anchor.voiced != phone.voiced) return invalid("Articulation voicing differs from compiled timing");
    const bool vowel = phone.role == domain::PhonemeRole::Nucleus && phone.voiced &&
        phonemizer::isVowelSymbol(phone.symbol);
    const bool nasal=phone.voiced && nasals.contains(phone.symbol) &&
        (phone.role==domain::PhonemeRole::Onset || phone.role==domain::PhonemeRole::Coda);
    auto start = anchor.nucleusFrame, end = anchor.endFrame;
    std::optional<FricationConfig> source;
    std::optional<PlosiveConfig> plosive;
    std::optional<double> voicingGain;
    std::optional<VoicedPlosiveConfig> voicedPlosive;
    if (vowel) {
      if (anchor.nucleusKey != std::optional{phone.key}) return invalid("Oral vowel lacks its own nucleus anchor");
    } else if (nasal && phone.symbol=="N" && phone.role==domain::PhonemeRole::Coda &&
        !anchor.nucleusKey && tokenCounts.at(phone.key.noteId)==1U) {
      // Japanese syllabic N owns its note's fallback time span, not an invented
      // vowel nucleus. Other nucleus-free consonants remain unsupported.
      start=anchor.explicitStartFrame.value_or(anchor.nucleusFrame);
      end=anchor.endFrame;
    } else if (nasal) {
      const auto onsetStart=anchor.explicitStartFrame?anchor.explicitStartFrame:anchor.inferredStartFrame;
      if (!onsetStart || !anchor.nucleusKey) return core::failure<ArticulationPlan>(core::ErrorCode::Unsupported,
          "Nasal gesture needs a resolved start and a same-note vowel nucleus");
      const auto nucleus=anchors.find(*anchor.nucleusKey);
      if (nucleus==anchors.end() || nucleus->second->key.noteId!=phone.key.noteId ||
          !nucleus->second->voiced.value_or(false) || nucleus->second->nucleusKey!=anchor.nucleusKey ||
          nucleus->second->nucleusFrame!=anchor.nucleusFrame) return invalid("Nasal nucleus binding is inconsistent");
      start=*onsetStart;
      if (phone.role==domain::PhonemeRole::Onset) {
        end=anchor.endExplicit?anchor.endFrame:anchor.nucleusFrame;
        if (end>anchor.nucleusFrame) return invalid("Nasal onset crosses its vowel nucleus");
      } else {
        if (start<anchor.nucleusFrame) return invalid("Nasal coda requires a resolved post-nucleus start");
        end=anchor.endFrame;
      }
    } else {
      const auto binding = sources.find(phone.symbol);
      const auto stopBinding = stops.find(phone.symbol);
      const bool noiseCoda=phone.role==domain::PhonemeRole::Coda && (stopBinding!=stops.end() || binding!=sources.end());
      const auto phoneLabel="Phone '"+phone.symbol+"' on note "+phone.key.noteId.toString();
      const bool voicedStop=stopBinding!=stops.end() && stopBinding->second.voicedClosure.has_value();
      if (stopBinding!=stops.end() && voicedStop!=phone.voiced) return invalid("Plosive binding voicing differs from its token");
      if (phone.voiced && !voicedStop && (binding==sources.end() || !binding->second.voicingGain))
        return core::failure<ArticulationPlan>(core::ErrorCode::Unsupported,phoneLabel+
            " requires a supported voiced articulation model; an unvoiced noise source cannot render it");
      if (!phone.voiced && binding!=sources.end() && binding->second.voicingGain)
        return invalid("Voiced frication binding cannot be used for an unvoiced token");
      if (binding == sources.end() && stopBinding == stops.end())
        return core::failure<ArticulationPlan>(core::ErrorCode::Unsupported,phoneLabel+
            " has no explicit frication or released-stop source in the selected style");
      if (phone.role != domain::PhonemeRole::Onset && !noiseCoda)
        return core::failure<ArticulationPlan>(core::ErrorCode::Unsupported,phoneLabel+
            " uses a role unsupported by its noise source; onset or coda is required");
      const auto onsetStart = anchor.explicitStartFrame ? anchor.explicitStartFrame : anchor.inferredStartFrame;
      if (!onsetStart || !anchor.nucleusKey) return core::failure<ArticulationPlan>(
          core::ErrorCode::Unsupported, "Noise gesture requires a resolved start and associated nucleus");
      const auto nucleus = anchors.find(*anchor.nucleusKey);
      if (nucleus == anchors.end() || nucleus->second->key.noteId != phone.key.noteId ||
          !nucleus->second->voiced.value_or(false) ||
          nucleus->second->nucleusKey != anchor.nucleusKey || nucleus->second->nucleusFrame != anchor.nucleusFrame)
        return invalid("Noise gesture nucleus binding is inconsistent");
      start = *onsetStart;
      end = noiseCoda || anchor.endExplicit ? anchor.endFrame : anchor.nucleusFrame;
      if (noiseCoda) {
        if (start<anchor.nucleusFrame) return invalid("Noise coda requires a post-nucleus start");
      } else if (end > anchor.nucleusFrame) return invalid("Noise onset crosses its vowel nucleus");
      if (stopBinding != stops.end()) {
        if (start < context.start || end > context.end || end <= start)
          return invalid("Plosive gesture is outside its context or empty");
        const auto burstFrames = static_cast<time::SampleFrame>(std::llround(stopBinding->second.burstMilliseconds * sampleRate / 1000.0));
        const auto span = end - start;
        if (span <= burstFrames || span > static_cast<time::SampleFrame>(sampleRate) * 2)
          return invalid("Plosive gesture has no room for its closure and nominal burst");
        plosive = PlosiveConfig{stopBinding->second.source, static_cast<std::uint32_t>(span - burstFrames),
            static_cast<std::uint32_t>(burstFrames)};
        const auto checked = PlosiveSource::create(*plosive, sampleRate, start);
        if (!checked) return core::Result<ArticulationPlan>{checked.error()};
        if (voicedStop) {
          const auto& closure=*stopBinding->second.voicedClosure;
          voicedPlosive=VoicedPlosiveConfig{*plosive,closure.gain,closure.lowpassHz};
          const auto voiced=VoicedPlosiveSource::create(*voicedPlosive,sampleRate,start);
          if (!voiced) return core::Result<ArticulationPlan>{voiced.error()};
        }
      } else { source = binding->second.source; voicingGain=binding->second.voicingGain; }
    }
    if (start < context.start || end > context.end || end <= start) return invalid("Articulation gesture is outside its context or empty");
    result.gestures_.push_back({vowel ? ArticulationGestureKind::OralVowel : nasal ? ArticulationGestureKind::Nasal :
        voicedPlosive ? ArticulationGestureKind::VoicedPlosive : plosive ? ArticulationGestureKind::Plosive : voicingGain ? ArticulationGestureKind::VoicedFrication : ArticulationGestureKind::Frication,
        phone.key, phone.symbol, {start, end}, source, plosive, voicingGain, voicedPlosive});
  }
  std::sort(result.gestures_.begin(), result.gestures_.end(), [](const auto& a, const auto& b) { return a.span.start < b.span.start; });
  for (std::size_t index = 1U; index < result.gestures_.size(); ++index)
    if (result.gestures_[index].span.start < result.gestures_[index - 1U].span.end) return invalid("Articulation gestures overlap");
  return result;
}
}
