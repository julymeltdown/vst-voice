#include "seam/native_ui/character_presentation.hpp"

#include <array>

namespace seam::native_ui {
namespace {

core::Result<void> makeCornerColorTransparent(PixelSurface& image) {
  if (image.width() == 0U || image.height() == 0U)
    return core::failure(core::ErrorCode::InvalidArgument,
                         "A mouth overlay image is empty");
  auto pixels = image.pixels();
  const auto colorMask = 0x00FFFFFFU;
  const auto key = pixels.front() & colorMask;
  const auto bottomLeft = static_cast<std::size_t>(image.height() - 1U) * image.width();
  const auto bottomRight = bottomLeft + image.width() - 1U;
  if ((pixels[image.width() - 1U] & colorMask) != key ||
      (pixels[bottomLeft] & colorMask) != key ||
      (pixels[bottomRight] & colorMask) != key)
    return core::failure(core::ErrorCode::InvariantViolation,
                         "A placed mouth overlay must have one flat corner background color");
  for (auto& pixel : pixels)
    if ((pixel & colorMask) == key) pixel &= colorMask;
  return core::success();
}

}  // namespace

namespace {

bool followsManifestSinger(const character::Manifest& manifest,
                           const character::PerformanceBindingKey& key) noexcept {
  if (manifest.resourceIdentity.has_value()) {
    const auto& resource = *manifest.resourceIdentity;
    return key.resourceKind == resource.kind && key.resourceId == resource.id &&
           key.resourceVersion == resource.version &&
           key.resourceContentHash == resource.contentHash;
  }
  return key.resourceKind == domain::SingerResourceKind::Sample &&
         key.resourceId == manifest.voicebankId;
}

}  // namespace

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
  // A performance package is loaded whole: every shape it declares must decode, so a presentation
  // cannot end up drawing half of one turnaround and half of its own fallback.
  std::map<character::MouthShape, PixelSurface> mouths;
  constexpr std::array<character::MouthShape, 6> shapes{
      character::MouthShape::Closed, character::MouthShape::Narrow,
      character::MouthShape::Nasal, character::MouthShape::Open,
      character::MouthShape::Wide, character::MouthShape::Round};
  for (const auto shape : shapes) {
    const auto relative = package.value().manifest.mouthAssetFor(shape);
    if (relative.empty()) continue;
    auto mouth = PixelSurface::loadPpm(package.value().mouthAssetPath(shape), maximumAssetBytes);
    if (!mouth) return core::Result<void>{mouth.error()};
    if (package.value().manifest.mouthOverlayPlacement()) {
      const auto keyed = makeCornerColorTransparent(mouth.value());
      if (!keyed) return keyed;
    }
    mouths.emplace(shape, std::move(mouth.value()));
  }
  package_ = std::move(package.value());
  portraits_ = std::move(loaded);
  mouths_ = std::move(mouths);
  state_ = package_->manifest.defaultState;
  // A successfully loaded package is a new artwork/voice association. Even reloading the same
  // package must not carry an earlier phrase into a new presentation lifetime.
  followedSinger_.reset();
  performance_.reset();
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

const PixelSurface* CharacterPresentation::mouth(character::MouthShape shape) const noexcept {
  const auto iterator = mouths_.find(shape);
  return iterator == mouths_.end() ? nullptr : &iterator->second;
}

std::string CharacterPresentation::displayName() const {
  return package_.has_value() ? package_->manifest.displayName : std::string{};
}

std::string CharacterPresentation::styleName() const {
  return package_.has_value() ? package_->manifest.style : std::string{};
}

core::Result<void> CharacterPresentation::setPerformanceSnapshot(
    character::CharacterPerformanceSnapshot snapshot) {
  const auto valid = snapshot.validate();
  if (!valid) return valid;
  // A snapshot may only drive the singer the dock is following. With no singer selected yet the
  // first snapshot adopts its own identity, which keeps a single-singer host from having to announce
  // and then bind in two steps; every later switch is explicit.
  const auto key = character::performanceBindingKey(snapshot);
  if (package_.has_value() && !followsManifestSinger(package_->manifest, key))
    return core::Result<void>{core::Error{core::ErrorCode::Conflict,
        "A performance snapshot does not belong to the loaded character package"}};
  if (followedSinger_.has_value() && !(*followedSinger_ == key))
    return core::Result<void>{core::Error{core::ErrorCode::Conflict,
        "A performance snapshot does not belong to the singer the dock follows"}};
  followedSinger_ = key;
  performance_ = std::move(snapshot);
  return core::success();
}

core::Result<void> CharacterPresentation::followSinger(character::PerformanceBindingKey key) {
  if (key.resourceId.empty() || key.resourceVersion.empty() || key.resourceContentHash.empty() ||
      key.style.empty())
    return core::Result<void>{core::Error{core::ErrorCode::InvalidArgument,
        "A followed singer has no complete identity"}};
  const auto resource = domain::SingerResourceIdentity{
      .kind = key.resourceKind,
      .id = key.resourceId,
      .version = key.resourceVersion,
      .contentHash = key.resourceContentHash};
  const auto validResource = resource.validate();
  if (!validResource) return validResource;
  if (package_.has_value() && !followsManifestSinger(package_->manifest, key))
    return core::Result<void>{core::Error{core::ErrorCode::Conflict,
        "The followed singer does not belong to the loaded character package"}};
  if (!followedSinger_.has_value() || !(*followedSinger_ == key)) performance_.reset();
  followedSinger_ = std::move(key);
  return core::success();
}

character::CharacterPerformanceFrame CharacterPresentation::performanceFrameAt(
    time::SampleFrame playhead) const noexcept {
  if (!performance_.has_value()) return character::CharacterPerformanceFrame{};
  return character::characterPerformanceFrameAt(*performance_, playhead);
}

}  // namespace seam::native_ui
