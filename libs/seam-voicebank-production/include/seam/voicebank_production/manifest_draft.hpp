#pragma once

#include "seam/voicebank_production/project.hpp"
#include "seam/voicebank/voicebank.hpp"
#include <filesystem>
#include <functional>
#include <stop_token>

namespace seam::voicebank_production {

struct SampleManifestDraftIdentity final {
  std::string id, version, displayName;
  domain::Language language{domain::Language::Unspecified};
  // Explicit single-style declaration, never guessed from another bank. U10
  // owns multi-style assignment identity; this builder cannot merge styles.
  std::string style;
};
enum class ManifestDraftStage { AudioStaged, BeforeCommit, AfterCommitBeforeParentSync };
struct SampleManifestDraftOptions final {
  std::uint64_t maximumAudioBytes{256ULL * 1024ULL * 1024ULL};
  std::uint64_t maximumFramesPerAsset{1024ULL * 1024ULL};
  // Aggregate FFT-work ceiling, not a claim of measured latency or quality.
  std::uint64_t maximumPitchTransformButterflies{256ULL * 1024ULL * 1024ULL};
  std::function<core::Result<void>(ManifestDraftStage)> faultInjector;
};
struct CreatedSampleManifestDraft final {
  std::filesystem::path root;
  std::string manifestSha256, draftSha256, sourceProjectSha256;
  std::uint64_t sourceGeneration{0U};
  std::vector<std::string> missingAssignments, diagnostics;
  bool durabilityConfirmed{true};
  bool releaseEligible{false};
};

// Creates an editable manifest/audio directory from current active takes.
// Coverage keys already contain their exact colon-separated phone sequence.
// Marker/pitch estimates are explicitly unreviewed; no producer, source policy,
// review, installed resource or existing manifest is modified. Missing takes
// remain listed as missing, never substituted. All output uses create-new
// publication outside the producer. A returned value always means committed,
// even when its parent-directory durability could not be confirmed.
[[nodiscard]] core::Result<CreatedSampleManifestDraft> createSampleManifestDraft(
    const std::filesystem::path& repositoryRoot,
    const VoicebankProductionProject& project,
    const SampleManifestDraftIdentity& identity,
    const std::filesystem::path& destination,
    const SampleManifestDraftOptions& options = {}, std::stop_token stop = {});

}  // namespace seam::voicebank_production
