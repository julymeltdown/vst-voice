#include "seam/clap_editor/host_tempo_map.hpp"

#include "seam/core/sha256.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace seam::clap_editor {
namespace {

bool validObservation(double beats, double bpm) noexcept {
  return std::isfinite(beats) && std::isfinite(bpm) && beats >= 0.0 &&
         beats <= HostTempoMap::kMaximumBeats && bpm >= HostTempoMap::kMinimumBpm &&
         bpm <= HostTempoMap::kMaximumBpm;
}

}  // namespace

core::Result<void> HostTempoMap::observe(double beats, double bpm) {
  if (!validObservation(beats, bpm)) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Host tempo observation is outside its supported range");
  }
  const HostTempoObservation value{beats, bpm};
  const auto position = std::lower_bound(observations_.begin(), observations_.end(), beats,
      [](const HostTempoObservation& entry, double target) {
        return entry.beats < target - HostTempoMap::kBeatsTolerance;
      });
  if (position != observations_.end() &&
      std::abs(position->beats - beats) <= HostTempoMap::kBeatsTolerance) {
    if (position->bpm == bpm) return core::success();
    position->bpm = bpm;
    ++revision_;
    return core::success();
  }
  if (observations_.size() >= kMaximumObservations) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Host tempo map exceeded its observation capacity");
  }
  observations_.insert(position, value);
  ++revision_;
  return core::success();
}

bool HostTempoMap::covers(double startBeats, double endBeats,
                          double maximumGapBeats) const noexcept {
  if (observations_.empty() || !std::isfinite(startBeats) || !std::isfinite(endBeats) ||
      !std::isfinite(maximumGapBeats) || maximumGapBeats <= 0.0 || endBeats <= startBeats) {
    return false;
  }
  if (observations_.front().beats > startBeats || observations_.back().beats < endBeats) {
    return false;
  }
  for (std::size_t index = 1U; index < observations_.size(); ++index) {
    if (observations_[index].beats - observations_[index - 1U].beats > maximumGapBeats) {
      return false;
    }
  }
  return true;
}

std::string HostTempoMap::uncoveredSpan(double startBeats, double endBeats,
                                        double maximumGapBeats) const {
  if (!std::isfinite(startBeats) || !std::isfinite(endBeats) || endBeats <= startBeats) {
    return {};
  }
  const auto describe = [](double begin, double end) {
    return std::to_string(begin) + ".." + std::to_string(end);
  };
  if (observations_.empty()) return describe(startBeats, endBeats);
  if (observations_.front().beats > startBeats) {
    return describe(startBeats, std::min(observations_.front().beats, endBeats));
  }
  if (std::isfinite(maximumGapBeats) && maximumGapBeats > 0.0) {
    for (std::size_t index = 1U; index < observations_.size(); ++index) {
      const auto previous = observations_[index - 1U].beats;
      const auto next = observations_[index].beats;
      if (next - previous <= maximumGapBeats) continue;
      if (next <= startBeats || previous >= endBeats) continue;
      return describe(std::max(previous, startBeats), std::min(next, endBeats));
    }
  }
  if (observations_.back().beats < endBeats) {
    return describe(std::max(observations_.back().beats, startBeats), endBeats);
  }
  return {};
}

std::string HostTempoMap::contentHash() const {
  std::string canonical;
  canonical.reserve(observations_.size() * 40U + 16U);
  for (const auto& entry : observations_) {
    canonical += std::to_string(entry.beats);
    canonical += ":";
    canonical += std::to_string(entry.bpm);
    canonical += ";";
  }
  return core::sha256Hex(canonical);
}

void HostTempoMap::clear() noexcept {
  if (!observations_.empty()) ++revision_;
  observations_.clear();
}

}  // namespace seam::clap_editor
