#include "seam/voicebank_production/repository.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"

#include <algorithm>

namespace seam::voicebank_production {
namespace {
bool isDigest(std::string_view value) {
  return value.size() == 64U && std::all_of(value.begin(), value.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}
// The migration the plan's own language and style imply, applied to the same legacy bytes. It is
// computed here rather than trusted from the document, so the plan and this implementation have to
// agree before anything is written.
VoicebankProductionProject migratedProject(const VoicebankProductionProject& project,
                                           const std::string& language, const std::string& style) {
  auto draft = project;
  draft.schemaVersion = kProductionStyleSchemaVersion;
  draft.language = language;
  draft.lifecycle = project.takes.empty() ? ProductionLifecycle::Draft : ProductionLifecycle::Experimental;
  for (auto& take : draft.takes) take.style = style;
  for (auto& assignment : draft.unitAssignments) {
    // The review basis changes with the identity, so an approval under the legacy style-free
    // identity cannot survive it. The historical review records stay exactly as they are.
    const bool approved = assignment.state == UnitQueueState::Approved ||
        assignment.state == UnitQueueState::PitchReview;
    assignment.style = style;
    assignment.markerReviewed = false;
    assignment.pitchReviewed = false;
    if (!approved) continue;
    assignment.state = UnitQueueState::MarkerReview;
    for (auto& take : draft.takes)
      if (take.takeId == assignment.takeId) take.state = UnitQueueState::MarkerReview;
  }
  return draft;
}
}  // namespace

core::Result<ProductionCommitReceipt> ProductionProjectRepository::applyStyleMigration(
    VoicebankProductionProject& project, const std::filesystem::path& planPath,
    std::string_view expectedProjectSha256, std::string producerId,
    std::string occurredAtUtc, std::stop_token stop) {
  using Output = ProductionCommitReceipt;
  if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Style migration cancelled");
  const auto verified = verify(project);
  if (!verified) return core::Result<Output>{verified.error()};
  if (project.schemaVersion >= kProductionStyleSchemaVersion)
    return core::failure<Output>(core::ErrorCode::Conflict,
        "Style migration requires the exact legacy producer snapshot it was planned from");
  // The plan binds the durable document's bytes, which is what both implementations read, so the
  // same document has to be what this workspace still holds.
  const auto document = core::readFileBytesLimited(root_ / "project.json", 64ULL * 1024ULL * 1024ULL);
  if (!document || core::sha256Hex(document.value()) != expectedProjectSha256)
    return core::failure<Output>(core::ErrorCode::Conflict,
        "Style migration requires the exact legacy producer snapshot it was planned from");
  if (!isProductionUtcTimestamp(occurredAtUtc) ||
      std::none_of(project.operators.begin(), project.operators.end(), [&](const auto& actor) {
        return actor.operatorId == producerId && actor.role == "PRODUCER";
      }))
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Style migration requires a registered producer and a UTC timestamp");
  const auto planBytes = core::readFileBytesLimited(planPath, 4ULL * 1024ULL * 1024ULL);
  if (!planBytes || planBytes.value().empty())
    return core::failure<Output>(core::ErrorCode::Conflict, "Style migration plan is missing or oversized");
  const std::string planText{reinterpret_cast<const char*>(planBytes.value().data()), planBytes.value().size()};
  const auto parsed = formats::parseJson(planText, {.maximumInputBytes = 4U * 1024U * 1024U,
      .maximumDepth = 12U, .maximumNodes = 1U << 20, .maximumStringBytes = 1U << 20,
      .maximumCollectionEntries = 1U << 20});
  if (!parsed) return core::Result<Output>{parsed.error()};
  const auto& plan = parsed.value();
  const auto stringField = [&](const char* name) {
    const auto* value = plan.find(name);
    return value && value->isString() ? value->asString() : std::string{};
  };
  if (!plan.isObject() || stringField("format") != "com.project-seam.production-style-migration-plan" ||
      !plan.find("schemaVersion") || !plan.find("schemaVersion")->isInteger() ||
      plan.find("schemaVersion")->asInt64() != 1)
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Style migration plan format is unsupported");
  if (stringField("sourceProjectSha256") != expectedProjectSha256 ||
      (!stringField("sourceProjectId").empty() && stringField("sourceProjectId") != project.projectId))
    return core::failure<Output>(core::ErrorCode::Conflict,
        "Style migration plan was prepared from a different producer snapshot");
  if (!isDigest(stringField("inventorySha256")))
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Style migration plan lacks a verified inventory digest");
  const auto status = stringField("status");
  if (status == "UNRESOLVED")
    return core::failure<Output>(core::ErrorCode::Unsupported,
        "Legacy ownership without a unique style binding requires explicit per-assignment evidence: " +
            stringField("reason"));
  if (status != "RESOLVED_NOT_APPLIED")
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Style migration plan is not an unapplied resolution");
  if (!plan.find("proposedProject") || !plan.find("proposedProject")->isObject())
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Style migration plan has no proposed project");
  const auto proposed = decodeProductionProject(formats::stringifyJson(*plan.find("proposedProject")));
  if (!proposed) return core::Result<Output>{proposed.error()};
  if (proposed.value().projectId != project.projectId || proposed.value().language.empty())
    return core::failure<Output>(core::ErrorCode::Conflict,
        "Style migration plan targets a different producer or declares no language");
  // One style, declared by the verified inventory, is the only ownership this operation applies.
  std::string style;
  for (const auto& take : proposed.value().takes)
    if (!take.style.empty()) { style = take.style; break; }
  if (style.empty())
    for (const auto& assignment : proposed.value().unitAssignments)
      if (!assignment.style.empty()) { style = assignment.style; break; }
  const auto oneStyle = [&](const auto& rows) {
    return std::all_of(rows.begin(), rows.end(),
        [&](const auto& row) { return row.style.empty() || row.style == style; });
  };
  if (style.empty() || !oneStyle(proposed.value().takes) || !oneStyle(proposed.value().unitAssignments))
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Style migration plan does not declare exactly one style for the legacy producer");
  auto draft = migratedProject(project, proposed.value().language, style);
  auto comparable = proposed.value();
  comparable.lastDurableGeneration = draft.lastDurableGeneration;
  if (encodeProductionProject(comparable) != encodeProductionProject(draft))
    return core::failure<Output>(core::ErrorCode::Conflict,
        "Plan's proposed migration differs from this operation's own result");
  const auto valid = validateProductionProject(draft);
  if (!valid) return core::Result<Output>{valid.error()};
  // Retain the exact plan before any generation depends on it, the way a source declaration is
  // retained before imported material depends on it.
  const auto directory = root_ / "migrations";
  std::error_code error;
  auto directoryStatus = std::filesystem::symlink_status(directory, error);
  // A workspace written by another implementation of this format may not carry the directory yet,
  // so it is created here and then still has to be a real directory rather than a link.
  if (!std::filesystem::exists(directoryStatus)) {
    std::filesystem::create_directory(directory, error);
    directoryStatus = std::filesystem::symlink_status(directory, error);
  }
  if (error || !std::filesystem::is_directory(directoryStatus) || std::filesystem::is_symlink(directoryStatus))
    return core::failure<Output>(core::ErrorCode::Conflict, "Style migration directory is unsafe");
  if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict,
      "Style migration cancelled before retention");
  const auto planSha256 = core::sha256Hex(planBytes.value());
  const auto retained = directory / (planSha256 + ".json");
  const auto written = core::durableAtomicWriteNew(retained, planBytes.value());
  if (!written) {
    const auto existing = core::readFileBytesLimited(retained, 4ULL * 1024ULL * 1024ULL);
    if (!existing || existing.value() != planBytes.value()) return core::Result<Output>{written.error()};
  }
  const auto saved = writeGeneration(draft,
      {"style-migration", planSha256, std::move(producerId), std::move(occurredAtUtc)}, stop, true);
  bool durable = true;
  std::string diagnostic;
  if (!saved) {
    const auto recovered = recover();
    if (!recovered || recovered.value().lastDurableGeneration <= project.lastDurableGeneration)
      return core::Result<Output>{saved.error()};
    auto recoveredComparable = recovered.value();
    recoveredComparable.lastDurableGeneration = draft.lastDurableGeneration;
    if (encodeProductionProject(recoveredComparable) != encodeProductionProject(draft))
      return core::failure<Output>(core::ErrorCode::Conflict,
          "Style migration was not confirmed; recovery found different work");
    draft = recovered.value();
    durable = false;
    diagnostic = "Exact style migration is recoverably committed; inspect before retrying. " + saved.error().message;
  }
  project = std::move(draft);
  return Output{project.lastDurableGeneration, core::sha256Hex(encodeProductionProject(project)), durable,
      std::move(diagnostic)};
}
}  // namespace seam::voicebank_production
