// The shipped Character 01 package and splash key art, as a development host loads them.
//
// These tests pin what the artwork must be for the state surfaces to mean anything: every state the
// shell can resolve decodes from the package, the six package states are different pictures, the
// singing mouth sprites land on the singing face without a seam, and the EMO/SCENE splash images
// decode at the plan's size. They do not judge the art itself; that is the owner's review.
#include "test_framework.hpp"

#include "seam/character/character.hpp"
#include "seam/native_ui/design/character_surface.hpp"
#include "seam/native_ui/paint/canvas2d.hpp"
#include "seam/native_ui/pixel_surface.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <utility>

namespace {

using seam::character::MouthShape;
using seam::native_ui::PixelSurface;
using seam::native_ui::design::CharacterState;
using seam::native_ui::design::CharacterSurface;

// The tests run from the repository root, like the other character-surface tests.
const std::filesystem::path kPackage{"assets/character-01"};

constexpr std::uint32_t kPortraitWidth = 320U;
constexpr std::uint32_t kPortraitHeight = 480U;

// One shell state per package state: Idle and Listening share neutral, Singing is focused.
constexpr std::array<CharacterState, 6> kOnePerPackageState{
    CharacterState::Idle,     CharacterState::Singing, CharacterState::Rendering,
    CharacterState::Complete, CharacterState::Warning, CharacterState::Error};

constexpr std::array<MouthShape, 6> kMouths{MouthShape::Closed, MouthShape::Narrow,
                                            MouthShape::Nasal,  MouthShape::Open,
                                            MouthShape::Wide,   MouthShape::Round};

int channel(std::uint32_t pixel, unsigned shift) { return static_cast<int>((pixel >> shift) & 0xFFU); }

int colorDistance(std::uint32_t a, std::uint32_t b) {
  return std::abs(channel(a, 16U) - channel(b, 16U)) + std::abs(channel(a, 8U) - channel(b, 8U)) +
         std::abs(channel(a, 0U) - channel(b, 0U));
}

// Mean absolute per-channel difference of two equally sized surfaces, 0..255.
double meanDifference(const PixelSurface& a, const PixelSurface& b) {
  const auto left = a.pixels();
  const auto right = b.pixels();
  double total = 0.0;
  for (std::size_t i = 0U; i < left.size(); ++i) total += colorDistance(left[i], right[i]);
  return total / (3.0 * static_cast<double>(left.size()));
}

bool transparent(std::uint32_t pixel) { return (pixel >> 24U) == 0U; }

}  // namespace

TEST_CASE("the shipped character package loads a distinct 320x480 portrait for every state") {
  CharacterSurface surface;
  CHECK(surface.loadPackage(kPackage));
  if (!surface.packageLoaded()) return;
  CHECK(surface.package()->manifest.developmentOnly);
  for (const auto state : {CharacterState::Idle, CharacterState::Listening, CharacterState::Singing,
                           CharacterState::Rendering, CharacterState::Complete,
                           CharacterState::Warning, CharacterState::Error}) {
    const auto* portrait = surface.portrait(state);
    CHECK(portrait != nullptr);
    if (portrait == nullptr) continue;
    CHECK(portrait->width() == kPortraitWidth);
    CHECK(portrait->height() == kPortraitHeight);
  }
  // Byte-identical state files were the defect this art replaces: each pair must be a different
  // picture, not a re-encode. Poses and expressions differ over a large share of the frame, so a
  // mean channel difference of several levels separates them from any near-duplicate.
  for (std::size_t i = 0U; i < kOnePerPackageState.size(); ++i) {
    for (std::size_t j = i + 1U; j < kOnePerPackageState.size(); ++j) {
      const auto* a = surface.portrait(kOnePerPackageState[i]);
      const auto* b = surface.portrait(kOnePerPackageState[j]);
      if (a == nullptr || b == nullptr) continue;
      CHECK(meanDifference(*a, *b) > 6.0);
    }
  }
}

TEST_CASE("the shipped singing mouths sit on the singing face without a visible edge") {
  CharacterSurface surface;
  CHECK(surface.loadPackage(kPackage));
  if (!surface.packageLoaded()) return;
  CHECK(surface.declaresPerformance());
  const auto placement = surface.mouthPlacement();
  CHECK(placement.has_value());
  const auto* singing = surface.portrait(CharacterState::Singing);
  CHECK(singing != nullptr);
  if (!placement.has_value() || singing == nullptr) return;
  const auto left = static_cast<std::uint32_t>(std::lround(placement->x * kPortraitWidth));
  const auto top = static_cast<std::uint32_t>(std::lround(placement->y * kPortraitHeight));
  const auto width = static_cast<std::uint32_t>(std::lround(placement->width * kPortraitWidth));
  const auto height = static_cast<std::uint32_t>(std::lround(placement->height * kPortraitHeight));
  // Each shape composited over the singing portrait's box, to compare the shapes as seen.
  std::array<PixelSurface, kMouths.size()> composites{};
  for (std::size_t index = 0U; index < kMouths.size(); ++index) {
    const auto* sprite = surface.mouth(kMouths[index]);
    CHECK(sprite != nullptr);
    if (sprite == nullptr) continue;
    // The sprite is exactly the declared box, drawn 1:1 over the portrait's own pixels.
    CHECK(sprite->width() == width);
    CHECK(sprite->height() == height);
    if (sprite->width() != width || sprite->height() != height) continue;
    const auto pixels = sprite->pixels();
    // The placed-overlay contract: one flat corner colour, keyed out, so only the lips are drawn.
    CHECK(transparent(pixels.front()));
    CHECK(transparent(pixels[width - 1U]));
    CHECK(transparent(pixels[(height - 1U) * width]));
    CHECK(transparent(pixels.back()));
    // Every opaque pixel on the edge of the drawn region (next to a keyed pixel or the box edge) is
    // the singing portrait's face at the same place, so the overlay has no seam; a sprite cut from
    // another frame or placed a few pixels off fails this.
    const auto at = [&](std::uint32_t x, std::uint32_t y) { return pixels[y * width + x]; };
    long long total = 0;
    std::size_t count = 0U;
    std::size_t opaque = 0U;
    composites[index] = PixelSurface{width, height};
    for (std::uint32_t y = 0U; y < height; ++y) {
      for (std::uint32_t x = 0U; x < width; ++x) {
        const auto face = singing->pixels()[(top + y) * kPortraitWidth + left + x];
        const auto pixel = at(x, y);
        composites[index].pixels()[y * width + x] = transparent(pixel) ? face : pixel;
        if (transparent(pixel)) continue;
        ++opaque;
        const auto edge = x == 0U || y == 0U || x + 1U == width || y + 1U == height ||
                          transparent(at(x - 1U, y)) || transparent(at(x + 1U, y)) ||
                          transparent(at(x, y - 1U)) || transparent(at(x, y + 1U));
        if (!edge) continue;
        total += colorDistance(pixel, face);
        ++count;
      }
    }
    CHECK(opaque > 20U);
    CHECK(count > 0U);
    if (count > 0U) CHECK(static_cast<double>(total) / (3.0 * static_cast<double>(count)) < 4.0);
  }
  // Each shape is its own drawing as it appears on the face.
  for (std::size_t i = 0U; i < composites.size(); ++i) {
    for (std::size_t j = i + 1U; j < composites.size(); ++j) {
      if (composites[i].width() != width || composites[j].width() != width) continue;
      CHECK(meanDifference(composites[i], composites[j]) > 1.0);
    }
  }
}

TEST_CASE("the EMO and SCENE splash key art decodes at 1600x1000") {
  for (const auto* mode : {"emo", "scene"}) {
    const auto image =
        seam::native_ui::paint::loadImage(std::filesystem::path{"assets/ui-design"} / mode /
                                          "splash.png");
    CHECK(image != nullptr);
    if (image == nullptr) continue;
    CHECK(image->width() == 1600U);
    CHECK(image->height() == 1000U);
  }
}
