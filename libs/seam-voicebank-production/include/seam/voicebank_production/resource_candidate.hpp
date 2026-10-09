#pragma once

#include "seam/core/result.hpp"
#include "seam/formats/json_value.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace seam::voicebank_production {

// The typed, versioned descriptor every published resource candidate carries as candidate.json.
// Schema 1 is the original sample-only descriptor; it is still read and checked against the files
// it names, but it never declared a complete dependency set and is never rewritten as schema 3.
// Schema 2 added canonical source kind, language and character fields, but no dependency list.
// Both legacy schemas remain readable without upgrading their closure claims.
// Schema 3 distinguishes sample, recipe and model payloads and lists every embedded byte, so the
// signed package identity (which covers candidate.json) covers every effective dependency.
inline constexpr std::string_view kResourceCandidateFormat = "com.project-seam.resource-candidate";
inline constexpr std::string_view kResourceCandidateDescriptorPath = "candidate.json";
inline constexpr std::int64_t kLegacySampleCandidateSchemaVersion = 1;
inline constexpr std::int64_t kLegacySourceCandidateSchemaVersion = 2;
inline constexpr std::int64_t kResourceCandidateSchemaVersion = 3;

// A candidate is engineering material awaiting later gates. None of these values can be changed
// by a producer; a descriptor carrying anything else is refused on read.
inline constexpr std::string_view kCandidateEvidenceScope = "engineering";
inline constexpr std::string_view kCandidateQualification = "NOT_QUALIFIED";
// Sample material passed independent unit review in one producer generation.
inline constexpr std::string_view kReviewedCandidateStatus = "REVIEWED_CANDIDATE";
// Recipe and model payloads carry only producer-declared facts: no review, no measurement.
inline constexpr std::string_view kDeclaredCandidateStatus = "DECLARED_CANDIDATE";

enum class ResourceCandidateKind { Sample, Recipe, Model };
[[nodiscard]] std::string_view toString(ResourceCandidateKind kind) noexcept;

// One embedded file other than candidate.json itself, listed exactly once.
struct ResourceCandidateFile final {
  std::string path;
  std::string role;
  std::string sha256;
  std::uint64_t bytes{0U};
  friend bool operator==(const ResourceCandidateFile&, const ResourceCandidateFile&) = default;
};

// An identity the resource needs but does not embed, for example the engine revision a recipe must
// be rendered by. It sits inside the signed identity and is never satisfied implicitly.
struct ResourceCandidateExternalDependency final {
  std::string kind;
  std::string id;
  std::string revision;
  friend bool operator==(const ResourceCandidateExternalDependency&,
                         const ResourceCandidateExternalDependency&) = default;
};

struct ResourceCandidateCharacter final {
  std::string characterId;
  std::string characterVersion;
  friend bool operator==(const ResourceCandidateCharacter&, const ResourceCandidateCharacter&) = default;
};

// The one producer generation a sample candidate was built from. Absent for recipe and model
// candidates, which are not forced into a producer or contracted-singer profile.
struct ResourceCandidateSource final {
  std::string projectId;  // Empty for legacy schemas 1/2, which did not record it.
  std::uint64_t generation{0U};
  std::string projectSha256;
  std::string inventorySha256;
  std::string licenseSha256;
  friend bool operator==(const ResourceCandidateSource&, const ResourceCandidateSource&) = default;
};

struct ResourceCandidateDescriptor final {
  std::int64_t schemaVersion{kResourceCandidateSchemaVersion};
  ResourceCandidateKind kind{ResourceCandidateKind::Sample};
  // Canonical source kind, retained separately from the payload family.
  std::string resourceKind;
  std::string status;
  // Identity and declared applicability. Schema 1 recorded none of these.
  std::string resourceId;
  std::string resourceVersion;
  std::string displayName;
  std::vector<std::string> languages;
  std::vector<std::string> styles;
  std::optional<ResourceCandidateCharacter> character;
  std::string rootManifest{"manifest.json"};
  std::string manifestSha256;
  // Kind-specific content identity: the voicebank content hash (sample), the canonical recipe
  // digest (recipe) or the canonical payload listing digest (model).
  std::string contentSha256;
  std::optional<ResourceCandidateSource> source;
  // Payload is what the resource needs to function; evidence is what proves where it came from.
  // Together they are every embedded file except candidate.json. Empty for legacy schemas 1/2.
  std::vector<ResourceCandidateFile> payload;
  std::vector<ResourceCandidateFile> evidence;
  std::vector<ResourceCandidateExternalDependency> externalDependencies;
  // Sample provenance carried over from schema 1: retained origin history and review bindings.
  formats::JsonValue::Array originHistory;
  formats::JsonValue::Array unitBindings;

  [[nodiscard]] bool declaresDependencySet() const noexcept {
    return schemaVersion == kResourceCandidateSchemaVersion;
  }
};

// Encodes schema 3 only. A legacy descriptor is refused so legacy material is never upgraded.
[[nodiscard]] core::Result<std::string> encodeResourceCandidateDescriptor(
    const ResourceCandidateDescriptor& descriptor);
// Decodes legacy schemas 1/2 and typed schema 3 strictly: unknown keys, relaxed honesty fields or malformed lists refuse.
[[nodiscard]] core::Result<ResourceCandidateDescriptor> decodeResourceCandidateDescriptor(
    std::string_view json);
// Structural and kind rules that need no files. Decoding already applies them.
[[nodiscard]] core::Result<void> validateResourceCandidateDescriptor(
    const ResourceCandidateDescriptor& descriptor);
// Canonical content identity for a model payload listing (path, digest and size of each file).
[[nodiscard]] std::string modelCandidateContentSha256(const std::vector<ResourceCandidateFile>& payload);
// The path rule packages enforce, applied before any file is listed.
[[nodiscard]] bool isPackageableCandidatePath(std::string_view path) noexcept;

struct VerifiedResourceCandidate final {
  std::filesystem::path root;
  ResourceCandidateDescriptor descriptor;
  std::string candidateSha256;
};

// Reads candidate.json and checks it against the directory it describes. Schema 3 requires the
// directory to hold exactly the listed files with their listed sizes and digests (no symlinks, no
// unlisted or missing file). Sample candidates are also reopened as a voicebank and producer
// snapshot. Legacy schemas are checked against their named files and reported with their original version.
[[nodiscard]] core::Result<VerifiedResourceCandidate> verifyResourceCandidateDirectory(
    const std::filesystem::path& root, std::stop_token stop = {});

// Producer-declared facts for a recipe or model candidate. Declared only: nothing here is
// reviewed, measured or qualified, and no producer source or contracted-singer profile is implied.
struct DeclaredResourceCandidateRequest final {
  ResourceCandidateKind kind{ResourceCandidateKind::Recipe};
  std::string resourceId;
  std::string resourceVersion;
  std::string displayName;
  std::vector<std::string> languages;
  std::vector<std::string> styles;
  std::vector<ResourceCandidateExternalDependency> externalDependencies;
  std::string rootManifest{"manifest.json"};
  // The role of every payload file by relative path; the payload directory must hold exactly these.
  std::map<std::string, std::string, std::less<>> roles;
  // Recipe: the canonical recipe digest. Model: left empty; derived from the payload listing.
  std::string contentSha256;
};

struct PublishedResourceCandidate final {
  std::filesystem::path root;
  std::string candidateSha256;
  std::string manifestSha256;
  std::string contentSha256;
  // False only for a committed candidate whose parent directory entry may not be durable yet.
  bool durabilityConfirmed{true};
  std::string diagnostic;
};

// Copies a declared payload into a new candidate directory with its schema-3 descriptor, verifies
// the staged directory against the descriptor and publishes it create-new. Never overwrites, signs,
// installs or qualifies anything. Sample candidates are refused here: they come only from a reviewed
// producer generation.
[[nodiscard]] core::Result<PublishedResourceCandidate> publishDeclaredResourceCandidate(
    const std::filesystem::path& payloadDirectory, const DeclaredResourceCandidateRequest& request,
    const std::filesystem::path& destination, std::stop_token stop = {});

}  // namespace seam::voicebank_production
