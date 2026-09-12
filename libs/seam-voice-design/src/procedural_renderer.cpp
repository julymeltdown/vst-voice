#include "seam/voice_design/procedural_renderer.hpp"
#include "seam/voice_design/phonation_source.hpp"
#include "seam/voice_design/vocal_tract.hpp"
#include "seam/phonemizer/phonemizer.hpp"
#include <algorithm>
#include <unordered_set>
#include <unordered_map>

namespace seam::voice_design {
core::Result<void> validateVowelTiming(const synthesis::CompiledScorePerformance& performance) {
  if (performance.phonemeTiming().empty()) return core::failure(core::ErrorCode::Conflict, "Procedural vowels require compiled timing");
  std::unordered_map<domain::NoteId, const synthesis::ScoreNoteSpan*> notes;
  for (const auto& note : performance.notes()) notes.emplace(note.id, &note);
  std::vector<synthesis::PhraseFrameRange> spans;
  for (const auto& timing : performance.phonemeTiming()) {
    const auto found = notes.find(timing.key.noteId);
    if (found == notes.end() || timing.nucleusKey != std::optional<domain::PhonemeKey>{timing.key} ||
        timing.endFrame <= timing.nucleusFrame) return core::failure(core::ErrorCode::Conflict, "Procedural vowel timing has invalid note coverage");
    if (timing.nucleusFrame < found->second->startFrame || timing.endFrame > found->second->endFrame)
      return core::failure(core::ErrorCode::Unsupported, "Procedural vowel timing outside its score note requires extended phonation context");
    spans.push_back({timing.nucleusFrame, timing.endFrame});
  }
  std::sort(spans.begin(), spans.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
  for (std::size_t i = 1U; i < spans.size(); ++i) if (spans[i].start < spans[i - 1U].end)
    return core::failure(core::ErrorCode::Conflict, "Procedural vowel spans overlap");
  return core::success();
}
core::Result<void> validateVowelRecipePoses(const VoiceRecipe& recipe,
    std::span<const domain::PhonemeToken> phonemes, std::string_view style, std::uint32_t sampleRate) {
  if (phonemes.empty() || phonemes.size() > 16384U) return core::failure(core::ErrorCode::InvalidArgument, "Vowel pose coverage exceeds bounds");
  std::unordered_set<std::string> checked;
  for (const auto& token : phonemes) {
    if (!checked.insert(token.symbol).second) continue;
    const auto tract = VocalTract::create(recipe, token.symbol, style, sampleRate);
    if (!tract) return core::Result<void>{tract.error()};
  }
  return core::success();
}
bool requiresArticulation(std::span<const domain::PhonemeToken> phonemes) noexcept {
  return std::any_of(phonemes.begin(), phonemes.end(), [](const auto& phone) {
    return !phone.voiced || phone.role != domain::PhonemeRole::Nucleus ||
        !phonemizer::isVowelSymbol(phone.symbol);
  });
}
core::Result<void> validateProceduralPhrase(const domain::VocalRegion& region,
    std::span<const domain::PhonemeToken> phonemes) {
  const auto valid = region.validate();
  if (!valid) return valid;
  if (phonemes.empty() || phonemes.size() > 16384U) return core::failure(
      core::ErrorCode::InvalidArgument, "Procedural pronunciation is empty or oversized");
  if (std::any_of(region.unitSelectionOverrides.begin(), region.unitSelectionOverrides.end(), [](const auto& edit) { return !edit.unresolved; }) ||
      std::any_of(region.seamOverrides.begin(), region.seamOverrides.end(), [](const auto& edit) { return !edit.unresolved; })) {
    return core::failure(core::ErrorCode::Unsupported, "Sample unit/seam edits cannot be applied to a procedural phrase");
  }
  std::unordered_map<domain::NoteId, std::size_t> covered;
  for (const auto& phone : phonemes) {
    const auto tokenValid = phone.validate();
    if (!tokenValid) return tokenValid;
    if (!region.findNote(phone.key.noteId) || phone.key.ordinal != covered[phone.key.noteId]++) {
      return core::failure(core::ErrorCode::InvalidArgument, "Procedural note coverage is invalid");
    }
  }
  if (covered.size() != region.notes.size()) return core::failure(core::ErrorCode::InvalidArgument,
      "Procedural phrase does not cover every score note");
  return core::success();
}
core::Result<std::string> validateSustainedVowelPhrase(const domain::VocalRegion& region,
    std::span<const domain::PhonemeToken> phonemes) {
  const auto valid = validateProceduralPhrase(region, phonemes);
  if (!valid) return core::Result<std::string>{valid.error()};
  if (requiresArticulation(phonemes)) return core::failure<std::string>(core::ErrorCode::Unsupported,
      "Consonants require the articulation backend");
  return phonemes.front().symbol;
}

core::Result<SustainedPoseStream> SustainedPoseStream::create(
    const synthesis::ProceduralSingerResource& resource,
    const synthesis::CompiledScorePerformance& performance,
    std::string_view posePhone, std::string_view style,
    synthesis::PhraseFrameRange context, std::size_t blockFrames,
    std::stop_token stopToken) {
  const auto valid = synthesis::PhraseOutputContract{performance.sampleRate(), context, context}.validate();
  if (!valid) return core::Result<SustainedPoseStream>{valid.error()};
  if (blockFrames == 0U || blockFrames > 65536U) {
    return core::failure<SustainedPoseStream>(core::ErrorCode::InvalidArgument, "Sustained-pose render configuration is invalid");
  }
  const auto recipe = decodeVoiceRecipeResource(resource, stopToken);
  if (!recipe) return core::Result<SustainedPoseStream>{recipe.error()};
  auto source = PhonationSource::create(recipe.value(), performance, context.start);
  if (!source) return core::Result<SustainedPoseStream>{source.error()};
  auto tract = VocalTract::create(recipe.value(), posePhone, style, performance.sampleRate());
  if (!tract) return core::Result<SustainedPoseStream>{tract.error()};
  SustainedPoseStream stream;
  stream.source_ = std::move(source).value(); stream.tract_ = std::move(tract).value();
  stream.initialTract_ = stream.tract_;
  stream.recipe_ = std::make_shared<const VoiceRecipe>(recipe.value());
  stream.performance_ = std::make_shared<const synthesis::CompiledScorePerformance>(performance);
  stream.resource_ = resource.identity; stream.phone_ = posePhone; stream.style_ = style;
  stream.context_ = context; stream.blockFrames_ = blockFrames;
  return stream;
}

core::Result<void> SustainedPoseStream::configureVowels(const domain::VocalRegion& region,
    std::span<const domain::PhonemeToken> phonemes) {
  if (!performance_ || !recipe_ || position() != context_.start ||
      context_.start != performance_->notes().front().startFrame) return core::failure(core::ErrorCode::Conflict,
          "Vowel scheduling requires the initial full score context");
  const auto valid = validateSustainedVowelPhrase(region, phonemes);
  if (!valid) return core::Result<void>{valid.error()};
  const auto timing = performance_->phonemeTiming();
  const auto validTiming = validateVowelTiming(*performance_);
  if (!validTiming) return validTiming;
  if (phonemes.size() != timing.size()) return core::failure(core::ErrorCode::Conflict,
      "Vowel schedule requires matching compiled phoneme timing");
  struct TimedPose { time::SampleFrame start, end; std::string phone; bool explicitStart, explicitEnd; domain::PhonemeKey key; };
  std::vector<TimedPose> ordered;
  for (std::size_t i = 0U; i < timing.size(); ++i) {
    if (timing[i].key != phonemes[i].key || timing[i].nucleusKey != std::optional<domain::PhonemeKey>{timing[i].key} ||
        timing[i].endFrame <= timing[i].nucleusFrame)
      return core::failure(core::ErrorCode::Conflict, "Vowel schedule differs from compiled phoneme keys");
    ordered.push_back({timing[i].nucleusFrame, timing[i].endFrame, phonemes[i].symbol,
        timing[i].explicitStartFrame.has_value(), timing[i].endExplicit, timing[i].key});
  }
  std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
  for (std::size_t i = 1U; i < ordered.size(); ++i) if (ordered[i].start < ordered[i - 1U].end)
    return core::failure(core::ErrorCode::Conflict, "Procedural vowel spans overlap");
  if (ordered.empty() || ordered.front().start < context_.start || ordered.back().end > context_.end)
    return core::failure(core::ErrorCode::Conflict, "Vowel timing exceeds its score context");
  std::vector<PoseEvent> events;
  std::string previous;
  std::optional<VocalTract> first;
  for (const auto& timed : ordered) {
    if (timed.phone == previous) continue;
    auto pose = VocalTract::create(*recipe_, timed.phone, style_, performance_->sampleRate());
    if (!pose) return core::Result<void>{pose.error()};
    if (!first) first = std::move(pose).value();
    else events.push_back({timed.start, timed.phone,
        static_cast<std::size_t>(std::min<time::SampleFrame>(performance_->sampleRate() / 50U, timed.end - timed.start))});
    previous = timed.phone;
  }
  auto schedule = std::make_shared<const std::vector<PoseEvent>>(std::move(events));
  std::vector<ActiveSpan> spans;
  for (std::size_t i = 0U; i < ordered.size(); ++i) {
    const auto& timed = ordered[i];
    const bool fadeIn = i == 0U ? timed.explicitStart || timed.start > context_.start : ordered[i - 1U].end < timed.start;
    const bool fadeOut = i + 1U == ordered.size() ? timed.explicitEnd || timed.end < context_.end : timed.end < ordered[i + 1U].start;
    spans.push_back({timed.start, timed.end, fadeIn, fadeOut, timed.key, timed.phone});
  }
  auto activity = std::make_shared<const std::vector<ActiveSpan>>(std::move(spans));
  initialTract_ = std::move(first); tract_ = initialTract_;
  phone_ = ordered.front().phone;
  events_ = std::move(schedule); nextEvent_ = 0U;
  activeSpans_ = std::move(activity); activeSpan_ = 0U;
  return core::success();
}

core::Result<SustainedPoseResult> SustainedPoseStream::renderOwned(synthesis::PhraseFrameRange window,
    std::stop_token stopToken) {
  if (!source_ || !tract_ || !performance_) return core::failure<SustainedPoseResult>(
      core::ErrorCode::InvalidArgument, "Sustained-pose stream is incomplete");
  const synthesis::PhraseOutputContract output{performance_->sampleRate(), context_, window};
  const auto valid = output.validate();
  if (!valid) return core::Result<SustainedPoseResult>{valid.error()};
  if (window.start < position()) return core::failure<SustainedPoseResult>(core::ErrorCode::Conflict,
      "Backward sustained-pose output requires reset or a prior checkpoint");
  if (stopToken.stop_requested()) return core::failure<SustainedPoseResult>(core::ErrorCode::Conflict, "Sustained-pose rendering cancelled");
  auto source = *source_; auto tract = *tract_;
  auto eventIndex = nextEvent_;
  auto spanIndex = activeSpan_;
  // This source/tract pair is causal. Keep all supplied musical context, but
  // evaluate DSP only through the owned end and retain no pre-roll PCM.
  synthesis::PhraseAudio audio{output.owned.start, {}};
  const auto frames = static_cast<std::size_t>(output.owned.end - position());
  audio.samples.reserve(static_cast<std::size_t>(output.owned.end - output.owned.start));
  std::size_t processed = 0U;
  while (processed < frames) {
    if (stopToken.stop_requested()) return core::failure<SustainedPoseResult>(core::ErrorCode::Conflict, "Sustained-pose rendering cancelled");
    if (events_ && eventIndex < events_->size() && (*events_)[eventIndex].frame == source.position()) {
      const auto& event = (*events_)[eventIndex];
      const auto transition = tract.transitionTo(*recipe_, event.phone, style_, event.frames);
      if (!transition) return core::Result<SustainedPoseResult>{transition.error()};
      ++eventIndex;
    }
    auto count = std::min(blockFrames_, frames - processed);
    if (events_ && eventIndex < events_->size()) count = std::min(count,
        static_cast<std::size_t>((*events_)[eventIndex].frame - source.position()));
    bool active = true;
    if (activeSpans_) {
      while (spanIndex < activeSpans_->size() && source.position() >= (*activeSpans_)[spanIndex].end) ++spanIndex;
      active = spanIndex < activeSpans_->size() && source.position() >= (*activeSpans_)[spanIndex].start;
      if (spanIndex < activeSpans_->size()) count = std::min(count, static_cast<std::size_t>(
          (active ? (*activeSpans_)[spanIndex].end : (*activeSpans_)[spanIndex].start) - source.position()));
    }
    auto excitation = source.render(count, stopToken);
    if (!excitation) return core::Result<SustainedPoseResult>{excitation.error()};
    if (!active) std::fill(excitation.value().samples.begin(), excitation.value().samples.end(), 0.0F);
    auto shaped = tract.process(excitation.value().samples, stopToken);
    if (!shaped) return core::Result<SustainedPoseResult>{shaped.error()};
    // Gates and accepted dynamics belong after resonance, so filter ringing
    // cannot reopen a closed staccato gate or undo a manual gain decision.
    const auto gained = synthesis::applyCompiledPerformanceGain(shaped.value(), *performance_,
        excitation.value().startFrame, stopToken);
    if (!gained) return core::Result<SustainedPoseResult>{gained.error()};
    if (!active) std::fill(shaped.value().begin(), shaped.value().end(), 0.0F);
    else if (activeSpans_) {
      const auto& span = (*activeSpans_)[spanIndex];
      const auto ramp = static_cast<double>(std::max<time::SampleFrame>(1,
          std::min<time::SampleFrame>(performance_->sampleRate() / 200U, (span.end - span.start) / 2)));
      const auto smooth = [](double value) { const auto t = std::clamp(value, 0.0, 1.0); return t * t * (3.0 - 2.0 * t); };
      if (span.fadeIn || span.fadeOut) for (std::size_t i = 0U; i < count; ++i) {
        const auto frame = excitation.value().startFrame + static_cast<time::SampleFrame>(i);
        const auto gain = (span.fadeIn ? smooth(static_cast<double>(frame - span.start) / ramp) : 1.0) *
            (span.fadeOut ? smooth(static_cast<double>(span.end - frame - 1) / ramp) : 1.0);
        shaped.value()[i] = static_cast<float>(shaped.value()[i] * gain);
      }
    }
    const auto firstOwned = std::clamp<time::SampleFrame>(output.owned.start - excitation.value().startFrame,
        0, static_cast<time::SampleFrame>(count));
    audio.samples.insert(audio.samples.end(), shaped.value().begin() + static_cast<std::ptrdiff_t>(firstOwned), shaped.value().end());
    processed += count;
  }
  auto owned = synthesis::finalizePhraseBackendAudio({output.sampleRate, output.owned, output.owned}, std::move(audio), stopToken);
  if (!owned) return core::Result<SustainedPoseResult>{owned.error()};
  std::vector<ProceduralPhoneMarker> markers;
  if (activeSpans_) for (const auto& span : *activeSpans_) {
    if (stopToken.stop_requested()) return core::failure<SustainedPoseResult>(core::ErrorCode::Conflict, "Procedural marker generation cancelled");
    const auto start = std::max(span.start, output.owned.start);
    const auto end = std::min(span.end, output.owned.end);
    if (start < end) markers.push_back({span.key, span.phone, {start, end}, start != span.start, end != span.end});
  }
  SustainedPoseResult result{std::move(owned).value(), resource_, phone_, style_,
      kSustainedPoseRendererRevision, processed, std::move(markers)};
  source_ = std::move(source); tract_ = std::move(tract);
  nextEvent_ = eventIndex;
  activeSpan_ = spanIndex;
  return result;
}

void SustainedPoseStream::reset() { if (source_) source_->reset(); tract_ = initialTract_; nextEvent_ = 0U; activeSpan_ = 0U; }

core::Result<SustainedPoseResult> renderSustainedPose(
    const synthesis::ProceduralSingerResource& resource, const synthesis::CompiledScorePerformance& performance,
    std::string_view posePhone, std::string_view style, synthesis::PhraseOutputContract output,
    std::size_t blockFrames, std::stop_token stopToken) {
  const auto valid = output.validate();
  if (!valid) return core::Result<SustainedPoseResult>{valid.error()};
  if (output.sampleRate != performance.sampleRate()) return core::failure<SustainedPoseResult>(
      core::ErrorCode::InvalidArgument, "Sustained-pose output rate differs from compiled performance");
  auto stream = SustainedPoseStream::create(resource, performance, posePhone, style, output.context, blockFrames, stopToken);
  if (!stream) return core::Result<SustainedPoseResult>{stream.error()};
  return stream.value().renderOwned(output.owned, stopToken);
}
}
