// The connected journey the plan calls D4.8: author a recipe, freeze and record a review, pack,
// install, discover, select, tune, save, reopen and export with the producer's source gone.
//
// This is the test that asks whether the separately green pieces compose. It drives the real
// application controller rather than the library entry points directly.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/core/sha256.hpp"
#include "seam/application/note_commands.hpp"
#include "seam/distribution/procedural_package.hpp"
#include "seam/distribution/procedural_review.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/platform/application_menu.hpp"
#include "seam/platform/file_dialog.hpp"
#include "seam/standalone/application_controller.hpp"
#include "seam/standalone/authoring_session.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voice_design/voice_recipe.hpp"
#include "seam/ui/expression_lane.hpp"

#include <algorithm>
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
      const platform::FileDialogRequest& request) override {
    requests.push_back(request);
    if (responses.empty()) return std::optional<std::filesystem::path>{};
    auto result = responses.front();
    responses.erase(responses.begin());
    return result;
  }
  std::vector<platform::FileDialogRequest> requests;
  std::vector<std::optional<std::filesystem::path>> responses;
};

class FakePrompt final : public platform::IUnsavedChangesPrompt {
public:
  core::Result<platform::UnsavedDecision> choose(std::string_view) override {
    return platform::UnsavedDecision::Discard;
  }
};

// An editable original voice, authored as a recipe rather than recorded.
voice_design::VoiceRecipe authoredRecipe() {
  voice_design::VoiceRecipe recipe;
  recipe.id = "authored-original";
  recipe.seed = 2026U;
  recipe.poses = {{"a", "neutral", 0.0,
                   {{640.0, 78.0, 0.0}, {1180.0, 96.0, -3.0}, {2650.0, 150.0, -7.0}}}};
  return recipe;
}

// Writes the package the way a producer would: manifest, recipe, then sign.
std::filesystem::path createProceduralPackage(const std::filesystem::path& root,
                                             const distribution::SigningKeyPair& key) {
  const auto source = root / "producer-source";
  std::filesystem::create_directories(source);
  const auto recipe = authoredRecipe();
  const auto encoded = voice_design::encodeVoiceRecipe(recipe);
  if (!encoded) throw test::Failure{"encoding the authored recipe failed: " + encoded.error().message};
  std::ofstream(source / "recipe.json", std::ios::binary | std::ios::trunc) << encoded.value();
  distribution::ProceduralSingerManifest manifest;
  manifest.id = "authored-original";
  manifest.version = "1.0.0";
  manifest.displayName = "Authored Original";
  manifest.language = "ja";
  manifest.styles = {"neutral"};
  manifest.engineId = recipe.engineId;
  manifest.engineRevision = 14U;
  manifest.recipeEntry = "recipe.json";
  // The declared digest is over the recipe's canonical encoding, the identity the renderer checks.
  manifest.recipeSha256 = core::sha256Hex(encoded.value());
  manifest.phones = {"a"};
  distribution::ProceduralSingerManifestJsonCodec codec;
  const auto text = codec.encode(manifest);
  if (!text) throw test::Failure{"encoding the manifest failed: " + text.error().message};
  std::ofstream(source / "manifest.json", std::ios::binary | std::ios::trunc) << text.value();
  const auto package = root / "authored.seamsinger";
  const auto packed = distribution::packProceduralPackage(source, package, key);
  if (!packed) throw test::Failure{"packing failed: " + packed.error().message};
  return package;
}

}  // namespace

namespace {

// A second release of the same singer: same recipe shape, a different seed, so it is a genuinely
// different voice installed side by side rather than a duplicate of the first.
std::filesystem::path createProceduralPackageVariant(const std::filesystem::path& root,
                                                    const distribution::SigningKeyPair& key,
                                                    std::uint64_t seed) {
  const auto source = root / "producer-source-variant";
  std::filesystem::create_directories(source);
  auto recipe = authoredRecipe();
  recipe.seed = seed;
  const auto encoded = voice_design::encodeVoiceRecipe(recipe);
  if (!encoded) throw test::Failure{"encoding the variant recipe failed: " + encoded.error().message};
  std::ofstream(source / "recipe.json", std::ios::binary | std::ios::trunc) << encoded.value();
  distribution::ProceduralSingerManifest manifest;
  manifest.id = "authored-original";
  manifest.version = "2.0.0";
  manifest.displayName = "Authored Original";
  manifest.language = "ja";
  manifest.styles = {"neutral"};
  manifest.engineId = recipe.engineId;
  manifest.engineRevision = 14U;
  manifest.recipeEntry = "recipe.json";
  manifest.recipeSha256 = core::sha256Hex(encoded.value());
  manifest.phones = {"a"};
  distribution::ProceduralSingerManifestJsonCodec codec;
  const auto text = codec.encode(manifest);
  if (!text) throw test::Failure{"encoding the variant manifest failed: " + text.error().message};
  std::ofstream(source / "manifest.json", std::ios::binary | std::ios::trunc) << text.value();
  const auto package = root / "authored-variant.seamsinger";
  const auto packed = distribution::packProceduralPackage(source, package, key);
  if (!packed) throw test::Failure{"packing the variant failed: " + packed.error().message};
  return package;
}

}  // namespace

// The first composition question: does the identity an installed singer is selected under match the
// identity the renderer validates when it loads the installed recipe?
TEST_CASE("An installed procedural singer records an identity the renderer can load") {
  const auto root = test::support::temporaryDirectory("procedural-install-identity");
  auto key = distribution::generateSigningKeyPair();
  CHECK(key.hasValue());
  if (!key) return;
  const auto package = createProceduralPackage(root, key.value());
  const auto installRoot = root / "singers";
  distribution::InstallProceduralOptions installOptions;
  installOptions.verification = distribution::VerifySeambankOptions{
      .limits = {}, .trustedPublicKeys = {key.value().publicKey}, .requireTrustedSigner = true};
  const auto installed = distribution::installProceduralPackage(package, installRoot, installOptions);
  CHECK(installed.hasValue());
  if (!installed) return;
  const auto installedRecipe = installed.value().installDirectory / "recipe.json";
  CHECK(std::filesystem::exists(installedRecipe));

  auto session = standalone::AuthoringSession::create(standalone::AuthoringSessionConfig{
      .cacheRoot = root / "cache",
      .voicebankRoots = {},
      .sampleRate = 48000U,
      .outputChannels = 2U,
      .bindFirstAvailableVoicebank = false,
      .allowDevelopmentVoicebanks = false});
  CHECK(session.hasValue());
  if (!session) return;
  auto dialog = std::make_unique<FakeDialog>();
  auto* picker = dialog.get();
  standalone::StandaloneApplicationControllerConfig config{
      .autosaveRoot = root / "autosaves", .recentProjectsPath = root / "recent.json"};
  config.proceduralSingerRoots = {distribution::ProceduralSearchRoot{
      .path = installRoot, .kind = distribution::ProceduralRootKind::Installed}};
  config.renderableProceduralEngineId = "seam.source-filter.v1";
  config.renderableProceduralEngineRevision = 14U;
  auto controller = standalone::StandaloneApplicationController::create(
      *session.value(), std::move(dialog), std::make_unique<FakePrompt>(), config);
  CHECK(controller.hasValue());
  if (!controller) return;
  auto& runtime = session.value()->runtime();
  const auto trackId = runtime.selectedTrack();
  CHECK(trackId.valid());

  const auto listed = controller.value()->installedProceduralSingers();
  CHECK(listed.hasValue());
  if (!listed) return;
  CHECK(listed.value().size() == 1U);
  // Cancelling the chooser records the labels it offered, so the next dispatch can choose one
  // without hard-coding a display string.
  picker->styleResponse = std::nullopt;
  CHECK(controller.value()->dispatch(platform::ApplicationCommand::SelectInstalledProceduralSinger));
  CHECK(picker->offeredStyles.size() == 1U);
  CHECK(picker->offeredStyles.front().find("Authored Original") != std::string::npos);
  CHECK(!runtime.document().session().project().findVocalTrack(trackId)->proceduralRecipe);
  picker->styleResponse = picker->offeredStyles.front();
  CHECK(controller.value()->dispatch(platform::ApplicationCommand::SelectInstalledProceduralSinger));
  const auto* track = runtime.document().session().project().findVocalTrack(trackId);
  CHECK(track != nullptr);
  if (track == nullptr) return;
  CHECK(track->proceduralRecipe.has_value());
  if (!track->proceduralRecipe) return;
  const auto recorded = *track->proceduralRecipe;
  CHECK(recorded.resource.id == "authored-original");
  CHECK(recorded.resource.kind == domain::SingerResourceKind::Procedural);
  CHECK(recorded.style == "neutral");

  // The recorded identity must be one the renderer will accept for the installed recipe. A
  // selection that records an identity the renderer refuses is a selection that cannot sing.
  const auto loaded = voice_design::loadVoiceRecipeResource(
      recorded.path, std::optional<domain::SingerResourceIdentity>{recorded.resource});
  CHECK(loaded.hasValue());
  if (!loaded) return;
  CHECK(loaded.value().identity == recorded.resource);
}

// The rest of the connected journey: with the installed singer selected, the creator writes a note,
// tunes a channel the resource actually supports, saves, reopens and exports after the producer's
// source directory is gone.
TEST_CASE("An installed procedural singer sings a tuned phrase after the producer source is gone") {
  const auto root = test::support::temporaryDirectory("procedural-install-journey");
  auto key = distribution::generateSigningKeyPair();
  CHECK(key.hasValue());
  if (!key) return;
  const auto package = createProceduralPackage(root, key.value());
  const auto installRoot = root / "singers";
  distribution::InstallProceduralOptions installOptions;
  installOptions.verification = distribution::VerifySeambankOptions{
      .limits = {}, .trustedPublicKeys = {key.value().publicKey}, .requireTrustedSigner = true};
  const auto installed = distribution::installProceduralPackage(package, installRoot, installOptions);
  CHECK(installed.hasValue());
  if (!installed) return;

  auto session = standalone::AuthoringSession::create(standalone::AuthoringSessionConfig{
      .cacheRoot = root / "cache",
      .voicebankRoots = {},
      .sampleRate = 48000U,
      .outputChannels = 2U,
      .bindFirstAvailableVoicebank = false,
      .allowDevelopmentVoicebanks = false});
  CHECK(session.hasValue());
  if (!session) return;
  auto dialog = std::make_unique<FakeDialog>();
  auto* picker = dialog.get();
  standalone::StandaloneApplicationControllerConfig config{
      .autosaveRoot = root / "autosaves", .recentProjectsPath = root / "recent.json"};
  config.proceduralSingerRoots = {distribution::ProceduralSearchRoot{
      .path = installRoot, .kind = distribution::ProceduralRootKind::Installed}};
  config.renderableProceduralEngineId = "seam.source-filter.v1";
  config.renderableProceduralEngineRevision = 14U;
  auto controller = standalone::StandaloneApplicationController::create(
      *session.value(), std::move(dialog), std::make_unique<FakePrompt>(), config);
  CHECK(controller.hasValue());
  if (!controller) return;
  auto& runtime = session.value()->runtime();
  const auto trackId = runtime.selectedTrack();
  CHECK(trackId.valid());
  picker->styleResponse = std::nullopt;
  CHECK(controller.value()->dispatch(platform::ApplicationCommand::SelectInstalledProceduralSinger));
  CHECK(picker->offeredStyles.size() == 1U);
  picker->styleResponse = picker->offeredStyles.front();
  CHECK(controller.value()->dispatch(platform::ApplicationCommand::SelectInstalledProceduralSinger));
  CHECK(runtime.document().session().project().findVocalTrack(trackId)->proceduralRecipe.has_value());

  // The creator writes a note and tunes expression on the installed singer.
  auto [lyric, note] = runtime.document().factory().makeNote(
      time::Tick{0}, time::Tick{1920}, 62U, U"あ", domain::Language::Japanese);
  CHECK(runtime.execute(std::make_unique<application::AddNoteCommand>(
      runtime.selectedRegion(), std::move(lyric), std::move(note))));

  auto lane = ui::ExpressionLaneModel::prepare(
      runtime.document().session(), runtime.selectedRegion(), ui::ExpressionChannel::Formant);
  CHECK(lane.hasValue());
  if (!lane) return;
  // A source-filter carrier owns its tract, so the formant channel is genuinely editable here.
  CHECK(lane.value().editable().hasValue());
  CHECK(lane.value().upsert(ui::ExpressionPoint{time::Tick{0}, 2.0F}).hasValue());
  CHECK(lane.value().upsert(ui::ExpressionPoint{time::Tick{960}, 4.0F}).hasValue());
  CHECK(lane.value().apply(runtime.document().session(), runtime.selectedRegion()).hasValue());
  CHECK(!ui::readExpressionPoints(*runtime.document().session().project().findRegion(
      runtime.selectedRegion()), ui::ExpressionChannel::Formant).empty());

  // Save, then delete the producer's source and the package. Only the installed resource and the
  // saved project remain.
  const auto projectPath = root / "installed-song.seam";
  picker->responses = {projectPath};
  CHECK(controller.value()->dispatch(platform::ApplicationCommand::SaveProjectAs));
  CHECK(std::filesystem::exists(projectPath));
  std::error_code error;
  std::filesystem::remove_all(root / "producer-source", error);
  CHECK(!error);
  CHECK(!std::filesystem::exists(root / "producer-source"));
  std::filesystem::remove(package, error);

  // Reopen and export using only the installed singer.
  picker->responses = {projectPath};
  CHECK(controller.value()->dispatch(platform::ApplicationCommand::OpenProject));
  const auto* reopened = runtime.document().session().project().findVocalTrack(trackId);
  CHECK(reopened != nullptr);
  if (reopened == nullptr) return;
  CHECK(reopened->proceduralRecipe.has_value());
  if (!reopened->proceduralRecipe) return;
  CHECK(reopened->proceduralRecipe->resource == installed.value().renderIdentity);
  authoring::ExportSettings settings;
  settings.includeMaster = true;
  const auto exported = controller.value()->exportSet(root / "export", settings);
  CHECK(exported.hasValue());
  if (!exported) return;
  CHECK(std::filesystem::exists(exported.value().masterPath));
  CHECK(std::filesystem::file_size(exported.value().masterPath) > 44U);
  CHECK(!exported.value().masterSha256.empty());
}

// A package the producer never signed by the trusted key must not install, and a resource whose
// engine this build cannot render must be visible but not offered.
TEST_CASE("An untrusted or incompatible procedural package cannot be selected") {
  const auto root = test::support::temporaryDirectory("procedural-install-refusals");
  auto key = distribution::generateSigningKeyPair();
  auto other = distribution::generateSigningKeyPair();
  CHECK(key.hasValue());
  CHECK(other.hasValue());
  if (!key || !other) return;
  const auto package = createProceduralPackage(root, key.value());
  const auto installRoot = root / "singers";
  // Signed by a key this build does not trust: nothing is installed, and no partial state remains.
  distribution::InstallProceduralOptions untrusted;
  untrusted.verification = distribution::VerifySeambankOptions{
      .limits = {}, .trustedPublicKeys = {other.value().publicKey}, .requireTrustedSigner = true};
  CHECK(!distribution::installProceduralPackage(package, installRoot, untrusted).hasValue());
  CHECK(!std::filesystem::exists(installRoot / "authored-original"));

  // Trusted and installed, but built for another engine: present and refused, not silently offered.
  distribution::InstallProceduralOptions trusted;
  trusted.verification = distribution::VerifySeambankOptions{
      .limits = {}, .trustedPublicKeys = {key.value().publicKey}, .requireTrustedSigner = true};
  CHECK(distribution::installProceduralPackage(package, installRoot, trusted).hasValue());
  auto session = standalone::AuthoringSession::create(standalone::AuthoringSessionConfig{
      .cacheRoot = root / "cache",
      .voicebankRoots = {},
      .sampleRate = 48000U,
      .outputChannels = 2U,
      .bindFirstAvailableVoicebank = false,
      .allowDevelopmentVoicebanks = false});
  CHECK(session.hasValue());
  if (!session) return;
  auto dialog = std::make_unique<FakeDialog>();
  standalone::StandaloneApplicationControllerConfig config{
      .autosaveRoot = root / "autosaves", .recentProjectsPath = root / "recent.json"};
  config.proceduralSingerRoots = {distribution::ProceduralSearchRoot{
      .path = installRoot, .kind = distribution::ProceduralRootKind::Installed}};
  config.renderableProceduralEngineId = "seam.source-filter.other";
  config.renderableProceduralEngineRevision = 14U;
  auto controller = standalone::StandaloneApplicationController::create(
      *session.value(), std::move(dialog), std::make_unique<FakePrompt>(), config);
  CHECK(controller.hasValue());
  if (!controller) return;
  const auto listed = controller.value()->installedProceduralSingers();
  CHECK(listed.hasValue());
  if (!listed) return;
  CHECK(listed.value().size() == 1U);
  CHECK(!controller.value()->dispatch(platform::ApplicationCommand::SelectInstalledProceduralSinger));
  CHECK(!session.value()->runtime().document().session().project()
             .findVocalTrack(session.value()->runtime().selectedTrack())
             ->proceduralRecipe.has_value());
}

// The remaining failure modes the plan names: a resource that disappears after the project was saved,
// an intentional replacement that must not disturb the previous version, and an interrupted install
// that must leave nothing behind.
TEST_CASE("A vanished installed singer is reported missing while the project still opens") {
  const auto root = test::support::temporaryDirectory("procedural-install-missing");
  auto key = distribution::generateSigningKeyPair();
  CHECK(key.hasValue());
  if (!key) return;
  const auto package = createProceduralPackage(root, key.value());
  const auto installRoot = root / "singers";
  distribution::InstallProceduralOptions installOptions;
  installOptions.verification = distribution::VerifySeambankOptions{
      .limits = {}, .trustedPublicKeys = {key.value().publicKey}, .requireTrustedSigner = true};
  const auto installed = distribution::installProceduralPackage(package, installRoot, installOptions);
  CHECK(installed.hasValue());
  if (!installed) return;

  distribution::ProceduralCatalogue catalogue;
  const auto roots = std::vector<distribution::ProceduralSearchRoot>{
      distribution::ProceduralSearchRoot{.path = installRoot,
                                         .kind = distribution::ProceduralRootKind::Installed}};
  auto scanned = catalogue.scan(roots);
  CHECK(scanned.hasValue());
  if (!scanned) return;
  CHECK(scanned.value().size() == 1U);
  const auto reference = scanned.value().front().renderIdentity;
  CHECK(distribution::resolveProceduralSinger(reference, scanned.value()).resolved());

  // Remove the installed resource entirely. The saved song must still resolve as missing and say so,
  // rather than resolving to a different singer or claiming success.
  std::error_code error;
  std::filesystem::remove_all(installRoot, error);
  CHECK(!error);
  auto rescan = catalogue.scan(roots);
  CHECK(rescan.hasValue());
  if (!rescan) return;
  CHECK(rescan.value().empty());
  const auto gone = distribution::resolveProceduralSinger(reference, rescan.value());
  CHECK(!gone.resolved());
  CHECK(gone.status == distribution::ProceduralResolveStatus::Missing);
  CHECK(gone.diagnostic.find("authored-original") != std::string::npos);
}

TEST_CASE("A replacement installs side by side and does not disturb the previous version") {
  const auto root = test::support::temporaryDirectory("procedural-install-replace");
  auto key = distribution::generateSigningKeyPair();
  CHECK(key.hasValue());
  if (!key) return;
  const auto package = createProceduralPackage(root, key.value());
  const auto installRoot = root / "singers";
  distribution::InstallProceduralOptions installOptions;
  installOptions.verification = distribution::VerifySeambankOptions{
      .limits = {}, .trustedPublicKeys = {key.value().publicKey}, .requireTrustedSigner = true};
  const auto first = distribution::installProceduralPackage(package, installRoot, installOptions);
  CHECK(first.hasValue());
  if (!first) return;

  distribution::ProceduralCatalogue catalogue;
  const auto roots = std::vector<distribution::ProceduralSearchRoot>{
      distribution::ProceduralSearchRoot{.path = installRoot,
                                         .kind = distribution::ProceduralRootKind::Installed}};
  auto scanned = catalogue.scan(roots);
  CHECK(scanned.hasValue());
  if (!scanned) return;
  const auto firstIdentity = scanned.value().front().renderIdentity;

  // A second, different version of the same singer, installed without replacement, must coexist.
  const auto secondPackage = createProceduralPackageVariant(root, key.value(), 111U);
  distribution::InstallProceduralOptions second = installOptions;
  second.replaceExisting = false;
  const auto installedSecond = distribution::installProceduralPackage(secondPackage, installRoot, second);
  CHECK(installedSecond.hasValue());
  if (!installedSecond) return;
  auto rescan = catalogue.scan(roots);
  CHECK(rescan.hasValue());
  if (!rescan) return;
  CHECK(rescan.value().size() == 2U);
  // The first version is still exactly installable and resolvable after the second arrived.
  const auto stillThere = distribution::resolveProceduralSinger(firstIdentity, rescan.value());
  CHECK(stillThere.resolved());
  CHECK(stillThere.candidate->renderIdentity == firstIdentity);
}

TEST_CASE("An interrupted procedural install leaves no staging or partial resource") {
  const auto root = test::support::temporaryDirectory("procedural-install-interrupted");
  auto key = distribution::generateSigningKeyPair();
  CHECK(key.hasValue());
  if (!key) return;
  const auto package = createProceduralPackage(root, key.value());
  const auto installRoot = root / "singers";
  distribution::InstallProceduralOptions installOptions;
  installOptions.verification = distribution::VerifySeambankOptions{
      .limits = {}, .trustedPublicKeys = {key.value().publicKey}, .requireTrustedSigner = true};
  CHECK(distribution::installProceduralPackage(package, installRoot, installOptions).hasValue());

  // A tampered package fails verification, and failing must not leave staging behind or damage the
  // installation that already exists.
  const auto tampered = root / "tampered.seamsinger";
  {
    std::ifstream in(package, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    if (!bytes.empty()) bytes[bytes.size() / 2U] = static_cast<char>(bytes[bytes.size() / 2U] ^ 0x5A);
    std::ofstream out(tampered, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }
  CHECK(!distribution::installProceduralPackage(tampered, installRoot, installOptions).hasValue());
  CHECK(std::filesystem::exists(installRoot / "authored-original" / "1.0.0" / "recipe.json"));
  for (const auto& entry : std::filesystem::directory_iterator(installRoot)) {
    const auto name = entry.path().filename().string();
    CHECK(!name.starts_with(".staging-"));
    CHECK(!name.starts_with(".backup-"));
  }
}

// Copy-to-edit is the D4.6 obligation that a creator can change an installed singer without
// rewriting signed content. These cases check the invariant, not just the happy path: the installed
// bytes must be untouched by any copy or edit.
TEST_CASE("A creator copies an installed singer to a draft and the installation is unchanged") {
  const auto root = test::support::temporaryDirectory("procedural-copy-to-draft");
  auto key = distribution::generateSigningKeyPair();
  CHECK(key.hasValue());
  if (!key) return;
  const auto package = createProceduralPackage(root, key.value());
  const auto installRoot = root / "singers";
  distribution::InstallProceduralOptions installOptions;
  installOptions.verification = distribution::VerifySeambankOptions{
      .limits = {}, .trustedPublicKeys = {key.value().publicKey}, .requireTrustedSigner = true};
  const auto installed = distribution::installProceduralPackage(package, installRoot, installOptions);
  CHECK(installed.hasValue());
  if (!installed) return;

  distribution::ProceduralCatalogue catalogue;
  auto scanned = catalogue.scan({distribution::ProceduralSearchRoot{
      .path = installRoot, .kind = distribution::ProceduralRootKind::Installed}});
  CHECK(scanned.hasValue());
  if (!scanned) return;
  CHECK(scanned.value().size() == 1U);
  const auto candidate = scanned.value().front();
  const auto installedRecipe = installed.value().installDirectory / "recipe.json";
  const auto installedBefore = core::sha256File(installedRecipe, 1024U * 1024U);
  CHECK(installedBefore.hasValue());
  if (!installedBefore) return;

  // The draft is written outside every installation root.
  const auto draft = root / "drafts" / "my-singer.json";
  const auto copied = distribution::copyInstalledSingerToDraft(
      candidate, draft,
      distribution::CopyInstalledProceduralOptions{.protectedRoots = {installRoot}});
  CHECK(copied.hasValue());
  if (!copied) return;
  CHECK(std::filesystem::exists(draft));
  // The draft is the same singer, so editing it starts from what the creator selected.
  const auto draftResource = voice_design::loadVoiceRecipeResource(draft, std::nullopt);
  CHECK(draftResource.hasValue());
  if (!draftResource) return;
  CHECK(draftResource.value().identity == candidate.renderIdentity);
  // Copying did not touch the signed installation.
  const auto installedAfter = core::sha256File(installedRecipe, 1024U * 1024U);
  CHECK(installedAfter.hasValue());
  if (!installedAfter) return;
  CHECK(installedAfter.value() == installedBefore.value());

  // Editing the draft produces a new identity and still leaves the installation alone.
  auto edited = draftResource.value();
  const auto loadedRecipe = voice_design::decodeVoiceRecipeResource(edited);
  CHECK(loadedRecipe.hasValue());
  if (!loadedRecipe) return;
  auto recipe = loadedRecipe.value();
  recipe.poses.front().formants.front().frequencyHz += 60.0;
  const auto editedResource = voice_design::freezeVoiceRecipeResource(recipe);
  CHECK(editedResource.hasValue());
  if (!editedResource) return;
  CHECK(editedResource.value().identity != candidate.renderIdentity);
  CHECK(editedResource.value().identity.id == candidate.renderIdentity.id);
  CHECK(voice_design::saveVoiceRecipeFile(draft, recipe).hasValue());
  const auto installedFinal = core::sha256File(installedRecipe, 1024U * 1024U);
  CHECK(installedFinal.hasValue());
  if (!installedFinal) return;
  CHECK(installedFinal.value() == installedBefore.value());
}

TEST_CASE("A draft can never be written inside an installation root") {
  const auto root = test::support::temporaryDirectory("procedural-copy-protected");
  auto key = distribution::generateSigningKeyPair();
  CHECK(key.hasValue());
  if (!key) return;
  const auto package = createProceduralPackage(root, key.value());
  const auto installRoot = root / "singers";
  distribution::InstallProceduralOptions installOptions;
  installOptions.verification = distribution::VerifySeambankOptions{
      .limits = {}, .trustedPublicKeys = {key.value().publicKey}, .requireTrustedSigner = true};
  const auto installed = distribution::installProceduralPackage(package, installRoot, installOptions);
  CHECK(installed.hasValue());
  if (!installed) return;
  distribution::ProceduralCatalogue catalogue;
  auto scanned = catalogue.scan({distribution::ProceduralSearchRoot{
      .path = installRoot, .kind = distribution::ProceduralRootKind::Installed}});
  CHECK(scanned.hasValue());
  if (!scanned) return;
  const auto candidate = scanned.value().front();
  const auto installedRecipe = installed.value().installDirectory / "recipe.json";
  const auto installedBefore = core::sha256File(installedRecipe, 1024U * 1024U);
  CHECK(installedBefore.hasValue());
  if (!installedBefore) return;

  // A destination inside the install root is refused, so the signed recipe cannot be overwritten.
  const auto insideRoot = installRoot / "authored-original" / "1.0.0" / "recipe.json";
  const auto refused = distribution::copyInstalledSingerToDraft(
      candidate, insideRoot,
      distribution::CopyInstalledProceduralOptions{.protectedRoots = {installRoot}});
  CHECK(!refused.hasValue());
  if (!refused) CHECK(refused.error().code == core::ErrorCode::Conflict);
  CHECK(std::filesystem::exists(installedRecipe));
  const auto installedAfter = core::sha256File(installedRecipe, 1024U * 1024U);
  CHECK(installedAfter.hasValue());
  if (!installedAfter) return;
  CHECK(installedAfter.value() == installedBefore.value());

  // A destination inside the resource's own directory is refused even without a protected-root list.
  const auto insideResource = candidate.resourceRoot / "copy.json";
  CHECK(!distribution::copyInstalledSingerToDraft(
      candidate, insideResource, distribution::CopyInstalledProceduralOptions{}).hasValue());

  // An existing draft is not silently overwritten unless the caller asks for it.
  const auto draft = root / "drafts" / "taken.json";
  CHECK(distribution::copyInstalledSingerToDraft(
      candidate, draft,
      distribution::CopyInstalledProceduralOptions{.protectedRoots = {installRoot}}).hasValue());
  CHECK(!distribution::copyInstalledSingerToDraft(
      candidate, draft,
      distribution::CopyInstalledProceduralOptions{.protectedRoots = {installRoot}}).hasValue());
  CHECK(distribution::copyInstalledSingerToDraft(
      candidate, draft,
      distribution::CopyInstalledProceduralOptions{.protectedRoots = {installRoot},
                                                  .overwriteExisting = true}).hasValue());
}

// The whole point of copy-to-edit: the creator can reach it from the running application and the
// project then follows the creator-owned draft rather than the signed installation.
TEST_CASE("The application copies the selected installed singer to an editable draft") {
  const auto root = test::support::temporaryDirectory("procedural-copy-app");
  auto key = distribution::generateSigningKeyPair();
  CHECK(key.hasValue());
  if (!key) return;
  const auto package = createProceduralPackage(root, key.value());
  const auto installRoot = root / "singers";
  distribution::InstallProceduralOptions installOptions;
  installOptions.verification = distribution::VerifySeambankOptions{
      .limits = {}, .trustedPublicKeys = {key.value().publicKey}, .requireTrustedSigner = true};
  const auto installed = distribution::installProceduralPackage(package, installRoot, installOptions);
  CHECK(installed.hasValue());
  if (!installed) return;
  auto session = standalone::AuthoringSession::create(standalone::AuthoringSessionConfig{
      .cacheRoot = root / "cache",
      .voicebankRoots = {},
      .sampleRate = 48000U,
      .outputChannels = 2U,
      .bindFirstAvailableVoicebank = false,
      .allowDevelopmentVoicebanks = false});
  CHECK(session.hasValue());
  if (!session) return;
  auto dialog = std::make_unique<FakeDialog>();
  auto* picker = dialog.get();
  standalone::StandaloneApplicationControllerConfig config{
      .autosaveRoot = root / "autosaves", .recentProjectsPath = root / "recent.json"};
  config.proceduralSingerRoots = {distribution::ProceduralSearchRoot{
      .path = installRoot, .kind = distribution::ProceduralRootKind::Installed}};
  config.renderableProceduralEngineId = "seam.source-filter.v1";
  config.renderableProceduralEngineRevision = 14U;
  auto controller = standalone::StandaloneApplicationController::create(
      *session.value(), std::move(dialog), std::make_unique<FakePrompt>(), config);
  CHECK(controller.hasValue());
  if (!controller) return;
  auto& runtime = session.value()->runtime();
  const auto trackId = runtime.selectedTrack();
  CHECK(trackId.valid());

  // Copying requires a selected procedural singer, so an unsupported request is refused first.
  const auto withoutSelection = controller.value()->copyInstalledSingerToDraft(
      root / "drafts" / "no-selection.json");
  CHECK(!withoutSelection.hasValue());

  picker->styleResponse = std::nullopt;
  CHECK(controller.value()->dispatch(platform::ApplicationCommand::SelectInstalledProceduralSinger));
  picker->styleResponse = picker->offeredStyles.front();
  CHECK(controller.value()->dispatch(platform::ApplicationCommand::SelectInstalledProceduralSinger));
  const auto selectedRecipe = runtime.document().session().project()
                                  .findVocalTrack(trackId)
                                  ->proceduralRecipe->path;
  CHECK(selectedRecipe == (installed.value().installDirectory / "recipe.json").string());
  const auto installedBefore = core::sha256File(selectedRecipe, 1024U * 1024U);
  CHECK(installedBefore.hasValue());
  if (!installedBefore) return;

  // Cancel writes nothing and leaves the project on the installed singer.
  picker->responses = {std::nullopt};
  const auto cancelled = controller.value()->copyInstalledSingerToDraft();
  CHECK(cancelled.hasValue());
  if (!cancelled) return;
  CHECK(cancelled.value().empty());
  CHECK(runtime.document().session().project().findVocalTrack(trackId)->proceduralRecipe->path ==
        selectedRecipe);

  // A draft inside the installation root is refused by the command as well as the library.
  const auto refused = controller.value()->copyInstalledSingerToDraft(
      installRoot / "authored-original" / "1.0.0" / "recipe.json");
  CHECK(!refused.hasValue());
  CHECK(runtime.document().session().project().findVocalTrack(trackId)->proceduralRecipe->path ==
        selectedRecipe);

  // A real draft is created, selected, and undoable back to the installed singer.
  const auto draft = root / "drafts" / "my-draft.json";
  const auto copied = controller.value()->copyInstalledSingerToDraft(draft);
  CHECK(copied.hasValue());
  if (!copied) return;
  CHECK(copied.value() == draft);
  CHECK(std::filesystem::exists(draft));
  const auto* afterCopy = runtime.document().session().project().findVocalTrack(trackId);
  CHECK(afterCopy->proceduralRecipe->path == draft.string());
  // The draft carries the installed singer's identity, because it is the same voice as a starting
  // point; editing it is what produces a different one.
  CHECK(afterCopy->proceduralRecipe->resource == installed.value().renderIdentity);
  CHECK(runtime.undo());
  CHECK(runtime.document().session().project().findVocalTrack(trackId)->proceduralRecipe->path ==
        selectedRecipe);
  // Nothing in this sequence modified the signed installation.
  const auto installedAfter = core::sha256File(selectedRecipe, 1024U * 1024U);
  CHECK(installedAfter.hasValue());
  if (!installedAfter) return;
  CHECK(installedAfter.value() == installedBefore.value());
}
