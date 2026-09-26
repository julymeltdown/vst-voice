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

TEST_CASE("the empty-project line appears only for a region that genuinely has no notes") {
  using native_ui::design::emptyProjectPrompt;
  using native_ui::design::kEmptyProjectPrompt;

  CHECK(!emptyProjectPrompt(1U).has_value());
  CHECK(!emptyProjectPrompt(4096U).has_value());
  const auto prompt = emptyProjectPrompt(0U);
  CHECK(prompt.has_value());
  if (prompt) CHECK(*prompt == kEmptyProjectPrompt);
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
    project.settings().characterDisplay = domain::CharacterDisplayMode::Off;
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
  const auto singingChecksum = [](character::MouthShape shape) {
    ShellFixture f;
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
}

TEST_CASE("an animating frame asks for the next one, and a still frame asks for nothing") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  if (!std::filesystem::is_directory(designAssetRoot())) return;
  // A state with motion (idle breathing, singing, the render spinner) has to keep the frame loop
  // alive, or the drift and the blink would stop after one frame.
  {
    ShellFixture f;
    CHECK(f.frame());
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
  }
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
