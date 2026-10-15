#include "source_intake_commands.hpp"
#include "signal_cancellation.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/voicebank_production/prepared_phone_donor.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/repository.hpp"
#include "seam/formats/json_value.hpp"
#include <algorithm>
#include <charconv>
#include <iostream>
#include <limits>
#include <string_view>

namespace seam::voicebank_cli {
namespace {
namespace p = voicebank_production;
using J = formats::JsonValue;
constexpr std::uint64_t MiB = 1024ULL * 1024ULL;
bool digest(std::string_view value) {
  return value.size() == 64U && std::all_of(value.begin(), value.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}
// The JSON parser preserves raw string bytes. A displayed capture must also be
// valid UTF-8, so neither notices nor labels can become unreadable JSON.
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
const std::string* text(const J& object, std::string_view key) {
  const auto* value = object.find(key);
  return value && value->isString() ? &value->asString() : nullptr;
}
std::string bytesText(const std::vector<std::byte>& bytes) {
  return std::string{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
int fail(const core::Error& error, const SignalCancellation* cancellation = nullptr) {
  std::cerr << "Prepared donor refused: " << error.message;
  if (!error.context.empty()) std::cerr << " (" << error.context << ')';
  std::cerr << '\n';
  return cancellation && cancellation->signal() != 0 ? 128 + cancellation->signal() : 2;
}
int invalid(std::string message) { return fail({core::ErrorCode::InvalidArgument, std::move(message), {}}); }
int print(const J::Object& value) {
  std::cout << formats::stringifyJson(value, true) << '\n';
  return std::cout ? 0 : 2;
}
struct Intake final { J display; p::RawTakeInput take; };
core::Result<Intake> describe(const std::filesystem::path& workspace, const p::VoicebankProductionProject& project,
    const p::PreparedPhoneDonorSnapshot& held, std::string_view sourceId, std::string_view coverage,
    std::int32_t pitch, std::string_view style, std::stop_token stop) {
  if (stop.stop_requested()) return core::failure<Intake>(core::ErrorCode::Conflict, "Prepared intake cancelled");
  if (project.schemaVersion < p::kProductionStyleSchemaVersion || project.language != "en" || sourceId != project.selectedSourceStrategyId)
    return core::failure<Intake>(core::ErrorCode::Conflict, "Prepared intake requires the exact English style-owned producer and selected source");
  const auto source = std::find_if(project.sourceStrategies.begin(), project.sourceStrategies.end(),
      [&](const auto& value) { return value.id == sourceId; });
  const auto authorization = p::requireSelectedSourceExecution(project);
  if (!authorization) return core::Result<Intake>{authorization.error()};
  if (source == project.sourceStrategies.end() || source->kind != p::SourceStrategyKind::HumanRecording)
    return core::failure<Intake>(core::ErrorCode::InvalidArgument, "Prepared intake requires a human-recording source declaration");
  const auto row = std::find_if(project.unitAssignments.begin(), project.unitAssignments.end(), [&](const auto& value) {
    return value.coverageKey == coverage && value.pitchLayer == pitch && value.style == style;
  });
  if (row == project.unitAssignments.end())
    return core::failure<Intake>(core::ErrorCode::Conflict, "Prepared intake requires an explicit existing coverage/pitch/style assignment");
  const auto declaration = core::readFileBytesLimited(workspace / "source-evidence" / (source->licenseSha256 + ".txt"), 4U * MiB);
  if (!declaration) return core::Result<Intake>{declaration.error()};
  if (core::sha256Hex(declaration.value()) != source->licenseSha256)
    return core::failure<Intake>(core::ErrorCode::Conflict, "Reviewed source declaration differs from its retained identity");
  const auto proposal = formats::parseJson(held.evidence.proposalJson);
  const auto packet = formats::parseJson(bytesText(held.evidence.packet.bytes));
  if (!proposal || !packet) return core::failure<Intake>(core::ErrorCode::ParseError, "Prepared intake metadata is malformed");
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
    return core::failure<Intake>(core::ErrorCode::Unsupported, "Prepared intake display requires bounded UTF-8 notice and assignment text");
  return Intake{std::move(display), std::move(take)};
}
bool inside(const std::filesystem::path& path, const std::filesystem::path& root) {
  const auto relative = path.lexically_relative(root);
  return !relative.empty() && *relative.begin() != "..";
}
int prepare(int argc, char** argv) {
  if (argc != 11) { printSourceIntakeUsage(); return 2; }
  std::int32_t pitch = 0;
  const std::string_view number{argv[8]};
  const auto parsed = std::from_chars(number.data(), number.data() + number.size(), pitch);
  if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size() || !digest(argv[4]))
    return invalid("Prepared intake requires an integer pitch and the retained packet SHA-256");
  SignalCancellation cancellation;
  if (!cancellation.install()) return invalid("Cannot install prepared-intake cancellation handlers");
  p::ProductionProjectRepository repository{argv[2]};
  const auto project = repository.recover(); if (!project) return fail(project.error(), &cancellation);
  const auto held = p::loadPreparedPhoneDonor(argv[3], argv[4], argv[5], cancellation.token());
  if (!held) return fail(held.error(), &cancellation);
  const auto intake = describe(argv[2], project.value(), held.value(), argv[6], argv[7], pitch, argv[9], cancellation.token());
  if (!intake) return fail(intake.error(), &cancellation);
  std::error_code error;
  const auto output = std::filesystem::absolute(argv[10], error).lexically_normal();
  if (error || output.filename().empty()) return invalid("Intake output must name a new file in a real directory");
  const auto status = std::filesystem::symlink_status(output.parent_path(), error);
  if (error || !std::filesystem::is_directory(status) || std::filesystem::is_symlink(status)) return invalid("Intake output parent must be a real directory");
  const auto parent = std::filesystem::canonical(output.parent_path(), error);
  if (error) return invalid("Cannot resolve intake output directory");
  const auto workspace = std::filesystem::canonical(argv[2], error);
  if (error) return invalid("Cannot resolve producer directory");
  const auto preparation = std::filesystem::canonical(argv[3], error);
  if (error || inside(parent, workspace) || inside(parent, preparation))
    return invalid("Intake output must be outside producer and preparation directories");
  const auto target = parent / output.filename();
  const auto raw = formats::stringifyJson(intake.value().display, true) + "\n";
  if (cancellation.token().stop_requested()) return invalid("Prepared intake cancelled before capture");
  const auto written = core::durableAtomicWriteTextNew(target, raw);
  if (!written) return fail(written.error(), &cancellation);
  return print({{"result", "PreparedDonorIntakeCaptured"}, {"requestFile", target.generic_string()},
      {"requestSha256", core::sha256Hex(raw)}, {"request", intake.value().display}, {"nativeImport", false}, {"releaseEligible", false}});
}
int importPrepared(int argc, char** argv) {
  if (argc != 8) { printSourceIntakeUsage(); return 2; }
  if (!digest(argv[5])) return invalid("Prepared intake requires the retained request SHA-256");
  SignalCancellation cancellation;
  if (!cancellation.install()) return invalid("Cannot install prepared-import cancellation handlers");
  const auto bytes = core::readFileBytesLimited(argv[4], 64U * MiB);
  if (!bytes) return fail(bytes.error(), &cancellation);
  const auto raw = bytesText(bytes.value());
  if (core::sha256Hex(bytes.value()) != argv[5] || !utf8(raw)) return invalid("Prepared intake request bytes differ or are not UTF-8");
  const auto request = formats::parseJson(raw);
  if (!request) return fail(request.error(), &cancellation);
  const auto* projectHash = text(request.value(), "projectSha256");
  const auto* source = text(request.value(), "sourceStrategyId");
  const auto* packetHash = text(request.value(), "packetSha256");
  const auto* donor = text(request.value(), "donorId");
  const auto* assignment = request.value().find("assignment");
  const auto* coverage = assignment ? text(*assignment, "coverageKey") : nullptr;
  const auto* style = assignment ? text(*assignment, "style") : nullptr;
  const auto* pitch = assignment ? assignment->find("pitchLayer") : nullptr;
  if (!projectHash || !source || !packetHash || !donor || !coverage || !style || !pitch || !pitch->isInteger() ||
      pitch->asInt64() < std::numeric_limits<std::int32_t>::min() || pitch->asInt64() > std::numeric_limits<std::int32_t>::max())
    return invalid("Prepared intake request identity or assignment is malformed");
  p::ProductionProjectRepository repository{argv[2]};
  auto project = repository.recover(); if (!project) return fail(project.error(), &cancellation);
  if (*projectHash != core::sha256Hex(p::encodeProductionProject(project.value()))) return invalid("Prepared intake producer snapshot is stale");
  auto held = p::loadPreparedPhoneDonor(argv[3], *packetHash, *donor, cancellation.token());
  if (!held) return fail(held.error(), &cancellation);
  const auto intake = describe(argv[2], project.value(), held.value(), *source, *coverage,
      static_cast<std::int32_t>(pitch->asInt64()), *style, cancellation.token());
  if (!intake) return fail(intake.error(), &cancellation);
  if (intake.value().display != request.value()) return invalid("Prepared intake differs from its displayed notices, donor or assignment");
  const auto& take = intake.value().take;
  const auto receipt = repository.importPreparedDonor(project.value(), std::move(held.value()),
      {*projectHash, *packetHash, *donor, *source}, take,
      {take.supersedesTakeId.empty() ? "import" : "retake", take.takeId, argv[6], argv[7]}, cancellation.token());
  if (!receipt) return fail(receipt.error(), &cancellation);
  return print({{"result", "PreparedDonorImported"}, {"takeId", take.takeId}, {"audioSha256", receipt.value().sha256},
      {"generation", std::to_string(receipt.value().committedGeneration)}, {"projectSha256", receipt.value().committedProjectSha256},
      {"durabilityConfirmed", receipt.value().durabilityConfirmed}, {"diagnostic", receipt.value().diagnostic},
      {"queueState", std::string{p::toString(project.value().takes.back().state)}}, {"bankAdmission", "NOT_APPROVED"},
      {"sourceRightsApproved", false}, {"releaseEligible", false}, {"nativeImport", true}});
}
int verify(int argc, char** argv) {
  if (argc != 5) { printSourceIntakeUsage(); return 2; }
  SignalCancellation cancellation;
  if (!cancellation.install()) return invalid("Cannot install donor verification cancellation handlers");
  const auto snapshot = voicebank_production::loadPreparedPhoneDonor(argv[2], argv[3], argv[4], cancellation.token());
  if (!snapshot) {
    std::cerr << "Prepared donor refused: " << snapshot.error().message << "\n";
    return 2;
  }
  const auto& value = snapshot.value();
  const auto& evidence = value.evidence;
  std::cout << formats::stringifyJson(formats::JsonValue::Object{
      {"documentKind", "SelectedPreparedPhoneDonorSnapshot"}, {"status", "UNAPPROVED"},
      {"donorId", evidence.donorId}, {"packetSha256", evidence.packet.sha256},
      {"audioSha256", value.audio.sha256}, {"pcmSha256", value.pcmSha256},
      {"attributionSha256", evidence.attribution.sha256}, {"licenseSha256", evidence.license.sha256},
      {"planSha256", evidence.plan.sha256}, {"sampleRate", static_cast<std::int64_t>(value.sampleRate)},
      {"frames", static_cast<std::int64_t>(value.frames)}, {"nonzeroPcm", value.nonzeroPcm},
      {"bankAdmission", "NOT_APPROVED"}, {"wholePacketAudioVerified", false},
      {"sourceRightsApproved", false}, {"nativeImport", false}}) << '\n';
  return std::cout ? 0 : 2;
}
}
std::optional<int> runSourceIntakeCommand(int argc, char** argv) {
  if (argc < 2) return std::nullopt;
  const std::string_view command{argv[1]};
  if (command == "verify-phone-donor") return verify(argc, argv);
  if (command == "prepare-donor-intake") return prepare(argc, argv);
  if (command == "import-prepared-donor") return importPrepared(argc, argv);
  return std::nullopt;
}
void printSourceIntakeUsage() {
  std::cerr << "  seam_voicebank_cli verify-phone-donor DIRECTORY PACKET_SHA256 DONOR_ID\n"
      << "  seam_voicebank_cli prepare-donor-intake WORKSPACE DIRECTORY PACKET_SHA256 DONOR_ID SOURCE_ID COVERAGE MIDI STYLE NEW_REQUEST_JSON\n"
      << "    Displays exact notices and source phones beside the assignment; creates an unapproved request without importing.\n"
      << "  seam_voicebank_cli import-prepared-donor WORKSPACE DIRECTORY REQUEST_JSON REQUEST_SHA256 PRODUCER UTC\n"
      << "    Imports only against the unchanged displayed request; no phonetic, listening, rights or bank approval.\n";
}
}
