#pragma once

#include "seam/character/character.hpp"
#include "seam/core/result.hpp"
#include "seam/domain/project.hpp"
#include "seam/native_ui/pixel_surface.hpp"

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

private:
  std::optional<character::Package> package_;
  std::map<character::State, PixelSurface> portraits_;
  character::State state_{character::State::Neutral};
  domain::CharacterDisplayMode mode_{domain::CharacterDisplayMode::Minimal};
};

}  // namespace seam::native_ui
