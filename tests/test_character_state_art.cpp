// The shipped Character 01 package and splash key art, as a development host loads them.
//
// These tests pin what the artwork must be for the state surfaces to mean anything: every state the
// shell can resolve decodes from the package, the six package states are different pictures, the
// singing mouth sprites land on the singing face without a seam, and the EMO/SCENE splash images
// decode at the plan's size. They do not judge the art itself; that is the owner's review.
#include "test_framework.hpp"

#include "seam/character/character.hpp"
#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/core/file_io.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/native_ui/design/character_surface.hpp"
#include "seam/native_ui/design/shell_overlays.hpp"
#include "seam/native_ui/design/sing_shell.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/paint/canvas2d.hpp"
#include "seam/native_ui/paint/qoi.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "test_support.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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

TEST_CASE("schema-four rings, stage layers and poses decode at their declared sizes") {
  if (auto loaded = seam::character::loadPackage(kPackage); !loaded)
    std::fprintf(stderr, "schema-four package: %s (%s)\n", loaded.error().message.c_str(),
                 loaded.error().context.c_str());
  CharacterSurface surface;
  CHECK(surface.loadPackage(kPackage));
  if (!surface.packageLoaded()) return;
  CHECK(surface.package()->manifest.schemaVersion == seam::character::kLayeredManifestSchema);
  CHECK(surface.package()->manifest.developmentOnly);
  for (const auto* mode : {"emo", "scene"}) {
    surface.setOutfit(mode);
    for (const auto state : kOnePerPackageState) {
      const auto* ring = surface.ringPortrait(state);
      CHECK(ring != nullptr);
      if (ring) CHECK(ring->width() == 512U && ring->height() == 512U);
    }
    for (const auto pose : {seam::character::Pose::Empty, seam::character::Pose::Error,
                            seam::character::Pose::Complete, seam::character::Pose::Listening}) {
      const auto* art = surface.pose(pose);
      CHECK(art != nullptr);
      if (art) CHECK(art->width() == 800U && art->height() == 800U);
    }
    const auto* stage = surface.stageManifest();
    CHECK(stage != nullptr);
    if (stage) CHECK(stage->width == 900U && stage->height == 1600U);
    CHECK(surface.stageLayers().size() == 3U);
    CHECK(surface.stageEyes(seam::character::StageEyes::Open) != nullptr);
    CHECK(surface.stageEyes(seam::character::StageEyes::Half) != nullptr);
    CHECK(surface.stageEyes(seam::character::StageEyes::Closed) != nullptr);
  }
}

TEST_CASE("QOI decoder is pixel exact against the original PPM and rejects trailing bytes") {
  auto ppm = PixelSurface::loadPpm(kPackage / "runtime/neutral.ppm");
  auto qoi = seam::native_ui::paint::loadQoi(kPackage / "runtime/v4/ppm-equivalence-neutral.qoi");
  CHECK(ppm.hasValue());
  CHECK(qoi.hasValue());
  if (!ppm || !qoi) return;
  CHECK(ppm.value().width() == qoi.value().width());
  CHECK(ppm.value().height() == qoi.value().height());
  CHECK(std::equal(ppm.value().pixels().begin(), ppm.value().pixels().end(),
                   qoi.value().pixels().begin(), qoi.value().pixels().end()));
  auto encoded = seam::core::readFileBytesLimited(kPackage / "runtime/v4/ppm-equivalence-neutral.qoi",
                                                   32ULL * 1024ULL * 1024ULL);
  CHECK(encoded.hasValue());
  if (!encoded) return;
  auto bytes = encoded.value();
  bytes.insert(bytes.end() - 8, std::byte{0x00});
  CHECK(!seam::native_ui::paint::decodeQoi(bytes).hasValue());
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

// ---- eyes, blink lids, per-mode outfits, the empty-project splash and the About sheet -----------

namespace {

double luminance(std::uint32_t pixel) {
  return 0.2126 * channel(pixel, 16U) + 0.7152 * channel(pixel, 8U) + 0.0722 * channel(pixel, 0U);
}

double luminance(seam::native_ui::Color color) {
  return 0.2126 * color.red + 0.7152 * color.green + 0.0722 * color.blue;
}

seam::native_ui::Color colorAt(const PixelSurface& surface, double x, double y) {
  const auto pixel = surface.pixels()[static_cast<std::size_t>(y) * surface.width() +
                                      static_cast<std::size_t>(x)];
  return {static_cast<std::uint8_t>(channel(pixel, 16U)),
          static_cast<std::uint8_t>(channel(pixel, 8U)),
          static_cast<std::uint8_t>(channel(pixel, 0U)), 255U};
}

// Luminance spread over a rectangle: key art is busy, an empty grid is nearly flat.
double luminanceSpread(const PixelSurface& surface, seam::ui::Rect r) {
  double sum = 0.0;
  double squares = 0.0;
  std::size_t count = 0U;
  for (auto y = r.y; y < r.bottom(); y += 3.0)
    for (auto x = r.x; x < r.right(); x += 3.0) {
      const auto l = luminance(surface.pixels()[static_cast<std::size_t>(y) * surface.width() +
                                                static_cast<std::size_t>(x)]);
      sum += l;
      squares += l * l;
      ++count;
    }
  if (count == 0U) return 0.0;
  const auto mean = sum / static_cast<double>(count);
  return std::sqrt(std::max(0.0, squares / static_cast<double>(count) - mean * mean));
}

constexpr std::array<const char*, 2> kOutfits{"", "scene"};

// The mouth checks of the shared set, for whichever outfit the surface draws.
void checkMouthsSitOnSingingFace(const CharacterSurface& surface) {
  const auto placement = surface.mouthPlacement();
  const auto* singing = surface.portrait(CharacterState::Singing);
  CHECK(placement.has_value());
  CHECK(singing != nullptr);
  if (!placement.has_value() || singing == nullptr) return;
  const auto left = static_cast<std::uint32_t>(std::lround(placement->x * kPortraitWidth));
  const auto top = static_cast<std::uint32_t>(std::lround(placement->y * kPortraitHeight));
  for (const auto shape : kMouths) {
    const auto* sprite = surface.mouth(shape);
    CHECK(sprite != nullptr);
    if (sprite == nullptr) continue;
    const auto width = sprite->width();
    const auto height = sprite->height();
    const auto pixels = sprite->pixels();
    CHECK(transparent(pixels.front()));
    CHECK(transparent(pixels.back()));
    long long total = 0;
    std::size_t count = 0U;
    for (std::uint32_t y = 1U; y + 1U < height; ++y)
      for (std::uint32_t x = 1U; x + 1U < width; ++x) {
        const auto pixel = pixels[y * width + x];
        if (transparent(pixel)) continue;
        if (!transparent(pixels[y * width + x - 1U]) && !transparent(pixels[y * width + x + 1U]) &&
            !transparent(pixels[(y - 1U) * width + x]) && !transparent(pixels[(y + 1U) * width + x]))
          continue;
        total += colorDistance(pixel, singing->pixels()[(top + y) * kPortraitWidth + left + x]);
        ++count;
      }
    CHECK(count > 0U);
    if (count > 0U) CHECK(static_cast<double>(total) / (3.0 * static_cast<double>(count)) < 4.0);
  }
}

}  // namespace

TEST_CASE("every state in every mode declares eyes, and the blink lid closes only inside them") {
  for (const auto* outfit : kOutfits) {
    CharacterSurface surface;
    CHECK(surface.loadPackage(kPackage));
    if (!surface.packageLoaded()) return;
    surface.setOutfit(outfit);
    const seam::ui::Rect frame{0.0, 0.0, kPortraitWidth, kPortraitHeight};
    for (const auto state : kOnePerPackageState) {
      const auto eyes = surface.eyes(state);
      CHECK(!eyes.empty());
      CHECK(eyes.size() <= seam::character::kMaximumEyesPerState);
      const auto tone = surface.lidTone(state);
      const auto* portrait = surface.portrait(state);
      CHECK(tone.has_value());
      if (!tone.has_value() || portrait == nullptr) continue;
      for (const auto& eye : eyes) {
        // The eyes are in the head: inside the ring's top square and the toast's head crop.
        CHECK(eye.y + eye.height <= 0.42);
        // An eye box holds lashes, iris and liner: much darker than the skin the lid closes with,
        // which is how a box placed on bare cheek or forehead would fail.
        double sum = 0.0;
        std::size_t count = 0U;
        for (auto y = eye.y * kPortraitHeight; y < (eye.y + eye.height) * kPortraitHeight; y += 1.0)
          for (auto x = eye.x * kPortraitWidth; x < (eye.x + eye.width) * kPortraitWidth; x += 1.0) {
            sum += luminance(colorAt(*portrait, x, y));
            ++count;
          }
        CHECK(count > 0U);
        if (count > 0U) CHECK(sum / static_cast<double>(count) < luminance(*tone) - 30.0);
      }
      // Every lid lies within its eye's box at every point of the blink, and none exists at rest.
      CHECK(seam::native_ui::design::blinkLids(frame, eyes, 0.0).empty());
      for (const auto blink : {0.1, 0.5, 1.0}) {
        const auto lids = seam::native_ui::design::blinkLids(frame, eyes, blink);
        CHECK(lids.size() == eyes.size());
        for (const auto& lid : lids) {
          CHECK(lid.lid.x >= lid.eye.x - 1e-9);
          CHECK(lid.lid.y >= lid.eye.y - 1e-9);
          CHECK(lid.lid.right() <= lid.eye.right() + 1e-9);
          CHECK(lid.lid.bottom() <= lid.eye.bottom() + 1e-9);
          CHECK_NEAR(lid.lid.height, lid.eye.height * blink, 1e-9);
        }
      }
    }
  }
  // Reduce Motion leaves the blink at zero, so no lid is drawn anywhere.
  seam::native_ui::design::CharacterAnimator animator{11U};
  const auto start = std::chrono::steady_clock::time_point{};
  animator.advance(CharacterState::Idle, start, false);
  const auto mid = start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                               std::chrono::duration<double>(
                                   animator.blinkIntervalSeconds() +
                                   seam::native_ui::design::CharacterAnimator::kBlinkSeconds * 0.5));
  animator.advance(CharacterState::Idle, mid, false);
  CHECK(animator.motion().blink > 0.5);
  animator.advance(CharacterState::Idle, mid, true);
  CHECK_NEAR(animator.motion().blink, 0.0, 1e-12);
  CHECK(seam::native_ui::design::blinkLids({0.0, 0.0, 320.0, 480.0},
                                           std::vector<seam::character::EyeBox>{{0.4, 0.15, 0.1, 0.03}},
                                           animator.motion().blink)
            .empty());
}

TEST_CASE("the SCENE mode draws its own state set and EMO falls back to the shared one") {
  CharacterSurface shared;
  CharacterSurface scene;
  CharacterSurface emo;
  CHECK(shared.loadPackage(kPackage));
  CHECK(scene.loadPackage(kPackage));
  CHECK(emo.loadPackage(kPackage));
  if (!shared.packageLoaded()) return;
  CHECK(shared.package()->manifest.outfit("scene") != nullptr);
  CHECK(shared.package()->manifest.outfit("emo") == nullptr);
  scene.setOutfit("scene");
  emo.setOutfit("emo");
  for (const auto state : kOnePerPackageState) {
    const auto* a = shared.portrait(state);
    const auto* b = scene.portrait(state);
    const auto* c = emo.portrait(state);
    CHECK(a != nullptr && b != nullptr && c != nullptr);
    if (a == nullptr || b == nullptr || c == nullptr) continue;
    CHECK(b->width() == kPortraitWidth && b->height() == kPortraitHeight);
    // Recoloured hair, bracelets and rim light: a different picture of the same pose.
    CHECK(meanDifference(*a, *b) > 2.0);
    // No EMO set is declared, so EMO is the shared set exactly.
    CHECK(meanDifference(*a, *c) == 0.0);
  }
  // SCENE's mouths are cut from SCENE's own singing face.
  checkMouthsSitOnSingingFace(scene);
  // Switching back drops the decoded art and draws the shared set again.
  scene.setOutfit("");
  const auto* back = scene.portrait(CharacterState::Idle);
  const auto* original = shared.portrait(CharacterState::Idle);
  CHECK(back != nullptr && original != nullptr);
  if (back != nullptr && original != nullptr) CHECK(meanDifference(*back, *original) == 0.0);
}

TEST_CASE("the loader refuses malformed eyes and outfits") {
  const auto text = seam::core::readTextFileLimited(kPackage / "manifest.json", 65536U);
  CHECK(text.hasValue());
  if (!text) return;
  const auto refuses = [&](const char* name, const auto& mutate) {
    auto manifest = seam::formats::parseJson(text.value());
    CHECK(manifest.hasValue());
    if (!manifest) return;
    mutate(manifest.value().asObject());
    const auto root = seam::test::support::temporaryDirectory(std::string{"character-"} + name);
    std::filesystem::copy(kPackage / "runtime", root / "runtime",
                          std::filesystem::copy_options::recursive);
    CHECK(seam::core::durableAtomicWriteText(root / "manifest.json",
                                             seam::formats::stringifyJson(manifest.value())));
    const auto loaded = seam::character::loadPackage(root);
    if (loaded.hasValue()) std::fprintf(stderr, "accepted: %s\n", name);
    CHECK(!loaded.hasValue());
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  };
  using Object = seam::formats::JsonValue::Object;
  refuses("unknown-eye-state", [](Object& m) {
    m["eyes"].asObject()["sleepy"] = m["eyes"].asObject()["neutral"];
  });
  refuses("eye-outside", [](Object& m) {
    m["eyes"].asObject()["neutral"].asArray().front().asObject()["x"] = seam::formats::JsonValue{0.98};
  });
  refuses("three-eyes", [](Object& m) {
    auto& eyes = m["eyes"].asObject()["neutral"].asArray();
    eyes.push_back(eyes.front());
  });
  refuses("outfit-missing-state", [](Object& m) {
    m["outfits"].asObject()["scene"].asObject()["states"].asObject().erase("error");
  });
  refuses("outfit-placement-without-mouths", [](Object& m) {
    m["outfits"].asObject()["scene"].asObject().erase("mouths");
  });
  refuses("outfit-bad-name", [](Object& m) {
    auto& outfits = m["outfits"].asObject();
    outfits["Scene Mode"] = outfits["scene"];
  });
  refuses("status-only-with-eyes", [](Object& m) {
    m.erase("mouths");
    m.erase("mouthPlacement");
    m.erase("developmentOnly");
    m.erase("outfits");
    m["schemaVersion"] = seam::formats::JsonValue{std::int64_t{1}};
  });
}

namespace {

using seam::native_ui::design::Contrast;
using seam::native_ui::design::DesignMode;
using seam::native_ui::design::DesignPreferences;
using seam::native_ui::design::SingShell;

struct SplashFixture final {
  seam::application::ProjectFactory factory{9700U};
  seam::domain::TrackId trackId{};
  seam::domain::RegionId regionId{};
  seam::application::EditorSession session;
  seam::native_ui::NativeEditorController controller;
  SingShell shell;
  PixelSurface surface{1600U, 900U};

  SplashFixture(bool empty, DesignMode mode, Contrast contrast)
      : session{makeProject(empty)},
        controller{session, factory, regionId, seam::native_ui::EditorHostCallbacks{}} {
    shell.activate("assets/ui-design", DesignPreferences{.mode = mode, .contrast = contrast});
    shell.setUiClock([] { return std::chrono::steady_clock::time_point{} + std::chrono::seconds{10}; });
  }

  seam::domain::Project makeProject(bool empty) {
    auto project = factory.createProject("Splash");
    // Full: the splash is character artwork, which a display that is Off does not draw.
    project.settings().characterDisplay = seam::domain::CharacterDisplayMode::Full;
    trackId = factory.addVocalTrack(project, "Singer");
    regionId = factory.addRegion(project, trackId, "Phrase", seam::time::Tick{0},
                                 seam::time::Tick{7680});
    if (empty) return project;
    auto [lyric, note] = factory.makeNote(seam::time::Tick{960}, seam::time::Tick{960}, 72U,
                                          U"\u3042", seam::domain::Language::Japanese);
    auto* region = project.findRegion(regionId);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
    return project;
  }

  bool frame(double width, double height) {
    controller.resize(width, height);
    if (!shell.prepareFrame(controller, width, height)) return false;
    surface = PixelSurface{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
    seam::native_ui::RasterCanvas canvas{surface, 1.0};
    return shell.paint(canvas, controller, controller.sceneState(), controller.playheadTick());
  }

  std::optional<seam::ui::Rect> nodeBounds(std::string_view id) {
    controller.rebuildAccessibilityTree();
    shell.rebuildSemantics(controller, controller.sceneState());
    std::optional<seam::ui::Rect> found;
    const auto walk = [&](const seam::native_ui::SemanticNode& node, const auto& self) -> void {
      if (node.id == id) found = node.bounds;
      for (const auto& child : node.children) self(child, self);
    };
    walk(shell.accessibilityTree().root(), walk);
    return found;
  }
};

// The background just inside the text block's top-left corner, where the block (centred in its area)
// leaves room: what the first line of text is read against.
seam::native_ui::Color backgroundUnderText(const PixelSurface& surface, seam::ui::Rect area) {
  return colorAt(surface, area.x + 2.0, area.y + 2.0);
}

// With SEAM_CHARACTER_PREVIEW_DIR set, the frames these tests judge are also written there as PPM,
// so a reviewer can look at exactly what was measured. Nothing is written otherwise.
void writePreview(const PixelSurface& surface, const std::string& name) {
  const char* directory = std::getenv("SEAM_CHARACTER_PREVIEW_DIR");
  if (directory == nullptr || *directory == '\0') return;
  static_cast<void>(surface.writePpm(std::filesystem::path{directory} / (name + ".ppm")));
}

std::string previewName(std::string_view what, DesignMode mode, Contrast contrast) {
  return std::string{what} + (mode == DesignMode::Scene ? "-scene" : "-emo") +
         (contrast == Contrast::High ? "-high" : "-standard");
}

}  // namespace

TEST_CASE("an empty roll shows the mode's splash when it fits, with a legible prompt") {
  if (!seam::native_ui::paint::vectorBackendAvailable()) return;
  for (const auto mode : {DesignMode::Emo, DesignMode::Scene})
    for (const auto contrast : {Contrast::Standard, Contrast::High}) {
      SplashFixture f{true, mode, contrast};
      CHECK(f.frame(1600.0, 900.0));
      writePreview(f.surface, previewName("empty-project", mode, contrast));
      const auto& tokens = seam::native_ui::design::tokensFor(mode, contrast);
      const auto art = seam::native_ui::design::emptyProjectSplashBounds(f.shell.layout().grid, 1.6);
      CHECK(art.has_value());
      if (!art) continue;
      CHECK(art->width >= seam::native_ui::design::kEmptySplashMinimum.x);
      // She is in the splash, seated, so the standing Stage figure is not drawn beside it.
      CHECK(!f.shell.lastFrameShowedStage());
      // The key art is drawn: the figure's side of it is busy, not the flat grid.
      CHECK(luminanceSpread(f.surface, {art->x + art->width * 0.6, art->y + art->height * 0.1,
                                        art->width * 0.3, art->height * 0.8}) > 20.0);
      // The prompt sits in the clear area and reads against what is under it.
      const auto area = seam::native_ui::design::splashTitleArea(*art);
      CHECK(area.right() <= art->x + art->width * seam::native_ui::design::kSplashClearShare);
      const auto under = backgroundUnderText(f.surface, area);
      CHECK(seam::native_ui::design::contrastRatio(tokens.color.textPrimary, under) >= 4.5);
      if (contrast == Contrast::High)
        CHECK(seam::native_ui::design::contrastRatio(tokens.color.textPrimary, under) >= 7.0);
    }
  // A roll too small for the art keeps the seated pose.
  SplashFixture small{true, DesignMode::Emo, Contrast::Standard};
  CHECK(small.frame(720.0, 480.0));
  writePreview(small.surface, "empty-project-small-emo-standard");
  CHECK(!seam::native_ui::design::emptyProjectSplashBounds(small.shell.layout().grid, 1.6)
             .has_value());
}

TEST_CASE("the About sheet shows the key art, name and version, legibly, and closes") {
  if (!seam::native_ui::paint::vectorBackendAvailable()) return;
  for (const auto mode : {DesignMode::Emo, DesignMode::Scene})
    for (const auto contrast : {Contrast::Standard, Contrast::High}) {
      SplashFixture f{false, mode, contrast};
      CHECK(f.frame(1280.0, 800.0));
      CHECK(f.shell.setAboutOpen(f.controller, true).hasValue());
      CHECK(f.frame(1280.0, 800.0));
      CHECK(f.shell.overlayKind(f.controller) == seam::native_ui::design::OverlayKind::About);
      writePreview(f.surface, previewName("about", mode, contrast));
      const auto close = f.nodeBounds(seam::native_ui::design::kAboutCloseId);
      const auto panel = f.nodeBounds("shell.overlay.about.panel");
      CHECK(close.has_value());
      CHECK(panel.has_value());
      if (!close || !panel) continue;
      CHECK(close->width >= 24.0);
      CHECK(close->height >= 24.0);
      const auto art = seam::native_ui::design::aboutArtBounds(*panel);
      CHECK(art.width > 400.0);
      CHECK(luminanceSpread(f.surface, {art.x + art.width * 0.6, art.y + art.height * 0.1,
                                        art.width * 0.3, art.height * 0.8}) > 20.0);
      const auto& tokens = seam::native_ui::design::tokensFor(mode, contrast);
      const auto under = backgroundUnderText(f.surface, seam::native_ui::design::splashTitleArea(art));
      CHECK(seam::native_ui::design::contrastRatio(tokens.color.textPrimary, under) >= 4.5);
      // Close runs, and the sheet is gone.
      CHECK(f.shell.dispatchSemantic(f.controller, seam::native_ui::design::kAboutCloseId,
                                     seam::native_ui::SemanticAction::Activate)
                .hasValue());
      CHECK(!f.shell.aboutOpen());
      // Escape closes it too.
      CHECK(f.shell.setAboutOpen(f.controller, true).hasValue());
      CHECK(f.frame(1280.0, 800.0));
      CHECK(f.shell.handleShellKey(f.controller,
                                   seam::native_ui::KeyEvent{.key = seam::native_ui::NativeKey::Escape}));
      CHECK(!f.shell.aboutOpen());
    }
}

namespace {

// Pixels that differ between two frames of the same size inside `outer` but outside `inner`.
std::size_t changedBetween(const PixelSurface& a, const PixelSurface& b, seam::ui::Rect outer,
                           seam::ui::Rect inner) {
  std::size_t changed = 0U;
  const auto width = a.width();
  for (std::uint32_t y = 0U; y < a.height(); ++y)
    for (std::uint32_t x = 0U; x < width; ++x) {
      const auto px = static_cast<double>(x) + 0.5;
      const auto py = static_cast<double>(y) + 0.5;
      const auto in = [px, py](seam::ui::Rect r) {
        return px >= r.x && py >= r.y && px < r.right() && py < r.bottom();
      };
      if (!in(outer) || in(inner)) continue;
      if (a.pixels()[y * width + x] != b.pixels()[y * width + x]) ++changed;
    }
  return changed;
}

seam::ui::Rect grown(seam::ui::Rect r, double by) {
  return {r.x - by, r.y - by, r.width + 2.0 * by, r.height + 2.0 * by};
}

// The largest per-channel change between two frames inside a rectangle.
int largestChange(const PixelSurface& a, const PixelSurface& b, seam::ui::Rect r) {
  int worst = 0;
  for (auto y = std::max(0.0, std::floor(r.y)); y < std::min<double>(a.height(), r.bottom()); y += 1.0)
    for (auto x = std::max(0.0, std::floor(r.x)); x < std::min<double>(a.width(), r.right()); x += 1.0) {
      const auto i = static_cast<std::size_t>(y) * a.width() + static_cast<std::size_t>(x);
      for (const auto shift : {0U, 8U, 16U})
        worst = std::max(worst, std::abs(static_cast<int>((a.pixels()[i] >> shift) & 255U) -
                                         static_cast<int>((b.pixels()[i] >> shift) & 255U)));
    }
  return worst;
}

PixelSurface paintScene(SplashFixture& f, const seam::native_ui::EditorSceneState& state,
                        double width = 1600.0, double height = 900.0) {
  f.controller.resize(width, height);
  CHECK(f.shell.prepareFrame(f.controller, width, height));
  PixelSurface surface{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
  seam::native_ui::RasterCanvas canvas{surface, 1.0};
  CHECK(f.shell.paint(canvas, f.controller, state, f.controller.playheadTick()));
  return surface;
}

std::string focusedId(SplashFixture& f) {
  f.controller.rebuildAccessibilityTree();
  f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
  const auto* node = f.shell.accessibilityTree().focusedNode();
  return node != nullptr ? node->id : std::string{};
}

}  // namespace

TEST_CASE("High Contrast keeps the singer ring's glow off the card around it") {
  using seam::native_ui::RenderStatusState;
  if (!seam::native_ui::paint::vectorBackendAvailable()) return;
  // The ring's ticks light with a finished, audible render; their glow is character art replayed
  // through drawRaster. Only the ring's own square may change between the two frames.
  const auto spill = [](Contrast contrast) {
    SplashFixture f{false, DesignMode::Emo, contrast};
    auto dark = f.controller.sceneState();
    dark.renderStatus.state = RenderStatusState::Idle;
    auto lit = dark;
    lit.renderStatus.state = RenderStatusState::Ready;
    lit.renderStatus.hasAudibleAudio = true;
    const auto before = paintScene(f, dark);
    const auto after = paintScene(f, lit);
    const auto ring = f.shell.layout().portraitRing;
    CHECK(ring.width > 0.0);
    return changedBetween(before, after, grown(ring, 10.0), grown(ring, 1.0));
  };
  // The standard look glows past the ring, so this band can see a glow...
  CHECK(spill(Contrast::Standard) > 0U);
  // ...and High Contrast draws none there.
  CHECK(spill(Contrast::High) == 0U);
}

TEST_CASE("the lane's playhead passes under the error and diagnostics toasts, never over them") {
  using seam::native_ui::RenderStatusState;
  if (!seam::native_ui::paint::vectorBackendAvailable()) return;
  SplashFixture f{false, DesignMode::Emo, Contrast::Standard};
  // The drawn expression lane carries the lane's playhead, in the dynamic layer.
  CHECK(f.controller.openExpressionLane(seam::ui::ExpressionChannel::Breathiness).hasValue());
  auto state = f.controller.sceneState();
  state.renderStatus.state = RenderStatusState::Failed;
  state.renderStatus.diagnostic =
      "Project has no audible rendered tracks: "
      "No voicebank unit covers the sound \"a\" of the lyric \"あ\" at bar 1, beat 1";
  state.diagnostics.push_back(seam::authoring::Diagnostic{.code = "RENDER_FAILED"});
  state.playheadPixel = -1.0;
  auto now = std::chrono::steady_clock::time_point{} + std::chrono::seconds{10};
  f.shell.setUiClock([&now] { return now; });
  // Start the toast's entrance, then compare playhead positions with the entrance settled.
  static_cast<void>(paintScene(f, state));
  now += std::chrono::milliseconds{200};
  const auto without = paintScene(f, state);
  const auto error = f.shell.lastFrameErrorToast();
  // The toast's node, published from the same scene the frame painted.
  f.shell.rebuildSemantics(f.controller, state);
  std::optional<seam::ui::Rect> diagnostics;
  for (const auto& node : f.shell.accessibilityTree().root().children)
    if (node.id == "shell.diagnostics.toast") diagnostics = node.bounds;
  CHECK(error.has_value());
  CHECK(diagnostics.has_value());
  if (!error || !diagnostics) return;
  // A playhead through both toasts, in the same (cached) shell: the partial frame recomposes the
  // dynamic layer, where the playhead is.
  const auto x = std::floor(error->x + 60.0) + 0.5;
  CHECK(x < diagnostics->right());
  state.playheadPixel = x - f.shell.layout().grid.x;
  const auto with = paintScene(f, state);
  // The playhead is drawn: the lane's plot column above the toasts changed...
  const auto plot = f.shell.layout().laneTimePlot;
  CHECK(plot.y < error->y - 4.0);
  const seam::ui::Rect above{x - 3.0, plot.y, 6.0, error->y - 4.0 - plot.y};
  CHECK(largestChange(without, with, above) > 40);
  // ...and neither toast is drawn over: at most the few levels its 97% card lets through from
  // underneath, never the line itself.
  CHECK(largestChange(without, with, *diagnostics) <= 8);
  CHECK(largestChange(without, with, *error) <= 8);
}

TEST_CASE("About's Close returns the keyboard to the control that opened it, as Escape does") {
  if (!seam::native_ui::paint::vectorBackendAvailable()) return;
  SplashFixture f{false, DesignMode::Emo, Contrast::Standard};
  CHECK(f.frame(1280.0, 800.0));
  const std::string opener{"shell.workspace.sing"};
  for (const auto way : {"close button", "pointer", "host", "escape"}) {
    CHECK(f.shell.dispatchSemantic(f.controller, opener, seam::native_ui::SemanticAction::SetFocus)
              .hasValue());
    CHECK(focusedId(f) == opener);
    CHECK(f.shell.setAboutOpen(f.controller, true).hasValue());
    CHECK(f.frame(1280.0, 800.0));
    CHECK(focusedId(f) != opener);
    const std::string_view how{way};
    if (how == "close button") {
      CHECK(f.shell.dispatchSemantic(f.controller, seam::native_ui::design::kAboutCloseId,
                                     seam::native_ui::SemanticAction::Activate)
                .hasValue());
    } else if (how == "pointer") {
      const auto close = f.nodeBounds(seam::native_ui::design::kAboutCloseId);
      CHECK(close.has_value());
      if (!close) continue;
      const seam::native_ui::PointerEvent press{
          .position = {close->x + close->width * 0.5, close->y + close->height * 0.5},
          .button = seam::native_ui::PointerButton::Left};
      CHECK(f.shell.pointerDown(f.controller, press).hasValue());
      CHECK(f.shell.pointerUp(f.controller, press).hasValue());
    } else if (how == "host") {
      CHECK(f.shell.setAboutOpen(f.controller, false).hasValue());
    } else {
      CHECK(f.shell.handleShellKey(f.controller,
                                   seam::native_ui::KeyEvent{.key = seam::native_ui::NativeKey::Escape}));
    }
    CHECK(!f.shell.aboutOpen());
    if (focusedId(f) != opener) throw seam::test::Failure{std::string{"focus lost after "} + way};
  }
}

TEST_CASE("the About sheet fits the minimum window, and a refusal would name the About sheet") {
  if (!seam::native_ui::paint::vectorBackendAvailable()) return;
  SplashFixture f{false, DesignMode::Emo, Contrast::Standard};
  // The layout never goes below 480x320, where the sheet still has room: it opens.
  CHECK(f.frame(480.0, 320.0));
  CHECK(f.shell.setAboutOpen(f.controller, true).hasValue());
  CHECK(f.shell.aboutOpen());
  CHECK(f.shell.setAboutOpen(f.controller, false).hasValue());
  // Its refusal is its own sentence, not the singer menu's.
  using seam::native_ui::design::englishShellString;
  using seam::native_ui::design::Str;
  CHECK(englishShellString(Str::TheWindowIsTooSmallForTheAbout) ==
        "The window is too small for the About sheet");
  CHECK(englishShellString(Str::TheWindowIsTooSmallForTheAbout) !=
        englishShellString(Str::TheWindowIsTooSmallFor));
}
