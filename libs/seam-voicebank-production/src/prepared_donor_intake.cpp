#include "seam/voicebank_production/prepared_donor_intake.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include <algorithm>

namespace seam::voicebank_production {
namespace {
namespace p = voicebank_production;
using J = formats::JsonValue;
constexpr std::uint64_t MiB = 1024ULL * 1024ULL;
bool utf8(std::string_view value) {
  std::uint32_t code = 0U, minimum = 0U;
  unsigned remaining = 0U;
  for (const char character : value) {
    const auto c = static_cast<unsigned char>(character);
    if (remaining != 0U) {
      if ((c & 0xc0U) != 0x80U) return false;
      code = (code << 6U) | (c & 0x3fU);
      if (--remaining == 0U && (code < minimum || code > 0x10ffffU || (code >= 0xd800U && code <= 0xdfffU))) return false;
    } else if (c < 0x80U) continue;
    else if (c >= 0xc2U && c <= 0xdfU) { code = c & 0x1fU; minimum = 0x80U; remaining = 1U; }
    else if (c >= 0xe0U && c <= 0xefU) { code = c & 0x0fU; minimum = 0x800U; remaining = 2U; }
    else if (c >= 0xf0U && c <= 0xf4U) { code = c & 0x07U; minimum = 0x10000U; remaining = 3U; }
    else return false;
  }
  return remaining == 0U;
}
std::string bytesText(const std::vector<std::byte>& bytes) {
  return std::string{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
}
core::Result<PreparedDonorIntake> capturePreparedDonorIntake(const std::filesystem::path& workspace, const p::VoicebankProductionProject& project,
    p::PreparedPhoneDonorSnapshot snapshot, std::string expectedPacketSha256,
    std::string expectedDonorId, std::string_view sourceId, std::string_view coverage,
    std::int32_t pitch, std::string_view style, std::stop_token stop) {
  if (stop.stop_requested()) return core::failure<PreparedDonorIntake>(core::ErrorCode::Conflict, "Prepared intake cancelled");
  auto validated = p::validatePreparedPhoneDonorSnapshot(std::move(snapshot), std::move(expectedPacketSha256),
      std::move(expectedDonorId), stop);
  if (!validated) return core::Result<PreparedDonorIntake>{validated.error()};
  const auto& held = validated.value();
  if (project.schemaVersion < p::kProductionStyleSchemaVersion || project.language != "en" || sourceId != project.selectedSourceStrategyId)
    return core::failure<PreparedDonorIntake>(core::ErrorCode::Conflict, "Prepared intake requires the exact English style-owned producer and selected source");
  const auto source = std::find_if(project.sourceStrategies.begin(), project.sourceStrategies.end(),
      [&](const auto& value) { return value.id == sourceId; });
  const auto authorization = p::requireSelectedSourceExecution(project);
  if (!authorization) return core::Result<PreparedDonorIntake>{authorization.error()};
  if (source == project.sourceStrategies.end() || source->kind != p::SourceStrategyKind::HumanRecording)
    return core::failure<PreparedDonorIntake>(core::ErrorCode::InvalidArgument, "Prepared intake requires a human-recording source declaration");
  const auto row = std::find_if(project.unitAssignments.begin(), project.unitAssignments.end(), [&](const auto& value) {
    return value.coverageKey == coverage && value.pitchLayer == pitch && value.style == style;
  });
  if (row == project.unitAssignments.end())
    return core::failure<PreparedDonorIntake>(core::ErrorCode::Conflict, "Prepared intake requires an explicit existing coverage/pitch/style assignment");
  const auto declaration = core::readFileBytesLimited(workspace / "source-evidence" / (source->licenseSha256 + ".txt"), 4U * MiB);
  if (!declaration) return core::Result<PreparedDonorIntake>{declaration.error()};
  if (core::sha256Hex(declaration.value()) != source->licenseSha256)
    return core::failure<PreparedDonorIntake>(core::ErrorCode::Conflict, "Reviewed source declaration differs from its retained identity");
  const auto proposal = formats::parseJson(held.evidence.proposalJson);
  const auto packet = formats::parseJson(bytesText(held.evidence.packet.bytes));
  if (!proposal || !packet) return core::failure<PreparedDonorIntake>(core::ErrorCode::ParseError, "Prepared intake metadata is malformed");
  // The existing worker helper computes only take/supersession identity; it
  // does not synthesize material or infer a phone, pitch, style or source.
  const auto identity = p::nextProceduralTakeIdentity(project, *row);
  p::RawTakeInput take{.takeId = identity.takeId, .promptId = row->promptId, .coverageKey = row->coverageKey,
      .pitchLayer = row->pitchLayer, .supersedesTakeId = identity.supersedesTakeId, .style = row->style};
  J display = J::Object{{"format", "com.project-seam.prepared-donor-intake"}, {"schemaVersion", std::int64_t{1}},
      {"status", "UNAPPROVED"}, {"qualification", "NOT_QUALIFIED"}, {"releaseEligible", false}, {"sourceRightsApproved", false},
      {"language", project.language}, {"projectId", project.projectId},
      {"projectSha256", core::sha256Hex(p::encodeProductionProject(project))}, {"sourceStrategyId", source->id},
      {"packetSha256", held.evidence.packet.sha256}, {"donorId", held.evidence.donorId},
      {"intakePlanSha256", held.evidence.plan.sha256},
      {"sourcePhones", *proposal.value().find("sourcePhones")}, {"unreviewedProposal", proposal.value()},
      {"assignment", J::Object{{"coverageKey", take.coverageKey}, {"pitchLayer", static_cast<std::int64_t>(take.pitchLayer)},
          {"style", take.style}, {"promptId", take.promptId}, {"takeId", take.takeId}, {"supersedesTakeId", take.supersedesTakeId}}},
      {"reviewedSourceDeclaration", J::Object{{"sha256", source->licenseSha256}, {"text", bytesText(declaration.value())},
          {"declaredRights", std::string{p::toString(source->rights)}}, {"sourceUse", source->permissions.sourceUse},
          {"transformation", source->permissions.transformation}, {"singingBankRedistribution", source->permissions.singingBankRedistribution},
          {"commercialRenders", source->permissions.commercialRenders}}},
      {"originalLicense", J::Object{{"sha256", held.evidence.license.sha256}, {"text", bytesText(held.evidence.license.bytes)},
          {"source", *packet.value().find("license")}}},
      {"attribution", J::Object{{"sha256", held.evidence.attribution.sha256}, {"text", bytesText(held.evidence.attribution.bytes)}}},
      {"audio", J::Object{{"sha256", held.audio.sha256}, {"pcmSha256", held.pcmSha256},
          {"sampleRate", static_cast<std::int64_t>(held.sampleRate)}, {"frames", static_cast<std::int64_t>(held.frames)},
          {"nonzeroPcm", held.nonzeroPcm}}}};
  const auto serialized = formats::stringifyJson(display, true) + "\n";
  if (serialized.size() > 64U * MiB || !utf8(serialized))
    return core::failure<PreparedDonorIntake>(core::ErrorCode::Unsupported, "Prepared intake display requires bounded UTF-8 notice and assignment text");
  return PreparedDonorIntake{std::move(display), std::move(take), std::move(validated.value())};
}
}
