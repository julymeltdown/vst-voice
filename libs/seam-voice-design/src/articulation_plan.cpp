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
  std::vector<AffricateBinding> affricates;
  std::vector<VoicedAffricateBinding> voicedAffricates;
  for (const auto& pose : recipe.value().plosives) if (pose.style == style && requestedStops.contains(pose.phone))
    plosives.push_back({pose.phone, pose.source, pose.burstMilliseconds, pose.voicedClosure});
  for (const auto& pose : recipe.value().frications) if (pose.style == style && requestedFrication.contains(pose.phone))
    bindings.push_back({pose.phone, pose.source, pose.voicingGain});
  for (const auto& pose : recipe.value().affricates) if (pose.style == style && requestedStops.contains(pose.phone))
    affricates.push_back({pose.phone, pose.burst, pose.tail, pose.burstMilliseconds});
  for (const auto& pose : recipe.value().voicedAffricates) if (pose.style == style && requestedStops.contains(pose.phone))
    voicedAffricates.push_back({pose.phone, pose.burst, pose.tail, pose.burstMilliseconds,
        pose.closureVoicingGain, pose.closureLowpassHz, pose.tailVoicingGain});
  std::vector<ApproximantBinding> approximants;
  for (const auto& pose : recipe.value().approximants) if (pose.style == style && requestedStops.contains(pose.phone))
    approximants.push_back({pose.phone, pose.transitionMilliseconds});
  // An event phone is requested by symbol whatever role the hint gave it: the moraic obstruent is
  // a coda in a vowel-to-coda unit and owns a whole note in a special unit, and both are the same
  // declaration.
  std::set<std::string> requestedSymbols;
  for (const auto& phone : phones) requestedSymbols.insert(phone.symbol);
  std::vector<ClosureBinding> closures;
  for (const auto& pose : recipe.value().closures) if (pose.style == style && requestedSymbols.contains(pose.phone))
    closures.push_back({pose.phone});
  std::vector<BreathBinding> breaths;
  for (const auto& pose : recipe.value().breaths) if (pose.style == style && requestedSymbols.contains(pose.phone))
    breaths.push_back({pose.phone, pose.source});
  const auto notes = performance.notes();
  std::vector<std::string> nasals;
  for (const auto& pose:recipe.value().poses) if (pose.style==style && pose.nasal && pose.nasalCoupling>0.0 && phonemizer::isNasalSymbol(pose.phone))
    nasals.push_back(pose.phone);
  // A palatalized consonant is not a new source: its release comes from the base consonant this
  // recipe already binds, and its colour from the pose declared under its own name. Both are
  // copied here so the ordinary gesture paths render it, and the phone keeps its own identity so
  // a bank still has a unit for it rather than a relabelled base consonant.
  std::vector<std::string> palatalizedPhones;
  for (const auto& pose : recipe.value().palatalized) {
    if (pose.style != style || !requestedStops.contains(pose.phone)) continue;
    const auto inStyle = [&](const auto& base) { return base.style == style && base.phone == pose.basePhone; };
    const auto basePlosive = std::find_if(recipe.value().plosives.begin(), recipe.value().plosives.end(), inStyle);
    const auto baseFrication = std::find_if(recipe.value().frications.begin(), recipe.value().frications.end(), inStyle);
    const auto baseAffricate = std::find_if(recipe.value().affricates.begin(), recipe.value().affricates.end(), inStyle);
    const auto baseApproximant = std::find_if(recipe.value().approximants.begin(), recipe.value().approximants.end(), inStyle);
    const auto baseNasal = std::find_if(recipe.value().poses.begin(), recipe.value().poses.end(),
        [&](const auto& base) { return inStyle(base) && base.nasal.has_value(); });
    if (basePlosive != recipe.value().plosives.end())
      plosives.push_back({pose.phone, basePlosive->source, basePlosive->burstMilliseconds, basePlosive->voicedClosure});
    else if (baseAffricate != recipe.value().affricates.end())
      affricates.push_back({pose.phone, baseAffricate->burst, baseAffricate->tail, baseAffricate->burstMilliseconds});
    else if (baseFrication != recipe.value().frications.end())
      bindings.push_back({pose.phone, baseFrication->source, baseFrication->voicingGain});
    else if (baseApproximant != recipe.value().approximants.end())
      approximants.push_back({pose.phone, baseApproximant->transitionMilliseconds});
    else if (baseNasal != recipe.value().poses.end()) nasals.push_back(pose.phone);
    else continue;
    palatalizedPhones.push_back(pose.phone);
  }
  const synthesis::PhraseFrameRange context{notes.front().startFrame, notes.back().endFrame};
  auto plan = compile(phones, performance.phonemeTiming(), bindings, performance.sampleRate(), context,nasals,plosives,affricates,approximants,palatalizedPhones,voicedAffricates,closures,breaths);
  if (!plan) return core::failure<ArticulationPlan>(plan.error().code,
      "Recipe '" + recipe.value().id + "', style '" + std::string(style) + "': " + plan.error().message);
  std::map<domain::NoteId, const synthesis::ScoreNoteSpan*> scoreNotes;
  for (const auto& note : notes) scoreNotes.emplace(note.id, &note);
  std::set<domain::NoteId> covered;
  std::set<std::string> checkedVowels;
  for (const auto& gesture : plan.value().gestures()) {
    if (stop.stop_requested()) return cancelled();
    const auto found = scoreNotes.find(gesture.key.noteId);
    if (found == scoreNotes.end()) return core::failure<ArticulationPlan>(core::ErrorCode::Unsupported,
        "Articulation names a note the performance does not contain");
    // The creator's own timing can place a gesture outside its own note: a consonant that begins
    // before its beat, or a release the next note starts inside. What bounds that gesture is the
    // phrase context and the partition rather than the note box. The span stays inside the context
    // the performance defines, the neighbouring gesture owns the frames on the far side of the
    // boundary because the plan is ordered and never overlaps, and no note is lost because the
    // coverage check below still requires a gesture for every note in the phrase.
    if (gesture.span.start < context.start || gesture.span.end > context.end)
      return core::failure<ArticulationPlan>(core::ErrorCode::Unsupported,
          "Articulation outside the phrase context requires a wider phonation context");
    covered.insert(gesture.key.noteId);
    if (isVoicedGesture(gesture.kind) && gesture.kind != ArticulationGestureKind::VoicedPlosive && checkedVowels.insert(gesture.phone).second) {
      const auto tract = VocalTract::create(recipe.value(), gesture.phone, style, performance.sampleRate());
      if (!tract) return core::Result<ArticulationPlan>{tract.error()};
    }
    if (gesture.posePhone) {
      const auto tract = VocalTract::create(recipe.value(), *gesture.posePhone, style, performance.sampleRate());
      if (!tract) return core::Result<ArticulationPlan>{tract.error()};
    }
  }
  // A phrase can be entirely event spans -- a pause, a closure, a breath -- and then no voiced
  // gesture exists to check. The recipe still has to offer this style a voice, because otherwise
  // the plan would be a partial read of a recipe that never declared the style at all.
  const bool styleDeclaresVoice = std::any_of(recipe.value().poses.begin(), recipe.value().poses.end(),
      [&](const auto& pose) { return pose.style == style; });
  if (covered.size() != notes.size() || (checkedVowels.empty() && !styleDeclaresVoice))
      return core::failure<ArticulationPlan>(core::ErrorCode::Unsupported,
          "Recipe articulation requires a declared style and complete score-note coverage");
  if (stop.stop_requested()) return cancelled();
  return plan;
}

core::Result<ArticulationPlan> ArticulationPlan::compile(
    std::span<const domain::PhonemeToken> phones,
    std::span<const synthesis::PhonemeTimingAnchor> timing,
    std::span<const FricationBinding> bindings, std::uint32_t sampleRate,
    synthesis::PhraseFrameRange context, std::span<const std::string> nasalBindings,
    std::span<const PlosiveBinding> plosiveBindings,
    std::span<const AffricateBinding> affricateBindings,
    std::span<const ApproximantBinding> approximantBindings,
    std::span<const std::string> palatalizedPhones,
    std::span<const VoicedAffricateBinding> voicedAffricateBindings,
    std::span<const ClosureBinding> closureBindings,
    std::span<const BreathBinding> breathBindings) {
  const auto invalid = [](const char* message) { return core::failure<ArticulationPlan>(core::ErrorCode::InvalidArgument, message); };
  const std::set<std::string,std::less<>> palatalized{palatalizedPhones.begin(), palatalizedPhones.end()};
  if (phones.empty() || phones.size() > 16384U || phones.size() != timing.size() || bindings.size() > 64U || nasalBindings.size()>64U || plosiveBindings.size()>64U || affricateBindings.size()>64U || approximantBindings.size()>64U ||
      closureBindings.size() > 64U || breathBindings.size() > 64U ||
      sampleRate < 8000U || sampleRate > 384000U || context.start < 0 || context.end <= context.start || context.end > (time::SampleFrame{1} << 52))
    return invalid("Articulation input or context exceeds bounds");
  std::map<std::string, FricationBinding, std::less<>> sources;
  std::set<std::string,std::less<>> nasals;
  // A palatalized nasal is admitted here as well: its resonance pose is the nasal one declared
  // under its own name, so it belongs on the nasal path rather than the noise paths.
  for (const auto& phone:nasalBindings) if ((!phonemizer::isNasalSymbol(phone) && palatalized.count(phone) == 0U) || !nasals.insert(phone).second)
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
    // A palatalized consonant carries its base's closure and burst, so its phone name is not one
    // of the plain plosive symbols, but it is admitted only when the caller named it as one.
    const bool validPhone=palatalized.count(binding.phone)!=0U || (binding.voicedClosure ? (binding.phone=="b" || binding.phone=="d" || binding.phone=="g") :
        (binding.phone=="p" || binding.phone=="t" || binding.phone=="k"));
    if (!validPhone ||
        (binding.voicedClosure && (!std::isfinite(binding.voicedClosure->gain) || binding.voicedClosure->gain<=0.0 || binding.voicedClosure->gain>0.5 ||
            !std::isfinite(binding.voicedClosure->lowpassHz) || binding.voicedClosure->lowpassHz<40.0 || binding.voicedClosure->lowpassHz>2000.0)) ||
        !std::isfinite(binding.burstMilliseconds) || binding.burstMilliseconds < 1.0 || binding.burstMilliseconds > 100.0 ||
        sources.contains(binding.phone) || !stops.emplace(binding.phone, binding).second)
      return invalid("Plosive binding is unsupported, ambiguous or duplicated");
    const auto source = FricationSource::create(binding.source, sampleRate, context.start);
    if (!source) return core::Result<ArticulationPlan>{source.error()};
  }
  std::map<std::string, AffricateBinding, std::less<>> affricates;
  for (const auto& binding : affricateBindings) {
    // Unvoiced affricates only. A voiced phone must not reach a noise pair, and a symbol outside
    // the admitted set is refused rather than guessed at.
    if ((binding.phone != "ts" && binding.phone != "ch") ||
        sources.contains(binding.phone) || stops.contains(binding.phone) ||
        !std::isfinite(binding.burstMilliseconds) || binding.burstMilliseconds < 1.0 ||
        binding.burstMilliseconds > 100.0 ||
        !affricates.emplace(binding.phone, binding).second)
      return invalid("Affricate binding is unsupported, ambiguous or duplicated");
    const auto burst = FricationSource::create(binding.burst, sampleRate, context.start);
    if (!burst) return core::Result<ArticulationPlan>{burst.error()};
    const auto tail = FricationSource::create(binding.tail, sampleRate, context.start);
    if (!tail) return core::Result<ArticulationPlan>{tail.error()};
  }
  std::map<std::string, VoicedAffricateBinding, std::less<>> voicedAffricates;
  for (const auto& binding : voicedAffricateBindings) {
    // A voiced affricate is admitted only for the symbol it is declared for, needs a bounded
    // closure, burst and tail, and cannot collide with any other binding of the same phone.
    if (binding.phone != "j" || sources.contains(binding.phone) || stops.contains(binding.phone) ||
        affricates.contains(binding.phone) ||
        !std::isfinite(binding.burstMilliseconds) || binding.burstMilliseconds < 1.0 ||
        binding.burstMilliseconds > 100.0 ||
        !std::isfinite(binding.closureVoicingGain) || binding.closureVoicingGain <= 0.0 || binding.closureVoicingGain > 0.5 ||
        !std::isfinite(binding.closureLowpassHz) || binding.closureLowpassHz < 40.0 || binding.closureLowpassHz > 2000.0 ||
        !std::isfinite(binding.tailVoicingGain) || binding.tailVoicingGain <= 0.0 || binding.tailVoicingGain > 1.0 ||
        !voicedAffricates.emplace(binding.phone, binding).second)
      return invalid("Voiced affricate binding is unsupported, ambiguous or duplicated");
    const auto burst = FricationSource::create(binding.burst, sampleRate, context.start);
    if (!burst) return core::Result<ArticulationPlan>{burst.error()};
    const auto tail = FricationSource::create(binding.tail, sampleRate, context.start);
    if (!tail) return core::Result<ArticulationPlan>{tail.error()};
  }
  std::map<std::string, ApproximantBinding, std::less<>> approximants;
  for (const auto& binding : approximantBindings) {
    // A voiced liquid or glide. The binding carries only the transition length; the resonance
    // bank it moves to is the recipe's own same-phone pose, which the stream checks.
    if ((binding.phone != "r" && binding.phone != "w" && binding.phone != "y" && palatalized.count(binding.phone) == 0U) ||
        sources.contains(binding.phone) || stops.contains(binding.phone) ||
        affricates.contains(binding.phone) ||
        !std::isfinite(binding.transitionMilliseconds) ||
        binding.transitionMilliseconds < 5.0 || binding.transitionMilliseconds > 200.0 ||
        !approximants.emplace(binding.phone, binding).second)
      return invalid("Approximant binding is unsupported, ambiguous or duplicated");
  }
  // An event binding is admitted only for the symbol whose own semantics it is, and it must not
  // collide with any other binding for that phone: a closure and a frication of the same symbol
  // would be two different claims about one unit.
  std::set<std::string,std::less<>> closures;
  for (const auto& binding : closureBindings) {
    const bool supportedPhone = binding.phone == "cl" || binding.phone == "pau" || binding.phone == "R" ||
        binding.phone == "glottal";
    if (!supportedPhone || !closures.insert(binding.phone).second || sources.contains(binding.phone) ||
        stops.contains(binding.phone) || affricates.contains(binding.phone) ||
        voicedAffricates.contains(binding.phone) || approximants.contains(binding.phone) || nasals.contains(binding.phone))
      return invalid("Closure binding is unsupported, ambiguous or duplicated");
  }
  std::map<std::string, BreathBinding, std::less<>> breaths;
  for (const auto& binding : breathBindings) {
    if (binding.phone != "br" || sources.contains(binding.phone) || stops.contains(binding.phone) ||
        affricates.contains(binding.phone) || voicedAffricates.contains(binding.phone) ||
        approximants.contains(binding.phone) || nasals.contains(binding.phone) || closures.contains(binding.phone) ||
        !breaths.emplace(binding.phone, binding).second)
      return invalid("Breath binding is unsupported, ambiguous or duplicated");
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
    std::optional<AffricateConfig> affricate;
    bool voicedAffricate = false;
    std::uint32_t transitionFrames{0U};
    bool closureEvent = false;
    bool breathEvent = false;
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
    } else if (approximants.find(phone.symbol) != approximants.end()) {
      const auto& binding = approximants.at(phone.symbol);
      const auto phoneLabel="Phone '"+phone.symbol+"' on note "+phone.key.noteId.toString();
      if (!phone.voiced) return invalid("Approximant binding cannot be used for a voiceless token");
      const bool approximantCoda = phone.role == domain::PhonemeRole::Coda;
      if (!approximantCoda && phone.role != domain::PhonemeRole::Onset)
        return core::failure<ArticulationPlan>(core::ErrorCode::Unsupported, phoneLabel +
            " uses a role unsupported by a voiced approximant; onset or coda is required");
      const auto onsetStart = anchor.explicitStartFrame ? anchor.explicitStartFrame : anchor.inferredStartFrame;
      if (!onsetStart || !anchor.nucleusKey) return core::failure<ArticulationPlan>(
          core::ErrorCode::Unsupported, "Approximant gesture requires a resolved start and associated nucleus");
      const auto nucleus = anchors.find(*anchor.nucleusKey);
      if (nucleus == anchors.end() || nucleus->second->key.noteId != phone.key.noteId ||
          !nucleus->second->voiced.value_or(false) ||
          nucleus->second->nucleusKey != anchor.nucleusKey ||
          nucleus->second->nucleusFrame != anchor.nucleusFrame)
        return invalid("Approximant nucleus binding is inconsistent");
      start = *onsetStart;
      end = approximantCoda || anchor.endExplicit ? anchor.endFrame : anchor.nucleusFrame;
      if (approximantCoda) {
        if (start<anchor.nucleusFrame) return invalid("Approximant coda requires a post-nucleus start");
      } else if (end > anchor.nucleusFrame) return invalid("Approximant onset crosses its vowel nucleus");
      if (end <= start) return invalid("Approximant gesture is empty");
      // The transition is the gesture, but it can never outlast the note it belongs to.
      const auto requested = static_cast<time::SampleFrame>(std::llround(
          binding.transitionMilliseconds * sampleRate / 1000.0));
      transitionFrames = static_cast<std::uint32_t>(std::clamp<time::SampleFrame>(requested, 1, end - start));
    } else if (closures.contains(phone.symbol) || breaths.contains(phone.symbol)) {
      // An event is a span, not an articulation: a declared closure is exactly silent and a
      // declared breath is its own unvoiced source. The span is the same one every other gesture
      // resolves through, so an event never invents timing; it only declines to borrow a vowel's.
      const auto phoneLabel = "Phone '" + phone.symbol + "' on note " + phone.key.noteId.toString();
      if (phone.role == domain::PhonemeRole::Nucleus) return invalid("Event phone cannot own a vowel nucleus");
      const bool soleToken = tokenCounts.at(phone.key.noteId) == 1U && !anchor.nucleusKey;
      if (soleToken) {
        // A unit of one event note has no vowel to attach to and owns the note's whole resolved
        // span, exactly as the syllabic nasal already does.
        start = anchor.explicitStartFrame.value_or(anchor.nucleusFrame);
        end = anchor.endFrame;
      } else {
        const auto onsetStart = anchor.explicitStartFrame ? anchor.explicitStartFrame : anchor.inferredStartFrame;
        if (!onsetStart || !anchor.nucleusKey) return core::failure<ArticulationPlan>(
            core::ErrorCode::Unsupported, "Event gesture requires a resolved start and associated nucleus");
        const auto nucleus = anchors.find(*anchor.nucleusKey);
        if (nucleus == anchors.end() || nucleus->second->key.noteId != phone.key.noteId ||
            !nucleus->second->voiced.value_or(false) ||
            nucleus->second->nucleusKey != anchor.nucleusKey ||
            nucleus->second->nucleusFrame != anchor.nucleusFrame)
          return invalid("Event gesture nucleus binding is inconsistent");
        const bool afterNucleus = phone.key.ordinal > anchor.nucleusKey->ordinal;
        start = *onsetStart;
        end = afterNucleus || anchor.endExplicit ? anchor.endFrame : anchor.nucleusFrame;
        if (afterNucleus) {
          if (start < anchor.nucleusFrame) return invalid("Event coda requires a post-nucleus start");
        } else if (end > anchor.nucleusFrame) return invalid("Event onset crosses its vowel nucleus");
      }
      const auto minimumFrames = std::max<time::SampleFrame>(1,
          static_cast<time::SampleFrame>(std::llround(5.0 * static_cast<double>(sampleRate) / 1000.0)));
      if (end - start < minimumFrames) return core::failure<ArticulationPlan>(core::ErrorCode::Unsupported,
          phoneLabel + " has less than five milliseconds of span to be an event; it needs more note time");
      if (breaths.contains(phone.symbol)) { source = breaths.at(phone.symbol).source; breathEvent = true; }
      else closureEvent = true;
    } else {
      const auto binding = sources.find(phone.symbol);
      const auto stopBinding = stops.find(phone.symbol);
      const auto affricateBinding = affricates.find(phone.symbol);
      const auto voicedAffricateBinding = voicedAffricates.find(phone.symbol);
      const bool noiseCoda=phone.role==domain::PhonemeRole::Coda &&
          (stopBinding!=stops.end() || binding!=sources.end() || affricateBinding!=affricates.end() ||
           voicedAffricateBinding!=voicedAffricates.end());
      const auto phoneLabel="Phone '"+phone.symbol+"' on note "+phone.key.noteId.toString();
      const bool voicedStop=stopBinding!=stops.end() && stopBinding->second.voicedClosure.has_value();
      if (affricateBinding!=affricates.end() && phone.voiced)
        return core::failure<ArticulationPlan>(core::ErrorCode::Unsupported,phoneLabel+
            " is a voiced affricate; it needs prevoiced closure and voiced frication, which this "
            "build does not model, so an unvoiced noise pair will not be substituted for it");
      if (voicedAffricateBinding != voicedAffricates.end() && !phone.voiced)
        return invalid("Voiced affricate binding cannot be used for an unvoiced token");
      if (voicedAffricateBinding != voicedAffricates.end() &&
          (binding != sources.end() || stopBinding != stops.end() || affricateBinding != affricates.end()))
        return invalid("Voiced affricate binding collides with another binding for the same phone");
      if (affricateBinding!=affricates.end() && voicedStop)
        return invalid("Affricate binding collides with a plosive binding for the same phone");
      if (stopBinding!=stops.end() && voicedStop!=phone.voiced) return invalid("Plosive binding voicing differs from its token");
      if (phone.voiced && !voicedStop && affricateBinding==affricates.end() &&
          voicedAffricateBinding==voicedAffricates.end() && (binding==sources.end() || !binding->second.voicingGain))
        return core::failure<ArticulationPlan>(core::ErrorCode::Unsupported,phoneLabel+
            " requires a supported voiced articulation model; an unvoiced noise source cannot render it");
      if (!phone.voiced && binding!=sources.end() && binding->second.voicingGain)
        return invalid("Voiced frication binding cannot be used for an unvoiced token");
      if (binding == sources.end() && stopBinding == stops.end() && affricateBinding == affricates.end() &&
          voicedAffricateBinding == voicedAffricates.end())
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
      if (affricateBinding != affricates.end()) {
        if (start < context.start || end > context.end || end <= start)
          return invalid("Affricate gesture is outside its context or empty");
        const auto burstFrames = static_cast<time::SampleFrame>(std::llround(
            affricateBinding->second.burstMilliseconds * sampleRate / 1000.0));
        const auto minimumTailFrames = static_cast<time::SampleFrame>(std::llround(
            kMinimumAffricateTailMilliseconds * sampleRate / 1000.0));
        const auto span = end - start;
        if (burstFrames <= 0 || span <= burstFrames + minimumTailFrames)
          return core::failure<ArticulationPlan>(core::ErrorCode::Unsupported, phoneLabel +
              " has no room for a released closure and its frication tail; it needs at least " +
              std::to_string(static_cast<double>(burstFrames + minimumTailFrames + 1) * 1000.0 /
                             static_cast<double>(sampleRate)) +
              " ms of note time");
        // The closure takes what is left after the release and the tail, and the tail is at least
        // the admitted minimum so a barely-long-enough note does not become a stop with a click.
        const auto afterBurst = span - burstFrames;
        const auto tailFrames = std::min<time::SampleFrame>(afterBurst - 1,
            std::max<time::SampleFrame>(minimumTailFrames, afterBurst / 2));
        const auto closureFrames = afterBurst - tailFrames;
        const PlosiveConfig release{affricateBinding->second.burst,
            static_cast<std::uint32_t>(closureFrames),
            static_cast<std::uint32_t>(burstFrames)};
        const auto checked = PlosiveSource::create(release, sampleRate, start);
        if (!checked) return core::Result<ArticulationPlan>{checked.error()};
        affricate = AffricateConfig{release, affricateBinding->second.tail,
            static_cast<std::uint32_t>(tailFrames)};
      } else if (voicedAffricateBinding != voicedAffricates.end()) {
        // The release is a closure that is already carrying voicing, followed by its burst and a
        // tail that is voiced through the tract. All three parts share one gesture so the phone
        // keeps one marker and one identity.
        if (start < context.start || end > context.end || end <= start)
          return invalid("Voiced affricate gesture is outside its context or empty");
        const auto burstFrames = static_cast<time::SampleFrame>(std::llround(
            voicedAffricateBinding->second.burstMilliseconds * sampleRate / 1000.0));
        const auto minimumTailFrames = static_cast<time::SampleFrame>(std::llround(
            kMinimumAffricateTailMilliseconds * sampleRate / 1000.0));
        const auto span = end - start;
        if (burstFrames <= 0 || span <= burstFrames + minimumTailFrames)
          return core::failure<ArticulationPlan>(core::ErrorCode::Unsupported, phoneLabel +
              " has no room for a prevoiced closure, its release and a voiced tail; it needs at least " +
              std::to_string(static_cast<double>(burstFrames + minimumTailFrames + 1) * 1000.0 /
                             static_cast<double>(sampleRate)) +
              " ms of note time");
        const auto afterBurst = span - burstFrames;
        const auto tailFrames = std::min<time::SampleFrame>(afterBurst - 1,
            std::max<time::SampleFrame>(minimumTailFrames, afterBurst / 2));
        const auto closureFrames = afterBurst - tailFrames;
        const PlosiveConfig release{voicedAffricateBinding->second.burst,
            static_cast<std::uint32_t>(closureFrames), static_cast<std::uint32_t>(burstFrames)};
        const auto checked = PlosiveSource::create(release, sampleRate, start);
        if (!checked) return core::Result<ArticulationPlan>{checked.error()};
        const VoicedPlosiveConfig closure{release, voicedAffricateBinding->second.closureVoicingGain,
            voicedAffricateBinding->second.closureLowpassHz};
        const auto voiced = VoicedPlosiveSource::create(closure, sampleRate, start);
        if (!voiced) return core::Result<ArticulationPlan>{voiced.error()};
        affricate = AffricateConfig{release, voicedAffricateBinding->second.tail,
            static_cast<std::uint32_t>(tailFrames)};
        voicedPlosive = closure;
        voicingGain = voicedAffricateBinding->second.tailVoicingGain;
        voicedAffricate = true;
      } else if (stopBinding != stops.end()) {
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
        closureEvent ? ArticulationGestureKind::Closure : breathEvent ? ArticulationGestureKind::Breath :
        transitionFrames > 0U ? ArticulationGestureKind::Approximant :
        voicedAffricate ? ArticulationGestureKind::VoicedAffricate :
        affricate ? ArticulationGestureKind::Affricate : voicedPlosive ? ArticulationGestureKind::VoicedPlosive : plosive ? ArticulationGestureKind::Plosive : voicingGain ? ArticulationGestureKind::VoicedFrication : ArticulationGestureKind::Frication,
        phone.key, phone.symbol, {start, end}, source, plosive, voicingGain, voicedPlosive, affricate, transitionFrames,
        palatalized.count(phone.symbol) == 0U ? std::optional<std::string>{} : std::optional<std::string>{phone.symbol}});
  }
  std::sort(result.gestures_.begin(), result.gestures_.end(), [](const auto& a, const auto& b) { return a.span.start < b.span.start; });
  for (std::size_t index = 1U; index < result.gestures_.size(); ++index)
    if (result.gestures_[index].span.start < result.gestures_[index - 1U].span.end) return invalid("Articulation gestures overlap");
  // The ordered spans above are the linguistic partition. This is the separate acoustic half: one
  // bounded overlap per boundary that can carry one, or none where a release or an event owns the
  // frames. A transition never shortens, lengthens or reorders a span.
  const auto milliseconds = [&](double value) {
    return static_cast<time::SampleFrame>(std::llround(value * static_cast<double>(sampleRate) / 1000.0));
  };
  for (std::size_t index = 0U; index + 1U < result.gestures_.size(); ++index) {
    const auto& from = result.gestures_[index];
    const auto& to = result.gestures_[index + 1U];
    if (to.span.start != from.span.end || !isVoicedGesture(to.kind)) continue;
    const auto spanLength = std::max<time::SampleFrame>(1, to.span.end - to.span.start);
    // A gesture that declares its own motion is the transition: an approximant's last frames move
    // the tract into the gesture that follows instead of holding one pose and then stepping.
    if (from.transitionFrames > 0U) {
      const auto frames = std::min<time::SampleFrame>(static_cast<time::SampleFrame>(from.transitionFrames),
          std::max<time::SampleFrame>(1, from.span.end - from.span.start));
      result.transitions_.push_back({from.key, to.key, from.phone, to.phone,
          {from.span.end - frames, from.span.end}, static_cast<std::uint32_t>(frames),
          TransitionKind::FormantInterpolation, TransitionComposition{true, false, false, true}});
      continue;
    }
    if (isVoicedGesture(from.kind)) {
      // Two voiced poses: the tract moves from the one it holds into the next over the entry window.
      // A nasal consonant is a different tract topology rather than another point in the same
      // space, so that boundary keeps the crossfade the two banks were built for.
      const auto frames = std::clamp<time::SampleFrame>(milliseconds(kBoundaryCrossfadeMilliseconds), 1, spanLength);
      const bool sameTopology = from.kind != ArticulationGestureKind::Nasal && to.kind != ArticulationGestureKind::Nasal;
      result.transitions_.push_back({from.key, to.key, from.phone, to.phone, {to.span.start, to.span.start + frames},
          static_cast<std::uint32_t>(frames), sameTopology ? TransitionKind::FormantInterpolation
                                                          : TransitionKind::BankCrossfade,
          TransitionComposition{true, false, true, true}});
    } else if (from.kind == ArticulationGestureKind::Frication || from.kind == ArticulationGestureKind::Plosive ||
               from.kind == ArticulationGestureKind::Affricate) {
      // A voiceless consonant is carried by its own noise source rather than by the tract, so the
      // tract is not sounding across this boundary and there is no movement to hear: the vowel is
      // an attack that has to be blended in, which is a crossfade and not a glide. Moving this
      // tract's own poles over the same window instead of blending was measured, and it attenuates
      // the vowel's onset: the pilot's own diagnostic pitch regression lost four voiced frames and
      // reported one octave error in the note that begins with a fricative, while the crossfade at
      // the same length keeps every analysed frame within fifty cents. The record still names the
      // boundary, its window and that voicing does not continue across it.
      const auto frames = std::clamp<time::SampleFrame>(milliseconds(kBoundaryCrossfadeMilliseconds), 1, spanLength);
      result.transitions_.push_back({from.key, to.key, from.phone, to.phone, {to.span.start, to.span.start + frames},
          static_cast<std::uint32_t>(frames), TransitionKind::BankCrossfade,
          TransitionComposition{false, false, true, true}});
    } else {
      // A closure, a pause or a breath is not a pose: the tract holds whatever it held before, so
      // the next gesture crossfades out of that state instead of being told where it came from.
      const auto frames = std::clamp<time::SampleFrame>(milliseconds(kBoundaryCrossfadeMilliseconds), 1, spanLength);
      result.transitions_.push_back({from.key, to.key, from.phone, to.phone, {to.span.start, to.span.start + frames},
          static_cast<std::uint32_t>(frames), TransitionKind::BankCrossfade,
          TransitionComposition{false, true, true, true}});
    }
  }
  return result;
}
}
