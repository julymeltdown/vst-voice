// A character package either declares a performance turnaround or it does not, and the difference has
// to be readable from the package's own bytes.
//
// A status-only package names six operating-state assets; a presentation may show the character's
// state and must not invent a mouth for it. A performance package declares every mouth shape it can be
// asked for, because a partially authored turnaround blended with the dock's own fallback drawing is
// worse than a status-only character that honestly has no face. The development flag travels with the
// package rather than with its path, so renaming or moving a directory cannot promote artwork.
//
// The assets here are flat colours written by the test, not reviewed artwork, and nothing is listened
// to or looked at by a person: these are the package-format rules.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/character/character.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/native_ui/character_presentation.hpp"
#include "seam/native_ui/pixel_surface.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace {

using namespace seam;

constexpr std::array<std::string_view, 6> kStateNames{
    "neutral", "focused", "rendering", "complete", "warning", "error"};
constexpr std::array<std::string_view, 6> kMouthNames{
    "closed", "narrow", "nasal", "open", "wide", "round"};

enum class Mouths { None, Complete, Partial, Escaping, Missing };

void writeSurface(const std::filesystem::path& path, std::uint8_t red) {
  std::filesystem::create_directories(path.parent_path());
  native_ui::PixelSurface surface{4U, 4U};
  surface.clear(native_ui::Color{red, 90U, 120U, 255U});
  CHECK(surface.writePpm(path));
}

std::filesystem::path writePackage(const std::filesystem::path& root, std::int64_t schemaVersion,
                                   Mouths mouths, std::optional<bool> developmentOnly) {
  std::filesystem::create_directories(root / "runtime");
  std::uint8_t red = 20U;
  formats::JsonValue::Object states;
  for (const auto name : kStateNames) {
    writeSurface(root / "runtime" / (std::string{name} + ".ppm"), red);
    red = static_cast<std::uint8_t>(red + 10U);
    states.emplace(std::string{name}, "runtime/" + std::string{name} + ".ppm");
  }
  formats::JsonValue::Object manifest;
  manifest.emplace("schemaVersion", formats::JsonValue{schemaVersion});
  manifest.emplace("characterId", "official.character.test");
  manifest.emplace("displayName", "Test Character");
  manifest.emplace("version", "1.0.0");
  manifest.emplace("voicebankId", "voice.test");
  manifest.emplace("style", "emo-low-poly");
  manifest.emplace("defaultState", "neutral");
  manifest.emplace("accent", formats::JsonValue{formats::JsonValue::Object{
                                 {"primary", "#8B4C69"}, {"secondary", "#6E5A86"}}});
  if (developmentOnly.has_value())
    manifest.emplace("developmentOnly", formats::JsonValue{*developmentOnly});
  manifest.emplace("states", formats::JsonValue{std::move(states)});
  if (mouths != Mouths::None) {
    const auto written = mouths == Mouths::Partial ? kMouthNames.size() - 1U : kMouthNames.size();
    formats::JsonValue::Object declared;
    for (std::size_t index = 0U; index < written; ++index) {
      const auto& name = kMouthNames[index];
      if (mouths == Mouths::Escaping && index == 0U) {
        declared.emplace(std::string{name}, "../escape.ppm");
        continue;
      }
      if (mouths == Mouths::Missing && index == 0U) {
        declared.emplace(std::string{name}, "runtime/absent.ppm");
        continue;
      }
      const auto relative = "runtime/mouth-" + std::string{name} + ".ppm";
      writeSurface(root / relative, static_cast<std::uint8_t>(150U + index));
      declared.emplace(std::string{name}, relative);
    }
    manifest.emplace("mouths", formats::JsonValue{std::move(declared)});
  }
  std::ofstream output(root / "manifest.json", std::ios::binary | std::ios::trunc);
  output << formats::stringifyJson(formats::JsonValue{std::move(manifest)});
  output.close();
  return root;
}

TEST_CASE("A status-only package stays status-only and is never given a mouth") {
  const auto root = writePackage(test::support::temporaryDirectory("character-status-only"),
                                 character::kStatusOnlyManifestSchema, Mouths::None, std::nullopt);
  const auto package = character::loadPackage(root);
  CHECK(package.hasValue());
  CHECK(!package.value().manifest.declaresPerformance());
  CHECK(package.value().manifest.mouthAssetFor(character::MouthShape::Open).empty());
  CHECK(!package.value().manifest.developmentOnly);

  native_ui::CharacterPresentation presentation;
  CHECK(presentation.load(root).hasValue());
  CHECK(presentation.loaded());
  CHECK(!presentation.hasPerformanceAssets());
  CHECK(!presentation.developmentOnly());
  CHECK(presentation.mouth(character::MouthShape::Open) == nullptr);
  CHECK(presentation.portrait() != nullptr);
}

TEST_CASE("A performance package declares every mouth it can be asked for") {
  const auto root = writePackage(test::support::temporaryDirectory("character-performance"),
                                 character::kPerformanceManifestSchema, Mouths::Complete, true);
  const auto package = character::loadPackage(root);
  CHECK(package.hasValue());
  CHECK(package.value().manifest.declaresPerformance());
  CHECK(package.value().manifest.developmentOnly);
  for (const auto shape : {character::MouthShape::Closed, character::MouthShape::Narrow,
                           character::MouthShape::Nasal, character::MouthShape::Open,
                           character::MouthShape::Wide, character::MouthShape::Round}) {
    CHECK(!package.value().manifest.mouthAssetFor(shape).empty());
    CHECK(std::filesystem::exists(package.value().mouthAssetPath(shape)));
  }

  native_ui::CharacterPresentation presentation;
  CHECK(presentation.load(root).hasValue());
  CHECK(presentation.hasPerformanceAssets());
  CHECK(presentation.developmentOnly());
  for (const auto shape : {character::MouthShape::Closed, character::MouthShape::Narrow,
                           character::MouthShape::Nasal, character::MouthShape::Open,
                           character::MouthShape::Wide, character::MouthShape::Round}) {
    const auto* mouth = presentation.mouth(shape);
    CHECK(mouth != nullptr);
    if (mouth != nullptr) CHECK(mouth->width() == 4U);
  }
}

TEST_CASE("A partial or contradictory package is refused by cause") {
  const auto partial = character::loadPackage(writePackage(
      test::support::temporaryDirectory("character-partial"), character::kPerformanceManifestSchema,
      Mouths::Partial, false));
  CHECK(!partial.hasValue());
  CHECK(partial.error().code == core::ErrorCode::InvariantViolation);

  // A status-only schema cannot carry a performance field at all: that would let a package look like a
  // turnaround while its own schema says a reader may not expect one.
  const auto claiming = character::loadPackage(writePackage(
      test::support::temporaryDirectory("character-status-claiming"),
      character::kStatusOnlyManifestSchema, Mouths::Complete, std::nullopt));
  CHECK(!claiming.hasValue());
  CHECK(claiming.error().code == core::ErrorCode::InvariantViolation);

  const auto flagged = character::loadPackage(
      writePackage(test::support::temporaryDirectory("character-status-flagged"),
                   character::kStatusOnlyManifestSchema, Mouths::None, true));
  CHECK(!flagged.hasValue());
  CHECK(flagged.error().code == core::ErrorCode::InvariantViolation);

  const auto future = character::loadPackage(writePackage(
      test::support::temporaryDirectory("character-future"), 3, Mouths::Complete, false));
  CHECK(!future.hasValue());
  CHECK(future.error().code == core::ErrorCode::Unsupported);
}

TEST_CASE("A mouth asset that escapes the package or is absent is refused") {
  const auto escaping = character::loadPackage(writePackage(
      test::support::temporaryDirectory("character-escaping"), character::kPerformanceManifestSchema,
      Mouths::Escaping, false));
  CHECK(!escaping.hasValue());
  CHECK(escaping.error().code == core::ErrorCode::InvariantViolation);

  const auto missing = character::loadPackage(writePackage(
      test::support::temporaryDirectory("character-missing-mouth"),
      character::kPerformanceManifestSchema, Mouths::Missing, false));
  CHECK(!missing.hasValue());
  CHECK(missing.error().code == core::ErrorCode::IoError);
}

TEST_CASE("Renaming a development package cannot promote it") {
  const auto source = writePackage(test::support::temporaryDirectory("character-development-source"),
                                   character::kPerformanceManifestSchema, Mouths::Complete, true);
  // A promoted-looking destination name is exactly the rename this rule exists to defeat.
  const auto destination = test::support::temporaryDirectory("character-production-release");
  std::filesystem::copy(source, destination,
                        std::filesystem::copy_options::recursive |
                            std::filesystem::copy_options::overwrite_existing);
  const auto promoted = character::loadPackage(destination);
  CHECK(promoted.hasValue());
  CHECK(promoted.value().manifest.developmentOnly);
  native_ui::CharacterPresentation presentation;
  CHECK(presentation.load(destination).hasValue());
  CHECK(presentation.developmentOnly());
  CHECK(presentation.hasPerformanceAssets());
}

}  // namespace

// One predicate decides whether a window reserves the character dock. The surfaces disagreed: the
// plug-in asked whether a portrait had decoded, standalone asked the display mode. Since the portrait is
// published for the current render status, the plug-in's answer could change when the state artwork
// changed -- the dock appearing or disappearing while nothing about the creator's intent had moved.
TEST_CASE("Dock presence follows the package and the display mode, not a decoded frame") {
  const auto root = test::support::temporaryDirectory("character-dock-presence");
  const auto packageRoot = writePackage(root / "package", 1, Mouths::None, std::nullopt);
  native_ui::CharacterPresentation presentation;
  CHECK(presentation.load(packageRoot).hasValue());

  // Off hides the dock whatever the package offers, which is a creator's explicit instruction.
  CHECK(!presentation.dockVisible(domain::CharacterDisplayMode::Off));
  // Full and Minimal both reserve it. Minimal is a smaller presentation, not an absent one, and reading
  // it as absence is exactly the disagreement this predicate removes.
  const auto visible = presentation.dockVisible(domain::CharacterDisplayMode::Full) &&
                       presentation.dockVisible(domain::CharacterDisplayMode::Minimal);
  CHECK(visible);

  // A presentation with no package reserves nothing, whatever mode is asked, because the dock would have
  // no artwork to draw and the roll should keep the room.
  native_ui::CharacterPresentation empty;
  CHECK(!empty.loaded());
  for (const auto mode : {domain::CharacterDisplayMode::Full, domain::CharacterDisplayMode::Minimal,
                          domain::CharacterDisplayMode::Off}) {
    CHECK(!empty.dockVisible(mode));
  }

  // The answer does not depend on which frame is currently being drawn, which is the property that
  // makes it usable for layout. Asking twice with a different render status in between -- the states a
  // render cycle walks -- must give the same answer.
  for (const auto state : {character::State::Neutral, character::State::Rendering,
                           character::State::Complete, character::State::Warning,
                           character::State::Error}) {
    presentation.setState(state);
    CHECK(presentation.portrait() != nullptr);
    CHECK(presentation.dockVisible(domain::CharacterDisplayMode::Full));
  }
}
