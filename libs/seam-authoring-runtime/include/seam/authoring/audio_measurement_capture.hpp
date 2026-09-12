#pragma once
#include "seam/application/editor_session.hpp"
#include "seam/authoring/render_coordinator.hpp"
#include "seam/rendering/audio_level_envelope.hpp"

namespace seam::authoring {
// Owner-thread capture/revalidation. Copies share immutable PCM and the render
// input snapshot; this is not a new render or permission to publish stale data.
class AudioMeasurementCapture final {
public:
  [[nodiscard]] static core::Result<AudioMeasurementCapture> prepare(
      const application::EditorSession& session, const AuthoringRenderCoordinator& coordinator) {
    const auto audio = coordinator.acquireCurrent();
    if (!audio || !audio->sourceProject || audio->projectId != session.project().id() ||
        audio->projectRevision != session.revision() || *audio->sourceProject != session.project())
      return core::failure<AudioMeasurementCapture>(core::ErrorCode::Conflict, "Measurement requires a current render of this document snapshot");
    auto context = session.capturePerformanceJob();
    if (!context) return core::Result<AudioMeasurementCapture>{context.error()};
    return AudioMeasurementCapture{std::move(context.value()), session.revision(), *audio};
  }
  [[nodiscard]] bool matches(const application::EditorSession& session,
      const AuthoringRenderCoordinator& coordinator) const {
    return !closed_ && session.revision() == revision_ && coordinator.matchesCurrent(source_) &&
        session.project() == context_.sourceProject() && static_cast<bool>(session.validatePerformanceJob(context_));
  }
  [[nodiscard]] const PublishedProjectAudio& source() const noexcept { return source_; }
  // Safe to invoke on an analysis worker while this immutable capture is held.
  // A successful analysis is NOT publication approval: revalidate on the owner.
  [[nodiscard]] core::Result<rendering::MeasuredAudioEnvelope> measure(
      std::size_t firstFrame, std::size_t frameCount, std::size_t bins = 512U, std::stop_token stop = {}) const {
    return rendering::measureAudioLevels({source_.result.interleaved.data(), source_.result.interleaved.size()},
        source_.result.channelCount, firstFrame, frameCount, bins, stop);
  }
  void close() noexcept { closed_ = true; }
private:
  AudioMeasurementCapture(application::PerformanceJobContext context, std::uint64_t revision, PublishedProjectAudio source)
      : context_(std::move(context)), revision_(revision), source_(std::move(source)) {}
  application::PerformanceJobContext context_;
  std::uint64_t revision_;
  PublishedProjectAudio source_;
  bool closed_{false};
};
}
