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
  // The declared mouth artwork, or nothing for a status-only package. A presentation never invents a
  // mouth from an absent asset: an undeclared shape falls back to the dock's own drawing.
  [[nodiscard]] const PixelSurface* mouth(character::MouthShape shape) const noexcept;
  [[nodiscard]] bool hasPerformanceAssets() const noexcept { return !mouths_.empty(); }
  // Whether this package declares itself a development turnaround. It is read from the package's own
  // bytes, so renaming or moving the directory cannot change the answer.
  [[nodiscard]] bool developmentOnly() const noexcept {
    return package_.has_value() && package_->manifest.developmentOnly;
  }
  // Whether this window should reserve the character dock. One predicate, because the surfaces that
  // ask the question disagreed: the plug-in tested whether a portrait had decoded, while standalone
  // tested the display mode, so the same project could reserve the dock in one and not the other. The
  // question is about the package and the display mode, and neither a decoded frame nor a loaded
  // portrait answers it. The mode is a parameter rather than read from this object's own state because
  // each surface owns its mode and they are set at different times in their own lifecycles.
  [[nodiscard]] bool dockVisible(domain::CharacterDisplayMode mode) const noexcept {
    // The display mode decides how much room the dock takes, not whether it exists: Full and Minimal
    // both show it and Off hides it. A decoded portrait is a drawing concern and is deliberately not
    // consulted, because a package that cannot draw is a load failure rather than a reason to reserve
    // the dock in one surface and not the other. Every state is mandatory at load, so a loaded package
    // is by construction one the dock can draw from.
    return mode != domain::CharacterDisplayMode::Off && loaded();
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
  std::map<character::MouthShape, PixelSurface> mouths_;
  character::State state_{character::State::Neutral};
  domain::CharacterDisplayMode mode_{domain::CharacterDisplayMode::Minimal};
  std::optional<character::CharacterPerformanceSnapshot> performance_;
  std::optional<character::PerformanceBindingKey> followedSinger_;
};

}  // namespace seam::native_ui
