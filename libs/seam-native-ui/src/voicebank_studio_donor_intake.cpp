#include "seam/native_ui/voicebank_studio.hpp"
#include "voicebank_studio_production_support.hpp"
#include "seam/platform/file_dialog.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include <algorithm>

namespace seam::native_ui {
namespace p = voicebank_production;
namespace {
// Preserve exact bytes in the capture; quote nonprinting ASCII controls in
// the visible presentation while retaining paragraph breaks and tabs.
std::string visible(std::string_view text) {
  std::string result;
  for (const auto c : text) {
    const auto byte = static_cast<unsigned char>(c);
    if (byte < 32U && c != '\n' && c != '\t') {
      constexpr char hex[] = "0123456789abcdef";
      result += "\\x"; result += hex[byte >> 4U]; result += hex[byte & 15U];
    } else result += c;
  }
  return result;
}
std::string details(const p::PreparedDonorIntake& capture) {
  const auto& d = capture.display;
  const auto section = [&](std::string_view key) -> const formats::JsonValue& { return *d.find(key); };
  const auto notice = [&](std::string_view key) {
    const auto& value = section(key);
    return "\nSHA256 " + value.find("sha256")->asString() + "\n" + visible(value.find("text")->asString()) + "\n";
  };
  return "UNAPPROVED DONOR / NO PHONE, PITCH, LISTENING OR RIGHTS APPROVAL\nPROJECT " + section("projectId").asString() +
      "\nPROJECT SHA256 " + section("projectSha256").asString() + "\nSELECTED SOURCE " + section("sourceStrategyId").asString() +
      "\nDONOR " + section("donorId").asString() + "\nPACKET SHA256 " + section("packetSha256").asString() +
      "\nSOURCE PHONES (AUTOMATIC, UNREVIEWED) " + formats::stringifyJson(section("sourcePhones"),false) +
      "\nEXPLICIT TARGET COVERAGE " + capture.take.coverageKey + " / MIDI " + std::to_string(capture.take.pitchLayer) +
      " / STYLE " + capture.take.style + "\nTAKE " + capture.take.takeId + "\nSUPERSEDES " + capture.take.supersedesTakeId +
      "\nSOURCE PROPOSAL (AUTOMATIC, UNASSIGNED)\n" + formats::stringifyJson(section("unreviewedProposal"), true) +
      "\nREVIEWED PRODUCER DECLARATION" + notice("reviewedSourceDeclaration") +
      "\nDECLARED SOURCE PERMISSIONS / RIGHTS " + section("reviewedSourceDeclaration").find("declaredRights")->asString() +
      " / USE " + (section("reviewedSourceDeclaration").find("sourceUse")->asBool()?"yes":"no") +
      " / TRANSFORM " + (section("reviewedSourceDeclaration").find("transformation")->asBool()?"yes":"no") +
      " / BANK REDISTRIBUTION " + (section("reviewedSourceDeclaration").find("singingBankRedistribution")->asBool()?"yes":"no") +
      " / COMMERCIAL RENDERS " + (section("reviewedSourceDeclaration").find("commercialRenders")->asBool()?"yes":"no") +
      "\nORIGINAL SOURCE LICENSE" + notice("originalLicense") +
      "\nORIGINAL SOURCE LOCATOR\n" + formats::stringifyJson(*section("originalLicense").find("source"), true) +
      "\nATTRIBUTION AND CHANGE NOTICE" + notice("attribution") +
      "\nAUDIO IDENTITY\n" + formats::stringifyJson(section("audio"), true) +
      "\nImport retains these bytes and creates an inspected, unapproved take. Source quality, marker review, "
      "phonetic correctness, listening, bank admission and release acceptance remain separate.\n"
      "A first prepared-donor import upgrades a schema-4 project to schema 5 and can reopen earlier take reviews. "
      "Retain the review history and inspect affected takes again before publication.\n";
}
}

core::Result<void> VoicebankStudioController::beginPreparedDonorCapture(const SampleReviewContext& context,
    std::filesystem::path packetPath, std::string donorId, std::size_t assignmentIndex) {
  if (proceduralImportBusy() || dirty_) return core::failure(core::ErrorCode::Conflict,"Finish work and save manifest edits before donor capture");
  const auto current = validateSampleReviewContext(context); if (!current) return current;
  if (packetPath.filename() != "donors.json" || assignmentIndex >= productionProject_->unitAssignments.size() || donorId.empty() || donorId.size() > 128U)
    return core::failure(core::ErrorCode::InvalidArgument,"Choose donors.json, an exact donor ID and an existing assignment");
  if (std::none_of(productionProject_->operators.begin(),productionProject_->operators.end(),[&](const auto& actor) {
      return actor.operatorId == productionOperatorId_ && actor.role == "PRODUCER";
    })) return core::failure(core::ErrorCode::Conflict,"Donor intake requires the actual registered producer");
  proceduralImportStop_ = std::stop_source{}; const auto stop = proceduralImportStop_.get_token(); statusBeforeImport_ = status_;
  try {
    sampleReviewWork_ = std::async(std::launch::async,
        [context,project=*productionProject_,root=productionWorkspaceRoot_,producer=productionOperatorId_,
         path=std::move(packetPath),donor=std::move(donorId),assignmentIndex,stop]() -> core::Result<SampleReviewWorkResult> {
      const auto verified = p::ProductionProjectRepository{root}.verify(project);
      if (!verified) return core::Result<SampleReviewWorkResult>{verified.error()};
      if (stop.stop_requested()) return core::failure<SampleReviewWorkResult>(core::ErrorCode::Conflict,"Donor capture cancelled");
      const auto bytes = core::readFileBytesLimited(path,64ULL*1024ULL*1024ULL);
      if (!bytes) return core::Result<SampleReviewWorkResult>{bytes.error()};
      const auto hash = core::sha256Hex(bytes.value());
      auto held = p::loadPreparedPhoneDonor(path.parent_path(),hash,donor,stop);
      if (!held) return core::Result<SampleReviewWorkResult>{held.error()};
      const auto& row = project.unitAssignments[assignmentIndex];
      auto intake = p::capturePreparedDonorIntake(root,project,std::move(held.value()),hash,donor,
          project.selectedSourceStrategyId,row.coverageKey,row.pitchLayer,row.style,stop);
      if (!intake) return core::Result<SampleReviewWorkResult>{intake.error()};
      auto description = details(intake.value());
      const auto identity = core::sha256Hex(formats::stringifyJson(intake.value().display,true) + "\n");
      return SampleReviewWorkResult{.context=context,.preparedDonorInspection=PreparedDonorInspection{context,producer,identity,
          std::move(description),std::make_shared<const p::PreparedDonorIntake>(std::move(intake.value()))}};
    });
  } catch (const std::exception& error) { return core::failure(core::ErrorCode::Internal,"Cannot start donor capture",error.what()); }
  preparedDonorInspection_.reset(); sampleReviewStatus_ = status_ = "CAPTURING DONOR AND ORIGINAL NOTICES / ESC CANCEL";
  return core::success();
}

core::Result<void> VoicebankStudioController::beginPreparedDonorImport(const SampleReviewContext& context, std::string captureSha256,
    p::PreparedDonorImportOptions options) {
  if (proceduralImportBusy() || dirty_ || !preparedDonorInspection_ || preparedDonorInspection_->context != context ||
      preparedDonorInspection_->captureSha256 != captureSha256 || preparedDonorInspection_->producerId != productionOperatorId_)
    return core::failure(core::ErrorCode::Conflict,"Capture and inspect this exact donor before importing");
  const auto current = validateSampleReviewContext(context); if (!current) return current;
  const auto held = preparedDonorInspection_->intake;
  proceduralImportStop_ = std::stop_source{}; const auto stop = proceduralImportStop_.get_token(); statusBeforeImport_ = status_;
  try {
    sampleReviewWork_ = std::async(std::launch::async,
        [context,held,project=*productionProject_,root=productionWorkspaceRoot_,producer=productionOperatorId_,
         utc=voicebank_studio_internal::currentUtcTimestamp(),stop,options=std::move(options)]() mutable -> core::Result<SampleReviewWorkResult> {
      const auto& take = held->take;
      const auto& evidence = held->snapshot.evidence;
      const auto receipt = p::ProductionProjectRepository{root}.importPreparedDonor(project,held->snapshot,
          {context.projectSha256,evidence.packet.sha256,evidence.donorId,project.selectedSourceStrategyId},take,
          {take.supersedesTakeId.empty()?"import":"retake",take.takeId,producer,utc},stop,options);
      if (!receipt) return core::Result<SampleReviewWorkResult>{receipt.error()};
      return SampleReviewWorkResult{.context=context,.committedProject=std::move(project),.preparedDonorReceipt=receipt.value()};
    });
  } catch (const std::exception& error) { return core::failure(core::ErrorCode::Internal,"Cannot start donor import",error.what()); }
  sampleReviewStatus_ = status_ = "IMPORTING CAPTURED DONOR / NO REVIEW OR RIGHTS APPROVAL";
  return core::success();
}

core::Result<void> VoicebankStudioController::beginProductionPointerReconciliation() {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict,"Finish current production work first");
  const auto context = captureSampleReviewContext(); if (!context) return core::Result<void>{context.error()};
  const ReconciledProductionPointer expected{productionWorkspaceRoot_,productionProject_->projectId,
      context.value().generation,context.value().projectSha256};
  proceduralImportStop_ = std::stop_source{}; const auto stop = proceduralImportStop_.get_token(); statusBeforeImport_ = status_;
  try {
    sampleReviewWork_ = std::async(std::launch::async,[context=context.value(),expected,stop]() -> core::Result<SampleReviewWorkResult> {
      const auto repaired = p::ProductionProjectRepository{expected.workspaceRoot}.reconcileCurrentPointer(
          expected.generation,expected.projectSha256,stop);
      if (!repaired) return core::Result<SampleReviewWorkResult>{repaired.error()};
      return SampleReviewWorkResult{.context=context,.reconciledProductionPointer=expected};
    });
  } catch (const std::exception& error) { return core::failure(core::ErrorCode::Internal,"Cannot start pointer reconciliation",error.what()); }
  sampleReviewStatus_ = status_ = "RECONCILING EXACT PRODUCER POINTER / NO NEW TAKE OR APPROVAL";
  return core::success();
}

core::Result<void> captureStudioPreparedDonor(VoicebankStudioController& controller, platform::IFileDialog& dialog) {
  if (controller.proceduralImportBusy() || controller.dirty()) return core::failure(core::ErrorCode::Conflict,"Finish work and save manifest edits first");
  const auto context = controller.captureSampleReviewContext(); if (!context) return core::Result<void>{context.error()};
  const auto project = *controller.productionProject();
  const auto path = dialog.choose({.purpose=platform::FileDialogPurpose::PreparedDonorPacket,.title="Capture prepared donor packet — no import or approval",
      .initialDirectory=controller.manifestPath().parent_path(),.suggestedName={},.extensions={"json"}});
  if (!path) return core::Result<void>{path.error()};
  if (!path.value()) return core::success();
  std::vector<std::string> choices;
  for (const auto& row : project.unitAssignments)
    choices.push_back(row.coverageKey + " / MIDI " + std::to_string(row.pitchLayer) + " / " + row.style + " / PROMPT " + row.promptId);
  const auto input = dialog.choosePreparedDonor("Packet: " + path.value()->generic_string() + "\nSelected source: " + project.selectedSourceStrategyId +
      "\nEnter the exact donor ID from donors.json and choose its target assignment. No phone, pitch or style is inferred.",choices);
  if (!input) return core::Result<void>{input.error()};
  return input.value() ? controller.beginPreparedDonorCapture(context.value(),*path.value(),input.value()->donorId,input.value()->assignmentIndex) : core::success();
}

core::Result<void> importStudioPreparedDonor(VoicebankStudioController& controller, platform::IFileDialog& dialog) {
  if (controller.proceduralImportBusy() || !controller.preparedDonorInspection())
    return core::failure(core::ErrorCode::Conflict,"Capture a donor and inspect its original notices first");
  // Only immutable data crosses the modal. A recapture under the same project
  // context must not silently substitute different notices or donor bytes.
  const auto expected = *controller.preparedDonorInspection();
  const auto current = controller.validateSampleReviewContext(expected.context); if (!current) return current;
  const auto answer = dialog.confirmPreparedDonorImport("PRODUCER " + expected.producerId + "\nCAPTURE SHA256 " + expected.captureSha256 + "\n" + expected.details);
  if (!answer) return core::Result<void>{answer.error()};
  return answer.value() ? controller.beginPreparedDonorImport(expected.context,expected.captureSha256) : core::success();
}
}
