#pragma once

#include "seam/distribution/seambank.hpp"
#include "seam/domain/performance_intent.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seam::distribution {

// A distributable original procedural singer: one immutable recipe plus the declared facts a
// selector needs. It is a different family from a sample bank, so the container carries a typed
// manifest and the sample-bank `manifest.json` contract is untouched.
//
// Declared support and reviewed qualification are deliberately different things. This manifest
// records what the producer *states* (language, styles, phones, engine revision). Signing proves
// authenticity, not musical quality, and no field here may be read as a review outcome.
struct ProceduralSingerManifest final {
  static constexpr std::int32_t kSchemaVersion = 1;
  static constexpr std::string_view kFormatId = "com.project-seam.procedural-singer";

  std::string id;
  std::string version;
  std::string displayName;
  std::string language{"und"};
  std::vector<std::string> styles{"neutral"};
  // The engine that must render this recipe, and its revision. A recipe built for one engine
  // revision is not silently rendered by another.
  std::string engineId;
  std::uint32_t engineRevision{0U};
  // Canonical recipe bytes inside the package, and their digest.
  std::string recipeEntry{"recipe.json"};
  std::string recipeSha256;
  // Declared phone coverage. Declared, not measured.
  std::vector<std::string> phones;

  [[nodiscard]] core::Result<void> validate() const;
  friend bool operator==(const ProceduralSingerManifest&, const ProceduralSingerManifest&) = default;
};

class ProceduralSingerManifestJsonCodec final {
public:
  [[nodiscard]] core::Result<std::string> encode(const ProceduralSingerManifest& manifest) const;
  [[nodiscard]] core::Result<ProceduralSingerManifest> decode(std::string_view json) const;
};

// A verified signed procedural package: the container it was admitted from, plus the declared
// manifest. The recipe itself is read on demand so a large recipe is not duplicated in memory.
struct ProceduralPackageInfo final {
  SignedContainerInfo container;
  ProceduralSingerManifest manifest;
};

struct PackProceduralPackageOptions final {
  SeambankLimits limits{};
};

// Packs a source directory that contains the procedural manifest and the exact recipe bytes it
// names. The manifest, the recipe digest and the recipe's own decodability are all checked before
// anything is signed, so a package that verifies is one a first-party renderer can admit.
[[nodiscard]] core::Result<ProceduralPackageInfo> packProceduralPackage(
    const std::filesystem::path& sourceDirectory,
    const std::filesystem::path& outputPackage,
    const SigningKeyPair& signingKey,
    const PackProceduralPackageOptions& options = {});

[[nodiscard]] core::Result<ProceduralPackageInfo> verifyProceduralPackage(
    const std::filesystem::path& packagePath,
    const VerifySeambankOptions& options = {});

// The recipe bytes exactly as packaged, re-checked against the manifest digest and decoded as a
// recipe before being returned. Read-only relative to signed content.
[[nodiscard]] core::Result<std::vector<std::byte>> readProceduralRecipe(
    const ProceduralPackageInfo& package);

struct InstallProceduralOptions final {
  VerifySeambankOptions verification{};
  bool replaceExisting{false};
};

struct InstalledProceduralSinger final {
  std::string id;
  std::string version;
  // A digest over the installed manifest and recipe, so a host can bind a song to the exact
  // resource it used the way a sample bank binds to its unit content hash.
  std::string contentHash;
  // The identity a project records and the renderer validates, as opposed to the distribution
  // identity above. A caller that selects this singer records this value.
  domain::SingerResourceIdentity renderIdentity{domain::SingerResourceKind::Procedural, {}, {}, {}};
  std::string packageDigest;
  std::string signerKeyId;
  std::filesystem::path installDirectory;
};

enum class ProceduralRootKind { Installed, Development };

enum class ProceduralTrust { TrustedInstalled, UntrustedInstalled, DevelopmentFixture };

// Every way a saved procedural selection can fail to resolve, kept separate so a surface can tell a
// creator which one happened instead of collapsing them into "missing".
enum class ProceduralResolveStatus {
  Resolved,
  Missing,
  VersionMismatch,
  ContentHashMissing,
  ContentMismatch,
  Untrusted,
  // The resource declares an engine or engine revision this build cannot render. It is present and
  // trusted; it is simply not playable here, which a creator must be told rather than shown silence.
  IncompatibleEngine,
  UnsafeEntry,
  InvalidReference,
};

struct ProceduralSearchRoot final {
  std::filesystem::path path;
  ProceduralRootKind kind{ProceduralRootKind::Installed};
};

struct ProceduralCandidate final {
  ProceduralSingerManifest manifest;
  std::filesystem::path resourceRoot;
  // Recomputed from the installed manifest and recipe, not taken from the receipt: a receipt that
  // disagrees with the bytes it sits beside describes a different resource.
  std::string contentHash;
  // The identity the renderer validates when it loads the installed recipe. It is derived from the
  // recipe itself, which is a different thing from the distribution identity above: the manifest
  // carries a producer's release version, while the recipe carries the schema version the renderer
  // checks and a digest over its canonical encoding. A selection must record this, or the renderer
  // will refuse the resource it was told to sing with.
  domain::SingerResourceIdentity renderIdentity{domain::SingerResourceKind::Procedural, {}, {}, {}};
  ProceduralTrust trust{ProceduralTrust::UntrustedInstalled};
  std::string packageDigest;
  std::string signerKeyId;
};

struct ProceduralCatalogue final {
  [[nodiscard]] core::Result<std::vector<ProceduralCandidate>> scan(
      const std::vector<ProceduralSearchRoot>& roots) const;
};

struct ProceduralResolveOptions final {
  bool requireTrustedInstalled{true};
  bool allowDevelopmentFixtures{true};
  // The engine this build can render, and its revision. An empty engine means the caller is browsing
  // rather than rendering and does not check compatibility. A zero revision means the caller knows
  // the engine but not the revision it will render with, so only the engine is compared.
  std::string renderableEngineId;
  std::uint32_t renderableEngineRevision{0U};
};

struct ProceduralResolution final {
  ProceduralResolveStatus status{ProceduralResolveStatus::Missing};
  std::optional<ProceduralCandidate> candidate{};
  std::vector<std::string> availableVersions;
  std::string expectedContentHash;
  std::vector<std::string> actualContentHashes;
  std::string diagnostic;

  [[nodiscard]] bool resolved() const noexcept {
    return status == ProceduralResolveStatus::Resolved && candidate.has_value();
  }
};

// Exact identity resolution against a catalogue. Relink means resolving the same declared identity
// against a different root; it never rewrites the identity the project asked for.
[[nodiscard]] ProceduralResolution resolveProceduralSinger(
    const domain::SingerResourceIdentity& reference,
    const std::vector<ProceduralCandidate>& candidates,
    const ProceduralResolveOptions& options = {});

[[nodiscard]] std::vector<ProceduralSearchRoot> defaultProceduralSearchRoots();
[[nodiscard]] std::string_view proceduralTrustName(ProceduralTrust trust) noexcept;
[[nodiscard]] std::string_view proceduralResolveStatusName(ProceduralResolveStatus status) noexcept;

// Installs a verified procedural package transactionally: everything is written to a staging
// directory, re-checked against the signed identity, given a receipt, and only then published.
// Installing never overwrites another singer's version unless the caller asks for replacement, and
// a failure leaves no partial installation and no stray staging directory.
[[nodiscard]] core::Result<InstalledProceduralSinger> installProceduralPackage(
    const std::filesystem::path& packagePath,
    const std::filesystem::path& installRoot,
    const InstallProceduralOptions& options = {});

}  // namespace seam::distribution
