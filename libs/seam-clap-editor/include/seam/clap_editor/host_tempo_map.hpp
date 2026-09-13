#pragma once

#include "seam/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace seam::clap_editor {

// One authoritative host tempo observation: the host's own musical position in beats
// and the tempo it reported there. A single instantaneous value is never a map, because
// it says nothing about what the host will do later in the rendered range.
struct HostTempoObservation final {
  double beats{0.0};
  double bpm{0.0};

  friend bool operator==(const HostTempoObservation&, const HostTempoObservation&) = default;
};

// The tempo history one host has actually reported, with an explicit revision and a
// canonical content hash. Follow Host final rendering must be authorized against a map
// that covers the rendered range, never against the tempo value that happened to be
// current when the render was requested.
class HostTempoMap final {
public:
  static constexpr std::size_t kMaximumObservations{4096U};
  static constexpr double kMinimumBpm{1.0};
  static constexpr double kMaximumBpm{1000.0};
  static constexpr double kMaximumBeats{1000000.0};
  // Two reports this close together are the same host position, not two events.
  static constexpr double kBeatsTolerance{1e-9};

  // Records one observation. The same position with the same tempo is idempotent; the
  // same position with a different tempo replaces it and advances the revision, because
  // a host that changed its mind must invalidate an older timing claim. Non-finite or
  // out-of-range values are refused rather than clamped, and the bounded capacity is
  // exhausted honestly instead of dropping history silently.
  [[nodiscard]] core::Result<void> observe(double beats, double bpm);

  // True when the map holds an observation at or before startBeats and one at or after
  // endBeats, with no gap between consecutive observations wider than maximumGapBeats.
  // The caller declares that tolerance; this class never invents one, and an empty
  // range is never reported as covered.
  [[nodiscard]] bool covers(double startBeats, double endBeats,
                            double maximumGapBeats) const noexcept;

  // The first span inside [startBeats, endBeats] this map cannot speak for, as a
  // human-readable a..b string, judged by the same tolerance as covers(), or empty when
  // nothing is missing.
  [[nodiscard]] std::string uncoveredSpan(double startBeats, double endBeats,
                                          double maximumGapBeats) const;

  [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
  [[nodiscard]] std::span<const HostTempoObservation> observations() const noexcept {
    return observations_;
  }
  [[nodiscard]] std::size_t size() const noexcept { return observations_.size(); }
  [[nodiscard]] bool empty() const noexcept { return observations_.empty(); }
  // Canonical hash of the observed history, not of the revision counter: two hosts that
  // reported the same map must produce the same timing identity.
  [[nodiscard]] std::string contentHash() const;
  void clear() noexcept;

private:
  std::vector<HostTempoObservation> observations_;
  std::uint64_t revision_{0U};
};

}  // namespace seam::clap_editor
