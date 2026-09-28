#pragma once

#include "seam/core/result.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank/take_inspection.hpp"
#include "seam/voicebank_production/project.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace seam::voicebank_production {

// Receipt for the automated QC of one admitted take. It names the exact stored
// bytes, the assignment the take was admitted for, and the QC policy and version
// that decided each outcome. It is technical evidence only and never a review.
inline constexpr std::string_view kTakeInspectionRevisionKind = "take-inspection.v2";
// Voiced-only receipts written before policy-aware QC. They stay valid history
// but no longer admit a take.
inline constexpr std::string_view kLegacyDryTakeInspectionRevisionKind = "dry-take-inspection.v1";

// The take and the unit identity that decide the QC request. The take ID pins a
// receipt to exactly one admitted take; the coverage key and pitch layer decide
// its policy and expected root. The style label is deliberately left out: it
// does not change which checks apply, and the style migration is the governed
// path that relabels legacy takes without re-deciding their QC.
struct TakeInspectionBinding final {
  std::string takeId;
  std::string promptId;
  std::string coverageKey;
  std::int32_t pitchLayer{0};

  friend bool operator==(const TakeInspectionBinding&, const TakeInspectionBinding&) = default;
};

struct TakeInspectionEvidence final {
  TakeInspectionBinding binding;
  voicebank::TakeInspection inspection;
};

// The request that applies to a unit: the policy its coverage key selects and,
// for a voiced unit, its pitch layer as the expected root note.
[[nodiscard]] core::Result<voicebank::TakeInspectionRequest> takeInspectionRequestFor(
    std::string_view coverageKey, std::int32_t pitchLayer);

// The inspection itself as a JSON object: inspector and policy versions, the
// exact digest and size, every measurement, each outcome and the status. The
// receipt adds its schema version and binding; a take sidecar adds its file.
[[nodiscard]] formats::JsonValue takeInspectionJson(const voicebank::TakeInspection& inspection);

[[nodiscard]] std::string encodeTakeInspectionEvidence(const TakeInspectionEvidence& evidence);
[[nodiscard]] core::Result<TakeInspectionEvidence> decodeTakeInspectionEvidence(std::string_view json);

[[nodiscard]] MetadataRevision makeTakeInspectionRevision(
    const TakeInspectionEvidence& evidence, std::string operatorId, std::string performedAtUtc);

// Checks a take-inspection.v2 revision against the take it names: exact digest
// and byte size, assignment binding, the policy that applies to the take, and
// outcomes that the policy version derives from the recorded measurements.
[[nodiscard]] core::Result<TakeInspectionEvidence> validateTakeInspectionRevision(
    const VoicebankProductionProject& project, const MetadataRevision& revision);

// The newest valid receipt for the take's current raw bytes, if any.
[[nodiscard]] std::optional<TakeInspectionEvidence> currentTakeInspection(
    const VoicebankProductionProject& project, std::string_view takeId);

[[nodiscard]] bool sameMetadataRevision(const MetadataRevision& left, const MetadataRevision& right);

}  // namespace seam::voicebank_production
