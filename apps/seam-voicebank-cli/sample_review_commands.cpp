#include "sample_review_commands.hpp"
#include "signal_cancellation.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank_production/candidate_publication.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/repository.hpp"
#include "seam/voicebank_production/manifest_draft.hpp"
#include "seam/voicebank_production/source_assessment.hpp"

#include <algorithm>
#include <charconv>
#include <iostream>
#include <limits>

namespace seam::voicebank_cli {
namespace {
namespace production = voicebank_production;
using Json = formats::JsonValue;
constexpr std::uint64_t kMaximumPacketBytes = 64U * 1024U * 1024U;

bool digestValid(std::string_view value) {
  return value.size()==64U && std::all_of(value.begin(), value.end(), [](char c) {
    return (c>='0' && c<='9') || (c>='a' && c<='f');
  });
}
int fail(const core::Error& error, const SignalCancellation* cancellation = nullptr) {
  std::cerr << "error: " << error.message;
  if (!error.context.empty()) std::cerr << " (" << error.context << ')';
  std::cerr << '\n';
  return cancellation && cancellation->signal()!=0 ? 128+cancellation->signal() : 2;
}
void print(const Json::Object& object) { std::cout << formats::stringifyJson(Json{object}, true) << '\n'; }

core::Result<std::string> readCapturedText(
    const std::filesystem::path& path, std::string_view expectedHash) {
  using Output = std::string;
  if (!digestValid(expectedHash)) return core::failure<Output>(core::ErrorCode::InvalidArgument,
      "Captured input requires the retained lowercase SHA-256 of its file bytes");
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Captured input must be a regular non-symlink file");
  const auto text = core::readTextFileLimited(path, kMaximumPacketBytes);
  if (!text) return core::Result<Output>{text.error()};
  if (core::sha256Hex(text.value())!=expectedHash) return core::failure<Output>(core::ErrorCode::Conflict,
      "Captured input bytes differ from their retained digest");
  return text.value();
}
core::Result<production::SampleCandidateReviewPacket> readPacket(
    const std::filesystem::path& path, std::string_view expectedHash) {
  const auto text=readCapturedText(path,expectedHash);
  if (!text) return core::Result<production::SampleCandidateReviewPacket>{text.error()};
  return production::decodeSampleCandidateReviewPacket(text.value());
}

int registerSource(int argc, char** argv) {
  if (argc != 15) { printSampleReviewUsage(); return 1; }
  const std::string_view kind{argv[5]}, rights{argv[6]};
  if ((kind != "human" && kind != "procedural" && kind != "tts") ||
      (rights != "pass" && rights != "blocked" && rights != "not-assessed") ||
      !digestValid(argv[3]) || !digestValid(argv[12]))
    return fail({core::ErrorCode::InvalidArgument,"Explicit source kind, rights declaration and captured digests are required",{}});
  for (int index = 7; index <= 10; ++index)
    if (std::string_view{argv[index]} != "yes" && std::string_view{argv[index]} != "no")
      return fail({core::ErrorCode::InvalidArgument,"Every applicable permission requires an explicit yes or no",{}});
  SignalCancellation cancellation;
  if (!cancellation.install()) return fail({core::ErrorCode::IoError,"Cannot install registration cancellation handlers",{}});
  production::ProductionProjectRepository repository{argv[2]};
  auto project = repository.recover(); if (!project) return fail(project.error(),&cancellation);
  std::error_code error;
  const auto evidence = std::filesystem::absolute(argv[11],error).lexically_normal();
  if (error) return fail({core::ErrorCode::InvalidArgument,"Cannot resolve source evidence path",error.message()});
  production::SourceStrategyAssessment source{
      .id = argv[4], .kind = kind == "human" ? production::SourceStrategyKind::HumanRecording :
          kind == "procedural" ? production::SourceStrategyKind::ProceduralSynthesis : production::SourceStrategyKind::TtsDerived,
      .rights = rights == "pass" ? production::Feasibility::Pass : rights == "blocked" ? production::Feasibility::Blocked : production::Feasibility::NotAssessed,
      .permissions = {std::string_view{argv[7]} == "yes",std::string_view{argv[8]} == "yes",
          std::string_view{argv[9]} == "yes",std::string_view{argv[10]} == "yes"},
      .licenseLocator = evidence.string(), .licenseSha256 = argv[12]};
  const auto receipt = repository.registerSource(project.value(),source,argv[3],argv[13],argv[14],cancellation.token());
  if (!receipt) return fail(receipt.error(),&cancellation);
  print({{"result","SourceRegistered"},{"strategyId",source.id},{"generation",std::to_string(receipt.value().committedGeneration)},
      {"projectSha256",receipt.value().committedProjectSha256},{"durabilityConfirmed",receipt.value().durabilityConfirmed},
      {"diagnostic",receipt.value().diagnostic},{"coverage","NOT_ASSESSED"},{"listening","NOT_ASSESSED"},
      {"rights","producer declaration, not legal verification"},{"unitApproval","unchanged"},{"releaseEligible",false}});
  return 0;
}

int initializeDraft(int argc, char** argv) {
  if (argc!=7) { printSampleReviewUsage(); return 1; }
  const auto definition=readCapturedText(argv[3],argv[4]); if (!definition) return fail(definition.error());
  auto project=production::decodeProductionProject(definition.value()); if (!project) return fail(project.error());
  const auto& state=project.value();
  if ((state.schemaVersion!=production::kProductionProjectSchemaVersion && state.schemaVersion!=production::kProductionStyleSchemaVersion) || state.lastDurableGeneration!=0U ||
      state.lifecycle!=production::ProductionLifecycle::Draft || !state.assets.empty() || !state.takes.empty() ||
      !state.derivedRevisions.empty() || !state.metadataRevisions.empty() || !state.reviews.empty() || !state.sourceBindings.empty() || !state.sourceQualityAssessments.empty() ||
      std::any_of(state.unitAssignments.begin(),state.unitAssignments.end(),[](const auto& assignment) {
        return assignment.state!=production::UnitQueueState::Missing || !assignment.takeId.empty() || assignment.markerReviewed || assignment.pitchReviewed;
      })) return fail({core::ErrorCode::InvalidArgument,"Initialization requires an empty current-schema Draft, not imported data or preapproved material",{}});
  const auto actor=std::find_if(state.operators.begin(),state.operators.end(),[&](const auto& value) { return value.operatorId==argv[5]; });
  if (actor==state.operators.end() || actor->role!="PRODUCER")
    return fail({core::ErrorCode::InvalidArgument,"Draft initialization requires an explicitly registered producer",{}});
  production::ProductionProjectRepository repository{argv[2]};
  const auto initialized=repository.initialize(project.value(),{.action="create",.subjectId=state.projectId,.operatorId=argv[5],.occurredAtUtc=argv[6]});
  if (!initialized) return fail(initialized.error());
  print({{"result","DraftCreated"},{"projectId",project.value().projectId},
      {"lifecycle",production::toString(project.value().lifecycle)},{"generation",std::to_string(project.value().lastDurableGeneration)},
      {"projectSha256",core::sha256Hex(production::encodeProductionProject(project.value()))},{"releaseEligible",false}});
  return 0;
}

int createDraft(int argc, char** argv) {
  if (argc != 9) { printSampleReviewUsage(); return 1; }
  const std::string_view language{argv[6]};
  const auto selectedLanguage = language == "ja" ? domain::Language::Japanese : language == "en" ? domain::Language::English :
      language == "ko" ? domain::Language::Korean : domain::Language::Unspecified;
  if (selectedLanguage == domain::Language::Unspecified)
    return fail({core::ErrorCode::InvalidArgument, "Choose an explicit draft language: ja, en or ko", {}});
  SignalCancellation cancellation;
  if (!cancellation.install()) return fail({core::ErrorCode::IoError, "Cannot install draft cancellation handlers", {}});
  production::ProductionProjectRepository repository{argv[2]};
  const auto project = repository.recover(); if (!project) return fail(project.error(), &cancellation);
  std::error_code error;
  const auto destination = std::filesystem::absolute(argv[8], error).lexically_normal();
  if (error) return fail({core::ErrorCode::InvalidArgument, "Cannot resolve draft destination", error.message()}, &cancellation);
  const auto created = production::createSampleManifestDraft(argv[2], project.value(),
      {argv[3], argv[4], argv[5], selectedLanguage, argv[7]}, destination, {}, cancellation.token());
  if (!created) return fail(created.error(), &cancellation);
  Json::Array diagnostics, missing;
  for (const auto& item : created.value().diagnostics) diagnostics.emplace_back(item);
  for (const auto& item : created.value().missingAssignments) missing.emplace_back(item);
  print({{"result", "EditableDraftCommitted"}, {"root", created.value().root.generic_string()},
      {"manifestSha256", created.value().manifestSha256}, {"draftSha256", created.value().draftSha256},
      {"sourceGeneration", std::to_string(created.value().sourceGeneration)}, {"sourceProjectSha256", created.value().sourceProjectSha256},
      {"durabilityConfirmed", created.value().durabilityConfirmed}, {"missingAssignments", std::move(missing)},
      {"diagnostics", std::move(diagnostics)}, {"approval", "unchanged"}, {"releaseEligible", false}});
  return 0; // A committed draft receipt wins over a late cancellation.
}

int prepare(int argc, char** argv) {
  if (argc!=5) { printSampleReviewUsage(); return 1; }
  SignalCancellation cancellation;
  if (!cancellation.install()) return fail({core::ErrorCode::IoError,"Cannot install review cancellation handlers",{}});
  production::ProductionProjectRepository repository{argv[2]};
  const auto project=repository.recover(); if (!project) return fail(project.error(), &cancellation);
  const auto manifest=voicebank::ManifestJsonCodec{}.load(argv[3]); if (!manifest) return fail(manifest.error(), &cancellation);
  const auto packet=production::prepareSampleCandidateReview(argv[2], project.value(), manifest.value(), cancellation.token());
  if (!packet) return fail(packet.error(), &cancellation);
  const auto encoded=production::encodeSampleCandidateReviewPacket(packet.value()); if (!encoded) return fail(encoded.error(), &cancellation);
  if (cancellation.token().stop_requested()) return fail({core::ErrorCode::Conflict,"Review capture cancelled",{}}, &cancellation);
  const auto written=core::durableAtomicWriteTextNew(argv[4], encoded.value());
  if (!written) {
    std::cerr << "Packet capture never approves takes. If the new file exists after an I/O failure, inspect its digest before retrying.\n";
    return fail(written.error(), &cancellation);
  }
  print({{"result","Captured"}, {"packet",argv[4]}, {"fileSha256",core::sha256Hex(encoded.value())},
      {"packetSha256",packet.value().packetSha256}, {"generation",std::to_string(packet.value().sourceGeneration)},
      {"projectSha256",packet.value().sourceProjectSha256}, {"units",static_cast<std::int64_t>(packet.value().units.size())},
      {"approval","unchanged"}, {"releaseEligible",false}});
  return 0;
}

int inspect(int argc, char** argv) {
  if (argc!=4) { printSampleReviewUsage(); return 1; }
  const auto packet=readPacket(argv[2],argv[3]); if (!packet) return fail(packet.error());
  const auto encoded=production::encodeSampleCandidateReviewPacket(packet.value()); if (!encoded) return fail(encoded.error());
  std::cout << encoded.value() << '\n';
  return 0;
}

int review(int argc, char** argv) {
  if (argc<8 || argc>4104) { printSampleReviewUsage(); return 1; }
  const std::string_view decision{argv[7]};
  if (decision!="accept" && decision!="reject") return fail({core::ErrorCode::InvalidArgument,
      "An explicit accept or reject decision is required",{}});
  SignalCancellation cancellation;
  if (!cancellation.install()) return fail({core::ErrorCode::IoError,"Cannot install review cancellation handlers",{}});
  const auto packet=readPacket(argv[3],argv[4]); if (!packet) return fail(packet.error(), &cancellation);
  production::ProductionProjectRepository repository{argv[2]};
  auto project=repository.recover(); if (!project) return fail(project.error(), &cancellation);
  std::vector<std::string> units;
  for (int i=8; i<argc; ++i) units.emplace_back(argv[i]);
  const auto receipt=production::commitSampleCandidateReview(argv[2], project.value(), packet.value(), argv[5],argv[6],
      decision=="accept" ? production::SampleCandidateReviewDecision::Accept : production::SampleCandidateReviewDecision::Reject,
      units,cancellation.token());
  if (!receipt) return fail(receipt.error(), &cancellation);
  Json::Array records;
  for (const auto& item:receipt.value().reviews) records.emplace_back(Json::Object{
      {"reviewId",item.reviewId},{"takeId",item.takeId},{"reviewerId",item.reviewerId},{"result",item.result}});
  // The durable decision wins over cancellation observed after its commit.
  print({{"result","ReviewCommitted"},{"generation",std::to_string(receipt.value().committedGeneration)},
      {"projectSha256",receipt.value().committedProjectSha256},{"reviews",std::move(records)},
      {"candidateAvailable",receipt.value().candidate.has_value()},
      {"durabilityConfirmed",receipt.value().durabilityConfirmed},{"diagnostic",receipt.value().diagnostic},
      {"releaseEligible",false}});
  return 0;
}

int publish(int argc, char** argv) {
  if (argc!=7) { printSampleReviewUsage(); return 1; }
  std::uint64_t expectedGeneration=0U;
  const std::string_view number{argv[4]};
  const auto parsed=std::from_chars(number.data(),number.data()+number.size(),expectedGeneration);
  if (parsed.ec!=std::errc{} || parsed.ptr!=number.data()+number.size() || expectedGeneration==0U ||
      expectedGeneration>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) || !digestValid(argv[5]))
    return fail({core::ErrorCode::InvalidArgument,"Publication requires the retained generation and project SHA-256",{}});
  SignalCancellation cancellation;
  if (!cancellation.install()) return fail({core::ErrorCode::IoError,"Cannot install publication cancellation handlers",{}});
  production::ProductionProjectRepository repository{argv[2]};
  const auto project=repository.recover(); if (!project) return fail(project.error(), &cancellation);
  if (project.value().lastDurableGeneration!=expectedGeneration || core::sha256Hex(production::encodeProductionProject(project.value()))!=argv[5])
    return fail({core::ErrorCode::Conflict,"Publication source changed since the captured review receipt",{}}, &cancellation);
  const auto manifest=voicebank::ManifestJsonCodec{}.load(argv[3]); if (!manifest) return fail(manifest.error(), &cancellation);
  const auto request=production::resolveReviewedSampleCandidate(argv[2],project.value(),manifest.value(),cancellation.token());
  if (!request) return fail(request.error(), &cancellation);
  std::error_code pathError;
  const auto destination=std::filesystem::absolute(argv[6],pathError).lexically_normal();
  if (pathError) return fail({core::ErrorCode::InvalidArgument,"Cannot resolve candidate destination",pathError.message()}, &cancellation);
  const auto candidate=production::publishSampleCandidate(argv[2],project.value(),request.value(),destination,{},cancellation.token());
  if (!candidate) return fail(candidate.error(), &cancellation);
  print({{"result","CandidateCommitted"},{"root",candidate.value().root.generic_string()},
      {"manifestSha256",candidate.value().manifestSha256},{"contentSha256",candidate.value().contentSha256},
      {"candidateSha256",candidate.value().candidateSha256},{"sourceGeneration",std::to_string(candidate.value().sourceGeneration)},
      {"durabilityConfirmed",candidate.value().durabilityConfirmed},{"diagnostic",candidate.value().diagnostic},
      {"releaseEligible",candidate.value().releaseEligible}});
  return 0;
}
int sourceQuality(int argc, char** argv, bool record) {
  if (argc != (record ? 12 : 4)) return fail({core::ErrorCode::InvalidArgument,"Invalid source quality command arguments",{}});
  SignalCancellation cancellation;
  production::ProductionProjectRepository repository{argv[2]};
  auto project = repository.recover(); if (!project) return fail(project.error(),&cancellation);
  const auto verified = repository.verify(project.value()); if (!verified) return fail(verified.error(),&cancellation);
  const auto source = std::find_if(project.value().sourceStrategies.begin(),project.value().sourceStrategies.end(),
      [&](const auto& value) { return value.id == argv[3]; });
  if (source == project.value().sourceStrategies.end()) return fail({core::ErrorCode::NotFound,"Source strategy is unavailable",argv[3]});
  const auto material = production::sourceQualityMaterialIdentity(project.value(),source->id);
  if (!material) return fail(material.error(),&cancellation);
  const auto projectHash = core::sha256Hex(production::encodeProductionProject(project.value()));
  if (!record) {
    const bool recorded = std::any_of(project.value().sourceQualityAssessments.begin(),project.value().sourceQualityAssessments.end(),
        [&](const auto& row) { return row.strategyId == source->id; });
    const bool passing = recorded && static_cast<bool>(production::requireCurrentSourceQualityAssessment(project.value(),source->id));
    print({{"projectSha256",projectHash},{"generation",std::to_string(project.value().lastDurableGeneration)},
        {"strategyId",source->id},{"policySha256",production::sourceQualityPolicyIdentity(*source)},
        {"materialSha256",material.value()},{"coverage",production::toString(source->coverage)},
        {"listening",production::toString(source->listening)},{"qualityAssessmentRecorded",recorded},
        {"recordedQualityPassingForCurrentMaterial",passing},{"approval","unchanged"},{"releaseEligible",false}});
    return 0;
  }
  const auto outcome = [](std::string_view value) -> std::optional<production::Feasibility> {
    if (value == "pass") return production::Feasibility::Pass;
    if (value == "blocked") return production::Feasibility::Blocked;
    if (value == "not-assessed") return production::Feasibility::NotAssessed;
    return std::nullopt;
  };
  const auto coverage = outcome(argv[8]), listening = outcome(argv[9]);
  if (!coverage || !listening || !digestValid(argv[4]) || !digestValid(argv[11]))
    return fail({core::ErrorCode::InvalidArgument,"Source quality outcomes or expected digests are invalid",{}});
  production::SourceQualityAssessment assessment{argv[5],source->id,production::sourceQualityPolicyIdentity(*source),
      material.value(),argv[11],argv[6],argv[7],*coverage,*listening};
  const auto committed = repository.recordSourceQualityAssessment(project.value(),assessment,argv[10],argv[4],cancellation.token());
  if (!committed) return fail(committed.error(),&cancellation);
  print({{"result","SourceQualityAssessmentCommitted"},{"assessmentId",assessment.id},
      {"generation",std::to_string(committed.value().committedGeneration)},{"projectSha256",committed.value().committedProjectSha256},
      {"durabilityConfirmed",committed.value().durabilityConfirmed},{"diagnostic",committed.value().diagnostic},
      {"sourcePermissions","unchanged"},{"unitApproval","not granted"},{"releaseEligible",false}});
  return 0;
}
} // namespace

std::optional<int> runSampleReviewCommand(int argc, char** argv) {
  if (argc<2) return std::nullopt;
  const std::string_view command{argv[1]};
  if (command=="register-source") return registerSource(argc,argv);
  if (command=="inspect-source-quality") return sourceQuality(argc,argv,false);
  if (command=="record-source-quality") return sourceQuality(argc,argv,true);
  if (command=="init-production") return initializeDraft(argc,argv);
  if (command=="create-sample-draft") return createDraft(argc,argv);
  if (command=="prepare-sample-review") return prepare(argc,argv);
  if (command=="inspect-sample-review") return inspect(argc,argv);
  if (command=="review-sample") return review(argc,argv);
  if (command=="publish-sample") return publish(argc,argv);
  return std::nullopt;
}
void printSampleReviewUsage() {
  std::cout << "  seam_voicebank_cli inspect-source-quality WORKSPACE STRATEGY\n"
    << "  seam_voicebank_cli register-source WORKSPACE PROJECT_SHA256 ID human|procedural|tts pass|blocked|not-assessed SOURCE_USE TRANSFORM REDISTRIBUTE COMMERCIAL LICENSE LICENSE_SHA256 PRODUCER UTC\n"
    << "    Each permission is yes|no. Appends/selects a NEW source; records your declaration, not legal or musical verification.\n"
    << "  seam_voicebank_cli record-source-quality WORKSPACE STRATEGY PROJECT_SHA256 ID REVIEWER UTC COVERAGE LISTENING EVIDENCE EVIDENCE_SHA256\n"
    << "    Outcomes: pass|blocked|not-assessed. Records an independent supplied decision; never grants source rights or unit approval.\n"
    << "  seam_voicebank_cli init-production WORKSPACE DRAFT_DEFINITION FILE_SHA256 PRODUCER UTC\n"
    << "    Creates a new empty Draft from a captured schema-2 or style-owned schema-4 definition, without musical approval.\n"
    << "  seam_voicebank_cli create-sample-draft WORKSPACE BANK_ID VERSION NAME ja|en|ko STYLE OUTPUT_DIRECTORY\n"
    << "    Copies current takes into a new editable manifest with UNREVIEWED marker/pitch estimates; never approves.\n"
    << "  seam_voicebank_cli prepare-sample-review WORKSPACE MANIFEST OUTPUT_PACKET\n"
    << "  seam_voicebank_cli inspect-sample-review PACKET FILE_SHA256\n"
    << "  seam_voicebank_cli review-sample WORKSPACE PACKET FILE_SHA256 REVIEWER UTC accept|reject [UNIT ...]\n"
    << "  seam_voicebank_cli publish-sample WORKSPACE MANIFEST EXPECTED_GENERATION PROJECT_SHA256 OUTPUT_DIRECTORY\n"
    << "    Capture/inspect never approves. Review requires a registered independent reviewer and explicit decision.\n"
    << "    Publication is an engineering candidate, not a signed package, install, or release approval.\n";
}
} // namespace seam::voicebank_cli
