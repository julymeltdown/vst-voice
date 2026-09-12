#include "seam/voice_design/frication_gesture_stream.hpp"
#include <algorithm>

namespace seam::voice_design {
core::Result<FricationGestureStream> FricationGestureStream::create(ArticulationPlan plan, std::size_t blockFrames, bool voicedStopsRenderedSeparately) {
  if (!voicedStopsRenderedSeparately && std::any_of(plan.gestures().begin(),plan.gestures().end(),[](const auto& gesture) {
        return gesture.kind==ArticulationGestureKind::VoicedPlosive;
      })) return core::failure<FricationGestureStream>(core::ErrorCode::Unsupported,
          "Voiced plosive gestures require score excitation and cannot use the noise-only renderer");
  if (blockFrames == 0U || blockFrames > 65536U) return core::failure<FricationGestureStream>(
      core::ErrorCode::InvalidArgument, "Frication stream block size is outside bounds");
  FricationGestureStream result;
  result.position_ = plan.context().start;
  result.plan_ = std::make_shared<const ArticulationPlan>(std::move(plan));
  result.blockFrames_ = blockFrames;
  return result;
}
core::Result<synthesis::PhraseAudio> FricationGestureStream::renderOwned(synthesis::PhraseFrameRange owned, std::stop_token stop) {
  using Output = synthesis::PhraseAudio;
  if (owned.start < position_ || owned.start < plan_->context().start || owned.end > plan_->context().end ||
      owned.end <= owned.start || owned.end - owned.start > 32LL * 1024LL * 1024LL)
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Frication owned window is invalid");
  const auto cancelled = [] { return core::failure<Output>(core::ErrorCode::Conflict, "Frication gesture rendering cancelled"); };
  if (stop.stop_requested()) return cancelled();
  time::SampleFrame work = 0;
  for (const auto& gesture : plan_->gestures()) {
    if (!isNoiseGesture(gesture.kind)) continue;
    const auto begin = std::max(position_, gesture.span.start);
    const auto end = std::min(owned.end, gesture.span.end);
    if (end <= begin) continue;
    if (end - begin > 32LL * 1024LL * 1024LL - work) return core::failure<Output>(
        core::ErrorCode::InvalidArgument, "Frication prefix replay exceeds its work budget");
    work += end - begin;
  }
  auto candidate = *this;
  Output output{owned.start, std::vector<float>(static_cast<std::size_t>(owned.end - owned.start), 0.0F)};
  const auto gestures = plan_->gestures();
  while (candidate.position_ < owned.end) {
    if (stop.stop_requested()) return cancelled();
    while (candidate.next_ < gestures.size() && (!isNoiseGesture(gestures[candidate.next_].kind) ||
        gestures[candidate.next_].span.end <= candidate.position_)) {
      ++candidate.next_; candidate.source_.reset(); candidate.plosive_.reset();
    }
    if (candidate.next_ == gestures.size()) { candidate.position_ = owned.end; break; }
    const auto& gesture = gestures[candidate.next_];
    if (candidate.position_ < gesture.span.start) {
      candidate.position_ = std::min(gesture.span.start, owned.end); continue;
    }
    const bool plosive = gesture.kind == ArticulationGestureKind::Plosive;
    if (plosive && !candidate.plosive_) {
      auto source = PlosiveSource::create(*gesture.plosive, plan_->sampleRate(), gesture.span.start);
      if (!source) return core::Result<Output>{source.error()};
      candidate.plosive_ = std::move(source.value());
    }
    if (!plosive && !candidate.source_) {
      auto source = FricationSource::create(*gesture.frication, plan_->sampleRate(), gesture.span.start);
      if (!source) return core::Result<Output>{source.error()};
      candidate.source_ = std::move(source.value());
    }
    const auto count = static_cast<std::size_t>(std::min({static_cast<time::SampleFrame>(blockFrames_),
        gesture.span.end - candidate.position_, owned.end - candidate.position_}));
    const auto rendered = plosive ? candidate.plosive_->render(count, stop) : candidate.source_->render(count, stop);
    if (!rendered) return core::Result<Output>{rendered.error()};
    const auto fade = std::min<time::SampleFrame>(plan_->sampleRate() / 200U, (gesture.span.end - gesture.span.start) / 2);
    for (std::size_t index = 0U; index < count; ++index) {
      const auto frame = candidate.position_ + static_cast<time::SampleFrame>(index);
      if (frame < owned.start) continue;
      auto envelope = plosive || fade == 0 ? 1.0 : std::min({1.0, static_cast<double>(frame - gesture.span.start) / static_cast<double>(fade),
          static_cast<double>(gesture.span.end - 1 - frame) / static_cast<double>(fade)});
      envelope = envelope * envelope * (3.0 - 2.0 * envelope);
      output.samples[static_cast<std::size_t>(frame - owned.start)] = static_cast<float>(rendered.value().samples[index] * envelope);
    }
    candidate.position_ += static_cast<time::SampleFrame>(count);
  }
  if (stop.stop_requested()) return cancelled();
  *this = std::move(candidate);
  return output;
}
}
