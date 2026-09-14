#pragma once

#include "seam/character/character.hpp"
#include "seam/character/performance.hpp"
#include "seam/core/result.hpp"
#include "seam/domain/project.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/time/tick.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace seam::native_ui {

class CharacterPresentation final {
public:
  [[nodiscard]] core::Result<void> load(
      const std::filesystem::path& packageRoot,
      std::uint64_t maximumAssetBytes = 16ULL * 1024ULL * 1024ULL);

  [[nodiscard]] bool loaded() const noexcept { return package_.has_value(); }
  [[nodiscard]] const character::Package* package() const noexcept {
    return package_.has_value() ? &*package_ : nullptr;
  }
  [[nodiscard]] const PixelSurface* portrait(character::State state) const noexcept;
  [[nodiscard]] const PixelSurface* portrait() const noexcept {
    return portrait(state_);
  }
  void setState(character::State state) noexcept { state_ = state; }
  [[nodiscard]] character::State state() const noexcept { return state_; }
  void setDisplayMode(domain::CharacterDisplayMode mode) noexcept { mode_ = mode; }
  [[nodiscard]] domain::CharacterDisplayMode displayMode() const noexcept { return mode_; }
  [[nodiscard]] std::string displayName() const;
  [[nodiscard]] std::string styleName() const;

  // What the character is singing is a separate read model from what the dock is reporting. A
  // snapshot that does not validate is refused rather than drawn, so a presentation can never show
  // a mouth the phrase it came from does not support. Clearing it leaves the operational state
  // exactly where it was.
  [[nodiscard]] core::Result<void> setPerformanceSnapshot(
      character::CharacterPerformanceSnapshot snapshot);
  // Which singer the dock follows. Selecting a different resource, style or render revision closes
  // the mouth: the phrase that was showing was the previous singer's, and the dock must not keep
  // drawing it while the new one has not been performed yet.
  [[nodiscard]] core::Result<void> followSinger(character::PerformanceBindingKey key);
  [[nodiscard]] bool followingSinger() const noexcept { return followedSinger_.has_value(); }
  [[nodiscard]] const character::PerformanceBindingKey& followedSinger() const
      noexcept { return *followedSinger_; }
  void clearPerformanceSnapshot() noexcept { performance_.reset(); }
  [[nodiscard]] bool hasPerformanceSnapshot() const noexcept {
    return performance_.has_value();
  }
  [[nodiscard]] const character::CharacterPerformanceSnapshot* performanceSnapshot() const noexcept {
    return performance_.has_value() ? &*performance_ : nullptr;
  }
  // With no snapshot the answer is a closed, silent, not-performing frame: the absence of a phrase
  // is not a phrase of silence, and it does not change state() either.
  [[nodiscard]] character::CharacterPerformanceFrame performanceFrameAt(
      time::SampleFrame playhead) const noexcept;

private:
  std::optional<character::Package> package_;
  std::map<character::State, PixelSurface> portraits_;
  character::State state_{character::State::Neutral};
  domain::CharacterDisplayMode mode_{domain::CharacterDisplayMode::Minimal};
  std::optional<character::CharacterPerformanceSnapshot> performance_;
  std::optional<character::PerformanceBindingKey> followedSinger_;
};

}  // namespace seam::native_ui
