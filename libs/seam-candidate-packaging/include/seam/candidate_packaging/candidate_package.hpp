#pragma once

#include "seam/distribution/installer.hpp"
#include "seam/distribution/procedural_package.hpp"
#include "seam/distribution/seambank.hpp"
#include "seam/voicebank_production/resource_candidate.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

// The bridge from a published resource candidate to a signed package and an installed resource.
// Publication, packaging and installation stay three distinct steps, each reporting the exact
// identities it produced. Signing proves authenticity only; nothing here reviews, measures or
// qualifies a resource.
namespace seam::candidate_packaging {

// A recipe candidate carries the same canonical recipe bytes and derived procedural-singer manifest
// a signed singer package carries. Language, styles and the render-engine dependency are taken from
// that derived manifest; no producer source or contracted-singer profile is implied.
[[nodiscard]] core::Result<voicebank_production::PublishedResourceCandidate> publishRecipeCandidate(
    const synthesis::ProceduralSingerResource& recipe,
    const distribution::PublishProceduralSingerOptions& options,
    const std::filesystem::path& destination, std::stop_token stop = {});

// The model payload contract: a root manifest.json object naming modelId and modelVersion, model
// files under graphs/ (model-graph) and any other data files (model-data). The declared facts below
// are the producer's statement. This build packages and verifies model candidates but installs none.
struct ModelCandidateDeclaration final {
  std::string displayName;
  std::vector<std::string> languages;
  std::vector<std::string> styles;
  std::string runtimeId;
  std::string runtimeRevision;
};

[[nodiscard]] core::Result<voicebank_production::PublishedResourceCandidate> publishModelCandidate(
    const std::filesystem::path& payloadDirectory, const ModelCandidateDeclaration& declaration,
    const std::filesystem::path& destination, std::stop_token stop = {});

struct VerifiedCandidatePackage final {
  distribution::SignedContainerInfo container;
  voicebank_production::ResourceCandidateDescriptor descriptor;
  std::string candidateSha256;
};

// Verifies the signed container, that its entries are exactly the descriptor's listed files plus
// candidate.json with the listed sizes and digests, and that the family manifest inside agrees with
// the declared identity, applicability and dependencies. Legacy schema-1/2 candidates are refused.
[[nodiscard]] core::Result<VerifiedCandidatePackage> verifyResourceCandidatePackage(
    const std::filesystem::path& packagePath, const distribution::VerifySeambankOptions& options);

enum class CandidatePackagingStage { BeforePublish, AfterPublishBeforeSync };

struct PackageCandidateOptions final {
  // The candidate digest the publication step reported; packaging refuses any other candidate.
  std::string expectedCandidateSha256;
  distribution::SeambankLimits limits{};
  std::function<core::Result<void>(CandidatePackagingStage)> faultInjector{};
};

struct PackagedResourceCandidate final {
  std::filesystem::path packagePath;
  voicebank_production::ResourceCandidateKind kind{voicebank_production::ResourceCandidateKind::Sample};
  std::string resourceId;
  std::string resourceVersion;
  std::string packageDigest;
  std::string signerKeyId;
  std::string candidateSha256;
  std::string manifestSha256;
  std::string contentSha256;
  std::size_t entries{0U};
  bool durabilityConfirmed{true};
  std::string diagnostic;
  std::string resourceKind;
};

// Signs exactly one verified schema-3 candidate directory into a new package file, verifies that
// package against the descriptor, and only then makes it visible. An existing package is never
// replaced, and a cancelled or failed attempt leaves nothing at the destination.
[[nodiscard]] core::Result<PackagedResourceCandidate> packageResourceCandidate(
    const std::filesystem::path& candidateRoot, const std::filesystem::path& outputPackage,
    const distribution::SigningKeyPair& signingKey, const PackageCandidateOptions& options = {},
    std::stop_token stop = {});

struct InstallCandidateOptions final {
  distribution::VerifySeambankOptions verification{};
  // Required: the digest the packaging step reported.
  std::string expectedPackageDigest;
  bool replaceExisting{false};
  std::function<core::Result<void>(distribution::InstallStage)> faultInjector{};
};

struct InstalledResourceCandidate final {
  voicebank_production::ResourceCandidateKind kind{voicebank_production::ResourceCandidateKind::Sample};
  std::string resourceId;
  std::string resourceVersion;
  std::string contentHash;
  std::string packageDigest;
  std::string signerKeyId;
  std::string candidateSha256;
  std::filesystem::path installDirectory;
  bool replacedExisting{false};
  bool durabilityConfirmed{true};
  // Independent of directory-sync durability; false means a postcommit identity read failed.
  bool descriptorReconfirmed{true};
  std::string diagnostic;
  std::string resourceKind;
};

// Installs one exact, verified candidate package through its family installer. Sample candidates also
// pin the staged voicebank content hash to the descriptor. Model candidates are refused because this
// build has no model installer.
[[nodiscard]] core::Result<InstalledResourceCandidate> installResourceCandidatePackage(
    const std::filesystem::path& packagePath, const std::filesystem::path& installRoot,
    const InstallCandidateOptions& options, std::stop_token stop = {});

inline constexpr std::string_view kModelInstallUnsupported = "MODEL_INSTALL_UNSUPPORTED";

// Observes the existing model installer's intentional refusal in owned scratch.
// Package validity is opaque-byte validity, never graph execution or qualification.
struct ModelCandidateInstallProbe final {
  voicebank_production::ResourceCandidateDescriptor descriptor;
  std::string candidateSha256;
  std::string packageDigest;
  std::string signerKeyId;
  std::size_t entries{0U};
};
[[nodiscard]] core::Result<ModelCandidateInstallProbe> probeModelCandidateInstallation(
    const std::filesystem::path& packagePath, std::string_view expectedPackageDigest,
    std::string_view expectedCandidateSha256, const distribution::VerifySeambankOptions& options,
    std::stop_token stop = {});

// Read-only engineering verification of an already installed sample or recipe.
// Trust is established from the signed package and caller's key, never from receipt
// booleans. Every installed file must match the signed entries, plus one typed
// family receipt. This is neither runtime/render qualification nor human evidence.
struct VerifiedInstalledCandidate final {
  voicebank_production::ResourceCandidateKind kind{voicebank_production::ResourceCandidateKind::Sample};
  std::string resourceKind;
  std::string resourceId;
  std::string resourceVersion;
  std::string candidateSha256;
  std::string packageDigest;
  std::string candidateContentSha256;
  std::string installedContentHash;
  std::string signerKeyId;
  std::string receiptSha256;
  std::string installedResourceTreeSha256;
  std::size_t installedFiles{0U};
  // Derived from the same verified package snapshot whose entries match the installed tree.
  domain::SingerResourceIdentity projectResource;
  std::vector<std::string> languages;
  std::vector<voicebank_production::ResourceCandidateExternalDependency> externalDependencies;
};

[[nodiscard]] core::Result<VerifiedInstalledCandidate> verifyInstalledResourceCandidate(
    const std::filesystem::path& packagePath, std::string_view expectedPackageDigest,
    std::string_view expectedCandidateSha256, const std::filesystem::path& installedDirectory,
    const distribution::VerifySeambankOptions& options, std::stop_token stop = {});

}  // namespace seam::candidate_packaging
