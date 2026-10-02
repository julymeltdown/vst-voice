#include "studio_app.hpp"
#include "options.hpp"
#include "seam/core/finite_decimal.hpp"

#include "seam/native_ui/native_window.hpp"
#include "seam/native_ui/voicebank_studio.hpp"
#include "seam/native_ui/voice_designer_session.hpp"
#include "seam/native_ui/voice_designer_layout.hpp"
#include "seam/native_ui/voice_designer_source_selection.hpp"
#include "seam/native_ui/installed_singer_song.hpp"
#include "seam/native_ui/installed_bank_song.hpp"
#include "seam/native_ui/sample_bank_package.hpp"
#include "seam/platform/application_menu.hpp"
#include "seam/platform/application_paths.hpp"
#include "seam/platform/audio_input_device.hpp"
#include "seam/platform/recording_session.hpp"
#include "seam/platform/recording_input_session.hpp"
#include "seam/platform/file_dialog.hpp"
#include "seam/platform/audio_device.hpp"
#include "seam/native_ui/candidate_audition.hpp"
#include "seam/native_ui/candidate_audition_session.hpp"
#include "seam/authoring/generation_job.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/distribution/procedural_package.hpp"
#include "seam/distribution/signing.hpp"
#include <atomic>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <charconv>

#include <chrono>
#include <future>
#include <memory>
#include <clocale>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace seam::voicebank_studio_native {
namespace {

// The Studio's own type scale for the rows a creator reads rather than scans. It is named here and
// used for both the geometry that holds the text and the text itself, so a strip can never be sized
// for one and filled with the other: the second developer found this window drawing 6-to-7 point text
// at 720 by 520, and a strip sized to the old type is what kept it small.
inline constexpr double kStudioHintHeight = 26.0;
inline constexpr double kStudioHintBaseline = 52.0;
inline constexpr double kStudioHintText = 12.0;
inline constexpr double kStudioControlText = 12.0;
// The app status is a sentence and it is given a full width row of its own for that reason. The two
// widths it has to work at are measured, not guessed: at 720 points the longest message needs about
// 545 points of type at 12 point, and the space beside the shortcut strips is 136, so a status
// squeezed there needs four lines and gets two, and the sentence is cut at both ends. The row below
// the header gives it 214 points at that width, which is three lines of the longest message and one
// line at 1100. The intake column starts below it (its first row is at region.y + 44), so the two
// never overlap. It wraps because a clipped sentence shows the creator its middle and hides what it
// says.
inline constexpr double kStudioStatusTop = 76.0;
inline constexpr double kStudioStatusHeight = 36.0;
// The status spans the window between the units rail, which occupies the first 252 points, and the
// inspector column on the right, which is 238 wide with an 8 point gap before it.
inline constexpr double kStudioStatusLeft = 258.0;
inline constexpr double kStudioStatusRightMargin = 246.0;
// The recording row is inside the units rail column, in the band between the rail heading and its
// first row. The rail occupies the first 252 points of the window and its rows start at 108, so this
// band is the one place in the window that is free at every width: the header above it is taken by
// the title, the status and the microphone line, and everything to the right of the rail is taken by
// the production body, which fills its region from 72 down to the bottom.
inline constexpr double kStudioRecordLeft = 8.0;
inline constexpr double kStudioRecordTop = 74.0;
inline constexpr double kStudioRecordHeight = 24.0;

class VoicebankStudioApp final : public IVoicebankStudioApp {
public:
  VoicebankStudioApp(bool forceSyntheticInput, StudioPlatform platform)
      : platform_(std::move(platform)), recording_(48000U, 300U), recordingInput_(recording_, forceSyntheticInput
            ? seam::platform::RecordingInputMode::SyntheticTest
            : seam::platform::RecordingInputMode::Physical, platform_.recordingInput) {
    // Installed singers are signed and immutable, so a Designer draft is never saved inside the
    // folders this Studio installs into, not only inside an individual singer's own folder.
    std::vector<std::filesystem::path> protectedRoots;
    for (const auto& root : platform_.singerRoots()) protectedRoots.push_back(root.path);
    designer_.setProtectedRoots(std::move(protectedRoots));
  }

  ~VoicebankStudioApp() override {
    stopAudition();
  }

  seam::core::Result<void> open(const Options& options) override {
    designerView_ = options.startDesigner;
    if (!options.manifest.empty()) {
      auto loaded = controller_.openManifest(options.manifest);
      if (!loaded) return loaded;
    }
    if (options.productionProject.has_value()) {
      auto production = controller_.openProductionProject(
          *options.productionProject, options.inventorySha256,
          options.operatorId);
      if (!production) return production;
    }
    // File, producer and Designer work must not require microphone permission.
    // Open a fresh selected input only when Record is requested, including retries.
    return seam::core::success();
  }

  seam::core::Result<seam::voicebank_production::ExportedU57Inputs>
  exportProductionInputs(const std::filesystem::path& destination) override {
    return controller_.exportProductionInputs(destination);
  }

  seam::core::Result<void> selectProductionUnit(std::size_t index) override {
    return controller_.selectUnit(index);
  }

  seam::core::Result<seam::voicebank_production::CommittedDerivedRevision>
  applyProductionOperation(const seam::voicebank_production::OperationRequest& request) override {
    return controller_.applySelectedProductionOperation(request);
  }

  seam::core::Result<void> importProductionTake(
      const std::filesystem::path& path) override {
    auto inspected = controller_.inspectSelectedProductionTake(path);
    if (!inspected) return inspected;
    return controller_.importSelectedTake(path);
  }

  seam::core::Result<void> importRecordedTakeFromDialog() {
    if (designerView_ || sampleReviewView_ || generationModal_ || takeImportModal_ ||
        controller_.proceduralImportBusy() || recordingInput_.capturing() || recordingInput_.pending())
      return seam::core::failure(seam::core::ErrorCode::Conflict,
          "Finish the current Designer, recording or production operation before importing a WAV take");
    struct ModalGuard final {
      bool& active;
      explicit ModalGuard(bool& value) : active(value) { active = true; }
      ~ModalGuard() { active = false; }
    } guard{takeImportModal_};
    const auto* project = controller_.productionProject();
    if (!project || !controller_.selectedProductionAssignment())
      return seam::core::failure(seam::core::ErrorCode::InvalidState,
          "Open a producer workspace and select an inventory row before importing a WAV take");
    const auto epoch = controller_.productionSessionEpoch();
    const auto generation = project->lastDurableGeneration;
    const auto selected = controller_.selectedIndex();
    auto dialog = platform_.fileDialog();
    const auto path = dialog->choose({.purpose = seam::platform::FileDialogPurpose::ImportAudio,
        .title = "Import an Existing Recording as an Unapproved Take", .initialDirectory = {},
        .suggestedName = {}, .extensions = {"wav"}});
    if (!path) return seam::core::Result<void>{path.error()};
    if (!path.value()) return seam::core::success();
    const auto context = controller_.validateProductionImportContext(epoch, generation, selected);
    if (!context) return context;
    if (controller_.proceduralImportBusy() || recordingInput_.capturing() || recordingInput_.pending())
      return seam::core::failure(seam::core::ErrorCode::Conflict,
          "Production state changed while the recording picker was open; review the selected row and retry");
    stopAudition();
    return controller_.beginRawTakeImport(*path.value());
  }

  void setWindow(seam::native_ui::INativeWindow& window) noexcept override { window_ = &window; }
  void finishPendingImport() override {
    stopAudition();
    while (pendingRecordingExportStarted_) {
      const auto exported = pollPendingRecordingExport();
      if (!exported || !pendingRecordingExportStarted_) {
        record(exported);
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    if (pendingRecordingImportStarted_) {
      auto imported = seam::core::success();
      do {
        imported = controller_.pollProceduralCandidateImport();
        if (!imported || !controller_.proceduralImportBusy()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
      } while (true);
      record(imported);
      completePendingRecordingImport(imported);
    } else {
      const auto imported = controller_.finishProceduralCandidateImport();
      record(imported);
      completePendingRecordingImport(imported);
    }
    record(designer_.finish());
  }

  seam::core::Result<bool> allowDesignerReplacement() {
    if (designerConfirmationActive_) return false;
    if (designer_.model() && designer_.model()->gestureActive())
      return seam::core::failure<bool>(seam::core::ErrorCode::Conflict, "Release or cancel the Designer drag first");
    if (designer_.busy()) return seam::core::failure<bool>(seam::core::ErrorCode::Conflict, "Finish Designer file work first");
    if (!designer_.model() || !designer_.model()->dirty()) return true;
    const auto epoch = designer_.epoch(); const auto revision = designer_.model()->revision();
    struct ConfirmationGuard final {
      bool& active;
      explicit ConfirmationGuard(bool& value) : active(value) { active = true; }
      ~ConfirmationGuard() { active = false; }
    } guard{designerConfirmationActive_};
    auto dialog = platform_.fileDialog();
    const auto discard = dialog->confirmDiscardDesignerChanges();
    if (!discard) return discard;
    if (designer_.busy() || designer_.epoch() != epoch || !designer_.model() || designer_.model()->revision() != revision)
      return seam::core::failure<bool>(seam::core::ErrorCode::Conflict, "Designer changed while confirming discard");
    return discard.value();
  }

  seam::core::Result<void> designerFileAction(bool save) {
    const auto* model = designer_.model();
    if (save && !model) return seam::core::failure(seam::core::ErrorCode::InvalidState, "Create or open a voice first");
    const auto epoch = designer_.epoch();
    const auto revision = model ? model->revision() : 0U;
    auto dialog = platform_.fileDialog();
    const auto path = dialog->choose({.purpose = save ? seam::platform::FileDialogPurpose::SaveDesignerRecipe
                                                     : seam::platform::FileDialogPurpose::SelectProceduralRecipe,
        .title = save ? "Save Draft Voice Recipe" : "Open Draft Voice Recipe",
        .initialDirectory = designer_.path().parent_path(),
        .suggestedName = save ? "voice-recipe.json" : "", .extensions = {"json"}});
    if (!path) return seam::core::Result<void>{path.error()};
    if (!path.value()) return seam::core::success();
    if (designer_.epoch() != epoch || (designer_.model() && designer_.model()->revision() != revision))
      return seam::core::failure(seam::core::ErrorCode::Conflict, "Designer changed while choosing a file");
    if (save) return designer_.beginSave(*path.value());
    const auto discard = allowDesignerReplacement();
    if (!discard) return seam::core::Result<void>{discard.error()};
    if (!discard.value()) return seam::core::success();
    return designer_.beginOpen(*path.value(), true);
  }

  seam::core::Result<void> publishDesignerSingerFromDialog() {
    using namespace seam;
    const auto* model = designer_.model();
    if (!designerView_ || !model || designer_.busy() || model->gestureActive())
      return core::failure(core::ErrorCode::Conflict,
          "Open a saved, idle voice recipe in Voice Designer before publishing");
    if (model->dirty() || designer_.path().empty())
      return core::failure(core::ErrorCode::Conflict,
          "Save the current voice recipe before publishing it as a singer");
    const auto epoch = designer_.epoch();
    const auto revision = model->revision();
    const auto stillCurrent = [&] {
      return designerView_ && !designer_.busy() && designer_.epoch() == epoch &&
          designer_.model() && designer_.model()->revision() == revision &&
          !designer_.model()->dirty() && !designer_.model()->gestureActive();
    };

    auto dialog = platform_.fileDialog();
    const auto entered = dialog->chooseProceduralSingerPublishInput();
    if (!entered) return core::Result<void>{entered.error()};
    if (!entered.value()) return core::success();
    if (!stillCurrent())
      return core::failure(core::ErrorCode::Conflict,
          "Designer changed while entering singer release identity");

    const auto keyPath = dialog->choose(platform::FileDialogRequest{
        .purpose = platform::FileDialogPurpose::SelectSingerSigningKey,
        .title = "Select Private Singer Signing Key",
        .initialDirectory = designer_.path().parent_path(),
        .suggestedName = {}, .extensions = {"json"}});
    if (!keyPath) return core::Result<void>{keyPath.error()};
    if (!keyPath.value()) return core::success();
    if (!stillCurrent())
      return core::failure(core::ErrorCode::Conflict,
          "Designer changed while selecting the signing key");

    auto signingKey = distribution::loadPrivateKey(*keyPath.value());
    if (!signingKey) return core::Result<void>{signingKey.error()};
    struct SigningKeyWiper final {
      distribution::SigningKeyPair& key;
      ~SigningKeyWiper() { key.privateKey.fill(std::byte{0}); }
    } wiper{signingKey.value()};

    const auto output = dialog->choose(platform::FileDialogRequest{
        .purpose = platform::FileDialogPurpose::PublishProceduralSinger,
        .title = "Publish Signed Procedural Singer Package",
        .initialDirectory = designer_.path().parent_path(),
        .suggestedName = model->recipe().id + ".seamsinger",
        .extensions = {"seamsinger"}});
    if (!output) return core::Result<void>{output.error()};
    if (!output.value()) return core::success();
    if (!stillCurrent())
      return core::failure(core::ErrorCode::Conflict,
          "Designer changed while choosing the singer package destination");
    if (output.value()->extension() != ".seamsinger")
      return core::failure(core::ErrorCode::InvalidArgument,
          "Choose a package destination ending in .seamsinger");

    static std::atomic_uint64_t stagingSequence{0U};
    const auto sequence = stagingSequence.fetch_add(1U, std::memory_order_relaxed);
    if (sequence == std::numeric_limits<std::uint64_t>::max())
      return core::failure(core::ErrorCode::Conflict,
          "Singer publication staging identity is exhausted");
    auto stagingParent = output.value()->parent_path();
    if (stagingParent.empty()) stagingParent = std::filesystem::current_path();
    const auto staging = stagingParent /
        ("." + output.value()->filename().string() + ".staging-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
         "-" + std::to_string(sequence));
    distribution::PublishProceduralSingerOptions options;
    options.version = entered.value()->version;
    options.displayName = entered.value()->displayName;
    options.language = entered.value()->language;
    const auto published = designer_.publishSavedSinger(
        staging, *output.value(), signingKey.value(), options);
    if (!published) return core::Result<void>{published.error()};
    lastError_.clear();
    publishedDesignerEpoch_ = epoch;
    publishedDesignerRevision_ = revision;
    publishedSingerPackagePath_ = *output.value();
    publishedSingerPackageDigest_ = published.value().container.packageDigest;
    publishedSingerDisplayName_ = published.value().manifest.displayName;
    publishedSingerPublicKey_ = signingKey.value().publicKey;
    publishedSingerInstalled_ = false;
    installedSinger_.reset();
    designerPublishStatus_ = "PUBLISHED SIGNED PACKAGE / NOT QUALITY-APPROVED / " +
        published.value().manifest.displayName + " " + published.value().manifest.version;
    return core::success();
  }

  seam::core::Result<void> installPublishedSinger() {
    if (publishedSingerPackagePath_.empty() || !publishedSingerPublicKey_ ||
        designerPublishStatus_.empty())
      return seam::core::failure(seam::core::ErrorCode::InvalidState,
          "Publish a signed singer package before installing it");
    if (!designer_.model() || designer_.busy() || designer_.model()->dirty() ||
        designer_.epoch() != publishedDesignerEpoch_ ||
        designer_.model()->revision() != publishedDesignerRevision_)
      return seam::core::failure(seam::core::ErrorCode::Conflict,
          "The Designer changed after publication; publish the saved voice again before installing");
    if (publishedSingerInstalled_)
      return seam::core::failure(seam::core::ErrorCode::Conflict,
          "This published singer is already installed");
    const auto roots = platform_.singerRoots();
    const auto installRoot = std::find_if(roots.begin(), roots.end(), [](const auto& root) {
      return root.kind == seam::distribution::ProceduralRootKind::Installed;
    });
    if (installRoot == roots.end() || installRoot->path.empty())
      return seam::core::failure(seam::core::ErrorCode::InvalidState,
          "No default standalone singer installation folder is available on this platform");
    const auto installed = designer_.installPublishedSinger(
        publishedDesignerEpoch_, publishedDesignerRevision_, publishedSingerPackagePath_,
        publishedSingerPackageDigest_, *publishedSingerPublicKey_, installRoot->path);
    if (!installed) return seam::core::Result<void>{installed.error()};
    publishedSingerInstalled_ = true;
    installedSinger_ = installed.value();
    designerPublishStatus_ = "INSTALLED FOR STANDALONE / NOT QUALITY-APPROVED / " +
        publishedSingerDisplayName_ + " " + installed.value().version + " / CMD-ALT-O SONG EDITOR";
    lastError_.clear();
    return seam::core::success();
  }

  // The explicit return-to-song step after installation: save a new song project bound to the exact
  // singer just installed and hand it to the Project SEAM editor. The project is written only after
  // the singer re-resolves as trusted and renderable, and an existing file is never replaced.
  seam::core::Result<void> openInstalledSingerInSongEditor() {
    if (!publishedSingerInstalled_ || !installedSinger_.has_value())
      return seam::core::failure(seam::core::ErrorCode::InvalidState,
          "Install the published singer before opening it in the song editor");
    const auto stillCurrent = [this, epoch = publishedDesignerEpoch_, revision = publishedDesignerRevision_] {
      return designer_.model() && !designer_.busy() && !designer_.model()->dirty() &&
             designer_.epoch() == epoch && designer_.model()->revision() == revision;
    };
    if (!stillCurrent())
      return seam::core::failure(seam::core::ErrorCode::Conflict,
          "The Designer changed after installation; publish and install the saved voice again");
    const auto installed = *installedSinger_;
    const auto displayName = publishedSingerDisplayName_.empty() ? installed.id : publishedSingerDisplayName_;
    auto dialog = platform_.fileDialog();
    if (dialog == nullptr)
      return seam::core::failure(seam::core::ErrorCode::Unsupported,
          "No native file dialog is available on this platform");
    const auto chosen = dialog->choose(seam::platform::FileDialogRequest{
        .purpose = seam::platform::FileDialogPurpose::SaveProject,
        .title = "Save New Song Project for " + displayName,
        .initialDirectory = designer_.path().parent_path(),
        .suggestedName = seam::native_ui::suggestedSongProjectFileName(displayName),
        .extensions = {"seam"}});
    if (!chosen) return seam::core::Result<void>{chosen.error()};
    if (!chosen.value()) return seam::core::success();
    if (!stillCurrent() || !installedSinger_.has_value())
      return seam::core::failure(seam::core::ErrorCode::Conflict,
          "The Designer changed while choosing the song project location");
    const auto& recipe = designer_.model()->recipe();
    const auto pose = designer_.auditionPose();
    const auto created = seam::native_ui::createInstalledSingerSongProject(
        installed, platform_.singerRoots(),
        seam::native_ui::InstalledSingerSongRequest{
            .projectPath = *chosen.value(),
            .preferredStyle = pose < recipe.poses.size() ? recipe.poses[pose].style : std::string{},
            .renderableEngineId = std::string{seam::voice_design::kSourceFilterEngineId},
            .renderableEngineRevision = seam::voice_design::kSourceFilterEngineRevision});
    if (!created) return seam::core::Result<void>{created.error()};
    const auto& project = created.value();
    const auto fileName = project.projectPath.filename().string();
    designerPublishStatus_ = "SONG PROJECT SAVED / NOT OPENED / " + fileName;
    const auto editor = platform_.locateSongEditor();
    if (!editor)
      return seam::core::failure(editor.error().code,
          "Song project saved at " + project.projectPath.string() + ". " + editor.error().message);
    const auto opened = platform_.openDocumentWithApplication(project.projectPath, editor.value());
    if (!opened)
      return seam::core::failure(opened.error().code,
          "Song project saved at " + project.projectPath.string() + ". " + opened.error().message);
    designerPublishStatus_ = "OPENED IN PROJECT SEAM / " + project.singer.style +
        " / NOT QUALITY-APPROVED / " + fileName;
    lastError_.clear();
    return seam::core::success();
  }

  // Packs the published engineering candidate into a signed .seambank. The key is chosen and read
  // here, zeroed after use, and never retained; a signature proves publisher authenticity and is
  // never a quality approval or release qualification.
  seam::core::Result<void> signPublishedSampleBank() {
    using namespace seam;
    const auto* candidate = controller_.publishedSampleCandidate()
        ? &*controller_.publishedSampleCandidate() : nullptr;
    if (candidate == nullptr)
      return core::failure(core::ErrorCode::InvalidState,
          "Publish a complete engineering candidate before signing a bank");
    if (controller_.proceduralImportBusy() || controller_.dirty())
      return core::failure(core::ErrorCode::Conflict,
          "Finish current Studio work and save manifest edits before signing a package");
    auto dialog = platform_.fileDialog();
    if (dialog == nullptr)
      return core::failure(core::ErrorCode::Unsupported, "No native file dialog is available on this platform");
    const auto keyPath = dialog->choose(platform::FileDialogRequest{
        .purpose = platform::FileDialogPurpose::SelectSingerSigningKey,
        .title = "Select Private Bank Signing Key",
        .initialDirectory = controller_.manifestPath().parent_path(),
        .suggestedName = {}, .extensions = {"json"}});
    if (!keyPath) return seam::core::Result<void>{keyPath.error()};
    if (!keyPath.value()) return seam::core::success();
    auto signingKey = distribution::loadPrivateKey(*keyPath.value());
    if (!signingKey) return seam::core::Result<void>{signingKey.error()};
    struct SigningKeyWiper final {
      distribution::SigningKeyPair& key;
      ~SigningKeyWiper() { key.privateKey.fill(std::byte{0}); }
    } wiper{signingKey.value()};
    const auto packagePath = dialog->choose(platform::FileDialogRequest{
        .purpose = platform::FileDialogPurpose::PublishSampleBank,
        .title = "Publish Signed Sample Bank Package (Not a Release Qualification)",
        .initialDirectory = candidate->root.parent_path(),
        .suggestedName = candidate->root.filename().string() + ".seambank",
        .extensions = {"seambank"}});
    if (!packagePath) return seam::core::Result<void>{packagePath.error()};
    if (!packagePath.value()) return seam::core::success();
    if (packagePath.value()->extension() != ".seambank")
      return core::failure(core::ErrorCode::InvalidArgument, "Choose a package destination ending in .seambank");
    const auto context = controller_.captureSampleReviewContext();
    if (!context) return seam::core::Result<void>{context.error()};
    const auto& currentCandidate = controller_.publishedSampleCandidate();
    if (!currentCandidate || currentCandidate->candidateSha256 != candidate->candidateSha256)
      return core::failure(core::ErrorCode::Conflict,
          "The published candidate changed while choosing where to sign it");
    publishedSampleBankKey_ = signingKey.value().publicKey;
    sampleBankStatus_.clear();
    return controller_.beginSampleBankPackaging(context.value(), *candidate, *packagePath.value(), signingKey.value());
  }

  // Installs the signed package into the installed root of the bank folders the song editor
  // catalogs, trusting exactly the key that signed it in this session.
  seam::core::Result<void> installSignedSampleBankFromDialog() {
    using namespace seam;
    const auto* signedBank = controller_.publishedSampleBank() ? &*controller_.publishedSampleBank() : nullptr;
    const auto* candidate = controller_.publishedSampleCandidate()
        ? &*controller_.publishedSampleCandidate() : nullptr;
    if (signedBank == nullptr || candidate == nullptr)
      return core::failure(core::ErrorCode::InvalidState,
          "Sign the published candidate before installing it as a bank");
    if (!publishedSampleBankKey_.has_value())
      return core::failure(core::ErrorCode::InvalidState,
          "The signing key of this package is not in this session; sign the candidate again");
    if (controller_.proceduralImportBusy())
      return core::failure(core::ErrorCode::Conflict, "Finish current Studio work before installing a bank");
    const auto roots = platform_.voicebankRoots();
    const auto installRoot = std::find_if(roots.begin(), roots.end(), [](const auto& root) {
      return root.kind == voicebank::VoicebankRootKind::Installed;
    });
    if (installRoot == roots.end() || installRoot->path.empty())
      return core::failure(core::ErrorCode::InvalidState,
          "No installed voicebank folder is available on this platform");
    const auto context = controller_.captureSampleReviewContext();
    if (!context) return seam::core::Result<void>{context.error()};
    return controller_.beginSampleBankInstallation(context.value(), *candidate,
        signedBank->packagePath, installRoot->path, {*publishedSampleBankKey_});
  }

  // The explicit return-to-song step: write a new song bound to the exact installed bank and hand it
  // to the Project SEAM editor. The project is written only after the bank re-resolves as a trusted
  // installation with the reviewed content hash, and an existing file is never replaced.
  seam::core::Result<void> openInstalledBankInSongEditor() {
    using namespace seam;
    const auto* installed = controller_.installedSampleBank() ? &*controller_.installedSampleBank() : nullptr;
    if (installed == nullptr)
      return core::failure(core::ErrorCode::InvalidState,
          "Install the signed bank before opening it in the song editor");
    auto dialog = platform_.fileDialog();
    if (dialog == nullptr)
      return core::failure(core::ErrorCode::Unsupported, "No native file dialog is available on this platform");
    const auto displayName = installed->voicebankId;
    const auto chosen = dialog->choose(platform::FileDialogRequest{
        .purpose = platform::FileDialogPurpose::SaveProject,
        .title = "Save New Song Project for " + displayName,
        .initialDirectory = installed->installDirectory.parent_path(),
        .suggestedName = native_ui::suggestedSongProjectFileName(displayName),
        .extensions = {"seam"}});
    if (!chosen) return seam::core::Result<void>{chosen.error()};
    if (!chosen.value()) return seam::core::success();
    if (!controller_.installedSampleBank() ||
        controller_.installedSampleBank()->contentHash != installed->contentHash)
      return core::failure(core::ErrorCode::Conflict,
          "The installed bank changed while choosing the song project location");
    const auto created = native_ui::createInstalledBankSongProject(platform_.voicebankRoots(),
        native_ui::InstalledBankSongRequest{.projectPath = *chosen.value(),
                                            .voicebankId = installed->voicebankId,
                                            .voicebankVersion = installed->voicebankVersion,
                                            .contentHash = installed->contentHash,
                                            .installDirectory = installed->installDirectory});
    if (!created) return seam::core::Result<void>{created.error()};
    const auto& project = created.value();
    const auto fileName = project.projectPath.filename().string();
    auto status = "SONG PROJECT SAVED / NOT OPENED / " + fileName;
    const auto editor = platform_.locateSongEditor();
    if (!editor) {
      reportSampleBankStatus(status);
      return seam::core::failure(editor.error().code,
          "Song project saved at " + project.projectPath.string() + ". " + editor.error().message);
    }
    const auto opened = platform_.openDocumentWithApplication(project.projectPath, editor.value());
    if (!opened) {
      reportSampleBankStatus(status);
      return seam::core::failure(opened.error().code,
          "Song project saved at " + project.projectPath.string() + ". " + opened.error().message);
    }
    reportSampleBankStatus("OPENED IN PROJECT SEAM / " + project.styleId +
        " / NOT A RELEASE QUALIFICATION / " + fileName);
    lastError_.clear();
    return seam::core::success();
  }

  // The Studio status line shows the outcome even when the editor launch fails, so a saved song is
  // never reported as nothing written.
  void reportSampleBankStatus(std::string value) {
    sampleBankStatus_ = std::move(value);
  }

  // One line for the review screen: a failure first, then playback, then the bank hand-off outcome,
  // and otherwise the controller's own review status.
  [[nodiscard]] std::string sampleReviewStatusLine() const {
    if (!lastError_.empty()) return lastError_;
    if (!auditionStatus_.empty()) return auditionStatus_;
    if (!sampleBankStatus_.empty()) return sampleBankStatus_;
    return controller_.sampleReviewStatus();
  }

  static std::vector<std::size_t> designerFrications(const seam::voice_design::VoiceRecipe& recipe, std::size_t pose) {
    std::vector<std::size_t> indices;
    for (std::size_t index = 0U; index < recipe.frications.size(); ++index)
      if (recipe.frications[index].style == recipe.poses[pose].style) indices.push_back(index);
    return indices;
  }
  void selectAddedDesignerSource(bool plosive) {
    const auto* model=designer_.model();
    if (!model) return;
    const auto count=plosive?model->recipe().plosives.size():model->recipe().frications.size();
    if (count==0U) return;
    const auto selection=seam::native_ui::designerSourceSelection(model->recipe(),designer_.auditionPose(),count-1U,plosive);
    if (!selection) return;
    const auto selected=designer_.selectAudition(designer_.epoch(),model->revision(),selection->pose,designer_.auditionPitch());
    record(selected);
    if (selected) {
      designerControl_=selection->control;
      designerSemanticFocus_=designerSemanticPrefix()+"control."+std::to_string(designerControl_);
    }
  }
  static std::size_t designerControlCount(const seam::voice_design::VoiceRecipe& recipe, std::size_t pose) {
    return designerNasalStart(recipe, pose) + 5U;
  }
  static std::vector<std::size_t> designerPlosives(const seam::voice_design::VoiceRecipe& recipe,std::size_t pose) {
    std::vector<std::size_t> indices;
    for (std::size_t i=0U;i<recipe.plosives.size();++i) if (recipe.plosives[i].style==recipe.poses[pose].style) indices.push_back(i);
    return indices;
  }
  static std::size_t designerPlosiveStart(const seam::voice_design::VoiceRecipe& recipe,std::size_t pose) {
    return 8U+recipe.poses[pose].formants.size()*3U+designerFrications(recipe,pose).size()*4U;
  }
  static std::vector<std::size_t> designerAffricates(const seam::voice_design::VoiceRecipe& recipe, std::size_t pose) {
    std::vector<std::size_t> indices;
    for (std::size_t i = 0U; i < recipe.affricates.size(); ++i)
      if (recipe.affricates[i].style == recipe.poses[pose].style) indices.push_back(i);
    return indices;
  }
  static std::vector<std::size_t> designerVoicedAffricates(const seam::voice_design::VoiceRecipe& recipe, std::size_t pose) {
    std::vector<std::size_t> indices;
    for (std::size_t i = 0U; i < recipe.voicedAffricates.size(); ++i)
      if (recipe.voicedAffricates[i].style == recipe.poses[pose].style) indices.push_back(i);
    return indices;
  }
  static std::vector<std::size_t> designerApproximants(const seam::voice_design::VoiceRecipe& recipe, std::size_t pose) {
    std::vector<std::size_t> indices;
    for (std::size_t i = 0U; i < recipe.approximants.size(); ++i)
      if (recipe.approximants[i].style == recipe.poses[pose].style) indices.push_back(i);
    return indices;
  }
  static std::vector<std::size_t> designerBreaths(const seam::voice_design::VoiceRecipe& recipe, std::size_t pose) {
    std::vector<std::size_t> indices;
    for (std::size_t i = 0U; i < recipe.breaths.size(); ++i)
      if (recipe.breaths[i].style == recipe.poses[pose].style) indices.push_back(i);
    return indices;
  }
  static std::size_t designerAffricateStart(const seam::voice_design::VoiceRecipe& recipe, std::size_t pose) {
    return designerPlosiveStart(recipe, pose) + designerPlosives(recipe, pose).size() * 4U;
  }
  static std::size_t designerVoicedAffricateStart(const seam::voice_design::VoiceRecipe& recipe, std::size_t pose) {
    return designerAffricateStart(recipe, pose) + designerAffricates(recipe, pose).size() * 7U;
  }
  static std::size_t designerApproximantStart(const seam::voice_design::VoiceRecipe& recipe, std::size_t pose) {
    return designerVoicedAffricateStart(recipe, pose) + designerVoicedAffricates(recipe, pose).size() * 10U;
  }
  static std::size_t designerBreathStart(const seam::voice_design::VoiceRecipe& recipe, std::size_t pose) {
    return designerApproximantStart(recipe, pose) + designerApproximants(recipe, pose).size();
  }
  static std::size_t designerNasalStart(const seam::voice_design::VoiceRecipe& recipe, std::size_t pose) {
    return designerBreathStart(recipe, pose) + designerBreaths(recipe, pose).size() * 3U;
  }
  std::optional<std::size_t> selectedDesignerPlosive() const {
    if (!designer_.model()) return {};
    const auto& recipe=designer_.model()->recipe();
    const auto start=designerPlosiveStart(recipe,designer_.auditionPose());
    const auto indices=designerPlosives(recipe,designer_.auditionPose());
    if (designerControl_<start || (designerControl_-start)/4U>=indices.size() || designerControl_>=designerAffricateStart(recipe,designer_.auditionPose())) return {};
    return indices[(designerControl_-start)/4U];
  }
  std::optional<std::string> selectedDesignerArticulation() const {
    if (!designer_.model()) return {};
    const auto& recipe=designer_.model()->recipe();
    const auto pose=designer_.auditionPose();
    if (designerControl_>=designerAffricateStart(recipe,pose) && designerControl_<designerVoicedAffricateStart(recipe,pose)) {
      const auto rows=designerAffricates(recipe,pose);
      const auto row=(designerControl_-designerAffricateStart(recipe,pose))/7U;
      if (row<rows.size()) return recipe.affricates[rows[row]].phone;
    }
    if (designerControl_>=designerVoicedAffricateStart(recipe,pose) && designerControl_<designerApproximantStart(recipe,pose)) {
      const auto rows=designerVoicedAffricates(recipe,pose);
      const auto row=(designerControl_-designerVoicedAffricateStart(recipe,pose))/10U;
      if (row<rows.size()) return recipe.voicedAffricates[rows[row]].phone;
    }
    if (designerControl_>=designerApproximantStart(recipe,pose) && designerControl_<designerBreathStart(recipe,pose)) {
      const auto rows=designerApproximants(recipe,pose);
      const auto row=designerControl_-designerApproximantStart(recipe,pose);
      if (row<rows.size()) return recipe.approximants[rows[row]].phone;
    }
    if (designerControl_>=designerBreathStart(recipe,pose) && designerControl_<designerNasalStart(recipe,pose)) {
      const auto rows=designerBreaths(recipe,pose);
      const auto row=(designerControl_-designerBreathStart(recipe,pose))/3U;
      if (row<rows.size()) return recipe.breaths[rows[row]].phone;
    }
    const auto oralEnd=8U+recipe.poses[pose].formants.size()*3U;
    if (designerControl_>=8U && designerControl_<oralEnd) {
      const auto& selected=recipe.poses[pose];
      if (selected.nasal && selected.nasalCoupling>0.0) return selected.phone;
      const auto palatalized=std::find_if(recipe.palatalized.begin(),recipe.palatalized.end(),[&](const auto& value) {
        return value.phone==selected.phone && value.style==selected.style;
      });
      if (palatalized!=recipe.palatalized.end()) return selected.phone;
    }
    return {};
  }
  seam::core::Result<void> editDesignerPlosiveSeed(std::size_t index) {
    const auto* model=designer_.model();
    if (!model || designer_.busy() || index>=model->recipe().plosives.size())
      return seam::core::failure(seam::core::ErrorCode::Conflict,"Select an available plosive source first");
    const auto epoch=designer_.epoch(),revision=model->revision();
    auto dialog=platform_.fileDialog();
    const auto seed=dialog->chooseDesignerSeed(std::to_string(model->recipe().plosives[index].source.seed),true);
    if (!seed) return seam::core::Result<void>{seed.error()};
    return seed.value()?designer_.setPlosiveSeed(epoch,revision,index,*seed.value()):seam::core::success();
  }
  static double plosiveControlValue(const seam::voice_design::VoiceRecipe::PlosivePose& pose,std::size_t index) {
    return index==0U?pose.source.centerHz:index==1U?pose.source.bandwidthHz:index==2U?pose.source.gain:pose.burstMilliseconds;
  }
  static void setPlosiveControl(seam::voice_design::VoiceRecipe::PlosivePose& pose,std::size_t index,double value) {
    if (index==0U) pose.source.centerHz=value;
    else if (index==1U) pose.source.bandwidthHz=value;
    else if (index==2U) pose.source.gain=value;
    else pose.burstMilliseconds=value;
  }
  static double nasalControlValue(const seam::voice_design::VoicePose& pose,std::size_t index) {
    const auto nasal=pose.nasal.value_or(seam::voice_design::NasalResonance{});
    if (index==0U) return pose.nasalCoupling;
    if (index==1U) return nasal.resonanceHz;
    if (index==2U) return nasal.resonanceBandwidthHz;
    if (index==3U) return nasal.antiresonanceHz;
    return nasal.antiresonanceBandwidthHz;
  }
  static void setNasalControl(seam::voice_design::VoicePose& pose,std::size_t index,double value) {
    if (!pose.nasal) pose.nasal=seam::voice_design::NasalResonance{};
    if (index==0U) pose.nasalCoupling=value;
    else if (index==1U) pose.nasal->resonanceHz=value;
    else if (index==2U) pose.nasal->resonanceBandwidthHz=value;
    else if (index==3U) pose.nasal->antiresonanceHz=value;
    else pose.nasal->antiresonanceBandwidthHz=value;
  }
  std::optional<std::size_t> selectedDesignerFrication() const {
    if (!designer_.model()) return std::nullopt;
    const auto& recipe = designer_.model()->recipe();
    const auto oralEnd = 8U + recipe.poses[designer_.auditionPose()].formants.size()*3U;
    const auto indices = designerFrications(recipe, designer_.auditionPose());
    if (designerControl_ < oralEnd || (designerControl_-oralEnd)/4U >= indices.size() || designerControl_>=designerPlosiveStart(recipe,designer_.auditionPose())) return std::nullopt;
    return indices[(designerControl_-oralEnd)/4U];
  }
  std::string fricationPreviewDescription() const {
    using Mode=seam::native_ui::FricationAuditionMode;
    return designer_.fricationAudioMode()==Mode::VowelFrication?"SELECTED VOWEL + FRICATION":
        designer_.fricationAudioMode()==Mode::FricationVowel?"FRICATION + SELECTED VOWEL":"FRICATION SOURCE";
  }
  seam::core::Result<void> editDesignerFricationSeed(std::size_t index) {
    const auto* model = designer_.model();
    if (!model || designer_.busy() || index >= model->recipe().frications.size())
      return seam::core::failure(seam::core::ErrorCode::Conflict, "Select an available frication source first");
    const auto epoch = designer_.epoch(); const auto revision = model->revision();
    auto dialog = platform_.fileDialog();
    const auto seed = dialog->chooseDesignerSeed(std::to_string(model->recipe().frications[index].source.seed), true);
    if (!seed) return seam::core::Result<void>{seed.error()};
    return seed.value() ? designer_.setFricationSeed(epoch, revision, index, *seed.value()) : seam::core::success();
  }
  static seam::voice_design::VoiceRecipe adjustedDesignerRecipe(seam::voice_design::VoiceRecipe desired,
      std::size_t pose, std::size_t control, double steps) {
    if (control == 0U) desired.phonation.openQuotient = std::clamp(desired.phonation.openQuotient + steps * 0.01, 0.05, 0.95);
    if (control == 1U) desired.phonation.spectralTiltDbPerOctave = std::clamp(desired.phonation.spectralTiltDbPerOctave + steps, -48.0, 0.0);
    if (control == 2U) desired.phonation.aspiration = std::clamp(desired.phonation.aspiration + steps * 0.01, 0.0, 1.0);
    if (control == 5U) desired.modulation.jitterCents = std::clamp(desired.modulation.jitterCents + steps * 0.5, 0.0, 100.0);
    if (control == 6U) desired.modulation.shimmerAmount = std::clamp(desired.modulation.shimmerAmount + steps * 0.01, 0.0, 1.0);
    if (control == 7U) desired.modulation.rateHz = std::clamp(desired.modulation.rateHz + steps * 0.1, 0.0, 20.0);
    const auto oralEnd = 8U + desired.poses[pose].formants.size()*3U;
    const auto nasalStart=designerNasalStart(desired,pose);
    if (control>=nasalStart && steps!=0.0) {
      const auto index=control-nasalStart;
      auto value=nasalControlValue(desired.poses[pose],index)+steps*(index==0U?0.01:(index==1U || index==3U)?10.0:5.0);
      if (index==0U) value=std::clamp(value,0.0,1.0);
      setNasalControl(desired.poses[pose],index,value);
    } else if (control >= nasalStart) {
      return desired;
    } else if (control >= designerBreathStart(desired,pose)) {
      auto& source=desired.breaths[designerBreaths(desired,pose)[(control-designerBreathStart(desired,pose))/3U]].source;
      const auto parameter=(control-designerBreathStart(desired,pose))%3U;
      if (parameter==0U) source.centerHz+=steps*10.0;
      else if (parameter==1U) source.bandwidthHz+=steps*10.0;
      else source.gain+=steps*0.005;
    } else if (control >= designerApproximantStart(desired,pose)) {
      auto& approximant=desired.approximants[designerApproximants(desired,pose)[control-designerApproximantStart(desired,pose)]];
      approximant.transitionMilliseconds+=steps;
    } else if (control >= designerVoicedAffricateStart(desired,pose)) {
      auto& affricate=desired.voicedAffricates[designerVoicedAffricates(desired,pose)[(control-designerVoicedAffricateStart(desired,pose))/10U]];
      const auto parameter=(control-designerVoicedAffricateStart(desired,pose))%10U;
      if (parameter<6U) {
        auto& source=parameter<3U?affricate.burst:affricate.tail;
        const auto field=parameter%3U;
        if (field==0U) source.centerHz+=steps*10.0;
        else if (field==1U) source.bandwidthHz+=steps*10.0;
        else source.gain+=steps*0.005;
      } else if (parameter==6U) affricate.burstMilliseconds+=steps;
      else if (parameter==7U) affricate.closureVoicingGain+=steps*0.01;
      else if (parameter==8U) affricate.closureLowpassHz+=steps*10.0;
      else affricate.tailVoicingGain+=steps*0.01;
    } else if (control >= designerAffricateStart(desired,pose)) {
      auto& affricate=desired.affricates[designerAffricates(desired,pose)[(control-designerAffricateStart(desired,pose))/7U]];
      const auto parameter=(control-designerAffricateStart(desired,pose))%7U;
      if (parameter<6U) {
        auto& source=parameter<3U?affricate.burst:affricate.tail;
        const auto field=parameter%3U;
        if (field==0U) source.centerHz+=steps*10.0;
        else if (field==1U) source.bandwidthHz+=steps*10.0;
        else source.gain+=steps*0.005;
      } else affricate.burstMilliseconds+=steps;
    } else if (control >= designerPlosiveStart(desired,pose)) {
      const auto relative=control-designerPlosiveStart(desired,pose);
      auto& plosive=desired.plosives[designerPlosives(desired,pose)[relative/4U]];
      const auto parameter=relative%4U;
      setPlosiveControl(plosive,parameter,plosiveControlValue(plosive,parameter)+steps*(parameter==2U?0.005:parameter==3U?1.0:10.0));
    } else if (control >= oralEnd) {
      auto& frication = desired.frications[designerFrications(desired, pose)[(control-oralEnd)/4U]];
      auto& source=frication.source;
      switch ((control-oralEnd)%4U) {
        case 0U: source.centerHz += steps*10.0; break;
        case 1U: source.bandwidthHz += steps*10.0; break;
        case 2U: source.gain += steps*0.005; break;
        case 3U: seam::native_ui::setDesignerFricationVoicing(frication,frication.voicingGain.value_or(0.0)+steps*0.01); break;
      }
    } else if (control >= 8U) {
      auto& band = desired.poses[pose].formants[(control - 8U) / 3U];
      switch ((control - 8U) % 3U) {
        case 0U: band.frequencyHz += steps * 10.0; break;
        case 1U: band.bandwidthHz += steps * 5.0; break;
        case 2U: band.gainDb += steps * 0.5; break;
      }
    }
    return desired;
  }
  void updateDesignerDrag(const seam::native_ui::PointerEvent& event) {
    if (!designerDrag_ || !designer_.model() || !std::isfinite(event.position.x)) return;
    const auto steps = (event.position.x - designerDrag_->startX) / 8.0 * (event.modifiers.shift ? 0.1 : 1.0);
    const auto result = designer_.updateGesture(designerDrag_->epoch, designer_.model()->revision(),
        adjustedDesignerRecipe(designerDrag_->baseline, designerDrag_->pose, designerDrag_->control, steps));
    if (result) lastError_.clear(); else record(result);
    repaint();
  }

  seam::core::Result<void> prepareDesignerFromDialog() {
    const auto* model = designer_.model(); const auto* producer = controller_.productionProject();
    if (!model || !producer || !controller_.selectedProductionAssignment() || designer_.busy() || model->gestureActive() ||
        controller_.proceduralImportBusy() || recording_.armed() || recording_.recordedFrames() > 0U)
      return seam::core::failure(seam::core::ErrorCode::Conflict, "Finish work and open a producer assignment before preparing the draft");
    const auto epoch = designer_.epoch(); const auto revision = model->revision();
    const auto producerEpoch = controller_.productionSessionEpoch(); const auto generation = producer->lastDurableGeneration;
    const auto row = controller_.selectedIndex();
    seam::authoring::GenerationRecipeSelection frozen{model->resource(), model->recipe().poses[designer_.auditionPose()].style};
    stopAudition();
    auto dialog = platform_.fileDialog();
    const auto path = dialog->choose({.purpose = seam::platform::FileDialogPurpose::OpenProject,
        .title = "Choose Score for Current Designer Snapshot", .initialDirectory = {}, .suggestedName = {}, .extensions = {"seam"}});
    if (!path) return seam::core::Result<void>{path.error()};
    if (!path.value()) return seam::core::success();
    if (designer_.busy() || designer_.epoch() != epoch || !designer_.model() || designer_.model()->revision() != revision ||
        recording_.armed() || recording_.recordedFrames() > 0U)
      return seam::core::failure(seam::core::ErrorCode::Conflict, "Designer changed while choosing the generation score");
    const auto current = controller_.validateProductionImportContext(producerEpoch, generation, row); if (!current) return current;
    const auto started = controller_.beginGenerationScoreInspection(*path.value(), std::move(frozen));
    if (started) designerView_ = false;
    return started;
  }

  // Starts the audio the Designer has ready and names it once it plays. The audition that plays now
  // goes first, and when its device does not say that it has stopped it is still the audition: the
  // start is refused, nothing is replaced, and the label of what plays stays what it was. Only a
  // start that succeeded names the audio it started.
  seam::core::Result<void> playDesignerAudio(
      const std::shared_ptr<const seam::voicebank::AudioBuffer>& audio, std::string label) {
    if (!audio) return seam::core::failure(seam::core::ErrorCode::InvalidState, "No audition audio is ready");
    const auto started = audition_.start(platform_.audioDevice(), audio, 0U, audio->frameCount(), 0.25F);
    if (started) auditionStatus_ = std::move(label);
    return started;
  }
  // What Space means in the Designer: render the audio the modifiers name when it is not ready, and
  // play it when it is. The result is the answer to whoever asked: the key handler shows it, and the
  // accessibility actions return it.
  seam::core::Result<void> designerSpace(const seam::native_ui::KeyEvent& event) {
    if (event.modifiers.primaryShortcut()) {
      if (const auto plosive=selectedDesignerPlosive()) {
        using Mode=seam::native_ui::PlosiveAuditionMode;
        const auto mode=event.modifiers.alt?Mode::VowelStop:event.modifiers.shift?Mode::StopVowel:Mode::Source;
        if (!designer_.plosiveAudio() || designer_.plosiveAudioIndex()!=plosive || designer_.plosiveAudioMode()!=mode)
          return designer_.beginPlosiveAudition(*plosive,mode);
        return playDesignerAudio(designer_.plosiveAudio(), mode==Mode::VowelStop?"SELECTED VOWEL + STOP / NOT APPROVED":mode==Mode::StopVowel?"STOP + SELECTED VOWEL / NOT APPROVED":"PLOSIVE SOURCE / 50 ms CLOSURE / NOT APPROVED");
      }
      const auto index = selectedDesignerFrication();
      if (!index) {
        if (const auto phone=selectedDesignerArticulation()) {
          if (!designer_.articulationAudio() || designer_.articulationAudioPhone()!=phone)
            return designer_.beginArticulationAudition(*phone);
          return playDesignerAudio(designer_.articulationAudio(), *phone+" + SELECTED VOWEL / NOT APPROVED");
        }
        return seam::core::failure(seam::core::ErrorCode::InvalidState,"Select an auditionable vowel or source articulation first");
      }
      using Mode=seam::native_ui::FricationAuditionMode;
      const auto mode=event.modifiers.alt?Mode::VowelFrication:event.modifiers.shift?Mode::FricationVowel:Mode::Source;
      if (!designer_.fricationAudio() || designer_.fricationAudioIndex() != index || designer_.fricationAudioMode()!=mode)
        return designer_.beginFricationAudition(*index,mode);
      return playDesignerAudio(designer_.fricationAudio(), fricationPreviewDescription()+" / NOT APPROVED");
    }
    if (event.modifiers.shift) {
      if (!designer_.referenceMatchesSelection())
        return seam::core::failure(seam::core::ErrorCode::Conflict, "Pin a reference and match its pose/style/pitch before A/B playback");
      return playDesignerAudio(designer_.auditionReference()->audio, "REFERENCE A / NOT APPROVED");
    }
    if (!designer_.auditionAudio()) return designer_.beginAudition();
    return playDesignerAudio(designer_.auditionAudio(), "CURRENT B / NOT APPROVED");
  }

  void designerKey(const seam::native_ui::KeyEvent& event) {
    designerSemanticFocus_.clear();
    using Key = seam::native_ui::NativeKey;
    if (event.key == Key::Escape && designer_.auditionBusy()) { designer_.cancelAudition(); return; }
    if (designer_.busy()) { if (event.key == Key::Escape) designer_.cancel(); return; }
    if (event.key == Key::P && event.modifiers.primaryShortcut() && event.modifiers.alt) {
      record(publishDesignerSingerFromDialog()); return;
    }
    if (event.key == Key::I && event.modifiers.primaryShortcut() && event.modifiers.alt) {
      stopAudition(); record(installPublishedSinger()); return;
    }
    if (event.key == Key::O && event.modifiers.primaryShortcut() && event.modifiers.alt) {
      stopAudition(); record(openInstalledSingerInSongEditor()); return;
    }
    if (event.key == Key::P && event.modifiers.primaryShortcut() && event.modifiers.shift) {
      record(prepareDesignerFromDialog()); return;
    }
    if (event.key == Key::B && event.modifiers.primaryShortcut()) {
      if (event.modifiers.shift) designer_.clearAuditionReference();
      else if (designer_.model()) record(designer_.pinAuditionReference(designer_.epoch(), designer_.model()->revision()));
      return;
    }
    if (event.key == Key::Space) { record(designerSpace(event)); return; }
    if (event.key == Key::D && event.modifiers.primaryShortcut()) {
      record(event.modifiers.shift ? createDesignerProducerWorkspace() : openDesignerProducerWorkspace());
      return;
    }
    if (event.key == Key::N && event.modifiers.primaryShortcut()) {
      const auto discard = allowDesignerReplacement();
      if (!discard) { record(seam::core::Result<void>{discard.error()}); return; }
      if (!discard.value()) return;
      const auto created = designer_.createJapaneseStarter(true);
      record(created);
      if (created) auditionStatus_ = "JA PHONE SET / SCREENING / UNQUALIFIED";
      return;
    }
    if (event.key == Key::O && event.modifiers.primaryShortcut()) { record(designerFileAction(false)); return; }
    if (event.key == Key::S && event.modifiers.primaryShortcut()) {
      record(!event.modifiers.shift && !designer_.path().empty() ? designer_.beginSave(designer_.path()) : designerFileAction(true)); return;
    }
    const auto* model = designer_.model(); if (!model) return;
    if (event.key==Key::I && event.modifiers.primaryShortcut()) {
      const auto epoch=designer_.epoch(),revision=model->revision();
      if (event.modifiers.shift) {
        const auto index=selectedDesignerPlosive();
        record(index?designer_.removePlosive(epoch,revision,*index):seam::core::failure(seam::core::ErrorCode::InvalidState,"Select a plosive control before removal"));
      } else {
        auto dialog=platform_.fileDialog();
        const auto identity=dialog->chooseDesignerPoseIdentity(seam::platform::IFileDialog::DesignerPoseKind::Plosive);
        if (!identity) record(seam::core::Result<void>{identity.error()});
        else if (identity.value()) {
          const auto added=designer_.addPlosive(epoch,revision,identity.value()->phone,identity.value()->style);
          record(added); if (added) selectAddedDesignerSource(true);
        }
      }
      return;
    }
    if (event.key == Key::E && event.modifiers.primaryShortcut()) {
      const auto epoch = designer_.epoch(); const auto revision = model->revision();
      if (event.modifiers.shift) {
        const auto index = selectedDesignerFrication();
        record(index ? designer_.removeFrication(epoch, revision, *index) : seam::core::failure(seam::core::ErrorCode::InvalidState, "Select a frication control before removing its source"));
        return;
      }
      auto dialog = platform_.fileDialog();
      const auto identity = dialog->chooseDesignerPoseIdentity(seam::platform::IFileDialog::DesignerPoseKind::Frication);
      if (!identity) { record(seam::core::Result<void>{identity.error()}); return; }
      if (identity.value()) {
        const auto added=designer_.addFrication(epoch,revision,identity.value()->phone,identity.value()->style);
        record(added); if (added) selectAddedDesignerSource(false);
      }
      return;
    }
    if (event.key == Key::R && event.modifiers.primaryShortcut()) {
      if (event.modifiers.shift) {
        if (const auto plosive=selectedDesignerPlosive()) { record(editDesignerPlosiveSeed(*plosive)); return; }
        const auto index = selectedDesignerFrication();
        record(index ? editDesignerFricationSeed(*index) : seam::core::failure(seam::core::ErrorCode::InvalidState, "Select a frication control before editing its seed"));
        return;
      }
      const auto epoch = designer_.epoch(); const auto revision = model->revision();
      auto dialog = platform_.fileDialog();
      const auto seed = dialog->chooseDesignerSeed(std::to_string(model->recipe().seed));
      if (!seed) { record(seam::core::Result<void>{seed.error()}); return; }
      if (seed.value()) record(designer_.setSeed(epoch, revision, *seed.value()));
      return;
    }
    if (event.key == Key::L && event.modifiers.primaryShortcut()) {
      const auto epoch = designer_.epoch(); const auto revision = model->revision();
      const auto sourcePose = designer_.auditionPose();
      if (event.modifiers.shift) { record(designer_.removePose(epoch, revision)); return; }
      auto dialog = platform_.fileDialog();
      const auto identity = dialog->chooseDesignerPoseIdentity();
      if (!identity) { record(seam::core::Result<void>{identity.error()}); return; }
      if (identity.value()) record(designer_.duplicatePose(epoch, revision, identity.value()->phone, identity.value()->style, sourcePose));
      return;
    }
    if (event.modifiers.primaryShortcut() && (event.key == Key::Z || event.key == Key::Y)) {
      record(event.key == Key::Y || event.modifiers.shift ? designer_.redo(designer_.epoch(), model->revision())
                                                       : designer_.undo(designer_.epoch(), model->revision())); return;
    }
    const auto controlCount = designerControlCount(model->recipe(), designer_.auditionPose());
    designerControl_ = std::min(designerControl_, controlCount - 1U);
    if (event.key == Key::Up || (event.key == Key::Tab && event.modifiers.shift)) {
      designerControl_ = (designerControl_ + controlCount - 1U) % controlCount; return;
    }
    if (event.key == Key::Down || event.key == Key::Tab) { designerControl_ = (designerControl_ + 1U) % controlCount; return; }
    if (event.key == Key::Left || event.key == Key::Right) {
      if (designerControl_ == 3U || designerControl_ == 4U) {
        auto pose = designer_.auditionPose(); auto pitch = designer_.auditionPitch();
        if (designerControl_ == 3U) {
          const auto count = model->recipe().poses.size();
          pose = event.key == Key::Right ? (pose + 1U) % count : (pose + count - 1U) % count;
        } else {
          pitch = static_cast<std::uint8_t>(std::clamp(static_cast<int>(pitch) + (event.key == Key::Right ? 1 : -1), 36, 96));
        }
        record(designer_.selectAudition(designer_.epoch(), model->revision(), pose, pitch)); return;
      }
      const auto direction = event.key == Key::Right ? 1.0 : -1.0;
      const auto scale = event.modifiers.shift ? 0.1 : 1.0;
      record(designer_.edit(designer_.epoch(), model->revision(),
          adjustedDesignerRecipe(model->recipe(), designer_.auditionPose(), designerControl_, direction * scale)));
    }
  }

  std::string designerSemanticPrefix() const {
    const auto* model = designer_.model();
    return "designer." + std::to_string(designer_.epoch()) + "." + std::to_string(model ? model->revision() : 0U) + "." +
        std::to_string(designer_.auditionPose()) + "." + std::to_string(designer_.auditionPitch()) + ".";
  }
  double designerNumericValue(std::size_t control) const {
    const auto& recipe = designer_.model()->recipe();
    if (control == 0U) return recipe.phonation.openQuotient;
    if (control == 1U) return recipe.phonation.spectralTiltDbPerOctave;
    if (control == 2U) return recipe.phonation.aspiration;
    if (control == 3U) return static_cast<double>(designer_.auditionPose());
    if (control == 4U) return designer_.auditionPitch();
    if (control == 5U) return recipe.modulation.jitterCents;
    if (control == 6U) return recipe.modulation.shimmerAmount;
    if (control == 7U) return recipe.modulation.rateHz;
    const auto oralEnd = 8U + recipe.poses[designer_.auditionPose()].formants.size()*3U;
    const auto pose=designer_.auditionPose();
    const auto nasalStart=designerNasalStart(recipe,pose);
    if (control>=nasalStart) return nasalControlValue(recipe.poses[designer_.auditionPose()],control-nasalStart);
    if (control>=designerBreathStart(recipe,pose)) {
      const auto relative=control-designerBreathStart(recipe,pose);
      const auto& source=recipe.breaths[designerBreaths(recipe,pose)[relative/3U]].source;
      return relative%3U==0U?source.centerHz:relative%3U==1U?source.bandwidthHz:source.gain;
    }
    if (control>=designerApproximantStart(recipe,pose)) {
      return recipe.approximants[designerApproximants(recipe,pose)[control-designerApproximantStart(recipe,pose)]].transitionMilliseconds;
    }
    if (control>=designerVoicedAffricateStart(recipe,pose)) {
      const auto relative=control-designerVoicedAffricateStart(recipe,pose);
      const auto& affricate=recipe.voicedAffricates[designerVoicedAffricates(recipe,pose)[relative/10U]];
      const auto parameter=relative%10U;
      if (parameter<6U) {
        const auto& source=parameter<3U?affricate.burst:affricate.tail;
        return parameter%3U==0U?source.centerHz:parameter%3U==1U?source.bandwidthHz:source.gain;
      }
      return parameter==6U?affricate.burstMilliseconds:parameter==7U?affricate.closureVoicingGain:
          parameter==8U?affricate.closureLowpassHz:affricate.tailVoicingGain;
    }
    if (control>=designerAffricateStart(recipe,pose)) {
      const auto relative=control-designerAffricateStart(recipe,pose);
      const auto& affricate=recipe.affricates[designerAffricates(recipe,pose)[relative/7U]];
      const auto parameter=relative%7U;
      if (parameter<6U) {
        const auto& source=parameter<3U?affricate.burst:affricate.tail;
        return parameter%3U==0U?source.centerHz:parameter%3U==1U?source.bandwidthHz:source.gain;
      }
      return affricate.burstMilliseconds;
    }
    const auto plosiveStart=designerPlosiveStart(recipe,designer_.auditionPose());
    if (control>=plosiveStart) return plosiveControlValue(recipe.plosives[designerPlosives(recipe,designer_.auditionPose())[(control-plosiveStart)/4U]],(control-plosiveStart)%4U);
    if (control >= oralEnd) {
      const auto& frication = recipe.frications[designerFrications(recipe, designer_.auditionPose())[(control-oralEnd)/4U]];
      const auto& source=frication.source;
      return (control-oralEnd)%4U == 0U ? source.centerHz : ((control-oralEnd)%4U == 1U ? source.bandwidthHz : (control-oralEnd)%4U==2U?source.gain:frication.voicingGain.value_or(0.0));
    }
    const auto& band = recipe.poses[designer_.auditionPose()].formants[(control - 8U) / 3U];
    return (control - 8U) % 3U == 0U ? band.frequencyHz : ((control - 8U) % 3U == 1U ? band.bandwidthHz : band.gainDb);
  }
  std::string designerControlDescription(std::size_t control) const {
    const auto& recipe = designer_.model()->recipe();
    const auto poseIndex = designer_.auditionPose();
    const auto& pose = recipe.poses[poseIndex];
    switch (control) {
      case 0U: return "Sets the open phase of each synthesized vocal pulse. It changes the source spectrum, not note pitch or loudness.";
      case 1U: return "Tilts harmonic energy toward lower or higher partials. This shapes vocal brightness without changing the authored vowel resonances.";
      case 2U: return "Sets the aperiodic aspiration share in the voiced source. Higher values add breath noise while retaining a pitched component.";
      case 3U: return "Chooses which authored phone and style pose the audition uses. Select another pose to edit its own resonances.";
      case 4U: return "Sets the MIDI note used only for audition. The supported audition range is MIDI 36 through 96.";
      case 5U: return "Sets deterministic periodic pitch variation depth in cents for the audition source.";
      case 6U: return "Sets periodic amplitude modulation depth for the audition source; zero disables this modulation.";
      case 7U: return "Sets the rate in hertz for the source's periodic pitch and amplitude modulation; zero disables modulation.";
      default: break;
    }

    const auto oralEnd = 8U + pose.formants.size() * 3U;
    const auto nasalStart = designerNasalStart(recipe, poseIndex);
    if (control >= nasalStart) {
      switch (control - nasalStart) {
        case 0U: return "Sets how strongly this vowel pose couples to its nasal resonance and antiresonance.";
        case 1U: return pose.nasal ? "Sets the nasal resonance frequency in hertz." : "Sets the nasal resonance frequency in hertz; editing enables the nasal filter for this pose.";
        case 2U: return pose.nasal ? "Sets the bandwidth of the nasal resonance in hertz." : "Sets the nasal resonance bandwidth in hertz; editing enables the nasal filter for this pose.";
        case 3U: return pose.nasal ? "Sets the nasal antiresonance frequency in hertz." : "Sets the nasal antiresonance frequency in hertz; editing enables the nasal filter for this pose.";
        default: return pose.nasal ? "Sets the bandwidth of the nasal antiresonance in hertz." : "Sets the nasal antiresonance bandwidth in hertz; editing enables the nasal filter for this pose.";
      }
    }

    if (control >= designerBreathStart(recipe,poseIndex) && control < nasalStart) {
      const auto relative=control-designerBreathStart(recipe,poseIndex);
      const auto& breath=recipe.breaths[designerBreaths(recipe,poseIndex)[relative/3U]];
      switch (relative%3U) {
        case 0U: return "Sets the broadband breath-noise center frequency for phone " + breath.phone + " in hertz.";
        case 1U: return "Sets the broadband breath-noise bandwidth for phone " + breath.phone + " in hertz.";
        default: return "Sets the broadband breath-noise gain for phone " + breath.phone + ".";
      }
    }
    if (control >= designerApproximantStart(recipe,poseIndex)) {
      const auto& approximant=recipe.approximants[designerApproximants(recipe,poseIndex)[control-designerApproximantStart(recipe,poseIndex)]];
      return "Sets the resonance transition duration for contracted voiced phone " + approximant.phone + " in milliseconds.";
    }
    if (control >= designerVoicedAffricateStart(recipe,poseIndex)) {
      const auto relative=control-designerVoicedAffricateStart(recipe,poseIndex);
      const auto& affricate=recipe.voicedAffricates[designerVoicedAffricates(recipe,poseIndex)[relative/10U]];
      switch (relative%10U) {
        case 0U: return "Sets the voiced-affricate burst center frequency for phone " + affricate.phone + " in hertz.";
        case 1U: return "Sets the voiced-affricate burst bandwidth for phone " + affricate.phone + " in hertz.";
        case 2U: return "Sets the voiced-affricate burst gain for phone " + affricate.phone + ".";
        case 3U: return "Sets the voiced-affricate tail center frequency for phone " + affricate.phone + " in hertz.";
        case 4U: return "Sets the voiced-affricate tail bandwidth for phone " + affricate.phone + " in hertz.";
        case 5U: return "Sets the voiced-affricate tail noise gain for phone " + affricate.phone + ".";
        case 6U: return "Sets the prevoiced closure and burst boundary for phone " + affricate.phone + " in milliseconds.";
        case 7U: return "Sets voicing level during the voiced-affricate closure for phone " + affricate.phone + ".";
        case 8U: return "Sets the closure low-pass cutoff for voiced-affricate phone " + affricate.phone + " in hertz.";
        default: return "Sets the voiced component in the affricate tail for phone " + affricate.phone + ".";
      }
    }
    if (control >= designerAffricateStart(recipe,poseIndex)) {
      const auto relative=control-designerAffricateStart(recipe,poseIndex);
      const auto& affricate=recipe.affricates[designerAffricates(recipe,poseIndex)[relative/7U]];
      switch (relative%7U) {
        case 0U: return "Sets the affricate burst center frequency for phone " + affricate.phone + " in hertz.";
        case 1U: return "Sets the affricate burst bandwidth for phone " + affricate.phone + " in hertz.";
        case 2U: return "Sets the affricate burst gain for phone " + affricate.phone + ".";
        case 3U: return "Sets the affricate tail center frequency for phone " + affricate.phone + " in hertz.";
        case 4U: return "Sets the affricate tail bandwidth for phone " + affricate.phone + " in hertz.";
        case 5U: return "Sets the affricate tail noise gain for phone " + affricate.phone + ".";
        default: return "Sets the affricate burst duration for phone " + affricate.phone + " in milliseconds.";
      }
    }

    const auto fricationEnd = designerPlosiveStart(recipe, poseIndex);
    if (control >= fricationEnd) {
      const auto relative = control - fricationEnd;
      const auto indices = designerPlosives(recipe, poseIndex);
      const auto& plosive = recipe.plosives[indices[relative / 4U]];
      switch (relative % 4U) {
        case 0U: return "Sets the burst-noise center frequency for phone " + plosive.phone + " in hertz.";
        case 1U: return "Sets the burst-noise bandwidth for phone " + plosive.phone + " in hertz.";
        case 2U: return "Sets the burst-noise gain for phone " + plosive.phone + ".";
        default: return "Sets the burst duration for phone " + plosive.phone + " in milliseconds.";
      }
    }
    if (control >= oralEnd) {
      const auto relative = control - oralEnd;
      const auto indices = designerFrications(recipe, poseIndex);
      const auto& frication = recipe.frications[indices[relative / 4U]];
      switch (relative % 4U) {
        case 0U: return "Sets the noise center frequency for phone " + frication.phone + " in hertz.";
        case 1U: return "Sets the noise bandwidth for phone " + frication.phone + " in hertz.";
        case 2U: return "Sets the noise gain for phone " + frication.phone + ".";
        default: return "Sets the voiced component mixed into phone " + frication.phone + "; zero disables voicing.";
      }
    }
    const auto relative = control - 8U;
    const auto formantIndex = relative / 3U;
    switch (relative % 3U) {
      case 0U: return "Sets formant F" + std::to_string(formantIndex + 1U) + " frequency for phone " + pose.phone + " in hertz; this moves a vocal-tract resonance, not the sung note.";
      case 1U: return "Sets formant F" + std::to_string(formantIndex + 1U) + " bandwidth for phone " + pose.phone + " in hertz; narrower bands produce a more selective resonance.";
      default: return "Sets formant F" + std::to_string(formantIndex + 1U) + " relative gain for phone " + pose.phone + ".";
    }
  }
  std::string designerControlRange(std::size_t control) const {
    const auto& recipe = designer_.model()->recipe();
    const auto poseIndex = designer_.auditionPose();
    const auto& pose = recipe.poses[poseIndex];
    switch (control) {
      case 0U: return "Accepted range 0.05 to 0.95; arrow adjustment 0.01, Shift plus arrow 0.001.";
      case 1U: return "Accepted range -48 to 0 dB per octave; arrow adjustment 1, Shift plus arrow 0.1.";
      case 2U: return "Accepted range 0 to 1; arrow adjustment 0.01, Shift plus arrow 0.001.";
      case 3U: return "Integer pose index from 0 to " + std::to_string(recipe.poses.size() - 1U) + ".";
      case 4U: return "Integer MIDI note from 36 to 96.";
      case 5U: return "Accepted range 0 to 100 cents; arrow adjustment 0.5, Shift plus arrow 0.05.";
      case 6U: return "Accepted range 0 to 1; arrow adjustment 0.01, Shift plus arrow 0.001.";
      case 7U: return "Accepted range 0 to 20 hertz; arrow adjustment 0.1, Shift plus arrow 0.01.";
      default: break;
    }

    const auto oralEnd = 8U + pose.formants.size() * 3U;
    const auto nasalStart = designerNasalStart(recipe, poseIndex);
    if (control >= nasalStart) {
      switch (control - nasalStart) {
        case 0U: return "Accepted range 0 to 1; arrow adjustment 0.01, Shift plus arrow 0.001.";
        case 1U: return "Accepted range 50 to 4000 hertz; arrow adjustment 10.";
        case 2U: return "Accepted range 10 to 5000 hertz; arrow adjustment 5.";
        case 3U: return "Accepted range 50 to 16000 hertz; arrow adjustment 10.";
        default: return "Accepted range 10 to 5000 hertz; arrow adjustment 5.";
      }
    }

    const std::string sourceSpectrumRange = "Accepted center 80 to 16000 hertz; bandwidth 20 to 16000 hertz; center/bandwidth ratio 0.25 to 20; gain 0 to 0.25. Arrow adjustment: frequency/bandwidth 10 hertz, gain 0.005.";
    if (control >= designerBreathStart(recipe,poseIndex) && control < nasalStart) {
      switch ((control-designerBreathStart(recipe,poseIndex))%3U) {
        case 0U: return "Accepted range 80 to 16000 hertz; arrow adjustment 10. Center/bandwidth ratio must stay between 0.25 and 20.";
        case 1U: return "Accepted range 20 to 16000 hertz; arrow adjustment 10. Center/bandwidth ratio must stay between 0.25 and 20.";
        default: return "Accepted range 0 to 0.25; arrow adjustment 0.005.";
      }
    }
    if (control >= designerApproximantStart(recipe,poseIndex))
      return "Accepted range 5 to 200 milliseconds; arrow adjustment 1.";
    if (control >= designerVoicedAffricateStart(recipe,poseIndex)) {
      const auto parameter=(control-designerVoicedAffricateStart(recipe,poseIndex))%10U;
      if (parameter<6U) return sourceSpectrumRange;
      if (parameter==6U) return "Accepted range 1 to 100 milliseconds; arrow adjustment 1.";
      if (parameter==7U) return "Accepted range greater than 0 and at most 0.5; arrow adjustment 0.01.";
      if (parameter==8U) return "Accepted range 40 to 2000 hertz; arrow adjustment 10.";
      return "Accepted range greater than 0 and at most 1; arrow adjustment 0.01.";
    }
    if (control >= designerAffricateStart(recipe,poseIndex)) {
      const auto parameter=(control-designerAffricateStart(recipe,poseIndex))%7U;
      if (parameter<6U) return sourceSpectrumRange;
      return "Accepted range 1 to 100 milliseconds; arrow adjustment 1.";
    }

    const auto plosiveStart = designerPlosiveStart(recipe, poseIndex);
    if (control >= plosiveStart) {
      switch ((control - plosiveStart) % 4U) {
        case 0U: return "Accepted range 80 to 16000 hertz; arrow adjustment 10. The center-to-bandwidth ratio must remain between 0.25 and 20.";
        case 1U: return "Accepted range 20 to 16000 hertz; arrow adjustment 10. The center-to-bandwidth ratio must remain between 0.25 and 20.";
        case 2U: return "Accepted range 0 to 0.25; arrow adjustment 0.005.";
        default: return "Accepted range 1 to 100 milliseconds; arrow adjustment 1.";
      }
    }
    if (control >= oralEnd) {
      switch ((control - oralEnd) % 4U) {
        case 0U: return "Accepted range 80 to 16000 hertz; arrow adjustment 10. The center-to-bandwidth ratio must remain between 0.25 and 20.";
        case 1U: return "Accepted range 20 to 16000 hertz; arrow adjustment 10. The center-to-bandwidth ratio must remain between 0.25 and 20.";
        case 2U: return "Accepted range 0 to 0.25; arrow adjustment 0.005.";
        default: return "Zero disables voicing; an enabled voiced component must be greater than 0 and at most 1. Arrow adjustment 0.01.";
      }
    }

    const auto relative = control - 8U;
    const auto formantIndex = relative / 3U;
    switch (relative % 3U) {
      case 0U: {
        auto bounds = formantIndex == 0U ? std::string{"at least 50"}
            : "above " + std::to_string(pose.formants[formantIndex - 1U].frequencyHz);
        bounds += formantIndex + 1U == pose.formants.size()
            ? " and at most 16000 hertz"
            : " and below " + std::to_string(pose.formants[formantIndex + 1U].frequencyHz) + " hertz";
        return "Accepted frequency is " + bounds + "; arrow adjustment 10. Formants must stay strictly ordered.";
      }
      case 1U: return "Accepted range 10 to 5000 hertz; arrow adjustment 5.";
      default: return "Accepted range -48 to 24 dB; arrow adjustment 0.5.";
    }
  }
  seam::core::Result<void> openDesignerProducerWorkspace() {
    if (designer_.busy() || designerDrag_ || controller_.proceduralImportBusy() || recording_.armed() || recording_.recordedFrames()>0U)
      return seam::core::failure(seam::core::ErrorCode::Conflict,"Finish active Designer or producer work before opening a workspace");
    if (controller_.productionProject() || !controller_.manifest().units.empty()) { designerView_=false; return seam::core::success(); }
    const auto epoch=designer_.epoch(), revision=designer_.model()?designer_.model()->revision():0U;
    const auto producerEpoch=controller_.productionSessionEpoch();
    auto dialog=platform_.fileDialog();
    const auto input=dialog->chooseProductionWorkspace();
    if (!input) return seam::core::Result<void>{input.error()};
    if (!input.value()) return seam::core::success();
    const auto valid=input.value()->validate(); if (!valid) return valid;
    if (epoch!=designer_.epoch() || revision!=(designer_.model()?designer_.model()->revision():0U) ||
        producerEpoch!=controller_.productionSessionEpoch() || designer_.busy() || controller_.proceduralImportBusy())
      return seam::core::failure(seam::core::ErrorCode::Conflict,"Workspace opening context changed while the dialog was open");
    // A producer folder's verified inventory.json supplies the digest.
    const auto opened=input.value()->producerFolder
        ? controller_.beginOpenProducerFolder(input.value()->root,input.value()->operatorId)
        : controller_.beginOpenProductionProject(input.value()->root,input.value()->inventorySha256,input.value()->operatorId);
    if (opened) designerView_=false;
    return opened;
  }
  bool canCreateProducerWorkspace() const noexcept {
    return !controller_.productionProject() && controller_.manifest().units.empty();
  }
  seam::core::Result<void> createDesignerProducerWorkspace() {
    if (designer_.busy() || designerDrag_ || controller_.proceduralImportBusy() || recording_.armed() || recording_.recordedFrames()>0U)
      return seam::core::failure(seam::core::ErrorCode::Conflict,"Finish active Designer or producer work before creating a workspace");
    if (!canCreateProducerWorkspace())
      return seam::core::failure(seam::core::ErrorCode::Conflict,"A producer workspace or bank is already open in this window");
    const auto epoch=designer_.epoch(), revision=designer_.model()?designer_.model()->revision():0U;
    const auto producerEpoch=controller_.productionSessionEpoch();
    auto dialog=platform_.fileDialog();
    const auto input=dialog->chooseNewProducerWorkspace();
    if (!input) return seam::core::Result<void>{input.error()};
    if (!input.value()) return seam::core::success();
    const auto valid=input.value()->validate(); if (!valid) return valid;
    if (epoch!=designer_.epoch() || revision!=(designer_.model()?designer_.model()->revision():0U) ||
        producerEpoch!=controller_.productionSessionEpoch() || designer_.busy() || controller_.proceduralImportBusy() ||
        !canCreateProducerWorkspace())
      return seam::core::failure(seam::core::ErrorCode::Conflict,"Workspace creation context changed while the dialog was open");
    const auto started=controller_.beginCreateProductionProject(input.value()->destination,input.value()->projectId,
        input.value()->producerId,{},
        input.value()->inventory==seam::platform::IFileDialog::NewProducerWorkspaceInput::Inventory::JapaneseVowelStarter
            ?seam::voicebank_production::DraftInventoryPreset::JapaneseVowelStarter
            :seam::voicebank_production::DraftInventoryPreset::JapaneseFull);
    if (started) designerView_=false;
    return started;
  }
  static seam::ui::Rect designerEntryBounds(std::size_t index,double width) {
    return {24.0,174.0+static_cast<double>(index)*62.0,std::max(0.0,std::min(width-48.0,440.0)),50.0};
  }
  void rebuildDesignerAccessibility(const std::vector<std::string>& values, std::size_t first, double width, double height) {
    using namespace seam::native_ui;
    const auto layout = voiceDesignerLayout(height);
    const auto prefix = designerSemanticPrefix();
    const bool enabled = !designer_.busy() && !(designer_.model() && designer_.model()->gestureActive());
    SemanticNode root{.id = prefix + "root", .role = SemanticRole::Panel, .name = "Voice Designer draft",
        .bounds = {0.0,0.0,width,height}};
    const std::pair<const char*, const char*> buttons[]{{"new","New voice"},{"open","Open voice"},{"save","Save voice"},
        {"back","Back to producer"},{"previous","Previous controls"},{"next","Next controls"}};
    const auto buttonWidth = std::max(1.0, (width - 48.0) / 6.0);
    for (std::size_t index = 0U; index < 6U; ++index) {
      const auto available = enabled && ((index != 2U && index < 4U) || designer_.model()) &&
          (index != 4U || first > 0U) && (index != 5U || first + layout.visibleRows < values.size());
      root.children.push_back({.id = prefix + buttons[index].first, .role = SemanticRole::Button, .name = index==3U && !controller_.productionProject() && controller_.manifest().units.empty()?"Open producer workspace":buttons[index].second,
          .bounds = !designer_.model() && (index<2U || index==3U) ? designerEntryBounds(index==3U?2U:index,width) : seam::ui::Rect{24.0 + static_cast<double>(index) * buttonWidth,58.0,buttonWidth,20.0}, .enabled = available,
          .actions = available ? std::vector<SemanticAction>{SemanticAction::Activate, SemanticAction::SetFocus} : std::vector<SemanticAction>{},
          .description = index==0U ? "Creates a Japanese source-filter draft covering symbols emitted by the built-in Japanese phonemizer; screening defaults are not a qualified singer."
              : index==3U && canCreateProducerWorkspace() ? "Cmd/Ctrl-D. Opens a producer folder created by Studio, or an existing producer workspace with its inventory digest."
              : std::string{}});
    }
    if (!designer_.model() && canCreateProducerWorkspace()) {
      root.children.push_back({.id = prefix + "create-producer", .role = SemanticRole::Button, .name = "Create producer workspace",
          .bounds = designerEntryBounds(3U,width), .enabled = enabled,
          .actions = enabled ? std::vector<SemanticAction>{SemanticAction::Activate, SemanticAction::SetFocus} : std::vector<SemanticAction>{},
          .description = "Cmd/Ctrl-Shift-D. Creates a new folder with a generated Japanese draft inventory, its recording script and an initialized producer workspace. Nothing is recorded or approved."});
    }
    if (designer_.model()) {
      const auto seed = std::to_string(designer_.model()->recipe().seed);
      root.children.push_back({.id = prefix+"seed", .role = SemanticRole::TextField, .name = "Reproducible voice seed",
          .value = seed, .bounds = {width*0.55,24.0,width*0.45-24.0,24.0}, .enabled = enabled,
          .actions = {SemanticAction::SetFocus,SemanticAction::EditText}, .editableValue = seed,
          .description = "Exact unsigned 64-bit decimal integer, 0 to 18446744073709551615"});
      const auto addAction = [&](const char* id, const char* name, bool available, double x, double y,
                                 double actionWidth, std::string_view description = {}) {
        root.children.push_back({.id = prefix + id, .role = SemanticRole::Button, .name = name,
            .bounds = y < 0.0 ? seam::ui::Rect{} : seam::ui::Rect{x,y,actionWidth,20.0}, .enabled = available,
            .actions = available ? std::vector<SemanticAction>{SemanticAction::Activate,SemanticAction::SetFocus} : std::vector<SemanticAction>{},
            .description = std::string{description}});
      };
      const auto smallWidth = std::max(1.0, (width-48.0)/7.0);
      addAction("save-as", "Save voice as new file", enabled, 24.0,82.0,smallWidth);
      addAction("duplicate-pose", "Duplicate selected pose", enabled, 24.0+smallWidth,82.0,smallWidth);
      addAction("remove-pose", "Remove selected pose", enabled && designer_.model()->recipe().poses.size()>1U, 24.0+smallWidth*2.0,82.0,smallWidth);
      addAction("undo", "Undo Designer edit", enabled && designer_.model()->canUndo(), 24.0+smallWidth*3.0,82.0,smallWidth);
      addAction("redo", "Redo Designer edit", enabled && designer_.model()->canRedo(), 24.0+smallWidth*4.0,82.0,smallWidth);
      addAction("publish-singer", "Publish signed singer package", enabled && !designer_.model()->dirty() &&
          !designer_.path().empty(), 24.0+smallWidth*5.0,82.0,smallWidth,
          "Publish the saved recipe as a .seamsinger package. Requires explicit version, display name, language and private signing key. A valid signature does not mean the voice was reviewed or quality-approved. Shortcut: Command/Control-Option-P.");
      // After installation the same slot becomes the return-to-song action, so the next step is where
      // the finished one was instead of an extra control squeezed into the row.
      if (publishedSingerInstalled_ && installedSinger_.has_value())
        addAction("open-in-song-editor", "Open in song editor", enabled &&
            designer_.epoch() == publishedDesignerEpoch_ &&
            designer_.model()->revision() == publishedDesignerRevision_ && !designer_.model()->dirty(),
            24.0+smallWidth*6.0,82.0,smallWidth,
            "Save a new song project bound to the exact singer just installed and open it in Project SEAM. An existing project is never replaced. Opening a song is not voice-quality approval. Shortcut: Command/Control-Option-O.");
      else
        addAction("install-published-singer", "Install published singer", enabled &&
            !publishedSingerPackagePath_.empty() && publishedSingerPublicKey_.has_value() &&
            !publishedSingerPackageDigest_.empty() &&
            !publishedSingerInstalled_ && designer_.epoch() == publishedDesignerEpoch_ &&
            designer_.model()->revision() == publishedDesignerRevision_ && !designer_.model()->dirty(),
            24.0+smallWidth*6.0,82.0,smallWidth,
            "Install the exact signed package just published into the current user's default standalone singer folder. Trusts the signing key explicitly selected for publication. Installation is not voice-quality approval. Shortcut: Command/Control-Option-I.");
      const auto audioWidth = std::max(1.0, (width-48.0)/7.0);
      addAction("render", "Render current audition", enabled && !designer_.auditionBusy(), 24.0,layout.helpY,audioWidth);
      addAction("play-current", "Play current B", enabled && static_cast<bool>(designer_.auditionAudio()), 24.0+audioWidth,layout.helpY,audioWidth);
      addAction("pin-reference", "Pin ready audition as reference A", enabled && !designer_.auditionBusy() && static_cast<bool>(designer_.auditionAudio()), 24.0+audioWidth*2.0,layout.helpY,audioWidth);
      addAction("play-reference", "Play reference A", enabled && designer_.referenceMatchesSelection(), 24.0+audioWidth*3.0,layout.helpY,audioWidth);
      addAction("clear-reference", "Clear reference A", enabled && designer_.auditionReference().has_value(), 24.0+audioWidth*4.0,layout.helpY,audioWidth);
      addAction("stop", "Stop audition playback", audition_.active(), 24.0+audioWidth*5.0,layout.helpY,audioWidth);
      addAction("cancel-preview", "Cancel audition render", designer_.auditionBusy(), 24.0+audioWidth*6.0,layout.helpY,audioWidth);
      addAction("prepare-draft", "Prepare generation job from current draft", enabled && controller_.productionProject() &&
          controller_.selectedProductionAssignment() && !controller_.proceduralImportBusy() && !recording_.armed() && recording_.recordedFrames() == 0U,
          24.0,layout.prepareY,width-48.0);
      const auto sourceWidth = std::max(1.0,(width-48.0)/6.0);
      addAction("add-plosive", "Add plosive source", enabled,24.0+sourceWidth*5.0,layout.fricationY,sourceWidth);
      if (const auto index=selectedDesignerPlosive()) {
        const auto preview=seam::native_ui::designerNoisePreviewAvailability(designer_.model()->recipe(),designer_.auditionPose(),*index,true);
        addAction(("remove-plosive."+std::to_string(*index)).c_str(),"Remove selected plosive source",enabled,24.0+sourceWidth,layout.fricationY,sourceWidth);
        addAction(("plosive-seed."+std::to_string(*index)).c_str(),"Edit selected plosive seed",enabled,24.0+sourceWidth*2.0,layout.fricationY,sourceWidth);
        addAction(("render-plosive."+std::to_string(*index)).c_str(),"Render selected plosive",enabled && !designer_.auditionBusy(),24.0+sourceWidth*3.0,layout.fricationY,sourceWidth/3.0);
        addAction(("render-plosive-phrase."+std::to_string(*index)).c_str(),preview.context?"Render plosive with selected vowel":"Select a vowel pose for CV preview",enabled && !designer_.auditionBusy() && preview.context,24.0+sourceWidth*(3.0+1.0/3.0),layout.fricationY,sourceWidth/3.0);
        addAction(("render-plosive-coda."+std::to_string(*index)).c_str(),preview.context?"Render selected vowel then plosive":"Select a vowel pose for VC preview",enabled && !designer_.auditionBusy() && preview.context,24.0+sourceWidth*(3.0+2.0/3.0),layout.fricationY,sourceWidth/3.0);
        addAction(("play-plosive."+std::to_string(*index)).c_str(),"Play selected plosive",enabled && designer_.plosiveAudio() && designer_.plosiveAudioIndex()==index,24.0+sourceWidth*4.0,layout.fricationY,sourceWidth);
      }
      addAction("add-frication", "Add frication source", enabled, 24.0,layout.fricationY,sourceWidth);
      if (const auto index = selectedDesignerFrication()) {
        const auto preview=seam::native_ui::designerNoisePreviewAvailability(designer_.model()->recipe(),designer_.auditionPose(),*index,false);
        addAction(("remove-frication."+std::to_string(*index)).c_str(), "Remove selected frication source", enabled, 24.0+sourceWidth,layout.fricationY,sourceWidth);
        addAction(("frication-seed."+std::to_string(*index)).c_str(), "Edit selected frication seed", enabled, 24.0+sourceWidth*2.0,layout.fricationY,sourceWidth);
        addAction(("render-frication."+std::to_string(*index)).c_str(), preview.source?"Render selected frication":"Voiced source requires CV or VC preview", enabled && !designer_.auditionBusy() && preview.source,24.0+sourceWidth*3.0,layout.fricationY,sourceWidth/3.0);
        addAction(("render-frication-phrase."+std::to_string(*index)).c_str(), preview.context?"Render frication with selected vowel":"Select a vowel pose for CV preview", enabled && !designer_.auditionBusy() && preview.context,24.0+sourceWidth*(3.0+1.0/3.0),layout.fricationY,sourceWidth/3.0);
        addAction(("render-frication-coda."+std::to_string(*index)).c_str(), preview.context?"Render selected vowel then frication":"Select a vowel pose for VC preview", enabled && !designer_.auditionBusy() && preview.context,24.0+sourceWidth*(3.0+2.0/3.0),layout.fricationY,sourceWidth/3.0);
        addAction(("play-frication."+std::to_string(*index)).c_str(), "Play selected frication", enabled && designer_.fricationAudio() && designer_.fricationAudioIndex() == index,
            24.0+sourceWidth*4.0,layout.fricationY,sourceWidth);
      }
      if (const auto phone=selectedDesignerArticulation()) {
        const auto suffix="articulation."+*phone;
        addAction(("render-"+suffix).c_str(),("Render "+*phone+" with selected vowel").c_str(),
            enabled && !designer_.auditionBusy(),24.0+sourceWidth*3.0,layout.fricationY,sourceWidth);
        addAction(("play-"+suffix).c_str(),("Play "+*phone+" + vowel").c_str(),
            enabled && designer_.articulationAudio() && designer_.articulationAudioPhone()==phone,
            24.0+sourceWidth*4.0,layout.fricationY,sourceWidth);
      }
      root.children.push_back({.id = prefix+"audition-state", .role = SemanticRole::Status, .name = "Voice Designer status",
          .value = designer_.auditionBusy() ? "Rendering" : (!auditionStatus_.empty() ? auditionStatus_ :
              (!designerPublishStatus_.empty() ? designerPublishStatus_ :
              (designer_.plosiveAudio() && designer_.plosiveAudioIndex()==selectedDesignerPlosive()?(designer_.plosiveAudioIsCoda()?"Vowel and stop ready":designer_.plosiveAudioIsPhrase()?"Stop and vowel ready":"Plosive source ready"):
              designer_.fricationAudio() && designer_.fricationAudioIndex()==selectedDesignerFrication()?fricationPreviewDescription()+" ready":
              designer_.articulationAudio() && designer_.articulationAudioPhone()==selectedDesignerArticulation()?*designer_.articulationAudioPhone()+" + vowel ready":
              designer_.auditionAudio() ? "Vowel ready" : "Not rendered"))),
          .bounds = {24.0,layout.statusY,width-48.0,24.0}});
      if (const auto& reference = designer_.auditionReference()) root.children.push_back({.id = prefix+"reference-state",
          .role = SemanticRole::Status, .name = "Reference A identity",
          .value = reference->phone+" / "+reference->style+" / MIDI "+std::to_string(reference->midiKey)+" / "+reference->resource.identity.contentHash,
          .bounds = layout.compact ? seam::ui::Rect{} : seam::ui::Rect{24.0,layout.referenceY,width-48.0,24.0}});
      root.children.push_back({.id = prefix + "status", .role = SemanticRole::Status, .name = "Draft save state",
          .value = designer_.model()->dirty() ? "Unsaved" : "Saved", .bounds = {24.0,112.0,width-48.0,24.0}});
      for (std::size_t index = first; index < std::min(values.size(), first + layout.visibleRows); ++index) {
        const auto value = std::to_string(designerNumericValue(index));
        root.children.push_back({.id = prefix + "control." + std::to_string(index), .role = SemanticRole::TextField,
            .name = values[index], .value = value, .bounds = {24.0,layout.controlsTop + static_cast<double>(index-first)*layout.rowHeight,width-48.0,layout.rowHeight},
            .enabled = enabled, .actions = {SemanticAction::SetFocus, SemanticAction::EditText}, .editableValue = value,
            .description = designerControlDescription(index) + " " + designerControlRange(index)});
      }
    }
    if (!lastError_.empty()) root.children.push_back({.id = prefix + "error", .role = SemanticRole::Status,
        .name = "Designer error", .value = lastError_, .bounds = {24.0,designer_.model()?layout.errorY:406.0,width-48.0,24.0}});
    const auto focus = designerSemanticFocus_.starts_with(prefix) && EditorSemanticTree::containsId(root, designerSemanticFocus_)
        ? designerSemanticFocus_ : prefix + "control." + std::to_string(designerControl_);
    designerAccessibility_.rebuildCustom(std::move(root), focus);
  }

  static std::string designerButtonLabel(std::string_view suffix) {
    const std::pair<std::string_view,std::string_view> labels[]{
      {"new","New"},{"open","Open"},{"save","Save"},{"back","Producer"},{"previous","Previous"},{"next","Next"},
      {"save-as","Save as"},{"duplicate-pose","Duplicate"},{"remove-pose","Remove pose"},{"undo","Undo"},{"redo","Redo"},{"publish-singer","Publish"},{"install-published-singer","Install"},{"open-in-song-editor","Song editor"},
      {"render","Render"},{"play-current","Play B"},{"pin-reference","Pin A"},{"play-reference","Play A"},
      {"clear-reference","Clear A"},{"stop","Stop"},{"cancel-preview","Cancel"},{"prepare-draft","Prepare generation job"},
      {"add-plosive","Add stop"},{"add-frication","Add noise"}};
    for (const auto& [id,label]:labels) if (suffix==id) return std::string{label};
    if (suffix.starts_with("render-plosive-coda.")) return "VC";
    if (suffix.starts_with("render-plosive-phrase.")) return "CV";
    if (suffix.starts_with("render-plosive.")) return "Src";
    if (suffix.starts_with("render-frication.")) return "Src";
    if (suffix.starts_with("render-frication-phrase.")) return "CV";
    if (suffix.starts_with("render-frication-coda.")) return "VC";
    if (suffix.starts_with("render-articulation.")) return "CV";
    if (suffix.starts_with("play-articulation.")) return "Play";
    if (suffix.starts_with("play-")) return "Play";
    if (suffix.starts_with("remove-")) return "Remove";
    if (suffix.find("-seed.")!=std::string_view::npos) return "Seed";
    return {};
  }
  void paintDesignerButtons(seam::native_ui::RasterCanvas& canvas) {
    using namespace seam::native_ui;
    const auto prefix=designerSemanticPrefix();
    for (const auto& node:designerAccessibility_.root().children) {
      if (node.role!=SemanticRole::Button || !node.id.starts_with(prefix) || node.bounds.width<=0.0 || node.bounds.height<=0.0) continue;
      const auto label=designerButtonLabel(std::string_view{node.id}.substr(prefix.size()));
      if (label.empty()) continue;
      const auto& bounds=node.bounds;
      canvas.fillRect({bounds.x+1.0,bounds.y,bounds.width-2.0,bounds.height},node.enabled?Color{61,42,63,255}:Color{31,28,34,255});
      canvas.drawText({bounds.x+4.0,bounds.y+2.0,std::max(0.0,bounds.width-8.0),bounds.height-2.0},label,
          node.enabled?Color{239,233,241,255}:Color{125,118,129,255},12.0);
    }
  }
  void paintDesigner(seam::native_ui::RasterCanvas& canvas) {
    using seam::native_ui::Color;
    const auto layout = seam::native_ui::voiceDesignerLayout(canvas.logicalHeight());
    canvas.fillRect({0.0, 0.0, canvas.logicalWidth(), canvas.logicalHeight()}, Color{15,14,18,255});
    const auto line = [&](double y, std::string text, Color color = Color{220,212,224,255}) {
      if (y < 0.0 || (layout.compact && y == layout.statusY && !lastError_.empty() && text != lastError_)) return;
      canvas.drawText({24.0,y,std::max(0.0,canvas.logicalWidth()-48.0),24.0}, text, color, y==24.0?20.0:layout.textSize);
    };
    line(24.0, "VOICE DESIGNER / DRAFT RECIPE");
    if (!designer_.model()) {
      line(58.0, "CMD/CTRL-N NEW / O OPEN / S SAVE / SHIFT-S SAVE AS / D PRODUCER / SHIFT-D NEW PRODUCER");
      line(82.0, "CMD/CTRL-L DUPLICATE POSE / CMD/CTRL-SHIFT-L REMOVE (UNDOABLE)");
    }
    const auto* model = designer_.model();
    if (!model) {
      rebuildDesignerAccessibility({}, 0U, canvas.logicalWidth(), canvas.logicalHeight());
      line(112.0, "Create a draft or open a saved recipe to begin.");
      const bool creatable=canCreateProducerWorkspace();
      const std::array<const char*,4U> labels{"NEW VOICE DRAFT","OPEN SAVED RECIPE",
          creatable?"OPEN PRODUCER WORKSPACE":"BACK TO PRODUCER","NEW PRODUCER WORKSPACE"};
      for (std::size_t index=0U;index<(creatable?4U:3U);++index) {
        const auto bounds=designerEntryBounds(index,canvas.logicalWidth());
        canvas.fillRect(bounds,Color{58,39,59,255});
        canvas.drawText({bounds.x+16.0,bounds.y+13.0,bounds.width-32.0,24.0},labels[index],Color{239,233,241,255},16.0);
      }
      if (designer_.busy()) line(146.0, "OPENING VOICE / ESC CANCEL");
      if (!lastError_.empty()) line(designerEntryBounds(4U,canvas.logicalWidth()).y, lastError_, Color{193,115,160,255});
      return;
    }
    line(112.0, model->recipe().id + (model->dirty() ? " / UNSAVED" : " / SAVED"));
    canvas.drawText({canvas.logicalWidth()*0.55,24.0,canvas.logicalWidth()*0.45-24.0,24.0},
        "SEED " + std::to_string(model->recipe().seed) + " / CMD/CTRL-R", Color{220,212,224,255}, 10.0);
    line(136.0, "ARROWS OR DRAG ADJUST / SHIFT FINE / ESC CANCEL / CMD-Z UNDO");
    std::vector<std::string> values{"OPEN QUOTIENT " + std::to_string(model->recipe().phonation.openQuotient),
        "SPECTRAL TILT " + std::to_string(model->recipe().phonation.spectralTiltDbPerOctave) + " dB/OCT",
        "ASPIRATION " + std::to_string(model->recipe().phonation.aspiration),
        "AUDITION POSE " + model->recipe().poses[designer_.auditionPose()].phone + " / " + model->recipe().poses[designer_.auditionPose()].style,
        "AUDITION MIDI " + std::to_string(designer_.auditionPitch()),
        "PERIODIC PITCH DEPTH " + std::to_string(model->recipe().modulation.jitterCents) + " cents",
        "PERIODIC AMPLITUDE DEPTH " + std::to_string(model->recipe().modulation.shimmerAmount),
        "MODULATION RATE " + std::to_string(model->recipe().modulation.rateHz) + " Hz (0 = OFF)"};
    const auto& pose = model->recipe().poses[designer_.auditionPose()];
    for (std::size_t index = 0U; index < pose.formants.size(); ++index) {
      const auto label = "F" + std::to_string(index + 1U);
      values.push_back(label + " FREQUENCY " + std::to_string(pose.formants[index].frequencyHz) + " Hz");
      values.push_back(label + " BANDWIDTH " + std::to_string(pose.formants[index].bandwidthHz) + " Hz");
      values.push_back(label + " GAIN " + std::to_string(pose.formants[index].gainDb) + " dB");
    }
    for (const auto index : designerFrications(model->recipe(), designer_.auditionPose())) {
      const auto& frication = model->recipe().frications[index];
      values.push_back(frication.phone + " NOISE CENTER " + std::to_string(frication.source.centerHz) + " Hz");
      values.push_back(frication.phone + " NOISE BANDWIDTH " + std::to_string(frication.source.bandwidthHz) + " Hz");
      values.push_back(frication.phone + " NOISE GAIN " + std::to_string(frication.source.gain));
      values.push_back(frication.phone + " VOICING GAIN " + std::to_string(frication.voicingGain.value_or(0.0)) + " / 0 = OFF");
    }
    for (const auto index:designerPlosives(model->recipe(),designer_.auditionPose())) {
      const auto& plosive=model->recipe().plosives[index];
      const std::array<const char*,4U> labels{" BURST CENTER Hz "," BURST WIDTH Hz "," BURST GAIN "," BURST DURATION ms "};
      for (std::size_t parameter=0U;parameter<labels.size();++parameter)
        values.push_back(plosive.phone+labels[parameter]+std::to_string(plosiveControlValue(plosive,parameter)));
    }
    for (const auto index:designerAffricates(model->recipe(),designer_.auditionPose())) {
      const auto& source=model->recipe().affricates[index];
      values.push_back(source.phone+" BURST CENTER Hz "+std::to_string(source.burst.centerHz));
      values.push_back(source.phone+" BURST WIDTH Hz "+std::to_string(source.burst.bandwidthHz));
      values.push_back(source.phone+" BURST GAIN "+std::to_string(source.burst.gain));
      values.push_back(source.phone+" TAIL CENTER Hz "+std::to_string(source.tail.centerHz));
      values.push_back(source.phone+" TAIL WIDTH Hz "+std::to_string(source.tail.bandwidthHz));
      values.push_back(source.phone+" TAIL GAIN "+std::to_string(source.tail.gain));
      values.push_back(source.phone+" BURST DURATION ms "+std::to_string(source.burstMilliseconds));
    }
    for (const auto index:designerVoicedAffricates(model->recipe(),designer_.auditionPose())) {
      const auto& source=model->recipe().voicedAffricates[index];
      values.push_back(source.phone+" BURST CENTER Hz "+std::to_string(source.burst.centerHz));
      values.push_back(source.phone+" BURST WIDTH Hz "+std::to_string(source.burst.bandwidthHz));
      values.push_back(source.phone+" BURST GAIN "+std::to_string(source.burst.gain));
      values.push_back(source.phone+" TAIL CENTER Hz "+std::to_string(source.tail.centerHz));
      values.push_back(source.phone+" TAIL WIDTH Hz "+std::to_string(source.tail.bandwidthHz));
      values.push_back(source.phone+" TAIL NOISE GAIN "+std::to_string(source.tail.gain));
      values.push_back(source.phone+" CLOSURE/BURST ms "+std::to_string(source.burstMilliseconds));
      values.push_back(source.phone+" CLOSURE VOICING "+std::to_string(source.closureVoicingGain));
      values.push_back(source.phone+" CLOSURE LOWPASS Hz "+std::to_string(source.closureLowpassHz));
      values.push_back(source.phone+" TAIL VOICING "+std::to_string(source.tailVoicingGain));
    }
    for (const auto index:designerApproximants(model->recipe(),designer_.auditionPose())) {
      const auto& source=model->recipe().approximants[index];
      values.push_back(source.phone+" RESONANCE TRANSITION ms "+std::to_string(source.transitionMilliseconds));
    }
    for (const auto index:designerBreaths(model->recipe(),designer_.auditionPose())) {
      const auto& source=model->recipe().breaths[index];
      values.push_back(source.phone+" BREATH CENTER Hz "+std::to_string(source.source.centerHz));
      values.push_back(source.phone+" BREATH BANDWIDTH Hz "+std::to_string(source.source.bandwidthHz));
      values.push_back(source.phone+" BREATH GAIN "+std::to_string(source.source.gain));
    }
    const std::array<const char*,5U> nasalLabels{"NASAL COUPLING ","NASAL RESONANCE Hz ","NASAL RESONANCE WIDTH Hz ","NASAL ANTIRESONANCE Hz ","NASAL ANTIRESONANCE WIDTH Hz "};
    for (std::size_t i=0U;i<nasalLabels.size();++i) values.push_back(std::string{nasalLabels[i]}+std::to_string(nasalControlValue(pose,i))+
        (pose.nasal?"":" / MODEL OFF: EDIT TO ENABLE"));
    designerControl_ = std::min(designerControl_, values.size() - 1U);
    const auto first = designerDrag_ ? designerDrag_->firstRow : layout.firstRow(values.size(), designerControl_);
    rebuildDesignerAccessibility(values, first, canvas.logicalWidth(), canvas.logicalHeight());
    for (std::size_t index = first; index < std::min(values.size(), first + layout.visibleRows); ++index) {
      const auto y=layout.controlsTop+static_cast<double>(index-first)*layout.rowHeight;
      if (index==designerControl_) canvas.fillRect({20.0,y-2.0,canvas.logicalWidth()-40.0,layout.rowHeight-2.0},Color{41,29,43,255});
      line(y, (index == designerControl_ ? "> " : "  ") + values[index],
          index == designerControl_ ? Color{193,115,160,255} : Color{220,212,224,255});
    }
    line(layout.summaryY, "CONTROL " + std::to_string(designerControl_ + 1U) + "/" + std::to_string(values.size()) + " / POSE " + pose.phone + " / " + pose.style);
    paintDesignerButtons(canvas);
    if (const auto& reference = designer_.auditionReference())
      line(layout.referenceY, "A: " + reference->phone + " / " + reference->style + " MIDI " + std::to_string(reference->midiKey) + " / " + reference->resource.identity.contentHash.substr(0U, 12U));
    if (designer_.busy()) line(layout.statusY, "FILE OPERATION / ESC CANCEL");
    else if (designer_.auditionBusy()) line(layout.statusY, "RENDERING PREVIEW / ESC CANCEL");
    else if (!auditionStatus_.empty()) line(layout.statusY, auditionStatus_);
    else if (!designerPublishStatus_.empty()) line(layout.statusY, designerPublishStatus_);
    else if (designer_.plosiveAudio() && designer_.plosiveAudioIndex()==selectedDesignerPlosive()) line(layout.statusY,designer_.plosiveAudioIsCoda()?"VOWEL + STOP READY / CMD-ALT-SPACE PLAY":designer_.plosiveAudioIsPhrase()?"STOP + VOWEL READY / CMD-SHIFT-SPACE PLAY":"STOP READY / CMD-SPACE / SHIFT CV / ALT VC");
    else if (designer_.fricationAudio() && designer_.fricationAudioIndex()==selectedDesignerFrication()) line(layout.statusY,fricationPreviewDescription()+" READY");
    else if (designer_.articulationAudio() && designer_.articulationAudioPhone()==selectedDesignerArticulation()) line(layout.statusY,*designer_.articulationAudioPhone()+" + VOWEL READY / CMD-SPACE PLAY");
    else if (designer_.auditionAudio()) line(layout.statusY, "POSE READY / SPACE PLAY");
    if (!lastError_.empty()) line(layout.errorY, lastError_, Color{193,115,160,255});
  }
  void stopAudition() noexcept {
    // A session whose device does not say that it has stopped stays active, still playing, and the
    // status stays what it is: the creator is told, and stopping again asks the device again.
    const auto stopped = audition_.stop();
    record(stopped);
    if (stopped) auditionStatus_.clear();
  }
  seam::core::Result<void> auditionCandidate(bool selectedGesture) {
    if (controller_.proceduralImportBusy() || recording_.armed() || recording_.recordedFrames() > 0U)
      return seam::core::failure(seam::core::ErrorCode::Conflict, "Finish recording or candidate work before audition");
    const auto& waveform = controller_.candidateWaveform();
    const auto& preview = controller_.candidateMarkerPreview();
    if (!waveform || !waveform->audio || !preview) return seam::core::failure(
        seam::core::ErrorCode::InvalidState, "Press P to verify and load the candidate before audition");
    std::size_t begin = 0U, end = waveform->audio->frameCount();
    if (selectedGesture) {
      const auto& marker = preview->markers[controller_.selectedCandidateMarker()];
      begin = static_cast<std::size_t>(marker.ownedSpan.start);
      end = static_cast<std::size_t>(marker.ownedSpan.end);
    }
    double peak = 0.0;
    for (const auto& [low, high] : waveform->peaks)
      peak = std::max({peak, std::abs(static_cast<double>(low)), std::abs(static_cast<double>(high))});
    const auto gain = static_cast<float>(peak > 0.0 ? std::min(0.25, 0.9 / peak) : 0.25);
    stopAudition();
    const auto started = audition_.start(platform_.audioDevice(), waveform->audio, begin, end, gain);
    if (!started) { stopAudition(); return started; }
    auditionStatus_ = selectedGesture ? "AUDITION: RAW GESTURE" : "AUDITION: RAW TAKE";
    return seam::core::success();
  }
  // Campaign planning and running. The recipe is an explicit file choice and the
  // destination names a new folder: the controller publishes one immutable
  // definition there and refuses an existing directory, so a stored campaign is
  // advanced or resumed rather than replaced.
  seam::core::Result<void> planCampaignFromDialog() {
    if (controller_.proceduralImportBusy() || recording_.armed() || recording_.recordedFrames() > 0U || designer_.busy())
      return seam::core::failure(seam::core::ErrorCode::Conflict, "Finish recording, Designer work or production work before campaign planning");
    const auto* project = controller_.productionProject();
    if (project == nullptr)
      return seam::core::failure(seam::core::ErrorCode::InvalidState, "Open a producer workspace first");
    std::vector<std::string> takeIds;
    for (const auto& row : project->unitAssignments)
      if (!row.plannedTakeId.empty() && std::find(takeIds.begin(), takeIds.end(), row.plannedTakeId) == takeIds.end())
        takeIds.push_back(row.plannedTakeId);
    if (takeIds.empty())
      return seam::core::failure(seam::core::ErrorCode::InvalidState, "The producer has no planned take ids to generate");
    const auto epoch = controller_.productionSessionEpoch();
    const auto generation = project->lastDurableGeneration;
    const auto selected = controller_.selectedIndex();
    auto dialog = platform_.fileDialog();
    const auto recipe = dialog->choose({.purpose = seam::platform::FileDialogPurpose::SelectProceduralRecipe,
        .title = "Select the Campaign Recipe", .initialDirectory = {}, .suggestedName = {}, .extensions = {"json"}});
    if (!recipe) return seam::core::Result<void>{recipe.error()};
    if (!recipe.value()) return seam::core::success();
    const auto afterRecipe = controller_.validateProductionImportContext(epoch, generation, selected);
    if (!afterRecipe) return afterRecipe;
    const auto destination = dialog->choose({.purpose = seam::platform::FileDialogPurpose::PlanGenerationCampaign,
        .title = "New Generation Campaign Folder", .initialDirectory = recipe.value()->parent_path(),
        .suggestedName = "campaign", .extensions = {}});
    if (!destination) return seam::core::Result<void>{destination.error()};
    if (!destination.value()) return seam::core::success();
    const auto afterDestination = controller_.validateProductionImportContext(epoch, generation, selected);
    if (!afterDestination) return afterDestination;
    return controller_.beginGenerationCampaignPlan(*recipe.value(), takeIds, *destination.value());
  }

  seam::core::Result<void> runCampaignFromDialog() {
    if (controller_.proceduralImportBusy() || recording_.armed() || recording_.recordedFrames() > 0U || designer_.busy())
      return seam::core::failure(seam::core::ErrorCode::Conflict, "Finish recording, Designer work or production work before a campaign run");
    if (controller_.generationCampaignPath().empty()) {
      const auto* project = controller_.productionProject();
      if (project == nullptr) return seam::core::failure(seam::core::ErrorCode::InvalidState,
          "Open a producer workspace before resuming a generation campaign");
      const auto epoch = controller_.productionSessionEpoch();
      const auto generation = project->lastDurableGeneration;
      const auto selected = controller_.selectedIndex();
      auto dialog = platform_.fileDialog();
      const auto path = dialog->choose({.purpose = seam::platform::FileDialogPurpose::OpenGenerationCampaign,
          .title = "Open and Resume Generation Campaign", .initialDirectory = {},
          .suggestedName = "campaign.json", .extensions = {"json"}});
      if (!path) return seam::core::Result<void>{path.error()};
      if (!path.value()) return seam::core::success();
      const auto current = controller_.validateProductionImportContext(epoch, generation, selected);
      if (!current) return current;
      if (recording_.armed() || recording_.recordedFrames() > 0U)
        return seam::core::failure(seam::core::ErrorCode::Conflict,
            "Finish recording before resuming a generation campaign");
      const auto bytes = seam::core::readTextFileLimited(*path.value(), 32U * 1024U * 1024U);
      if (!bytes) return seam::core::Result<void>{bytes.error()};
      return controller_.beginGenerationCampaignAdvance(*path.value(), seam::core::sha256Hex(bytes.value()));
    }
    // One call covers a fresh campaign, a cancelled one and a completed one: the
    // controller advances from the identity it recorded, and the service re-verifies
    // receipts instead of re-planning.
    return controller_.beginGenerationCampaignResume();
  }

  // Renders the campaign's held-out phrases. A campaign that has not cleared its own preflight
  // cannot be advanced, so this is the step between planning and generating. It generates and
  // collects nothing, and a failed report is reported as a failure rather than a completed step.
  seam::core::Result<void> preflightCampaignFromDialog() {
    if (controller_.proceduralImportBusy() || recording_.armed() || recording_.recordedFrames() > 0U ||
        designer_.busy())
      return seam::core::failure(seam::core::ErrorCode::Conflict,
          "Finish recording, Designer work or production work before a campaign preflight");
    if (controller_.generationCampaignPath().empty())
      return seam::core::failure(seam::core::ErrorCode::InvalidState,
          "Plan a campaign or resume a retained one before rendering its preflight phrases");
    return controller_.beginGenerationCampaignPreflight(controller_.generationCampaignPath(),
                                                        controller_.generationCampaignSha256());
  }

  seam::core::Result<void> locateGenerationRequestDefinitionFromDialog(std::string_view requestId) {
    if (generationModal_ || controller_.proceduralImportBusy() || recording_.armed() ||
        recording_.recordedFrames() > 0U || designer_.busy())
      return seam::core::failure(seam::core::ErrorCode::Conflict,
          "Finish recording, Designer work or production work before locating a campaign definition");
    const auto* project = controller_.productionProject();
    if (project == nullptr)
      return seam::core::failure(seam::core::ErrorCode::InvalidState,
          "Open the producer workspace before locating a campaign definition");
    const auto epoch = controller_.productionSessionEpoch();
    const auto generation = project->lastDurableGeneration;
    const auto selected = controller_.selectedIndex();
    struct ModalGuard final {
      bool& active;
      explicit ModalGuard(bool& value) : active(value) { active = true; }
      ~ModalGuard() { active = false; }
    } guard{generationModal_};
    auto dialog = platform_.fileDialog();
    const auto path = dialog->choose({.purpose = seam::platform::FileDialogPurpose::OpenGenerationCampaign,
        .title = "Locate Definition for Generation Request", .initialDirectory = {},
        .suggestedName = "campaign.json", .extensions = {"json"}});
    if (!path) return seam::core::Result<void>{path.error()};
    if (!path.value()) return seam::core::success();
    const auto current = controller_.validateProductionImportContext(epoch, generation, selected);
    if (!current) return current;
    if (recording_.armed() || recording_.recordedFrames() > 0U)
      return seam::core::failure(seam::core::ErrorCode::Conflict,
          "Finish recording before resuming a generation request");
    // A selected path is not trusted as identity. The worker checks its bytes
    // against the durable request ID before it can write to the producer.
    return controller_.beginGenerationRequestResumeFromDefinition(requestId, *path.value());
  }

  seam::core::Result<void> assembleBatchFromDialog() {
    if (controller_.proceduralImportBusy() || recording_.armed() || recording_.recordedFrames() > 0U)
      return seam::core::failure(seam::core::ErrorCode::Conflict, "Finish recording or candidate work before batch assembly");
    const auto* project = controller_.productionProject();
    if (!project || !controller_.selectedProductionAssignment())
      return seam::core::failure(seam::core::ErrorCode::InvalidState, "Select a producer assignment first");
    stopAudition();
    const auto epoch = controller_.productionSessionEpoch();
    const auto generation = project->lastDurableGeneration;
    const auto selected = controller_.selectedIndex();
    const auto validate = [&]() -> seam::core::Result<void> {
      if (recording_.armed() || recording_.recordedFrames() > 0U)
        return seam::core::failure(seam::core::ErrorCode::Conflict, "Finish recording before batch assembly");
      return controller_.validateProductionImportContext(epoch, generation, selected);
    };
    auto dialog = platform_.fileDialog();
    const auto references = dialog->chooseGenerationJobs();
    if (!references) return seam::core::Result<void>{references.error()};
    if (references.value().empty()) return seam::core::success();
    const auto afterSelection = validate(); if (!afterSelection) return afterSelection;
    const auto destination = dialog->choose({.purpose = seam::platform::FileDialogPurpose::PrepareGenerationBatch,
        .title = "Save New Generation Batch", .initialDirectory = {}, .suggestedName = "generation-batch.json", .extensions = {"json"}});
    if (!destination) return seam::core::Result<void>{destination.error()};
    if (!destination.value()) return seam::core::success();
    const auto afterDestination = validate(); if (!afterDestination) return afterDestination;
    return controller_.beginGenerationBatchAssembly(references.value(), *destination.value());
  }

  seam::core::Result<void> preparationFromDialog() {
    if (controller_.proceduralImportBusy() || recording_.armed() || recording_.recordedFrames() > 0U)
      return seam::core::failure(seam::core::ErrorCode::Conflict, "Finish recording or candidate work before preparation");
    const auto* project = controller_.productionProject();
    if (!project || !controller_.selectedProductionAssignment())
      return seam::core::failure(seam::core::ErrorCode::InvalidState, "Select a producer assignment first");
    stopAudition();
    auto dialog = platform_.fileDialog();
    if (auto selection = controller_.generationScoreSelection()) {
      const auto validate = [&]() -> seam::core::Result<void> {
        if (recording_.armed() || recording_.recordedFrames() > 0U)
          return seam::core::failure(seam::core::ErrorCode::Conflict, "Finish recording before preparation");
        return controller_.validateProductionImportContext(selection->epoch, selection->generation, selection->selectedIndex);
      };
      const auto current = validate();
      if (!current) {
        // Retain cancelled dialogs, not an obsolete producer assignment.
        static_cast<void>(controller_.takeGenerationScoreSelection());
        return current;
      }
      std::vector<std::string> labels;
      for (const auto& region : selection->regions) labels.push_back(region.label + (selection->selectedRecipe
          ? " / " + selection->selectedRecipe->resource.identity.id + " / " + selection->selectedRecipe->style : ""));
      const auto chosen = dialog->chooseGenerationRegion(labels);
      if (!chosen) return seam::core::Result<void>{chosen.error()};
      if (!chosen.value()) return seam::core::success();
      const auto afterRegion = validate(); if (!afterRegion) return afterRegion;
      if (*chosen.value() >= selection->regions.size())
        return seam::core::failure(seam::core::ErrorCode::InvalidArgument, "Generation region selection is invalid");
      const auto destination = dialog->choose({.purpose = seam::platform::FileDialogPurpose::PrepareGenerationJob,
          .title = generationPreparationTitle(selection->selectedRecipe.has_value()), .initialDirectory = selection->path.parent_path(),
          .suggestedName = "generation-job", .extensions = {"seamjobdir"}});
      if (!destination) return seam::core::Result<void>{destination.error()};
      if (!destination.value()) return seam::core::success();
      const auto afterDestination = validate(); if (!afterDestination) return afterDestination;
      const auto& region = selection->regions[*chosen.value()];
      return controller_.beginGenerationPreparation(selection->path, region.trackId, region.regionId,
          *destination.value(), destination.value()->stem().string(), selection->sha256, std::move(selection->selectedRecipe));
    }
    const auto epoch = controller_.productionSessionEpoch();
    const auto generation = project->lastDurableGeneration;
    const auto selected = controller_.selectedIndex();
    const auto path = dialog->choose({.purpose = seam::platform::FileDialogPurpose::OpenProject,
        .title = "Choose Saved Procedural Score", .initialDirectory = {}, .suggestedName = {}, .extensions = {"seam"}});
    if (!path) return seam::core::Result<void>{path.error()};
    if (!path.value()) return seam::core::success();
    const auto current = controller_.validateProductionImportContext(epoch, generation, selected);
    if (!current) return current;
    if (recording_.armed() || recording_.recordedFrames() > 0U)
      return seam::core::failure(seam::core::ErrorCode::Conflict, "Finish recording before score inspection");
    return controller_.beginGenerationScoreInspection(*path.value());
  }

  // Replacing a take is a decision the person choosing a job folder has to see, so a row that
  // already holds one is named as a retake in the dialogs that lead to and run its job.
  bool selectedRowHoldsTake() const {
    const auto* assignment = controller_.selectedProductionAssignment();
    return assignment != nullptr && !assignment->takeId.empty();
  }
  const char* generationPreparationTitle(bool designerSnapshot) const {
    const bool retake = selectedRowHoldsTake();
    if (designerSnapshot)
      return retake ? "Prepare Frozen Designer Retake Snapshot (No Audio Generated)"
                    : "Prepare Frozen Designer Snapshot (No Audio Generated)";
    return retake ? "Prepare Retake Job Folder (No Audio Generated)"
                  : "Prepare New Job Folder (No Audio Generated)";
  }

  seam::core::Result<void> generationFromDialog(bool batch = false) {
    if (controller_.proceduralImportBusy()) return seam::core::failure(seam::core::ErrorCode::Conflict, "Production worker is busy");
    if (recording_.armed() || recording_.recordedFrames() > 0U) return seam::core::failure(seam::core::ErrorCode::Conflict,
        "Finish the current recording before generating a candidate");
    const auto* project = controller_.productionProject();
    if (!project || !controller_.selectedProductionAssignment()) return seam::core::failure(seam::core::ErrorCode::InvalidState,
        "Open a producer workspace and select an inventory row first");
    stopAudition();
    const auto epoch = controller_.productionSessionEpoch();
    const auto generation = project->lastDurableGeneration;
    const auto selected = controller_.selectedIndex();
    auto dialog = platform_.fileDialog();
    const auto path = dialog->choose({.purpose = batch ? seam::platform::FileDialogPurpose::OpenGenerationBatch
                                                    : seam::platform::FileDialogPurpose::OpenGenerationJob,
        .title = batch ? "Generate and Collect an Unapproved Batch"
                       : (selectedRowHoldsTake() ? "Generate and Collect an Unapproved Retake"
                                                 : "Generate and Collect an Unapproved Candidate"),
        .initialDirectory = {}, .suggestedName = batch ? "batch.json" : "job.seamjob",
        .extensions = {batch ? "json" : "seamjob"}});
    if (!path) return seam::core::Result<void>{path.error()};
    if (!path.value()) return seam::core::success();
    const auto context = controller_.validateProductionImportContext(epoch, generation, selected);
    if (!context) return context;
    if (recording_.armed() || recording_.recordedFrames() > 0U) return seam::core::failure(seam::core::ErrorCode::Conflict,
        "Finish the current recording before generating a candidate");
    if (batch) {
      // Explicit file selection establishes the batch trust input. Its retained job
      // digests/expectations are not regenerated; worker reload must match these bytes.
      const auto bytes = seam::core::readTextFileLimited(*path.value(), 64U * 1024U);
      if (!bytes) return seam::core::Result<void>{bytes.error()};
      markerDrag_.reset(); pitchDrag_.reset();
      return controller_.beginPreparedGenerationBatch(*path.value(), seam::core::sha256Hex(bytes.value()));
    }
    const auto reference = seam::authoring::loadGenerationJobReference(*path.value());
    if (!reference) return seam::core::Result<void>{reference.error()};
    markerDrag_.reset(); pitchDrag_.reset();
    return controller_.beginPreparedGenerationJob(reference.value().directory, reference.value().manifestSha256);
  }

  seam::core::Result<void> importProceduralFromDialog() {
    if (controller_.proceduralImportBusy()) return seam::core::failure(seam::core::ErrorCode::Conflict,
        "Candidate import is busy");
    if (recording_.armed() || recording_.recordedFrames() > 0U) return seam::core::failure(seam::core::ErrorCode::Conflict,
        "Finish the current recording before importing a candidate");
    const auto* project = controller_.productionProject();
    const auto* assignment = controller_.selectedProductionAssignment();
    if (!project || !assignment) return seam::core::failure(seam::core::ErrorCode::InvalidState,
        "Open a producer workspace and select an inventory row first");
    const auto epoch = controller_.productionSessionEpoch();
    const auto generation = project->lastDurableGeneration;
    const auto selectedIndex = controller_.selectedIndex();
    const bool retake = !assignment->takeId.empty();
    auto dialog = platform_.fileDialog();
    const auto metadata = dialog->choose({.purpose = seam::platform::FileDialogPurpose::ImportProceduralCandidate,
        .title = retake ? "Import Unapproved Candidate Retake" : "Import Unapproved Procedural Candidate",
        .initialDirectory = {}, .suggestedName = {}, .extensions = {"json"}});
    if (!metadata) return seam::core::Result<void>{metadata.error()};
    if (!metadata.value()) return seam::core::success();
    const auto afterMetadata = controller_.validateProductionImportContext(epoch, generation, selectedIndex);
    if (!afterMetadata) return afterMetadata;
    const auto recipe = dialog->choose({.purpose = seam::platform::FileDialogPurpose::SelectProceduralRecipe,
        .title = "Select the Candidate's Exact Recipe", .initialDirectory = metadata.value()->parent_path(),
        .suggestedName = {}, .extensions = {"json"}});
    if (!recipe) return seam::core::Result<void>{recipe.error()};
    if (!recipe.value()) return seam::core::success();
    const auto afterRecipe = controller_.validateProductionImportContext(epoch, generation, selectedIndex);
    if (!afterRecipe) return afterRecipe;
    auto audio = *metadata.value(); audio.replace_extension(".wav");
    markerDrag_.reset();
    pitchDrag_.reset();
    return controller_.beginProceduralCandidateImport(*metadata.value(), audio, *recipe.value());
  }

  seam::core::Result<void> exportPitchInspectionFromDialog() {
    if (controller_.proceduralImportBusy() || !controller_.candidatePitchInspection() || !controller_.productionProject())
      return seam::core::failure(seam::core::ErrorCode::InvalidState, "Complete pitch inspection before exporting evidence");
    const auto epoch = controller_.productionSessionEpoch();
    const auto generation = controller_.productionProject()->lastDurableGeneration;
    const auto selection = controller_.selectedIndex();
    const auto key = controller_.candidatePitchInspection()->key;
    auto dialog = platform_.fileDialog();
    const auto path = dialog->choose({.purpose = seam::platform::FileDialogPurpose::ExportPitchInspection,
        .title = "Export Pitch Estimate (Not Approval)", .initialDirectory = {},
        .suggestedName = "candidate-pitch-inspection.json", .extensions = {"json"}});
    if (!path) return seam::core::Result<void>{path.error()};
    if (!path.value()) return seam::core::success();
    const auto current = controller_.validateProductionImportContext(epoch, generation, selection);
    if (!current) return current;
    if (!controller_.candidatePitchInspection() || controller_.candidatePitchInspection()->key != key)
      return seam::core::failure(seam::core::ErrorCode::Conflict, "Pitch inspection changed while choosing its destination");
    return controller_.exportCandidatePitchInspection(*path.value());
  }

  std::string sampleSemanticPrefix() const {
    const auto context = controller_.captureSampleReviewContext();
    if (!context) return "studio-review.unavailable.";
    const auto& value = context.value();
    return "studio-review." + std::to_string(value.epoch) + "." + std::to_string(value.generation) + "." +
        std::to_string(value.selectionRevision) + "." + value.manifestSha256 + ".";
  }

  seam::core::Result<void> sampleReviewAction(std::string_view action) {
    using namespace seam::native_ui;
    using Decision = seam::voicebank_production::SampleCandidateReviewDecision;
    if (sampleReviewModal_ || recording_.armed() || recording_.recordedFrames() > 0U || designer_.busy())
      return seam::core::failure(seam::core::ErrorCode::Conflict, "Finish recording, Designer work or the current dialog first");
    if (action == "cancel") { controller_.cancelProceduralCandidateImport(); return seam::core::success(); }
    if (action == "previous-page" || action == "next-page") {
      const auto count = studioSampleReviewDetailLines(controller_, controller_.logicalWidth()).size();
      const auto page = studioSampleReviewVisibleLines(controller_.logicalHeight());
      sampleReviewFirstLine_ = action == "previous-page" ? (sampleReviewFirstLine_ > page ? sampleReviewFirstLine_ - page : 0U)
          : std::min(sampleReviewFirstLine_ + page, count > page ? count - page : 0U);
      return seam::core::success();
    }
    if (controller_.proceduralImportBusy()) return seam::core::failure(seam::core::ErrorCode::Conflict, "Sample review work is busy");
    if (action == "back") { stopAudition(); sampleReviewView_ = false; return seam::core::success(); }
    if (action == "play") {
      if (audition_.active()) { stopAudition(); return seam::core::success(); }
      const auto& inspection = controller_.sampleReviewInspection();
      if (!inspection || !inspection->audio) return seam::core::failure(seam::core::ErrorCode::InvalidState, "Capture review audio first");
      const auto current = controller_.validateSampleReviewContext(inspection->context); if (!current) return current;
      const auto& unit = inspection->packet.manifest.units.front();
      const auto started = audition_.start(platform_.audioDevice(), inspection->audio,
          static_cast<std::uint64_t>(unit.markers.audioOffset), static_cast<std::uint64_t>(unit.markers.audioEnd), 0.25F);
      if (started) auditionStatus_ = "REVIEW AUDIO / EXACT CAPTURE / PLAYBACK IS NOT APPROVAL";
      return started;
    }
    stopAudition();
    if (action == "capture") { sampleReviewFirstLine_ = 0U; return controller_.beginSelectedSampleReview(); }
    if (action == "previous-unit" || action == "next-unit") {
      sampleReviewFirstLine_ = 0U;
      if (action == "previous-unit" && controller_.selectedIndex() == 0U) return seam::core::success();
      return controller_.beginSampleReviewUnitSelection(action == "previous-unit" ? controller_.selectedIndex() - 1U : controller_.selectedIndex() + 1U);
    }
    struct ModalGuard final { bool& flag; explicit ModalGuard(bool& value) : flag(value) { flag = true; } ~ModalGuard() { flag = false; } } guard{sampleReviewModal_};
    auto dialog = platform_.fileDialog();
    if (action == "open-manifest") return openStudioSampleManifest(controller_, *dialog);
    if (action == "source-evidence") { sampleReviewFirstLine_ = 0U; return captureStudioSourceQuality(controller_, *dialog); }
    if (action == "source-decision") { sampleReviewFirstLine_ = 0U; return confirmStudioSourceQuality(controller_, *dialog); }
    if (action == "source-license") { sampleReviewFirstLine_ = 0U; return captureStudioSourceLicense(controller_, *dialog); }
    if (action == "source-register") { sampleReviewFirstLine_ = 0U; return registerStudioSource(controller_, *dialog); }
    if (action == "register-reviewer") { sampleReviewFirstLine_ = 0U; return registerStudioReviewer(controller_, *dialog); }
    if (action == "create-draft") { sampleReviewFirstLine_ = 0U; return createStudioSampleManifestDraft(controller_, *dialog); }
    if (action == "reviewer") return chooseStudioSampleReviewer(controller_, *dialog);
    if (action == "accept" || action == "reject") return confirmStudioSampleReview(controller_, *dialog,
        action == "accept" ? Decision::Accept : Decision::Reject);
    if (action == "publish") { sampleReviewFirstLine_ = 0U; return publishStudioSampleCandidate(controller_, *dialog); }
    if (action == "sign-bank") { sampleReviewFirstLine_ = 0U; return signPublishedSampleBank(); }
    if (action == "install-bank") { sampleReviewFirstLine_ = 0U; return installSignedSampleBankFromDialog(); }
    if (action == "open-bank-in-song-editor") { sampleReviewFirstLine_ = 0U; return openInstalledBankInSongEditor(); }
    return seam::core::failure(seam::core::ErrorCode::InvalidArgument, "Unknown sample review action");
  }

  void paintSampleReview(seam::native_ui::RasterCanvas& canvas) {
    using namespace seam::native_ui;
    const auto width = canvas.logicalWidth(), height = canvas.logicalHeight();
    const auto lines = studioSampleReviewDetailLines(controller_, width);
    const auto count = studioSampleReviewVisibleLines(height);
    sampleReviewFirstLine_ = std::min(sampleReviewFirstLine_, lines.size() > count ? lines.size() - count : 0U);
    paintStudioSampleReview(canvas, controller_, sampleReviewFirstLine_, sampleReviewStatusLine());
    const auto prefix = sampleSemanticPrefix();
    SemanticNode root{.id = prefix + "root", .role = SemanticRole::Panel, .name = "Selected unit review and engineering candidate publication",
        .bounds = {0.0, 0.0, width, height}};
    for (const auto& control : studioSampleReviewControls(controller_, width)) {
      const bool enabled = control.enabled && !sampleReviewModal_;
      root.children.push_back({.id = prefix + control.id, .role = SemanticRole::Button, .name = control.label,
          .bounds = control.bounds, .enabled = enabled,
          .actions = enabled ? std::vector<SemanticAction>{SemanticAction::Activate, SemanticAction::SetFocus} : std::vector<SemanticAction>{}});
    }
    root.children.push_back({.id = prefix + "reviewer-status", .role = SemanticRole::Status, .name = "Explicit registered reviewer",
        .value = controller_.sampleReviewerId().empty() ? "None selected" : controller_.sampleReviewerId(), .bounds = {24.0,40.0,width-48.0,16.0}});
    for (std::size_t i = 0U; i < count && sampleReviewFirstLine_ + i < lines.size(); ++i)
      root.children.push_back({.id = prefix + "detail." + std::to_string(sampleReviewFirstLine_ + i), .role = SemanticRole::Status,
          .name = "Captured review data", .value = lines[sampleReviewFirstLine_ + i], .bounds = {24.0,302.0+18.0*static_cast<double>(i),width-48.0,16.0}});
    root.children.push_back({.id = prefix + "status", .role = SemanticRole::Status, .name = "Review operation status",
        .value = sampleReviewStatusLine(), .bounds = {24.0,height-28.0,width-48.0,18.0}});
    sampleReviewAccessibility_.rebuildCustom(std::move(root), sampleReviewSemanticFocus_);
  }

  std::string generationSemanticPrefix() const {
    const auto* project=controller_.productionProject();
    return "studio-generation."+std::to_string(controller_.productionSessionEpoch())+"."+
        std::to_string(project?project->lastDurableGeneration:0U)+"."+std::to_string(controller_.selectedIndex())+".";
  }
  seam::core::Result<void> generationControlAction(std::string_view id) {
    const auto controls = generationQueueView_
        ? seam::native_ui::studioGenerationQueueControls(controller_, controller_.logicalWidth(),
            controller_.logicalHeight(), recording_.armed() || recording_.recordedFrames() > 0U,
            generationRequestFirst_, generationRequestDetailId_, generationRequestJobFirst_)
        : seam::native_ui::studioGenerationControls(controller_, controller_.logicalWidth(),
            recording_.armed() || recording_.recordedFrames() > 0U);
    const auto found=std::find_if(controls.begin(),controls.end(),[&](const auto& control){return control.id==id;});
    if (generationModal_ || designerView_ || sampleReviewView_ || found==controls.end() || !found->enabled)
      return seam::core::failure(seam::core::ErrorCode::Conflict,"Generation action is unavailable or busy");
    if (generationQueueView_) {
      if (id == "request-detail-back") {
        generationRequestDetailId_.clear(); generationRequestJobFirst_ = 0U; return seam::core::success();
      }
      constexpr std::string_view inspectPrefix{"inspect-request:"};
      if (id.starts_with(inspectPrefix)) {
        generationRequestDetailId_ = std::string{id.substr(inspectPrefix.size())};
        generationRequestJobFirst_ = 0U;
        return seam::core::success();
      }
      constexpr std::string_view resumeDetailPrefix{"request-detail-resume:"};
      if (id.starts_with(resumeDetailPrefix))
        return controller_.beginGenerationRequestResume(id.substr(resumeDetailPrefix.size()));
      constexpr std::string_view locateDetailPrefix{"request-detail-locate:"};
      if (id.starts_with(locateDetailPrefix))
        return locateGenerationRequestDefinitionFromDialog(id.substr(locateDetailPrefix.size()));
      if (id == "request-detail-previous") {
        const auto rows = seam::native_ui::studioGenerationRequestDetailVisibleRows(controller_.logicalHeight());
        generationRequestJobFirst_ = generationRequestJobFirst_ > rows ? generationRequestJobFirst_ - rows : 0U;
        return seam::core::success();
      }
      if (id == "request-detail-next") {
        generationRequestJobFirst_ += seam::native_ui::studioGenerationRequestDetailVisibleRows(controller_.logicalHeight());
        return seam::core::success();
      }
      if (id == "request-detail-inspect-outputs")
        return controller_.beginGenerationRequestOutputInspection(
            generationRequestDetailId_, generationRequestJobFirst_,
            seam::native_ui::studioGenerationRequestDetailVisibleRows(controller_.logicalHeight()));
      if (id == "request-detail-cancel-output-inspection")
        return controller_.cancelGenerationRequestOutputInspection();
      if (id == "queue-close") { generationQueueView_ = false; generationRequestDetailId_.clear(); return seam::core::success(); }
      if (id == "queue-refresh") {
        generationRequestFirst_ = 0U;
        return controller_.refreshGenerationRequests();
      }
      if (id == "queue-previous") {
        const auto rows = seam::native_ui::studioGenerationQueueVisibleRows(controller_.logicalHeight());
        generationRequestFirst_ = generationRequestFirst_ > rows ? generationRequestFirst_ - rows : 0U;
        return seam::core::success();
      }
      if (id == "queue-next") {
        generationRequestFirst_ += seam::native_ui::studioGenerationQueueVisibleRows(controller_.logicalHeight());
        return seam::core::success();
      }
      return seam::core::failure(seam::core::ErrorCode::Unsupported, "Unknown request queue action");
    }
    if (id=="cancel") {
      controller_.cancelProceduralCandidateImport();
      return seam::core::success();
    }
    if (id=="request-queue") {
      generationQueueView_ = true; generationRequestFirst_ = 0U;
      generationRequestDetailId_.clear(); generationRequestJobFirst_ = 0U;
      return seam::core::success();
    }
    if (id=="plan-campaign") return planCampaignFromDialog();
    if (id=="run-campaign") return runCampaignFromDialog();
    if (id=="preflight-campaign") return preflightCampaignFromDialog();
    struct ModalGuard {
      bool& active;
      explicit ModalGuard(bool& value):active(value) { active=true; }
      ~ModalGuard() { active=false; }
    } guard{generationModal_};
    const std::string action{id};
    return action=="prepare"?preparationFromDialog():action=="assemble"?assembleBatchFromDialog():generationFromDialog(action=="batch");
  }
  void rebuildGenerationAccessibility(double width,double height) {
    using namespace seam::native_ui;
    const auto prefix=generationSemanticPrefix();
    SemanticNode root{.id=prefix+"root",.role=SemanticRole::Panel,
        .name=generationRequestDetailId_.empty()?"Producer generation actions":"Generation request job and coverage details",
        .bounds={0.0,0.0,width,height}};
    const auto controls = generationQueueView_
        ? studioGenerationQueueControls(controller_, width, height,
            recording_.armed() || recording_.recordedFrames() > 0U, generationRequestFirst_,
            generationRequestDetailId_, generationRequestJobFirst_)
        : studioGenerationControls(controller_, width,
            recording_.armed() || recording_.recordedFrames() > 0U);
    for (const auto& control : controls) {
      const bool enabled=control.enabled && !generationModal_;
      root.children.push_back({.id=prefix+control.id,.role=SemanticRole::Button,.name=control.label,
          .bounds=control.bounds,.enabled=enabled,
          .actions=enabled?std::vector<SemanticAction>{SemanticAction::Activate,SemanticAction::SetFocus}:std::vector<SemanticAction>{}});
    }
    if (generationQueueView_) {
      const auto& requests = controller_.generationRequests();
      if (!generationRequestDetailId_.empty()) {
        const auto request = std::find_if(requests.begin(), requests.end(), [this](const auto& entry) {
          return entry.request.requestId == generationRequestDetailId_;
        });
        if (request == requests.end()) {
          root.children.push_back({.id=prefix+"request-detail-missing",.role=SemanticRole::Status,
              .name="Selected request unavailable",.value="Request is not in the current verified snapshot; return and refresh",
              .bounds={44.0,170.0,std::max(0.0,width-88.0),18.0}});
        } else {
          const auto& item = *request;
          root.children.push_back({.id=prefix+"request-detail-identity",.role=SemanticRole::Status,
              .name="Immutable generation request identity",
              .value="request=" + item.request.requestId + "; language=" + item.request.language +
                  "; recipe=" + item.request.recipeId + "@" + item.request.recipeVersion +
                  "; recipe_sha256=" + item.request.recipeHash,
              .bounds={44.0,154.0,std::max(0.0,width-88.0),18.0}});
          root.children.push_back({.id=prefix+"request-detail-producer",.role=SemanticRole::Status,
              .name="Expected producer state",
              .value="generation=" + std::to_string(item.request.expectedGeneration) +
                  "; project_sha256=" + item.request.expectedProjectSha256,
              .bounds={44.0,172.0,std::max(0.0,width-88.0),18.0}});
          root.children.push_back({.id=prefix+"request-detail-submission",.role=SemanticRole::Status,
              .name="Request submission and worker definition",
              .value="submitted_by=" + item.request.submittedBy + "; submitted_at=" + item.request.submittedAtUtc +
                  "; definition_locator=" + (item.request.definitionLocator.empty()?"unavailable":item.request.definitionLocator),
              .bounds={44.0,190.0,std::max(0.0,width-88.0),18.0}});
          root.children.push_back({.id=prefix+"request-detail-budget",.role=SemanticRole::Status,
              .name="Admitted generation budgets",
              .value="jobs=" + std::to_string(item.request.budget.maximumJobs) +
                  "; frames=" + std::to_string(item.request.budget.maximumFrames) +
                  "; bytes=" + std::to_string(item.request.budget.maximumBytes) +
                  "; batch_jobs=" + std::to_string(item.request.budget.batchMaximumJobs) +
                  "; batch_frames=" + std::to_string(item.request.budget.batchMaximumFrames),
              .bounds={44.0,208.0,std::max(0.0,width-88.0),18.0}});
          if (item.terminal)
            root.children.push_back({.id=prefix+"request-detail-terminal",.role=SemanticRole::Status,
                .name="Durable terminal outcome",
                .value=seam::voicebank_production::toString(item.terminal->outcome) + "; " +
                    std::to_string(item.terminal->completedBatches) + " of " + std::to_string(item.batchCount()) +
                    " batches; " + item.terminal->detail,
                .bounds={44.0,244.0,std::max(0.0,width-88.0),18.0}});
          else
            root.children.push_back({.id=prefix+"request-detail-pending",.role=SemanticRole::Status,
                .name="Pending request safety state",
                .value="Definition bytes must still match the request ID before resume; queue is not evidence of completion or review",
                .bounds={44.0,244.0,std::max(0.0,width-88.0),18.0}});
          const auto rows = studioGenerationRequestDetailVisibleRows(height);
          const auto campaignProgress = controller_.generationCampaignProgress();
          const auto activeRequestId = controller_.generationCampaignSha256();
          const auto* outputPage = controller_.generationRequestOutputInspectionPage();
          std::string outputSummary{"Not inspected for the visible request page"};
          if (controller_.generationRequestOutputInspectionLoading()) {
            outputSummary = std::string{controller_.generationRequestOutputInspectionStatus()};
          } else if (outputPage && outputPage->requestId == item.request.requestId) {
            outputSummary = "Read-only evidence for jobs " + std::to_string(outputPage->firstJob + 1U) +
                " through " + std::to_string(outputPage->firstJob + outputPage->jobs.size()) +
                "; output verification does not imply collection or review";
          }
          root.children.push_back({.id=prefix+"request-output-evidence-summary",.role=SemanticRole::Status,
              .name="Read-only generation output evidence",.value=std::move(outputSummary),
              .bounds={44.0,262.0,std::max(0.0,width-88.0),14.0}});
          for (std::size_t index = generationRequestJobFirst_;
              index < std::min(item.request.jobs.size(), generationRequestJobFirst_ + rows); ++index) {
            const auto& job = item.request.jobs[index];
            const auto jobState = studioGenerationJobState(item, index, activeRequestId, campaignProgress);
            const auto* outputEvidence = controller_.generationRequestJobOutputInspection(
                item.request.requestId, index);
            root.children.push_back({.id=prefix+"generation-job."+job.jobId,.role=SemanticRole::Status,
                .name="Generation job " + std::to_string(index + 1U) + ": " + job.jobId,
                .value="state=" + std::string{studioGenerationJobStateLabel(jobState)} +
                    "; take=" + job.takeId + "; coverage=" + job.coverageKey + "; style=" + job.style +
                    "; pitch_layer=" + std::to_string(job.pitchLayer) + "; frames=" +
                    std::to_string(job.frameCount) + "; batch=" + std::to_string(job.batchIndex + 1) +
                    "; output_evidence=" + std::string{outputEvidence
                        ? studioGenerationOutputEvidenceStateLabel(outputEvidence->state)
                        : std::string_view{"NOT INSPECTED"}} +
                    (outputEvidence && !outputEvidence->diagnostic.empty()
                        ? "; output_diagnostic=" + outputEvidence->diagnostic : ""),
                .bounds={52.0,282.0+static_cast<double>(index-generationRequestJobFirst_)*42.0,
                    std::max(0.0,width-104.0),40.0}});
          }
        }
      } else {
        root.children.push_back({.id=prefix+"request-queue-status",.role=SemanticRole::Status,
            .name="Verified generation request queue",.value=std::string(controller_.generationRequestQueueStatus()),
            .bounds={294.0,126.0,std::max(0.0,width-574.0),14.0}});
        const auto rows = studioGenerationQueueVisibleRows(height);
        const auto buttonWidth = std::min(146.0, std::max(54.0, std::max(0.0, width - 574.0) * 0.38));
        const auto detailsWidth = std::max(0.0, width - 574.0 - buttonWidth - 18.0);
        for (std::size_t index = generationRequestFirst_; index < std::min(requests.size(), generationRequestFirst_ + rows); ++index) {
          const auto& request = requests[index];
          std::string value = request.terminal
              ? "Terminal " + seam::voicebank_production::toString(request.terminal->outcome) + ", " +
                  std::to_string(request.terminal->completedBatches) + " of " + std::to_string(request.batchCount()) + " batches"
              : "Submitted, " + std::to_string(request.request.jobs.size()) + " jobs, expected producer generation " +
                  std::to_string(request.request.expectedGeneration) + "; campaign definition SHA-256 is checked before resume";
          root.children.push_back({.id=prefix+"request-status."+request.request.requestId,
              .role=SemanticRole::Status,.name="Generation request " + request.request.requestId,
              .value=std::move(value),.bounds={302.0,150.0+static_cast<double>(index-generationRequestFirst_)*30.0,
                  detailsWidth,26.0}});
        }
      }
    }
    if (!generationQueueView_) {
      const bool importEnabled=controller_.productionProject() && controller_.selectedProductionAssignment() &&
          !controller_.proceduralImportBusy() && !recordingInput_.capturing() && !recordingInput_.pending() &&
          !generationModal_ && !takeImportModal_;
      root.children.push_back({.id=prefix+"import-wav",.role=SemanticRole::Button,
          .name="Import existing WAV as an unapproved take",
          .bounds={24.0,48.0,200.0,20.0},.enabled=importEnabled,
          .actions=importEnabled?std::vector<SemanticAction>{SemanticAction::Activate,SemanticAction::SetFocus}:std::vector<SemanticAction>{}});
      addRecordingAccessibility(root,prefix);
    }
    root.children.push_back({.id=prefix+"status",.role=SemanticRole::Status,.name="Producer operation status",
        .value=!lastError_.empty()?lastError_:!recordingStatus_.empty()?recordingStatus_:controller_.status(),
        .bounds={width-360.0,24.0,340.0,44.0}});
    generationAccessibility_.rebuildCustom(std::move(root),generationSemanticFocus_);
  }
  void rebuildStudioAccessibility(double width,double height) {
    using namespace seam::native_ui;
    const auto prefix=generationSemanticPrefix();
    SemanticNode root{.id=prefix+"root",.role=SemanticRole::Panel,.name="Voicebank production actions",
        .bounds={0.0,0.0,width,height}};
    const bool importEnabled=controller_.productionProject() && controller_.selectedProductionAssignment() &&
        !controller_.proceduralImportBusy() && !recordingInput_.capturing() && !recordingInput_.pending() &&
        !takeImportModal_;
    root.children.push_back({.id=prefix+"import-wav",.role=SemanticRole::Button,
        .name="Import existing WAV as an unapproved take",
        .value="Cmd/Ctrl+R. Requires a producer workspace and selected inventory row. Imported takes require review.",
        .bounds={24.0,48.0,200.0,20.0},.enabled=importEnabled,
        .actions=importEnabled?std::vector<SemanticAction>{SemanticAction::Activate,SemanticAction::SetFocus}:std::vector<SemanticAction>{}});
    addRecordingAccessibility(root,prefix);
    root.children.push_back({.id=prefix+"status",.role=SemanticRole::Status,.name="Studio operation status",
        .value=!lastError_.empty()?lastError_:!recordingStatus_.empty()?recordingStatus_:controller_.status(),
        .bounds={width-360.0,24.0,340.0,44.0}});
    studioAccessibility_.rebuildCustom(std::move(root),studioSemanticFocus_);
  }

  void paint(seam::native_ui::RasterCanvas& canvas) noexcept override {
    if (const auto polled = recordingInput_.poll(); !polled)
      (void)noteRecordingFailure("NO TAKE RECORDED / ", polled.error());
    record(pollPendingRecordingExport());
    const auto auditionState = audition_.poll();
    if (!auditionState) { auditionStatus_.clear(); lastError_ = auditionState.error().message; }
    else if (!auditionState.value()) auditionStatus_.clear();
    const bool workspaceWasOpening=controller_.workspaceOpening();
    const auto producerPoll=controller_.pollProceduralCandidateImport();
    record(producerPoll);
    completePendingRecordingImport(producerPoll);
    if (workspaceWasOpening && !controller_.workspaceOpening() && !controller_.productionProject()) {
      designerView_=true;
      if (producerPoll) lastError_=designer_.model()?"Workspace opening cancelled; voice draft retained":"Workspace opening cancelled; no workspace loaded";
    }
    record(designer_.poll());
    if (!designerPublishStatus_.empty() &&
        (!designer_.model() || designer_.epoch() != publishedDesignerEpoch_ ||
         designer_.model()->revision() != publishedDesignerRevision_)) {
      designerPublishStatus_.clear();
      publishedSingerPackagePath_.clear();
      publishedSingerPackageDigest_.clear();
      publishedSingerDisplayName_.clear();
      publishedSingerPublicKey_.reset();
      publishedSingerInstalled_ = false;
      installedSinger_.reset();
    }
    if (designerView_) { paintDesigner(canvas); if (designer_.busy() || designer_.auditionBusy() || audition_.active()) repaint(); return; }
    if (sampleReviewView_) { paintSampleReview(canvas); if (controller_.proceduralImportBusy() || audition_.active()) repaint(); return; }
    painter_.paint(canvas, controller_, recording_.armed(), inputBackend_);
    rebuildGenerationAccessibility(canvas.logicalWidth(),canvas.logicalHeight());
    rebuildStudioAccessibility(canvas.logicalWidth(),canvas.logicalHeight());
    const auto generationControls=seam::native_ui::studioGenerationControls(controller_,canvas.logicalWidth(),recording_.armed() || recording_.recordedFrames()>0U);
    if (!generationControls.empty()) {
      const auto bottom = std::max_element(generationControls.begin(), generationControls.end(),
          [](const auto& left, const auto& right) { return left.bounds.bottom() < right.bounds.bottom(); })->bounds.bottom();
      canvas.fillRect({294.0,268.0,canvas.logicalWidth()-574.0,bottom-268.0},seam::native_ui::Color{15,14,18,255});
      for (const auto& control:generationControls) {
        canvas.fillRect(control.bounds,control.enabled?seam::native_ui::Color{72,52,76,255}:seam::native_ui::Color{34,31,38,255});
        canvas.drawText({control.bounds.x+4.0,control.bounds.y+2.0,control.bounds.width-8.0,14.0},
            seam::native_ui::studioControlPaintLabel(canvas,control,10.0,4.0),
            control.enabled?seam::native_ui::Color{239,233,241,255}:seam::native_ui::Color{125,118,129,255},10.0);
      }
    }
    if (generationQueueView_) {
      seam::native_ui::paintStudioGenerationRequestQueue(canvas, controller_, generationRequestFirst_,
          generationRequestDetailId_, generationRequestJobFirst_);
      const auto queueControls = seam::native_ui::studioGenerationQueueControls(controller_, canvas.logicalWidth(),
          canvas.logicalHeight(), recording_.armed() || recording_.recordedFrames() > 0U, generationRequestFirst_,
          generationRequestDetailId_, generationRequestJobFirst_);
      for (const auto& control : queueControls) {
        const auto fill = control.enabled ? seam::native_ui::Color{72,52,76,255} : seam::native_ui::Color{34,31,38,255};
        canvas.fillRect(control.bounds, fill);
        canvas.drawText({control.bounds.x+4.0,control.bounds.y+3.0,control.bounds.width-8.0,14.0},
            control.label, control.enabled ? seam::native_ui::Color{239,233,241,255} : seam::native_ui::Color{125,118,129,255}, 8.0);
      }
      if (controller_.proceduralImportBusy()) repaint();
      return;
    }
    const bool importEnabled=controller_.productionProject() && controller_.selectedProductionAssignment() &&
        !controller_.proceduralImportBusy() && !recordingInput_.capturing() && !recordingInput_.pending() && !takeImportModal_;
    // The shortcut hints and the recording labels are read, not decoration, so they are drawn at a
    // size a person can read at a glance: the strips that hold them grew to fit the larger type and
    // the row moved down by the same amount, so nothing below it moved onto it. The strips are laid
    // out from these two constants, so the geometry and the text cannot drift apart again.
    const auto hintHeight = kStudioHintHeight;
    const auto hintText = kStudioHintText;
    const auto hintBaseline = kStudioHintBaseline;
    // Each strip is as wide as the hint it holds, measured at the size it is drawn: the earlier ones
    // were sized for 6-to-7 point text, and raising the size without widening them only moved where
    // the label was cut, which a source check cannot see and a person can. These labels are the
    // creator's only route to the shortcut they name.
    // The two hints are told apart by their keys rather than their full titles: the row has the width
    // of the units rail beside it, and at a readable size a full title each no longer fits there. The
    // keys are the part a creator looks for, and every one of these has its own accessible name and
    // description in full.
    const auto designerHint = std::string{"CMD-D DESIGNER"};
    const auto reviewHint = std::string{"CMD-R IMPORT"};
    const auto hintWidth = [&canvas](const std::string& text) {
      return std::ceil(canvas.measureText(text, kStudioHintText)) + 12.0;
    };
    // Each strip is exactly as wide as its own hint, so neither truncates the other: the earlier pair
    // shared one row sized for 6-point text and cut both labels off, which is what the second
    // developer saw. The row ends at kStudioHintRowRight, where the column beside it begins, so a
    // strip can never draw over that column's own heading.
    const auto designerWidth = hintWidth(designerHint);
    const auto reviewWidth = hintWidth(reviewHint);
    canvas.fillRect({24.0,48.0,designerWidth,hintHeight},importEnabled?seam::native_ui::Color{72,52,76,255}:seam::native_ui::Color{34,31,38,255});
    canvas.drawText({28.0,hintBaseline,designerWidth-8.0,hintHeight-4.0}, designerHint,
        importEnabled?seam::native_ui::Color{239,233,241,255}:seam::native_ui::Color{150,145,153,255}, hintText);
    const auto reviewLeft = 24.0 + designerWidth + 6.0;
    canvas.fillRect({reviewLeft,48.0,reviewWidth,hintHeight}, seam::native_ui::Color{72,52,76,255});
    canvas.drawText({reviewLeft+4.0,hintBaseline,reviewWidth-8.0,hintHeight-4.0}, reviewHint, seam::native_ui::Color{239,233,241,255}, hintText);
    const auto controlWidth = [&canvas](const std::string& label) {
      return std::ceil(canvas.measureText(label, kStudioControlText)) + 14.0;
    };
    for (const auto& control : recordingControls(controlWidth("R STOP + PUBLISH"),
                                                 controlWidth("X DISCARD"))) {
      canvas.fillRect(control.bounds, control.enabled ? seam::native_ui::Color{72,52,76,255} : seam::native_ui::Color{34,31,38,255});
      canvas.drawText({control.bounds.x+6.0,control.bounds.y+5.0,control.bounds.width-12.0,16.0}, control.label,
          control.enabled ? seam::native_ui::Color{239,233,241,255} : seam::native_ui::Color{150,145,153,255}, kStudioControlText);
    }
    // The status is the sentence the app is currently saying. It has a full width row of its own
    // (kStudioStatusTop) between the units rail and the inspector column, and it wraps inside it, so
    // a long message is said in full instead of being cut: a clipped sentence shows the creator its
    // middle and hides what it says. Both edges are bounded by a column, not by the window, so a long
    // sentence can never be drawn under the rail or the production project heading.
    const auto statusLeft = std::max(kStudioStatusLeft, reviewLeft + reviewWidth + 8.0);
    const auto statusRight = std::min(canvas.logicalWidth() - kStudioStatusRightMargin,
                                      canvas.logicalWidth());
    const auto statusWidth = std::max(0.0, statusRight - statusLeft);
    canvas.drawTextWrapped({statusLeft, kStudioStatusTop, statusWidth, kStudioStatusHeight},
        !lastError_.empty() ? lastError_ : !recordingStatus_.empty() ? recordingStatus_ :
            (auditionStatus_.empty() ? "SPACE PLAY / ALT ARROWS START / ALT-SHIFT END / ALT +/- PAN" : auditionStatus_),
        !lastError_.empty() ? seam::native_ui::Color{169, 79, 119, 255} : seam::native_ui::Color{166, 154, 170, 255},
        kStudioHintText, kStudioHintText * 1.35);
    if (controller_.proceduralImportBusy() || pendingRecordingExportStarted_ ||
        audition_.active() || recordingInput_.capturing()) repaint();
  }
  void resized(double width, double height, double) noexcept override {
    if (designerDrag_ && designer_.model()) {
      record(designer_.endGesture(designerDrag_->epoch,designer_.model()->revision(),false));
      designerDrag_.reset();
    }
    controller_.resize(width, height);
  }

  void pointerDown(const seam::native_ui::PointerEvent& event) noexcept override {
    if (event.button == seam::native_ui::PointerButton::Left && !sampleReviewModal_) {
      for (const auto& control : recordingControls()) {
        if (!control.bounds.contains(event.position)) continue;
        if (control.enabled) {
          lastError_.clear();
          record(control.id == "record" ? recordingAction() : discardRecording());
        }
        repaint(); return;
      }
    }
    if (recordingInput_.capturing() || recordingInput_.pending()) {
      lastError_ = "Press R to finish or retry publishing the current recording, or X to discard it";
      repaint(); return;
    }
    if (sampleReviewModal_) return;
    if (generationQueueView_) {
      if (event.button == seam::native_ui::PointerButton::Left)
        for (const auto& control : seam::native_ui::studioGenerationQueueControls(controller_,
                 controller_.logicalWidth(), controller_.logicalHeight(),
                 recording_.armed() || recording_.recordedFrames() > 0U, generationRequestFirst_,
                 generationRequestDetailId_, generationRequestJobFirst_)) {
          if (control.bounds.contains(event.position)) {
            if (control.enabled) { lastError_.clear(); record(generationControlAction(control.id)); }
            repaint(); return;
          }
        }
      repaint(); return;
    }
    if (sampleReviewView_) {
      if (event.button == seam::native_ui::PointerButton::Left)
        for (const auto& control : seam::native_ui::studioSampleReviewControls(controller_, controller_.logicalWidth()))
          if (control.enabled && control.bounds.contains(event.position)) { lastError_.clear(); record(sampleReviewAction(control.id)); break; }
      repaint(); return;
    }
    if (!designerView_ && event.button==seam::native_ui::PointerButton::Left) {
      for (const auto& control:seam::native_ui::studioGenerationControls(controller_,controller_.logicalWidth(),recording_.armed() || recording_.recordedFrames()>0U)) {
        if (!control.bounds.contains(event.position)) continue;
        if (control.enabled) {
          lastError_.clear();
          record(generationControlAction(control.id));
        }
        repaint(); return;
      }
    }
    if (!designerView_ && event.button==seam::native_ui::PointerButton::Left &&
        seam::ui::Rect{24.0,48.0,200.0,20.0}.contains(event.position)) {
      lastError_.clear();
      record(importRecordedTakeFromDialog());
      repaint(); return;
    }
    if (!designerView_ && event.button == seam::native_ui::PointerButton::Left && seam::ui::Rect{230.0,48.0,120.0,20.0}.contains(event.position)) {
      if (!controller_.proceduralImportBusy() && !recording_.armed() && recording_.recordedFrames() == 0U) {
        stopAudition(); markerDrag_.reset(); pitchDrag_.reset(); sampleReviewView_ = true; sampleReviewFirstLine_ = 0U;
      }
      repaint(); return;
    }
    stopAudition();
    if (designerView_) {
      const auto* model = designer_.model();
      if (model && !designerDrag_ && event.button==seam::native_ui::PointerButton::Left) {
        for (const auto& node:designerAccessibility_.root().children) {
          if (node.role==seam::native_ui::SemanticRole::Button && node.enabled && node.bounds.width>0.0 &&
              node.bounds.height>0.0 && node.bounds.contains(event.position)) {
            const auto target=node.id;
            record(dispatchAccessibility(target,seam::native_ui::SemanticAction::Activate));
            return;
          }
        }
      }
      if (!model && !designer_.busy() && event.button==seam::native_ui::PointerButton::Left) {
        for (std::size_t index=0U;index<(canCreateProducerWorkspace()?4U:3U);++index) if (designerEntryBounds(index,controller_.logicalWidth()).contains(event.position)) {
          record(dispatchAccessibility(designerSemanticPrefix()+(index==0U?"new":index==1U?"open":index==2U?"back":"create-producer"),
              seam::native_ui::SemanticAction::Activate));
          return;
        }
      }
      const auto layout = seam::native_ui::voiceDesignerLayout(controller_.logicalHeight());
      if (!model || designer_.busy() || designerDrag_ || event.button != seam::native_ui::PointerButton::Left ||
          !std::isfinite(event.position.x) || !std::isfinite(event.position.y) ||
          event.position.x < 24.0 || event.position.x >= controller_.logicalWidth() - 24.0 ||
          event.position.y < layout.controlsTop || event.position.y >= layout.controlsTop+static_cast<double>(layout.visibleRows)*layout.rowHeight) return;
      const auto count = designerControlCount(model->recipe(), designer_.auditionPose());
      const auto first = layout.firstRow(count,designerControl_);
      const auto control = first + static_cast<std::size_t>((event.position.y-layout.controlsTop)/layout.rowHeight);
      if (control >= count) return;
      designerControl_ = control;
      if (control != 3U && control != 4U) {
        const auto baseline = model->recipe(); const auto epoch = designer_.epoch();
        const auto started = designer_.beginGesture(epoch, model->revision());
        if (!started) { record(started); return; }
        designerDrag_ = DesignerDrag{epoch, control, designer_.auditionPose(), first, event.position.x, baseline};
      }
      repaint(); return;
    }
    if (controller_.proceduralImportBusy()) return;
    if (event.button != seam::native_ui::PointerButton::Left) return;
    if (!recording_.armed() && recording_.recordedFrames() == 0U) {
      const auto drag = controller_.beginCandidateMarkerDrag(event.position);
      if (!drag) { record(seam::core::Result<void>{drag.error()}); return; }
      if (drag.value()) { markerDrag_.reset(); pitchDrag_.reset(); repaint(); return; }
    }
    if (const auto marker = controller_.microscope().hitTestMarker(event.position, 6.0);
        marker.has_value()) {
      markerDrag_ = *marker;
      pitchDrag_.reset();
      return;
    }
    if (const auto pitch = controller_.microscope().hitTestPitchMark(event.position, 5.0);
        pitch.has_value()) {
      pitchDrag_ = *pitch;
      markerDrag_.reset();
      return;
    }
    if (event.position.x >= 8.0 && event.position.x < 244.0 &&
        event.position.y >= 108.0) {
      const auto first = controller_.selectedIndex() > 8U
                             ? controller_.selectedIndex() - 8U
                             : 0U;
      const auto index = seam::native_ui::voicebankStudioUnitRailIndexAt(
          event.position.y, first, controller_.selectableUnitCount(),
          controller_.logicalHeight(), controller_.manifest().units.empty());
      if (index.has_value()) record(controller_.beginEditableUnitSelection(*index));
    }
  }

  void pointerMove(const seam::native_ui::PointerEvent& event) noexcept override {
    if (generationQueueView_) return;
    if (sampleReviewView_ || sampleReviewModal_) return;
    if (designerView_) { updateDesignerDrag(event); return; }
    if (controller_.candidateMarkerDragging()) {
      record(controller_.updateCandidateMarkerDrag(event.position));
      repaint();
      return;
    }
    if (markerDrag_.has_value()) {
      record(controller_.moveSelectedMarker(*markerDrag_, event.position.x));
      repaint();
    } else if (pitchDrag_.has_value()) {
      record(controller_.moveSelectedPitchMark(*pitchDrag_, event.position.x));
      repaint();
    }
  }
  void pointerUp(const seam::native_ui::PointerEvent& event) noexcept override {
    if (generationQueueView_) return;
    if (sampleReviewView_ || sampleReviewModal_) return;
    if (designerView_) {
      if (designerDrag_ && event.button == seam::native_ui::PointerButton::Left) {
        updateDesignerDrag(event);
        record(designer_.endGesture(designerDrag_->epoch, designer_.model()->revision(), true));
        designerDrag_.reset(); repaint();
      }
      return;
    }
    if (controller_.candidateMarkerDragging() && event.button == seam::native_ui::PointerButton::Left) {
      const auto updated = controller_.updateCandidateMarkerDrag(event.position);
      if (updated) record(controller_.finishCandidateMarkerDrag());
      else { controller_.cancelCandidateMarkerDrag(); record(updated); }
    }
    markerDrag_.reset();
    pitchDrag_.reset();
    repaint();
  }
  void scroll(double, double deltaY, seam::ui::Point point,
              seam::native_ui::InputModifiers) noexcept override {
    if (recordingInput_.capturing() || recordingInput_.pending()) return;
    if (sampleReviewModal_) return;
    if (sampleReviewView_) {
      if (deltaY != 0.0) { record(sampleReviewAction(deltaY > 0.0 ? "previous-page" : "next-page")); repaint(); }
      return;
    }
    if (generationQueueView_) {
      if (!generationRequestDetailId_.empty()) {
        const auto rows = seam::native_ui::studioGenerationRequestDetailVisibleRows(controller_.logicalHeight());
        const auto request = std::find_if(controller_.generationRequests().begin(), controller_.generationRequests().end(), [this](const auto& entry) {
          return entry.request.requestId == generationRequestDetailId_;
        });
        if (request != controller_.generationRequests().end()) {
          if (deltaY > 0.0 && generationRequestJobFirst_ > 0U)
            generationRequestJobFirst_ = generationRequestJobFirst_ > rows ? generationRequestJobFirst_ - rows : 0U;
          else if (deltaY < 0.0 && generationRequestJobFirst_ + rows < request->request.jobs.size())
            generationRequestJobFirst_ += rows;
        }
      } else {
        const auto rows = seam::native_ui::studioGenerationQueueVisibleRows(controller_.logicalHeight());
        if (deltaY > 0.0 && generationRequestFirst_ > 0U)
          generationRequestFirst_ = generationRequestFirst_ > rows ? generationRequestFirst_ - rows : 0U;
        else if (deltaY < 0.0 && generationRequestFirst_ + rows < controller_.generationRequests().size())
          generationRequestFirst_ += rows;
      }
      repaint(); return;
    }
    if (designerView_) return;
    if (point.x < 270.0 || point.x >= controller_.logicalWidth() - 256.0 || point.y < 300.0 ||
        !controller_.manifest().units.empty() || !controller_.candidateMarkerPreview() || deltaY == 0.0) return;
    if (deltaY > 0.0) record(controller_.moveCandidateMarker(-1));
    else if (deltaY < 0.0) record(controller_.moveCandidateMarker(1));
    stopAudition();
    repaint();
  }

  void keyDown(const seam::native_ui::KeyEvent& event) noexcept override {
    if (sampleReviewModal_) return;
    lastError_.clear();
    if (recordingInput_.capturing() || recordingInput_.pending()) {
      if (pendingRecordingImportStarted_ && event.key == seam::native_ui::NativeKey::Escape && !event.repeat) {
        controller_.cancelProceduralCandidateImport();
        lastError_ = "Cancelling recorded take import; the capture and saved WAV are retained until completion";
      } else if (pendingRecordingExportStarted_ && event.key == seam::native_ui::NativeKey::Escape && !event.repeat) {
        lastError_ = "Recorded WAV export cannot be interrupted; the capture is retained until its file is verified";
      } else if (event.key == seam::native_ui::NativeKey::R && !event.repeat) record(stopRecording());
      else if (event.key == seam::native_ui::NativeKey::X && !event.repeat) record(discardRecording());
      else if (pendingRecordingImportStarted_ && event.key == seam::native_ui::NativeKey::Escape) {
        lastError_ = "Recorded take import cancellation requested; capture remains retained";
      }
      else if (!event.repeat) lastError_ = pendingRecordingExportStarted_
          ? "Wait for the saved WAV to finish writing and verification"
          : "Press R to finish or retry publishing the current recording, or X to discard it";
      repaint(); return;
    }
    if (generationQueueView_) {
      using Key = seam::native_ui::NativeKey;
      if (event.key == Key::Escape) {
        if (controller_.generationRequestOutputInspectionLoading())
          record(controller_.cancelGenerationRequestOutputInspection());
        else if (controller_.proceduralImportBusy()) controller_.cancelProceduralCandidateImport();
        else if (!generationRequestDetailId_.empty()) {
          generationRequestDetailId_.clear(); generationRequestJobFirst_ = 0U;
        } else generationQueueView_ = false;
      }
      else if (event.key == Key::R && !event.repeat && !recording_.armed() &&
          recording_.recordedFrames() == 0U && !controller_.proceduralImportBusy())
        record(controller_.refreshGenerationRequests());
      else if (!generationRequestDetailId_.empty() &&
          (event.key == Key::Left || event.key == Key::Right)) {
        const auto rows = seam::native_ui::studioGenerationRequestDetailVisibleRows(controller_.logicalHeight());
        const auto request = std::find_if(controller_.generationRequests().begin(), controller_.generationRequests().end(), [this](const auto& entry) {
          return entry.request.requestId == generationRequestDetailId_;
        });
        if (request != controller_.generationRequests().end()) {
          if (event.key == Key::Left && generationRequestJobFirst_ > 0U)
            generationRequestJobFirst_ = generationRequestJobFirst_ > rows ? generationRequestJobFirst_ - rows : 0U;
          else if (event.key == Key::Right && generationRequestJobFirst_ + rows < request->request.jobs.size())
            generationRequestJobFirst_ += rows;
        }
      } else if (event.key == Key::Left && generationRequestFirst_ > 0U) {
        const auto rows = seam::native_ui::studioGenerationQueueVisibleRows(controller_.logicalHeight());
        generationRequestFirst_ = generationRequestFirst_ > rows ? generationRequestFirst_ - rows : 0U;
      } else if (event.key == Key::Right) {
        const auto rows = seam::native_ui::studioGenerationQueueVisibleRows(controller_.logicalHeight());
        if (generationRequestFirst_ + rows < controller_.generationRequests().size()) generationRequestFirst_ += rows;
      }
      repaint(); return;
    }
    if (sampleReviewView_) {
      using Key = seam::native_ui::NativeKey;
      if (event.repeat && event.key != Key::Left && event.key != Key::Right) return;
      std::string_view action;
      if (event.key == Key::Escape) action = controller_.proceduralImportBusy() ? "cancel" : "back";
      else if (event.key == Key::Q) action = "back";
      else if (event.key == Key::C) action = "capture";
      else if (event.key == Key::R) action = "reviewer";
      else if (event.key == Key::A) action = "accept";
      else if (event.key == Key::X) action = "reject";
      else if (event.key == Key::E) action = "publish";
      else if (event.key == Key::O) action = "open-manifest";
      else if (event.key == Key::N) action = "create-draft";
      else if (event.key == Key::I) action = "source-evidence";
      else if (event.key == Key::D) action = "source-decision";
      else if (event.key == Key::L) action = "source-license";
      else if (event.key == Key::S) action = "source-register";
      else if (event.key == Key::V) action = "register-reviewer";
      else if (event.key == Key::B) action = "sign-bank";
      else if (event.key == Key::P) action = "install-bank";
      else if (event.key == Key::Y) action = "open-bank-in-song-editor";
      else if (event.key == Key::Space) action = "play";
      else if (event.key == Key::Left) action = "previous-page";
      else if (event.key == Key::Right) action = "next-page";
      else if (event.key == Key::Up) action = "previous-unit";
      else if (event.key == Key::Down) action = "next-unit";
      if (!action.empty()) record(sampleReviewAction(action));
      repaint(); return;
    }
    if (!designerView_ && event.key == seam::native_ui::NativeKey::Q) {
      if (controller_.proceduralImportBusy() || recording_.armed() || recording_.recordedFrames() > 0U)
        record(seam::core::failure(seam::core::ErrorCode::Conflict, "Finish recording or production work before sample review"));
      else { stopAudition(); markerDrag_.reset(); pitchDrag_.reset(); sampleReviewView_ = true; sampleReviewFirstLine_ = 0U; }
      repaint(); return;
    }
    if (designerDrag_ && designer_.model()) {
      record(designer_.endGesture(designerDrag_->epoch, designer_.model()->revision(), false));
      designerDrag_.reset();
      if (event.key == seam::native_ui::NativeKey::Escape) { repaint(); return; }
    }
    if (designerView_) {
      if (event.key == seam::native_ui::NativeKey::Space && audition_.active()) stopAudition();
      else { stopAudition(); designerKey(event); }
      repaint(); return;
    }
    if (controller_.candidateMarkerDragging()) {
      controller_.cancelCandidateMarkerDrag();
      repaint();
      if (event.key == seam::native_ui::NativeKey::Escape) return;
    }
    if (event.key == seam::native_ui::NativeKey::Space) {
      if (!event.repeat) {
        if (audition_.active()) stopAudition();
        else record(auditionCandidate(event.modifiers.shift));
      }
      repaint();
      return;
    }
    stopAudition();
    if (event.key == seam::native_ui::NativeKey::D && event.modifiers.primaryShortcut()) {
      if (controller_.proceduralImportBusy() || recording_.armed() || recording_.recordedFrames() > 0U)
        record(seam::core::failure(seam::core::ErrorCode::Conflict, "Finish recording or production work before opening Designer"));
      else { markerDrag_.reset(); pitchDrag_.reset(); designerView_ = true; }
      repaint(); return;
    }
    if (controller_.proceduralImportBusy()) {
      if (event.key == seam::native_ui::NativeKey::Escape) controller_.cancelProceduralCandidateImport();
      repaint();
      return;
    }
    const auto size = controller_.selectableUnitCount();
    if ((event.key == seam::native_ui::NativeKey::Plus || event.key == seam::native_ui::NativeKey::Minus) &&
        controller_.manifest().units.empty() && controller_.candidateMarkerPreview()) {
      record(event.modifiers.alt ? controller_.panCandidateWaveform(event.key == seam::native_ui::NativeKey::Plus)
                                : controller_.zoomCandidateWaveform(event.key == seam::native_ui::NativeKey::Plus));
    } else if (event.modifiers.primaryShortcut() && (event.key == seam::native_ui::NativeKey::Z || event.key == seam::native_ui::NativeKey::Y) &&
        controller_.manifest().units.empty() && controller_.candidateMarkerPreview()) {
      if (recording_.armed() || recording_.recordedFrames() > 0U) {
        record(seam::core::failure(seam::core::ErrorCode::Conflict, "Finish recording before undoing candidate edits"));
      } else {
        record(event.key == seam::native_ui::NativeKey::Y || event.modifiers.shift
            ? controller_.redoCandidateMarkerEdit() : controller_.undoCandidateMarkerEdit());
      }
    } else if (event.modifiers.alt && (event.key == seam::native_ui::NativeKey::Left || event.key == seam::native_ui::NativeKey::Right) &&
        controller_.manifest().units.empty() && controller_.candidateMarkerPreview()) {
      if (recording_.armed() || recording_.recordedFrames() > 0U) {
        record(seam::core::failure(seam::core::ErrorCode::Conflict, "Finish recording before editing candidate bounds"));
      } else {
        const auto& preview = *controller_.candidateMarkerPreview();
        const auto& marker = preview.markers[controller_.selectedCandidateMarker()];
        const auto step = static_cast<seam::time::SampleFrame>(std::max(1U, preview.sampleRate / 1000U)) *
            (event.key == seam::native_ui::NativeKey::Left ? -1 : 1);
        record(controller_.editSelectedCandidateMarker(marker.ownedSpan.start + (event.modifiers.shift ? 0 : step),
            marker.ownedSpan.end + (event.modifiers.shift ? step : 0)));
      }
    } else if (event.key == seam::native_ui::NativeKey::Left && controller_.manifest().units.empty() && controller_.candidateMarkerPreview()) {
      record(controller_.moveCandidateMarker(-1));
    } else if (event.key == seam::native_ui::NativeKey::Right && controller_.manifest().units.empty() && controller_.candidateMarkerPreview()) {
      record(controller_.moveCandidateMarker(1));
    } else if (event.key == seam::native_ui::NativeKey::Up && controller_.selectedIndex() > 0U) {
      record(controller_.beginEditableUnitSelection(controller_.selectedIndex() - 1U));
    } else if (event.key == seam::native_ui::NativeKey::Down &&
               controller_.selectedIndex() + 1U < size) {
      record(controller_.beginEditableUnitSelection(controller_.selectedIndex() + 1U));
    } else if (event.key == seam::native_ui::NativeKey::S &&
               event.modifiers.primaryShortcut()) {
      record(controller_.save());
    } else if (event.key == seam::native_ui::NativeKey::R) {
      if (!event.repeat && event.modifiers.primaryShortcut()) record(importRecordedTakeFromDialog());
      else if (!event.repeat) record(startRecording());
    } else if (event.key == seam::native_ui::NativeKey::I && event.modifiers.primaryShortcut()) {
      record(event.modifiers.shift ? generationFromDialog() : importProceduralFromDialog());
    } else if (event.key == seam::native_ui::NativeKey::B && event.modifiers.primaryShortcut() && event.modifiers.shift) {
      record(generationFromDialog(true));
    } else if (event.key == seam::native_ui::NativeKey::C && event.modifiers.primaryShortcut() && event.modifiers.shift) {
      record(planCampaignFromDialog());
    } else if (event.key == seam::native_ui::NativeKey::Y && event.modifiers.primaryShortcut() && event.modifiers.shift) {
      record(runCampaignFromDialog());
    } else if (event.key == seam::native_ui::NativeKey::P && event.modifiers.shift) {
      record(preparationFromDialog());
    } else if (event.key == seam::native_ui::NativeKey::P) {
      if (recording_.armed() || recording_.recordedFrames() > 0U) {
        record(seam::core::failure(seam::core::ErrorCode::Conflict, "Finish recording before loading a waveform"));
      } else {
        record(controller_.beginCandidateWaveformPreview());
      }
    } else if (event.key == seam::native_ui::NativeKey::E && event.modifiers.primaryShortcut()) {
      record(exportPitchInspectionFromDialog());
    } else if (event.key == seam::native_ui::NativeKey::B) {
      record(event.modifiers.shift ? assembleBatchFromDialog() : controller_.toggleCandidatePitchView());
    } else if (event.key == seam::native_ui::NativeKey::N) {
      if (recording_.armed() || recording_.recordedFrames() > 0U)
        record(seam::core::failure(seam::core::ErrorCode::Conflict, "Finish recording before pitch inspection"));
      else record(controller_.beginCandidatePitchInspection());
    }
    repaint();
  }

  void textComposition(std::u32string, seam::ui::CompositionSelection) noexcept override {}
  void textCommit(std::u32string) noexcept override {}
  void textCancel() noexcept override {}
  bool wantsClose() const noexcept override { return false; }
  const seam::native_ui::AccessibilityTree* accessibilityTree() const noexcept override {
    return sampleReviewView_ ? &sampleReviewAccessibility_ : (designerView_ ? &designerAccessibility_ :
        controller_.productionProject() && controller_.manifest().units.empty()?&generationAccessibility_:&studioAccessibility_);
  }
  seam::core::Result<void> dispatchAccessibility(std::string_view id, seam::native_ui::SemanticAction action) noexcept override {
    using namespace seam::native_ui;
    if (recordingInput_.capturing() || recordingInput_.pending()) {
      // The capture pins its target row, so only stopping, retrying or discarding it stays reachable.
      const auto prefix = generationSemanticPrefix();
      const auto command = id.starts_with(prefix) ? id.substr(prefix.size()) : std::string_view{};
      if (designerView_ || sampleReviewView_ || (command != "record" && command != "discard-recording"))
        return seam::core::failure(seam::core::ErrorCode::Conflict,
            "Finish or publish the pending recording before changing its target");
    }
    if (!designerView_ && !sampleReviewView_ && controller_.productionProject() && controller_.manifest().units.empty()) {
      const auto prefix=generationSemanticPrefix();
      if (generationModal_ || !id.starts_with(prefix)) return seam::core::failure(seam::core::ErrorCode::Conflict,"Generation accessibility target is stale or modal");
      return generationAccessibility_.dispatch(id,action,[&](std::string_view target,SemanticAction selected)->seam::core::Result<void> {
        if (selected==SemanticAction::SetFocus) { generationSemanticFocus_=target; return generationAccessibility_.setFocus(target); }
        if (selected!=SemanticAction::Activate) return seam::core::failure(seam::core::ErrorCode::Unsupported,"Generation status is read-only");
        lastError_.clear();
        const std::string command{target.substr(prefix.size())};
        const auto result=command=="import-wav"?importRecordedTakeFromDialog()
            :command=="record"?recordingAction():command=="discard-recording"?discardRecording()
            :generationControlAction(command);
        record(result); repaint(); return result;
      });
    }
    if (sampleReviewView_) {
      const auto prefix = sampleSemanticPrefix();
      if (sampleReviewModal_ || !id.starts_with(prefix)) return seam::core::failure(seam::core::ErrorCode::Conflict, "Sample review accessibility target is stale");
      return sampleReviewAccessibility_.dispatch(id, action, [&](std::string_view target, SemanticAction selected) -> seam::core::Result<void> {
        if (selected == SemanticAction::SetFocus) { sampleReviewSemanticFocus_ = target; return sampleReviewAccessibility_.setFocus(target); }
        if (selected != SemanticAction::Activate) return seam::core::failure(seam::core::ErrorCode::Unsupported, "Review data is inspection-only; use the explicit controls");
        // A new action starts with a clean error line, as a key press does; its own failure sets it again.
        lastError_.clear();
        const auto result = sampleReviewAction(target.substr(prefix.size())); record(result); repaint(); return result;
      });
    }
    if (!designerView_) {
      const auto prefix=generationSemanticPrefix();
      if (takeImportModal_ || !id.starts_with(prefix))
        return seam::core::failure(seam::core::ErrorCode::Conflict,"Studio accessibility target is stale or modal");
      return studioAccessibility_.dispatch(id,action,[&](std::string_view target,SemanticAction selected)->seam::core::Result<void> {
        if (selected==SemanticAction::SetFocus) { studioSemanticFocus_=target; return studioAccessibility_.setFocus(target); }
        if (selected!=SemanticAction::Activate) return seam::core::failure(seam::core::ErrorCode::Unsupported,"Studio status is read-only");
        lastError_.clear();
        const auto command=target.substr(prefix.size());
        const auto result=command=="import-wav"?importRecordedTakeFromDialog()
            :command=="record"?recordingAction():command=="discard-recording"?discardRecording()
            :seam::core::failure(seam::core::ErrorCode::Unsupported,"Unknown Studio action");
        record(result); repaint(); return result;
      });
    }
    const auto prefix = designerSemanticPrefix();
    const bool transportOnly = id == prefix+"cancel-preview" || id == prefix+"stop";
    if (!designerView_ || !id.starts_with(prefix) || (!transportOnly && (designer_.busy() || designerDrag_)))
      return seam::core::failure(seam::core::ErrorCode::Conflict, "Designer accessibility target is stale or busy");
    return designerAccessibility_.dispatch(id, action, [&](std::string_view target, SemanticAction selected) -> seam::core::Result<void> {
      const auto suffix = target.substr(designerSemanticPrefix().size());
      if (selected == SemanticAction::SetFocus) {
        designerSemanticFocus_ = target;
        if (suffix.starts_with("control.")) {
          std::size_t index = 0U; const auto number = suffix.substr(8U);
          const auto parsed = std::from_chars(number.data(), number.data()+number.size(), index);
          if (parsed.ec == std::errc{}) designerControl_ = index;
        }
        repaint(); return designerAccessibility_.setFocus(target);
      }
      if (selected != SemanticAction::Activate) return seam::core::failure(seam::core::ErrorCode::Unsupported, "Set the numeric field value to edit this control");
      if (suffix == "cancel-preview") { designer_.cancelAudition(); repaint(); return seam::core::success(); }
      if (suffix == "stop") { stopAudition(); repaint(); return seam::core::success(); }
      if (suffix == "render") {
        stopAudition(); const auto result = designer_.beginAudition(); record(result); repaint(); return result;
      }
      if (suffix == "prepare-draft") {
        const auto result = prepareDesignerFromDialog(); record(result); repaint(); return result;
      }
      if (suffix.starts_with("render-articulation.") || suffix.starts_with("play-articulation.")) {
        const auto phone=std::string{suffix.substr(suffix.find('.')+1U)};
        if (!designer_.model() || phone.empty())
          return seam::core::failure(seam::core::ErrorCode::InvalidArgument,"Articulation action target is invalid");
        // The error a refused stop left goes when the action is asked again, as it does for the
        // keyed actions: a refusal sets it again and a success leaves it clear. Play does not stop
        // the audition first: its start stops it, once, and answers for it. Asked twice, a stop that
        // only the first ask refuses would leave its error on screen beside a source that plays. The
        // other actions stop it and report a refusal.
        lastError_.clear();
        if (!suffix.starts_with("play-articulation.")) stopAudition();
        if (suffix.starts_with("render-articulation.")) {
          const auto result=designer_.beginArticulationAudition(phone); record(result); repaint(); return result;
        }
        if (!designer_.articulationAudio() || designer_.articulationAudioPhone()!=phone)
          return seam::core::failure(seam::core::ErrorCode::Conflict,"Articulation preview is no longer ready");
        const auto result=playDesignerAudio(designer_.articulationAudio(),phone+" + SELECTED VOWEL / NOT APPROVED");
        record(result); repaint(); return result;
      }
      if (suffix.starts_with("remove-plosive.") || suffix.starts_with("plosive-seed.") || suffix.starts_with("render-plosive.") || suffix.starts_with("render-plosive-phrase.") || suffix.starts_with("render-plosive-coda.") || suffix.starts_with("play-plosive.")) {
        const auto number=suffix.substr(suffix.find('.')+1U); std::size_t index=0U;
        const auto parsed=std::from_chars(number.data(),number.data()+number.size(),index);
        if (parsed.ec!=std::errc{} || parsed.ptr!=number.data()+number.size() || !designer_.model())
          return seam::core::failure(seam::core::ErrorCode::InvalidArgument,"Plosive action target is invalid");
        lastError_.clear();
        if (!suffix.starts_with("play-plosive.")) stopAudition();  // Play's start stops it, once
        if (suffix.starts_with("render-plosive.") || suffix.starts_with("render-plosive-phrase.") || suffix.starts_with("render-plosive-coda.")) {
          using Mode=seam::native_ui::PlosiveAuditionMode;
          const auto mode=suffix.starts_with("render-plosive-coda.")?Mode::VowelStop:suffix.starts_with("render-plosive-phrase.")?Mode::StopVowel:Mode::Source;
          const auto result=designer_.beginPlosiveAudition(index,mode); record(result); repaint(); return result;
        }
        if (suffix.starts_with("play-plosive.")) {
          if (!designer_.plosiveAudio() || designer_.plosiveAudioIndex()!=index)
            return seam::core::failure(seam::core::ErrorCode::Conflict,"Plosive preview is no longer ready");
          const auto result=playDesignerAudio(designer_.plosiveAudio(),designer_.plosiveAudioIsCoda()?"SELECTED VOWEL + STOP / NOT APPROVED":designer_.plosiveAudioIsPhrase()?"STOP + SELECTED VOWEL / NOT APPROVED":"PLOSIVE SOURCE / 50 ms CLOSURE / NOT APPROVED");
          record(result); repaint(); return result;
        }
        const auto result=suffix.starts_with("remove-plosive.")?designer_.removePlosive(designer_.epoch(),designer_.model()->revision(),index):editDesignerPlosiveSeed(index);
        record(result); repaint(); return result;
      }
      if (suffix.starts_with("remove-frication.") || suffix.starts_with("frication-seed.") ||
          suffix.starts_with("render-frication.") || suffix.starts_with("render-frication-phrase.") || suffix.starts_with("render-frication-coda.") || suffix.starts_with("play-frication.")) {
        const auto number = suffix.substr(suffix.find('.')+1U); std::size_t index = 0U;
        const auto parsed = std::from_chars(number.data(),number.data()+number.size(),index);
        if (parsed.ec != std::errc{} || parsed.ptr != number.data()+number.size() || !designer_.model())
          return seam::core::failure(seam::core::ErrorCode::InvalidArgument,"Frication action target is invalid");
        lastError_.clear();
        if (!suffix.starts_with("play-frication.")) stopAudition();  // Play's start stops it, once
        if (suffix.starts_with("render-frication.") || suffix.starts_with("render-frication-phrase.") || suffix.starts_with("render-frication-coda.")) {
          using Mode=seam::native_ui::FricationAuditionMode;
          const auto mode=suffix.starts_with("render-frication-coda.")?Mode::VowelFrication:suffix.starts_with("render-frication-phrase.")?Mode::FricationVowel:Mode::Source;
          const auto result = designer_.beginFricationAudition(index,mode); record(result); repaint(); return result;
        }
        if (suffix.starts_with("play-frication.")) {
          if (!designer_.fricationAudio() || designer_.fricationAudioIndex() != index)
            return seam::core::failure(seam::core::ErrorCode::Conflict,"Frication preview is no longer ready");
          const auto result = playDesignerAudio(designer_.fricationAudio(),fricationPreviewDescription()+" / NOT APPROVED");
          record(result); repaint(); return result;
        }
        const auto result = suffix.starts_with("remove-frication.") ? designer_.removeFrication(designer_.epoch(),designer_.model()->revision(),index)
            : editDesignerFricationSeed(index);
        record(result); repaint(); return result;
      }
      if (suffix == "previous" || suffix == "next") {
        if (designer_.model()) {
          const auto count = designerControlCount(designer_.model()->recipe(), designer_.auditionPose());
          const auto page = seam::native_ui::voiceDesignerLayout(controller_.logicalHeight()).visibleRows;
          designerControl_ = suffix == "next" ? std::min(designerControl_+page,count-1U) : (designerControl_ > page ? designerControl_-page : 0U);
          designerSemanticFocus_ = designerSemanticPrefix() + "control." + std::to_string(designerControl_);
        }
      } else {
        NativeKey key = NativeKey::Unknown; bool shift = false;
        if (suffix == "new") key = NativeKey::N;
        else if (suffix == "open") key = NativeKey::O;
        else if (suffix == "save" || suffix == "save-as") { key = NativeKey::S; shift = suffix == "save-as"; }
        else if (suffix == "publish-singer") {
          lastError_.clear(); stopAudition();
          const auto result = publishDesignerSingerFromDialog();
          record(result); repaint(); return result;
        }
        else if (suffix == "install-published-singer") {
          lastError_.clear(); stopAudition();
          const auto result = installPublishedSinger();
          record(result); repaint(); return result;
        }
        else if (suffix == "open-in-song-editor") {
          lastError_.clear(); stopAudition();
          const auto result = openInstalledSingerInSongEditor();
          record(result); repaint(); return result;
        }
        else if (suffix == "create-producer") {
          lastError_.clear(); stopAudition();
          const auto result = createDesignerProducerWorkspace();
          record(result); repaint(); return result;
        }
        else if (suffix == "back") key = NativeKey::D;
        else if (suffix == "add-frication") key = NativeKey::E;
        else if (suffix == "add-plosive") key = NativeKey::I;
        else if (suffix == "duplicate-pose" || suffix == "remove-pose") { key = NativeKey::L; shift = suffix == "remove-pose"; }
        else if (suffix == "undo" || suffix == "redo") { key = NativeKey::Z; shift = suffix == "redo"; }
        else if (suffix == "pin-reference" || suffix == "clear-reference") { key = NativeKey::B; shift = suffix == "clear-reference"; }
        else if (suffix == "play-current" || suffix == "play-reference") {
          if ((suffix == "play-current" && !designer_.auditionAudio()) || (suffix == "play-reference" && !designer_.referenceMatchesSelection()))
            return seam::core::failure(seam::core::ErrorCode::Conflict, "Requested audition is no longer available");
          // The audition that plays goes first, and the session says so when it cannot: a device that
          // does not stop leaves the old audition playing, with its label and the error in place, and
          // the creator's action is answered with that failure and not with success.
          lastError_.clear();
          const auto result = designerSpace({.key = NativeKey::Space, .modifiers = {.shift = suffix == "play-reference"}});
          record(result); repaint(); return result;
        } else return seam::core::failure(seam::core::ErrorCode::Unsupported, "Unknown Designer action");
        lastError_.clear(); stopAudition();
        designerKey({.key = key, .modifiers = {.shift = shift, .control = true, .command = true}});
      }
      repaint(); return seam::core::success();
    });
  }
  seam::core::Result<void> setAccessibilityValue(std::string_view id, std::string_view value) override {
    if (designerView_ && designer_.model() && !designer_.busy() && !designerDrag_ && id == designerSemanticPrefix()+"seed" &&
        seam::native_ui::EditorSemanticTree::containsId(designerAccessibility_.root(), id)) {
      stopAudition();
      const auto result = designer_.setSeed(designer_.epoch(), designer_.model()->revision(), value);
      if (result) { lastError_.clear(); designerSemanticFocus_ = designerSemanticPrefix()+"seed"; } else record(result);
      repaint(); return result;
    }
    const auto prefix = designerSemanticPrefix() + "control.";
    if (!designerView_ || !designer_.model() || designer_.busy() || designerDrag_ || !id.starts_with(prefix) || value.empty() || value.size() > 128U ||
        !seam::native_ui::EditorSemanticTree::containsId(designerAccessibility_.root(), id))
      return seam::core::failure(seam::core::ErrorCode::Conflict, "Designer value target is stale or busy");
    std::size_t control = 0U; const auto index = id.substr(prefix.size());
    const auto parsedIndex = std::from_chars(index.data(), index.data()+index.size(), control);
    double number = 0.0;
    const auto parsed = seam::core::parseFiniteDecimal(value, number);
    const auto* model = designer_.model(); const auto count = designerControlCount(model->recipe(), designer_.auditionPose());
    if (parsedIndex.ec != std::errc{} || parsedIndex.ptr != index.data()+index.size() || control >= count ||
        !parsed)
      return seam::core::failure(seam::core::ErrorCode::InvalidArgument, "Designer numeric value is invalid");
    stopAudition();
    seam::core::Result<void> result = seam::core::success();
    if (control == 3U || control == 4U) {
      if (number != std::floor(number) || number < 0.0 || number > 127.0)
        return seam::core::failure(seam::core::ErrorCode::InvalidArgument, "Pose and pitch must be bounded integers");
      result = designer_.selectAudition(designer_.epoch(), model->revision(), control == 3U ? static_cast<std::size_t>(number) : designer_.auditionPose(),
          control == 4U ? static_cast<std::uint8_t>(number) : designer_.auditionPitch());
    } else {
      auto desired = model->recipe();
      if (control == 0U) desired.phonation.openQuotient = number;
      else if (control == 1U) desired.phonation.spectralTiltDbPerOctave = number;
      else if (control == 2U) desired.phonation.aspiration = number;
      else if (control == 5U) desired.modulation.jitterCents = number;
      else if (control == 6U) desired.modulation.shimmerAmount = number;
      else if (control == 7U) desired.modulation.rateHz = number;
      else {
        const auto oralEnd = 8U + desired.poses[designer_.auditionPose()].formants.size()*3U;
        const auto pose=designer_.auditionPose();
        const auto nasalStart=designerNasalStart(desired,pose);
        if (control>=nasalStart) setNasalControl(desired.poses[designer_.auditionPose()],control-nasalStart,number);
        else if (control>=designerBreathStart(desired,pose)) {
          auto& source=desired.breaths[designerBreaths(desired,pose)[(control-designerBreathStart(desired,pose))/3U]].source;
          const auto parameter=(control-designerBreathStart(desired,pose))%3U;
          if (parameter==0U) source.centerHz=number;
          else if (parameter==1U) source.bandwidthHz=number;
          else source.gain=number;
        }
        else if (control>=designerApproximantStart(desired,pose)) {
          desired.approximants[designerApproximants(desired,pose)[control-designerApproximantStart(desired,pose)]].transitionMilliseconds=number;
        }
        else if (control>=designerVoicedAffricateStart(desired,pose)) {
          const auto relative=control-designerVoicedAffricateStart(desired,pose);
          auto& affricate=desired.voicedAffricates[designerVoicedAffricates(desired,pose)[relative/10U]];
          const auto parameter=relative%10U;
          if (parameter<6U) {
            auto& source=parameter<3U?affricate.burst:affricate.tail;
            const auto field=parameter%3U;
            if (field==0U) source.centerHz=number;
            else if (field==1U) source.bandwidthHz=number;
            else source.gain=number;
          } else if (parameter==6U) affricate.burstMilliseconds=number;
          else if (parameter==7U) affricate.closureVoicingGain=number;
          else if (parameter==8U) affricate.closureLowpassHz=number;
          else affricate.tailVoicingGain=number;
        }
        else if (control>=designerAffricateStart(desired,pose)) {
          const auto relative=control-designerAffricateStart(desired,pose);
          auto& affricate=desired.affricates[designerAffricates(desired,pose)[relative/7U]];
          const auto parameter=relative%7U;
          if (parameter<6U) {
            auto& source=parameter<3U?affricate.burst:affricate.tail;
            const auto field=parameter%3U;
            if (field==0U) source.centerHz=number;
            else if (field==1U) source.bandwidthHz=number;
            else source.gain=number;
          } else affricate.burstMilliseconds=number;
        }
        else if (control>=designerPlosiveStart(desired,designer_.auditionPose())) {
          const auto relative=control-designerPlosiveStart(desired,designer_.auditionPose());
          setPlosiveControl(desired.plosives[designerPlosives(desired,designer_.auditionPose())[relative/4U]],relative%4U,number);
        }
        else if (control >= oralEnd) {
          auto& frication = desired.frications[designerFrications(desired, designer_.auditionPose())[(control-oralEnd)/4U]];
          auto& source=frication.source;
          if ((control-oralEnd)%4U == 0U) source.centerHz = number;
          else if ((control-oralEnd)%4U == 1U) source.bandwidthHz = number;
          else if ((control-oralEnd)%4U == 2U) source.gain = number;
          else seam::native_ui::setDesignerFricationVoicing(frication,number);
        } else {
          auto& band = desired.poses[designer_.auditionPose()].formants[(control-8U)/3U];
          if ((control-8U)%3U == 0U) band.frequencyHz = number;
          else if ((control-8U)%3U == 1U) band.bandwidthHz = number;
          else band.gainDb = number;
        }
      }
      result = designer_.edit(designer_.epoch(), model->revision(), std::move(desired));
    }
    if (result) { lastError_.clear(); designerControl_ = control; designerSemanticFocus_ = designerSemanticPrefix() + "control." + std::to_string(control); } else record(result);
    repaint(); return result;
  }
  bool requestClose() noexcept override {
    if (closeConfirmationActive_) return false;
    struct Guard final { bool& active; explicit Guard(bool& flag) : active(flag) { active = true; } ~Guard() { active = false; } } guard{closeConfirmationActive_};
    try {
      if (recordingInput_.capturing() || recordingInput_.pending()) {
        const auto captured = stopRecording();
        if (!captured) { record(captured); repaint(); return false; }
      }
      const auto epoch = designer_.epoch();
      const auto revision = designer_.model() ? designer_.model()->revision() : 0U;
      const auto discard = allowDesignerReplacement();
      if (!discard) { lastError_ = discard.error().message; designerView_ = true; repaint(); return false; }
      if (!discard.value()) return false;
      auto dialog = platform_.fileDialog();
      const auto sample = controller_.confirmSampleClose(*dialog);
      if (!sample) { lastError_ = sample.error().message; designerView_ = false; repaint(); return false; }
      if (designer_.busy() || designer_.epoch() != epoch ||
          (designer_.model() ? designer_.model()->revision() : 0U) != revision) {
        lastError_ = "Designer changed during close confirmation; review the current draft"; repaint(); return false;
      }
      return sample.value();
    } catch (const std::exception& error) {
      lastError_ = error.what(); repaint(); return false;
    } catch (...) { return false; }
  }

  [[nodiscard]] const std::string& lastError() const noexcept override { return lastError_; }
  [[nodiscard]] const std::filesystem::path& lastRecording() const noexcept override {
    return lastRecording_;
  }
  [[nodiscard]] std::size_t lastRecordedFrames() const noexcept override {
    return lastRecordedFrames_;
  }
  [[nodiscard]] seam::platform::AudioInputDeviceInfo inputInfo() const override {
    return recordingInput_.info();
  }
  [[nodiscard]] seam::platform::AudioInputDeviceStats inputStats() const noexcept override {
    return recordingInput_.stats();
  }
  [[nodiscard]] const seam::voicebank_production::VoicebankProductionProject*
  productionProject() const noexcept override {
    return controller_.productionProject();
  }
  [[nodiscard]] seam::voicebank_production::ProductionQueueSummary
  productionQueues() const noexcept override {
    return controller_.productionQueues();
  }
  [[nodiscard]] std::size_t stagedRecoveryCandidateCount() const noexcept override {
    return controller_.stagedRecoveryCandidateCount();
  }

  [[nodiscard]] const seam::voicebank_production::PublishedSampleCandidate*
  publishedSampleCandidate() const noexcept override {
    return controller_.publishedSampleCandidate() ? &*controller_.publishedSampleCandidate() : nullptr;
  }

  [[nodiscard]] const seam::native_ui::SampleBankInstallation*
  installedSampleBank() const noexcept override {
    return controller_.installedSampleBank() ? &*controller_.installedSampleBank() : nullptr;
  }

  [[nodiscard]] const seam::authoring::InventoryPreflightReport*
  campaignPreflightReport() const noexcept override {
    return controller_.campaignPreflightReport() ? &*controller_.campaignPreflightReport() : nullptr;
  }

  bool recordingTargetSelected() const noexcept {
    return controller_.productionProject() != nullptr
        ? controller_.selectedProductionAssignment() != nullptr
        : controller_.selectedUnit() != nullptr;
  }
  // A failed or lost capture must not keep describing itself as capturing. The error line and the
  // recording status carry the same message; the status, unlike the transient error line, survives
  // the next key press, so the creator can still read why nothing was published and that Record is
  // the way to retry. The returned error carries that message to accessibility and command-line callers.
  seam::core::Error noteRecordingFailure(std::string_view prefix, const seam::core::Error& error) {
    recordingStatus_ = std::string{prefix} + error.message;
    lastError_ = recordingStatus_;
    return {error.code, recordingStatus_, error.context};
  }
  struct RecordingControl final {
    std::string id, label, name, description;
    seam::ui::Rect bounds;
    bool enabled{false};
  };
  // Recording is reachable from the unit rail header by pointer and by accessibility, not only through
  // the R key: a screen-reader user has to be able to start, stop, retry and discard a take, and the
  // stop and discard actions stay available while every other target is locked by the capture.
  std::vector<RecordingControl> recordingControls(double primaryWidth = 94.0,
                                                  double discardWidth = 94.0) const {
    if (designerView_ || sampleReviewView_ || generationQueueView_) return {};
    const bool capturing = recordingInput_.capturing(), pending = recordingInput_.pending();
    const bool publishing = pendingRecordingExportStarted_ || pendingRecordingImportStarted_;
    // The buttons are as wide as their labels at the size they are drawn (kStudioControlText), which
    // they were not: at 7 point the longest label fitted 94 points and at 12 it does not, so the
    // labels were cut off mid-word. The row is pinned to kStudioRecordTop, which clears the header
    // panel above and the units rail below: the status is a different row and a different column, so
    // a message that wraps onto a second line cannot land on a button.
    const seam::ui::Rect primary{kStudioRecordLeft,kStudioRecordTop,std::max(94.0,primaryWidth),kStudioRecordHeight},
        secondary{kStudioRecordLeft + std::max(94.0,primaryWidth) + 6.0,kStudioRecordTop,
                  std::max(94.0,discardWidth),kStudioRecordHeight};
    std::vector<RecordingControl> controls;
    if (capturing)
      controls.push_back({"record","R STOP + PUBLISH","Stop recording and publish the take",
          "Shortcut R. Stops the microphone, writes the WAV and imports it as an unapproved take for review.",
          primary,true});
    else if (publishing)
      controls.push_back({"record","PUBLISHING","Publishing the recorded take",
          "The recorded WAV is being written, verified and imported. Escape cancels the import; the capture is kept.",
          primary,false});
    else if (pending)
      controls.push_back({"record","R RETRY","Retry publishing the recorded take",
          "Shortcut R. Writes and imports the retained capture again without recording it again.",
          primary,!controller_.proceduralImportBusy()});
    else
      controls.push_back({"record","R RECORD","Record a take for the selected row",
          "Shortcut R. Opens the microphone only now. The take is imported unapproved and needs marker and pitch review.",
          primary,recordingTargetSelected() && !controller_.proceduralImportBusy() && !takeImportModal_ && !generationModal_});
    if (pending && !publishing)
      controls.push_back({"discard-recording","X DISCARD","Discard the retained capture",
          "Shortcut X. Drops the in-memory capture only. A WAV already saved in the recordings folder is kept.",
          secondary,true});
    return controls;
  }
  seam::core::Result<void> recordingAction() {
    if (recordingInput_.capturing() || recordingInput_.pending()) return stopRecording();
    return startRecording();
  }
  void addRecordingAccessibility(seam::native_ui::SemanticNode& root, const std::string& prefix) const {
    using seam::native_ui::SemanticAction;
    for (const auto& control : recordingControls())
      root.children.push_back({.id=prefix+control.id,.role=seam::native_ui::SemanticRole::Button,.name=control.name,
          .bounds=control.bounds,.enabled=control.enabled,
          .actions=control.enabled?std::vector<SemanticAction>{SemanticAction::Activate,SemanticAction::SetFocus}:std::vector<SemanticAction>{},
          .description=control.description});
    root.children.push_back({.id=prefix+"microphone",.role=seam::native_ui::SemanticRole::Status,.name="Microphone input",
        .value=recordingInput_.capturing() ? "Capturing from " + inputBackend_ : "Not capturing / " + inputBackend_,
        .bounds={controller_.logicalWidth()-360.0,40.0,344.0,14.0}});
  }

  seam::core::Result<void> startRecording() override {
    stopAudition();
    if (controller_.proceduralImportBusy()) return seam::core::failure(seam::core::ErrorCode::Conflict,
        "Finish candidate import before recording");
    // A take belongs to a producer row or a manifest unit. Without one there is nowhere to publish it,
    // so the microphone is not opened at all rather than capturing into an unrelated folder.
    if (!recordingTargetSelected())
      return seam::core::failure(seam::core::ErrorCode::InvalidState,
          "Open a producer workspace or voicebank and select a row before recording; the microphone was not opened");
    const auto started = recordingInput_.begin();
    const auto input = recordingInput_.info();
    inputBackend_ = input.backend;
    if (!input.deviceName.empty()) inputBackend_ += " / " + input.deviceName;
    if (!started)
      return seam::core::Result<void>{noteRecordingFailure("NO TAKE STARTED / ", started.error())};
    controller_.cancelCandidateMarkerDrag();
    markerDrag_.reset();
    pitchDrag_.reset();
    designerView_ = false;
    lastRecordedFrames_ = 0U;
    lastRecording_.clear();
    pendingRecordingPath_.clear();
    pendingRecordingHash_.clear();
    recordingStatus_ = "CAPTURING FROM " + input.deviceName;
    lastError_.clear();
    return seam::core::success();
  }

  seam::core::Result<void> stopRecording() override {
    const auto finished = recordingInput_.finish();
    if (!finished) {
      if (recordingInput_.capturing() || recordingInput_.pending()) return finished;
      return seam::core::Result<void>{noteRecordingFailure("NO TAKE RECORDED / ", finished.error())};
    }
    if (!recordingInput_.pending()) return seam::core::success();
    if (pendingRecordingImportStarted_ || pendingRecordingExportStarted_) return seam::core::success();
    const auto frames = recording_.recordedFrames();
    lastRecordedFrames_ = frames;
    std::error_code error;
    auto directory = controller_.recordingDirectory();
    std::filesystem::create_directories(directory, error);
    if (error) {
      return seam::core::failure(seam::core::ErrorCode::IoError,
                                 "Unable to create recording directory",
                                 error.message());
    }
    const auto directoryStatus = std::filesystem::symlink_status(directory, error);
    if (error || std::filesystem::is_symlink(directoryStatus) ||
        !std::filesystem::is_directory(directoryStatus)) {
      return seam::core::failure(
          seam::core::ErrorCode::Conflict,
          "Recording directory is not a real directory", directory.string());
    }
    const auto* productionAssignment = controller_.selectedProductionAssignment();
    auto name = controller_.selectedUnit() != nullptr
                    ? controller_.selectedUnit()->id
                    : productionAssignment != nullptr
                          ? productionAssignment->plannedTakeId
                          : std::string{"take"};
    if (controller_.productionProject() != nullptr) {
      if (pendingRecordingHash_.empty()) {
        if (!pendingRecordingPath_.empty()) {
          // A previous export failed after writing or could not be hashed. Keep
          // that user file untouched; a retry writes a fresh direct-child WAV.
          if (recoveryExportAttempts_ >= 1U)
            return seam::core::failure(seam::core::ErrorCode::Conflict,
                "Recovery already wrote a new WAV that still could not be verified; press X to discard the capture "
                "(the saved file is kept) or inspect the recording directory",
                pendingRecordingPath_.string());
          ++recoveryExportAttempts_;
        }
        const auto destination = seam::native_ui::nextVoicebankRecordingPath(directory, name);
        if (!destination) return seam::core::Result<void>{destination.error()};
        pendingRecordingPath_ = destination.value();
        lastRecording_ = pendingRecordingPath_;
      }
      return beginPendingRecordingExport(pendingRecordingPath_, pendingRecordingHash_);
    }
    if (pendingRecordingHash_.empty()) {
      // First publication, or recovery from a written file whose identity could
      // not be read. Recovery always writes a NEW file from the retained capture;
      // the earlier file remains untouched user data and is never rebound.
      if (!pendingRecordingPath_.empty()) {
        if (recoveryExportAttempts_ >= 1U)
          return seam::core::failure(seam::core::ErrorCode::Conflict,
              "Recovery already wrote a new WAV that still could not be verified; press X to discard the capture "
              "(the saved file is kept) or inspect the recording directory",
              pendingRecordingPath_.string());
        ++recoveryExportAttempts_;
      }
      const auto destination = seam::native_ui::nextVoicebankRecordingPath(directory, name);
      if (!destination) return seam::core::Result<void>{destination.error()};
      const auto saved = recordingInput_.exportPending(destination.value());
      if (!saved) return saved;  // The valid capture remains pending for a retry.
      // A successful write is retained immediately, even if its identity cannot
      // be read. Never make another WAV or bind later, potentially edited bytes
      // as the original capture merely because a hash retry succeeds.
      pendingRecordingPath_ = destination.value();
      lastRecording_ = pendingRecordingPath_;
      const auto hash = seam::core::sha256File(destination.value());
      if (!hash) return seam::core::failure(hash.error().code,
          "Recording WAV was saved but its identity could not be verified; press R to retry, press X to discard the capture",
          pendingRecordingPath_.string());
      pendingRecordingHash_ = hash.value();
    } else {
      const auto hash = seam::core::sha256File(pendingRecordingPath_);
      if (!hash) return seam::core::Result<void>{hash.error()};
      if (hash.value() != pendingRecordingHash_)
        return seam::core::failure(seam::core::ErrorCode::Conflict,
            "Saved recording changed before publication; press R to retry or X to discard the capture",
            pendingRecordingPath_.string());
    }
    const auto expectedRootMidi = controller_.selectedUnit() != nullptr
                                      ? controller_.selectedUnit()->rootMidi
                                      : productionAssignment != nullptr
                                            ? productionAssignment->pitchLayer
                                            : 60;
    const auto inspected = controller_.inspectTake(
        pendingRecordingPath_, expectedRootMidi);
    if (!inspected) return inspected;
    const auto persisted = controller_.persistTakeInspection(pendingRecordingPath_);
    if (!persisted) return seam::core::Result<void>{persisted.error()};
    if (controller_.productionProject() != nullptr) {
      auto imported = controller_.importSelectedTake(pendingRecordingPath_);
      if (!imported) return imported;
    }
    const auto published = recordingInput_.acknowledgePublished();
    if (!published) return published;
    pendingRecordingPath_.clear();
    pendingRecordingHash_.clear();
    recoveryExportAttempts_ = 0U;
    lastError_.clear();
    return seam::core::success();
  }

  // Explicit escape from a capture whose publication keeps failing. The in-memory
  // capture is dropped; any WAV already written stays on disk as user data.
  seam::core::Result<void> discardRecording() {
    if (!recordingInput_.pending())
      return seam::core::failure(seam::core::ErrorCode::InvalidState,
          "There is no completed recording to discard");
    if (pendingRecordingExportStarted_ || pendingRecordingImportStarted_)
      return seam::core::failure(seam::core::ErrorCode::Conflict,
          "Wait for recorded WAV export/import to settle before discarding its capture");
    const auto preserved = pendingRecordingPath_;
    const auto discarded = recordingInput_.discardPending();
    if (!discarded) return discarded;
    pendingRecordingPath_.clear();
    pendingRecordingHash_.clear();
    recoveryExportAttempts_ = 0U;
    lastError_.clear();
    recordingStatus_ = preserved.empty()
        ? std::string{"Recording capture discarded; no file had been written"}
        : "Recording capture discarded; saved file kept at " + preserved.string();
    return seam::core::success();
  }

private:
  struct PendingRecordingExport final {
    std::filesystem::path path;
    std::string sha256;
  };

  seam::core::Result<void> beginPendingRecordingExport(
      std::filesystem::path path, std::string expectedSha256) {
    if (pendingRecordingExportStarted_)
      return seam::core::failure(seam::core::ErrorCode::Conflict,
          "Recorded WAV export is already running");
    try {
      recordingExport_ = std::async(std::launch::async,
          [this, path = std::move(path), expectedSha256 = std::move(expectedSha256)]()
              -> seam::core::Result<PendingRecordingExport> {
        if (expectedSha256.empty()) {
          const auto written = recordingInput_.exportPending(path);
          if (!written) return seam::core::Result<PendingRecordingExport>{written.error()};
        }
        const auto digest = seam::core::sha256File(path);
        if (!digest) return seam::core::Result<PendingRecordingExport>{digest.error()};
        if (!expectedSha256.empty() && digest.value() != expectedSha256)
          return seam::core::failure<PendingRecordingExport>(seam::core::ErrorCode::Conflict,
              "Saved recording changed before publication; retry or discard the retained capture",
              path.string());
        return seam::core::success(PendingRecordingExport{std::move(path), digest.value()});
      });
      pendingRecordingExportStarted_ = true;
      recordingStatus_ = "WRITING AND VERIFYING WAV / CAPTURE RETAINED";
      lastError_.clear();
      return seam::core::success();
    } catch (const std::exception& error) {
      return seam::core::failure(seam::core::ErrorCode::Internal,
          "Unable to start recorded WAV export", error.what());
    }
  }

  seam::core::Result<void> pollPendingRecordingExport() {
    if (!pendingRecordingExportStarted_ || !recordingExport_.valid() ||
        recordingExport_.wait_for(std::chrono::seconds{0}) != std::future_status::ready)
      return seam::core::success();
    pendingRecordingExportStarted_ = false;
    try {
      auto result = recordingExport_.get();
      if (!result) {
        lastError_ = result.error().message;
        recordingStatus_ = "RECORDED WAV EXPORT FAILED / CAPTURE RETAINED";
        return seam::core::Result<void>{result.error()};
      }
      pendingRecordingPath_ = result.value().path;
      pendingRecordingHash_ = result.value().sha256;
      lastRecording_ = pendingRecordingPath_;
      const auto imported = controller_.beginRawTakeImport(
          pendingRecordingPath_, {}, pendingRecordingHash_);
      if (!imported) {
        lastError_ = imported.error().message;
        recordingStatus_ = "RECORDED WAV READY / IMPORT RETRY AVAILABLE";
        return imported;
      }
      pendingRecordingImportStarted_ = true;
      recordingStatus_ = "WAV VERIFIED / IMPORTING TAKE";
      return seam::core::success();
    } catch (const std::exception& error) {
      lastError_ = "Recorded WAV export worker failed";
      recordingStatus_ = "RECORDED WAV EXPORT FAILED / CAPTURE RETAINED";
      return seam::core::failure(seam::core::ErrorCode::Internal,
          lastError_, error.what());
    }
  }

  void repaint() noexcept {
    if (window_ != nullptr) window_->requestRepaint();
  }
  void completePendingRecordingImport(const seam::core::Result<void>& result) noexcept {
    if (!pendingRecordingImportStarted_ || controller_.proceduralImportBusy()) return;
    if (!result) {
      // Keep the captured audio and its already-written WAV so the creator can
      // retry publication without recording again or rebinding different bytes.
      pendingRecordingImportStarted_ = false;
      recordingStatus_ = "TAKE IMPORT FAILED / CAPTURE AND WAV RETAINED";
      return;
    }
    const auto published = recordingInput_.acknowledgePublished();
    if (!published) {
      lastError_ = published.error().message;
      pendingRecordingImportStarted_ = false;
      return;
    }
    pendingRecordingImportStarted_ = false;
    pendingRecordingPath_.clear();
    pendingRecordingHash_.clear();
    recoveryExportAttempts_ = 0U;
    lastError_.clear();
    recordingStatus_.clear();
  }
  void record(const seam::core::Result<void>& result) noexcept {
    if (!result) lastError_ = result.error().message;
  }

  seam::native_ui::CandidateAuditionSession audition_;
  std::string auditionStatus_;
  std::string recordingStatus_;
  std::string designerPublishStatus_;
  std::filesystem::path publishedSingerPackagePath_;
  std::string publishedSingerPackageDigest_;
  std::string publishedSingerDisplayName_;
  std::optional<seam::distribution::Ed25519PublicKey> publishedSingerPublicKey_;
  bool publishedSingerInstalled_{false};
  // What the last installation actually produced; the song hand-off binds to this exact singer.
  std::optional<seam::distribution::InstalledProceduralSinger> installedSinger_;
  // The public key of the package this session signed last, and what the bank hand-off last did. The
  // private key is never retained; the song editor is asked to trust exactly this key on installation.
  std::optional<seam::distribution::Ed25519PublicKey> publishedSampleBankKey_;
  std::string sampleBankStatus_;
  std::uint64_t publishedDesignerEpoch_{};
  std::uint64_t publishedDesignerRevision_{};
  seam::native_ui::VoicebankStudioController controller_;
  seam::native_ui::VoicebankStudioScenePainter painter_;
  // Declared before the recording members because their input session is built from these
  // factories; every other operating-system effect in this class also goes through it.
  StudioPlatform platform_;
  seam::platform::RecordingSession recording_;
  seam::platform::RecordingInputSession recordingInput_;
  std::future<seam::core::Result<PendingRecordingExport>> recordingExport_;
  bool pendingRecordingExportStarted_{false};
  std::string inputBackend_{"OFF"};
  std::optional<seam::ui::AcousticMarkerKind> markerDrag_;
  std::optional<std::size_t> pitchDrag_;
  seam::native_ui::INativeWindow* window_{nullptr};
  std::string lastError_;
  seam::native_ui::VoiceDesignerSession designer_;
  seam::native_ui::AccessibilityTree designerAccessibility_;
  std::string designerSemanticFocus_;
  bool designerView_{false};
  bool sampleReviewView_{false}, sampleReviewModal_{false};
  std::size_t sampleReviewFirstLine_{0U};
  seam::native_ui::AccessibilityTree sampleReviewAccessibility_;
  seam::native_ui::AccessibilityTree generationAccessibility_;
  std::string generationSemanticFocus_;
  bool generationQueueView_{false};
  std::size_t generationRequestFirst_{0U};
  std::string generationRequestDetailId_;
  std::size_t generationRequestJobFirst_{0U};
  seam::native_ui::AccessibilityTree studioAccessibility_;
  std::string studioSemanticFocus_;
  bool generationModal_{false}, takeImportModal_{false};
  std::string sampleReviewSemanticFocus_;
  bool designerConfirmationActive_{false};
  bool closeConfirmationActive_{false};
  std::size_t designerControl_{0U};
  struct DesignerDrag final {
    std::uint64_t epoch;
    std::size_t control, pose, firstRow;
    double startX;
    seam::voice_design::VoiceRecipe baseline;
  };
  std::optional<DesignerDrag> designerDrag_;
  std::filesystem::path lastRecording_;
  std::filesystem::path pendingRecordingPath_;
  std::string pendingRecordingHash_;
  bool pendingRecordingImportStarted_{false};
  std::size_t recoveryExportAttempts_{0U};
  std::size_t lastRecordedFrames_{0U};
};

}  // namespace

std::unique_ptr<IVoicebankStudioApp> createVoicebankStudioApp(
    bool forceSyntheticInput, StudioPlatform platform) {
  return std::make_unique<VoicebankStudioApp>(forceSyntheticInput, std::move(platform));
}

}  // namespace seam::voicebank_studio_native
