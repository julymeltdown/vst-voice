#include "seam/native_ui/voicebank_studio.hpp"
#include "voicebank_studio_production_support.hpp"
#include "seam/platform/file_dialog.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/voicebank_production/source_assessment.hpp"
#include <algorithm>

namespace seam::native_ui {
namespace production = voicebank_production;

core::Result<void> VoicebankStudioController::beginSourceLicenseCapture(const SampleReviewContext& context, std::filesystem::path path) {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict,"Finish current Studio work before source setup");
  const auto current = validateSampleReviewContext(context); if (!current) return current;
  if (productionProject_->schemaVersion < 2 || std::none_of(productionProject_->operators.begin(),productionProject_->operators.end(),[&](const auto& actor) {
      return actor.operatorId == productionOperatorId_ && actor.role == "PRODUCER";
    })) return core::failure(core::ErrorCode::Conflict,"Source setup requires a source-aware project and its actual registered producer");
  proceduralImportStop_ = std::stop_source{}; const auto stop = proceduralImportStop_.get_token(); statusBeforeImport_ = status_;
  try {
    sampleReviewWork_ = std::async(std::launch::async,
        [context,project=*productionProject_,root=productionWorkspaceRoot_,producer=productionOperatorId_,path=std::move(path),stop]() -> core::Result<SampleReviewWorkResult> {
      if (stop.stop_requested()) return core::failure<SampleReviewWorkResult>(core::ErrorCode::Conflict,"License capture cancelled");
      const auto verified = production::ProductionProjectRepository{root}.verify(project);
      if (!verified) return core::Result<SampleReviewWorkResult>{verified.error()};
      const auto bytes = core::readFileBytesLimited(path,4ULL*1024ULL*1024ULL);
      if (!bytes || bytes.value().empty()) return core::failure<SampleReviewWorkResult>(core::ErrorCode::InvalidArgument,"License evidence must be a nonempty bounded regular file");
      std::error_code error;
      const auto absolute = std::filesystem::absolute(path,error).lexically_normal();
      if (error) return core::failure<SampleReviewWorkResult>(core::ErrorCode::InvalidArgument,"Cannot resolve license path",error.message());
      if (stop.stop_requested()) return core::failure<SampleReviewWorkResult>(core::ErrorCode::Conflict,"License capture cancelled");
      return SampleReviewWorkResult{.context=context,.sourceRegistrationInspection=SourceRegistrationInspection{context,absolute,core::sha256Hex(bytes.value()),producer}};
    });
  } catch (const std::exception& error) { return core::failure(core::ErrorCode::Internal,"Cannot start source license capture",error.what()); }
  sourceRegistrationInspection_.reset(); sampleReviewStatus_ = status_ = "CAPTURING SOURCE LICENSE / ESC CANCEL";
  return core::success();
}

core::Result<void> VoicebankStudioController::beginSourceRegistration(const SourceRegistrationInspection& expected,
    production::SourceStrategyAssessment source) {
  if (proceduralImportBusy() || !sourceRegistrationInspection_ || *sourceRegistrationInspection_ != expected)
    return core::failure(core::ErrorCode::Conflict,"Capture the current source license before registration");
  const auto current = validateSampleReviewContext(expected.context); if (!current) return current;
  if (expected.producerId != productionOperatorId_) return core::failure(core::ErrorCode::Conflict,"Source producer identity changed");
  source.licenseLocator = expected.evidencePath.string(); source.licenseSha256 = expected.evidenceSha256;
  proceduralImportStop_ = std::stop_source{}; const auto stop = proceduralImportStop_.get_token(); statusBeforeImport_ = status_;
  try {
    sampleReviewWork_ = std::async(std::launch::async,
        [context=expected.context,project=*productionProject_,root=productionWorkspaceRoot_,producer=expected.producerId,
         source=std::move(source),utc=voicebank_studio_internal::currentUtcTimestamp(),stop]() mutable -> core::Result<SampleReviewWorkResult> {
      const auto receipt = production::ProductionProjectRepository{root}.registerSource(project,source,context.projectSha256,producer,utc,stop);
      if (!receipt) return core::Result<SampleReviewWorkResult>{receipt.error()};
      return SampleReviewWorkResult{.context=context,.committedProject=std::move(project),.sourceRegistrationReceipt=receipt.value()};
    });
  } catch (const std::exception& error) { return core::failure(core::ErrorCode::Internal,"Cannot start source registration",error.what()); }
  sampleReviewStatus_ = status_ = "REGISTERING SOURCE / DECLARATION ONLY / NO MUSICAL APPROVAL";
  return core::success();
}

core::Result<void> captureStudioSourceLicense(VoicebankStudioController& controller, platform::IFileDialog& dialog) {
  if (controller.proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict,"Finish current Studio work first");
  const auto context = controller.captureSampleReviewContext(); if (!context) return core::Result<void>{context.error()};
  const auto path = dialog.choose({.purpose=platform::FileDialogPurpose::SourceLicenseEvidence,
      .title="Capture authorization evidence for a NEW source — no approval",.initialDirectory=controller.manifestPath().parent_path(),
      .suggestedName={},.extensions={"txt","md","json","pdf"}});
  if (!path) return core::Result<void>{path.error()};
  return path.value() ? controller.beginSourceLicenseCapture(context.value(),*path.value()) : core::success();
}

core::Result<void> registerStudioSource(VoicebankStudioController& controller, platform::IFileDialog& dialog) {
  if (controller.proceduralImportBusy() || !controller.sourceRegistrationInspection())
    return core::failure(core::ErrorCode::Conflict,"Capture a source license before registration");
  const auto expected = *controller.sourceRegistrationInspection();
  const auto current = controller.validateSampleReviewContext(expected.context); if (!current) return current;
  const std::string summary = "PRODUCER " + expected.producerId + "\nPROJECT SHA256 " + expected.context.projectSha256 +
      "\nEVIDENCE " + expected.evidencePath.generic_string() + "\nEVIDENCE SHA256 " + expected.evidenceSha256 +
      "\nDeclare only rights supported by this evidence. Existing take provenance remains unchanged. Coverage/listening stay Not assessed.";
  const auto entered = dialog.chooseSourceRegistration(summary);
  if (!entered) return core::Result<void>{entered.error()};
  if (!entered.value()) return core::success();
  const auto& input = *entered.value();
  if ((input.kind!="human" && input.kind!="procedural" && input.kind!="tts") ||
      (input.rights!="pass" && input.rights!="blocked" && input.rights!="not-assessed") ||
      std::any_of(input.permissions.begin(),input.permissions.end(),[](const auto& permission) { return permission!="yes" && permission!="no"; }))
    return core::failure(core::ErrorCode::InvalidArgument,"Explicit source kind, rights outcome and all four yes/no permissions are required");
  production::SourceStrategyAssessment source{.id=input.id,
      .kind=input.kind=="human"?production::SourceStrategyKind::HumanRecording:input.kind=="procedural"?production::SourceStrategyKind::ProceduralSynthesis:production::SourceStrategyKind::TtsDerived,
      .rights=input.rights=="pass"?production::Feasibility::Pass:input.rights=="blocked"?production::Feasibility::Blocked:production::Feasibility::NotAssessed,
      .permissions={input.permissions[0]=="yes",input.permissions[1]=="yes",input.permissions[2]=="yes",input.permissions[3]=="yes"}};
  return controller.beginSourceRegistration(expected,std::move(source));
}

core::Result<void> VoicebankStudioController::beginSourceQualityEvidenceCapture(
    const SampleReviewContext& context, std::filesystem::path evidencePath) {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict,"Finish current Studio work before source evidence capture");
  const auto current = validateSampleReviewContext(context); if (!current) return current;
  proceduralImportStop_ = std::stop_source{}; const auto stop = proceduralImportStop_.get_token(); statusBeforeImport_ = status_;
  try {
    sampleReviewWork_ = std::async(std::launch::async,
        [context, project = *productionProject_, root = productionWorkspaceRoot_, evidencePath = std::move(evidencePath), stop]() -> core::Result<SampleReviewWorkResult> {
      if (stop.stop_requested()) return core::failure<SampleReviewWorkResult>(core::ErrorCode::Conflict,"Source evidence capture cancelled");
      const auto verified = production::ProductionProjectRepository{root}.verify(project);
      if (!verified) return core::Result<SampleReviewWorkResult>{verified.error()};
      const auto source = std::find_if(project.sourceStrategies.begin(),project.sourceStrategies.end(),[&](const auto& value) { return value.id == project.selectedSourceStrategyId; });
      if (source == project.sourceStrategies.end()) return core::failure<SampleReviewWorkResult>(core::ErrorCode::InvalidState,"Select a source strategy before assessment");
      const auto material = production::sourceQualityMaterialIdentity(project,source->id);
      if (!material) return core::Result<SampleReviewWorkResult>{material.error()};
      const auto bytes = core::readFileBytesLimited(evidencePath,4ULL*1024ULL*1024ULL);
      if (!bytes || bytes.value().empty()) return core::failure<SampleReviewWorkResult>(core::ErrorCode::InvalidArgument,"Source evidence must be a nonempty bounded document");
      SourceQualityInspection inspection;
      inspection.context = context; inspection.evidencePath = std::filesystem::absolute(evidencePath).lexically_normal();
      inspection.assessment.strategyId = source->id; inspection.assessment.policySha256 = production::sourceQualityPolicyIdentity(*source);
      inspection.assessment.materialSha256 = material.value(); inspection.assessment.evidenceSha256 = core::sha256Hex(bytes.value());
      for (const auto& actor : project.operators) {
        auto candidate = inspection.assessment; candidate.reviewerId = actor.operatorId;
        if (production::validateSourceQualityReviewer(project,candidate)) inspection.reviewers.push_back(actor.operatorId);
      }
      inspection.details = {"SOURCE QUALITY / EXPLICIT REVIEWER DECISION, NOT AN AUTOMATIC MEASUREMENT",
          "SOURCE " + source->id, "PROJECT SHA256 " + context.projectSha256,
          "POLICY SHA256 " + inspection.assessment.policySha256, "MATERIAL SHA256 " + material.value(),
          "EVIDENCE " + inspection.evidencePath.generic_string(), "EVIDENCE SHA256 " + inspection.assessment.evidenceSha256,
          "Coverage/listening decisions do not grant source rights or unit approval."};
      for (const auto& binding : project.sourceBindings) if (binding.strategy.id == source->id &&
          std::any_of(project.unitAssignments.begin(),project.unitAssignments.end(),[&](const auto& row) { return row.takeId == binding.takeId; }))
        inspection.details.push_back("ASSESSED TAKE " + binding.takeId + " / RAW " + binding.rawAssetSha256 + " / IMPORTER " + binding.importerId);
      if (stop.stop_requested()) return core::failure<SampleReviewWorkResult>(core::ErrorCode::Conflict,"Source evidence capture cancelled");
      return SampleReviewWorkResult{.context = context,.sourceQualityInspection = std::move(inspection)};
    });
  } catch (const std::exception& error) { return core::failure(core::ErrorCode::Internal,"Cannot start source evidence capture",error.what()); }
  sourceQualityInspection_.reset(); sampleReviewStatus_ = status_ = "CAPTURING SOURCE EVIDENCE / ESC CANCEL";
  return core::success();
}

core::Result<void> VoicebankStudioController::beginSourceQualityDecision(const SourceQualityInspection& expected,
    std::string id, std::string reviewerId, production::Feasibility coverage, production::Feasibility listening, std::string reviewedAtUtc) {
  if (proceduralImportBusy() || !sourceQualityInspection_ || sourceQualityInspection_->context != expected.context ||
      sourceQualityInspection_->assessment != expected.assessment || sourceQualityInspection_->evidencePath != expected.evidencePath)
    return core::failure(core::ErrorCode::Conflict,"Capture current source evidence before recording a decision");
  const auto context = expected.context;
  const auto current = validateSampleReviewContext(context); if (!current) return current;
  if (id.empty() || id.size()>128U || std::find(sourceQualityInspection_->reviewers.begin(),sourceQualityInspection_->reviewers.end(),reviewerId)==sourceQualityInspection_->reviewers.end() ||
      production::toString(coverage).empty() || production::toString(listening).empty())
    return core::failure(core::ErrorCode::InvalidArgument,"Provide a unique assessment ID, registered independent reviewer and explicit outcomes");
  if (reviewedAtUtc.empty()) reviewedAtUtc = voicebank_studio_internal::currentUtcTimestamp();
  auto assessment = expected.assessment;
  assessment.id = std::move(id); assessment.reviewerId = std::move(reviewerId); assessment.reviewedAtUtc = std::move(reviewedAtUtc);
  assessment.coverage = coverage; assessment.listening = listening;
  proceduralImportStop_ = std::stop_source{}; const auto stop = proceduralImportStop_.get_token(); statusBeforeImport_ = status_;
  try {
    sampleReviewWork_ = std::async(std::launch::async,
        [context, project = *productionProject_, root = productionWorkspaceRoot_, assessment = std::move(assessment), evidence = expected.evidencePath, stop]() mutable -> core::Result<SampleReviewWorkResult> {
      const auto receipt = production::ProductionProjectRepository{root}.recordSourceQualityAssessment(project,assessment,evidence,context.projectSha256,stop);
      if (!receipt) return core::Result<SampleReviewWorkResult>{receipt.error()};
      return SampleReviewWorkResult{.context = context,.committedProject = std::move(project),.sourceQualityReceipt = receipt.value()};
    });
  } catch (const std::exception& error) { return core::failure(core::ErrorCode::Internal,"Cannot start source quality recording",error.what()); }
  sampleReviewStatus_ = status_ = "RECORDING SOURCE QUALITY / NO SOURCE RIGHTS OR UNIT APPROVAL GRANTED";
  return core::success();
}

core::Result<void> captureStudioSourceQuality(VoicebankStudioController& controller, platform::IFileDialog& dialog) {
  if (controller.proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict,"Finish current Studio work first");
  const auto context = controller.captureSampleReviewContext(); if (!context) return core::Result<void>{context.error()};
  const auto path = dialog.choose({.purpose = platform::FileDialogPurpose::SourceQualityEvidence,.title = "Capture source quality evidence — no approval",
      .initialDirectory = controller.manifestPath().parent_path(),.suggestedName = {},.extensions = {"txt","md","json"}});
  if (!path) return core::Result<void>{path.error()};
  return path.value() ? controller.beginSourceQualityEvidenceCapture(context.value(),*path.value()) : core::success();
}

core::Result<void> confirmStudioSourceQuality(VoicebankStudioController& controller, platform::IFileDialog& dialog) {
  if (controller.proceduralImportBusy() || !controller.sourceQualityInspection()) return core::failure(core::ErrorCode::Conflict,"Capture source quality evidence first");
  const auto inspection = *controller.sourceQualityInspection();
  const auto current = controller.validateSampleReviewContext(inspection.context); if (!current) return current;
  std::string summary;
  for (std::size_t i=0U;i<std::min<std::size_t>(8U,inspection.details.size());++i) summary += inspection.details[i]+"\n";
  const auto input = dialog.chooseSourceQualityDecision(summary,inspection.reviewers);
  if (!input) return core::Result<void>{input.error()};
  if (!input.value()) return core::success();
  const auto outcome = [](std::string_view value) -> std::optional<production::Feasibility> {
    if (value=="pass") return production::Feasibility::Pass;
    if (value=="blocked") return production::Feasibility::Blocked;
    if (value=="not-assessed") return production::Feasibility::NotAssessed;
    return std::nullopt;
  };
  const auto coverage = outcome(input.value()->coverage), listening = outcome(input.value()->listening);
  if (!coverage || !listening) return core::failure(core::ErrorCode::InvalidArgument,"Unknown source quality outcome");
  return controller.beginSourceQualityDecision(inspection,input.value()->id,input.value()->reviewerId,*coverage,*listening);
}
}
