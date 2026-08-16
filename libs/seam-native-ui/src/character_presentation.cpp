#include "seam/native_ui/character_presentation.hpp"

#include <array>

namespace seam::native_ui {

core::Result<void> CharacterPresentation::load(
    const std::filesystem::path& packageRoot, std::uint64_t maximumAssetBytes) {
  auto package = character::loadPackage(packageRoot);
  if (!package) return core::Result<void>{package.error()};
  constexpr std::array<character::State, 6> states{
      character::State::Neutral, character::State::Focused,
      character::State::Rendering, character::State::Complete,
      character::State::Warning, character::State::Error};
  std::map<character::State, PixelSurface> loaded;
  for (const auto state : states) {
    auto portrait = PixelSurface::loadPpm(package.value().assetPath(state), maximumAssetBytes);
    if (!portrait) return core::Result<void>{portrait.error()};
    loaded.emplace(state, std::move(portrait.value()));
  }
  package_ = std::move(package.value());
  portraits_ = std::move(loaded);
  state_ = package_->manifest.defaultState;
  return core::success();
}

const PixelSurface* CharacterPresentation::portrait(character::State state) const noexcept {
  const auto iterator = portraits_.find(state);
  if (iterator != portraits_.end()) return &iterator->second;
  if (package_.has_value()) {
    const auto fallback = portraits_.find(package_->manifest.defaultState);
    if (fallback != portraits_.end()) return &fallback->second;
  }
  return nullptr;
}

std::string CharacterPresentation::displayName() const {
  return package_.has_value() ? package_->manifest.displayName : std::string{};
}

std::string CharacterPresentation::styleName() const {
  return package_.has_value() ? package_->manifest.style : std::string{};
}

}  // namespace seam::native_ui
