// M1.2/M1.3: one installed original singer, one authored lyric song, and a real tuning session that
// survives save, reopen, undo and export.
//
// This extends the covered install journey in test_procedural_install_journey.cpp. That test already
// proves installation, identity resolution and an installed phrase export; it is not repeated here.
// What this file adds is the part a creator actually does: a song with consonants and lyrics, edits
// made through the application's own commands, undo/redo, and a fresh-session reopen that must render
// the same sound rather than an unbound substitute.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/note_commands.hpp"
#include "seam/application/arrangement_commands.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/distribution/procedural_package.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/platform/application_menu.hpp"
#include "seam/platform/file_dialog.hpp"
#include "seam/standalone/application_controller.hpp"
#include "seam/standalone/authoring_session.hpp"
#include "seam/ui/expression_lane.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voice_design/voice_recipe.hpp"
#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace seam;

class FakeDialog final : public platform::IFileDialog {
public:
  core::Result<std::optional<bool>> chooseRecipePackaging() override {
    return std::optional<bool>{false};
  }
  core::Result<std::optional<std::string>> chooseRecipeStyle(
      const std::vector<std::string>& styles) override {
    offeredStyles = styles;
    return styleResponse;
  }
  std::vector<std::string> offeredStyles;
  std::optional<std::string> styleResponse;
  core::Result<std::optional<std::filesystem::path>> choose(
      const platform::FileDialogRequest&) override {
    if (responses.empty()) return std::optional<std::filesystem::path>{};
    auto result = responses.front();
    responses.erase(responses.begin());
    return result;
  }
  std::vector<std::optional<std::filesystem::path>> responses;
};

class FakePrompt final : public platform::IUnsavedChangesPrompt {
public:
  core::Result<platform::UnsavedDecision> choose(std::string_view) override {
    return platform::UnsavedDecision::Discard;
  }
};

// An original singer that can actually carry a lyric song: the five Japanese vowels plus the
// consonants this song's lyrics need. The parameter choices are development screening values, not
// phonetic qualification, and the nasal and stop models are declared explicitly so a consonant is
// refused rather than approximated when its model is missing.
voice_design::VoiceRecipe songRecipe() {
  voice_design::VoiceRecipe recipe;
  recipe.id = "song-01-original";
  recipe.seed = 7130U;
  recipe.poses = {
      {"a", "neutral", 0.0, {{800.0, 90.0, 0.0}, {1250.0, 110.0, -3.0}, {2800.0, 160.0, -6.0}}},
      {"i", "neutral", 0.0, {{300.0, 70.0, 0.0}, {2300.0, 120.0, -3.0}, {3200.0, 170.0, -6.0}}},
      {"u", "neutral", 0.0, {{350.0, 80.0, 0.0}, {1100.0, 100.0, -3.0}, {2500.0, 160.0, -6.0}}},
      {"e", "neutral", 0.0, {{500.0, 80.0, 0.0}, {1900.0, 110.0, -3.0}, {2900.0, 160.0, -6.0}}},
      {"o", "neutral", 0.0, {{500.0, 90.0, 0.0}, {900.0, 110.0, -3.0}, {2600.0, 160.0, -6.0}}},
      // Nasal poses carry an explicit active nasal tract, which is what makes them different from a
      // vowel sung through the mouth.
      {"m", "neutral", 0.85, {{300.0, 80.0, 0.0}, {1100.0, 110.0, -6.0}, {2500.0, 160.0, -9.0}},
       voice_design::NasalResonance{280.0, 80.0, 1200.0, 120.0}},
      {"n", "neutral", 0.75, {{300.0, 80.0, 0.0}, {1700.0, 110.0, -6.0}, {2800.0, 160.0, -9.0}},
       voice_design::NasalResonance{300.0, 90.0, 1500.0, 120.0}},
      // Approximants are defined by the formant transition into their neighbouring vowel, so each
      // needs its own resonance pose as well as a declared transition.
      {"r", "neutral", 0.0, {{400.0, 80.0, 0.0}, {1400.0, 110.0, -3.0}, {2200.0, 160.0, -6.0}}},
      {"w", "neutral", 0.0, {{300.0, 80.0, 0.0}, {610.0, 100.0, -3.0}, {2200.0, 160.0, -6.0}}},
      {"y", "neutral", 0.0, {{250.0, 70.0, 0.0}, {2200.0, 120.0, -3.0}, {3000.0, 170.0, -6.0}}},
  };
  recipe.frications = {
      {"s", "neutral", voice_design::FricationConfig{.seed = 7130U, .centerHz = 5500.0, .bandwidthHz = 3000.0, .gain = 0.12}},
      {"sh", "neutral", voice_design::FricationConfig{.seed = 7131U, .centerHz = 3500.0, .bandwidthHz = 3000.0, .gain = 0.12}},
      {"h", "neutral", voice_design::FricationConfig{.seed = 7132U, .centerHz = 1200.0, .bandwidthHz = 2400.0, .gain = 0.06}},
  };
  recipe.plosives = {
      {"t", "neutral", voice_design::FricationConfig{.seed = 7133U, .centerHz = 4500.0, .bandwidthHz = 3000.0, .gain = 0.12}, 10.0},
      {"k", "neutral", voice_design::FricationConfig{.seed = 7134U, .centerHz = 2500.0, .bandwidthHz = 2200.0, .gain = 0.12}, 10.0},
      // A voiced stop is a prevoiced closure followed by the same release a voiceless one has, so `b`
      // is declared with an explicit closure model rather than borrowed from `p`.
      {"p", "neutral", voice_design::FricationConfig{.seed = 7135U, .centerHz = 1200.0, .bandwidthHz = 1800.0, .gain = 0.12}, 10.0},
  };
  {
    auto voiced = recipe.plosives.back();
    voiced.phone = "b";
    voiced.voicedClosure = voice_design::VoiceRecipe::VoicedClosure{0.2, 400.0};
    recipe.plosives.push_back(std::move(voiced));
  }
  // A voiced fricative is noise plus voicing shaped by the same tract, so `z` needs its own
  // same-phone resonance pose as well as a declared voicing gain.
  recipe.poses.push_back({"z", "neutral", 0.0, {{300.0, 80.0, 0.0}, {1700.0, 110.0, -3.0}, {2800.0, 160.0, -6.0}}});
  recipe.frications.push_back({"z", "neutral",
      voice_design::FricationConfig{.seed = 7136U, .centerHz = 5000.0, .bandwidthHz = 2500.0, .gain = 0.10}, 0.35});
  recipe.approximants = {{"r", "neutral", 45.0}, {"w", "neutral", 60.0}, {"y", "neutral", 40.0}};
  return recipe;
}

// One lyric per note. The sequence deliberately mixes consonants, rising and falling pitch, unequal
// durations, a rest and a sustained final vowel, so a phrase that renders is a phrase with real
// articulation rather than a scale of held vowels. Two sections bring the total to about 39.5 seconds
// at the default 120 BPM, which is the 30–60-second working length the plan asks for and long enough
// that a creator runs out of breath before the song does. Every lyric uses a phone this recipe
// declares; a phone with no admitted model is refused at render preparation rather than substituted.
struct SongNote final {
  const char32_t* lyric;
  std::uint8_t midi;
  std::int64_t ticks;
};

// The checked-in definition of this song, resolved from the configured source root so the test finds
// it whether it runs from the repository or from a build directory. `SEAM_SONG_SOURCE_ROOT` overrides
// the root for an out-of-tree run.
#ifndef SEAM_SONG_SOURCE_ROOT
#define SEAM_SONG_SOURCE_ROOT "."
#endif
std::filesystem::path fixtureDirectory() {
  if (const char* root = std::getenv("SEAM_SONG_SOURCE_ROOT"); root != nullptr)
    return std::filesystem::path{root} / "assets" / "pilots" / "seam-song-01";
  return std::filesystem::path{SEAM_SONG_SOURCE_ROOT} / "assets" / "pilots" / "seam-song-01";
}

// Where the auditionable renders are retained, when the caller asks for them. The default stays a
// temporary directory so an ordinary test run leaves nothing behind; naming a root keeps the masters
// and a manifest for a listener, which is what the workflow and perceptual observations need.
std::optional<std::filesystem::path> artifactRoot() {
  const char* root = std::getenv("SEAM_SONG_ARTIFACT_ROOT");
  if (root == nullptr || *root == '\0') return std::nullopt;
  return std::filesystem::path{root};
}

const std::vector<SongNote>& songNotes() {
  static const std::vector<SongNote> notes{
      // Section one: a 14.5-second phrase over the opening set of phones.
      {U"あ", 62U, 960}, {U"さ", 64U, 960}, {U"き", 65U, 960}, {U"の", 67U, 1920},
      {U"は", 64U, 960}, {U"な", 62U, 960}, {U"た", 60U, 960}, {U"ら", 62U, 1920},
      {U"ま", 65U, 960}, {U"ゆ", 67U, 960}, {U"め", 65U, 960}, {U"を", 64U, 1920},
      {U"い", 69U, 960}, {U"そ", 67U, 960}, {U"き", 65U, 960}, {U"や", 64U, 1920},
      {U"か", 62U, 960}, {U"に", 64U, 960}, {U"て", 65U, 960}, {U"し", 67U, 1920},
      {U"ぼ", 69U, 960}, {U"く", 67U, 960}, {U"は", 65U, 960}, {U"あ", 64U, 2880},
      // Section two: the voiced fricative, voiced stop, and liquid sets, at a rising then falling
      // contour. These are the phones most likely to expose an articulation defect, which is why the
      // song keeps them for its second half rather than spreading them through a held-vowel scale.
      {U"ざ", 62U, 1920}, {U"ぶ", 64U, 1920}, {U"ぺ", 65U, 1920}, {U"み", 67U, 1920},
      {U"ね", 65U, 1920}, {U"る", 64U, 1920}, {U"わ", 62U, 1920}, {U"ゆ", 60U, 1920},
      {U"ず", 62U, 1920}, {U"べ", 64U, 1920}, {U"ぽ", 65U, 1920}, {U"む", 67U, 1920},
      {U"の", 65U, 1920}, {U"れ", 64U, 1920}, {U"を", 62U, 1920}, {U"や", 60U, 1920},
      {U"ぜ", 62U, 1920}, {U"ぼ", 64U, 1920}, {U"ぱ", 65U, 1920}, {U"も", 67U, 1920},
      {U"ら", 69U, 1920}, {U"しょ", 67U, 1920}, {U"せ", 65U, 1920}, {U"あ", 64U, 3840},
  };
  return notes;
}

// Writes the package exactly the way a producer would: manifest, recipe, then sign with the trusted
// key. The declared digest is over the recipe's canonical encoding, which is the identity the
// renderer validates when it loads the installed recipe.
std::filesystem::path createSongPackage(const std::filesystem::path& root,
                                        const distribution::SigningKeyPair& key) {
  const auto source = root / "producer-source";
  std::filesystem::create_directories(source);
  const auto recipe = songRecipe();
  const auto encoded = voice_design::encodeVoiceRecipe(recipe);
  if (!encoded) throw test::Failure{"encoding the song recipe failed: " + encoded.error().message};
  // The checked-in fixture must describe the same voice the code builds. When this disagrees, either
  // the song changed deliberately and the fixture has to be regenerated, or a drift was introduced
  // that would make the retained definition describe a singer the test never rendered.
  const auto fixture = fixtureDirectory();
  if (std::filesystem::exists(fixture / "recipe.json")) {
    const auto onDisk = core::readTextFileLimited(fixture / "recipe.json", 1U << 20U);
    if (!onDisk) throw test::Failure{"reading the song fixture recipe failed: " + onDisk.error().message};
    if (onDisk.value() != encoded.value())
      throw test::Failure{
          "the checked-in song fixture no longer matches the code recipe; regenerate it deliberately "
          "(SEAM_SONG_FIXTURE_OUT=" + fixture.string() + ")"};
  } else if (const char* out = std::getenv("SEAM_SONG_FIXTURE_OUT"); out != nullptr) {
    std::filesystem::create_directories(out);
    const auto written = core::durableAtomicWriteTextNew(
        std::filesystem::path{out} / "recipe.json", encoded.value());
    if (!written) throw test::Failure{"writing the song fixture recipe failed: " + written.error().message};
  }
  std::ofstream(source / "recipe.json", std::ios::binary | std::ios::trunc) << encoded.value();
  distribution::ProceduralSingerManifest manifest;
  manifest.id = "song-01-original";
  manifest.version = "1.0.0";
  manifest.displayName = "Song 01 Original";
  manifest.language = "ja";
  manifest.styles = {"neutral"};
  manifest.engineId = recipe.engineId;
  manifest.engineRevision = voice_design::kSourceFilterEngineRevision;
  manifest.recipeEntry = "recipe.json";
  manifest.recipeSha256 = core::sha256Hex(encoded.value());
  // Every phone this song's lyrics can produce, including the voiced fricative and the voiced stop the
  // second section needs.
  manifest.phones = {"a", "i", "u", "e", "o", "s", "sh", "z", "h", "t", "k", "p", "b", "m", "n", "r", "w", "y"};
  distribution::ProceduralSingerManifestJsonCodec codec;
  const auto text = codec.encode(manifest);
  if (!text) throw test::Failure{"encoding the song manifest failed: " + text.error().message};
  std::ofstream(source / "manifest.json", std::ios::binary | std::ios::trunc) << text.value();
  const auto package = root / "song-01.seamsinger";
  const auto packed = distribution::packProceduralPackage(source, package, key);
  if (!packed) throw test::Failure{"packing the song singer failed: " + packed.error().message};
  return package;
}

struct Installed final {
  std::filesystem::path root;
  std::filesystem::path installRoot;
  distribution::SigningKeyPair key;
};

// Installs the original singer into a fresh root and returns what a session needs to find it.
Installed installSongSinger(const std::string& label) {
  Installed installed{};
  installed.root = test::support::temporaryDirectory(label);
  auto key = distribution::generateSigningKeyPair();
  if (!key) throw test::Failure{"generating a signing key failed: " + key.error().message};
  installed.key = key.value();
  const auto package = createSongPackage(installed.root, installed.key);
  installed.installRoot = installed.root / "singers";
  distribution::InstallProceduralOptions options;
  options.verification = distribution::VerifySeambankOptions{
      .limits = {}, .trustedPublicKeys = {installed.key.publicKey}, .requireTrustedSigner = true};
  const auto result = distribution::installProceduralPackage(package, installed.installRoot, options);
  if (!result) throw test::Failure{"installing the song singer failed: " + result.error().message};
  return installed;
}

std::unique_ptr<standalone::AuthoringSession> makeSession(const Installed& installed) {
  auto session = standalone::AuthoringSession::create(standalone::AuthoringSessionConfig{
      .cacheRoot = installed.root / "cache",
      .voicebankRoots = {},
      .sampleRate = 48000U,
      .outputChannels = 2U,
      .bindFirstAvailableVoicebank = false,
      .allowDevelopmentVoicebanks = false});
  if (!session) throw test::Failure{"creating the authoring session failed: " + session.error().message};
  return std::move(session.value());
}

struct Editor final {
  std::unique_ptr<standalone::AuthoringSession> session;
  std::unique_ptr<standalone::StandaloneApplicationController> controller;
  FakeDialog* dialog{nullptr};
};

Editor makeEditor(const Installed& installed) {
  Editor editor{};
  editor.session = makeSession(installed);
  auto dialog = std::make_unique<FakeDialog>();
  editor.dialog = dialog.get();
  standalone::StandaloneApplicationControllerConfig config{
      .autosaveRoot = installed.root / "autosaves",
      .recentProjectsPath = installed.root / "recent.json"};
  config.proceduralSingerRoots = {distribution::ProceduralSearchRoot{
      .path = installed.installRoot, .kind = distribution::ProceduralRootKind::Installed}};
  config.renderableProceduralEngineId = std::string{voice_design::kSourceFilterEngineId};
  config.renderableProceduralEngineRevision = voice_design::kSourceFilterEngineRevision;
  auto controller = standalone::StandaloneApplicationController::create(
      *editor.session, std::move(dialog), std::make_unique<FakePrompt>(), config);
  if (!controller) throw test::Failure{"creating the controller failed: " + controller.error().message};
  editor.controller = std::move(controller.value());
  return editor;
}

// Selects the one installed singer through the application command a creator would use.
void selectInstalledSinger(Editor& editor) {
  editor.dialog->styleResponse = std::nullopt;
  const auto listed = editor.controller->dispatch(
      platform::ApplicationCommand::SelectInstalledProceduralSinger);
  if (!listed) throw test::Failure{"listing installed singers failed: " + listed.error().message};
  if (editor.dialog->offeredStyles.size() != 1U)
    throw test::Failure{"exactly one installed singer was expected"};
  // The label the chooser shows must state what the singer will render, so the creator sees the
  // supported controls and the review state before committing to a voice rather than after.
  const auto& label = editor.dialog->offeredStyles.front();
  if (label.find("voice designer") == std::string::npos || label.find("formant") == std::string::npos ||
      label.find("unreviewed") == std::string::npos)
    throw test::Failure{"the singer chooser label does not describe what the singer renders: " + label};
  editor.dialog->styleResponse = editor.dialog->offeredStyles.front();
  const auto selected = editor.controller->dispatch(
      platform::ApplicationCommand::SelectInstalledProceduralSinger);
  if (!selected) throw test::Failure{"selecting the installed singer failed: " + selected.error().message};
}

// Types the song into the selected region through the real add-note command, one lyric per note.
void writeSong(Editor& editor) {
  auto& runtime = editor.session->runtime();
  const auto region = runtime.selectedRegion();
  // The default region is shorter than this song, so it is grown first. Adding a note that extends
  // beyond its region is refused by the command, which is what this test hit before the resize.
  time::Tick total{0};
  for (const auto& note : songNotes()) total = total + time::Tick{note.ticks};
  const auto resized = runtime.execute(std::make_unique<application::ResizeVocalRegionCommand>(
      runtime.selectedTrack(), region, total));
  if (!resized) throw test::Failure{"resizing the song region failed: " + resized.error().message};
  time::Tick start{0};
  for (const auto& note : songNotes()) {
    auto [lyric, value] = runtime.document().factory().makeNote(
        start, time::Tick{note.ticks}, note.midi, std::u32string{note.lyric},
        domain::Language::Japanese);
    const auto added = runtime.execute(std::make_unique<application::AddNoteCommand>(
        region, std::move(lyric), std::move(value)));
    if (!added) throw test::Failure{"adding a song note failed: " + added.error().message};
    start = start + time::Tick{note.ticks};
  }
}

double peakAndEnergy(const std::vector<float>& samples, double& energy) {
  double peak = 0.0;
  energy = 0.0;
  for (const auto sample : samples) {
    peak = std::max(peak, std::abs(static_cast<double>(sample)));
    energy += static_cast<double>(sample) * static_cast<double>(sample);
  }
  return peak;
}

// Reads an exported master and returns its samples, failing the test rather than returning silence.
std::vector<float> readMaster(const std::filesystem::path& path) {
  const auto master = voicebank::readWav(path);
  if (!master) throw test::Failure{"reading the exported master failed: " + master.error().message};
  return master.value().interleaved;
}

}  // namespace

// The first question M1 asks: with one original singer installed and selected, does an authored lyric
// song actually render to non-silent audio through the ordinary export path?
TEST_CASE("An installed original singer renders an authored lyric song") {
  const auto installed = installSongSinger("song-journey-render");
  auto editor = makeEditor(installed);
  // The creator sees what the installed singer supports before selecting it: the offer carries a
  // summary from the shared route resolver, naming the carrier and the controls it will actually apply.
  const auto offers = editor.controller->installedSingerOffers();
  CHECK(offers.hasValue());
  if (!offers) return;
  CHECK(offers.value().size() == 1U);
  if (!offers.value().empty()) {
    const auto& offer = offers.value().front();
    CHECK(offer.selectable);
    CHECK(offer.capabilitySummary.find("voice designer") != std::string::npos);
    CHECK(offer.capabilitySummary.find("formant") != std::string::npos);
  }
  selectInstalledSinger(editor);
  writeSong(editor);

  const auto* track = editor.session->runtime().document().session().project().findVocalTrack(
      editor.session->runtime().selectedTrack());
  CHECK(track != nullptr);
  if (track == nullptr) return;
  CHECK(track->proceduralRecipe.has_value());
  // The song is written and the project is valid, so a failure below is a renderer failure rather
  // than a malformed fixture.
  CHECK(editor.session->runtime().document().session().project().validate().hasValue());
  const auto* region = editor.session->runtime().document().session().project().findRegion(
      editor.session->runtime().selectedRegion());
  CHECK(region != nullptr);
  if (region == nullptr) return;
  CHECK(region->notes.size() == songNotes().size());

  authoring::ExportSettings settings;
  settings.includeMaster = true;
  const auto exported = editor.controller->exportSet(installed.root / "export", settings);
  if (!exported) throw test::Failure{"exporting the song failed: " + exported.error().message};
  CHECK(exported.hasValue());
  if (!exported) return;
  CHECK(std::filesystem::exists(exported.value().masterPath));
  const auto samples = readMaster(exported.value().masterPath);
  CHECK(!samples.empty());
  double energy = 0.0;
  const auto peak = peakAndEnergy(samples, energy);
  // A lyric song with consonants must carry real signal; a header-sized or silent export fails.
  CHECK(peak > 1e-3);
  CHECK(energy > 0.0);
  for (const auto sample : samples) CHECK(std::isfinite(sample));
}

// The tuning question: do the edits a creator makes reach the audio, survive undo and redo, and come
// back the same after the project is saved and reopened in a fresh session? A re-render that silently
// lost the installed singer would produce different audio, so the comparison is meaningful.
TEST_CASE("Tuning an installed singer survives undo, save, reopen and export") {
  const auto installed = installSongSinger("song-journey-tuning");
  auto editor = makeEditor(installed);
  selectInstalledSinger(editor);
  writeSong(editor);
  auto& runtime = editor.session->runtime();
  const auto regionId = runtime.selectedRegion();

  authoring::ExportSettings settings;
  settings.includeMaster = true;
  const auto baseline = editor.controller->exportSet(installed.root / "baseline", settings);
  if (!baseline) throw test::Failure{"baseline export failed: " + baseline.error().message};
  CHECK(baseline.hasValue());
  if (!baseline) return;
  const auto baselineSha = baseline.value().masterSha256;
  CHECK(!baselineSha.empty());

  // Draw a formant curve through the lane, which is the edit the source-filter singer really supports.
  auto lane = ui::ExpressionLaneModel::prepare(
      runtime.document().session(), regionId, ui::ExpressionChannel::Formant);
  CHECK(lane.hasValue());
  if (!lane) return;
  CHECK(lane.value().editable().hasValue());
  CHECK(lane.value().upsert(ui::ExpressionPoint{time::Tick{0}, 1.0F}).hasValue());
  CHECK(lane.value().upsert(ui::ExpressionPoint{time::Tick{4800}, 4.0F}).hasValue());
  CHECK(lane.value().apply(runtime.document().session(), regionId).hasValue());

  const auto tuned = editor.controller->exportSet(installed.root / "tuned", settings);
  if (!tuned) throw test::Failure{"tuned export failed: " + tuned.error().message};
  CHECK(tuned.hasValue());
  if (!tuned) return;
  // The edit reached the audio, so the exported master is no longer the baseline.
  CHECK(tuned.value().masterSha256 != baselineSha);

  // Undo restores exactly the sound the project had before the edit.
  CHECK(runtime.undo().hasValue());
  const auto undone = editor.controller->exportSet(installed.root / "undone", settings);
  CHECK(undone.hasValue());
  if (!undone) return;
  CHECK(undone.value().masterSha256 == baselineSha);
  CHECK(runtime.redo().hasValue());
  const auto redone = editor.controller->exportSet(installed.root / "redone", settings);
  CHECK(redone.hasValue());
  if (!redone) return;
  CHECK(redone.value().masterSha256 == tuned.value().masterSha256);

  // Save and reopen in a new session, then export again. The reopened project must resolve the same
  // installed singer and reproduce the tuned sound.
  const auto projectPath = installed.root / "song-01.seam";
  editor.dialog->responses = {projectPath};
  const auto saved = editor.controller->dispatch(platform::ApplicationCommand::SaveProjectAs);
  CHECK(saved.hasValue());
  CHECK(std::filesystem::exists(projectPath));

  auto reopened = makeEditor(installed);
  reopened.dialog->responses = {projectPath};
  const auto opened = reopened.controller->dispatch(platform::ApplicationCommand::OpenProject);
  CHECK(opened.hasValue());
  if (!opened) return;
  const auto regionAfter = reopened.session->runtime().document().session().project().findRegion(
      reopened.session->runtime().selectedRegion());
  CHECK(regionAfter != nullptr);
  if (regionAfter == nullptr) return;
  CHECK(regionAfter->formantAutomation.points().size() == 2U);
  const auto reopenedExport = reopened.controller->exportSet(installed.root / "reopened", settings);
  CHECK(reopenedExport.hasValue());
  if (!reopenedExport) return;
  CHECK(reopenedExport.value().masterSha256 == tuned.value().masterSha256);

  // Retain listenable audio beside a manifest when a caller names an artifact root. The material is
  // what the workflow and perceptual observations need, and it is written outside the repository
  // because a master WAV is not source. Absent the variable this test leaves nothing behind.
  if (const auto artifacts = artifactRoot(); artifacts.has_value()) {
    std::filesystem::create_directories(*artifacts);
    struct Retained final { const char* name; std::filesystem::path from; std::string sha; };
    const std::vector<Retained> retained{
        {"baseline-master.wav", baseline.value().masterPath, baseline.value().masterSha256},
        {"tuned-master.wav", tuned.value().masterPath, tuned.value().masterSha256},
    };
    formats::JsonValue::Array entries;
    for (const auto& item : retained) {
      const auto destination = *artifacts / item.name;
      std::filesystem::copy_file(item.from, destination,
                                 std::filesystem::copy_options::overwrite_existing);
      const auto copied = core::sha256File(destination);
      CHECK(copied.hasValue());
      if (!copied) return;
      // The retained copy must be the export that was verified, not a similar file.
      CHECK(copied.value() == item.sha);
      entries.emplace_back(formats::JsonValue::Object{
          {"file", formats::JsonValue{item.name}}, {"masterSha256", formats::JsonValue{item.sha}}});
      std::cout << destination << '\n';
    }
    const auto manifest = formats::stringifyJson(formats::JsonValue::Object{
        {"status", formats::JsonValue{"UNREVIEWED_WORKFLOW_MATERIAL"}},
        {"releaseEligible", formats::JsonValue{false}},
        {"route", formats::JsonValue{"voice designer (seam.source-filter.v1)"}},
        {"song", formats::JsonValue{"assets/pilots/seam-song-01/recipe.json"}},
        {"notes", formats::JsonValue{static_cast<std::int64_t>(songNotes().size())}},
        {"listening", formats::JsonValue{"NOT_REVIEWED"}},
        {"creatorWorkflow", formats::JsonValue{"NOT_OBSERVED"}},
        {"comparison", formats::JsonValue{"baseline-master.wav is the untuned song; tuned-master.wav "
            "has one drawn formant curve. They are for tuning judgement, not for a quality verdict on "
            "the voice."}},
        {"files", std::move(entries)}}, true);
    const auto written = core::durableAtomicWriteText(*artifacts / "manifest.json", manifest);
    CHECK(written.hasValue());
  }
}
