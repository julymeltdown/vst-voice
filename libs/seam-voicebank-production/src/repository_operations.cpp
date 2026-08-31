#include "seam/voicebank_production/repository.hpp"

#include "seam/core/sha256.hpp"
#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <system_error>

namespace seam::voicebank_production {
namespace {

bool safeStagingId(const std::string& value) {
  return !value.empty() && value.size() <= 128U &&
         std::all_of(value.begin(), value.end(), [](unsigned char item) {
           return std::isalnum(item) != 0 || item == '-' || item == '_' || item == '.';
         }) && value != "." && value != "..";
}

}

core::Result<StagedOperation> ProductionProjectRepository::stageOperation(
    const AssetRecord& input, const OperationRequest& request,
    std::string stagingId) const {
  if (!safeStagingId(stagingId)) {
    return core::failure<StagedOperation>(core::ErrorCode::InvalidArgument,
                                          "Staging identifier is unsafe");
  }
  auto verified = assetStore_.verify(input);
  if (!verified) return core::Result<StagedOperation>{verified.error()};
  auto audio = voicebank::readWav(assetStore_.pathFor(input));
  if (!audio) return core::Result<StagedOperation>{audio.error()};
  auto processed = applyOperation(audio.value(), request);
  if (!processed) return core::Result<StagedOperation>{processed.error()};
  const auto path = root_ / "staging" / (stagingId + ".wav");
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) {
    return core::failure<StagedOperation>(
        core::ErrorCode::IoError, "Unable to create staging directory",
        error.message());
  }
  if (std::filesystem::exists(path, error)) {
    return core::failure<StagedOperation>(
        core::ErrorCode::Conflict, "Staged output already exists", path.string());
  }
  auto written = voicebank::writeWav(
      path,
      {.sampleRate = processed.value().sampleRate,
       .channels = processed.value().channels,
       .sampleFormat = voicebank::WavSampleFormat::Pcm24},
      processed.value().interleaved);
  if (!written) return core::Result<StagedOperation>{written.error()};
  auto digest = core::sha256File(path);
  if (!digest) return core::Result<StagedOperation>{digest.error()};
  return StagedOperation{
      .stagingId = std::move(stagingId),
      .inputSha256 = input.sha256,
      .outputSha256 = digest.value(),
      .path = path,
      .request = request,
  };
}

core::Result<DerivedRevision> ProductionProjectRepository::commitStaged(
    VoicebankProductionProject& project, const StagedOperation& staged,
    std::string revisionId, std::string operatorId,
    std::string performedAtUtc) {
  if (revisionId.empty() || operatorId.empty() || performedAtUtc.empty()) {
    return core::failure<DerivedRevision>(
        core::ErrorCode::InvalidArgument, "Derived revision identity is incomplete");
  }
  if (std::any_of(project.derivedRevisions.begin(), project.derivedRevisions.end(),
                  [&revisionId](const DerivedRevision& value) {
                    return value.revisionId == revisionId;
                  })) {
    return core::failure<DerivedRevision>(
        core::ErrorCode::Conflict, "Derived revision identifier already exists");
  }
  const auto expectedParent = (root_ / "staging").lexically_normal();
  if (staged.path.parent_path().lexically_normal() != expectedParent) {
    return core::failure<DerivedRevision>(
        core::ErrorCode::InvalidArgument, "Staged output is outside this workspace");
  }
  auto digest = core::sha256File(staged.path);
  if (!digest) return core::Result<DerivedRevision>{digest.error()};
  if (digest.value() != staged.outputSha256) {
    return core::failure<DerivedRevision>(
        core::ErrorCode::InvariantViolation, "Staged output digest changed");
  }
  const auto inputExists = std::any_of(
      project.assets.begin(), project.assets.end(),
      [&staged](const AssetRecord& value) {
        return value.sha256 == staged.inputSha256;
      });
  if (!inputExists) {
    return core::failure<DerivedRevision>(
        core::ErrorCode::NotFound, "Derived input asset is unavailable");
  }
  auto imported = assetStore_.importFile(staged.path, AssetKind::Derived);
  if (!imported) return core::Result<DerivedRevision>{imported.error()};
  if (imported.value().sha256 != staged.outputSha256) {
    return core::failure<DerivedRevision>(
        core::ErrorCode::InvariantViolation, "Derived asset digest changed during commit");
  }
  const auto original = project;
  if (std::none_of(project.assets.begin(), project.assets.end(),
                   [&imported](const AssetRecord& value) {
                     return value.sha256 == imported.value().sha256;
                   })) {
    project.assets.push_back(imported.value());
  }
  DerivedRevision revision{
      .revisionId = std::move(revisionId),
      .inputSha256 = staged.inputSha256,
      .outputSha256 = staged.outputSha256,
      .operation = staged.request.kind,
      .operationVersion = "seam-pcm-ops-1",
      .parameters = operationParameters(staged.request),
      .operatorId = std::move(operatorId),
      .performedAtUtc = std::move(performedAtUtc),
  };
  project.derivedRevisions.push_back(revision);
  auto take = std::find_if(
      project.takes.begin(), project.takes.end(),
      [&project, &staged](const TakeRecord& value) {
        if (value.rawAssetSha256 == staged.inputSha256) return true;
        return std::any_of(
            value.derivedRevisionIds.begin(), value.derivedRevisionIds.end(),
            [&project, &staged](const std::string& id) {
              const auto revision = std::find_if(
                  project.derivedRevisions.begin(), project.derivedRevisions.end(),
                  [&id](const DerivedRevision& item) { return item.revisionId == id; });
              return revision != project.derivedRevisions.end() &&
                     revision->outputSha256 == staged.inputSha256;
            });
      });
  if (take == project.takes.end()) {
    project = original;
    return core::failure<DerivedRevision>(
        core::ErrorCode::NotFound, "No take owns the derived input asset");
  }
  take->derivedRevisionIds.push_back(revision.revisionId);
  auto saved = save(
      project, {.action = "transform", .subjectId = revision.revisionId,
                .operatorId = revision.operatorId,
                .occurredAtUtc = revision.performedAtUtc});
  if (!saved) {
    project = original;
    return core::Result<DerivedRevision>{saved.error()};
  }
  return revision;
}

core::Result<void> ProductionProjectRepository::recordMetadataRevision(
    VoicebankProductionProject& project, MetadataRevision revision,
    const ProductionJournalEvent& event) {
  if (revision.revisionId.empty() || revision.takeId.empty() ||
      revision.kind.empty() || revision.values.empty() ||
      revision.operatorId.empty() || revision.performedAtUtc.empty() ||
      event.action != "marker" || event.subjectId != revision.revisionId) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Metadata revision or journal event is invalid");
  }
  const auto take = std::find_if(
      project.takes.begin(), project.takes.end(),
      [&revision](const TakeRecord& value) { return value.takeId == revision.takeId; });
  if (take == project.takes.end() ||
      take->rawAssetSha256 != revision.rawAssetSha256) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Metadata revision is not bound to its raw take");
  }
  if (std::any_of(project.metadataRevisions.begin(),
                  project.metadataRevisions.end(),
                  [&revision](const MetadataRevision& value) {
                    return value.revisionId == revision.revisionId;
                  })) {
    return core::failure(core::ErrorCode::Conflict,
                         "Metadata revision identifier already exists");
  }
  const auto original = project;
  project.metadataRevisions.push_back(std::move(revision));
  auto saved = save(project, event);
  if (!saved) project = original;
  return saved;
}

std::vector<std::filesystem::path>
ProductionProjectRepository::inspectStaged(
    const VoicebankProductionProject& project) const {
  std::vector<std::filesystem::path> outputs;
  std::set<std::string, std::less<>> committedDigests;
  for (const auto& revision : project.derivedRevisions) {
    committedDigests.insert(revision.outputSha256);
  }
  std::error_code error;
  for (std::filesystem::directory_iterator iterator{root_ / "staging", error}, end;
       !error && iterator != end; iterator.increment(error)) {
    if (!iterator->is_regular_file(error) || error ||
        iterator->path().extension() != ".wav") {
      continue;
    }
    const auto digest = core::sha256File(iterator->path());
    if (!digest || committedDigests.find(digest.value()) == committedDigests.end()) {
      outputs.push_back(iterator->path());
    }
  }
  std::sort(outputs.begin(), outputs.end());
  return outputs;
}

std::filesystem::path ProductionProjectRepository::assetPath(
    const AssetRecord& asset) const {
  return assetStore_.pathFor(asset);
}

}
