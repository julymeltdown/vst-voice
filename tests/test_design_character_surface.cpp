// The character state surfaces of the SING shell: the state each read model maps to, which asset the
// artwork is drawn from, the Stage's exclusions, the animator's clock and its Reduce Motion rule, the
// empty-project line and the error toast.
//
// These are unit tests over the mapping, the layout and the painters' decisions. They do not open a
// window, compare a screenshot, or say whether the development turnaround is artwork the owner
// approved: the state being drawn truthfully is the whole claim.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/application/view_commands.hpp"
#include "seam/character/character.hpp"
#include "seam/core/file_io.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/native_ui/design/character_surface.hpp"
#include "seam/native_ui/design/sing_layout.hpp"
#include "seam/native_ui/design/sing_shell.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/paint/canvas2d.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/native_ui/render_status_panel.hpp"
#include "seam/native_ui/voice_identity.hpp"
#include "seam/text/unicode.hpp"

// A sentence drawn through drawTextWrapped has to appear on a surface that has no font engine, which
// is every headless surface: the call returned early there, so the text was neither wrapped nor
// clipped, it was simply absent, and only a rendered frame showed it. It also has to break at a word
// boundary, because breaking at the column is what puts half a word on the next line.
TEST_CASE("Wrapped text is drawn without a font engine and breaks between words") {
  using seam::native_ui::Color;
  using seam::native_ui::PixelSurface;
  using seam::native_ui::RasterCanvas;
  using seam::ui::Rect;
  const Color background{15, 14, 18, 255};
  const auto inkIn = [background](const PixelSurface& surface, Rect area) {
    std::size_t count = 0U;
    for (auto y = static_cast<std::int32_t>(area.y);
         y < static_cast<std::int32_t>(area.bottom()); ++y) {
      for (auto x = static_cast<std::int32_t>(area.x);
           x < static_cast<std::int32_t>(area.right()); ++x) {
        if (x < 0 || y < 0 || x >= static_cast<std::int32_t>(surface.width()) ||
            y >= static_cast<std::int32_t>(surface.height())) {
          continue;
        }
        const auto index = static_cast<std::size_t>(y) * surface.width() +
                           static_cast<std::size_t>(x);
        if (surface.pixels()[index] != background.bgra()) ++count;
      }
    }
    return count;
  };

  PixelSurface surface{400U, 200U};
  surface.clear(background);
  RasterCanvas canvas{surface, 1.0};
  // The built-in face is the only one available here, so this also pins that the fallback draws.
  canvas.drawTextWrapped(Rect{20.0, 20.0, 160.0, 60.0}, "ALPHA BRAVO CHARLIE DELTA",
                         Color{239, 233, 241, 255}, 12.0, 16.0);
  CHECK(inkIn(surface, Rect{20.0, 20.0, 160.0, 60.0}) > 0U);

  // The second line is below the first: this is the case that held the whole sentence on one clipped
  // line before, so both rows have to carry ink for the sentence to be readable at all.
  CHECK(inkIn(surface, Rect{20.0, 20.0, 160.0, 16.0}) > 0U);
  CHECK(inkIn(surface, Rect{20.0, 36.0, 160.0, 16.0}) > 0U);
  // A line that ended mid word opens with the tail of the word above it. A line that ends at a space
  // opens with a whole word, so its ink reaches the left edge of the box.
  CHECK(inkIn(surface, Rect{20.0, 36.0, 24.0, 16.0}) > 0U);
}

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace {

using namespace seam;
using native_ui::RenderStatusState;
using native_ui::VoiceIdentityState;
using native_ui::design::CharacterState;
using native_ui::design::CharacterSurface;
using native_ui::design::CharacterSurfaceInput;
using native_ui::design::Contrast;
using native_ui::design::DesignMode;
using native_ui::design::DesignPreferences;
using native_ui::design::SingShell;
using native_ui::design::StageFade;
using native_ui::design::StageInput;

constexpr std::array<std::string_view, 6> kStateNames{
    "neutral", "focused", "rendering", "complete", "warning", "error"};
constexpr std::array<std::string_view, 6> kMouthNames{
    "closed", "narrow", "nasal", "open", "wide", "round"};

std::chrono::steady_clock::time_point at(double seconds) {
  return std::chrono::steady_clock::time_point{} +
         std::chrono::duration_cast<std::chrono::steady_clock::duration>(
             std::chrono::duration<double>(seconds));
}

// A flat colour per state, so the state a painter chose is identifiable in the pixels it decoded and
// not only in the file name.
void writeSurface(const std::filesystem::path& path, std::uint8_t red) {
  std::filesystem::create_directories(path.parent_path());
  native_ui::PixelSurface surface{4U, 4U};
  surface.clear(native_ui::Color{red, 90U, 120U, 255U});
  CHECK(surface.writePpm(path));
}

// A sprite whose border is one flat colour and whose centre is another: what a placed mouth overlay
// is. The border keys out and the centre is the shape. The border color is deliberately not a PPM
// whitespace byte: a payload whose first sample is 0x0A (or space, tab or CR) is indistinguishable
// from the header's trailing whitespace, which is a real property of the format, not a loader bug.
void writeMouthSurface(const std::filesystem::path& path, std::uint8_t background,
                       std::uint8_t foreground) {
  std::filesystem::create_directories(path.parent_path());
  native_ui::PixelSurface surface{8U, 8U};
  surface.clear(native_ui::Color{background, 10U, 20U, 255U});
  surface.pixels()[4U * 8U + 4U] = native_ui::Color{foreground, 30U, 40U, 255U}.bgra();
  CHECK(surface.writePpm(path));
}

// A package of the requested schema. A status-only package names only its six state assets; a
// performance package declares every mouth shape and, when asked, a normalized placement.
std::filesystem::path writePackage(const std::filesystem::path& root, std::int64_t schemaVersion,
                                   bool withMouths, bool withPlacement) {
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
  manifest.emplace("states", formats::JsonValue{std::move(states)});
  if (withMouths) {
  std::uint8_t background = 40U;
    formats::JsonValue::Object mouths;
    for (const auto name : kMouthNames) {
      const auto relative = "runtime/mouth-" + std::string{name} + ".ppm";
      writeMouthSurface(root / relative, background, static_cast<std::uint8_t>(background + 100U));
      background = static_cast<std::uint8_t>(background + 20U);
      mouths.emplace(std::string{name}, relative);
    }
    manifest.emplace("mouths", formats::JsonValue{std::move(mouths)});
  }
  if (withPlacement) {
    manifest.emplace("mouthPlacement",
                     formats::JsonValue{formats::JsonValue::Object{{"x", 0.44},
                                                                   {"y", 0.16},
                                                                   {"width", 0.1},
                                                                   {"height", 0.08}}});
  }
  std::filesystem::create_directories(root);
  std::ofstream out{root / "manifest.json", std::ios::binary | std::ios::trunc};
  out << formats::stringifyJson(formats::JsonValue{std::move(manifest)});
  out.close();
  return root;
}

CharacterSurfaceInput readyInput() {
  CharacterSurfaceInput input;
  input.voiceIdentity = VoiceIdentityState::Ready;
  input.render = RenderStatusState::Idle;
  return input;
}

bool toastContains(ui::Rect outer, ui::Rect inner) {
  return inner.x >= outer.x - 0.01 && inner.y >= outer.y - 0.01 &&
         inner.right() <= outer.right() + 0.01 && inner.bottom() <= outer.bottom() + 0.01;
}

// Wrapping may move whitespace to a line boundary, but it must not lose any part of a cause or
// damage a UTF-8 character, including an unspaced package identifier or path.
std::string withoutSpacing(std::string_view text) {
  std::string result;
  for (const auto byte : text)
    if (byte != ' ' && byte != '\t' && byte != '\n' && byte != '\r') result.push_back(byte);
  return result;
}

std::vector<native_ui::paint::TextRecord> toastText(
    const native_ui::design::CharacterToast& toast, DesignMode mode, Contrast contrast,
    double scale = 1.0) {
  native_ui::PixelSurface surface{static_cast<std::uint32_t>(1600.0 * scale),
                                 static_cast<std::uint32_t>(900.0 * scale)};
  const auto& tokens = native_ui::design::tokensFor(mode, contrast);
  surface.clear(tokens.color.canvas);
  auto vector = native_ui::paint::makeCanvas(surface, scale);
  CHECK(vector != nullptr);
  native_ui::RasterCanvas raster{surface, scale};
  native_ui::paint::ScopedTextCapture capture;
  native_ui::design::paintCharacterToast({*vector, raster}, tokens, toast, nullptr, nullptr);
  vector->flush();
  return capture.records();
}

}  // namespace

TEST_CASE("each character state maps from a concrete scene state, in one precedence order") {
  using native_ui::design::resolveCharacterState;

  CHECK(resolveCharacterState(readyInput()) == CharacterState::Idle);
  {
    auto input = readyInput();
    input.auditionLevel = 0.4F;
    CHECK(resolveCharacterState(input) == CharacterState::Listening);
  }
  {
    auto input = readyInput();
    input.playing = true;
    input.performing = true;
    CHECK(resolveCharacterState(input) == CharacterState::Singing);
    // Playing with no phrase bound is not singing: an absent snapshot is not a phrase of silence.
    input.performing = false;
    CHECK(resolveCharacterState(input) == CharacterState::Idle);
  }
  {
    auto input = readyInput();
    input.render = RenderStatusState::Rendering;
    CHECK(resolveCharacterState(input) == CharacterState::Rendering);
  }
  {
    auto input = readyInput();
    input.voiceIdentity = VoiceIdentityState::Complete;
    input.completeDwell = true;
    CHECK(resolveCharacterState(input) == CharacterState::Complete);
    // Ready alone is not a completion: there was no transition to observe.
    input.completeDwell = false;
    CHECK(resolveCharacterState(input) == CharacterState::Idle);
  }
  {
    auto warning = readyInput();
    warning.voiceIdentity = VoiceIdentityState::Warning;
    CHECK(resolveCharacterState(warning) == CharacterState::Warning);
    auto staleAudio = readyInput();
    staleAudio.audibleStale = true;
    CHECK(resolveCharacterState(staleAudio) == CharacterState::Warning);
    auto staleRender = readyInput();
    staleRender.render = RenderStatusState::Stale;
    CHECK(resolveCharacterState(staleRender) == CharacterState::Warning);
  }
  {
    auto failed = readyInput();
    failed.render = RenderStatusState::Failed;
    CHECK(resolveCharacterState(failed) == CharacterState::Error);
    auto bank = readyInput();
    bank.bankMissing = true;
    CHECK(resolveCharacterState(bank) == CharacterState::Error);
    auto identity = readyInput();
    identity.voiceIdentity = VoiceIdentityState::Error;
    CHECK(resolveCharacterState(identity) == CharacterState::Error);
  }

  // The more specific claim wins, and the order is the one the table states.
  {
    auto staleWhileRendering = readyInput();
    staleWhileRendering.render = RenderStatusState::Rendering;
    staleWhileRendering.audibleStale = true;
    CHECK(resolveCharacterState(staleWhileRendering) == CharacterState::Warning);

    auto failedWhileListening = readyInput();
    failedWhileListening.render = RenderStatusState::Failed;
    failedWhileListening.auditionLevel = 0.9F;
    CHECK(resolveCharacterState(failedWhileListening) == CharacterState::Error);

    auto singingWhileRendering = readyInput();
    singingWhileRendering.render = RenderStatusState::Rendering;
    singingWhileRendering.auditionLevel = 0.9F;
    singingWhileRendering.playing = true;
    singingWhileRendering.performing = true;
    CHECK(resolveCharacterState(singingWhileRendering) == CharacterState::Singing);
    // An audition that is not being sung along with is Listening, even while a render runs.
    singingWhileRendering.playing = false;
    CHECK(resolveCharacterState(singingWhileRendering) == CharacterState::Listening);
  }
}

TEST_CASE("cached singer-ring glow clips to every surface edge like an unclipped ring") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  const auto& tokens = native_ui::design::tokensFor(DesignMode::Emo);
  for (const ui::Rect bounds : {ui::Rect{-12.0, 28.0, 72.0, 72.0},
                               ui::Rect{88.0, 28.0, 72.0, 72.0},
                               ui::Rect{28.0, -12.0, 72.0, 72.0},
                               ui::Rect{28.0, 88.0, 72.0, 72.0}}) {
    native_ui::PixelSurface clipped{128U, 128U};
    native_ui::PixelSurface padded{256U, 256U};
    clipped.clear(native_ui::Color{12U, 10U, 14U, 255U});
    padded.clear(native_ui::Color{12U, 10U, 14U, 255U});
    native_ui::design::RingGlowCache smallGlow, largeGlow;
    auto draw = [&](native_ui::PixelSurface& surface, ui::Rect ring,
                    native_ui::design::RingGlowCache& glow) {
      auto vector = native_ui::paint::makeCanvas(surface, 1.0);
      CHECK(vector != nullptr);
      if (vector == nullptr) return;
      native_ui::RasterCanvas raster{surface, 1.0};
      native_ui::design::SingerRingSpec spec{.bounds = ring,
                                             .state = CharacterState::Singing,
                                             .lit = 0.8,
                                             .glows = &glow};
      static_cast<void>(native_ui::design::paintSingerRingLive({*vector, raster}, tokens, spec));
      vector->flush();
    };
    draw(clipped, bounds, smallGlow);
    draw(padded, {bounds.x + 64.0, bounds.y + 64.0, bounds.width, bounds.height}, largeGlow);
    for (std::size_t y = 0; y < 128U; ++y)
      for (std::size_t x = 0; x < 128U; ++x)
        CHECK(clipped.pixels()[y * 128U + x] == padded.pixels()[(y + 64U) * 256U + x + 64U]);
  }
}

TEST_CASE("a package state selects its own asset, and a status-only or absent package invents none") {
  using native_ui::design::characterArtworkChoice;
  using native_ui::design::characterMouthChoice;
  using native_ui::design::characterPackageState;

  // The shell state vocabulary maps onto the package's six assets, with Listening sharing neutral
  // because the package declares no listening asset of its own.
  CHECK(characterPackageState(CharacterState::Idle) == character::State::Neutral);
  CHECK(characterPackageState(CharacterState::Listening) == character::State::Neutral);
  CHECK(characterPackageState(CharacterState::Singing) == character::State::Focused);
  CHECK(characterPackageState(CharacterState::Rendering) == character::State::Rendering);
  CHECK(characterPackageState(CharacterState::Complete) == character::State::Complete);
  CHECK(characterPackageState(CharacterState::Warning) == character::State::Warning);
  CHECK(characterPackageState(CharacterState::Error) == character::State::Error);

  const auto performance = writePackage(test::support::temporaryDirectory("character-surface-perf"),
                                        2, true, true);
  auto loaded = character::loadPackage(performance);
  CHECK(loaded.hasValue());
  if (loaded) {
    auto package = std::move(loaded).value();
    for (const auto state : {CharacterState::Idle, CharacterState::Listening,
                             CharacterState::Singing, CharacterState::Rendering,
                             CharacterState::Complete, CharacterState::Warning,
                             CharacterState::Error}) {
      const auto choice = characterArtworkChoice(&package, state);
      CHECK(choice.fromPackage);
      CHECK(choice.path.filename() ==
            std::string{character::stateName(characterPackageState(state))} + ".ppm");
    }
    // A performance package declares every shape, so every shape resolves to its own sprite.
    for (const auto shape : {character::MouthShape::Closed, character::MouthShape::Narrow,
                             character::MouthShape::Nasal, character::MouthShape::Open,
                             character::MouthShape::Wide, character::MouthShape::Round}) {
      const auto mouth = characterMouthChoice(&package, shape);
      CHECK(mouth.fromPackage);
      CHECK(mouth.path.filename() ==
            "mouth-" + std::string{character::mouthShapeName(shape)} + ".ppm");
    }
  }

  // A status-only package still answers for its six states and refuses every mouth rather than
  // guessing one.
  const auto statusOnly = writePackage(test::support::temporaryDirectory("character-surface-status"),
                                       1, false, false);
  auto statusLoaded = character::loadPackage(statusOnly);
  CHECK(statusLoaded.hasValue());
  if (statusLoaded) {
    auto package = std::move(statusLoaded).value();
    CHECK(characterArtworkChoice(&package, CharacterState::Warning).fromPackage);
    CHECK(!characterMouthChoice(&package, character::MouthShape::Open).fromPackage);
    CHECK(characterMouthChoice(&package, character::MouthShape::Open).path.empty());
  }

  // No package at all: the look's own portrait answers, and no package path is invented.
  CHECK(!characterArtworkChoice(nullptr, CharacterState::Idle).fromPackage);
  CHECK(characterArtworkChoice(nullptr, CharacterState::Idle).path.empty());
  CHECK(!characterMouthChoice(nullptr, character::MouthShape::Closed).fromPackage);
}

TEST_CASE("the mouth sprite follows the published shape, and a status-only package stays closed") {
  const auto root = writePackage(test::support::temporaryDirectory("character-surface-mouths"), 2,
                                 true, true);
  CharacterSurface surface;
  const auto opened = surface.loadPackage(root);
  CHECK(opened.hasValue());
  if (!opened) return;
  CHECK(surface.declaresPerformance());
  const auto placement = surface.mouthPlacement();
  CHECK(placement.has_value());
  if (placement) {
    CHECK_NEAR(placement->x, 0.44, 1e-9);
    CHECK_NEAR(placement->width, 0.1, 1e-9);
  }
  // Every shape resolves to its own decoded sprite, distinguished by the centre colour the writer
  // chose, so the choice is the shape the performance reported and not a single default sprite.
  const auto centreOf = [](const native_ui::PixelSurface* sprite) {
    return sprite == nullptr ? std::optional<std::uint32_t>{}
                             : std::optional<std::uint32_t>{
                                   sprite->pixels()[4U * sprite->width() + 4U]};
  };
  std::array<std::uint32_t, 6U> centres{};
  std::size_t index = 0U;
  for (const auto shape : {character::MouthShape::Closed, character::MouthShape::Narrow,
                           character::MouthShape::Nasal, character::MouthShape::Open,
                           character::MouthShape::Wide, character::MouthShape::Round}) {
    const auto centre = centreOf(surface.mouth(shape));
    CHECK(centre.has_value());
    if (centre) centres[index] = *centre;
    ++index;
  }
  // Every shape is a different sprite, which is what makes the choice the performance's shape rather
  // than one shared default.
  for (std::size_t i = 0U; i < centres.size(); ++i)
    for (std::size_t j = i + 1U; j < centres.size(); ++j) CHECK(centres[i] != centres[j]);
  // The keyed background is fully transparent, and the shape's own pixel is not.
  if (const auto* sprite = surface.mouth(character::MouthShape::Open); sprite != nullptr) {
    CHECK((sprite->pixels().front() >> 24U) == 0U);
    CHECK((sprite->pixels()[4U * sprite->width() + 4U] >> 24U) == 255U);
  }

  const auto statusOnly =
      writePackage(test::support::temporaryDirectory("character-surface-status-mouth"), 1, false,
                   false);
  CharacterSurface bare;
  CHECK(bare.loadPackage(statusOnly).hasValue());
  CHECK(!bare.declaresPerformance());
  CHECK(bare.mouth(character::MouthShape::Open) == nullptr);
  CHECK(!bare.mouthPlacement().has_value());
  // The state asset is still there: a status-only package is not an empty one.
  CHECK(bare.portrait(CharacterState::Warning) != nullptr);
  CHECK(bare.packageLoaded() && !bare.declaresPerformance());
}

TEST_CASE("a refused package is absent rather than half-loaded, and keeps its reason") {
  const auto root = writePackage(test::support::temporaryDirectory("character-surface-refused"), 1,
                                 false, false);
  auto parsed =
      formats::parseJson(core::readTextFileLimited(root / "manifest.json", 65536U).value());
  CHECK(parsed.hasValue());
  parsed.value().asObject()["schemaVersion"] = formats::JsonValue{std::int64_t{99}};
  {
    std::ofstream out{root / "manifest.json", std::ios::binary | std::ios::trunc};
    out << formats::stringifyJson(parsed.value());
  }
  CharacterSurface surface;
  const auto refused = surface.loadPackage(root);
  CHECK(!refused.hasValue());
  CHECK(!surface.packageLoaded());
  CHECK(surface.package() == nullptr);
  CHECK(!surface.packageError().empty());
  // Nothing is drawn from a package the loader refused, at any state.
  for (const auto state : {CharacterState::Idle, CharacterState::Singing, CharacterState::Error})
    CHECK(surface.portrait(state) == nullptr);
  CHECK(surface.mouth(character::MouthShape::Closed) == nullptr);
}

TEST_CASE("the Stage is off in High Contrast, with an expanded lane and outside the full rack") {
  using native_ui::design::kStageHeightFraction;
  using native_ui::design::kStageOpacityCrowded;
  using native_ui::design::kStageOpacityRest;
  using native_ui::design::kStageAnchorInset;
  using native_ui::design::resolveStage;

  const ui::Rect grid{80.0, 172.0, 1040.0, 504.0};
  StageInput input;
  input.fullRack = true;
  input.grid = grid;
  // A 362x1152 stage figure: the aspect the look publishes.
  const auto aspect = 362.0 / 1152.0;
  const auto shown = resolveStage(input, aspect);
  CHECK(shown.shown);
  CHECK_NEAR(shown.bounds.height, grid.height * kStageHeightFraction, 1e-9);
  CHECK_NEAR(shown.bounds.width, shown.bounds.height * aspect, 1e-9);
  // Bottom-right anchored inside the roll, held off the edges by the anchor inset.
  CHECK_NEAR(shown.bounds.bottom(), grid.bottom(), 1e-9);
  CHECK_NEAR(shown.bounds.right(), grid.right() - kStageAnchorInset, 1e-9);
  CHECK(shown.bounds.x >= grid.x && shown.bounds.y >= grid.y);
  CHECK_NEAR(shown.targetOpacity, kStageOpacityRest, 1e-9);
  // The pointer over the figure, or a visible note sharing its bounds, dims it.
  auto pointer = input;
  pointer.pointerInside = true;
  CHECK_NEAR(resolveStage(pointer, aspect).targetOpacity, kStageOpacityCrowded, 1e-9);
  auto note = input;
  note.noteIntersects = true;
  CHECK_NEAR(resolveStage(note, aspect).targetOpacity, kStageOpacityCrowded, 1e-9);

  // Each exclusion turns it off entirely, at the same rectangle the layout would have given it.
  auto contrast = input;
  contrast.highContrast = true;
  CHECK(!resolveStage(contrast, aspect).shown);
  auto lane = input;
  lane.laneExpanded = true;
  CHECK(!resolveStage(lane, aspect).shown);
  auto compact = input;
  compact.fullRack = false;  // the rail and the drawer
  CHECK(!resolveStage(compact, aspect).shown);
  // The empty project's splash already shows her seated, so the standing figure stays off.
  auto splash = input;
  splash.splashShown = true;
  CHECK(!resolveStage(splash, aspect).shown);
  // A roll too short to hold a figure behind the notes keeps it off rather than drawing a smear.
  auto shortRoll = input;
  shortRoll.grid = ui::Rect{80.0, 172.0, 1040.0, 120.0};
  CHECK(!resolveStage(shortRoll, aspect).shown);
  // A roll too narrow to hold the figure and its inset keeps it off rather than clipping her.
  auto narrowRoll = input;
  narrowRoll.grid = ui::Rect{80.0, 172.0, 100.0, 400.0};
  CHECK(!resolveStage(narrowRoll, aspect).shown);
  // No artwork, no aspect: nothing to draw.
  CHECK(!resolveStage(input, 0.0).shown);

  // The figure stays inside the roll it is drawn into, so it can never cover the lane, the rack or the
  // status bar, and it is inside the grid the notes and the pointer routing already own.
  CHECK(shown.bounds.intersects(grid));
  CHECK(!shown.bounds.intersects(ui::Rect{grid.x, grid.bottom(), grid.width, 148.0}));
}

TEST_CASE("the Stage fade settles on its target, instantly under Reduce Motion") {
  using native_ui::design::kStageOpacityCrowded;
  using native_ui::design::kStageOpacityRest;
  using native_ui::design::kStageFadeSeconds;
  using native_ui::design::StagePlacement;

  StagePlacement rest;
  rest.shown = true;
  rest.targetOpacity = kStageOpacityRest;
  StagePlacement crowded;
  crowded.shown = true;
  crowded.targetOpacity = kStageOpacityCrowded;
  StagePlacement hidden;

  StageFade fade;
  // The frame the target changes on is the fade's first frame, so it still shows the old opacity; the
  // fade then runs over the next 180 ms.
  CHECK_NEAR(fade.advance(rest, at(0.0), false), kStageOpacityRest, 1e-9);
  CHECK(!fade.fading());
  CHECK_NEAR(fade.advance(crowded, at(1.0), false), kStageOpacityRest, 1e-9);
  CHECK(fade.fading());
  const auto midway = fade.advance(crowded, at(1.0 + kStageFadeSeconds * 0.5), false);
  CHECK(midway < kStageOpacityRest && midway > kStageOpacityCrowded);
  CHECK(fade.fading());
  CHECK_NEAR(fade.advance(crowded, at(1.0 + kStageFadeSeconds), false), kStageOpacityCrowded, 1e-9);
  CHECK(!fade.fading());
  // Leaving the roll drops it to nothing; returning fades back up.
  CHECK_NEAR(fade.advance(hidden, at(4.0), false), 0.0, 1e-9);
  CHECK_NEAR(fade.advance(rest, at(4.0), false), 0.0, 1e-9);
  CHECK_NEAR(fade.advance(rest, at(4.0 + kStageFadeSeconds), false), kStageOpacityRest, 1e-9);

  // Reduce Motion applies the change at once, and the settled opacity is the same one.
  StageFade reduced;
  CHECK_NEAR(reduced.advance(crowded, at(0.0), true), kStageOpacityCrowded, 1e-9);
  CHECK(!reduced.fading());
  CHECK_NEAR(reduced.advance(rest, at(0.001), true), kStageOpacityRest, 1e-9);
  CHECK(!reduced.fading());
}

TEST_CASE("the blink is seeded in [4, 7] s, and Reduce Motion advances nothing") {
  using native_ui::design::CharacterAnimator;

  // The interval is reproducible from the seed, so a capture and a re-run agree.
  CharacterAnimator first{1234U};
  CharacterAnimator second{1234U};
  CharacterAnimator other{99U};
  CHECK_NEAR(first.blinkIntervalSeconds(), second.blinkIntervalSeconds(), 1e-12);
  // Different seeds give different schedules, and every one stays inside the documented band: a
  // generator that returned one constant would satisfy the band while making the seed a lie.
  std::vector<double> intervals;
  for (std::uint64_t seed = 1U; seed <= 64U; ++seed) {
    CharacterAnimator animator{seed};
    const auto interval = animator.blinkIntervalSeconds();
    CHECK(interval >= 4.0 && interval <= 7.0);
    intervals.push_back(interval);
  }
  std::sort(intervals.begin(), intervals.end());
  CHECK(intervals.back() - intervals.front() > 1.0);

  // Idle breathes at 0.25 Hz within 2 points, and reports a change only when one of its quantities
  // actually moved. At the instant the sine crosses zero the drift is at rest, so the first frame
  // reports no motion: a caller asking "did anything move" gets an honest no, not a busy loop.
  CharacterAnimator idle{7U};
  CHECK(!idle.advance(CharacterState::Idle, at(0.0), false));
  const auto atRest = idle.motion();
  CHECK_NEAR(atRest.breath, 0.0, 1e-12);
  // The same instant again is still no change; a quarter period later the drift has moved.
  CHECK(idle.advance(CharacterState::Idle, at(0.0), false) == false);
  const auto quarterPeriod = 1.0 / 0.25 * 0.25;  // a quarter of the breathing period
  CHECK(idle.advance(CharacterState::Idle, at(quarterPeriod), false));
  const auto drifted = idle.motion();
  CHECK(std::abs(drifted.breath - atRest.breath) > 1e-6);
  for (double t = 0.0; t < 8.0; t += 0.05) {
    CharacterAnimator breathe{3U};
    breathe.advance(CharacterState::Idle, at(t), false);
    CHECK(std::abs(breathe.motion().breath) <= CharacterAnimator::kBreathAmplitude + 1e-9);
  }

  // Blink: the lid closes and opens across the blink window and the interval is redrawn after it.
  CharacterAnimator blink{11U};
  blink.advance(CharacterState::Idle, at(0.0), false);
  const auto interval = blink.blinkIntervalSeconds();
  CHECK(blink.secondsUntilBlink(at(0.0)) > 0.0);
  const auto mid = at(interval + CharacterAnimator::kBlinkSeconds * 0.5);
  CHECK(blink.advance(CharacterState::Idle, mid, false));
  CHECK(blink.motion().blink > 0.9);  // the peak of the blink
  // Past the blink window the lid is open again and the next interval is the one pending.
  blink.advance(CharacterState::Idle, at(interval + CharacterAnimator::kBlinkSeconds * 2.0), false);
  CHECK_NEAR(blink.motion().blink, 0.0, 1e-9);
  CHECK(blink.secondsUntilBlink(at(interval + CharacterAnimator::kBlinkSeconds * 2.0)) > 3.0);
  // The same instant twice reports no change: nothing painted moved between the two.
  const auto repeated = at(interval + CharacterAnimator::kBlinkSeconds * 2.0);
  CHECK(!blink.advance(CharacterState::Idle, repeated, false));

  // Reduce Motion advances nothing at all, whatever the state, and leaves no motion behind.
  for (const auto state : {CharacterState::Idle, CharacterState::Singing,
                           CharacterState::Rendering}) {
    CharacterAnimator animator{5U};
    CHECK(!animator.advance(state, at(0.0), true));
    CHECK(!animator.advance(state, at(12.0), true));
    CHECK_NEAR(animator.motion().blink, 0.0, 1e-12);
    CHECK_NEAR(animator.motion().breath, 0.0, 1e-12);
    CHECK_NEAR(animator.motion().spinner, 0.0, 1e-12);
    CHECK(!animator.moving());
  }
  // A held pose animates nothing either, so a caller schedules no frame for it.
  for (const auto state : {CharacterState::Listening, CharacterState::Complete,
                           CharacterState::Warning, CharacterState::Error}) {
    CharacterAnimator animator{5U};
    CHECK(!animator.advance(state, at(0.0), false));
    CHECK(!animator.moving());
    CHECK(!native_ui::design::characterStateAnimates(state));
  }
  CHECK(native_ui::design::characterStateAnimates(CharacterState::Idle));
  CHECK(native_ui::design::characterStateAnimates(CharacterState::Singing));
  CHECK(native_ui::design::characterStateAnimates(CharacterState::Rendering));

  // The render spinner turns while rendering and does not exist otherwise.
  CharacterAnimator spinner{5U};
  CHECK(!spinner.advance(CharacterState::Rendering, at(0.0), false));
  CharacterAnimator spinnerLater{5U};
  CHECK(spinnerLater.advance(CharacterState::Rendering, at(0.3), false));
  CHECK(spinnerLater.motion().spinner != spinner.motion().spinner);
  CHECK(spinnerLater.moving());
  CharacterAnimator idleNow{5U};
  idleNow.advance(CharacterState::Idle, at(0.3), false);
  CHECK_NEAR(idleNow.motion().spinner, 0.0, 1e-12);
}

TEST_CASE("an animator asks for the next frame at the pace of its breath, and at once for a blink or the spinner") {
  using native_ui::design::CharacterAnimator;
  using Duration = std::chrono::steady_clock::duration;
  const auto breath = std::chrono::duration_cast<Duration>(
      std::chrono::duration<double>(CharacterAnimator::kBreathFrameSeconds));
  const auto immediately = Duration::zero();

  // Nothing moves in a held pose or under Reduce Motion, so no frame is asked for; an animator that
  // has not been advanced has not been asked to move.
  {
    CharacterAnimator animator{5U};
    CHECK(!animator.nextFrameDelay(at(0.0)).has_value());
    for (const auto state : {CharacterState::Listening, CharacterState::Complete,
                             CharacterState::Warning, CharacterState::Error}) {
      animator.advance(state, at(1.0), false);
      CHECK(!animator.nextFrameDelay(at(1.0)).has_value());
    }
    for (const auto state : {CharacterState::Idle, CharacterState::Singing,
                             CharacterState::Rendering}) {
      animator.advance(state, at(1.0), true);
      CHECK(!animator.nextFrameDelay(at(1.0)).has_value());
    }
  }

  // The render spinner turns at the pace of the frames.
  {
    CharacterAnimator animator{5U};
    animator.advance(CharacterState::Rendering, at(1.0), false);
    CHECK(animator.nextFrameDelay(at(1.0)) == immediately);
  }

  // A singer between blinks breathes: a frame every kBreathFrameSeconds, and sooner only for a blink
  // that is about to begin.
  CharacterAnimator idle{11U};
  idle.advance(CharacterState::Idle, at(0.0), false);
  const auto interval = idle.blinkIntervalSeconds();
  CHECK(idle.nextFrameDelay(at(0.0)) == breath);
  idle.advance(CharacterState::Idle, at(1.0), false);
  CHECK(idle.nextFrameDelay(at(1.0)) == breath);
  const auto beforeBlink = at(interval - 0.04);
  idle.advance(CharacterState::Idle, beforeBlink, false);
  const auto soon = idle.nextFrameDelay(beforeBlink);
  CHECK(soon.has_value());
  if (soon.has_value()) {
    CHECK(*soon > immediately);
    CHECK(*soon < breath);
  }
  // From the blink's first instant to its last, each frame is asked for at once.
  for (const auto into : {0.001, 0.03, 0.06, 0.09, 0.119}) {
    const auto now = at(interval + into);
    idle.advance(CharacterState::Idle, now, false);
    CHECK(idle.nextFrameDelay(now) == immediately);
  }
  // Once it is over, the next blink is seconds away and the breath sets the pace again.
  const auto after = at(interval + CharacterAnimator::kBlinkSeconds + 0.01);
  idle.advance(CharacterState::Idle, after, false);
  CHECK(idle.nextFrameDelay(after) == breath);

  // A singer who sings breathes and does not blink, so no blink is to be followed: not by one that
  // was never idle, and not by one that was idle and had a blink scheduled.
  CharacterAnimator singing{11U};
  singing.advance(CharacterState::Singing, at(0.0), false);
  CHECK(singing.nextFrameDelay(at(0.0)) == breath);
  CharacterAnimator wasIdle{11U};
  wasIdle.advance(CharacterState::Idle, at(0.0), false);
  wasIdle.advance(CharacterState::Singing, at(interval + 0.05), false);
  CHECK(wasIdle.nextFrameDelay(at(interval + 0.05)) == breath);
}

TEST_CASE("the empty-project line appears only for a region that genuinely has no notes") {
  using native_ui::design::emptyProjectPrompt;
  using native_ui::design::kEmptyProjectPrompt;

  CHECK(!emptyProjectPrompt(1U).has_value());
  CHECK(!emptyProjectPrompt(4096U).has_value());
  const auto prompt = emptyProjectPrompt(0U);
  CHECK(prompt.has_value());
  if (prompt) CHECK(*prompt == std::string_view{native_ui::design::tr(kEmptyProjectPrompt)});
}

TEST_CASE("the error toast appears for a failed render and a missing voicebank, naming the reason") {
  using native_ui::design::characterErrorToast;
  using native_ui::design::kStageOpacityRest;
  using native_ui::design::solveSingLayout;


  const auto layout = solveSingLayout(1600.0, 900.0);
  const std::string reason = "Project has no audible rendered tracks";

  // A healthy project has no toast at all.
  CHECK(!characterErrorToast(layout, readyInput(), reason).has_value());
  // Warning and stale are not errors: the status line carries them without a toast.
  auto warning = readyInput();
  warning.audibleStale = true;
  CHECK(!characterErrorToast(layout, warning, reason).has_value());

  auto failed = readyInput();
  failed.render = RenderStatusState::Failed;
  const auto failedToast = characterErrorToast(layout, failed, reason);
  CHECK(failedToast.has_value());
  if (failedToast) {
    CHECK(failedToast->title == "Render did not complete");
    CHECK(failedToast->reason == reason);
    // A 40-point pose crop, above the status bar, clear of it.
    CHECK_NEAR(failedToast->pose.width, 40.0, 1e-9);
    CHECK_NEAR(failedToast->pose.height, 40.0, 1e-9);
    CHECK(failedToast->bounds.bottom() <= layout.status.y);
    CHECK(failedToast->bounds.x >= layout.rackArea.x - layout.rackArea.x);
  }

  auto bank = readyInput();
  bank.bankMissing = true;
  const auto bankToast = characterErrorToast(layout, bank, "BANK_MISSING");
  CHECK(bankToast.has_value());
  if (bankToast) {
    CHECK(bankToast->title == "Voicebank needs attention");
    CHECK(bankToast->reason == "BANK_MISSING");
  }
  // The toast never covers the rack, where the SINGER card's recovery action lives.
  if (failedToast) CHECK(!failedToast->bounds.intersects(layout.rackArea));
  // The pose is a head crop, not the whole figure: the source is the frame's own square, and the
  // destination is that section 8.4 rectangle.
  if (failedToast) {
    CHECK_NEAR(failedToast->pose.x - failedToast->bounds.x, 12.0, 1e-9);
    CHECK(failedToast->pose.width == failedToast->pose.height);
  }
  static_cast<void>(kStageOpacityRest);
}

TEST_CASE("error toast cause rows use the available lane without breaking compact stacking") {
  using native_ui::design::characterErrorToast;
  using native_ui::design::solveSingLayout;
  auto failed = readyInput();
  failed.render = RenderStatusState::Failed;
  for (const auto width : {480.0, 720.0, 1100.0, 1600.0}) {
    const auto height = width < 1100.0 ? 480.0 : 900.0;
    const auto layout = solveSingLayout(width, height);
    const ui::Rect diagnostics{layout.status.x, layout.status.y - 34.0, 280.0, 28.0};
    for (const auto stacked : {false, true}) {
      const auto toast = characterErrorToast(
          layout, failed, "Voicebank has multiple styles; select a style before rendering",
          stacked ? std::optional<ui::Rect>{diagnostics} : std::nullopt);
      // Compact lanes retain the one-row card when it fits and omit the second toast when the
      // diagnostics row leaves less than that minimum, just as the existing recovery flow does.
      CHECK(toast.has_value() == !(stacked && height < 720.0));
      if (!toast) continue;
      CHECK(toastContains(layout.lane, toast->bounds));
      CHECK(toastContains(toast->bounds, toast->pose));
      CHECK(!toast->bounds.intersects(layout.laneTabs));
      CHECK(!toast->bounds.intersects(layout.status));
      CHECK(!toast->bounds.intersects(layout.rackArea));
      CHECK_NEAR(toast->pose.width, 40.0, 1e-9);
      CHECK_NEAR(toast->pose.height, 40.0, 1e-9);
      CHECK_NEAR(toast->bounds.height, height < 720.0 ? 56.0 : stacked ? 74.0 : 92.0, 1e-9);
      if (stacked) CHECK(toast->bounds.bottom() <= diagnostics.y - 8.0);
    }
  }
  // The public layout helper also accepts a narrower lane: its minimum still leaves a real text
  // column beside the fixed head crop, and a lane one point smaller cannot produce that card.
  auto narrow = solveSingLayout(1600.0, 900.0);
  narrow.lane.width = 192.0;
  const auto minimum = characterErrorToast(narrow, failed, "BANK_MISSING");
  CHECK(minimum.has_value());
  CHECK_NEAR(minimum->bounds.width, 160.0, 1e-9);
  CHECK(toastContains(narrow.lane, minimum->bounds));
  narrow.lane.width = 191.0;
  CHECK(!characterErrorToast(narrow, failed, "BANK_MISSING").has_value());
}

TEST_CASE("a measured error toast holds only the rows its wrapped reason needs") {
  using native_ui::design::CharacterToastMeasure;
  using native_ui::design::characterErrorToast;
  auto failed = readyInput();
  failed.render = RenderStatusState::Failed;
  const auto layout = native_ui::design::solveSingLayout(1600.0, 900.0);
  // A fixed advance keeps the row count independent of the installed fonts.
  const CharacterToastMeasure measure{
      native_ui::design::characterToastReasonStyle(
          native_ui::design::tokensFor(DesignMode::Emo, Contrast::Standard)),
      [](std::string_view text, const native_ui::paint::TextStyle&) {
        return 7.0 * static_cast<double>(text.size());
      }};
  const std::string shortReason = "Voicebank root is unavailable";
  std::string longReason = "Voicebank root is unavailable:";
  for (std::size_t i = 0U; i < 40U; ++i) longReason += " segment";
  const ui::Rect diagnostics{layout.status.x, layout.status.y - 34.0, 280.0, 28.0};
  for (const auto stacked : {false, true}) {
    const auto below = stacked ? std::optional<ui::Rect>{diagnostics} : std::nullopt;
    const auto reserved = characterErrorToast(layout, failed, shortReason, below);
    const auto one = characterErrorToast(layout, failed, shortReason, below, &measure);
    const auto many = characterErrorToast(layout, failed, longReason, below, &measure);
    CHECK(reserved.has_value());
    CHECK(one.has_value());
    CHECK(many.has_value());
    if (!reserved || !one || !many) continue;
    // A short cause gets one row on the same floor, with the head crop centred on it.
    CHECK_NEAR(one->bounds.height, 56.0, 1e-9);
    CHECK_NEAR(one->bounds.bottom(), reserved->bounds.bottom(), 1e-9);
    CHECK_NEAR(one->pose.y - one->bounds.y, 8.0, 1e-9);
    CHECK(one->reason == shortReason);
    // A long cause takes every row that fits and never more than the reserved card.
    CHECK_NEAR(many->bounds.height, reserved->bounds.height, 1e-9);
    CHECK(many->bounds.height > one->bounds.height);
  }
  // An empty cause keeps the one-row minimum.
  const auto empty = characterErrorToast(layout, failed, "", std::nullopt, &measure);
  CHECK(empty.has_value());
  if (empty) CHECK_NEAR(empty->bounds.height, 56.0, 1e-9);
}

TEST_CASE("error toast paints complete long ASCII and CJK causes as measured body text") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  auto failed = readyInput();
  failed.render = RenderStatusState::Failed;
  const auto layout = native_ui::design::solveSingLayout(1600.0, 900.0);
  // The actual resource-error wording with deliberately long fixture paths. These remain the
  // supplied causes, including non-ASCII path components; the painter must not substitute prose.
  const std::string ascii =
      "Voicebank root is unavailable: /Users/test/Music/SEAM/Voicebanks/"
      "official.voice.01.phase2.synthetic/versions/0.2.0-dev/sources/"
      "voicebank-with-a-long-package-path-for-the-render-failure-fixture";
  std::string cjk = "Voicebank root is unavailable: /음성자료/";
  for (std::size_t i = 0U; i < 12U; ++i) cjk += "가나다音声";
  cjk += "/source.wav";
  const std::string renderFailure =
      "Render did not complete — Project has no audible rendered tracks: "
      "No voicebank unit covers the sound \"a\" of the lyric \"あ\" at bar 1, beat 1";
  for (const auto& reason : {ascii, cjk, renderFailure}) {
    // The captured production failure must also fit above an existing diagnostics toast, where
    // the canonical lane holds two cause rows. The longer path fixtures use all three rows.
    const auto stacked = reason == renderFailure;
    const auto diagnostics = stacked
        ? std::optional<ui::Rect>{{layout.status.x, layout.status.y - 34.0, 380.0, 28.0}}
        : std::nullopt;
    const auto toast = native_ui::design::characterErrorToast(layout, failed, reason, diagnostics);
    CHECK(toast.has_value());
    CHECK(toast->reason == reason);
    for (const auto mode : {DesignMode::Emo, DesignMode::Scene}) {
      for (const auto contrast : {Contrast::Standard, Contrast::High}) {
        for (const auto scale : {1.0, 2.0}) {
          const auto lines = toastText(*toast, mode, contrast, scale);
          CHECK(lines.size() >= (stacked ? 2U : 3U));
          CHECK(lines.size() <= (stacked ? 3U : 4U));
          CHECK(lines.front().text == toast->title);
          std::string paintedReason;
          for (std::size_t i = 0U; i < lines.size(); ++i) {
            const auto& line = lines[i];
            CHECK(!line.elided);
            CHECK(line.naturalWidth <= line.bounds.width + 0.01);
            CHECK(toastContains(toast->bounds, line.bounds));
            CHECK(toastContains(toast->bounds, line.ink));
            CHECK(toastContains(line.clip, line.ink));
            CHECK(text::decodeUtf8Strict(line.text).hasValue());
            if (i > 0U) {
              CHECK(line.bounds.y >= lines[i - 1U].bounds.bottom());
              paintedReason += line.text;
            }
          }
          CHECK(withoutSpacing(paintedReason) == withoutSpacing(reason));
        }
      }
    }
  }
}

TEST_CASE("error toast bounds long causes and only elides the final cause row at narrow widths") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  auto failed = readyInput();
  failed.render = RenderStatusState::Failed;
  std::string cjk = "Voicebank root is unavailable: /";
  for (std::size_t i = 0U; i < 96U; ++i) cjk += "音声가";
  const std::string ascii = "Voicebank root is unavailable: /" + std::string(400U, 'W');
  for (const auto width : {160.0, 240.0, 356.0, 720.0}) {
    auto layout = native_ui::design::solveSingLayout(1600.0, 900.0);
    layout.lane.width = width + 32.0;
    const ui::Rect diagnostics{layout.status.x, layout.status.y - 34.0, 120.0, 28.0};
    for (const auto stacked : {false, true}) {
      for (const auto& reason : {ascii, cjk}) {
        const auto toast = native_ui::design::characterErrorToast(
            layout, failed, reason, stacked ? std::optional<ui::Rect>{diagnostics} : std::nullopt);
        CHECK(toast.has_value());
        CHECK(toast->reason == reason);
        const auto lines = toastText(*toast, DesignMode::Emo, Contrast::High);
        CHECK(lines.size() == (stacked ? 3U : 4U));
        CHECK(lines.back().elided);
        for (std::size_t i = 0U; i < lines.size(); ++i) {
          const auto& line = lines[i];
          CHECK(text::decodeUtf8Strict(line.text).hasValue());
          CHECK(toastContains(toast->bounds, line.bounds));
          CHECK(toastContains(toast->bounds, line.ink));
          CHECK(toastContains(line.clip, line.ink));
          // A title may also elide at this deliberately minimal width; earlier cause rows fit.
          if (i > 0U && i + 1U < lines.size()) CHECK(!line.elided);
        }
      }
    }
  }
}

TEST_CASE("error toast keeps its head crop and paints High Contrast without a halo") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  auto failed = readyInput();
  failed.render = RenderStatusState::Failed;
  const auto toast = native_ui::design::characterErrorToast(
      native_ui::design::solveSingLayout(1600.0, 900.0), failed, "Voicebank root is unavailable");
  CHECK(toast.has_value());
  native_ui::PixelSurface portrait{20U, 40U};
  constexpr native_ui::Color head{180U, 110U, 80U, 255U};
  portrait.clear(native_ui::Color{20U, 40U, 220U, 255U});
  std::fill_n(portrait.pixels().begin(), 20U * 17U, head.bgra());
  for (const auto mode : {DesignMode::Emo, DesignMode::Scene}) {
    const auto& tokens = native_ui::design::tokensFor(mode, Contrast::High);
    native_ui::PixelSurface surface{1600U, 900U};
    surface.clear(tokens.color.canvas);
    auto vector = native_ui::paint::makeCanvas(surface, 1.0);
    CHECK(vector != nullptr);
    native_ui::RasterCanvas raster{surface, 1.0};
    // No GlowlessCanvas wrapper: the standalone painter honors the contrast token itself.
    native_ui::design::paintCharacterToast({*vector, raster}, tokens, *toast, &portrait, nullptr);
    vector->flush();
    const auto pixel = [&](double x, double y) {
      return surface.pixels()[static_cast<std::size_t>(y) * surface.width() +
                              static_cast<std::size_t>(x)];
    };
    CHECK(pixel(toast->pose.x + 20.0, toast->pose.y + 30.0) == head.bgra());
    CHECK(pixel(toast->bounds.x + 100.0, toast->bounds.bottom() - 4.0) ==
          tokens.color.surfaceRaised.bgra());
    // All drawing stays in the card plus its half-point border; High Contrast adds no halo.
    const ui::Rect painted{toast->bounds.x - 1.0, toast->bounds.y - 1.0,
                           toast->bounds.width + 2.0, toast->bounds.height + 2.0};
    for (std::uint32_t y = 0U; y < surface.height(); ++y) {
      for (std::uint32_t x = 0U; x < surface.width(); ++x) {
        if (painted.contains({static_cast<double>(x) + 0.5, static_cast<double>(y) + 0.5})) continue;
        CHECK(surface.pixels()[static_cast<std::size_t>(y) * surface.width() + x] ==
              tokens.color.canvas.bgra());
      }
    }
  }
}

namespace {

// The repository's own design and character assets, as a development build finds them. The tests run
// from the repository root, so the same relative paths a development host uses are available here.
const std::filesystem::path& designAssetRoot() {
  static const std::filesystem::path root{"assets/ui-design"};
  return root;
}

// A shell over a real controller and session, painting real frames into a surface this fixture owns,
// so the state, the Stage and the ring under test are the ones a painted frame produces.
  struct ShellFixture final {
  application::ProjectFactory factory{9600U};
  domain::TrackId trackId{};
  domain::RegionId regionId{};
  application::EditorSession session;
    native_ui::NativeEditorController controller;
    SingShell shell;
    native_ui::PixelSurface surface{1600U, 900U};
    // Frames the shell asked for, so the repaint policy is testable without a window.
    int repaints{0};
  // The clock the shell animates against, when a test wants it frozen instead of live.
  std::chrono::steady_clock::time_point now{at(10.0)};

  explicit ShellFixture(bool emptyRegion = false, Contrast contrast = Contrast::Standard,
                        DesignMode mode = DesignMode::Emo)
      : session{makeProject(emptyRegion)},
        controller{session, factory, regionId, native_ui::EditorHostCallbacks{}} {
    controller.resize(1600.0, 900.0);
    shell.activate(designAssetRoot(), DesignPreferences{.mode = mode, .contrast = contrast});
    // A frozen clock keeps a frame's animation deterministic, which is what a capture needs and what
    // the plan's injectable UI clock is for. Time is advanced explicitly by the tests that care.
    shell.setUiClock([this] { return now; });
    shell.setRepaintCallback([this] { ++repaints; });
  }

  domain::Project makeProject(bool emptyRegion) {
    auto project = factory.createProject("Character surface");
    // Full: every character surface this file checks is drawn; the modes have their own test.
    project.settings().characterDisplay = domain::CharacterDisplayMode::Full;
    trackId = factory.addVocalTrack(project, "Singer");
    regionId = factory.addRegion(project, trackId, "Phrase", time::Tick{0}, time::Tick{7680});
    if (emptyRegion) return project;
    auto [lyric, note] = factory.makeNote(time::Tick{960}, time::Tick{960}, 72U, U"\u3042",
                                          domain::Language::Japanese);
    auto* region = project.findRegion(regionId);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
    return project;
  }

  bool frame(double width = 1600.0, double height = 900.0) {
    if (!shell.prepareFrame(controller, width, height)) return false;
    surface = native_ui::PixelSurface{static_cast<std::uint32_t>(width),
                                      static_cast<std::uint32_t>(height)};
    native_ui::RasterCanvas canvas{surface, 1.0};
    return shell.paint(canvas, controller, controller.sceneState(), controller.playheadTick());
  }
};

}  // namespace

TEST_CASE("a painted frame keeps the Stage on in the full rack and off at every exclusion") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  if (!std::filesystem::is_directory(designAssetRoot())) return;
  // The full rack shows this look's Stage figure, drawn from the look's own artwork.
  {
    ShellFixture f;
    CHECK(f.shell.assetsLoaded(DesignMode::Emo));
    CHECK(f.frame());
    CHECK(f.shell.lastFrameShowedStage());
    // The painted figure is the resolved rectangle, anchored bottom-right inside the roll, and it is
    // neither a hit target nor published: the grid owns the pointer there and the tree has no node.
    const auto subject = f.shell.lastFrameStageBounds();
    CHECK(subject.has_value());
    if (subject) {
      const auto& grid = f.shell.layout().grid;
      CHECK(subject->x >= grid.x && subject->right() <= grid.right());
      CHECK(subject->y >= grid.y && subject->bottom() <= grid.bottom());
    }
    f.controller.rebuildAccessibilityTree();
    f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
    bool published = false;
    const auto walk = [&](const native_ui::SemanticNode& node, const auto& self) -> void {
      if (node.name.find("Stage") != std::string::npos ||
          node.description.find("Stage") != std::string::npos)
        published = true;
      for (const auto& child : node.children) self(child, self);
    };
    walk(f.shell.accessibilityTree().root(), walk);
    CHECK(!published);
    // The figure's whole rectangle lies in the musical grid, which is the region that owns the
    // pointer there: the figure is never a control, and a press on it is the grid's own command.
    if (subject) {
      CHECK(subject->intersects(f.shell.layout().grid));
      CHECK(!subject->intersects(f.shell.layout().lane));
      CHECK(!subject->intersects(f.shell.layout().rackArea));
      CHECK(!subject->intersects(f.shell.layout().status));
    }
  }
  // High Contrast turns it off, with the same layout and the same artwork loaded.
  {
    ShellFixture f{false, Contrast::High};
    CHECK(f.shell.assetsLoaded(DesignMode::Emo));
    CHECK(f.frame());
    CHECK(!f.shell.lastFrameShowedStage());
  }
  // An expanded technical lane turns it off.
  {
    ShellFixture f;
    CHECK(f.frame());
    CHECK(f.shell.lastFrameShowedStage());
    auto presentation = f.session.project().settings().technicalLanes[0];
    presentation.mode = domain::TechnicalLaneMode::Expanded;
    CHECK(f.session
              .execute(std::make_unique<application::SetTechnicalLanePresentationCommand>(
                  domain::TechnicalLane::Phoneme, presentation))
              .hasValue());
    CHECK(f.frame());
    CHECK(!f.shell.lastFrameShowedStage());
  }
  // The compact presentations keep it off: the portrait lives in the inspector there.
  {
    ShellFixture f;
    CHECK(f.frame(720.0, 480.0));
    CHECK(f.shell.layout().rack == native_ui::design::RackPresentation::Drawer);
    CHECK(!f.shell.lastFrameShowedStage());
    CHECK(f.frame(1000.0, 700.0));
    CHECK(f.shell.layout().rack == native_ui::design::RackPresentation::Rail);
    CHECK(!f.shell.lastFrameShowedStage());
  }
}

TEST_CASE("a painted frame shows the empty-project line only when the region has no notes") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  if (!std::filesystem::is_directory(designAssetRoot())) return;
  // The pixels of the roll differ between the two: the pose and the line are painted into it for an
  // empty region and are absent for a region with a note.
  const auto gridChecksum = [](const ShellFixture& f) {
    const auto& grid = f.shell.layout().grid;
    std::uint64_t hash = 1469598103934665603ULL;
    for (auto y = static_cast<std::uint32_t>(grid.y);
         y < static_cast<std::uint32_t>(grid.bottom()) && y < f.surface.height(); ++y) {
      for (auto x = static_cast<std::uint32_t>(grid.x);
           x < static_cast<std::uint32_t>(grid.right()) && x < f.surface.width(); ++x) {
        hash ^= f.surface.pixels()[static_cast<std::size_t>(y) * f.surface.width() + x];
        hash *= 1099511628211ULL;
      }
    }
    return hash;
  };
  ShellFixture occupied;
  CHECK(occupied.frame());
  const auto withNote = gridChecksum(occupied);
  // A note in the region means the prompt is not shown at all, not merely covered by the pose.
  CHECK(occupied.controller.pianoRoll().noteCount() > 0U);
  ShellFixture empty{true};
  CHECK(empty.frame());
  const auto withoutNote = gridChecksum(empty);
  CHECK(withNote != withoutNote);
  CHECK(empty.controller.pianoRoll().noteCount() == 0U);
  // And the prompt itself is only asked for by a region that genuinely has no notes.
  CHECK(!native_ui::design::emptyProjectPrompt(occupied.controller.pianoRoll().noteCount())
             .has_value());
  CHECK(native_ui::design::emptyProjectPrompt(empty.controller.pianoRoll().noteCount())
            .has_value());
}

TEST_CASE("a rendering frame and a failed frame paint different singer-ring pixels") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  if (!std::filesystem::is_directory(designAssetRoot())) return;
  // The shell loads the repository's own character package beside the design assets, so the ring is
  // drawn from the package's state portraits rather than from the look's fallback.
  {
    ShellFixture probe;
    CHECK(probe.frame());
    CHECK(probe.shell.characterPackageLoaded());
    CHECK(probe.shell.characterPackageError().empty());
  }
  // The ring is the singer's state made visible, so a ring around a live render cannot be the same
  // pixels as a ring around a failure: one is the look's accent, the other is red.
  const auto ringChecksum = [](const ShellFixture& f) {
    const auto& ring = f.shell.layout().portraitRing;
    std::uint64_t hash = 1469598103934665603ULL;
    constexpr std::size_t kTicks = 64U;
    for (std::size_t i = 0U; i < kTicks; ++i) {
      const auto angle = -std::numbers::pi * 0.5 +
                         static_cast<double>(i) * 2.0 * std::numbers::pi / static_cast<double>(kTicks);
      // The middle of each tick, where the state's colour is painted.
      const auto x = static_cast<std::uint32_t>(
          ring.x + ring.width * 0.5 + std::cos(angle) * (ring.width * 0.5 - 4.0));
      const auto y = static_cast<std::uint32_t>(
          ring.y + ring.height * 0.5 + std::sin(angle) * (ring.height * 0.5 - 4.0));
      if (x >= f.surface.width() || y >= f.surface.height()) continue;
      hash ^= f.surface.pixels()[static_cast<std::size_t>(y) * f.surface.width() + x];
      hash *= 1099511628211ULL;
    }
    return hash;
  };

  ShellFixture rendering;
  native_ui::RenderStatusView status;
  status.state = RenderStatusState::Rendering;
  status.fraction = 0.5;
  rendering.controller.setRenderStatus(status);
  CHECK(rendering.frame());
  CHECK(rendering.shell.characterState() == CharacterState::Rendering);
  const auto lit = ringChecksum(rendering);

  ShellFixture failed;
  native_ui::RenderStatusView broken;
  broken.state = RenderStatusState::Failed;
  broken.diagnostic = "Project has no audible rendered tracks";
  failed.controller.setRenderStatus(broken);
  CHECK(failed.frame());
  CHECK(failed.shell.characterState() == CharacterState::Error);
  const auto alarming = ringChecksum(failed);
  CHECK(lit != alarming);
}

TEST_CASE("a singing frame draws the published mouth shape onto the ring portrait") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  if (!std::filesystem::is_directory(designAssetRoot())) return;
  // Singing takes the mouth from the published performance and the sprite from the package. Two
  // different shapes must not produce the same pixels on the face, or the mouth is not following the
  // phrase at all.
  const auto singingChecksum = [](character::MouthShape shape, bool reduceMotion = false) {
    ShellFixture f;
    if (reduceMotion) f.shell.setReduceMotion(true);
    f.controller.setPlaying(true);
    auto view = native_ui::EditorSceneState::CharacterPerformanceView{};
    view.mouth = shape;
    view.energy = 0.7F;
    view.performing = true;
    f.controller.setCharacterPerformance(view);
    CHECK(f.frame());
    CHECK(f.shell.characterState() == CharacterState::Singing);
    const auto& ring = f.shell.layout().portraitRing;
    // The window the mouth placement occupies on the ring portrait, sampled coarsely.
    std::uint64_t hash = 1469598103934665603ULL;
    for (auto y = static_cast<std::uint32_t>(ring.y + ring.height * 0.30);
         y < static_cast<std::uint32_t>(ring.y + ring.height * 0.44); ++y) {
      for (auto x = static_cast<std::uint32_t>(ring.x + ring.width * 0.35);
           x < static_cast<std::uint32_t>(ring.x + ring.width * 0.65); ++x) {
        if (x >= f.surface.width() || y >= f.surface.height()) continue;
        hash ^= f.surface.pixels()[static_cast<std::size_t>(y) * f.surface.width() + x];
        hash *= 1099511628211ULL;
      }
    }
    return hash;
  };
  const auto closed = singingChecksum(character::MouthShape::Closed);
  const auto wide = singingChecksum(character::MouthShape::Wide);
  CHECK(closed != wide);
  CHECK(singingChecksum(character::MouthShape::Closed, true) ==
        singingChecksum(character::MouthShape::Wide, true));
}

TEST_CASE("a committed export shows Complete briefly and returns to Idle") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  if (!std::filesystem::is_directory(designAssetRoot())) return;
  ShellFixture f;
  f.shell.setWorkspace(f.controller, native_ui::design::Workspace::Export);
  authoring::ExportResult receipt;
  receipt.state = authoring::ExportState::Committed;
  receipt.masterPath = "development-master.wav";
  receipt.masterSha256 = "2c2f8f7c";
  f.controller.setLastExport(receipt);
  CHECK(f.frame());
  CHECK(f.shell.characterState() == CharacterState::Complete);
  CHECK(f.repaints > 0);
  f.now += std::chrono::seconds{2};
  CHECK(f.frame());
  CHECK(f.shell.characterState() == CharacterState::Idle);
}

TEST_CASE("an animating frame asks for the next one, and a still frame asks for nothing") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  if (!std::filesystem::is_directory(designAssetRoot())) return;
  // A state with motion (idle breathing, singing, the render spinner) has to keep the frame loop
  // alive, or the drift and the blink would stop after one frame. The breath asks for its next
  // frame through nextFrameDue(), at its own pace; the spinner asks for each at once.
  {
    ShellFixture f;
    CHECK(f.frame());
    f.now = at(11.0);
    CHECK(f.frame());
    f.repaints = 0;
    f.now = at(12.0);
    CHECK(f.frame());
    CHECK(f.shell.characterState() == CharacterState::Idle);
    CHECK(f.repaints == 0);
    CHECK(f.shell.nextFrameDue().has_value());
  }
  {
    ShellFixture f;
    native_ui::RenderStatusView status;
    status.state = RenderStatusState::Rendering;
    status.fraction = 0.4;
    f.controller.setRenderStatus(status);
    CHECK(f.frame());
    f.repaints = 0;
    f.now = at(10.5);
    CHECK(f.frame());
    CHECK(f.shell.characterState() == CharacterState::Rendering);
    CHECK(f.repaints > 0);
  }
  // A held pose animates nothing, so it requests no further frame: the protagonist is still and the
  // window can idle.
  for (const auto state : {CharacterState::Warning, CharacterState::Error}) {
    ShellFixture f;
    native_ui::RenderStatusView status;
    status.state = state == CharacterState::Error ? RenderStatusState::Failed
                                                 : RenderStatusState::Stale;
    f.controller.setRenderStatus(status);
    CHECK(f.frame());
    CHECK(f.shell.characterState() == state);
    CHECK(f.repaints == 0);
    CHECK(!f.shell.nextFrameDue().has_value());
  }
  // Reduce Motion: no motion at all, so no frame is requested, whatever the state. The state itself
  // is unchanged, which is the point: a screen that reduces motion still says what the singer is
  // doing.
  {
    ShellFixture f{false, Contrast::Standard, DesignMode::Emo};
    f.shell.setReduceMotion(true);
    f.repaints = 0;
    native_ui::RenderStatusView status;
    status.state = RenderStatusState::Rendering;
    status.fraction = 0.4;
    f.controller.setRenderStatus(status);
    CHECK(f.frame());
    CHECK(f.shell.characterState() == CharacterState::Rendering);
    CHECK(f.repaints == 0);
    CHECK(!f.shell.nextFrameDue().has_value());
  }
}

TEST_CASE("a missing voicebank's error toast never covers the diagnostics toast or the controls") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  if (!std::filesystem::is_directory(designAssetRoot())) return;
  // BANK_MISSING brings both toasts: the diagnostics toast with its DIAGNOSTICS opener, and the
  // character's error toast. The error toast stacks above the diagnostics row where the lane has
  // room for it and is left out where it has none, so the diagnostic title and its recovery stay
  // visible and reachable at every contract size.
  struct Case final {
    double width;
    double height;
    bool stacked;
  };
  for (const auto& size : {Case{480.0, 320.0, false}, Case{720.0, 480.0, false},
                           Case{1100.0, 720.0, true}, Case{1600.0, 900.0, true}}) {
    ShellFixture f;
    f.controller.setDiagnostics({authoring::Diagnostic{
        .code = "BANK_MISSING",
        .severity = authoring::DiagnosticSeverity::Critical,
        .messageKey = "bank.missing",
        .actions = {authoring::DiagnosticAction::RelinkVoicebank,
                    authoring::DiagnosticAction::ChooseVoicebank}}});
    f.controller.resize(size.width, size.height);
    CHECK(f.frame(size.width, size.height));
    CHECK(f.shell.characterState() == CharacterState::Error);
    f.controller.rebuildAccessibilityTree();
    f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
    std::optional<native_ui::SemanticNode> diagnostics;
    std::optional<native_ui::SemanticNode> opener;
    const auto walk = [&](const native_ui::SemanticNode& node, const auto& self) -> void {
      if (node.id == "shell.diagnostics.toast") diagnostics = node;
      if (node.id == "shell.diagnostics.open") opener = node;
      for (const auto& child : node.children) self(child, self);
    };
    walk(f.shell.accessibilityTree().root(), walk);
    CHECK(diagnostics.has_value());
    if (!diagnostics) continue;
    const auto& layout = f.shell.layout();
    const auto error = f.shell.lastFrameErrorToast();
    CHECK(error.has_value() == size.stacked);
    if (!error) continue;
    CHECK(!error->intersects(diagnostics->bounds));
    if (opener) CHECK(!error->intersects(opener->bounds));
    CHECK(error->bottom() <= diagnostics->bounds.y);
    CHECK(!error->intersects(layout.status));
    CHECK(!error->intersects(layout.laneTabs));
    CHECK(!error->intersects(layout.laneReviewButton));
    CHECK(!error->intersects(layout.tools));
    CHECK(!error->intersects(layout.rackArea));
    CHECK(error->y >= layout.lane.y && error->bottom() <= layout.lane.bottom());
  }
}

TEST_CASE("an idle frame asks for the next one only when a painted surface actually moves") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  if (!std::filesystem::is_directory(designAssetRoot())) return;
  // Two idle frames one second apart under a frozen clock, with the look's artwork and
  // assets/character-01 loaded. No blink falls between them: the first is drawn at least 4 s after
  // the first frame. Only where a surface that carries the motion is on screen do they differ, and
  // only there may a frame ask for the next.
  struct Case final {
    double width;
    double height;
    bool moves;
  };
  for (const auto& size : {Case{1100.0, 700.0, false}, Case{720.0, 480.0, false},
                           Case{1600.0, 900.0, true}}) {
    ShellFixture f;
    CHECK(f.shell.characterPackageLoaded());
    f.controller.resize(size.width, size.height);
    f.now = at(3.0);
    CHECK(f.frame(size.width, size.height));
    CHECK(f.shell.characterState() == CharacterState::Idle);
    const auto first = f.surface.checksum();
    f.repaints = 0;
    f.now = at(4.0);
    CHECK(f.frame(size.width, size.height));
    const auto moved = f.surface.checksum() != first;
    CHECK(moved == size.moves);
    // The rail and the drawer show a still portrait and no header avatar: nothing moved, so the
    // window idles. The full rack's ring breathes, so the loop continues there, a breath at a time.
    CHECK(f.repaints == 0);
    CHECK(f.shell.nextFrameDue().has_value() == size.moves);
  }
  // A render in flight turns only the full rack's ring. The rail's portrait has no spinner, so a
  // compact window asks for nothing while it renders.
  {
    ShellFixture f;
    native_ui::RenderStatusView status;
    status.state = RenderStatusState::Rendering;
    status.fraction = 0.4;
    f.controller.setRenderStatus(status);
    f.controller.resize(1100.0, 700.0);
    CHECK(f.frame(1100.0, 700.0));
    CHECK(f.shell.characterState() == CharacterState::Rendering);
    f.repaints = 0;
    f.now = at(10.5);
    CHECK(f.frame(1100.0, 700.0));
    CHECK(f.repaints == 0);
    CHECK(!f.shell.nextFrameDue().has_value());
  }
  // Reduce Motion asks for nothing even where the ring would breathe.
  {
    ShellFixture f;
    f.shell.setReduceMotion(true);
    CHECK(f.frame());
    f.repaints = 0;
    f.now = at(11.0);
    CHECK(f.frame());
    CHECK(f.shell.characterState() == CharacterState::Idle);
    CHECK(f.repaints == 0);
    CHECK(!f.shell.nextFrameDue().has_value());
  }
}

TEST_CASE("the idle breath asks for its frames through nextFrameDue at its own pace, and a blink asks for each at once") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  if (!std::filesystem::is_directory(designAssetRoot())) return;
  using native_ui::design::CharacterAnimator;
  const auto breath = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
      std::chrono::duration<double>(CharacterAnimator::kBreathFrameSeconds));
  // The shells' animators share the default seed, so a probe advanced at the same first frame knows
  // when the first blink falls.
  CharacterAnimator probe;
  static_cast<void>(probe.advance(CharacterState::Idle, at(10.0), false));
  const auto blinkAt = 10.0 + probe.secondsUntilBlink(at(10.0));

  ShellFixture f;
  CHECK(f.frame());
  CHECK(f.shell.characterState() == CharacterState::Idle);
  // A second on, the fades that the first frames start are over.
  f.now = at(11.0);
  CHECK(f.frame());
  f.repaints = 0;
  f.now = at(12.0);
  CHECK(f.frame());
  // Between blinks a frame does not ask for the next one: the breath moves too little for that. The
  // next is due a breath later, and the frame painted then has the one after it due a breath on.
  CHECK(f.repaints == 0);
  CHECK(f.shell.nextFrameDue() == at(12.0) + breath);
  f.now = at(12.0) + breath;
  CHECK(f.frame());
  CHECK(f.repaints == 0);
  CHECK(f.shell.nextFrameDue() == f.now + breath);

  // A blink that is about to begin is not made to wait for a whole breath.
  f.now = at(blinkAt - 0.04);
  CHECK(f.frame());
  CHECK(f.repaints == 0);
  const auto soon = f.shell.nextFrameDue();
  CHECK(soon.has_value());
  if (soon.has_value()) {
    CHECK(*soon > f.now);
    CHECK(*soon < f.now + breath);
  }
  // From its first instant to its last, a blink asks for the next frame at once.
  for (const auto into : {0.1, 0.4, 0.7, 0.95}) {
    f.repaints = 0;
    f.now = at(blinkAt + CharacterAnimator::kBlinkSeconds * into);
    CHECK(f.frame());
    CHECK(f.repaints > 0);
  }
  // Once it is over, the breath sets the pace again.
  f.repaints = 0;
  f.now = at(blinkAt + CharacterAnimator::kBlinkSeconds + 0.05);
  CHECK(f.frame());
  CHECK(f.repaints == 0);
  CHECK(f.shell.nextFrameDue() == f.now + breath);

  // A frame after which nothing animates leaves nothing due, though the one before it asked: the
  // singer's state has become a held pose (a failed render).
  native_ui::RenderStatusView failed;
  failed.state = RenderStatusState::Failed;
  f.controller.setRenderStatus(failed);
  f.repaints = 0;
  f.now += std::chrono::seconds{1};
  CHECK(f.frame());
  CHECK(f.shell.characterState() == CharacterState::Error);
  CHECK(f.repaints == 0);
  CHECK(!f.shell.nextFrameDue().has_value());
}

TEST_CASE("a waiting tooltip and the idle breath share nextFrameDue, and the earlier is due first") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  if (!std::filesystem::is_directory(designAssetRoot())) return;
  const auto breath = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
      std::chrono::duration<double>(native_ui::design::CharacterAnimator::kBreathFrameSeconds));
  ShellFixture f;
  f.now = at(11.0);
  CHECK(f.frame());
  f.now = at(12.0);
  CHECK(f.frame());
  f.controller.rebuildAccessibilityTree();
  f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
  const auto play = f.shell.layout().playButton;
  CHECK(play.width > 0.0);
  const ui::Point over{play.x + play.width * 0.5, play.y + play.height * 0.5};
  // The pointer comes to rest on the button at this instant, and its tip is due a delay later. The
  // breath is due first: a tenth of a second on.
  f.now = at(13.0);
  static_cast<void>(f.shell.pointerMove(f.controller, native_ui::PointerEvent{.position = over}));
  CHECK(f.frame());
  CHECK(f.shell.nextFrameDue() == at(13.0) + breath);
  // Half a second on, the tip is 50 ms away and the breath 100 ms: the tip is due first.
  f.now = at(13.0) + std::chrono::milliseconds{550};
  CHECK(f.frame());
  CHECK(f.shell.nextFrameDue() == at(13.0) + native_ui::design::kTooltipDelay);
}

TEST_CASE("a frame that is not painted leaves no frame due") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  if (!std::filesystem::is_directory(designAssetRoot())) return;
  ShellFixture f;
  CHECK(f.frame());
  f.now = at(12.0);
  CHECK(f.frame());
  CHECK(f.shell.nextFrameDue().has_value());
  // The window shrinks to the smallest layout and then has no pixels at all, as a minimised window
  // has none: the shell cannot paint into the surface and says so, and the frame it owed from the
  // one before is not owed by a frame that did not happen.
  CHECK(f.shell.prepareFrame(f.controller, 480.0, 320.0));
  native_ui::PixelSurface nothing{0U, 0U};
  native_ui::RasterCanvas canvas{nothing, 1.0};
  f.now = at(13.0);
  CHECK(!f.shell.paint(canvas, f.controller, f.controller.sceneState(), f.controller.playheadTick()));
  CHECK(!f.shell.nextFrameDue().has_value());
}

TEST_CASE("a window that cannot be painted waits for no tip either") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  if (!std::filesystem::is_directory(designAssetRoot())) return;
  ShellFixture f;
  f.now = at(11.0);
  CHECK(f.frame());
  f.now = at(12.0);
  CHECK(f.frame());
  f.controller.rebuildAccessibilityTree();
  f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
  const auto play = f.shell.layout().playButton;
  CHECK(play.width > 0.0);
  const ui::Point over{play.x + play.width * 0.5, play.y + play.height * 0.5};
  // The pointer comes to rest on the button, and half a second on its tip is 50 ms away: a frame is
  // due for it, and it is the tip that is due first.
  f.now = at(13.0);
  static_cast<void>(f.shell.pointerMove(f.controller, native_ui::PointerEvent{.position = over}));
  CHECK(f.frame());
  f.now = at(13.0) + std::chrono::milliseconds{550};
  CHECK(f.frame());
  CHECK(f.shell.nextFrameDue() == at(13.0) + native_ui::design::kTooltipDelay);
  // The window then has no pixels, as a minimised window has none, before the tip's time comes. The
  // shell cannot paint, so the tip still waits, and a frame that cannot be painted is not asked for
  // whatever is waiting to be shown in it.
  native_ui::PixelSurface nothing{0U, 0U};
  native_ui::RasterCanvas canvas{nothing, 1.0};
  f.now = at(13.0) + std::chrono::milliseconds{560};
  CHECK(!f.shell.paint(canvas, f.controller, f.controller.sceneState(), f.controller.playheadTick()));
  CHECK(!f.shell.nextFrameDue().has_value());
}

TEST_CASE("the header avatar is reserved only where the header has room, never in the menu layouts") {
  using native_ui::design::solveSingLayout;
  using native_ui::design::kSingHeaderAvatar;

  // A header whose fixed regions leave no gap between the tabs and the mode switch reserves nothing:
  // the contract's tab strip runs up to the switch and no rectangle is inserted between them.
  const auto narrow = solveSingLayout(1440.0, 900.0);
  CHECK(narrow.headerAvatar.width == 0.0);
  CHECK(narrow.workspaceTabs.right() > narrow.modeSwitch.x - 12.0 - kSingHeaderAvatar);
  // Wider headers have room in that gap, and then reserve exactly the section 8.4 circle.
  const auto wide = solveSingLayout(2200.0, 900.0);
  // A header with room for it reserves exactly the section 8.4 circle, between the tabs and the mode
  // switch, clear of both and inside the header.
  CHECK_NEAR(wide.headerAvatar.width, kSingHeaderAvatar, 1e-9);
  CHECK_NEAR(wide.headerAvatar.height, kSingHeaderAvatar, 1e-9);
  CHECK(wide.headerAvatar.x >= wide.workspaceTabs.right());
  CHECK(wide.headerAvatar.right() <= wide.modeSwitch.x);
  CHECK(wide.headerAvatar.y >= wide.header.y);
  CHECK(wide.headerAvatar.bottom() <= wide.header.bottom());
  // And it never overlaps a header region the contract fixes.
  for (const auto& region : {wide.wordmark, wide.workspaceTabs, wide.modeSwitch, wide.transport,
                             wide.outputMeter, wide.settings})
    CHECK(!wide.headerAvatar.intersects(region));
  // The compact menu cases carry none: below 720 the tabs and mode switch become the menu.
  for (const auto width : {720.0, 600.0, 480.0}) {
    const auto compact = solveSingLayout(width, 480.0);
    CHECK(compact.headerAvatar.width == 0.0);
  }
}

TEST_CASE("a frozen clock freezes the character, so a capture is reproducible") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  if (!std::filesystem::is_directory(designAssetRoot())) return;
  // Two frames at the same clock reading are the same frame. The breathing drift is a function of the
  // clock alone, so this holds only because the animator reads that clock and nothing else.
  const auto frameAt = [](ShellFixture& f, std::chrono::steady_clock::time_point when) {
    f.now = when;
    CHECK(f.frame());
  };
  ShellFixture f;
  frameAt(f, at(3.0));
  const auto first = f.surface.checksum();
  frameAt(f, at(3.0));
  CHECK(f.surface.checksum() == first);
  // A later reading differs while the idle drift is under way, and the animator reports the interval
  // it is waiting on so a capture can place itself between blinks.
  frameAt(f, at(3.6));
  CHECK(f.surface.checksum() != first);
}

// The project's character display mode in the shell (the parity checklist's "character
// Full/Minimal/Off", with the modes as the fidelity review's section 9 defines them): Full draws the
// Stage and every portrait; Minimal keeps the compact identity (ring, avatar, the empty project's
// key art or pose) and drops the Stage; Off draws no character artwork, while the ring's ticks, the
// avatar's state ring and the empty project's line still carry the singer's status. C cycles the
// modes.
TEST_CASE("the character display mode shows, trims or removes each character surface") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  if (!std::filesystem::is_directory(designAssetRoot())) return;
  using Mode = domain::CharacterDisplayMode;
  const auto checksum = [](const ShellFixture& f, ui::Rect r) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (auto y = static_cast<std::uint32_t>(std::max(0.0, r.y));
         y < static_cast<std::uint32_t>(std::max(0.0, r.bottom())) && y < f.surface.height(); ++y)
      for (auto x = static_cast<std::uint32_t>(std::max(0.0, r.x));
           x < static_cast<std::uint32_t>(std::max(0.0, r.right())) && x < f.surface.width(); ++x) {
        hash ^= f.surface.pixels()[static_cast<std::size_t>(y) * f.surface.width() + x];
        hash *= 1099511628211ULL;
      }
    return hash;
  };
  // The middle of a circle, where only the portrait is drawn.
  const auto core = [](ui::Rect r, double share) {
    return ui::Rect{r.x + r.width * (0.5 - share * 0.5), r.y + r.height * (0.5 - share * 0.5),
                    r.width * share, r.height * share};
  };
  // Where the ring's state ticks are painted, as the ring-pixel test samples them.
  const auto ticks = [](const ShellFixture& f) {
    const auto& ring = f.shell.layout().portraitRing;
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::size_t i = 0U; i < 64U; ++i) {
      const auto angle = -std::numbers::pi * 0.5 + static_cast<double>(i) * 2.0 * std::numbers::pi / 64.0;
      const auto x = static_cast<std::uint32_t>(ring.x + ring.width * 0.5 + std::cos(angle) * (ring.width * 0.5 - 4.0));
      const auto y = static_cast<std::uint32_t>(ring.y + ring.height * 0.5 + std::sin(angle) * (ring.height * 0.5 - 4.0));
      if (x >= f.surface.width() || y >= f.surface.height()) continue;
      hash ^= f.surface.pixels()[static_cast<std::size_t>(y) * f.surface.width() + x];
      hash *= 1099511628211ULL;
    }
    return hash;
  };
  struct Look final {
    bool stage{false};
    std::uint64_t ring{0U};
    std::uint64_t ringTicks{0U};
    std::uint64_t avatar{0U};
    std::uint64_t emptyPose{0U};
  };
  // A header wide enough for the avatar, and the full rack for the Stage.
  constexpr double kWidth = 2200.0;
  constexpr double kHeight = 900.0;
  const auto look = [&](Mode mode) {
    Look out;
    ShellFixture f;
    f.session.project().settings().characterDisplay = mode;
    CHECK(f.frame(kWidth, kHeight));
    CHECK(f.controller.sceneState().characterMode == mode);
    const auto& l = f.shell.layout();
    CHECK(l.headerAvatar.width > 0.0);
    CHECK(l.portraitRing.width > 0.0);
    out.stage = f.shell.lastFrameShowedStage();
    out.ring = checksum(f, core(l.portraitRing, 0.5));
    out.ringTicks = ticks(f);
    out.avatar = checksum(f, core(l.headerAvatar, 0.5));
    ShellFixture empty{true};
    empty.session.project().settings().characterDisplay = mode;
    CHECK(empty.frame(kWidth, kHeight));
    // The seated pose's box, centred on the roll above the line (paintEmptyProject); the mode's
    // key art, where the roll holds it, covers the same place.
    const auto& grid = empty.shell.layout().grid;
    const auto pose = std::min(grid.height * 0.42, 208.0);
    out.emptyPose = checksum(empty, {grid.x + grid.width * 0.5 - pose * 0.5,
                                     grid.y + grid.height * 0.42 - pose, pose, pose});
    return out;
  };
  const auto full = look(Mode::Full);
  const auto minimal = look(Mode::Minimal);
  const auto off = look(Mode::Off);
  // The Stage is Full's alone.
  CHECK(full.stage);
  CHECK(!minimal.stage);
  CHECK(!off.stage);
  // Minimal keeps the compact identity exactly as Full draws it.
  CHECK(minimal.ring == full.ring);
  CHECK(minimal.avatar == full.avatar);
  CHECK(minimal.emptyPose == full.emptyPose);
  // Off removes the portraits and the pose, and keeps the ring's status ticks.
  CHECK(off.ring != full.ring);
  CHECK(off.avatar != full.avatar);
  CHECK(off.emptyPose != full.emptyPose);
  CHECK(off.ringTicks == full.ringTicks);

  // C is not the shell's key: it reaches the editor, which cycles Full, Minimal, Off and back, and
  // the next frame follows.
  ShellFixture f;
  f.session.project().settings().characterDisplay = Mode::Full;
  CHECK(f.frame(kWidth, kHeight));
  CHECK(f.shell.lastFrameShowedStage());
  for (const auto expected : {Mode::Minimal, Mode::Off, Mode::Full}) {
    const native_ui::KeyEvent c{.key = native_ui::NativeKey::C};
    CHECK(!f.shell.handleShellKey(f.controller, c));
    CHECK(f.controller.keyDown(c).hasValue());
    CHECK(f.session.project().settings().characterDisplay == expected);
    CHECK(f.frame(kWidth, kHeight));
    CHECK(f.shell.lastFrameShowedStage() == (expected == Mode::Full));
  }
}
