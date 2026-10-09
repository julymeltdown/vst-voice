#include "candidate_package_commands.hpp"
#include "project_binding_commands.hpp"
#include "signal_cancellation.hpp"

#include "seam/candidate_packaging/candidate_package.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voice_design/recipe_resource.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>

namespace seam::voicebank_cli {
namespace {
namespace packaging = candidate_packaging;
namespace production = voicebank_production;
using Json = formats::JsonValue;

int fail(const core::Error& error, const SignalCancellation* cancellation = nullptr) {
  std::cerr << "error: " << error.message;
  if (!error.context.empty()) std::cerr << " (" << error.context << ')';
  std::cerr << '\n';
  return cancellation && cancellation->signal() != 0 ? 128 + cancellation->signal() : 2;
}
void print(const Json::Object& object) { std::cout << formats::stringifyJson(Json{object}, true) << '\n'; }

core::Result<std::filesystem::path> absolute(const char* value) {
  std::error_code error;
  auto path = std::filesystem::absolute(value, error);
  if (error || path != path.lexically_normal()) return core::failure<std::filesystem::path>(core::ErrorCode::InvalidArgument, "Path must be normalized; resolve dot segments before use", value);
  return path;
}

Json::Array strings(const std::vector<std::string>& values) {
  Json::Array output;
  for (const auto& value : values) output.emplace_back(value);
  return output;
}

std::vector<std::string> splitList(std::string_view text) {
  std::vector<std::string> values;
  std::stringstream stream{std::string{text}};
  for (std::string item; std::getline(stream, item, ',');) values.push_back(item);
  return values;
}

core::Result<distribution::VerifySeambankOptions> trustedKey(const char* path) {
  auto key = distribution::loadPublicKey(path);
  if (!key) return core::Result<distribution::VerifySeambankOptions>{key.error()};
  distribution::VerifySeambankOptions options;
  options.trustedPublicKeys = {key.value()};
  options.requireTrustedSigner = true;
  return options;
}

Json::Object describe(const production::VerifiedResourceCandidate& verified) {
  const auto& descriptor = verified.descriptor;
  Json::Object object{{"schemaVersion", descriptor.schemaVersion},
      {"resourceKind", descriptor.resourceKind}, {"payloadFamily", std::string{production::toString(descriptor.kind)}}, {"status", descriptor.status},
      {"candidateSha256", verified.candidateSha256}, {"manifestSha256", descriptor.manifestSha256},
      {"contentSha256", descriptor.contentSha256}, {"dependencySetDeclared", descriptor.declaresDependencySet()},
      {"qualification", std::string{production::kCandidateQualification}}, {"releaseEligible", false}};
  if (descriptor.source) {
    object.emplace("sourceGeneration", std::to_string(descriptor.source->generation));
    object.emplace("sourceProjectSha256", descriptor.source->projectSha256);
  }
  if (!descriptor.declaresDependencySet()) {
    object.emplace("typedPackaging", "refused: legacy schemas 1/2 declare no dependency set; publish a schema-3 candidate from the same generation");
    return object;
  }
  Json::Array dependencies;
  for (const auto& value : descriptor.externalDependencies)
    dependencies.emplace_back(Json::Object{{"kind", value.kind}, {"id", value.id}, {"revision", value.revision}});
  object.emplace("resourceId", descriptor.resourceId);
  object.emplace("resourceVersion", descriptor.resourceVersion);
  object.emplace("languages", strings(descriptor.languages));
  object.emplace("styles", strings(descriptor.styles));
  object.emplace("character", descriptor.character ? Json{Json::Object{{"characterId", descriptor.character->characterId},
      {"characterVersion", descriptor.character->characterVersion}}} : Json{});
  object.emplace("payloadFiles", static_cast<std::int64_t>(descriptor.payload.size()));
  object.emplace("evidenceFiles", static_cast<std::int64_t>(descriptor.evidence.size()));
  object.emplace("externalDependencies", std::move(dependencies));
  return object;
}

Json::Object published(const production::PublishedResourceCandidate& candidate, production::ResourceCandidateKind kind) {
  return {{"result", "CandidateCommitted"}, {"root", candidate.root.generic_string()},
      {"schemaVersion", production::kResourceCandidateSchemaVersion}, {"resourceKind", kind == production::ResourceCandidateKind::Recipe ? "recipe-original" : "neural-original"}, {"payloadFamily", std::string{production::toString(kind)}},
      {"status", std::string{production::kDeclaredCandidateStatus}},
      {"qualification", std::string{production::kCandidateQualification}},
      {"candidateSha256", candidate.candidateSha256}, {"manifestSha256", candidate.manifestSha256},
      {"contentSha256", candidate.contentSha256}, {"durabilityConfirmed", candidate.durabilityConfirmed},
      {"diagnostic", candidate.diagnostic}, {"reviewed", false}, {"signed", false}, {"releaseEligible", false}};
}

int inspectCandidate(int argc, char** argv) {
  if (argc != 3) { printCandidatePackageUsage(); return 1; }
  SignalCancellation cancellation;
  if (!cancellation.install()) return fail({core::ErrorCode::IoError, "Cannot install inspection cancellation handlers", {}});
  const auto root = absolute(argv[2]);
  if (!root) return fail(root.error());
  const auto verified = production::verifyResourceCandidateDirectory(root.value(), cancellation.token());
  if (!verified) return fail(verified.error(), &cancellation);
  auto object = describe(verified.value());
  object.emplace("result", "CandidateVerified");
  print(object);
  return 0;
}

int packageCandidate(int argc, char** argv) {
  if (argc != 6) { printCandidatePackageUsage(); return 1; }
  SignalCancellation cancellation;
  if (!cancellation.install()) return fail({core::ErrorCode::IoError, "Cannot install packaging cancellation handlers", {}});
  const auto root = absolute(argv[2]);
  const auto output = absolute(argv[4]);
  if (!root) return fail(root.error());
  if (!output) return fail(output.error());
  const auto key = distribution::loadPrivateKey(argv[5]);
  if (!key) return fail(key.error());
  const auto packaged = packaging::packageResourceCandidate(root.value(), output.value(), key.value(),
      {.expectedCandidateSha256 = std::string{argv[3]}}, cancellation.token());
  if (!packaged) return fail(packaged.error(), &cancellation);
  const auto& value = packaged.value();
  print({{"result", "PackageCommitted"}, {"package", value.packagePath.generic_string()},
      {"resourceKind", value.resourceKind}, {"payloadFamily", std::string{production::toString(value.kind)}}, {"resourceId", value.resourceId},
      {"resourceVersion", value.resourceVersion}, {"packageDigest", value.packageDigest}, {"signerKeyId", value.signerKeyId},
      {"candidateSha256", value.candidateSha256}, {"manifestSha256", value.manifestSha256},
      {"contentSha256", value.contentSha256}, {"entries", static_cast<std::int64_t>(value.entries)},
      {"durabilityConfirmed", value.durabilityConfirmed}, {"diagnostic", value.diagnostic},
      {"signing", "authenticity only; not review, qualification or release approval"}, {"installed", false},
      {"releaseEligible", false}});
  return 0;
}

int verifyCandidatePackage(int argc, char** argv) {
  if (argc != 4) { printCandidatePackageUsage(); return 1; }
  const auto options = trustedKey(argv[3]);
  if (!options) return fail(options.error());
  const auto package = absolute(argv[2]);
  if (!package) return fail(package.error());
  const auto verified = packaging::verifyResourceCandidatePackage(package.value(), options.value());
  if (!verified) return fail(verified.error());
  const auto& descriptor = verified.value().descriptor;
  print({{"result", "PackageVerified"}, {"packageDigest", verified.value().container.packageDigest},
      {"signerKeyId", verified.value().container.signerKeyId}, {"signerTrusted", verified.value().container.signerTrusted},
      {"candidateSha256", verified.value().candidateSha256},
      {"resourceKind", descriptor.resourceKind}, {"payloadFamily", std::string{production::toString(descriptor.kind)}}, {"resourceId", descriptor.resourceId},
      {"resourceVersion", descriptor.resourceVersion}, {"contentSha256", descriptor.contentSha256},
      {"entries", static_cast<std::int64_t>(verified.value().container.entries.size())},
      {"qualification", std::string{production::kCandidateQualification}}, {"releaseEligible", false}});
  return 0;
}

int probeModelCandidate(int argc, char** argv) {
  if (argc != 6) { printCandidatePackageUsage(); return 1; }
  SignalCancellation cancellation;
  if (!cancellation.install()) return fail({core::ErrorCode::IoError, "Cannot install model probe cancellation handlers", {}});
  const auto options = trustedKey(argv[5]);
  const auto package = absolute(argv[2]);
  if (!options) return fail(options.error());
  if (!package) return fail(package.error());
  const auto probe = packaging::probeModelCandidateInstallation(package.value(), argv[3], argv[4], options.value(), cancellation.token());
  if (!probe) return fail(probe.error(), &cancellation);
  const auto& value = probe.value();
  const auto& descriptor = value.descriptor;
  Json::Array dependencies;
  for (const auto& dependency : descriptor.externalDependencies)
    dependencies.emplace_back(Json::Object{{"kind", dependency.kind}, {"id", dependency.id}, {"revision", dependency.revision}});
  print({{"schemaVersion", std::int64_t{1}}, {"recordType", "seam.u14.model-installation-refusal.v1"},
      {"result", "ModelPackageVerifiedInstallRefused"}, {"evidenceScope", "ENGINEERING_ONLY"},
      {"resourceKind", descriptor.resourceKind}, {"payloadFamily", "model"},
      {"resourceId", descriptor.resourceId}, {"resourceVersion", descriptor.resourceVersion},
      {"resourceCandidateSha256", value.candidateSha256}, {"packageDigest", value.packageDigest},
      {"candidateContentSha256", descriptor.contentSha256}, {"signerKeyId", value.signerKeyId},
      {"packageEntries", static_cast<std::int64_t>(value.entries)},
      {"installationResult", "REFUSED"}, {"refusalReason", std::string{packaging::kModelInstallUnsupported}},
      {"installDirectoryCreated", false}, {"externalDependencies", std::move(dependencies)},
      {"dependencyEvidence", "SIGNED_DECLARATION"}, {"runtimeAvailability", "NOT_CHECKED"},
      {"graphExecution", "NOT_RUN"}, {"qualification", std::string{production::kCandidateQualification}},
      {"humanAcceptance", "NOT_RUN"}, {"authorizesRelease", false}, {"releaseEligible", false}});
  return 0;
}

Json::Object installedRecord(const packaging::VerifiedInstalledCandidate& value) {
  formats::JsonValue::Array dependencies;
  for (const auto& dependency : value.externalDependencies)
    dependencies.emplace_back(formats::JsonValue::Object{{"kind", dependency.kind}, {"id", dependency.id}, {"revision", dependency.revision}});
  return Json::Object{{"schemaVersion", std::int64_t{2}}, {"recordType", "seam.u14.installed-candidate-verification.v2"},
      {"result", "InstalledCandidateVerified"}, {"evidenceScope", "ENGINEERING_ONLY"},
      {"resourceKind", value.resourceKind}, {"payloadFamily", std::string{production::toString(value.kind)}},
      {"resourceId", value.resourceId}, {"resourceVersion", value.resourceVersion},
      {"resourceCandidateSha256", value.candidateSha256}, {"packageDigest", value.packageDigest},
      {"candidateContentSha256", value.candidateContentSha256}, {"installedContentHash", value.installedContentHash},
      {"signerKeyId", value.signerKeyId}, {"receiptSha256", value.receiptSha256},
      {"installedResourceTreeSha256", value.installedResourceTreeSha256}, {"installedFiles", static_cast<std::int64_t>(value.installedFiles)},
      {"externalDependencies", std::move(dependencies)}, {"dependencyEvidence", "SIGNED_DECLARATION"},
      {"runtimeAvailability", "NOT_CHECKED"},
      {"qualification", std::string{production::kCandidateQualification}}, {"humanAcceptance", "NOT_RUN"},
      {"authorizesRelease", false}, {"releaseEligible", false}};
}

int verifyInstalledCandidate(int argc, char** argv) {
  if (argc != 7) { printCandidatePackageUsage(); return 1; }
  SignalCancellation cancellation;
  if (!cancellation.install()) return fail({core::ErrorCode::IoError, "Cannot install verification cancellation handlers", {}});
  const auto options = trustedKey(argv[6]);
  if (!options) return fail(options.error());
  const auto package = absolute(argv[2]);
  const auto installed = absolute(argv[5]);
  if (!package) return fail(package.error());
  if (!installed) return fail(installed.error());
  const auto verified = packaging::verifyInstalledResourceCandidate(package.value(), argv[3], argv[4], installed.value(), options.value(), cancellation.token());
  if (!verified) return fail(verified.error(), &cancellation);
  print(installedRecord(verified.value()));
  return 0;
}

int verifyInstalledProjectBinding(int argc, char** argv) {
  if (argc != 12) { printCandidatePackageUsage(); return 1; }
  SignalCancellation cancellation;
  if (!cancellation.install()) return fail({core::ErrorCode::IoError, "Cannot install verification cancellation handlers", {}});
  const auto options=trustedKey(argv[11]);
  if (!options) return fail(options.error());
  const auto package=absolute(argv[7]), installed=absolute(argv[10]);
  if (!package) return fail(package.error());
  if (!installed) return fail(installed.error());
  const auto verified=packaging::verifyInstalledResourceCandidate(package.value(),argv[8],argv[9],installed.value(),options.value(),cancellation.token());
  if (!verified) return fail(verified.error(),&cancellation);
  const auto& value=verified.value();
  const auto& resource=value.projectResource;
  const auto binding=verifyProjectBindingRecord({argv[2],argv[3],argv[4],argv[5],production::toString(value.kind),
      resource.id,resource.version,resource.contentHash,argv[6]});
  if (!binding) return fail(binding.error(),&cancellation);
  for (const auto& language : binding.value().find("languages")->asArray())
    if (std::find(value.languages.begin(),value.languages.end(),language.asString())==value.languages.end())
      return fail({core::ErrorCode::Conflict,"Project language is absent from the signed resource declaration",{}});
  if (cancellation.token().stop_requested()) return fail({core::ErrorCode::Conflict,"Installed project binding cancelled",{}},&cancellation);
  print({{"schemaVersion",std::int64_t{1}},{"recordType","seam.u45.installed-project-binding.v1"},
      {"result","InstalledProjectBindingVerified"},{"evidenceScope","ENGINEERING_ONLY"},
      {"bindingEvidence","VERIFIED_INSTALLED_CONTENT_TO_PROJECT_REFERENCE"},
      {"languageCoverage","SIGNED_DECLARATION"},{"resourceLanguages",strings(value.languages)},
      {"installed",installedRecord(value)},{"project",binding.value()},
      {"runtimeAvailability","NOT_CHECKED"},{"playback","NOT_RUN"},{"hostExecution","NOT_RUN"},
      {"humanAcceptance","NOT_RUN"},{"qualification","NOT_QUALIFIED"},{"authorizesRelease",false},{"releaseEligible",false}});
  return 0;
}

int installCandidate(int argc, char** argv) {
  const bool replace = argc == 7 && std::string_view{argv[6]} == "--replace";
  if (argc != 6 && !replace) { printCandidatePackageUsage(); return 1; }
  SignalCancellation cancellation;
  if (!cancellation.install()) return fail({core::ErrorCode::IoError, "Cannot install installation cancellation handlers", {}});
  const auto verification = trustedKey(argv[5]);
  if (!verification) return fail(verification.error());
  const auto package = absolute(argv[2]);
  const auto root = absolute(argv[4]);
  if (!package) return fail(package.error());
  if (!root) return fail(root.error());
  packaging::InstallCandidateOptions options;
  options.verification = verification.value();
  options.expectedPackageDigest = argv[3];
  options.replaceExisting = replace;
  const auto installed = packaging::installResourceCandidatePackage(package.value(), root.value(), options, cancellation.token());
  if (!installed) return fail(installed.error(), &cancellation);
  const auto& value = installed.value();
  print({{"result", "InstallCommitted"}, {"installDirectory", value.installDirectory.generic_string()},
      {"resourceKind", value.resourceKind}, {"payloadFamily", std::string{production::toString(value.kind)}}, {"resourceId", value.resourceId},
      {"resourceVersion", value.resourceVersion}, {"contentHash", value.contentHash},
      {"packageDigest", value.packageDigest}, {"candidateSha256", value.candidateSha256},
      {"signerKeyId", value.signerKeyId}, {"replacedExisting", value.replacedExisting},
      {"descriptorReconfirmed", value.descriptorReconfirmed}, {"durabilityConfirmed", value.durabilityConfirmed}, {"diagnostic", value.diagnostic}, {"releaseEligible", false}});
  return 0;  // A committed installation wins over a late cancellation.
}

int publishRecipeCandidate(int argc, char** argv) {
  if (argc != 6 && argc != 7) { printCandidatePackageUsage(); return 1; }
  SignalCancellation cancellation;
  if (!cancellation.install()) return fail({core::ErrorCode::IoError, "Cannot install publication cancellation handlers", {}});
  const auto recipe = voice_design::loadVoiceRecipeResource(argv[2]);
  if (!recipe) return fail(recipe.error());
  const auto destination = absolute(argv[5]);
  if (!destination) return fail(destination.error());
  distribution::PublishProceduralSingerOptions options;
  options.version = argv[3];
  options.language = argv[4];
  if (argc == 7) options.displayName = argv[6];
  const auto candidate = packaging::publishRecipeCandidate(recipe.value(), options, destination.value(), cancellation.token());
  if (!candidate) return fail(candidate.error(), &cancellation);
  print(published(candidate.value(), production::ResourceCandidateKind::Recipe));
  return 0;
}

int publishModelCandidate(int argc, char** argv) {
  if (argc != 8 && argc != 9) { printCandidatePackageUsage(); return 1; }
  SignalCancellation cancellation;
  if (!cancellation.install()) return fail({core::ErrorCode::IoError, "Cannot install publication cancellation handlers", {}});
  const auto payload = absolute(argv[2]);
  const auto destination = absolute(argv[7]);
  if (!payload) return fail(payload.error());
  if (!destination) return fail(destination.error());
  packaging::ModelCandidateDeclaration declaration{argc == 9 ? argv[8] : "", splitList(argv[3]), splitList(argv[4]), argv[5], argv[6]};
  const auto candidate = packaging::publishModelCandidate(payload.value(), declaration, destination.value(), cancellation.token());
  if (!candidate) return fail(candidate.error(), &cancellation);
  print(published(candidate.value(), production::ResourceCandidateKind::Model));
  return 0;
}
}  // namespace

std::optional<int> runCandidatePackageCommand(int argc, char** argv) {
  if (argc >= 2 && std::string_view{argv[1]} == "verify-installed-project-binding") return verifyInstalledProjectBinding(argc,argv);
  if (argc < 2) return std::nullopt;
  const std::string_view command{argv[1]};
  if (command == "inspect-candidate") return inspectCandidate(argc, argv);
  if (command == "package-candidate") return packageCandidate(argc, argv);
  if (command == "verify-candidate-package") return verifyCandidatePackage(argc, argv);
  if (command == "probe-model-candidate") return probeModelCandidate(argc, argv);
  if (command == "verify-installed-candidate") return verifyInstalledCandidate(argc, argv);
  if (command == "install-candidate") return installCandidate(argc, argv);
  if (command == "publish-recipe-candidate") return publishRecipeCandidate(argc, argv);
  if (command == "publish-model-candidate") return publishModelCandidate(argc, argv);
  return std::nullopt;
}

void printCandidatePackageUsage() {
  std::cerr << "  verify-installed-project-binding PROJECT SHA TRACK REGION LANGUAGES PACKAGE PACKAGE_SHA CANDIDATE_SHA INSTALL_DIR KEY\n";
  std::cout << "  seam_voicebank_cli inspect-candidate CANDIDATE_DIRECTORY\n"
    << "    Verifies a published candidate against its descriptor; legacy schemas 1/2 retain their original version.\n"
    << "  seam_voicebank_cli package-candidate CANDIDATE_DIRECTORY CANDIDATE_SHA256 OUTPUT_PACKAGE PRIVATE_KEY\n"
    << "    Signs exactly that schema-3 candidate into a new package; never replaces a package.\n"
    << "  seam_voicebank_cli verify-candidate-package PACKAGE PUBLIC_KEY\n"
    << "  seam_voicebank_cli probe-model-candidate PACKAGE PACKAGE_SHA256 CANDIDATE_SHA256 PUBLIC_KEY\n"
    << "    Verifies opaque model bytes and observes intentional installation refusal in private scratch.\n"
    << "  seam_voicebank_cli verify-installed-candidate PACKAGE PACKAGE_SHA256 CANDIDATE_SHA256 INSTALL_DIRECTORY PUBLIC_KEY\n"
    << "    Read-only exact signed-entry and family-receipt verification; engineering only.\n"
    << "  seam_voicebank_cli install-candidate PACKAGE PACKAGE_SHA256 INSTALL_ROOT PUBLIC_KEY [--replace]\n"
    << "    Installs only the package with that digest; a failure leaves the installed version intact.\n"
    << "  seam_voicebank_cli publish-recipe-candidate RECIPE_JSON VERSION LANGUAGE OUTPUT_DIRECTORY [DISPLAY_NAME]\n"
    << "  seam_voicebank_cli publish-model-candidate PAYLOAD_DIRECTORY LANGUAGES STYLES RUNTIME_ID RUNTIME_REVISION OUTPUT_DIRECTORY [DISPLAY_NAME]\n"
    << "    Declared candidates: producer statements only, never reviewed, measured or qualified.\n"
    << "    Model candidates can be packaged and verified; this build installs no model resources.\n";
}
}  // namespace seam::voicebank_cli
