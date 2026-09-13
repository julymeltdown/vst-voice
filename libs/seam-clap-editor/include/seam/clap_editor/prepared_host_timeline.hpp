#pragma once

#include "seam/clap_editor/host_tempo_map.hpp"
#include "seam/clap_editor/host_timeline.hpp"
#include "seam/core/result.hpp"
#include "seam/domain/project.hpp"
#include "seam/time/tempo_map.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace seam::clap_editor {

// One meter the host actually stated, at the musical position it stated it. A host that
// never states a meter contributes no segment at all, which is not the same claim as
// 4/4 and must not be recorded as one.
struct HostMeterSegment final {
  double startBeats{0.0};
  std::uint16_t numerator{4U};
  std::uint16_t denominator{4U};

  friend bool operator==(const HostMeterSegment&, const HostMeterSegment&) = default;
};

// The musical extent one Follow Host bounce asks the host's report to cover, plus the
// project state the resulting authority belongs to.
struct HostTimelineCaptureRequest final {
  domain::ProjectId projectId{};
  std::uint64_t projectRevision{0U};
  std::uint32_t sampleRate{48000U};
  time::Ppq ppq{time::kDefaultPpq};
  double projectOffsetSeconds{0.0};
  double requestedStartBeats{0.0};
  double requestedEndBeats{0.0};
  // How far apart two host reports may be and still describe a continuous map. The
  // caller declares this tolerance; the capture never invents one.
  double maximumGapBeats{1.0};
  std::string hostId{};
};

class HostTimelineCapture;

// The range the host spoke for, expressed in project seconds through the same tempo map
// the render compiles against. It is derived, never reported directly by the host.
struct HostSampleRange final {
  double startSeconds{0.0};
  double endSeconds{0.0};

  friend bool operator==(const HostSampleRange&, const HostSampleRange&) = default;
};

// The frozen timing authority one Follow Host bounce was prepared against: what range the
// host actually covered, which tempo and meter segments it stated there, the loop and
// project-offset semantics in force, the sample rate, and a canonical identity over all of
// it. It is immutable after freezing, so a later host report can change the identity the
// next bounce will use but can never rewrite this one.
class PreparedHostTimeline final {
public:
  PreparedHostTimeline(const PreparedHostTimeline&) = default;
  PreparedHostTimeline& operator=(const PreparedHostTimeline&) = default;

  [[nodiscard]] const std::string& hostId() const noexcept { return hostId_; }
  [[nodiscard]] domain::ProjectId projectId() const noexcept { return projectId_; }
  [[nodiscard]] std::uint64_t projectRevision() const noexcept { return projectRevision_; }
  [[nodiscard]] std::uint32_t sampleRate() const noexcept { return sampleRate_; }
  [[nodiscard]] time::Ppq ppq() const noexcept { return ppq_; }
  [[nodiscard]] double projectOffsetSeconds() const noexcept { return projectOffsetSeconds_; }
  [[nodiscard]] double requestedStartBeats() const noexcept { return requestedStartBeats_; }
  [[nodiscard]] double requestedEndBeats() const noexcept { return requestedEndBeats_; }
  // What the host actually reported, which may be wider than what was requested.
  [[nodiscard]] double observedStartBeats() const noexcept;
  [[nodiscard]] double observedEndBeats() const noexcept;
  // The tempo segments the bounce actually depends on: every observation inside the
  // requested range plus the bounding observations that make coverage decidable. A later
  // host report outside the range cannot change them.
  [[nodiscard]] std::span<const HostTempoObservation> tempoSegments() const noexcept {
    return segments_;
  }
  [[nodiscard]] std::span<const HostMeterSegment> meterSegments() const noexcept {
    return meters_;
  }
  [[nodiscard]] bool hasHostMeter() const noexcept { return !meters_.empty(); }
  [[nodiscard]] bool loopActive() const noexcept { return loopActive_; }
  [[nodiscard]] double loopStartBeats() const noexcept { return loopStartBeats_; }
  [[nodiscard]] double loopEndBeats() const noexcept { return loopEndBeats_; }
  [[nodiscard]] std::size_t reportCount() const noexcept { return reportCount_; }
  [[nodiscard]] std::uint64_t captureRevision() const noexcept { return captureRevision_; }
  [[nodiscard]] std::size_t seekCount() const noexcept { return seekCount_; }
  // The tempo map the render actually compiles against, at this project's resolution.
  [[nodiscard]] const time::TempoMap& tempoMap() const noexcept { return tempoMap_; }
  [[nodiscard]] HostSampleRange observedSampleRange() const;
  [[nodiscard]] bool covers(double startBeats, double endBeats,
                            double maximumGapBeats) const noexcept;
  [[nodiscard]] std::string uncoveredSpan(double startBeats, double endBeats,
                                          double maximumGapBeats) const;
  // Identity of the host content this authority was frozen from. A capture whose content
  // hash still matches is the same authority, however many reports it took to say so.
  [[nodiscard]] const std::string& captureContentHash() const noexcept {
    return captureContentHash_;
  }
  [[nodiscard]] const std::string& contentHash() const noexcept { return contentHash_; }

private:
  friend class HostTimelineCapture;
  PreparedHostTimeline() = default;

  std::string hostId_;
  domain::ProjectId projectId_{};
  std::uint64_t projectRevision_{0U};
  std::uint32_t sampleRate_{48000U};
  time::Ppq ppq_{time::kDefaultPpq};
  double projectOffsetSeconds_{0.0};
  double requestedStartBeats_{0.0};
  double requestedEndBeats_{0.0};
  HostTempoMap authority_;
  std::vector<HostTempoObservation> segments_;
  time::TempoMap tempoMap_{};
  std::vector<HostMeterSegment> meters_;
  bool loopActive_{false};
  double loopStartBeats_{0.0};
  double loopEndBeats_{0.0};
  std::size_t reportCount_{0U};
  std::uint64_t captureRevision_{0U};
  std::size_t seekCount_{0U};
  std::string captureContentHash_;
  std::string contentHash_;
};

// The host reports one Follow Host bounce is prepared from. Reports are accumulated as the
// host sends them: positions, tempo, meter and loop state. Nothing here is authoritative
// until freeze() admits a range, and nothing a refused report contained is silently dropped
// from the refusal's explanation.
class HostTimelineCapture final {
public:
  static constexpr std::size_t kMaximumReports{4096U};
  static constexpr std::size_t kMaximumMeterSegments{256U};

  void observe(const HostTimelineState& state, std::uint32_t sampleRate);
  void clear() noexcept;

  [[nodiscard]] std::size_t reportCount() const noexcept { return reportCount_; }
  [[nodiscard]] std::uint64_t captureRevision() const noexcept { return revision_; }
  [[nodiscard]] std::size_t seekCount() const noexcept { return seekCount_; }
  [[nodiscard]] bool empty() const noexcept { return reportCount_ == 0U; }
  [[nodiscard]] const HostTempoMap& tempoMap() const noexcept { return tempoMap_; }
  [[nodiscard]] std::span<const HostMeterSegment> meterSegments() const noexcept {
    return meters_;
  }
  [[nodiscard]] bool loopActive() const noexcept { return loopActive_; }
  [[nodiscard]] std::optional<std::uint32_t> sampleRate() const noexcept {
    return sampleRate_;
  }
  [[nodiscard]] bool sampleRateChanged() const noexcept { return sampleRateChanged_; }
  // Identity of the host's *content*, not of how many times the host repeated it. A host
  // that keeps reporting the same map while the transport advances produces the same hash.
  [[nodiscard]] std::string contentHash() const;
  // True while this capture still says the same musical thing about the range a prepared
  // authority was frozen for: same tempo segments inside it, same bounding observations,
  // same meter, same loop and sample rate. A host that merely keeps reporting as the
  // transport advances still describes the same authority.
  [[nodiscard]] bool describes(const PreparedHostTimeline& prepared) const;

  [[nodiscard]] core::Result<PreparedHostTimeline> freeze(
      const HostTimelineCaptureRequest& request) const;

private:
  HostTempoMap tempoMap_;
  std::vector<HostMeterSegment> meters_;
  std::optional<std::uint32_t> sampleRate_;
  bool sampleRateChanged_{false};
  bool loopActive_{false};
  bool loopHasBeats_{false};
  double loopStartBeats_{0.0};
  double loopEndBeats_{0.0};
  bool hasBeats_{false};
  bool playing_{false};
  double lastBeats_{0.0};
  bool meterDropped_{false};
  std::size_t reportCount_{0U};
  std::size_t seekCount_{0U};
  std::uint64_t revision_{0U};
};

}  // namespace seam::clap_editor
