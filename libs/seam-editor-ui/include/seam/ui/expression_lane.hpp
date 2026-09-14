#pragma once

#include "seam/application/editor_session.hpp"
#include "seam/domain/project.hpp"
#include "seam/synthesis/renderer_capabilities.hpp"

#include <cstddef>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace seam::ui {

// One editable surface for the timbral channels that share a region-scoped curve. Each channel keeps
// its own persisted unit and bound; the surface is shared, the meaning is not.
enum class ExpressionChannel { Formant, Breathiness, Tension, Airiness, Gender, Growl };

inline constexpr std::size_t kExpressionChannelCount = 6U;

struct ExpressionChannelDescriptor final {
  ExpressionChannel channel{ExpressionChannel::Formant};
  std::string_view id;
  std::string_view label;
  // What the persisted number means, so a surface never presents semitones as a normalized share.
  std::string_view unit;
  float minimum{-24.0F};
  float maximum{24.0F};
  float neutral{0.0F};
  // One keyboard step. A normalized channel moves a tenth; a semitone channel moves a semitone.
  float step{1.0F};
  bool bipolar{false};
  synthesis::RendererControl control{synthesis::RendererControl::Formant};
};

[[nodiscard]] ExpressionChannelDescriptor describeExpressionChannel(ExpressionChannel channel) noexcept;
[[nodiscard]] ExpressionChannel expressionChannelAt(std::size_t index) noexcept;
[[nodiscard]] std::size_t expressionChannelIndex(ExpressionChannel channel) noexcept;
// Wraps, so a channel picker has a defined order instead of an unbounded index.
[[nodiscard]] ExpressionChannel nextExpressionChannel(ExpressionChannel channel, int direction) noexcept;

struct ExpressionPoint final {
  time::Tick tick;
  float amount{0.0F};

  friend bool operator==(const ExpressionPoint&, const ExpressionPoint&) = default;
};

// The stored curve of one channel, in the channel's own persisted unit. A surface can therefore show
// what is stored even when the selected singer refuses to render it.
[[nodiscard]] std::vector<ExpressionPoint> readExpressionPoints(const domain::VocalRegion& region,
                                                               ExpressionChannel channel);

// Why a channel cannot be rendered by the selected singer, in the product's own words. Empty means
// the selected carrier advertises the channel.
[[nodiscard]] core::Result<void> validateExpressionCarrier(
    const domain::Project& project, domain::TrackId trackId, ExpressionChannel channel);

// A region-scoped draft of one channel, captured from an immutable job context exactly as the
// dynamics lane is. Nothing here renders, allocates or touches the audio callback.
class ExpressionLaneModel final {
public:
  enum class State { Ready, Applied, Cancelled };

  [[nodiscard]] static core::Result<ExpressionLaneModel> prepare(
      const application::EditorSession& session, domain::RegionId region,
      ExpressionChannel channel, std::stop_token stop = {});

  [[nodiscard]] ExpressionChannel channel() const noexcept { return channel_; }
  [[nodiscard]] ExpressionChannelDescriptor descriptor() const noexcept {
    return describeExpressionChannel(channel_);
  }
  [[nodiscard]] const std::vector<ExpressionPoint>& points() const noexcept { return draft_; }
  [[nodiscard]] const std::vector<ExpressionPoint>& sourcePoints() const noexcept {
    return source_;
  }
  [[nodiscard]] bool hasChanges() const noexcept { return draft_ != source_; }
  [[nodiscard]] State state() const noexcept { return state_; }
  [[nodiscard]] float valueAt(time::Tick tick) const noexcept;
  // The channel's own carrier decision for the track the region lives on.
  [[nodiscard]] core::Result<void> editable() const;
  [[nodiscard]] core::Result<void> upsert(ExpressionPoint point);
  // Wholesale replacement for a gesture that rewrites several points at once, validated as a whole
  // so the draft can never hold an unordered or out-of-range curve.
  [[nodiscard]] core::Result<void> replacePoints(std::vector<ExpressionPoint> points);
  [[nodiscard]] core::Result<void> erase(time::Tick tick);
  // Moving onto a different existing point rejects rather than silently merging, exactly as the
  // dynamics lane does.
  [[nodiscard]] core::Result<void> move(time::Tick from, ExpressionPoint to);
  [[nodiscard]] core::Result<void> reset();
  [[nodiscard]] core::Result<void> apply(application::EditorSession& session,
                                         domain::RegionId activeRegion,
                                         std::stop_token stop = {});
  void cancel() noexcept;
  [[nodiscard]] bool matches(const application::EditorSession& session,
                             domain::RegionId activeRegion) const;

private:
  ExpressionLaneModel(application::PerformanceJobContext context, domain::RegionId region,
                      domain::TrackId track, ExpressionChannel channel, std::uint64_t revision,
                      std::vector<ExpressionPoint> points);
  [[nodiscard]] core::Result<void> validatePoint(ExpressionPoint point) const;
  [[nodiscard]] core::Result<void> commit(application::EditorSession& session) const;

  application::PerformanceJobContext context_;
  domain::RegionId region_;
  domain::TrackId track_;
  ExpressionChannel channel_{ExpressionChannel::Formant};
  std::uint64_t revision_{0U};
  std::vector<ExpressionPoint> source_;
  std::vector<ExpressionPoint> draft_;
  State state_{State::Ready};
};

}  // namespace seam::ui
