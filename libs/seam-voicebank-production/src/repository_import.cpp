#include "seam/voicebank_production/repository.hpp"
#include "seam/voice_design/procedural_candidate.hpp"
#include "seam/core/sha256.hpp"
#include "seam/core/file_io.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/candidate_markers.hpp"

#include <algorithm>
#include <set>

namespace seam::voicebank_production {
namespace {
// Stage into a private draft. Only a successful save or an exact recovered
// generation may replace caller state; a late cancellation never hides commit.
core::Result<ProductionCommitReceipt> commitImportedDraft(ProductionProjectRepository& repository,
    VoicebankProductionProject& project, VoicebankProductionProject draft,
    const ProductionJournalEvent& event, std::stop_token stop) {
  const auto previousGeneration = project.lastDurableGeneration;
  const auto saved = repository.save(draft, event, stop);
  bool durabilityConfirmed = true;
  std::string diagnostic;
  if (!saved) {
    const auto recovered = repository.recover();
    if (recovered && recovered.value().lastDurableGeneration > previousGeneration) {
      auto comparable = recovered.value();
      comparable.lastDurableGeneration = draft.lastDurableGeneration;
      if (encodeProductionProject(comparable) != encodeProductionProject(draft))
        return core::failure<ProductionCommitReceipt>(core::ErrorCode::Conflict,
            "Import save failed and recovery found different work. Recover before retrying; no matching import is confirmed.", event.subjectId);
      draft = recovered.value();
      durabilityConfirmed = false;
      diagnostic = "The exact import is recoverably committed, but pointer durability is uncertain. "
          "Recover and inspect this generation before further work; do not repeat the import. " + saved.error().message;
    } else {
      return core::failure<ProductionCommitReceipt>(saved.error().code,
          "Import did not confirm a commit. Recover and compare the intended take or batch before retrying: " + saved.error().message,
          event.subjectId);
    }
  }
  const auto generation = draft.lastDurableGeneration;
  auto hash = core::sha256Hex(encodeProductionProject(draft));
  project = std::move(draft);
  return ProductionCommitReceipt{generation, std::move(hash), durabilityConfirmed, std::move(diagnostic)};
}

core::Result<std::string> snapshotSourceEvidence(const std::filesystem::path& root, const SourceStrategyAssessment& strategy) {
  const auto bytes = core::readFileBytesLimited(strategy.licenseLocator, 4ULL * 1024ULL * 1024ULL);
  if (!bytes) return core::Result<std::string>{bytes.error()};
  if (core::sha256Hex(bytes.value()) != strategy.licenseSha256)
    return core::failure<std::string>(core::ErrorCode::Conflict, "Source evidence differs from its declared execution authorization");
  const auto directory = root / "source-evidence";
  std::error_code error;
  auto status = std::filesystem::symlink_status(directory, error);
  if (error == std::errc::no_such_file_or_directory || (!error && status.type() == std::filesystem::file_type::not_found)) {
    error.clear();
    std::filesystem::create_directory(directory, error);
    if (error && error != std::errc::file_exists)
      return core::failure<std::string>(core::ErrorCode::IoError, "Cannot create retained source evidence directory", error.message());
    error.clear();
    status = std::filesystem::symlink_status(directory, error);
  }
  if (error || !std::filesystem::is_directory(status) || std::filesystem::is_symlink(status))
    return core::failure<std::string>(core::ErrorCode::Conflict, "Source evidence directory must be a real directory");
  const auto relative = "source-evidence/" + strategy.licenseSha256 + ".txt";
  const auto path = root / relative;
  error.clear();
  const auto existing = std::filesystem::symlink_status(path, error);
  if (!error && std::filesystem::exists(existing)) {
    const auto retained = core::readFileBytesLimited(path, 4ULL * 1024ULL * 1024ULL);
    if (!retained || core::sha256Hex(retained.value()) != strategy.licenseSha256)
      return core::failure<std::string>(core::ErrorCode::Conflict, "Retained source evidence is unsafe or changed");
  } else {
    if (error && error != std::errc::no_such_file_or_directory)
      return core::failure<std::string>(core::ErrorCode::IoError, "Cannot inspect retained source evidence", error.message());
    const auto saved = core::durableAtomicWriteNew(path, bytes.value());
    if (!saved) return core::Result<std::string>{saved.error()};
  }
  return relative;
}
}  // namespace

core::Result<GenerationImportExpectation> captureGenerationImportExpectation(
    const VoicebankProductionProject& project, const RawTakeInput& take,
    const synthesis::ProceduralSingerResource& recipe, std::string style,
    std::string renderContentHash, std::uint32_t sampleRate, std::int64_t frameCount) {
  const auto valid = validateProductionProject(project);
  if (!valid) return core::Result<GenerationImportExpectation>{valid.error()};
  const auto decoded = voice_design::decodeVoiceRecipeResource(recipe);
  if (!decoded) return core::Result<GenerationImportExpectation>{decoded.error()};
  const auto strategy = std::find_if(project.sourceStrategies.begin(), project.sourceStrategies.end(),
      [&](const auto& value) { return value.id == project.selectedSourceStrategyId; });
  const auto execution = requireSelectedSourceExecution(project);
  if (!execution) return core::Result<GenerationImportExpectation>{execution.error()};
  if (strategy == project.sourceStrategies.end() || strategy->kind != SourceStrategyKind::ProceduralSynthesis ||
      std::any_of(project.takes.begin(), project.takes.end(), [&](const auto& value) { return value.takeId == take.takeId; }))
    return core::failure<GenerationImportExpectation>(core::ErrorCode::Conflict, "Generation requires an authorized procedural strategy and a new take ID");
  if ((project.schemaVersion >= kProductionStyleSchemaVersion ? take.style != style : !take.style.empty()) ||
      project.lastDurableGeneration == 0U || style.empty() || style.size() > 128U ||
      renderContentHash.size() != 64U || !std::all_of(renderContentHash.begin(), renderContentHash.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
      }) || sampleRate < 8000U || sampleRate > 384000U || frameCount <= 0 || frameCount > 32LL * 1024LL * 1024LL ||
      take.takeId.empty() || take.initialState != UnitQueueState::MarkerReview || take.review)
    return core::failure<GenerationImportExpectation>(core::ErrorCode::InvalidArgument, "Generation expectation is incomplete or exceeds bounds");
  const auto assignment = std::find_if(project.unitAssignments.begin(), project.unitAssignments.end(), [&](const auto& value) {
    return value.style == take.style && value.coverageKey == take.coverageKey && value.pitchLayer == take.pitchLayer && value.promptId == take.promptId &&
        value.takeId == take.supersedesTakeId;
  });
  if (assignment == project.unitAssignments.end()) return core::failure<GenerationImportExpectation>(
      core::ErrorCode::Conflict, "Generation target differs from its current assignment");
  GenerationImportExpectation result{core::sha256Hex(encodeProductionProject(project)), take.takeId, take.promptId,
      take.coverageKey, take.supersedesTakeId, take.pitchLayer, recipe.identity.id, recipe.identity.version,
      recipe.identity.contentHash, std::move(style), std::move(renderContentHash), sampleRate, frameCount};
  const auto encoded = encodeGenerationImportExpectation(result);
  if (!encoded) return core::Result<GenerationImportExpectation>{encoded.error()};
  return result;
}

core::Result<CommittedAssetRecord> ProductionProjectRepository::importRaw(
    VoicebankProductionProject& project, const std::filesystem::path& source,
    const RawTakeInput& take, const ProductionJournalEvent& event) {
  auto draft = project;
  const auto imported = importRawBound(draft, source, take, event, {}, std::nullopt);
  if (!imported) return core::Result<CommittedAssetRecord>{imported.error()};
  const auto committed = commitImportedDraft(*this, project, std::move(draft), event, {});
  if (!committed) return core::Result<CommittedAssetRecord>{committed.error()};
  return CommittedAssetRecord{imported.value(), committed.value()};
}

core::Result<std::optional<CollectedGenerationResult>> ProductionProjectRepository::findCollectedGeneration(
    const GenerationImportExpectation& expectation) const {
  using Output = std::optional<CollectedGenerationResult>;
  const auto encoded = encodeGenerationImportExpectation(expectation);
  if (!encoded) return core::Result<Output>{encoded.error()};
  const auto project = recover();
  if (!project) return core::Result<Output>{project.error()};
  const auto& state = project.value();
  const auto take = std::find_if(state.takes.begin(), state.takes.end(), [&](const auto& value) { return value.takeId == expectation.takeId; });
  if (take == state.takes.end()) return Output{};
  const auto mismatch = [] { return core::failure<Output>(core::ErrorCode::Conflict, "Existing take does not prove collection of this generation request"); };
  if ((state.schemaVersion >= kProductionStyleSchemaVersion && take->style != expectation.style) ||
      take->promptId != expectation.promptId || take->coverageKey != expectation.coverageKey || take->pitchLayer != expectation.pitchLayer ||
      take->supersedesTakeId != expectation.supersedesTakeId) return mismatch();
  const auto lineage = std::find_if(state.metadataRevisions.begin(), state.metadataRevisions.end(), [&](const auto& value) {
    return value.takeId == take->takeId && value.kind == "procedural-lineage";
  });
  if (lineage == state.metadataRevisions.end() || !lineage->values.contains("generationExpectationSha256") ||
      lineage->values.at("generationExpectationSha256") != core::sha256Hex(encoded.value())) return mismatch();
  const auto resolved = resolveCandidateMarkers(state, take->takeId);
  if (!resolved) return core::Result<Output>{resolved.error()};
  const auto& candidate = resolved.value().candidate;
  if (candidate.recipe.identity.id != expectation.recipeId || candidate.recipe.identity.version != expectation.recipeVersion ||
      candidate.recipe.identity.contentHash != expectation.recipeHash || candidate.renderContentHash != expectation.renderContentHash ||
      candidate.style != expectation.style || candidate.sampleRate != expectation.sampleRate || candidate.frameCount != expectation.frameCount)
    return mismatch();
  const bool active = std::any_of(state.unitAssignments.begin(), state.unitAssignments.end(), [&](const auto& value) { return value.takeId == take->takeId; });
  return Output{CollectedGenerationResult{take->takeId, take->rawAssetSha256, state.lastDurableGeneration, take->state, active}};
}

core::Result<CommittedAssetRecord> ProductionProjectRepository::importProceduralCandidate(
    VoicebankProductionProject& project, const std::filesystem::path& metadataPath,
    const std::filesystem::path& audioPath, const synthesis::ProceduralSingerResource& recipe,
    const RawTakeInput& take, const ProductionJournalEvent& event, std::stop_token stopToken,
    const GenerationImportExpectation* expectation) {
  auto draft = project;
  const auto imported = importProceduralCandidateBound(draft, metadataPath, audioPath, recipe, take, event, stopToken,
      expectation, core::sha256Hex(encodeProductionProject(project)));
  if (!imported) return core::Result<CommittedAssetRecord>{imported.error()};
  const auto committed = commitImportedDraft(*this, project, std::move(draft), event, stopToken);
  if (!committed) return core::Result<CommittedAssetRecord>{committed.error()};
  return CommittedAssetRecord{imported.value(), committed.value()};
}

core::Result<CommittedImportBatch> ProductionProjectRepository::importGeneratedBatch(
    VoicebankProductionProject& project, std::span<const GeneratedCandidateInput> inputs,
    const ProductionJournalEvent& event, std::uint64_t maximumFrames, std::stop_token stopToken) {
  using Output = CommittedImportBatch;
  if (stopToken.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Batch collection cancelled");
  if (inputs.empty() || inputs.size() > 64U || maximumFrames == 0U || maximumFrames > 64ULL * 32ULL * 1024ULL * 1024ULL ||
      event.action != "import-generated-batch" || event.subjectId.empty() || !isProductionUtcTimestamp(event.occurredAtUtc) ||
      std::none_of(project.operators.begin(), project.operators.end(), [&](const auto& value) { return value.operatorId == event.operatorId; }))
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Batch collection request exceeds bounds or has an invalid journal event");
  const auto valid = validateProductionProject(project);
  if (!valid) return core::Result<Output>{valid.error()};
  const auto originalState = core::sha256Hex(encodeProductionProject(project));
  std::set<std::string> takes;
  std::set<ProductionUnitIdentity> assignments;
  std::uint64_t frames = 0U;
  for (const auto& input : inputs) {
    const auto encoded = encodeGenerationImportExpectation(input.expectation);
    if (!encoded) return core::Result<Output>{encoded.error()};
    const auto& request = input.expectation;
    const auto count = static_cast<std::uint64_t>(request.frameCount);
    const auto assignment = std::find_if(project.unitAssignments.begin(), project.unitAssignments.end(), [&](const auto& value) {
      return value.coverageKey == request.coverageKey && value.pitchLayer == request.pitchLayer &&
          (project.schemaVersion < kProductionStyleSchemaVersion || value.style == request.style) &&
          value.promptId == request.promptId && value.takeId == request.supersedesTakeId;
    });
    if (request.projectStateSha256 != originalState || !takes.insert(request.takeId).second ||
        !assignments.insert({project.language, project.schemaVersion >= kProductionStyleSchemaVersion ? request.style : "", request.coverageKey, request.pitchLayer}).second || count > maximumFrames - frames ||
        assignment == project.unitAssignments.end() || std::any_of(project.takes.begin(), project.takes.end(), [&](const auto& value) { return value.takeId == request.takeId; }))
      return core::failure<Output>(core::ErrorCode::Conflict, "Batch collection is stale, duplicated or over its frame budget");
    frames += count;
  }
  auto draft = project;
  std::vector<AssetRecord> result;
  result.reserve(inputs.size());
  for (const auto& input : inputs) {
    const auto& request = input.expectation;
    const auto imported = importProceduralCandidateBound(draft, input.metadataPath, input.audioPath, input.recipe,
        {.takeId = request.takeId, .promptId = request.promptId, .coverageKey = request.coverageKey,
         .pitchLayer = request.pitchLayer, .supersedesTakeId = request.supersedesTakeId,
         .style = project.schemaVersion >= kProductionStyleSchemaVersion ? request.style : ""},
        {.action = request.supersedesTakeId.empty() ? "import-procedural" : "retake", .subjectId = request.takeId,
         .operatorId = event.operatorId, .occurredAtUtc = event.occurredAtUtc}, stopToken, &request, originalState);
    if (!imported) return core::Result<Output>{imported.error()};
    result.push_back(imported.value());
  }
  const auto committed = commitImportedDraft(*this, project, std::move(draft), event, stopToken);
  if (!committed) return core::Result<Output>{committed.error()};
  return CommittedImportBatch{committed.value(), std::move(result)};
}

core::Result<AssetRecord> ProductionProjectRepository::importProceduralCandidateBound(
    VoicebankProductionProject& project, const std::filesystem::path& metadataPath,
    const std::filesystem::path& audioPath, const synthesis::ProceduralSingerResource& recipe,
    const RawTakeInput& take, const ProductionJournalEvent& event, std::stop_token stopToken,
    const GenerationImportExpectation* expectation, std::string_view originalState) {
  if (stopToken.stop_requested()) return core::failure<AssetRecord>(
      core::ErrorCode::Conflict, "Candidate import cancelled before commit");
  if (expectation) {
    const auto valid = encodeGenerationImportExpectation(*expectation);
    if (!valid) return core::Result<AssetRecord>{valid.error()};
  }
  if (expectation && (expectation->projectStateSha256 != originalState ||
      (project.schemaVersion >= kProductionStyleSchemaVersion && expectation->style != take.style) ||
      expectation->takeId != take.takeId || expectation->promptId != take.promptId ||
      expectation->coverageKey != take.coverageKey || expectation->pitchLayer != take.pitchLayer ||
      expectation->supersedesTakeId != take.supersedesTakeId || expectation->recipeId != recipe.identity.id ||
      expectation->recipeVersion != recipe.identity.version || expectation->recipeHash != recipe.identity.contentHash))
    return core::failure<AssetRecord>(core::ErrorCode::Conflict, "Generated candidate request is stale or belongs to another target");
  const auto strategy = std::find_if(project.sourceStrategies.begin(), project.sourceStrategies.end(),
      [&](const auto& value) { return value.id == project.selectedSourceStrategyId; });
  const auto execution = requireSelectedSourceExecution(project);
  if (!execution) return core::Result<AssetRecord>{execution.error()};
  if (strategy == project.sourceStrategies.end() || strategy->kind != SourceStrategyKind::ProceduralSynthesis ||
      take.initialState != UnitQueueState::MarkerReview || take.review || event.subjectId != take.takeId ||
      event.action != (take.supersedesTakeId.empty() ? "import-procedural" : "retake")) {
    return core::failure<AssetRecord>(core::ErrorCode::InvalidArgument,
        "Procedural candidates require an authorized procedural strategy and an unapproved marker-review import");
  }
  const auto candidate = voice_design::loadProceduralCandidate(metadataPath, audioPath, recipe, stopToken);
  if (!candidate) return core::Result<AssetRecord>{candidate.error()};
  if (project.schemaVersion >= kProductionStyleSchemaVersion && candidate.value().style != take.style)
    return core::failure<AssetRecord>(core::ErrorCode::Conflict, "Generated candidate style differs from its producer assignment");
  if (expectation && (candidate.value().style != expectation->style ||
      candidate.value().renderContentHash != expectation->renderContentHash ||
      candidate.value().sampleRate != expectation->sampleRate || candidate.value().frameCount != expectation->frameCount))
    return core::failure<AssetRecord>(core::ErrorCode::Conflict, "Generated candidate output differs from its captured request");
  const auto bytes = recipe.patch->bytes();
  MetadataRevision lineage{.revisionId = "procedural:" + take.takeId, .takeId = take.takeId,
      .rawAssetSha256 = candidate.value().audioSha256, .kind = "procedural-lineage",
      .values = {{"candidateMetadata", candidate.value().metadataJson},
                 {"candidateMetadataSha256", core::sha256Hex(candidate.value().metadataJson)},
                 {"recipeJson", std::string{reinterpret_cast<const char*>(bytes.data()), bytes.size()}},
                 {"recipeHash", recipe.identity.contentHash}, {"renderContentHash", candidate.value().renderContentHash},
                 {"approval", "unapproved"}},
      .operatorId = event.operatorId, .performedAtUtc = event.occurredAtUtc};
  if (expectation) lineage.values.emplace("generationExpectationSha256", core::sha256Hex(encodeGenerationImportExpectation(*expectation).value()));
  return importRawBound(project, audioPath, take, event, candidate.value().audioSha256, lineage, stopToken);
}

core::Result<AssetRecord> ProductionProjectRepository::importRawBound(
    VoicebankProductionProject& project, const std::filesystem::path& source,
    const RawTakeInput& take, const ProductionJournalEvent& event,
    std::string_view expectedDigest, const std::optional<MetadataRevision>& lineage,
    std::stop_token stopToken) {
  if (stopToken.stop_requested()) return core::failure<AssetRecord>(
      core::ErrorCode::Conflict, "Candidate import cancelled before commit");
  const auto expectedAction = !take.supersedesTakeId.empty() ? "retake" : (lineage ? "import-procedural" : "import");
  if (event.action != expectedAction || event.subjectId != take.takeId || take.takeId.empty())
    return core::failure<AssetRecord>(core::ErrorCode::InvalidArgument,
        "Import journal action and subject must identify this exact raw, procedural or retake operation");
  const auto validProject = validateProductionProject(project);
  if (!validProject) return core::Result<AssetRecord>{validProject.error()};
  const auto execution = requireSelectedSourceExecution(project);
  if (!execution) return core::Result<AssetRecord>{execution.error()};
  if (take.initialState == UnitQueueState::Approved || !isProductionUtcTimestamp(event.occurredAtUtc) ||
      std::none_of(project.operators.begin(), project.operators.end(), [&](const auto& value) { return value.operatorId == event.operatorId; }))
    return core::failure<AssetRecord>(core::ErrorCode::InvalidArgument, "Import requires an attributed unapproved take");
  const auto selected = std::find_if(project.sourceStrategies.begin(), project.sourceStrategies.end(),
      [&](const auto& value) { return value.id == project.selectedSourceStrategyId; });
  const auto sourceStrategy = *selected;
  if (take.takeId.empty() || take.promptId.empty() || take.coverageKey.empty() ||
      (project.schemaVersion >= kProductionStyleSchemaVersion ? take.style.empty() : !take.style.empty())) {
    return core::failure<AssetRecord>(core::ErrorCode::InvalidArgument,
                                      "Raw take identity is incomplete");
  }
  const auto duplicate = std::find_if(
      project.takes.begin(), project.takes.end(),
      [&take](const TakeRecord& value) { return value.takeId == take.takeId; });
  if (duplicate != project.takes.end()) {
    return core::failure<AssetRecord>(core::ErrorCode::Conflict,
                                      "Raw take identifier already exists");
  }
  auto assignment = std::find_if(
      project.unitAssignments.begin(), project.unitAssignments.end(),
      [&take](const UnitAssignment& value) {
        return value.coverageKey == take.coverageKey &&
               value.pitchLayer == take.pitchLayer && value.style == take.style;
      });
  if (assignment == project.unitAssignments.end()) {
    return core::failure<AssetRecord>(core::ErrorCode::InvalidArgument,
                                      "Raw take is not bound to required inventory");
  }
  auto superseded = project.takes.end();
  if (!take.supersedesTakeId.empty()) {
    superseded = std::find_if(
        project.takes.begin(), project.takes.end(),
        [&take](const TakeRecord& value) {
          return value.takeId == take.supersedesTakeId;
        });
    if (superseded == project.takes.end() ||
        superseded->coverageKey != take.coverageKey ||
        superseded->pitchLayer != take.pitchLayer ||
        superseded->style != take.style ||
        superseded->promptId != take.promptId || event.action != "retake") {
      return core::failure<AssetRecord>(
          core::ErrorCode::InvalidArgument,
          "Retake must supersede a matching take with a retake journal event");
    }
  } else if (!assignment->takeId.empty()) {
    return core::failure<AssetRecord>(
        core::ErrorCode::Conflict,
        "Occupied inventory assignment requires an explicit retake chain");
  }
  if (take.review.has_value() &&
      (take.review->takeId != take.takeId || take.review->reviewId.empty() ||
       take.review->reviewerId.empty() || take.review->reviewedAtUtc.empty() ||
       (take.review->result != "PASS" && take.review->result != "REJECTED") ||
       std::any_of(project.reviews.begin(), project.reviews.end(),
                   [&take](const ReviewRecord& value) {
                     return value.reviewId == take.review->reviewId;
                   }))) {
    return core::failure<AssetRecord>(
        core::ErrorCode::InvalidArgument,
        "Raw take review is invalid or duplicated");
  }
  if (lineage && std::any_of(project.metadataRevisions.begin(), project.metadataRevisions.end(),
      [&](const auto& value) { return value.revisionId == lineage->revisionId; })) return core::failure<AssetRecord>(
          core::ErrorCode::Conflict, "Procedural lineage revision already exists");
  const auto evidence = snapshotSourceEvidence(root_, sourceStrategy);
  if (!evidence) return core::Result<AssetRecord>{evidence.error()};
  auto imported = assetStore_.importFile(source, AssetKind::Raw);
  if (!imported) return imported;
  if (!expectedDigest.empty() && imported.value().sha256 != expectedDigest) return core::failure<AssetRecord>(
      core::ErrorCode::Conflict, "Imported candidate audio changed after verification");
  if (stopToken.stop_requested()) return core::failure<AssetRecord>(
      core::ErrorCode::Conflict, "Candidate import cancelled before commit");
  if (project.schemaVersion == 1) project.schemaVersion = kProductionProjectSchemaVersion;
  project.lifecycle = ProductionLifecycle::Experimental;
  if (std::none_of(project.assets.begin(), project.assets.end(),
                   [&imported](const AssetRecord& value) {
                     return value.sha256 == imported.value().sha256;
                   })) {
    project.assets.push_back(imported.value());
  }
  if (superseded != project.takes.end()) superseded->state = UnitQueueState::Retake;
  const auto bindingId = "source-" + core::sha256Hex(formats::stringifyJson(formats::JsonValue{formats::JsonValue::Object{
      {"format", "take-source-binding-v1"}, {"projectId", project.projectId}, {"takeId", take.takeId},
      {"audioSha256", imported.value().sha256}, {"strategyId", sourceStrategy.id}, {"licenseSha256", sourceStrategy.licenseSha256},
      {"importerId", event.operatorId}, {"importedAtUtc", event.occurredAtUtc}}}, false));
  project.sourceBindings.push_back({bindingId, take.takeId, imported.value().sha256, sourceStrategy,
      event.operatorId, event.occurredAtUtc, evidence.value()});
  project.takes.push_back(TakeRecord{
      .takeId = take.takeId,
      .promptId = take.promptId,
      .coverageKey = take.coverageKey,
      .pitchLayer = take.pitchLayer,
      .rawAssetSha256 = imported.value().sha256,
      .supersedesTakeId = take.supersedesTakeId,
      .state = take.initialState,
      .sourceBindingId = bindingId,
      .style = take.style,
  });
  if (take.review.has_value()) {
    project.reviews.push_back(*take.review);
  }
  assignment = std::find_if(
      project.unitAssignments.begin(), project.unitAssignments.end(),
      [&take](const UnitAssignment& value) {
        return value.coverageKey == take.coverageKey &&
               value.pitchLayer == take.pitchLayer && value.style == take.style;
      });
  assignment->takeId = take.takeId;
  assignment->state = take.initialState;
  assignment->markerReviewed = false;
  assignment->pitchReviewed = false;
  if (lineage) {
    project.metadataRevisions.push_back(*lineage);
    assignment->markerReviewed = false;
    assignment->pitchReviewed = false;
  }
  return imported;
}

}
