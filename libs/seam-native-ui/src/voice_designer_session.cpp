#include "seam/native_ui/voice_designer_session.hpp"
#include <chrono>
#include <limits>
#include <charconv>
#include "seam/core/exclusive_file_lock.hpp"

namespace seam::native_ui {
VoiceDesignerSession::~VoiceDesignerSession() { cancel(); if (work_.valid()) work_.wait(); if (auditionWork_.valid()) auditionWork_.wait(); }
core::Result<void> VoiceDesignerSession::canReplace(bool discardUnsaved) const {
  if (busy()) return core::failure(core::ErrorCode::Conflict, "Designer file operation is busy");
  if (epoch_ == std::numeric_limits<std::uint64_t>::max()) return core::failure(core::ErrorCode::Conflict, "Designer session epoch is exhausted");
  if (model_ && (model_->gestureActive() || (model_->dirty() && !discardUnsaved)))
    return core::failure(core::ErrorCode::Conflict, "Save or explicitly discard the current Designer changes first");
  return core::success();
}
core::Result<void> VoiceDesignerSession::create(voice_design::VoiceRecipe recipe, bool discardUnsaved) {
  const auto ready = canReplace(discardUnsaved); if (!ready) return ready;
  auto created = VoiceDesignerModel::create(std::move(recipe));
  if (!created) return core::Result<void>{created.error()};
  model_ = std::move(created.value()); path_.clear(); persistedHash_.clear();
  invalidateAudition();
  ++epoch_;
  auditionPose_ = 0U; auditionPitch_ = 69U;
  auditionReference_.reset();
  return core::success();
}
core::Result<void> VoiceDesignerSession::createJapaneseStarter(bool discardUnsaved) {
  return create(voice_design::makeJapaneseStarterRecipe(), discardUnsaved);
}
core::Result<void> VoiceDesignerSession::beginOpen(std::filesystem::path path, bool discardUnsaved) {
  const auto ready = canReplace(discardUnsaved); if (!ready) return ready;
  stop_ = std::stop_source{}; const auto stop = stop_.get_token();
  try {
    work_ = std::async(std::launch::async, [path = std::move(path), stop]() -> core::Result<FileResult> {
      std::error_code error; const auto absolute = std::filesystem::absolute(path, error).lexically_normal();
      if (error) return core::failure<FileResult>(core::ErrorCode::InvalidArgument, "Cannot resolve Designer recipe path");
      const auto resource = voice_design::loadVoiceRecipeResource(absolute, {}, stop);
      if (!resource) return core::Result<FileResult>{resource.error()};
      auto recipe = voice_design::decodeVoiceRecipeResource(resource.value(), stop);
      if (!recipe) return core::Result<FileResult>{recipe.error()};
      auto opened = VoiceDesignerModel::create(std::move(recipe.value()));
      if (!opened) return core::Result<FileResult>{opened.error()};
      const auto acknowledged = opened.value().acknowledgeSave(0U, resource.value().identity.contentHash);
      if (!acknowledged) return core::Result<FileResult>{acknowledged.error()};
      if (stop.stop_requested()) return core::failure<FileResult>(core::ErrorCode::Conflict, "Designer open cancelled");
      return FileResult{absolute, resource.value().identity.contentHash, 0U, std::move(opened.value())};
    });
  } catch (const std::exception& error) { return core::failure(core::ErrorCode::Internal, "Cannot start Designer open", error.what()); }
  return core::success();
}
core::Result<void> VoiceDesignerSession::beginSave(std::filesystem::path path) {
  if (busy() || !model_ || model_->gestureActive())
    return core::failure(core::ErrorCode::Conflict, "Finish Designer work before saving");
  std::error_code error; path = std::filesystem::absolute(path, error).lexically_normal();
  if (error) return core::failure(core::ErrorCode::InvalidArgument, "Cannot resolve Designer save path");
  // An installed singer is signed and immutable, so a draft is never written inside one. The check
  // runs before any background work starts, so a refused save leaves the session untouched.
  // An installed procedural resource is self-describing: its directory carries the manifest and the
  // installation receipt beside the recipe. Detecting that marker needs no configuration, so the
  // refusal holds even for a caller that never declares a protected root.
  {
    const auto parent = path.parent_path();
    std::error_code markerError;
    if (!parent.empty() &&
        std::filesystem::is_regular_file(parent / "install-receipt.json", markerError) &&
        std::filesystem::is_regular_file(parent / "manifest.json", markerError))
      return core::failure(core::ErrorCode::Conflict,
                           "A draft cannot be saved over an installed singer", parent.string());
  }
  for (const auto& root : protectedRoots_) {
    if (root.empty()) continue;
    std::error_code rootError;
    const auto canonicalRoot = std::filesystem::weakly_canonical(root, rootError);
    if (rootError) continue;
    const auto canonicalPath = std::filesystem::weakly_canonical(path, rootError);
    if (rootError) continue;
    const auto rootText = canonicalRoot.generic_string();
    const auto pathText = canonicalPath.generic_string();
    if (pathText == rootText ||
        (pathText.size() > rootText.size() && pathText.compare(0, rootText.size(), rootText) == 0 &&
         pathText[rootText.size()] == '/'))
      return core::failure(core::ErrorCode::Conflict,
                           "A draft cannot be saved inside an installed singer", rootText);
  }
  const auto recipe = model_->recipe(); const auto revision = model_->revision();
  const auto hash = model_->resource().identity.contentHash;
  const auto previousHash = path == path_ ? persistedHash_ : std::string{};
  stop_ = std::stop_source{}; const auto stop = stop_.get_token();
  try {
    work_ = std::async(std::launch::async, [path = std::move(path), recipe, revision, hash, previousHash, stop]() -> core::Result<FileResult> {
      core::ExclusiveFileLock lock;
      const auto locked = lock.acquire(path.string() + ".designer.lock");
      if (!locked) return core::Result<FileResult>{locked.error()};
      if (!previousHash.empty()) {
        const auto existing = voice_design::loadVoiceRecipeResource(path, {}, stop);
        if (!existing) return core::Result<FileResult>{existing.error()};
        if (existing.value().identity.contentHash != previousHash)
          return core::failure<FileResult>(core::ErrorCode::Conflict, "Recipe changed outside the Designer; save to a new file");
      }
      const auto bytes = voice_design::encodeVoiceRecipe(recipe);
      if (!bytes) return core::Result<FileResult>{bytes.error()};
      if (stop.stop_requested()) return core::failure<FileResult>(core::ErrorCode::Conflict, "Designer save cancelled");
      const auto saved = previousHash.empty() ? core::durableAtomicWriteTextNew(path, bytes.value())
                                             : core::durableAtomicWriteText(path, bytes.value());
      if (!saved) return core::Result<FileResult>{saved.error()};
      return FileResult{path, hash, revision, std::nullopt};
    });
  } catch (const std::exception& exception) { return core::failure(core::ErrorCode::Internal, "Cannot start Designer save", exception.what()); }
  return core::success();
}
core::Result<distribution::ProceduralPackageInfo> VoiceDesignerSession::publishSavedSinger(
    const std::filesystem::path& stagingDirectory,
    const std::filesystem::path& outputPackage,
    const distribution::SigningKeyPair& signingKey,
    const distribution::PublishProceduralSingerOptions& options) const {
  using Output = distribution::ProceduralPackageInfo;
  if (busy() || !model_ || model_->gestureActive())
    return core::failure<Output>(core::ErrorCode::Conflict,
        "Finish Designer work before publishing a singer");
  if (path_.empty() || persistedHash_.empty() || model_->dirty() ||
      model_->resource().identity.contentHash != persistedHash_)
    return core::failure<Output>(core::ErrorCode::Conflict,
        "Save the exact current Designer recipe before publishing");

  // A previous save acknowledgement is not enough: the file may have changed
  // outside this session since it was opened or saved. Re-read and bind
  // publication to the exact persisted content before signing it.
  const auto persisted = voice_design::loadVoiceRecipeResource(path_);
  if (!persisted) return core::Result<Output>{persisted.error()};
  if (persisted.value().identity != model_->resource().identity ||
      persisted.value().identity.contentHash != persistedHash_)
    return core::failure<Output>(core::ErrorCode::Conflict,
        "Saved Designer recipe changed outside this session; reopen or save a new copy");

  std::error_code pathError;
  const auto draftPath = std::filesystem::weakly_canonical(path_, pathError);
  if (pathError)
    return core::failure<Output>(core::ErrorCode::IoError,
        "Cannot resolve the saved Designer recipe path", pathError.message());
  const auto packagePath = std::filesystem::weakly_canonical(outputPackage, pathError);
  if (pathError)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Cannot resolve the singer package output path", pathError.message());
  if (draftPath == packagePath)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Singer package output cannot replace the saved Designer recipe");

  // Distribution metadata is required explicitly. The package signature
  // establishes publisher authenticity only, never singer quality approval.
  return distribution::publishProceduralSingerFromRecipe(
      persisted.value(), stagingDirectory, outputPackage, signingKey, options);
}
core::Result<distribution::InstalledProceduralSinger> VoiceDesignerSession::installPublishedSinger(
    std::uint64_t expectedEpoch, std::uint64_t expectedRevision,
    const std::filesystem::path& packagePath,
    std::string_view expectedPackageDigest,
    const distribution::Ed25519PublicKey& trustedSigner,
    const std::filesystem::path& installRoot) const {
  using Output = distribution::InstalledProceduralSinger;
  if (busy() || !model_ || model_->gestureActive() || epoch_ != expectedEpoch ||
      model_->revision() != expectedRevision)
    return core::failure<Output>(core::ErrorCode::Conflict,
        "Designer changed after publication; return to the published recipe before installing");
  if (packagePath.empty() || expectedPackageDigest.empty() || installRoot.empty())
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Published singer package and install folder are required");
  if (path_.empty() || persistedHash_.empty() || model_->dirty() ||
      model_->resource().identity.contentHash != persistedHash_)
    return core::failure<Output>(core::ErrorCode::Conflict,
        "Install requires the exact saved Designer recipe used for publication");
  const auto persisted = voice_design::loadVoiceRecipeResource(path_);
  if (!persisted) return core::Result<Output>{persisted.error()};
  if (persisted.value().identity != model_->resource().identity ||
      persisted.value().identity.contentHash != persistedHash_)
    return core::failure<Output>(core::ErrorCode::Conflict,
        "Saved Designer recipe changed outside this session; reopen or save a new copy");

  distribution::VerifySeambankOptions verification;
  verification.trustedPublicKeys = {trustedSigner};
  verification.requireTrustedSigner = true;
  const auto package = distribution::verifyProceduralPackage(packagePath, verification);
  if (!package) return core::Result<Output>{package.error()};
  if (package.value().manifest.id != model_->recipe().id ||
      package.value().manifest.recipeSha256 != persistedHash_)
    return core::failure<Output>(core::ErrorCode::Conflict,
        "Published singer package does not contain this exact saved Designer recipe");
  if (package.value().container.packageDigest != expectedPackageDigest)
    return core::failure<Output>(core::ErrorCode::Conflict,
        "Published singer package bytes differ from the captured package identity");

  distribution::InstallProceduralOptions options;
  options.verification = std::move(verification);
  options.expectedPackageDigest = std::string{expectedPackageDigest};
  options.replaceExisting = false;
  return distribution::installProceduralPackage(packagePath, installRoot, options);
}
core::Result<void> VoiceDesignerSession::poll() {
  if (auditionWork_.valid() && auditionWork_.wait_for(std::chrono::seconds{0}) == std::future_status::ready) {
    try {
      auto preview = auditionWork_.get();
      if (!auditionStop_.stop_requested()) {
        if (!preview) return core::Result<void>{preview.error()};
        if (model_ && preview.value().epoch == epoch_ && preview.value().revision == model_->revision() &&
            preview.value().poseIndex == auditionPose_ && preview.value().midiKey == auditionPitch_) {
          if (preview.value().plosive) {
            plosiveAudioIndex_=preview.value().plosive;
            plosiveAudioMode_=preview.value().plosiveMode;
            plosiveAudio_=std::make_shared<const voicebank::AudioBuffer>(std::move(preview.value().audio));
          } else if (preview.value().frication) {
            fricationAudioIndex_ = preview.value().frication;
            fricationAudioMode_ = preview.value().plosiveMode==PlosiveAuditionMode::Source?FricationAuditionMode::Source:
                preview.value().plosiveMode==PlosiveAuditionMode::StopVowel?FricationAuditionMode::FricationVowel:FricationAuditionMode::VowelFrication;
            fricationAudio_ = std::make_shared<const voicebank::AudioBuffer>(std::move(preview.value().audio));
          } else if (preview.value().articulationPhone) {
            articulationAudioPhone_=std::move(preview.value().articulationPhone);
            articulationAudio_=std::make_shared<const voicebank::AudioBuffer>(std::move(preview.value().audio));
          } else auditionAudio_ = std::make_shared<const voicebank::AudioBuffer>(std::move(preview.value().audio));
        }
      }
    } catch (const std::exception& error) { return core::failure(core::ErrorCode::Internal, "Designer audition worker failed", error.what()); }
  }
  if (!work_.valid() || work_.wait_for(std::chrono::seconds{0}) != std::future_status::ready) return core::success();
  try {
    auto result = work_.get(); if (!result) return core::Result<void>{result.error()};
    if (result.value().opened) {
      model_ = std::move(result.value().opened); ++epoch_; invalidateAudition();
      auditionPose_ = 0U; auditionPitch_ = 69U;
      auditionReference_.reset();
    }
    else {
      if (!model_) return core::failure(core::ErrorCode::InvalidState, "Saved Designer model is missing");
      const auto acknowledged = model_->acknowledgeSave(result.value().revision, result.value().hash);
      if (!acknowledged) return acknowledged;
    }
    path_ = std::move(result.value().path); persistedHash_ = std::move(result.value().hash);
    return core::success();
  } catch (const std::exception& error) { return core::failure(core::ErrorCode::Internal, "Designer file worker failed", error.what()); }
}
core::Result<void> VoiceDesignerSession::finish() { cancel(); if (work_.valid()) work_.wait(); if (auditionWork_.valid()) auditionWork_.wait(); return poll(); }
core::Result<void> VoiceDesignerSession::pinAuditionReference(std::uint64_t epoch, std::uint64_t revision) {
  if (busy() || auditionBusy() || !model_ || !auditionAudio_ || epoch != epoch_ || revision != model_->revision())
    return core::failure(core::ErrorCode::Conflict, "Render the current Designer preview before pinning a reference");
  const auto& pose = model_->recipe().poses[auditionPose_];
  auditionReference_ = AuditionReference{model_->resource(), pose.phone, pose.style, auditionPitch_, auditionAudio_};
  return core::success();
}
bool VoiceDesignerSession::referenceMatchesSelection() const noexcept {
  if (!model_ || !auditionReference_ || auditionPose_ >= model_->recipe().poses.size()) return false;
  const auto& pose = model_->recipe().poses[auditionPose_];
  return auditionReference_->phone == pose.phone && auditionReference_->style == pose.style && auditionReference_->midiKey == auditionPitch_;
}

core::Result<void> VoiceDesignerSession::selectAudition(std::uint64_t epoch, std::uint64_t revision,
    std::size_t poseIndex, std::uint8_t midiKey) {
  if (busy() || !model_ || epoch != epoch_ || revision != model_->revision())
    return core::failure(core::ErrorCode::Conflict, "Designer audition selection is stale or busy");
  if (poseIndex >= model_->recipe().poses.size() || midiKey < 36U || midiKey > 96U)
    return core::failure(core::ErrorCode::InvalidArgument, "Audition pose or pitch is outside its bounds");
  if (poseIndex != auditionPose_ || midiKey != auditionPitch_) {
    invalidateAudition(); auditionPose_ = poseIndex; auditionPitch_ = midiKey;
  }
  return core::success();
}
core::Result<void> VoiceDesignerSession::beginFricationAudition(std::size_t index,FricationAuditionMode mode) {
  if (mode!=FricationAuditionMode::Source && mode!=FricationAuditionMode::FricationVowel && mode!=FricationAuditionMode::VowelFrication)
    return core::failure(core::ErrorCode::InvalidArgument,"Unknown frication audition mode");
  return beginNoiseAudition(index,false,mode==FricationAuditionMode::Source?PlosiveAuditionMode::Source:
      mode==FricationAuditionMode::FricationVowel?PlosiveAuditionMode::StopVowel:PlosiveAuditionMode::VowelStop);
}
core::Result<void> VoiceDesignerSession::beginPlosiveAudition(std::size_t index,bool phrase) {
  return beginPlosiveAudition(index,phrase?PlosiveAuditionMode::StopVowel:PlosiveAuditionMode::Source);
}
core::Result<void> VoiceDesignerSession::beginPlosiveAudition(std::size_t index,PlosiveAuditionMode mode) {
  if (mode!=PlosiveAuditionMode::Source && mode!=PlosiveAuditionMode::StopVowel && mode!=PlosiveAuditionMode::VowelStop)
    return core::failure(core::ErrorCode::InvalidArgument,"Unknown plosive audition mode");
  return beginNoiseAudition(index,true,mode);
}
core::Result<void> VoiceDesignerSession::beginArticulationAudition(std::string phone) {
  if (busy() || auditionBusy() || !model_ || phone.empty() || phone.size()>128U)
    return core::failure(core::ErrorCode::Conflict,"Finish Designer work and select a supported articulation first");
  auditionStop_=std::stop_source{}; const auto stop=auditionStop_.get_token();
  const auto epoch=epoch_; const auto revision=model_->revision(); const auto resource=model_->resource();
  const auto pose=auditionPose_; const auto pitch=auditionPitch_;
  auditionAudio_.reset(); fricationAudio_.reset(); fricationAudioIndex_.reset();
  plosiveAudio_.reset(); plosiveAudioIndex_.reset(); articulationAudio_.reset(); articulationAudioPhone_.reset();
  try {
    auditionWork_=std::async(std::launch::async,[resource,epoch,revision,pose,pitch,phone=std::move(phone),stop]() mutable -> core::Result<AuditionResult> {
      auto audio=renderDesignerArticulationPhraseAudition(resource,phone,pose,pitch,stop);
      if (!audio) return core::Result<AuditionResult>{audio.error()};
      AuditionResult result{epoch,revision,pose,pitch,std::move(audio.value())};
      result.articulationPhone=std::move(phone);
      return result;
    });
  } catch (const std::exception& error) { return core::failure(core::ErrorCode::Internal,"Cannot start articulation audition",error.what()); }
  return core::success();
}
core::Result<void> VoiceDesignerSession::beginNoiseAudition(std::size_t index,bool plosive,PlosiveAuditionMode mode) {
  if (busy() || auditionBusy() || !model_ || index >= (plosive?model_->recipe().plosives.size():model_->recipe().frications.size()))
    return core::failure(core::ErrorCode::Conflict,"Finish Designer work and select a valid noise source");
  auditionStop_ = std::stop_source{}; const auto stop = auditionStop_.get_token();
  const auto epoch = epoch_; const auto revision = model_->revision(); const auto resource = model_->resource();
  const auto pose = auditionPose_; const auto pitch = auditionPitch_;
  fricationAudio_.reset(); fricationAudioIndex_.reset();
  plosiveAudio_.reset(); plosiveAudioIndex_.reset();
  articulationAudio_.reset(); articulationAudioPhone_.reset();
  try {
    auditionWork_ = std::async(std::launch::async,[resource,epoch,revision,pose,pitch,index,stop,plosive,mode]() -> core::Result<AuditionResult> {
      auto audio = plosive?(mode!=PlosiveAuditionMode::Source?renderDesignerPlosivePhraseAudition(resource,index,pose,pitch,stop,mode):renderDesignerPlosiveAudition(resource,index,stop)):
          mode==PlosiveAuditionMode::Source?renderDesignerFricationAudition(resource,index,stop):
          renderDesignerFricationPhraseAudition(resource,index,pose,pitch,stop,mode==PlosiveAuditionMode::StopVowel?FricationAuditionMode::FricationVowel:FricationAuditionMode::VowelFrication);
      if (!audio) return core::Result<AuditionResult>{audio.error()};
      return AuditionResult{epoch,revision,pose,pitch,std::move(audio.value()),plosive?std::nullopt:std::optional{index},plosive?std::optional{index}:std::nullopt,mode};
    });
  } catch (const std::exception& error) { return core::failure(core::ErrorCode::Internal,"Cannot start noise-source audition",error.what()); }
  return core::success();
}
core::Result<void> VoiceDesignerSession::beginAudition() {
  if (busy() || auditionBusy() || !model_)
    return core::failure(core::ErrorCode::Conflict, "Finish Designer work and select a voice before audition");
  auditionStop_ = std::stop_source{}; const auto stop = auditionStop_.get_token();
  const auto epoch = epoch_; const auto revision = model_->revision(); const auto resource = model_->resource();
  const auto poseIndex = auditionPose_; const auto midiKey = auditionPitch_;
  auditionAudio_.reset();
  try {
    auditionWork_ = std::async(std::launch::async, [resource, epoch, revision, poseIndex, midiKey, stop]() -> core::Result<AuditionResult> {
      auto rendered = renderDesignerAudition(resource, poseIndex, midiKey, stop);
      if (!rendered) return core::Result<AuditionResult>{rendered.error()};
      return AuditionResult{epoch, revision, poseIndex, midiKey, std::move(rendered.value())};
    });
  } catch (const std::exception& error) { return core::failure(core::ErrorCode::Internal, "Cannot start Designer audition", error.what()); }
  return core::success();
}
core::Result<void> VoiceDesignerSession::setSeed(std::uint64_t epoch, std::uint64_t revision, std::string_view decimalSeed) {
  return setSeedAt(epoch, revision, decimalSeed, std::nullopt);
}
core::Result<void> VoiceDesignerSession::setFricationSeed(std::uint64_t epoch, std::uint64_t revision, std::size_t index, std::string_view decimalSeed) {
  return setSeedAt(epoch, revision, decimalSeed, index);
}
core::Result<void> VoiceDesignerSession::setSeedAt(std::uint64_t epoch, std::uint64_t revision,
    std::string_view decimalSeed, std::optional<std::size_t> frication, std::optional<std::size_t> plosive) {
  if (busy() || !model_ || epoch != epoch_ || revision != model_->revision())
    return core::failure(core::ErrorCode::Conflict, "Designer seed edit is stale or busy");
  if (frication && *frication >= model_->recipe().frications.size())
    return core::failure(core::ErrorCode::InvalidArgument, "Frication seed target is missing");
  if (plosive && (frication || *plosive >= model_->recipe().plosives.size()))
    return core::failure(core::ErrorCode::InvalidArgument, "Plosive seed target is missing or ambiguous");
  if (decimalSeed.empty() || decimalSeed.size() > 20U)
    return core::failure(core::ErrorCode::InvalidArgument, "Seed must be a canonical unsigned 64-bit decimal integer");
  std::uint64_t seed = 0U;
  const auto parsed = std::from_chars(decimalSeed.data(), decimalSeed.data()+decimalSeed.size(), seed);
  if (parsed.ec != std::errc{} || parsed.ptr != decimalSeed.data()+decimalSeed.size() || std::to_string(seed) != decimalSeed)
    return core::failure(core::ErrorCode::InvalidArgument, "Seed must be a canonical unsigned 64-bit decimal integer");
  auto desired = model_->recipe();
  if (frication) desired.frications[*frication].source.seed = seed;
  else if (plosive) desired.plosives[*plosive].source.seed = seed;
  else desired.seed = seed;
  return edit(epoch, revision, std::move(desired));
}
core::Result<void> VoiceDesignerSession::duplicatePose(std::uint64_t epoch, std::uint64_t revision,
    std::string phone, std::string style, std::optional<std::size_t> expectedSourcePose) {
  if (busy() || !model_ || epoch != epoch_ || revision != model_->revision())
    return core::failure(core::ErrorCode::Conflict, "Pose duplication is stale or busy");
  if (expectedSourcePose && *expectedSourcePose != auditionPose_)
    return core::failure(core::ErrorCode::Conflict, "Selected source pose changed while naming its duplicate");
  auto desired = model_->recipe(); auto pose = desired.poses[auditionPose_];
  pose.phone = std::move(phone); pose.style = std::move(style);
  desired.poses.push_back(std::move(pose));
  const auto index = desired.poses.size() - 1U;
  const auto changed = edit(epoch, revision, std::move(desired)); if (!changed) return changed;
  auditionPose_ = index;
  return core::success();
}
core::Result<void> VoiceDesignerSession::addFrication(std::uint64_t epoch, std::uint64_t revision, std::string phone, std::string style) {
  if (busy() || !model_ || epoch != epoch_ || revision != model_->revision())
    return core::failure(core::ErrorCode::Conflict, "Frication creation is stale or busy");
  auto desired = model_->recipe();
  desired.frications.push_back({std::move(phone), style, {.seed = desired.seed}});
  const auto changed = edit(epoch, revision, std::move(desired)); if (!changed) return changed;
  if (model_->recipe().poses[auditionPose_].style != style)
    for (std::size_t index = 0U; index < model_->recipe().poses.size(); ++index)
      if (model_->recipe().poses[index].style == style) { auditionPose_ = index; break; }
  return core::success();
}
core::Result<void> VoiceDesignerSession::removeFrication(std::uint64_t epoch, std::uint64_t revision, std::size_t index) {
  if (busy() || !model_ || epoch != epoch_ || revision != model_->revision())
    return core::failure(core::ErrorCode::Conflict, "Frication removal is stale or busy");
  if (index >= model_->recipe().frications.size()) return core::failure(core::ErrorCode::InvalidArgument, "Frication target is missing");
  auto desired = model_->recipe();
  desired.frications.erase(desired.frications.begin()+static_cast<std::ptrdiff_t>(index));
  return edit(epoch, revision, std::move(desired));
}
core::Result<void> VoiceDesignerSession::setPlosiveSeed(std::uint64_t epoch, std::uint64_t revision, std::size_t index, std::string_view seed) {
  return setSeedAt(epoch, revision, seed, {}, index);
}
core::Result<void> VoiceDesignerSession::addPlosive(std::uint64_t epoch, std::uint64_t revision, std::string phone, std::string style) {
  if (busy() || !model_ || epoch != epoch_ || revision != model_->revision())
    return core::failure(core::ErrorCode::Conflict, "Plosive creation is stale or busy");
  auto desired = model_->recipe();
  desired.plosives.push_back({std::move(phone), style, {.seed=desired.seed}, 10.0});
  const auto changed = edit(epoch, revision, std::move(desired)); if (!changed) return changed;
  if (model_->recipe().poses[auditionPose_].style != style)
    for (std::size_t index=0U; index<model_->recipe().poses.size(); ++index)
      if (model_->recipe().poses[index].style==style) { auditionPose_=index; break; }
  return core::success();
}
core::Result<void> VoiceDesignerSession::removePlosive(std::uint64_t epoch, std::uint64_t revision, std::size_t index) {
  if (busy() || !model_ || epoch != epoch_ || revision != model_->revision())
    return core::failure(core::ErrorCode::Conflict, "Plosive removal is stale or busy");
  if (index>=model_->recipe().plosives.size()) return core::failure(core::ErrorCode::InvalidArgument, "Plosive target is missing");
  auto desired=model_->recipe(); desired.plosives.erase(desired.plosives.begin()+static_cast<std::ptrdiff_t>(index));
  return edit(epoch, revision, std::move(desired));
}
core::Result<void> VoiceDesignerSession::removePose(std::uint64_t epoch, std::uint64_t revision) {
  if (busy() || !model_ || epoch != epoch_ || revision != model_->revision())
    return core::failure(core::ErrorCode::Conflict, "Pose removal is stale or busy");
  auto desired = model_->recipe();
  if (desired.poses.size() <= 1U) return core::failure(core::ErrorCode::InvalidArgument, "A voice must retain at least one pose");
  const auto& selected=desired.poses[auditionPose_];
  if (std::any_of(desired.frications.begin(),desired.frications.end(),[&](const auto& source){
      return source.voicingGain && source.phone==selected.phone && source.style==selected.style;
    })) return core::failure(core::ErrorCode::Conflict,
        "This resonance pose is used by voiced frication; disable its voicing or remove the source first");
  desired.poses.erase(desired.poses.begin() + static_cast<std::ptrdiff_t>(auditionPose_));
  return edit(epoch, revision, std::move(desired));
}
core::Result<void> VoiceDesignerSession::beginGesture(std::uint64_t epoch, std::uint64_t revision) {
  if (busy() || !model_ || epoch != epoch_) return core::failure(core::ErrorCode::Conflict, "Designer gesture is stale or busy");
  const auto result = model_->beginGesture(revision); if (result) invalidateAudition(); return result;
}
core::Result<void> VoiceDesignerSession::updateGesture(std::uint64_t epoch, std::uint64_t revision, voice_design::VoiceRecipe recipe) {
  if (busy() || !model_ || epoch != epoch_) return core::failure(core::ErrorCode::Conflict, "Designer gesture is stale or busy");
  const auto result = model_->updateGesture(revision, std::move(recipe));
  if (result) { invalidateAudition(); if (auditionPose_ >= model_->recipe().poses.size()) auditionPose_ = 0U; }
  return result;
}
core::Result<void> VoiceDesignerSession::endGesture(std::uint64_t epoch, std::uint64_t revision, bool commit) {
  if (busy() || !model_ || epoch != epoch_) return core::failure(core::ErrorCode::Conflict, "Designer gesture is stale or busy");
  const auto result = commit ? model_->commitGesture(revision) : model_->cancelGesture(revision);
  if (result) { invalidateAudition(); if (auditionPose_ >= model_->recipe().poses.size()) auditionPose_ = 0U; }
  return result;
}
core::Result<void> VoiceDesignerSession::edit(std::uint64_t epoch, std::uint64_t revision, voice_design::VoiceRecipe recipe) {
  if (busy() || !model_ || epoch != epoch_) return core::failure(core::ErrorCode::Conflict, "Designer is busy, replaced or has no voice");
  const auto result = model_->edit(revision, std::move(recipe));
  if (result && model_->revision() != revision) {
    invalidateAudition(); if (auditionPose_ >= model_->recipe().poses.size()) auditionPose_ = 0U;
  }
  return result;
}
core::Result<void> VoiceDesignerSession::undo(std::uint64_t epoch, std::uint64_t revision) {
  if (busy() || !model_ || epoch != epoch_) return core::failure(core::ErrorCode::Conflict, "Designer is busy, replaced or has no voice");
  const auto result = model_->undo(revision);
  if (result) { invalidateAudition(); if (auditionPose_ >= model_->recipe().poses.size()) auditionPose_ = 0U; }
  return result;
}
core::Result<void> VoiceDesignerSession::redo(std::uint64_t epoch, std::uint64_t revision) {
  if (busy() || !model_ || epoch != epoch_) return core::failure(core::ErrorCode::Conflict, "Designer is busy, replaced or has no voice");
  const auto result = model_->redo(revision);
  if (result) { invalidateAudition(); if (auditionPose_ >= model_->recipe().poses.size()) auditionPose_ = 0U; }
  return result;
}
}
