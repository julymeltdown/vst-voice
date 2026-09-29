#pragma once

#include "options.hpp"

#include "seam/core/result.hpp"
#include "seam/distribution/procedural_package.hpp"
#include "seam/native_ui/native_window.hpp"
#include "seam/native_ui/sample_bank_package.hpp"
#include "seam/platform/application_menu.hpp"
#include "seam/platform/audio_device.hpp"
#include "seam/platform/audio_input_device.hpp"
#include "seam/platform/file_dialog.hpp"
#include "seam/platform/recording_input_session.hpp"
#include "seam/voicebank_production/operations.hpp"
#include "seam/voicebank_production/project.hpp"
#include "seam/voicebank_production/repository.hpp"
#include "seam/voicebank/catalog.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace seam::voicebank_studio_native {

// Every operating-system effect Voicebank Studio performs: modal file and entry dialogs, audio
// output for auditions, microphone input, the installed-singer folders, and handing a song project
// to the song editor. The application reaches none of these directly, so the same code runs in the
// shipped app and under tests that drive it through its real keyboard, pointer and accessibility
// actions. Defaults are the native implementations.
struct StudioPlatform final {
  std::function<std::unique_ptr<platform::IFileDialog>()> fileDialog{platform::createNativeFileDialog};
  std::function<std::unique_ptr<platform::IAudioDevice>()> audioDevice{platform::createSystemAudioDevice};
  platform::RecordingInputFactories recordingInput{};
  std::function<std::vector<distribution::ProceduralSearchRoot>()> singerRoots{
      distribution::defaultProceduralSearchRoots};
  // The voicebank roots the song editor catalogs. A signed sample bank built here is installed into
  // the installed root of this list and the hand-off re-scans the same list, so the bank Studio
  // writes is the bank the editor finds; a test supplies its own folder instead of the user library.
  std::function<std::vector<voicebank::VoicebankSearchRoot>()> voicebankRoots{
      voicebank::defaultVoicebankSearchRoots};
  std::function<core::Result<std::filesystem::path>()> locateSongEditor{
      platform::locateSongEditorApplication};
  std::function<core::Result<void>(const std::filesystem::path&, const std::filesystem::path&)>
      openDocumentWithApplication{platform::openDocumentWithApplication};
};

// The native window client plus the few entry points the command line uses. Everything a creator
// does goes through the INativeWindowClient surface: paint, pointer, key and accessibility actions.
class IVoicebankStudioApp : public native_ui::INativeWindowClient {
public:
  [[nodiscard]] virtual core::Result<void> open(const Options& options) = 0;
  virtual void setWindow(native_ui::INativeWindow& window) noexcept = 0;

  [[nodiscard]] virtual core::Result<voicebank_production::ExportedU57Inputs>
  exportProductionInputs(const std::filesystem::path& destination) = 0;
  [[nodiscard]] virtual core::Result<void> selectProductionUnit(std::size_t index) = 0;
  [[nodiscard]] virtual core::Result<voicebank_production::CommittedDerivedRevision>
  applyProductionOperation(const voicebank_production::OperationRequest& request) = 0;
  [[nodiscard]] virtual core::Result<void> importProductionTake(
      const std::filesystem::path& path) = 0;

  [[nodiscard]] virtual core::Result<void> startRecording() = 0;
  [[nodiscard]] virtual core::Result<void> stopRecording() = 0;
  virtual void finishPendingImport() = 0;

  [[nodiscard]] virtual const std::string& lastError() const noexcept = 0;
  [[nodiscard]] virtual const std::filesystem::path& lastRecording() const noexcept = 0;
  [[nodiscard]] virtual std::size_t lastRecordedFrames() const noexcept = 0;
  [[nodiscard]] virtual platform::AudioInputDeviceInfo inputInfo() const = 0;
  [[nodiscard]] virtual platform::AudioInputDeviceStats inputStats() const noexcept = 0;
  [[nodiscard]] virtual const voicebank_production::VoicebankProductionProject*
  productionProject() const noexcept = 0;
  [[nodiscard]] virtual voicebank_production::ProductionQueueSummary
  productionQueues() const noexcept = 0;
  [[nodiscard]] virtual std::size_t stagedRecoveryCandidateCount() const noexcept = 0;
  // The signed package and installation this session produced, so a caller can verify the bank that
  // was actually installed instead of inferring it from a folder listing.
  [[nodiscard]] virtual const voicebank_production::PublishedSampleCandidate*
  publishedSampleCandidate() const noexcept = 0;
  [[nodiscard]] virtual const native_ui::SampleBankInstallation*
  installedSampleBank() const noexcept = 0;
};

[[nodiscard]] std::unique_ptr<IVoicebankStudioApp> createVoicebankStudioApp(
    bool forceSyntheticInput, StudioPlatform platform = {});

}  // namespace seam::voicebank_studio_native
