#include "seam/voice_design/articulated_stream.hpp"
#include <algorithm>
#include <map>
#include <cmath>

namespace seam::voice_design {
core::Result<ArticulatedStream> ArticulatedStream::createFromRecipe(
    const synthesis::ProceduralSingerResource& resource, synthesis::CompiledScorePerformance performance,
    std::span<const domain::PhonemeToken> phones, std::string style, std::size_t blockFrames, std::stop_token stop, bool allowVoicedFrication) {
  auto plan = ArticulationPlan::compileRecipe(resource, performance, phones, style, stop, allowVoicedFrication);
  if (!plan) return core::Result<ArticulatedStream>{plan.error()};
  auto stream = create(resource, std::move(performance), std::move(plan.value()), std::move(style), blockFrames,allowVoicedFrication);
  if (stop.stop_requested()) return core::failure<ArticulatedStream>(core::ErrorCode::Conflict, "Articulated preparation cancelled");
  return stream;
}

core::Result<ArticulatedStream> ArticulatedStream::create(
    const synthesis::ProceduralSingerResource& resource, synthesis::CompiledScorePerformance performance,
    ArticulationPlan plan, std::string style, std::size_t blockFrames, bool allowVoicedFrication) {
  if (performance.sampleRate() != plan.sampleRate() || blockFrames == 0U || blockFrames > 65536U ||
      performance.phonemeTiming().size() != plan.gestures().size()) return core::failure<ArticulatedStream>(
          core::ErrorCode::InvalidArgument, "Articulated stream clock or timing coverage differs");
  auto recipe = decodeVoiceRecipeResource(resource,{},allowVoicedFrication);
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
    const bool syllabic=gesture.kind==ArticulationGestureKind::Nasal && gesture.phone=="N" && !anchor.nucleusKey;
    const auto start = vowel ? anchor.nucleusFrame : anchor.explicitStartFrame.value_or(anchor.inferredStartFrame.value_or(syllabic?anchor.nucleusFrame:-1));
    const auto end = vowel || anchor.endExplicit || start>=anchor.nucleusFrame ? anchor.endFrame : anchor.nucleusFrame;
    if (start != gesture.span.start || end != gesture.span.end || anchor.voiced != std::optional{voiced})
      return core::failure<ArticulatedStream>(core::ErrorCode::Conflict, "Articulation span or voicing differs from compiled performance");
    if (voiced) {
      const auto pose = VocalTract::create(recipe.value(), gesture.phone, style, plan.sampleRate());
      if (!pose) return core::Result<ArticulatedStream>{pose.error()};
      if (initialPhone.empty()) initialPhone = gesture.phone;
    }
    if (gesture.kind == ArticulationGestureKind::Plosive) {
      const auto binding = std::find_if(recipe.value().plosives.begin(), recipe.value().plosives.end(),
          [&](const auto& pose) { return pose.phone == gesture.phone && pose.style == style; });
      if (binding == recipe.value().plosives.end() || !gesture.plosive ||
          binding->source != gesture.plosive->burst ||
          static_cast<time::SampleFrame>(std::llround(binding->burstMilliseconds * plan.sampleRate() / 1000.0)) != gesture.plosive->burstFrames)
        return core::failure<ArticulatedStream>(core::ErrorCode::Conflict, "Plosive plan differs from the frozen recipe");
    } else if (isNoiseGesture(gesture.kind)) {
      const auto binding = std::find_if(recipe.value().frications.begin(), recipe.value().frications.end(),
          [&](const auto& pose) { return pose.phone == gesture.phone && pose.style == style; });
      if (binding == recipe.value().frications.end() || !gesture.frication || binding->source != *gesture.frication ||
          binding->voicingGain!=gesture.voicingGain ||
          (gesture.kind==ArticulationGestureKind::VoicedFrication)!=gesture.voicingGain.has_value())
        return core::failure<ArticulatedStream>(core::ErrorCode::Conflict, "Frication plan differs from the frozen recipe");
    }
  }
  if (initialPhone.empty()) return core::failure<ArticulatedStream>(core::ErrorCode::Unsupported, "Articulated stream requires a voiced pose");
  auto voice = PhonationSource::create(recipe.value(), performance, plan.context().start);
  auto tract = VocalTract::create(recipe.value(), initialPhone, style, plan.sampleRate());
  auto noise = FricationGestureStream::create(plan, blockFrames);
  if (!voice) return core::Result<ArticulatedStream>{voice.error()};
  if (!tract) return core::Result<ArticulatedStream>{tract.error()};
  if (!noise) return core::Result<ArticulatedStream>{noise.error()};
  ArticulatedStream result;
  result.plan_ = std::make_shared<const ArticulationPlan>(std::move(plan));
  result.performance_ = std::make_shared<const synthesis::CompiledScorePerformance>(std::move(performance));
  result.recipe_ = std::make_shared<const VoiceRecipe>(std::move(recipe.value()));
  result.voice_ = std::move(voice.value()); result.tract_ = std::move(tract.value()); result.initialTract_ = result.tract_;
  result.frication_ = std::move(noise.value()); result.style_ = std::move(style);
  result.initialPhone_ = initialPhone; result.currentPhone_ = initialPhone; result.blockFrames_ = blockFrames;
  return result;
}
void ArticulatedStream::reset() {
  voice_->reset(); frication_->reset(); tract_ = initialTract_; next_ = 0U; currentPhone_ = initialPhone_;
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
    while (candidate.next_ < gestures.size() && gestures[candidate.next_].span.end <= position) ++candidate.next_;
    const auto* gesture = candidate.next_ < gestures.size() ? &gestures[candidate.next_] : nullptr;
    const bool active = gesture && gesture->span.start <= position;
    const bool tonal = active && isVoicedGesture(gesture->kind);
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
    if (gesture) boundary = std::min(boundary, active ? gesture->span.end : gesture->span.start);
    const auto count = static_cast<std::size_t>(std::min<time::SampleFrame>(static_cast<time::SampleFrame>(blockFrames_), boundary - position));
    if (tonal && position == gesture->span.start && gesture->phone != candidate.currentPhone_) {
      const auto transition = candidate.tract_->transitionTo(*recipe_, gesture->phone, style_,
          static_cast<std::size_t>(std::min<time::SampleFrame>(plan_->sampleRate() / 50U, gesture->span.end - gesture->span.start)));
      if (!transition) return core::Result<Output>{transition.error()};
      candidate.currentPhone_ = gesture->phone;
    }
    auto excitation = candidate.voice_->render(count, stop);
    if (!excitation) return core::Result<Output>{excitation.error()};
    if (!tonal) std::fill(excitation.value().samples.begin(), excitation.value().samples.end(), 0.0F);
    auto voiced = candidate.tract_->process(excitation.value().samples, stop);
    if (!voiced) return core::Result<Output>{voiced.error()};
    const auto noise = candidate.frication_->renderOwned({position, position + static_cast<time::SampleFrame>(count)}, stop);
    if (!noise) return core::Result<Output>{noise.error()};
    for (std::size_t index = 0U; index < count; ++index) {
      double envelope = tonal ? 1.0 : 0.0;
      if (tonal) {
        const auto frame = position + static_cast<time::SampleFrame>(index);
        const auto fade = std::max<time::SampleFrame>(1, std::min<time::SampleFrame>(plan_->sampleRate() / 200U, (gesture->span.end - gesture->span.start) / 2));
        const bool fadeIn = noteAttack || candidate.next_ == 0U || !isVoicedGesture(gestures[candidate.next_ - 1U].kind) || gestures[candidate.next_ - 1U].span.end != gesture->span.start;
        const bool fadeOut = noteRelease || candidate.next_ + 1U == gestures.size() || !isVoicedGesture(gestures[candidate.next_ + 1U].kind) || gestures[candidate.next_ + 1U].span.start != gesture->span.end;
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
      voiced.value()[index] = static_cast<float>(voiced.value()[index] * envelope * voicingGain) + noise.value().samples[index];
    }
    const auto gained = synthesis::applyCompiledPerformanceGain(voiced.value(), *performance_, position, stop);
    if (!gained) return core::Result<Output>{gained.error()};
    for (std::size_t index = 0U; index < count; ++index) {
      const auto frame = position + static_cast<time::SampleFrame>(index);
      if (frame >= owned.start) output.samples[static_cast<std::size_t>(frame - owned.start)] = voiced.value()[index];
    }
  }
  if (stop.stop_requested()) return cancelled();
  *this = std::move(candidate);
  return output;
}
}
