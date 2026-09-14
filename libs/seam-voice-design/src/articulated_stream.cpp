#include "seam/voice_design/articulated_stream.hpp"
#include <algorithm>
#include <map>
#include <cmath>

namespace seam::voice_design {
core::Result<ArticulatedStream> ArticulatedStream::createFromRecipe(
    const synthesis::ProceduralSingerResource& resource, synthesis::CompiledScorePerformance performance,
    std::span<const domain::PhonemeToken> phones, std::string style, std::size_t blockFrames, std::stop_token stop, bool allowVoicedFrication, bool allowVoicedStops) {
  auto plan = ArticulationPlan::compileRecipe(resource, performance, phones, style, stop, allowVoicedFrication, allowVoicedStops);
  if (!plan) return core::Result<ArticulatedStream>{plan.error()};
  auto stream = create(resource, std::move(performance), std::move(plan.value()), std::move(style), blockFrames,allowVoicedFrication,allowVoicedStops);
  if (stop.stop_requested()) return core::failure<ArticulatedStream>(core::ErrorCode::Conflict, "Articulated preparation cancelled");
  return stream;
}

core::Result<ArticulatedStream> ArticulatedStream::create(
    const synthesis::ProceduralSingerResource& resource, synthesis::CompiledScorePerformance performance,
    ArticulationPlan plan, std::string style, std::size_t blockFrames, bool allowVoicedFrication, bool allowVoicedStops) {
  if (performance.sampleRate() != plan.sampleRate() || blockFrames == 0U || blockFrames > 65536U ||
      performance.phonemeTiming().size() != plan.gestures().size()) return core::failure<ArticulatedStream>(
          core::ErrorCode::InvalidArgument, "Articulated stream clock or timing coverage differs");
  auto recipe = decodeVoiceRecipeResource(resource,{},allowVoicedFrication,allowVoicedStops);
  if (!recipe) return core::Result<ArticulatedStream>{recipe.error()};
  std::map<domain::PhonemeKey, const synthesis::PhonemeTimingAnchor*> anchors;
  for (const auto& anchor : performance.phonemeTiming()) anchors.emplace(anchor.key, &anchor);
  std::string initialPhone;
  for (const auto& gesture : plan.gestures()) {
    const auto found = anchors.find(gesture.key);
    if (found == anchors.end()) return core::failure<ArticulatedStream>(core::ErrorCode::Conflict, "Articulation is not bound to compiled performance");
    const auto& anchor = *found->second;
    const bool vowel = gesture.kind == ArticulationGestureKind::OralVowel;
    const bool voiced=isVoicedGesture(gesture.kind);
    // A gesture whose note has no vowel owns the note's fallback span: the syllabic nasal, and a
    // unit of one declared event, which has no nucleus to attach itself to.
    const bool syllabic=!anchor.nucleusKey && (gesture.kind==ArticulationGestureKind::Nasal ||
        gesture.kind==ArticulationGestureKind::Closure || gesture.kind==ArticulationGestureKind::Breath);
    const auto start = vowel ? anchor.nucleusFrame : anchor.explicitStartFrame.value_or(anchor.inferredStartFrame.value_or(syllabic?anchor.nucleusFrame:-1));
    const auto end = vowel || anchor.endExplicit || start>=anchor.nucleusFrame ? anchor.endFrame : anchor.nucleusFrame;
    if (start != gesture.span.start || end != gesture.span.end || anchor.voiced != std::optional{voiced})
      return core::failure<ArticulatedStream>(core::ErrorCode::Conflict, "Articulation span or voicing differs from compiled performance");
    const bool voicedStop=gesture.kind==ArticulationGestureKind::VoicedPlosive;
    if (voiced && !voicedStop) {
      const auto pose = VocalTract::create(recipe.value(), gesture.posePhone.value_or(gesture.phone), style, plan.sampleRate());
      if (!pose) return core::Result<ArticulatedStream>{pose.error()};
      if (initialPhone.empty()) initialPhone = gesture.posePhone.value_or(gesture.phone);
    }
    // A palatalized consonant declares both halves of itself: the frozen recipe must name its
    // base source and its own palatal pose. Either half missing means the plan is not the recipe's
    // articulation, and a plan that drops the pose would be the base consonant with a new label.
    const auto palatalized = std::find_if(recipe.value().palatalized.begin(), recipe.value().palatalized.end(),
        [&](const auto& pose) { return pose.phone == gesture.phone && pose.style == style; });
    if ((palatalized != recipe.value().palatalized.end()) != gesture.posePhone.has_value() ||
        (gesture.posePhone && *gesture.posePhone != gesture.phone))
      return core::failure<ArticulatedStream>(core::ErrorCode::Conflict, "Palatalized plan differs from the frozen recipe");
    // The source a palatalized consonant renders with is its base consonant's, so every binding
    // comparison below resolves through the base the recipe names rather than the phone's own name.
    const std::string& sourcePhone = palatalized == recipe.value().palatalized.end() ? gesture.phone : palatalized->basePhone;
    if (gesture.kind == ArticulationGestureKind::Plosive || voicedStop) {
      const auto binding = std::find_if(recipe.value().plosives.begin(), recipe.value().plosives.end(),
          [&](const auto& pose) { return pose.phone == sourcePhone && pose.style == style; });
      if (binding == recipe.value().plosives.end() || !gesture.plosive || binding->voicedClosure.has_value()!=voicedStop ||
          binding->source != gesture.plosive->burst ||
          static_cast<time::SampleFrame>(std::llround(binding->burstMilliseconds * plan.sampleRate() / 1000.0)) != gesture.plosive->burstFrames)
        return core::failure<ArticulatedStream>(core::ErrorCode::Conflict, "Plosive plan differs from the frozen recipe");
      if (voicedStop && (!allowVoicedStops || !gesture.voicedPlosive ||
          gesture.voicedPlosive->release!=*gesture.plosive ||
          gesture.voicedPlosive->closureVoicingGain!=binding->voicedClosure->gain ||
          gesture.voicedPlosive->closureLowpassHz!=binding->voicedClosure->lowpassHz))
        return core::failure<ArticulatedStream>(core::ErrorCode::Conflict,"Voiced closure plan differs from the frozen recipe");
    } else if (gesture.kind == ArticulationGestureKind::Affricate) {
      const auto binding = std::find_if(recipe.value().affricates.begin(), recipe.value().affricates.end(),
          [&](const auto& pose) { return pose.phone == sourcePhone && pose.style == style; });
      if (binding == recipe.value().affricates.end() || !gesture.affricate ||
          binding->burst != gesture.affricate->release.burst ||
          binding->tail != gesture.affricate->tail ||
          static_cast<time::SampleFrame>(std::llround(binding->burstMilliseconds * plan.sampleRate() / 1000.0)) !=
              gesture.affricate->release.burstFrames)
        return core::failure<ArticulatedStream>(core::ErrorCode::Conflict, "Affricate plan differs from the frozen recipe");
    } else if (gesture.kind == ArticulationGestureKind::VoicedAffricate) {
      const auto binding = std::find_if(recipe.value().voicedAffricates.begin(), recipe.value().voicedAffricates.end(),
          [&](const auto& pose) { return pose.phone == sourcePhone && pose.style == style; });
      if (binding == recipe.value().voicedAffricates.end() || !gesture.affricate || !gesture.voicedPlosive ||
          !gesture.voicingGain || *gesture.voicingGain != binding->tailVoicingGain ||
          binding->burst != gesture.affricate->release.burst || binding->tail != gesture.affricate->tail ||
          binding->closureVoicingGain != gesture.voicedPlosive->closureVoicingGain ||
          binding->closureLowpassHz != gesture.voicedPlosive->closureLowpassHz ||
          static_cast<time::SampleFrame>(std::llround(binding->burstMilliseconds * plan.sampleRate() / 1000.0)) !=
              gesture.affricate->release.burstFrames)
        return core::failure<ArticulatedStream>(core::ErrorCode::Conflict, "Voiced affricate plan differs from the frozen recipe");
    } else if (gesture.kind == ArticulationGestureKind::Approximant) {
      const auto binding = std::find_if(recipe.value().approximants.begin(), recipe.value().approximants.end(),
          [&](const auto& pose) { return pose.phone == sourcePhone && pose.style == style; });
      const auto span = gesture.span.end - gesture.span.start;
      const auto expected = binding == recipe.value().approximants.end() ? time::SampleFrame{0}
          : std::clamp<time::SampleFrame>(static_cast<time::SampleFrame>(std::llround(
                binding->transitionMilliseconds * plan.sampleRate() / 1000.0)), 1, span);
      if (binding == recipe.value().approximants.end() || gesture.transitionFrames == 0U ||
          static_cast<time::SampleFrame>(gesture.transitionFrames) != expected)
        return core::failure<ArticulatedStream>(core::ErrorCode::Conflict, "Approximant plan differs from the frozen recipe");
    } else if (isNoiseGesture(gesture.kind)) {
      const auto binding = std::find_if(recipe.value().frications.begin(), recipe.value().frications.end(),
          [&](const auto& pose) { return pose.phone == sourcePhone && pose.style == style; });
      if (binding == recipe.value().frications.end() || !gesture.frication || binding->source != *gesture.frication ||
          binding->voicingGain!=gesture.voicingGain ||
          (gesture.kind==ArticulationGestureKind::VoicedFrication)!=gesture.voicingGain.has_value())
        return core::failure<ArticulatedStream>(core::ErrorCode::Conflict, "Frication plan differs from the frozen recipe");
    } else if (gesture.kind == ArticulationGestureKind::Closure) {
      // A closure declares silence and nothing else. Any source on it would be a claim that the
      // span is not silent, so the plan is refused rather than rendered with an invented one.
      const auto binding = std::find_if(recipe.value().closures.begin(), recipe.value().closures.end(),
          [&](const auto& pose) { return pose.phone == gesture.phone && pose.style == style; });
      if (binding == recipe.value().closures.end() || gesture.frication || gesture.plosive ||
          gesture.voicingGain || gesture.voicedPlosive || gesture.affricate ||
          gesture.transitionFrames != 0U || gesture.posePhone.has_value())
        return core::failure<ArticulatedStream>(core::ErrorCode::Conflict, "Closure plan differs from the frozen recipe");
    } else if (gesture.kind == ArticulationGestureKind::Breath) {
      const auto binding = std::find_if(recipe.value().breaths.begin(), recipe.value().breaths.end(),
          [&](const auto& pose) { return pose.phone == gesture.phone && pose.style == style; });
      if (binding == recipe.value().breaths.end() || !gesture.frication ||
          binding->source != *gesture.frication || gesture.voicingGain)
        return core::failure<ArticulatedStream>(core::ErrorCode::Conflict, "Breath plan differs from the frozen recipe");
    }
  }
  auto voice = PhonationSource::create(recipe.value(), performance, plan.context().start);
  auto noise = FricationGestureStream::create(plan, blockFrames, allowVoicedStops);
  if (!voice) return core::Result<ArticulatedStream>{voice.error()};
  if (!noise) return core::Result<ArticulatedStream>{noise.error()};
  ArticulatedStream result;
  result.plan_ = std::make_shared<const ArticulationPlan>(std::move(plan));
  result.performance_ = std::make_shared<const synthesis::CompiledScorePerformance>(std::move(performance));
  result.recipe_ = std::make_shared<const VoiceRecipe>(std::move(recipe.value()));
  result.voice_ = std::move(voice.value());
  // A phrase can be entirely event spans -- a pause, a closure, a breath -- and then no gesture is
  // voiced and no pose is held. The tract stays unset instead of borrowing another phone's pose,
  // and the voiced lane contributes exactly nothing for every frame.
  if (!initialPhone.empty()) {
    // The stream's own immutable recipe, not the local one: that was moved into the stream above,
    // so reading it here would validate a moved-from recipe instead of the one that was checked.
    auto tract = VocalTract::create(*result.recipe_, initialPhone, style, plan.sampleRate());
    if (!tract) return core::Result<ArticulatedStream>{tract.error()};
    result.tract_ = std::move(tract.value());
  }
  result.initialTract_ = result.tract_;
  result.frication_ = std::move(noise.value()); result.style_ = std::move(style);
  result.initialPhone_ = initialPhone; result.currentPhone_ = initialPhone; result.blockFrames_ = blockFrames;
  return result;
}
void ArticulatedStream::reset() {
  voice_->reset(); frication_->reset(); voicedStop_.reset(); tract_ = initialTract_; next_ = 0U; currentPhone_ = initialPhone_;
}
core::Result<synthesis::PhraseAudio> ArticulatedStream::renderOwned(synthesis::PhraseFrameRange owned, std::stop_token stop) {
  using Output = synthesis::PhraseAudio;
  if (owned.start < position() || owned.end <= owned.start || owned.end > plan_->context().end ||
      owned.end - position() > 32LL * 1024LL * 1024LL) return core::failure<Output>(core::ErrorCode::InvalidArgument, "Articulated replay window exceeds bounds");
  const auto cancelled = [] { return core::failure<Output>(core::ErrorCode::Conflict, "Articulated rendering cancelled"); };
  if (stop.stop_requested()) return cancelled();
  auto candidate = *this;
  Output output{owned.start, std::vector<float>(static_cast<std::size_t>(owned.end - owned.start), 0.0F)};
  const auto gestures = plan_->gestures();
  while (candidate.position() < owned.end) {
    if (stop.stop_requested()) return cancelled();
    const auto position = candidate.position();
    while (candidate.next_ < gestures.size() && gestures[candidate.next_].span.end <= position) {
      ++candidate.next_; candidate.voicedStop_.reset();
    }
    const auto* gesture = candidate.next_ < gestures.size() ? &gestures[candidate.next_] : nullptr;
    const bool active = gesture && gesture->span.start <= position;
    // A voiced affricate is voiced in two different ways inside one gesture: its closure carries
    // the voicing a stop's closure carries, and its frication tail is voiced through the tract.
    // The release end is where the burst stops and the tail begins, and a block never straddles it.
    const auto releaseEndOf = [](const ArticulationGesture& value) {
      return value.affricate ? value.span.start +
          static_cast<time::SampleFrame>(value.affricate->release.closureFrames) +
          static_cast<time::SampleFrame>(value.affricate->release.burstFrames) : value.span.start;
    };
    const bool inVoicedRelease = active && gesture->kind==ArticulationGestureKind::VoicedAffricate &&
        position < releaseEndOf(*gesture);
    const bool voicedStop=active && (gesture->kind==ArticulationGestureKind::VoicedPlosive || inVoicedRelease);
    const bool tonal = active && isVoicedGesture(gesture->kind) && !voicedStop;
    // Source phase restarts on score reattacks, not on every phone change.
    // Keep intra-note articulation and shared-lyric continuations connected.
    const auto reattacksAt = [&](time::SampleFrame frame) {
      if (frame <= plan_->context().start) return false;
      const auto before = performance_->at(frame - 1);
      const auto after = performance_->at(frame);
      return after.noteId && before.noteId != after.noteId && after.reattack;
    };
    const bool noteAttack = tonal && reattacksAt(gesture->span.start);
    const bool noteRelease = tonal && reattacksAt(gesture->span.end);
    auto boundary = owned.end;
    auto glide = static_cast<const ArticulationGesture*>(nullptr);
    auto entryFrames = time::SampleFrame{0};
    auto glideFrames = time::SampleFrame{0};
    const ArticulationTransition* entry = nullptr;
    const ArticulationTransition* declared = nullptr;
    if (gesture) {
      boundary = std::min(boundary, active ? gesture->span.end : gesture->span.start);
      if (inVoicedRelease) boundary = std::min(boundary, releaseEndOf(*gesture));
      // The transition plan already decided whether this boundary carries an acoustic overlap and
      // in which mode, so the renderer reads it instead of deriving a window of its own.
      for (const auto& transition : plan_->transitions())
        if (transition.to == gesture->key && transition.span.start == gesture->span.start) { entry = &transition; break; }
      entryFrames = entry ? static_cast<time::SampleFrame>(entry->frames)
          : std::min<time::SampleFrame>(plan_->sampleRate() / 50U, gesture->span.end - gesture->span.start);
      // A voiced approximant declares the formant motion into its neighbouring vowel, and that
      // motion is the gesture: it begins exactly `transitionFrames` before the gesture ends, so it
      // lands on the nucleus instead of being a short step at the vowel's own onset. This tract
      // owns one transition at a time, so the motion starts when the entry transition has released
      // it; a glide with no room for both keeps the declaration and compresses the shorter window
      // rather than dropping the declared motion.
      if (tonal && gesture->transitionFrames > 0U && candidate.next_ + 1U < gestures.size()) {
        glide = &gestures[candidate.next_ + 1U];
        for (const auto& transition : plan_->transitions())
          if (transition.from == gesture->key && transition.span.end == gesture->span.end) { declared = &transition; break; }
        if (declared) glideFrames = static_cast<time::SampleFrame>(declared->frames);
      }
      if (glide && (glide->span.start != gesture->span.end || glide->phone == candidate.currentPhone_)) {
        glide = nullptr;
        declared = nullptr;
      }
      if (glide) {
        if (!declared) glideFrames = std::min<time::SampleFrame>(static_cast<time::SampleFrame>(gesture->transitionFrames),
                                                                 gesture->span.end - gesture->span.start);
        // A transition is scheduled at a block boundary, and the same owned range has to render
        // identically however a caller splits it, so the block ends exactly where the motion must
        // begin and exactly where the entry crossfade would release the tract.
        if (position < gesture->span.end - glideFrames) boundary = std::min(boundary, gesture->span.end - glideFrames);
        if (position < gesture->span.start + entryFrames) boundary = std::min(boundary, gesture->span.start + entryFrames);
      }
    }
    const auto count = static_cast<std::size_t>(std::min<time::SampleFrame>(static_cast<time::SampleFrame>(blockFrames_), boundary - position));
    // A palatalized consonant puts its own palatal resonance in force for the whole gesture, even
    // when the gesture itself is unvoiced, so the release and the vowel's onset transition start
    // from the palatal shape. That transition into the vowel is what distinguishes きゃ from か.
    const auto posePhone = gesture->posePhone.value_or(gesture->phone);
    // A declared movement needs the pose it starts from and the pose it arrives at to be two points
    // in one parameter space: the same resonance count. The plan decides the kind from the gesture
    // classes, which is what identifies a coarticulation boundary, but only the recipe can say
    // whether the two banks line up. A boundary that declares a movement the banks cannot make
    // keeps the crossfade it had before the transition plan existed, which is the case that
    // crossfade is for, so a recipe that mixes resonance counts renders as it always did.
    const auto applyTransition = [&](const ArticulationTransition* transition, std::string_view phone,
                                     time::SampleFrame frames) -> core::Result<void> {
      if (transition && transition->kind == TransitionKind::FormantInterpolation) {
        auto moved = candidate.tract_->interpolateTo(*recipe_, transition->toPhone, style_,
            static_cast<std::size_t>(frames));
        if (moved || moved.error().code != core::ErrorCode::Unsupported) return moved;
      }
      return candidate.tract_->transitionTo(*recipe_, phone, style_, static_cast<std::size_t>(frames));
    };
    if (candidate.tract_ && (tonal || gesture->posePhone.has_value()) && position == gesture->span.start && posePhone != candidate.currentPhone_) {
      const auto frames = static_cast<std::size_t>(entryFrames);
      const auto applied = applyTransition(entry, posePhone, static_cast<time::SampleFrame>(frames));
      if (!applied) return core::Result<Output>{applied.error()};
      candidate.currentPhone_ = posePhone;
    }
    if (glide && position >= gesture->span.end - glideFrames &&
        candidate.tract_->transitionFramesRemaining() == 0U) {
      const auto frames = std::min<time::SampleFrame>(glideFrames, gesture->span.end - position);
      if (frames > 0) {
        const auto applied = applyTransition(declared, glide->phone, frames);
        if (!applied) return core::Result<Output>{applied.error()};
        candidate.currentPhone_ = glide->phone;
      }
    }
    auto excitation = candidate.voice_->render(count, stop);
    if (!excitation) return core::Result<Output>{excitation.error()};
    std::optional<Output> stopAudio;
    if (voicedStop) {
      if (!candidate.voicedStop_) {
        auto source=VoicedPlosiveSource::create(*gesture->voicedPlosive,plan_->sampleRate(),gesture->span.start);
        if (!source) return core::Result<Output>{source.error()};
        candidate.voicedStop_=std::move(source.value());
      }
      auto rendered=candidate.voicedStop_->render(excitation.value().samples,stop);
      if (!rendered) return core::Result<Output>{rendered.error()};
      stopAudio=std::move(rendered.value());
    }
    if (!tonal) std::fill(excitation.value().samples.begin(), excitation.value().samples.end(), 0.0F);
    // A plan of nothing but event spans holds no pose, so it has no tract to shape the
    // excitation and the voiced lane is exactly silent for every frame of it.
    std::vector<float> voicedSamples;
    if (candidate.tract_) {
      // The vocal-tract envelope is a control-rate channel: one shift per block, applied before the
      // block is filtered. A shift that has not changed costs nothing, and a shift that would put a
      // resonance past Nyquist is refused by cause instead of being clamped.
      // The formant channel owns the tract alone; gender couples the tract to the source, so its tract
      // half is added here and its source half is applied where the excitation is generated.
      const auto musical = performance_->at(position);
      const auto formant =
          static_cast<double>(musical.formantSemitones) +
          static_cast<double>(std::clamp(musical.gender, -1.0F, 1.0F)) *
              static_cast<double>(domain::kGenderFormantSemitones);
      if (formant != candidate.tract_->formantShiftSemitones()) {
        const auto applied = candidate.tract_->setFormantShift(formant);
        if (!applied) return core::Result<Output>{applied.error()};
      }
      auto voiced = candidate.tract_->process(excitation.value().samples, stop);
      if (!voiced) return core::Result<Output>{voiced.error()};
      voicedSamples = std::move(voiced.value());
    } else voicedSamples.assign(static_cast<std::size_t>(count), 0.0F);
    const auto noise = candidate.frication_->renderOwned({position, position + static_cast<time::SampleFrame>(count)}, stop);
    if (!noise) return core::Result<Output>{noise.error()};
    for (std::size_t index = 0U; index < count; ++index) {
      double envelope = tonal ? 1.0 : 0.0;
      if (tonal) {
        const auto frame = position + static_cast<time::SampleFrame>(index);
        const auto fade = std::max<time::SampleFrame>(1, std::min<time::SampleFrame>(plan_->sampleRate() / 200U, (gesture->span.end - gesture->span.start) / 2));
        const bool fadeIn = noteAttack || candidate.next_ == 0U || !isVoicedGesture(gestures[candidate.next_ - 1U].kind) || gestures[candidate.next_ - 1U].kind==ArticulationGestureKind::VoicedPlosive || gestures[candidate.next_ - 1U].span.end != gesture->span.start;
        const bool fadeOut = noteRelease || candidate.next_ + 1U == gestures.size() || !isVoicedGesture(gestures[candidate.next_ + 1U].kind) || gestures[candidate.next_ + 1U].kind==ArticulationGestureKind::VoicedPlosive || gestures[candidate.next_ + 1U].span.start != gesture->span.end;
        if (fadeIn) envelope = std::min(envelope, static_cast<double>(frame - gesture->span.start) / static_cast<double>(fade));
        if (fadeOut) envelope = std::min(envelope, static_cast<double>(gesture->span.end - 1 - frame) / static_cast<double>(fade));
        envelope = envelope * envelope * (3.0 - 2.0 * envelope);
      }
      double voicingGain=tonal?gesture->voicingGain.value_or(1.0):0.0;
      if (tonal && candidate.next_>0U) {
        const auto& previous=gestures[candidate.next_-1U];
        if (isVoicedGesture(previous.kind) && previous.span.end==gesture->span.start) {
          const auto ramp=std::max<time::SampleFrame>(1,std::min<time::SampleFrame>(plan_->sampleRate()/200U,gesture->span.end-gesture->span.start));
          auto t=std::clamp(static_cast<double>(position+static_cast<time::SampleFrame>(index)-gesture->span.start)/static_cast<double>(ramp),0.0,1.0);
          t=t*t*(3.0-2.0*t);
          voicingGain=previous.voicingGain.value_or(1.0)+(voicingGain-previous.voicingGain.value_or(1.0))*t;
        }
      }
      voicedSamples[index] = static_cast<float>(voicedSamples[index] * envelope * voicingGain) + noise.value().samples[index] +
          (stopAudio ? stopAudio->samples[index] : 0.0F);
    }
    const auto gained = synthesis::applyCompiledPerformanceGain(voicedSamples, *performance_, position, stop);
    if (!gained) return core::Result<Output>{gained.error()};
    for (std::size_t index = 0U; index < count; ++index) {
      const auto frame = position + static_cast<time::SampleFrame>(index);
      if (frame >= owned.start) output.samples[static_cast<std::size_t>(frame - owned.start)] = voicedSamples[index];
    }
  }
  if (stop.stop_requested()) return cancelled();
  *this = std::move(candidate);
  return output;
}
}
