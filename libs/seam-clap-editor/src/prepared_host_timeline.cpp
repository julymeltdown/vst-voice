#include "seam/clap_editor/prepared_host_timeline.hpp"

#include "seam/core/sha256.hpp"

#include <algorithm>
#include <cmath>

namespace seam::clap_editor {
namespace {

time::Tick tickForBeats(double beats, time::Ppq ppq) noexcept {
  return time::Tick{static_cast<std::int64_t>(
      std::llround(beats * static_cast<double>(ppq))) };
}

// Beats are the identity of a musical position; two positions inside this distance are
// the same position. It is the same tolerance HostTempoMap uses for its own segments.
bool samePosition(double first, double second) noexcept {
  return std::abs(first - second) <= HostTempoMap::kBeatsTolerance;
}

}  // namespace

double PreparedHostTimeline::observedStartBeats() const noexcept {
  const auto segments = authority_.observations();
  return segments.empty() ? 0.0 : segments.front().beats;
}

double PreparedHostTimeline::observedEndBeats() const noexcept {
  const auto segments = authority_.observations();
  return segments.empty() ? 0.0 : segments.back().beats;
}

HostSampleRange PreparedHostTimeline::observedSampleRange() const {
  return HostSampleRange{
      .startSeconds = absoluteTempoMap_.secondsAt(
                          tickForBeats(observedStartBeats(), ppq_)) -
                      projectOffsetSeconds_,
      .endSeconds = absoluteTempoMap_.secondsAt(
                        tickForBeats(observedEndBeats(), ppq_)) -
                    projectOffsetSeconds_,
  };
}

bool PreparedHostTimeline::covers(double startBeats, double endBeats,
                                  double maximumGapBeats) const noexcept {
  return HostTempoMap::coversObservations(segments_, startBeats, endBeats,
                                          maximumGapBeats);
}

std::string PreparedHostTimeline::uncoveredSpan(double startBeats, double endBeats,
                                                double maximumGapBeats) const {
  return HostTempoMap::uncoveredSpanOf(segments_, startBeats, endBeats, maximumGapBeats);
}

void HostTimelineCapture::clear() noexcept {
  tempoMap_.clear();
  meters_.clear();
  sampleRate_.reset();
  sampleRateChanged_ = false;
  loopActive_ = false;
  loopHasBeats_ = false;
  loopStartBeats_ = 0.0;
  loopEndBeats_ = 0.0;
  hasBeats_ = false;
  playing_ = false;
  lastBeats_ = 0.0;
  meterDropped_ = false;
  captureIncomplete_ = false;
  tempoRampSeen_ = false;
  reportCount_ = 0U;
  seekCount_ = 0U;
  revision_ = 0U;
}

void HostTimelineCapture::observe(const HostTimelineState& state,
                                  std::uint32_t sampleRate) {
  if (reportCount_ < kMaximumReports) ++reportCount_;
  ++revision_;
  if (state.captureIncomplete) captureIncomplete_ = true;
  if (state.hasTempoRamp) tempoRampSeen_ = true;
  if (!sampleRate_.has_value()) {
    sampleRate_ = sampleRate;
  } else if (*sampleRate_ != sampleRate) {
    // Mixing two rates in one capture would make the derived sample range a fiction, so it
    // is remembered and refused at freeze time rather than averaged away here.
    sampleRateChanged_ = true;
  }

  if (state.loopActive) {
    loopActive_ = true;
    if (state.loopHasBeats && std::isfinite(state.loopStartBeats) &&
        std::isfinite(state.loopEndBeats)) {
      loopHasBeats_ = true;
      loopStartBeats_ = state.loopStartBeats;
      loopEndBeats_ = state.loopEndBeats;
    }
  }

  if (!state.hasBeats || !std::isfinite(state.beats) || state.beats < 0.0) return;

  // A backward jump is what a seek looks like from the host's side. Musical tempo is a
  // function of the beat position, so a seek does not invalidate the map; it is recorded
  // because the sample-domain relation the host described is no longer monotonic.
  if (hasBeats_ && state.playing && state.beats < lastBeats_ - HostTempoMap::kBeatsTolerance) {
    ++seekCount_;
  }
  hasBeats_ = true;
  playing_ = state.playing;
  lastBeats_ = state.beats;

  if (state.hasTempo) {
    static_cast<void>(tempoMap_.observe(state.beats, state.tempo));
  }
  if (!state.hasTimeSignature || state.numerator == 0U || state.denominator == 0U) return;

  const HostMeterSegment value{state.beats, state.numerator, state.denominator};
  const auto position = std::lower_bound(
      meters_.begin(), meters_.end(), state.beats,
      [](const HostMeterSegment& entry, double target) {
        return entry.startBeats < target - HostTempoMap::kBeatsTolerance;
      });
  if (position != meters_.end() && samePosition(position->startBeats, state.beats)) {
    *position = value;
    return;
  }
  if (meters_.size() >= kMaximumMeterSegments) {
    meterDropped_ = true;
    return;
  }
  meters_.insert(position, value);
}

std::string HostTimelineCapture::contentHash() const {
  core::Sha256 hash;
  hash.update("host-timeline-capture/v1");
  hash.update(sampleRate_.has_value() ? std::to_string(*sampleRate_)
                                      : std::string{"no-sample-rate"});
  hash.update(tempoMap_.contentHash());
  hash.update(captureIncomplete_ ? "incomplete" : "complete");
  hash.update(tempoRampSeen_ ? "tempo-ramp" : "step-tempo");
  hash.update(std::to_string(captureIncomplete_));
  hash.update(loopActive_ ? "loop" : "no-loop");
  if (loopActive_) {
    hash.update(std::to_string(loopStartBeats_));
    hash.update(std::to_string(loopEndBeats_));
    hash.update(loopHasBeats_ ? "loop-beats" : "loop-seconds-only");
  }
  for (const auto& meter : meters_) {
    hash.update(std::to_string(meter.startBeats));
    hash.update(std::to_string(meter.numerator));
    hash.update(std::to_string(meter.denominator));
  }
  return hash.hexDigest();
}

bool HostTimelineCapture::describes(const PreparedHostTimeline& prepared) const {
  if (reportCount_ == 0U || sampleRateChanged_ || captureIncomplete_ || tempoRampSeen_) return false;
  if (sampleRate_.value_or(0U) != prepared.sampleRate()) return false;
  if (loopActive_ != prepared.loopActive()) return false;
  if (loopActive_ &&
      (!samePosition(loopStartBeats_, prepared.loopStartBeats()) ||
       !samePosition(loopEndBeats_, prepared.loopEndBeats()))) {
    return false;
  }

  const auto start = prepared.requestedStartBeats();
  const auto end = prepared.requestedEndBeats();
  const auto inRange = [start, end](double beats) {
    return beats >= start - HostTempoMap::kBeatsTolerance &&
           beats <= end + HostTempoMap::kBeatsTolerance;
  };
  const auto tempoAt = [this](double beats) -> std::optional<HostTempoObservation> {
    const auto observations = tempoMap_.observations();
    const auto position = std::lower_bound(
        observations.begin(), observations.end(), beats,
        [](const HostTempoObservation& entry, double target) {
          return entry.beats < target - HostTempoMap::kBeatsTolerance;
        });
    if (position == observations.end() || !samePosition(position->beats, beats)) {
      return std::nullopt;
    }
    return *position;
  };
  const auto meterAt = [this](double beats) -> std::optional<HostMeterSegment> {
    const auto position = std::lower_bound(
        meters_.begin(), meters_.end(), beats,
        [](const HostMeterSegment& entry, double target) {
          return entry.startBeats < target - HostTempoMap::kBeatsTolerance;
        });
    if (position == meters_.end() || !samePosition(position->startBeats, beats)) {
      return std::nullopt;
    }
    return *position;
  };

  // Every segment the preparation depends on must still be what the host says, including
  // the bounding observations that make coverage decidable.
  for (const auto& segment : prepared.tempoSegments()) {
    const auto current = tempoAt(segment.beats);
    if (!current.has_value() || current->bpm != segment.bpm) return false;
  }
  // ...and the host cannot have introduced a different tempo or meter inside the range
  // that the preparation never saw.
  for (const auto& segment : tempoMap_.observations()) {
    if (!inRange(segment.beats)) continue;
    const auto expected = std::find_if(
        prepared.tempoSegments().begin(), prepared.tempoSegments().end(),
        [&segment](const HostTempoObservation& entry) {
          return samePosition(entry.beats, segment.beats);
        });
    if (expected == prepared.tempoSegments().end() || expected->bpm != segment.bpm) return false;
  }
  for (const auto& meter : prepared.meterSegments()) {
    const auto current = meterAt(meter.startBeats);
    if (!current.has_value() || *current != meter) return false;
  }
  for (const auto& meter : meters_) {
    if (!inRange(meter.startBeats)) continue;
    const auto expected = std::find_if(
        prepared.meterSegments().begin(), prepared.meterSegments().end(),
        [&meter](const HostMeterSegment& entry) {
          return samePosition(entry.startBeats, meter.startBeats);
        });
    if (expected == prepared.meterSegments().end() || *expected != meter) return false;
  }
  return true;
}

core::Result<PreparedHostTimeline> HostTimelineCapture::freeze(
    const HostTimelineCaptureRequest& request) const {
  if (!std::isfinite(request.requestedStartBeats) ||
      !std::isfinite(request.requestedEndBeats) ||
      request.requestedEndBeats <= request.requestedStartBeats) {
    return core::failure<PreparedHostTimeline>(core::ErrorCode::InvalidArgument,
                         "A prepared host timeline needs a non-empty musical range");
  }
  if (request.sampleRate < 8000U || request.sampleRate > 384000U) {
    return core::failure<PreparedHostTimeline>(core::ErrorCode::InvalidArgument,
                         "Prepared host timeline sample rate is outside supported bounds");
  }
  if (!std::isfinite(request.projectStartBeats) || request.projectStartBeats < 0.0 ||
      request.projectStartBeats >= request.requestedEndBeats ||
      request.requestedStartBeats != 0.0) {
    return core::failure<PreparedHostTimeline>(core::ErrorCode::InvalidArgument,
                         "Prepared host timeline must cover beat zero through the project start and score end");
  }
  if (!std::isfinite(request.maximumGapBeats) || request.maximumGapBeats <= 0.0) {
    return core::failure<PreparedHostTimeline>(core::ErrorCode::InvalidArgument,
                         "Prepared host timeline needs a positive unobserved-gap tolerance");
  }
  // A capture with no reports is not special-cased: the coverage check below refuses it and
  // names the whole range as the span that would have to be recaptured.
  if (sampleRateChanged_) {
    return core::failure<PreparedHostTimeline>(
        core::ErrorCode::Conflict,
        "The host sample rate changed during capture; recapture the range at one rate");
  }
  if (captureIncomplete_) {
    return core::failure<PreparedHostTimeline>(
        core::ErrorCode::Unsupported,
        "Host transport history overflowed or contained an unsupported event; recapture the full range");
  }
  if (tempoRampSeen_) {
    return core::failure<PreparedHostTimeline>(
        core::ErrorCode::Unsupported,
        "The host supplied a tempo ramp, but Follow Host currently requires piecewise-constant tempo events");
  }
  if (sampleRate_.has_value() && *sampleRate_ != request.sampleRate) {
    return core::failure<PreparedHostTimeline>(
        core::ErrorCode::Conflict,
        "The host capture sample rate differs from the final render rate; recapture at the render rate");
  }
  if (meterDropped_) {
    return core::failure<PreparedHostTimeline>(core::ErrorCode::InvalidArgument,
                         "Host meter changes exceeded the capture's segment capacity");
  }
  if (loopActive_ && (!loopHasBeats_ || loopEndBeats_ <= loopStartBeats_)) {
    return core::failure<PreparedHostTimeline>(
        core::ErrorCode::Unsupported,
        "The host is looping without stating usable musical loop boundaries; "
        "its timing cannot be frozen into a bounce authority");
  }
  if (!tempoMap_.covers(request.requestedStartBeats, request.requestedEndBeats,
                        request.maximumGapBeats)) {
    const auto missing = tempoMap_.uncoveredSpan(request.requestedStartBeats,
                                                 request.requestedEndBeats,
                                                 request.maximumGapBeats);
    const std::string message =
        "Host timing does not cover beats " + std::to_string(request.requestedStartBeats) +
        ".." + std::to_string(request.requestedEndBeats) + "; the uncovered span is " +
        (missing.empty() ? std::string{"unknown"} : missing) + " and the host has reported " +
        std::to_string(tempoMap_.size()) + " tempo observation(s). Recapture through the " +
        "end of the score.";
    return core::failure<PreparedHostTimeline>(core::ErrorCode::Unsupported, message);
  }

  PreparedHostTimeline prepared;
  prepared.hostId_ = request.hostId;
  prepared.projectId_ = request.projectId;
  prepared.projectRevision_ = request.projectRevision;
  prepared.sampleRate_ = request.sampleRate;
  prepared.ppq_ = request.ppq > 0 ? request.ppq : time::kDefaultPpq;
  prepared.projectStartBeats_ = request.projectStartBeats;
  prepared.requestedStartBeats_ = request.requestedStartBeats;
  prepared.requestedEndBeats_ = request.requestedEndBeats;
  prepared.authority_ = tempoMap_;
  prepared.meters_ = meters_;
  prepared.loopActive_ = loopActive_;
  prepared.loopStartBeats_ = loopStartBeats_;
  prepared.loopEndBeats_ = loopEndBeats_;
  prepared.reportCount_ = reportCount_;
  prepared.captureRevision_ = revision_;
  prepared.seekCount_ = seekCount_;
  prepared.captureContentHash_ = contentHash();

  // Clip the map to what this range needs: every observation inside it, plus the last one
  // at or before the start and the first one at or after the end, so the map on the range
  // is fully determined and later host reports elsewhere cannot rewrite it.
  {
    const auto all = prepared.authority_.observations();
    const auto first = std::lower_bound(
        all.begin(), all.end(), request.requestedStartBeats - HostTempoMap::kBeatsTolerance,
        [](const HostTempoObservation& entry, double target) {
          return entry.beats < target;
        });
    // Start one before the range so the segment leading into it is known as well.
    auto position = first == all.begin() ? first : first - 1;
    for (; position != all.end() &&
           position->beats <= request.requestedEndBeats + HostTempoMap::kBeatsTolerance;
         ++position) {
      prepared.segments_.push_back(*position);
    }
    // The tail observation is only needed when nothing inside the range already reaches
    // the end; otherwise a later report beyond the range cannot affect this authority.
    const auto reachesEnd =
        !prepared.segments_.empty() &&
        prepared.segments_.back().beats >=
            request.requestedEndBeats - HostTempoMap::kBeatsTolerance;
    if (!reachesEnd && position != all.end()) prepared.segments_.push_back(*position);
  }

  prepared.absoluteTempoMap_ = time::TempoMap{prepared.ppq_};
  for (const auto& segment : prepared.authority_.observations()) {
    const auto added = prepared.absoluteTempoMap_.addOrReplace(
        tickForBeats(segment.beats, prepared.ppq_), segment.bpm);
    if (!added) {
      return core::failure<PreparedHostTimeline>(
          core::ErrorCode::InvalidArgument,
          "Host tempo segments cannot be expressed at this project's tick resolution");
    }
  }
  const auto startTick = tickForBeats(request.projectStartBeats, prepared.ppq_);
  prepared.projectOffsetSeconds_ = prepared.absoluteTempoMap_.secondsAt(startTick);
  prepared.tempoMap_ = time::TempoMap{prepared.ppq_};
  const auto initial = prepared.tempoMap_.addOrReplace(
      time::Tick{0}, prepared.absoluteTempoMap_.bpmAt(startTick));
  if (!initial) return core::Result<PreparedHostTimeline>{initial.error()};
  for (const auto& segment : prepared.authority_.observations()) {
    if (segment.beats <= request.projectStartBeats ||
        segment.beats > request.requestedEndBeats) continue;
    const auto added = prepared.tempoMap_.addOrReplace(
        tickForBeats(segment.beats, prepared.ppq_) - startTick, segment.bpm);
    if (!added) {
      return core::failure<PreparedHostTimeline>(
          core::ErrorCode::InvalidArgument,
          "Host tempo segments cannot be rebased at this project's tick resolution");
    }
  }

  core::Sha256 hash;
  hash.update("prepared-host-timeline/v1");
  hash.update(prepared.hostId_);
  hash.update(std::to_string(prepared.projectId_.value()));
  hash.update(std::to_string(prepared.projectRevision_));
  hash.update(std::to_string(prepared.sampleRate_));
  hash.update(std::to_string(prepared.ppq_));
  hash.update(std::to_string(prepared.projectOffsetSeconds_));
  hash.update(std::to_string(prepared.projectStartBeats_));
  hash.update(std::to_string(prepared.requestedStartBeats_));
  hash.update(std::to_string(prepared.requestedEndBeats_));
  hash.update(prepared.loopActive_ ? "loop" : "no-loop");
  hash.update(std::to_string(prepared.loopStartBeats_));
  hash.update(std::to_string(prepared.loopEndBeats_));
  hash.update(std::to_string(prepared.seekCount_));
  hash.update(prepared.captureContentHash_);
  prepared.contentHash_ = hash.hexDigest();
  return prepared;
}

}  // namespace seam::clap_editor
