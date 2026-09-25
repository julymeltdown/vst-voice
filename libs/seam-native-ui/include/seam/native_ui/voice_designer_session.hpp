#pragma once
#include "seam/native_ui/voice_designer_model.hpp"
#include "seam/native_ui/voice_designer_audition.hpp"
#include "seam/distribution/procedural_package.hpp"
#include "seam/distribution/signing.hpp"
#include <future>
#include <string_view>

namespace seam::native_ui {
class VoiceDesignerSession final {
public:
  ~VoiceDesignerSession();
  // Roots the Designer must never save into. An installed singer's files are signed and immutable, so
  // a draft belongs outside them; a save aimed inside a protected root is refused rather than
  // overwriting signed content. Empty by default, because a caller that has no installed singers has
  // nothing to protect.
  void setProtectedRoots(std::vector<std::filesystem::path> roots) {
    protectedRoots_ = std::move(roots);
  }
  [[nodiscard]] const std::vector<std::filesystem::path>& protectedRoots() const noexcept {
    return protectedRoots_;
  }
  [[nodiscard]] bool busy() const noexcept { return work_.valid(); }
  [[nodiscard]] bool auditionBusy() const noexcept { return auditionWork_.valid(); }
  [[nodiscard]] const std::shared_ptr<const voicebank::AudioBuffer>& auditionAudio() const noexcept { return auditionAudio_; }
  [[nodiscard]] core::Result<void> beginAudition();
  [[nodiscard]] core::Result<void> beginFricationAudition(std::size_t index, FricationAuditionMode mode = FricationAuditionMode::Source);
  [[nodiscard]] FricationAuditionMode fricationAudioMode() const noexcept { return fricationAudio_?fricationAudioMode_:FricationAuditionMode::Source; }
  [[nodiscard]] core::Result<void> beginPlosiveAudition(std::size_t index, bool phrase = false);
  [[nodiscard]] core::Result<void> beginPlosiveAudition(std::size_t index, PlosiveAuditionMode mode);
  [[nodiscard]] PlosiveAuditionMode plosiveAudioMode() const noexcept { return plosiveAudio_?plosiveAudioMode_:PlosiveAuditionMode::Source; }
  [[nodiscard]] bool plosiveAudioIsPhrase() const noexcept { return plosiveAudio_ && plosiveAudioMode_!=PlosiveAuditionMode::Source; }
  [[nodiscard]] bool plosiveAudioIsCoda() const noexcept { return plosiveAudio_ && plosiveAudioMode_==PlosiveAuditionMode::VowelStop; }
  [[nodiscard]] const std::shared_ptr<const voicebank::AudioBuffer>& plosiveAudio() const noexcept { return plosiveAudio_; }
  [[nodiscard]] core::Result<void> beginArticulationAudition(std::string phone);
  [[nodiscard]] const std::shared_ptr<const voicebank::AudioBuffer>& articulationAudio() const noexcept { return articulationAudio_; }
  [[nodiscard]] const std::optional<std::string>& articulationAudioPhone() const noexcept { return articulationAudioPhone_; }
  [[nodiscard]] std::optional<std::size_t> plosiveAudioIndex() const noexcept { return plosiveAudioIndex_; }
  [[nodiscard]] const std::shared_ptr<const voicebank::AudioBuffer>& fricationAudio() const noexcept { return fricationAudio_; }
  [[nodiscard]] std::optional<std::size_t> fricationAudioIndex() const noexcept { return fricationAudioIndex_; }
  [[nodiscard]] core::Result<void> selectAudition(std::uint64_t epoch, std::uint64_t revision,
      std::size_t poseIndex, std::uint8_t midiKey);
  [[nodiscard]] std::size_t auditionPose() const noexcept { return auditionPose_; }
  [[nodiscard]] std::uint8_t auditionPitch() const noexcept { return auditionPitch_; }
  struct AuditionReference final {
    synthesis::ProceduralSingerResource resource;
    std::string phone, style;
    std::uint8_t midiKey;
    std::shared_ptr<const voicebank::AudioBuffer> audio;
  };
  [[nodiscard]] core::Result<void> pinAuditionReference(std::uint64_t epoch, std::uint64_t revision);
  void clearAuditionReference() noexcept { auditionReference_.reset(); }
  [[nodiscard]] const std::optional<AuditionReference>& auditionReference() const noexcept { return auditionReference_; }
  [[nodiscard]] bool referenceMatchesSelection() const noexcept;
  [[nodiscard]] const VoiceDesignerModel* model() const noexcept { return model_ ? &*model_ : nullptr; }
  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
  [[nodiscard]] std::uint64_t epoch() const noexcept { return epoch_; }
  [[nodiscard]] core::Result<void> create(voice_design::VoiceRecipe recipe, bool discardUnsaved = false);
  // Creates an unqualified Japanese source-filter starter with a partial phone inventory.
  // Its screening defaults are not phonetic qualification or a finished singer.
  [[nodiscard]] core::Result<void> createJapaneseStarter(bool discardUnsaved = false);
  [[nodiscard]] core::Result<void> beginOpen(std::filesystem::path path, bool discardUnsaved = false);
  // Save As is new-file-only. Saving the currently opened file checks its last
  // observed canonical identity before atomic replacement.
  [[nodiscard]] core::Result<void> beginSave(std::filesystem::path path);
  // Publish only the exact, clean recipe already persisted at path(). This is
  // a package operation, not review or quality approval; signer material and
  // distribution metadata are explicit caller inputs.
  [[nodiscard]] core::Result<distribution::ProceduralPackageInfo> publishSavedSinger(
      const std::filesystem::path& stagingDirectory,
      const std::filesystem::path& outputPackage,
      const distribution::SigningKeyPair& signingKey,
      const distribution::PublishProceduralSingerOptions& options) const;
  // Install only a trusted package for this exact, still-current saved recipe.
  // This is an explicit user action and never upgrades the package to quality-approved.
  [[nodiscard]] core::Result<distribution::InstalledProceduralSinger> installPublishedSinger(
      std::uint64_t expectedEpoch, std::uint64_t expectedRevision,
      const std::filesystem::path& packagePath,
      std::string_view expectedPackageDigest,
      const distribution::Ed25519PublicKey& trustedSigner,
      const std::filesystem::path& installRoot) const;
  [[nodiscard]] core::Result<void> poll();
  [[nodiscard]] core::Result<void> finish();
  void cancel() noexcept { stop_.request_stop(); auditionStop_.request_stop(); }
  void cancelAudition() noexcept { auditionStop_.request_stop(); }
  [[nodiscard]] core::Result<void> edit(std::uint64_t epoch, std::uint64_t revision, voice_design::VoiceRecipe recipe);
  [[nodiscard]] core::Result<void> setSeed(std::uint64_t epoch, std::uint64_t revision, std::string_view decimalSeed);
  [[nodiscard]] core::Result<void> duplicatePose(std::uint64_t epoch, std::uint64_t revision,
      std::string phone, std::string style, std::optional<std::size_t> expectedSourcePose = std::nullopt);
  [[nodiscard]] core::Result<void> removePose(std::uint64_t epoch, std::uint64_t revision);
  [[nodiscard]] core::Result<void> addFrication(std::uint64_t epoch, std::uint64_t revision, std::string phone, std::string style);
  [[nodiscard]] core::Result<void> removeFrication(std::uint64_t epoch, std::uint64_t revision, std::size_t index);
  [[nodiscard]] core::Result<void> setFricationSeed(std::uint64_t epoch, std::uint64_t revision, std::size_t index, std::string_view decimalSeed);
  [[nodiscard]] core::Result<void> addPlosive(std::uint64_t epoch, std::uint64_t revision, std::string phone, std::string style);
  [[nodiscard]] core::Result<void> removePlosive(std::uint64_t epoch, std::uint64_t revision, std::size_t index);
  [[nodiscard]] core::Result<void> setPlosiveSeed(std::uint64_t epoch, std::uint64_t revision, std::size_t index, std::string_view decimalSeed);
  [[nodiscard]] core::Result<void> beginGesture(std::uint64_t epoch, std::uint64_t revision);
  [[nodiscard]] core::Result<void> updateGesture(std::uint64_t epoch, std::uint64_t revision, voice_design::VoiceRecipe recipe);
  [[nodiscard]] core::Result<void> endGesture(std::uint64_t epoch, std::uint64_t revision, bool commit);
  [[nodiscard]] core::Result<void> undo(std::uint64_t epoch, std::uint64_t revision);
  [[nodiscard]] core::Result<void> redo(std::uint64_t epoch, std::uint64_t revision);
private:
  [[nodiscard]] core::Result<void> beginNoiseAudition(std::size_t index, bool plosive, PlosiveAuditionMode mode = PlosiveAuditionMode::Source);
  [[nodiscard]] core::Result<void> setSeedAt(std::uint64_t epoch, std::uint64_t revision,
      std::string_view decimalSeed, std::optional<std::size_t> frication, std::optional<std::size_t> plosive = {});
  [[nodiscard]] core::Result<void> canReplace(bool discardUnsaved) const;
  struct FileResult final {
    std::filesystem::path path;
    std::string hash;
    std::uint64_t revision{0U};
    std::optional<VoiceDesignerModel> opened;
  };
  std::optional<VoiceDesignerModel> model_;
  std::filesystem::path path_;
  std::string persistedHash_;
  std::vector<std::filesystem::path> protectedRoots_;
  std::uint64_t epoch_{0U};
  std::future<core::Result<FileResult>> work_;
  std::stop_source stop_;
  struct AuditionResult final {
    std::uint64_t epoch, revision; std::size_t poseIndex; std::uint8_t midiKey; voicebank::AudioBuffer audio;
    std::optional<std::size_t> frication{std::nullopt};
    std::optional<std::size_t> plosive{std::nullopt};
    PlosiveAuditionMode plosiveMode{PlosiveAuditionMode::Source};
    std::optional<std::string> articulationPhone{std::nullopt};
  };
  std::future<core::Result<AuditionResult>> auditionWork_;
  std::stop_source auditionStop_;
  std::shared_ptr<const voicebank::AudioBuffer> auditionAudio_;
  std::shared_ptr<const voicebank::AudioBuffer> fricationAudio_;
  std::optional<std::size_t> fricationAudioIndex_;
  FricationAuditionMode fricationAudioMode_{FricationAuditionMode::Source};
  std::shared_ptr<const voicebank::AudioBuffer> plosiveAudio_;
  std::optional<std::size_t> plosiveAudioIndex_;
  PlosiveAuditionMode plosiveAudioMode_{PlosiveAuditionMode::Source};
  std::shared_ptr<const voicebank::AudioBuffer> articulationAudio_;
  std::optional<std::string> articulationAudioPhone_;
  std::optional<AuditionReference> auditionReference_;
  std::size_t auditionPose_{0U};
  std::uint8_t auditionPitch_{69U};
  void invalidateAudition() noexcept { auditionStop_.request_stop(); auditionAudio_.reset(); fricationAudio_.reset(); fricationAudioIndex_.reset(); plosiveAudio_.reset(); plosiveAudioIndex_.reset(); articulationAudio_.reset(); articulationAudioPhone_.reset(); }
};
}
