#include "options.hpp"

#include "seam/native_ui/native_window.hpp"
#include "seam/native_ui/voicebank_studio.hpp"
#include "seam/native_ui/voice_designer_session.hpp"
#include "seam/native_ui/voice_designer_layout.hpp"
#include "seam/native_ui/voice_designer_source_selection.hpp"
#include "seam/platform/audio_input_device.hpp"
#include "seam/platform/recording_session.hpp"
#include "seam/platform/file_dialog.hpp"
#include "seam/platform/audio_device.hpp"
#include "seam/native_ui/candidate_audition.hpp"
#include "seam/native_ui/candidate_audition_session.hpp"
#include "seam/authoring/generation_job.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include <cmath>
#include <algorithm>
#include <charconv>

#include <memory>
#include <clocale>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

using seam::voicebank_studio_native::Options;
using seam::voicebank_studio_native::parseOptions;
using seam::voicebank_studio_native::printUsage;

class VoicebankStudioApp final : public seam::native_ui::INativeWindowClient {
public:
  explicit VoicebankStudioApp(bool forceSyntheticInput)
      : forceSyntheticInput_(forceSyntheticInput), recording_(48000U, 300U) {}

  ~VoicebankStudioApp() override {
    stopAudition();
    static_cast<void>(stopRecording());
  }

  seam::core::Result<void> open(const Options& options) {
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
    // Designing a patch needs output audition, not microphone access or a bank.
    if (options.startDesigner && options.manifest.empty() && !options.productionProject)
      return seam::core::success();
    return initializeInput();
  }

  seam::core::Result<seam::voicebank_production::ExportedU57Inputs>
  exportProductionInputs(const std::filesystem::path& destination) {
    return controller_.exportProductionInputs(destination);
  }

  seam::core::Result<void> selectProductionUnit(std::size_t index) {
    return controller_.selectUnit(index);
  }

  seam::core::Result<seam::voicebank_production::CommittedDerivedRevision>
  applyProductionOperation(const seam::voicebank_production::OperationRequest& request) {
    return controller_.applySelectedProductionOperation(request);
  }

  seam::core::Result<void> importProductionTake(
      const std::filesystem::path& path) {
    auto inspected = controller_.inspectSelectedProductionTake(path);
    if (!inspected) return inspected;
    return controller_.importSelectedTake(path);
  }

  void setWindow(seam::native_ui::INativeWindow& window) noexcept { window_ = &window; }
  void finishPendingImport() { stopAudition(); record(controller_.finishProceduralCandidateImport()); record(designer_.finish()); }

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
    auto dialog = seam::platform::createNativeFileDialog();
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
    auto dialog = seam::platform::createNativeFileDialog();
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
    return 13U + recipe.poses[pose].formants.size()*3U + designerFrications(recipe, pose).size()*4U + designerPlosives(recipe,pose).size()*4U;
  }
  static std::vector<std::size_t> designerPlosives(const seam::voice_design::VoiceRecipe& recipe,std::size_t pose) {
    std::vector<std::size_t> indices;
    for (std::size_t i=0U;i<recipe.plosives.size();++i) if (recipe.plosives[i].style==recipe.poses[pose].style) indices.push_back(i);
    return indices;
  }
  static std::size_t designerPlosiveStart(const seam::voice_design::VoiceRecipe& recipe,std::size_t pose) {
    return 8U+recipe.poses[pose].formants.size()*3U+designerFrications(recipe,pose).size()*4U;
  }
  std::optional<std::size_t> selectedDesignerPlosive() const {
    if (!designer_.model()) return {};
    const auto& recipe=designer_.model()->recipe();
    const auto start=designerPlosiveStart(recipe,designer_.auditionPose());
    const auto indices=designerPlosives(recipe,designer_.auditionPose());
    if (designerControl_<start || (designerControl_-start)/4U>=indices.size()) return {};
    return indices[(designerControl_-start)/4U];
  }
  seam::core::Result<void> editDesignerPlosiveSeed(std::size_t index) {
    const auto* model=designer_.model();
    if (!model || designer_.busy() || index>=model->recipe().plosives.size())
      return seam::core::failure(seam::core::ErrorCode::Conflict,"Select an available plosive source first");
    const auto epoch=designer_.epoch(),revision=model->revision();
    auto dialog=seam::platform::createNativeFileDialog();
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
    if (designerControl_ < oralEnd || (designerControl_-oralEnd)/4U >= indices.size()) return std::nullopt;
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
    auto dialog = seam::platform::createNativeFileDialog();
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
    const auto nasalStart=designerControlCount(desired,pose)-5U;
    if (control>=nasalStart && steps!=0.0) {
      const auto index=control-nasalStart;
      auto value=nasalControlValue(desired.poses[pose],index)+steps*(index==0U?0.01:(index==1U || index==3U)?10.0:5.0);
      if (index==0U) value=std::clamp(value,0.0,1.0);
      setNasalControl(desired.poses[pose],index,value);
    } else if (control >= nasalStart) {
      return desired;
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
    auto dialog = seam::platform::createNativeFileDialog();
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

  void designerKey(const seam::native_ui::KeyEvent& event) {
    designerSemanticFocus_.clear();
    using Key = seam::native_ui::NativeKey;
    if (event.key == Key::Escape && designer_.auditionBusy()) { designer_.cancelAudition(); return; }
    if (designer_.busy()) { if (event.key == Key::Escape) designer_.cancel(); return; }
    if (event.key == Key::P && event.modifiers.primaryShortcut() && event.modifiers.shift) {
      record(prepareDesignerFromDialog()); return;
    }
    if (event.key == Key::B && event.modifiers.primaryShortcut()) {
      if (event.modifiers.shift) designer_.clearAuditionReference();
      else if (designer_.model()) record(designer_.pinAuditionReference(designer_.epoch(), designer_.model()->revision()));
      return;
    }
    if (event.key == Key::Space) {
      if (event.modifiers.primaryShortcut()) {
        if (const auto plosive=selectedDesignerPlosive()) {
          using Mode=seam::native_ui::PlosiveAuditionMode;
          const auto mode=event.modifiers.alt?Mode::VowelStop:event.modifiers.shift?Mode::StopVowel:Mode::Source;
          if (!designer_.plosiveAudio() || designer_.plosiveAudioIndex()!=plosive || designer_.plosiveAudioMode()!=mode) { record(designer_.beginPlosiveAudition(*plosive,mode)); return; }
          const auto& audio=designer_.plosiveAudio();
          record(audition_.start(seam::platform::createSystemAudioDevice(),audio,0U,audio->frameCount(),0.25F));
          if (audition_.active()) auditionStatus_=mode==Mode::VowelStop?"SELECTED VOWEL + STOP / NOT APPROVED":mode==Mode::StopVowel?"STOP + SELECTED VOWEL / NOT APPROVED":"PLOSIVE SOURCE / 50 ms CLOSURE / NOT APPROVED";
          return;
        }
        const auto index = selectedDesignerFrication();
        if (!index) { record(seam::core::failure(seam::core::ErrorCode::InvalidState,"Select a frication control before noise audition")); return; }
        using Mode=seam::native_ui::FricationAuditionMode;
        const auto mode=event.modifiers.alt?Mode::VowelFrication:event.modifiers.shift?Mode::FricationVowel:Mode::Source;
        if (!designer_.fricationAudio() || designer_.fricationAudioIndex() != index || designer_.fricationAudioMode()!=mode) { record(designer_.beginFricationAudition(*index,mode)); return; }
        const auto& audio = designer_.fricationAudio();
        record(audition_.start(seam::platform::createSystemAudioDevice(),audio,0U,audio->frameCount(),0.25F));
        if (audition_.active()) auditionStatus_ = fricationPreviewDescription()+" / NOT APPROVED";
        return;
      }
      if (event.modifiers.shift) {
        if (!designer_.referenceMatchesSelection()) {
          record(seam::core::failure(seam::core::ErrorCode::Conflict, "Pin a reference and match its pose/style/pitch before A/B playback")); return;
        }
        const auto& audio = designer_.auditionReference()->audio;
        record(audition_.start(seam::platform::createSystemAudioDevice(), audio, 0U, audio->frameCount(), 0.25F));
        if (audition_.active()) auditionStatus_ = "REFERENCE A / NOT APPROVED";
        return;
      }
      if (!designer_.auditionAudio()) { record(designer_.beginAudition()); return; }
      const auto& audio = designer_.auditionAudio();
      record(audition_.start(seam::platform::createSystemAudioDevice(), audio, 0U, audio->frameCount(), 0.25F));
      if (audition_.active()) auditionStatus_ = "CURRENT B / NOT APPROVED";
      return;
    }
    if (event.key == Key::D && event.modifiers.primaryShortcut()) { record(openDesignerProducerWorkspace()); return; }
    if (event.key == Key::N && event.modifiers.primaryShortcut()) {
      const auto discard = allowDesignerReplacement();
      if (!discard) { record(seam::core::Result<void>{discard.error()}); return; }
      if (!discard.value()) return;
      seam::voice_design::VoiceRecipe recipe; recipe.id = "voice-draft";
      recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
      record(designer_.create(std::move(recipe), true)); return;
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
        auto dialog=seam::platform::createNativeFileDialog();
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
      auto dialog = seam::platform::createNativeFileDialog();
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
      auto dialog = seam::platform::createNativeFileDialog();
      const auto seed = dialog->chooseDesignerSeed(std::to_string(model->recipe().seed));
      if (!seed) { record(seam::core::Result<void>{seed.error()}); return; }
      if (seed.value()) record(designer_.setSeed(epoch, revision, *seed.value()));
      return;
    }
    if (event.key == Key::L && event.modifiers.primaryShortcut()) {
      const auto epoch = designer_.epoch(); const auto revision = model->revision();
      const auto sourcePose = designer_.auditionPose();
      if (event.modifiers.shift) { record(designer_.removePose(epoch, revision)); return; }
      auto dialog = seam::platform::createNativeFileDialog();
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
    const auto nasalStart=designerControlCount(recipe,designer_.auditionPose())-5U;
    if (control>=nasalStart) return nasalControlValue(recipe.poses[designer_.auditionPose()],control-nasalStart);
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
  seam::core::Result<void> openDesignerProducerWorkspace() {
    if (designer_.busy() || designerDrag_ || controller_.proceduralImportBusy() || recording_.armed() || recording_.recordedFrames()>0U)
      return seam::core::failure(seam::core::ErrorCode::Conflict,"Finish active Designer or producer work before opening a workspace");
    if (controller_.productionProject() || !controller_.manifest().units.empty()) { designerView_=false; return seam::core::success(); }
    const auto epoch=designer_.epoch(), revision=designer_.model()?designer_.model()->revision():0U;
    const auto producerEpoch=controller_.productionSessionEpoch();
    auto dialog=seam::platform::createNativeFileDialog();
    const auto input=dialog->chooseProductionWorkspace();
    if (!input) return seam::core::Result<void>{input.error()};
    if (!input.value()) return seam::core::success();
    const auto valid=input.value()->validate(); if (!valid) return valid;
    if (epoch!=designer_.epoch() || revision!=(designer_.model()?designer_.model()->revision():0U) ||
        producerEpoch!=controller_.productionSessionEpoch() || designer_.busy() || controller_.proceduralImportBusy())
      return seam::core::failure(seam::core::ErrorCode::Conflict,"Workspace opening context changed while the dialog was open");
    const auto opened=controller_.beginOpenProductionProject(input.value()->root,input.value()->inventorySha256,input.value()->operatorId);
    if (opened) designerView_=false;
    return opened;
  }
  static seam::ui::Rect designerEntryBounds(std::size_t index,double width) {
    return {24.0,174.0+static_cast<double>(index)*72.0,std::max(0.0,std::min(width-48.0,440.0)),56.0};
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
          .actions = available ? std::vector<SemanticAction>{SemanticAction::Activate, SemanticAction::SetFocus} : std::vector<SemanticAction>{}});
    }
    if (designer_.model()) {
      const auto seed = std::to_string(designer_.model()->recipe().seed);
      root.children.push_back({.id = prefix+"seed", .role = SemanticRole::TextField, .name = "Reproducible voice seed",
          .value = seed, .bounds = {width*0.55,24.0,width*0.45-24.0,24.0}, .enabled = enabled,
          .actions = {SemanticAction::SetFocus,SemanticAction::EditText}, .editableValue = seed,
          .description = "Exact unsigned 64-bit decimal integer, 0 to 18446744073709551615"});
      const auto addAction = [&](const char* id, const char* name, bool available, double x, double y, double actionWidth) {
        root.children.push_back({.id = prefix + id, .role = SemanticRole::Button, .name = name,
            .bounds = y < 0.0 ? seam::ui::Rect{} : seam::ui::Rect{x,y,actionWidth,20.0}, .enabled = available,
            .actions = available ? std::vector<SemanticAction>{SemanticAction::Activate,SemanticAction::SetFocus} : std::vector<SemanticAction>{}});
      };
      const auto smallWidth = std::max(1.0, (width-48.0)/5.0);
      addAction("save-as", "Save voice as new file", enabled, 24.0,82.0,smallWidth);
      addAction("duplicate-pose", "Duplicate selected pose", enabled, 24.0+smallWidth,82.0,smallWidth);
      addAction("remove-pose", "Remove selected pose", enabled && designer_.model()->recipe().poses.size()>1U, 24.0+smallWidth*2.0,82.0,smallWidth);
      addAction("undo", "Undo Designer edit", enabled && designer_.model()->canUndo(), 24.0+smallWidth*3.0,82.0,smallWidth);
      addAction("redo", "Redo Designer edit", enabled && designer_.model()->canRedo(), 24.0+smallWidth*4.0,82.0,smallWidth);
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
      root.children.push_back({.id = prefix+"audition-state", .role = SemanticRole::Status, .name = "Audition state",
          .value = designer_.auditionBusy() ? "Rendering" : (!auditionStatus_.empty() ? auditionStatus_ :
              (designer_.plosiveAudio() && designer_.plosiveAudioIndex()==selectedDesignerPlosive()?(designer_.plosiveAudioIsCoda()?"Vowel and stop ready":designer_.plosiveAudioIsPhrase()?"Stop and vowel ready":"Plosive source ready"):
              designer_.fricationAudio() && designer_.fricationAudioIndex()==selectedDesignerFrication()?fricationPreviewDescription()+" ready":designer_.auditionAudio() ? "Vowel ready" : "Not rendered")),
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
            .description = index == 3U ? "Zero-based recipe pose index" : (index == 4U ? "Audition MIDI pitch, 36 to 96" : "Validated draft voice parameter")});
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
      {"save-as","Save as"},{"duplicate-pose","Duplicate"},{"remove-pose","Remove pose"},{"undo","Undo"},{"redo","Redo"},
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
      line(58.0, "CMD/CTRL-N NEW / O OPEN / S SAVE / SHIFT-S SAVE AS / D BACK");
      line(82.0, "CMD/CTRL-L DUPLICATE POSE / CMD/CTRL-SHIFT-L REMOVE (UNDOABLE)");
    }
    const auto* model = designer_.model();
    if (!model) {
      rebuildDesignerAccessibility({}, 0U, canvas.logicalWidth(), canvas.logicalHeight());
      line(112.0, "Create a draft or open a saved recipe to begin.");
      const std::array<const char*,3U> labels{"NEW VOICE DRAFT","OPEN SAVED RECIPE","OPEN PRODUCER WORKSPACE"};
      for (std::size_t index=0U;index<labels.size();++index) {
        const auto bounds=designerEntryBounds(index,canvas.logicalWidth());
        canvas.fillRect(bounds,Color{58,39,59,255});
        canvas.drawText({bounds.x+16.0,bounds.y+16.0,bounds.width-32.0,24.0},labels[index],Color{239,233,241,255},16.0);
      }
      if (designer_.busy()) line(146.0, "OPENING VOICE / ESC CANCEL");
      if (!lastError_.empty()) line(406.0, lastError_, Color{193,115,160,255});
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
    else if (designer_.plosiveAudio() && designer_.plosiveAudioIndex()==selectedDesignerPlosive()) line(layout.statusY,designer_.plosiveAudioIsCoda()?"VOWEL + STOP READY / CMD-ALT-SPACE PLAY":designer_.plosiveAudioIsPhrase()?"STOP + VOWEL READY / CMD-SHIFT-SPACE PLAY":"STOP READY / CMD-SPACE / SHIFT CV / ALT VC");
    else if (designer_.fricationAudio() && designer_.fricationAudioIndex()==selectedDesignerFrication()) line(layout.statusY,fricationPreviewDescription()+" READY");
    else if (designer_.auditionAudio()) line(layout.statusY, "POSE READY / SPACE PLAY");
    if (!lastError_.empty()) line(layout.errorY, lastError_, Color{193,115,160,255});
  }
  void stopAudition() noexcept {
    audition_.stop();
    auditionStatus_.clear();
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
    const auto started = audition_.start(seam::platform::createSystemAudioDevice(), waveform->audio, begin, end, gain);
    if (!started) { stopAudition(); return started; }
    auditionStatus_ = selectedGesture ? "AUDITION: RAW GESTURE" : "AUDITION: RAW TAKE";
    return seam::core::success();
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
    auto dialog = seam::platform::createNativeFileDialog();
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
    auto dialog = seam::platform::createNativeFileDialog();
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
          .title = selection->selectedRecipe ? "Prepare Frozen Designer Snapshot (No Audio Generated)" : "Prepare New Job Folder (No Audio Generated)", .initialDirectory = selection->path.parent_path(),
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
    auto dialog = seam::platform::createNativeFileDialog();
    const auto path = dialog->choose({.purpose = batch ? seam::platform::FileDialogPurpose::OpenGenerationBatch
                                                    : seam::platform::FileDialogPurpose::OpenGenerationJob,
        .title = batch ? "Generate and Collect an Unapproved Batch" : "Generate and Collect an Unapproved Candidate",
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
    auto dialog = seam::platform::createNativeFileDialog();
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
    auto dialog = seam::platform::createNativeFileDialog();
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
      const auto started = audition_.start(seam::platform::createSystemAudioDevice(), inspection->audio,
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
    auto dialog = seam::platform::createNativeFileDialog();
    if (action == "open-manifest") return openStudioSampleManifest(controller_, *dialog);
    if (action == "source-evidence") { sampleReviewFirstLine_ = 0U; return captureStudioSourceQuality(controller_, *dialog); }
    if (action == "source-decision") { sampleReviewFirstLine_ = 0U; return confirmStudioSourceQuality(controller_, *dialog); }
    if (action == "source-license") { sampleReviewFirstLine_ = 0U; return captureStudioSourceLicense(controller_, *dialog); }
    if (action == "source-register") { sampleReviewFirstLine_ = 0U; return registerStudioSource(controller_, *dialog); }
    if (action == "create-draft") { sampleReviewFirstLine_ = 0U; return createStudioSampleManifestDraft(controller_, *dialog); }
    if (action == "reviewer") return chooseStudioSampleReviewer(controller_, *dialog);
    if (action == "accept" || action == "reject") return confirmStudioSampleReview(controller_, *dialog,
        action == "accept" ? Decision::Accept : Decision::Reject);
    if (action == "publish") { sampleReviewFirstLine_ = 0U; return publishStudioSampleCandidate(controller_, *dialog); }
    return seam::core::failure(seam::core::ErrorCode::InvalidArgument, "Unknown sample review action");
  }

  void paintSampleReview(seam::native_ui::RasterCanvas& canvas) {
    using namespace seam::native_ui;
    const auto width = canvas.logicalWidth(), height = canvas.logicalHeight();
    const auto lines = studioSampleReviewDetailLines(controller_, width);
    const auto count = studioSampleReviewVisibleLines(height);
    sampleReviewFirstLine_ = std::min(sampleReviewFirstLine_, lines.size() > count ? lines.size() - count : 0U);
    paintStudioSampleReview(canvas, controller_, sampleReviewFirstLine_, !lastError_.empty() ? lastError_ : auditionStatus_);
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
          .name = "Captured review data", .value = lines[sampleReviewFirstLine_ + i], .bounds = {24.0,274.0+18.0*static_cast<double>(i),width-48.0,16.0}});
    root.children.push_back({.id = prefix + "status", .role = SemanticRole::Status, .name = "Review operation status",
        .value = !lastError_.empty() ? lastError_ : controller_.sampleReviewStatus(), .bounds = {24.0,height-28.0,width-48.0,18.0}});
    sampleReviewAccessibility_.rebuildCustom(std::move(root), sampleReviewSemanticFocus_);
  }

  std::string generationSemanticPrefix() const {
    const auto* project=controller_.productionProject();
    return "studio-generation."+std::to_string(controller_.productionSessionEpoch())+"."+
        std::to_string(project?project->lastDurableGeneration:0U)+"."+std::to_string(controller_.selectedIndex())+".";
  }
  seam::core::Result<void> generationControlAction(std::string_view id) {
    const auto controls=seam::native_ui::studioGenerationControls(controller_,controller_.logicalWidth(),recording_.armed() || recording_.recordedFrames()>0U);
    const auto found=std::find_if(controls.begin(),controls.end(),[&](const auto& control){return control.id==id;});
    if (generationModal_ || designerView_ || sampleReviewView_ || found==controls.end() || !found->enabled)
      return seam::core::failure(seam::core::ErrorCode::Conflict,"Generation action is unavailable or busy");
    if (id=="cancel") {
      controller_.cancelProceduralCandidateImport();
      return seam::core::success();
    }
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
    SemanticNode root{.id=prefix+"root",.role=SemanticRole::Panel,.name="Producer generation actions",
        .bounds={0.0,0.0,width,height}};
    for (const auto& control:studioGenerationControls(controller_,width,recording_.armed() || recording_.recordedFrames()>0U)) {
      const bool enabled=control.enabled && !generationModal_;
      root.children.push_back({.id=prefix+control.id,.role=SemanticRole::Button,.name=control.label,
          .bounds=control.bounds,.enabled=enabled,
          .actions=enabled?std::vector<SemanticAction>{SemanticAction::Activate,SemanticAction::SetFocus}:std::vector<SemanticAction>{}});
    }
    root.children.push_back({.id=prefix+"status",.role=SemanticRole::Status,.name="Producer operation status",
        .value=lastError_.empty()?controller_.status():lastError_,.bounds={width-360.0,24.0,340.0,44.0}});
    generationAccessibility_.rebuildCustom(std::move(root),generationSemanticFocus_);
  }

  void paint(seam::native_ui::RasterCanvas& canvas) noexcept override {
    const auto auditionState = audition_.poll();
    if (!auditionState) { auditionStatus_.clear(); lastError_ = auditionState.error().message; }
    else if (!auditionState.value()) auditionStatus_.clear();
    const bool workspaceWasOpening=controller_.workspaceOpening();
    const auto producerPoll=controller_.pollProceduralCandidateImport();
    record(producerPoll);
    if (workspaceWasOpening && !controller_.workspaceOpening() && !controller_.productionProject()) {
      designerView_=true;
      if (producerPoll) lastError_=designer_.model()?"Workspace opening cancelled; voice draft retained":"Workspace opening cancelled; no workspace loaded";
    }
    record(designer_.poll());
    if (designerView_) { paintDesigner(canvas); if (designer_.busy() || designer_.auditionBusy() || audition_.active()) repaint(); return; }
    if (sampleReviewView_) { paintSampleReview(canvas); if (controller_.proceduralImportBusy() || audition_.active()) repaint(); return; }
    painter_.paint(canvas, controller_, recording_.armed(), inputBackend_);
    rebuildGenerationAccessibility(canvas.logicalWidth(),canvas.logicalHeight());
    const auto generationControls=seam::native_ui::studioGenerationControls(controller_,canvas.logicalWidth(),recording_.armed() || recording_.recordedFrames()>0U);
    if (!generationControls.empty()) {
      canvas.fillRect({294.0,268.0,canvas.logicalWidth()-574.0,36.0},seam::native_ui::Color{15,14,18,255});
      for (const auto& control:generationControls) {
        canvas.fillRect(control.bounds,control.enabled?seam::native_ui::Color{72,52,76,255}:seam::native_ui::Color{34,31,38,255});
        canvas.drawText({control.bounds.x+4.0,control.bounds.y+2.0,control.bounds.width-8.0,14.0},control.label,
            control.enabled?seam::native_ui::Color{239,233,241,255}:seam::native_ui::Color{125,118,129,255},10.0);
      }
    }
    canvas.drawText({24.0, 52.0, 200.0, 14.0}, "CMD/CTRL-D DESIGNER", seam::native_ui::Color{166,154,170,255}, 7.0);
    canvas.fillRect({230.0,48.0,120.0,20.0}, seam::native_ui::Color{72,52,76,255});
    canvas.drawText({236.0,53.0,108.0,12.0}, "Q SAMPLE REVIEW", seam::native_ui::Color{239,233,241,255}, 7.0);
    canvas.drawText({canvas.logicalWidth() - 360.0, 56.0, 340.0, 12.0},
        !lastError_.empty() ? lastError_ : (auditionStatus_.empty() ? "SPACE PLAY / ALT ARROWS START / ALT-SHIFT END / ALT +/- PAN" : auditionStatus_),
        !lastError_.empty() ? seam::native_ui::Color{169, 79, 119, 255} : seam::native_ui::Color{166, 154, 170, 255}, 6.0);
    if (controller_.proceduralImportBusy() || audition_.active()) repaint();
  }
  void resized(double width, double height, double) noexcept override {
    if (designerDrag_ && designer_.model()) {
      record(designer_.endGesture(designerDrag_->epoch,designer_.model()->revision(),false));
      designerDrag_.reset();
    }
    controller_.resize(width, height);
  }

  void pointerDown(const seam::native_ui::PointerEvent& event) noexcept override {
    if (sampleReviewModal_) return;
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
        for (std::size_t index=0U;index<3U;++index) if (designerEntryBounds(index,controller_.logicalWidth()).contains(event.position)) {
          record(dispatchAccessibility(designerSemanticPrefix()+(index==0U?"new":index==1U?"open":"back"),seam::native_ui::SemanticAction::Activate));
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
    if (sampleReviewModal_) return;
    if (sampleReviewView_) {
      if (deltaY != 0.0) { record(sampleReviewAction(deltaY > 0.0 ? "previous-page" : "next-page")); repaint(); }
      return;
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
      record(recording_.armed() || recording_.recordedFrames() > 0U
                 ? stopRecording()
                 : startRecording());
    } else if (event.key == seam::native_ui::NativeKey::I && event.modifiers.primaryShortcut()) {
      record(event.modifiers.shift ? generationFromDialog() : importProceduralFromDialog());
    } else if (event.key == seam::native_ui::NativeKey::B && event.modifiers.primaryShortcut() && event.modifiers.shift) {
      record(generationFromDialog(true));
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
        controller_.productionProject() && controller_.manifest().units.empty()?&generationAccessibility_:nullptr);
  }
  seam::core::Result<void> dispatchAccessibility(std::string_view id, seam::native_ui::SemanticAction action) noexcept override {
    using namespace seam::native_ui;
    if (!designerView_ && !sampleReviewView_ && controller_.productionProject() && controller_.manifest().units.empty()) {
      const auto prefix=generationSemanticPrefix();
      if (generationModal_ || !id.starts_with(prefix)) return seam::core::failure(seam::core::ErrorCode::Conflict,"Generation accessibility target is stale or modal");
      return generationAccessibility_.dispatch(id,action,[&](std::string_view target,SemanticAction selected)->seam::core::Result<void> {
        if (selected==SemanticAction::SetFocus) { generationSemanticFocus_=target; return generationAccessibility_.setFocus(target); }
        if (selected!=SemanticAction::Activate) return seam::core::failure(seam::core::ErrorCode::Unsupported,"Generation status is read-only");
        const std::string command{target.substr(prefix.size())};
        const auto result=generationControlAction(command); record(result); repaint(); return result;
      });
    }
    if (sampleReviewView_) {
      const auto prefix = sampleSemanticPrefix();
      if (sampleReviewModal_ || !id.starts_with(prefix)) return seam::core::failure(seam::core::ErrorCode::Conflict, "Sample review accessibility target is stale");
      return sampleReviewAccessibility_.dispatch(id, action, [&](std::string_view target, SemanticAction selected) -> seam::core::Result<void> {
        if (selected == SemanticAction::SetFocus) { sampleReviewSemanticFocus_ = target; return sampleReviewAccessibility_.setFocus(target); }
        if (selected != SemanticAction::Activate) return seam::core::failure(seam::core::ErrorCode::Unsupported, "Review data is inspection-only; use the explicit controls");
        const auto result = sampleReviewAction(target.substr(prefix.size())); record(result); repaint(); return result;
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
      if (suffix.starts_with("remove-plosive.") || suffix.starts_with("plosive-seed.") || suffix.starts_with("render-plosive.") || suffix.starts_with("render-plosive-phrase.") || suffix.starts_with("render-plosive-coda.") || suffix.starts_with("play-plosive.")) {
        const auto number=suffix.substr(suffix.find('.')+1U); std::size_t index=0U;
        const auto parsed=std::from_chars(number.data(),number.data()+number.size(),index);
        if (parsed.ec!=std::errc{} || parsed.ptr!=number.data()+number.size() || !designer_.model())
          return seam::core::failure(seam::core::ErrorCode::InvalidArgument,"Plosive action target is invalid");
        stopAudition();
        if (suffix.starts_with("render-plosive.") || suffix.starts_with("render-plosive-phrase.") || suffix.starts_with("render-plosive-coda.")) {
          using Mode=seam::native_ui::PlosiveAuditionMode;
          const auto mode=suffix.starts_with("render-plosive-coda.")?Mode::VowelStop:suffix.starts_with("render-plosive-phrase.")?Mode::StopVowel:Mode::Source;
          const auto result=designer_.beginPlosiveAudition(index,mode); record(result); repaint(); return result;
        }
        if (suffix.starts_with("play-plosive.")) {
          if (!designer_.plosiveAudio() || designer_.plosiveAudioIndex()!=index)
            return seam::core::failure(seam::core::ErrorCode::Conflict,"Plosive preview is no longer ready");
          const auto& audio=designer_.plosiveAudio();
          const auto result=audition_.start(seam::platform::createSystemAudioDevice(),audio,0U,audio->frameCount(),0.25F);
          if (result) auditionStatus_=designer_.plosiveAudioIsCoda()?"SELECTED VOWEL + STOP / NOT APPROVED":designer_.plosiveAudioIsPhrase()?"STOP + SELECTED VOWEL / NOT APPROVED":"PLOSIVE SOURCE / 50 ms CLOSURE / NOT APPROVED";
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
        stopAudition();
        if (suffix.starts_with("render-frication.") || suffix.starts_with("render-frication-phrase.") || suffix.starts_with("render-frication-coda.")) {
          using Mode=seam::native_ui::FricationAuditionMode;
          const auto mode=suffix.starts_with("render-frication-coda.")?Mode::VowelFrication:suffix.starts_with("render-frication-phrase.")?Mode::FricationVowel:Mode::Source;
          const auto result = designer_.beginFricationAudition(index,mode); record(result); repaint(); return result;
        }
        if (suffix.starts_with("play-frication.")) {
          if (!designer_.fricationAudio() || designer_.fricationAudioIndex() != index)
            return seam::core::failure(seam::core::ErrorCode::Conflict,"Frication preview is no longer ready");
          const auto& audio = designer_.fricationAudio();
          const auto result = audition_.start(seam::platform::createSystemAudioDevice(),audio,0U,audio->frameCount(),0.25F);
          if (result) auditionStatus_ = fricationPreviewDescription()+" / NOT APPROVED";
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
        else if (suffix == "back") key = NativeKey::D;
        else if (suffix == "add-frication") key = NativeKey::E;
        else if (suffix == "add-plosive") key = NativeKey::I;
        else if (suffix == "duplicate-pose" || suffix == "remove-pose") { key = NativeKey::L; shift = suffix == "remove-pose"; }
        else if (suffix == "undo" || suffix == "redo") { key = NativeKey::Z; shift = suffix == "redo"; }
        else if (suffix == "pin-reference" || suffix == "clear-reference") { key = NativeKey::B; shift = suffix == "clear-reference"; }
        else if (suffix == "play-current" || suffix == "play-reference") {
          if ((suffix == "play-current" && !designer_.auditionAudio()) || (suffix == "play-reference" && !designer_.referenceMatchesSelection()))
            return seam::core::failure(seam::core::ErrorCode::Conflict, "Requested audition is no longer available");
          key = NativeKey::Space; shift = suffix == "play-reference";
        } else return seam::core::failure(seam::core::ErrorCode::Unsupported, "Unknown Designer action");
        lastError_.clear(); stopAudition();
        designerKey({.key = key, .modifiers = {.shift = shift, .control = key != NativeKey::Space, .command = key != NativeKey::Space}});
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
    double number = 0.0; const auto parsed = std::from_chars(value.data(), value.data()+value.size(), number);
    const auto* model = designer_.model(); const auto count = designerControlCount(model->recipe(), designer_.auditionPose());
    if (parsedIndex.ec != std::errc{} || parsedIndex.ptr != index.data()+index.size() || control >= count ||
        parsed.ec != std::errc{} || parsed.ptr != value.data()+value.size() || !std::isfinite(number))
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
        const auto nasalStart=designerControlCount(desired,designer_.auditionPose())-5U;
        if (control>=nasalStart) setNasalControl(desired.poses[designer_.auditionPose()],control-nasalStart,number);
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
      const auto epoch = designer_.epoch();
      const auto revision = designer_.model() ? designer_.model()->revision() : 0U;
      const auto discard = allowDesignerReplacement();
      if (!discard) { lastError_ = discard.error().message; designerView_ = true; repaint(); return false; }
      if (!discard.value()) return false;
      auto dialog = seam::platform::createNativeFileDialog();
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

  [[nodiscard]] const std::string& lastError() const noexcept { return lastError_; }
  [[nodiscard]] const std::filesystem::path& lastRecording() const noexcept {
    return lastRecording_;
  }
  [[nodiscard]] std::size_t lastRecordedFrames() const noexcept {
    return lastRecordedFrames_;
  }
  [[nodiscard]] seam::platform::AudioInputDeviceInfo inputInfo() const {
    return input_ == nullptr ? seam::platform::AudioInputDeviceInfo{}
                             : input_->info();
  }
  [[nodiscard]] seam::platform::AudioInputDeviceStats inputStats() const noexcept {
    return input_ == nullptr ? seam::platform::AudioInputDeviceStats{}
                             : input_->stats();
  }
  [[nodiscard]] const seam::voicebank_production::VoicebankProductionProject*
  productionProject() const noexcept {
    return controller_.productionProject();
  }
  [[nodiscard]] seam::voicebank_production::ProductionQueueSummary
  productionQueues() const noexcept {
    return controller_.productionQueues();
  }
  [[nodiscard]] std::size_t stagedRecoveryCandidateCount() const noexcept {
    return controller_.stagedRecoveryCandidateCount();
  }

  seam::core::Result<void> startRecording() {
    stopAudition();
    if (controller_.proceduralImportBusy()) return seam::core::failure(seam::core::ErrorCode::Conflict,
        "Finish candidate import before recording");
    if (input_ == nullptr) {
      if (controller_.productionProject()) {
        const auto initialized=initializeInput(); if (!initialized) return initialized;
      }
    }
    if (input_ == nullptr) {
      return seam::core::failure(seam::core::ErrorCode::InvalidState,
                                 "Voicebank Studio input is unavailable");
    }
    const auto armed = recording_.arm();
    if (!armed) return armed;
    lastRecordedFrames_ = 0U;
    lastRecording_.clear();
    const auto started = input_->start();
    if (!started) {
      recording_.stop();
      return started;
    }
    return seam::core::success();
  }

  seam::core::Result<void> stopRecording() {
    if (input_ != nullptr) input_->stop();
    if (!recording_.armed() && recording_.recordedFrames() == 0U) {
      return seam::core::success();
    }
    recording_.stop();
    const auto frames = recording_.recordedFrames();
    if (frames == 0U) return seam::core::success();
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
    const auto destination = seam::native_ui::nextVoicebankRecordingPath(
        directory, name);
    if (!destination) return seam::core::Result<void>{destination.error()};
    lastRecording_ = destination.value();
    const auto saved = recording_.exportWav(
        lastRecording_, seam::voicebank::WavSampleFormat::Pcm24, false);
    if (!saved) return saved;
    const auto expectedRootMidi = controller_.selectedUnit() != nullptr
                                      ? controller_.selectedUnit()->rootMidi
                                      : productionAssignment != nullptr
                                            ? productionAssignment->pitchLayer
                                            : 60;
    const auto inspected = controller_.inspectTake(
        lastRecording_, expectedRootMidi);
    if (!inspected) return inspected;
    const auto persisted = controller_.persistTakeInspection(lastRecording_);
    if (!persisted) return seam::core::Result<void>{persisted.error()};
    if (controller_.productionProject() != nullptr) {
      auto imported = controller_.importSelectedTake(lastRecording_);
      if (!imported) return imported;
    }
    recording_.clear();
    return seam::core::success();
  }

private:
  seam::core::Result<void> initializeInput() {
    seam::platform::AudioInputDeviceConfig config{
        .sampleRate = 48000U,
        .blockFrames = 256U,
        .applicationName = "Project SEAM",
        .streamName = "Voicebank Studio recording",
    };
    if (!forceSyntheticInput_) {
      auto physical = seam::platform::createSystemAudioInputDevice();
      auto opened = physical->open(config, recording_);
      if (opened) input_ = std::move(physical);
      else lastError_ = opened.error().message;
    }
    if (input_ == nullptr) {
      auto fallback = seam::platform::createThreadedSilenceInputDevice();
      auto opened = fallback->open(config, recording_);
      if (!opened) return opened;
      input_ = std::move(fallback);
    }
    inputBackend_ = input_->info().backend;
    return seam::core::success();
  }

  void repaint() noexcept {
    if (window_ != nullptr) window_->requestRepaint();
  }
  void record(const seam::core::Result<void>& result) noexcept {
    if (!result) lastError_ = result.error().message;
  }

  bool forceSyntheticInput_{false};
  seam::native_ui::CandidateAuditionSession audition_;
  std::string auditionStatus_;
  seam::native_ui::VoicebankStudioController controller_;
  seam::native_ui::VoicebankStudioScenePainter painter_;
  seam::platform::RecordingSession recording_;
  std::unique_ptr<seam::platform::IAudioInputDevice> input_;
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
  bool generationModal_{false};
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
  std::size_t lastRecordedFrames_{0U};
};

}  // namespace

int main(int argc, char** argv) {
  static_cast<void>(std::setlocale(LC_ALL, ""));
  const auto options = parseOptions(argc, argv);
  if (!options.has_value()) {
    if (argc > 1 && std::string_view{argv[1]} == "--help") return 0;
    printUsage();
    return 2;
  }

  VoicebankStudioApp app{options->forceSyntheticInput};
  const auto openedBank = app.open(*options);
  if (!openedBank) {
    std::cerr << "Voicebank Studio load failed: " << openedBank.error().message;
    if (!openedBank.error().context.empty()) std::cerr << " (" << openedBank.error().context << ')';
    std::cerr << '\n';
    return 3;
  }
  if (options->productionUnitIndex.has_value()) {
    const auto selected = app.selectProductionUnit(*options->productionUnitIndex);
    if (!selected) {
      std::cerr << "Voicebank production selection failed: "
                << selected.error().message << '\n';
      return 7;
    }
  }
  if (options->importTake.has_value()) {
    const auto imported = app.importProductionTake(*options->importTake);
    if (!imported) {
      std::cerr << "Voicebank production import failed: "
                << imported.error().message << '\n';
      return 7;
    }
    std::cout << "production_imported_take="
              << options->importTake->string() << '\n';
  }
  if (options->operationKind.has_value()) {
    const auto processed = app.applyProductionOperation(
        {.kind = *options->operationKind,
         .channelIndex = options->channelIndex,
         .targetSampleRate = options->targetSampleRate,
         .targetPeak = options->targetPeak,
         .startFrame = options->startFrame,
         .endFrame = options->endFrame});
    if (!processed) {
      std::cerr << "Voicebank production operation failed: "
                << processed.error().message << '\n';
      return 7;
    }
    std::cout << "production_revision=" << processed.value().revisionId << '\n'
              << "production_output_sha256="
              << processed.value().outputSha256 << '\n';
  }
  if (options->exportU57Inputs.has_value()) {
    const auto exported = app.exportProductionInputs(*options->exportU57Inputs);
    if (!exported) {
      std::cerr << "Voicebank production export failed: "
                << exported.error().message << '\n';
      return 6;
    }
    std::cout << "production_brief=" << exported.value().briefPath.string() << '\n'
              << "candidate_template="
              << exported.value().candidateTemplatePath.string() << '\n';
  }
  auto window = seam::native_ui::createNativeWindow();
  app.setWindow(*window);
  const auto opened = window->open(seam::native_ui::NativeWindowConfig{
      .title = "Project SEAM / Voicebank Studio",
      .width = options->windowWidth,
      .height = options->windowHeight,
      .scale = 1.0,
      .minimumWidth = 720U,
      .minimumHeight = 520U,
      .autoCloseAfter = options->recordDuration.count() > 0
                            ? options->recordDuration
                            : options->autoClose,
      .screenshotPath = options->screenshot,
      .restoreSavedFrame = !options->windowSizeSpecified,
  }, app);
  if (!opened) {
    std::cerr << "Native Voicebank Studio unavailable: " << opened.error().message << '\n';
    return 4;
  }
  if (options->recordDuration.count() > 0) {
    const auto started = app.startRecording();
    if (!started) {
      std::cerr << "Voicebank Studio recording failed: "
                << started.error().message << '\n';
      return 5;
    }
  }
  const auto result = window->run();
  app.finishPendingImport();
  const auto stopped = app.stopRecording();
  const auto info = app.inputInfo();
  const auto stats = app.inputStats();
  std::cout << "input_backend=" << info.backend << '\n'
            << "input_physical=" << (info.physical ? "true" : "false") << '\n'
            << "input_callbacks=" << stats.callbacks << '\n'
            << "input_frames=" << stats.frames << '\n'
            << "input_read_failures=" << stats.readFailures << '\n'
            << "recorded_frames=" << app.lastRecordedFrames() << '\n'
            << "recorded_wav=" << app.lastRecording().string() << '\n';
  if (const auto* production = app.productionProject(); production != nullptr) {
    const auto queues = app.productionQueues();
    std::cout << "production_project=" << production->projectId << '\n'
              << "production_generation=" << production->lastDurableGeneration << '\n'
              << "production_inventory_sha256=" << production->inventorySha256 << '\n'
              << "production_missing=" << queues.missing << '\n'
              << "production_rejected=" << queues.rejected << '\n'
              << "production_retake=" << queues.retake << '\n'
              << "production_marker_review=" << queues.markerReview << '\n'
              << "production_pitch_review=" << queues.pitchReview << '\n'
              << "production_approved=" << queues.approved << '\n'
              << "production_staged_recovery="
              << app.stagedRecoveryCandidateCount() << '\n';
  }
  if (!stopped) {
    std::cerr << "Voicebank Studio recording failed: "
              << stopped.error().message << '\n';
    return 5;
  }
  if (!app.lastError().empty()) {
    std::cerr << "last_studio_error=" << app.lastError() << '\n';
  }
  return result;
}
