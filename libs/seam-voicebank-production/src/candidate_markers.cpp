#include "seam/voicebank_production/candidate_markers.hpp"
#include "seam/core/sha256.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include <algorithm>
#include <charconv>

namespace seam::voicebank_production {
core::Result<ResolvedCandidateMarkers> resolveCandidateMarkers(
    const VoicebankProductionProject& project, std::string_view takeId) {
  const auto fail = [](const char* message) { return core::failure<ResolvedCandidateMarkers>(core::ErrorCode::InvalidArgument, message); };
  const MetadataRevision* lineage = nullptr;
  const auto take = std::find_if(project.takes.begin(), project.takes.end(),
      [&](const auto& value) { return value.takeId == takeId; });
  if (take == project.takes.end()) return fail("Candidate take is missing");
  for (const auto& revision : project.metadataRevisions) {
    if (revision.takeId == takeId && revision.kind == "procedural-lineage") {
      if (lineage) return fail("Candidate lineage is ambiguous");
      lineage = &revision;
    }
  }
  if (!lineage || lineage->rawAssetSha256 != take->rawAssetSha256) return fail("Candidate lineage is missing or unbound");
  const auto& values = lineage->values;
  for (const auto* key : {"candidateMetadata", "candidateMetadataSha256", "recipeJson", "recipeHash"})
    if (!values.contains(key)) return fail("Candidate lineage is incomplete");
  if (core::sha256Hex(values.at("candidateMetadata")) != values.at("candidateMetadataSha256") ||
      core::sha256Hex(values.at("recipeJson")) != values.at("recipeHash")) return fail("Candidate lineage hash differs");
  const auto recipe = voice_design::decodeVoiceRecipe(values.at("recipeJson"));
  if (!recipe) return core::Result<ResolvedCandidateMarkers>{recipe.error()};
  const auto resource = voice_design::freezeVoiceRecipeResource(recipe.value());
  if (!resource) return core::Result<ResolvedCandidateMarkers>{resource.error()};
  auto candidate = voice_design::parseProceduralCandidateMetadata(values.at("candidateMetadata"), resource.value());
  if (!candidate) return core::Result<ResolvedCandidateMarkers>{candidate.error()};
  if (candidate.value().audioSha256 != take->rawAssetSha256) return fail("Candidate audio binding differs");
  ResolvedCandidateMarkers result{std::move(candidate.value()), lineage->revisionId};
  bool sawLineage = false;
  for (const auto& revision : project.metadataRevisions) {
    if (&revision == lineage) sawLineage = true;
    if (revision.takeId != takeId || revision.kind != "candidate-marker-edit") continue;
    const auto& edit = revision.values;
    if (!sawLineage || edit.size() != 4U || revision.rawAssetSha256 != take->rawAssetSha256)
      return fail("Candidate marker edit is unbound or malformed");
    for (const auto* key : {"previousRevisionId", "key", "startFrame", "endFrame"})
      if (!edit.contains(key)) return fail("Candidate marker edit is incomplete");
    if (edit.at("previousRevisionId") != result.revisionId) return fail("Candidate marker edit chain is stale");
    std::int64_t start = 0, end = 0;
    const auto parse = [](const std::string& text, std::int64_t& value) {
      const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
      return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && text == std::to_string(value);
    };
    if (!parse(edit.at("startFrame"), start) || !parse(edit.at("endFrame"), end)) return fail("Candidate marker frames are invalid");
    auto& markers = result.candidate.markers;
    const auto marker = std::find_if(markers.begin(), markers.end(), [&](const auto& value) { return value.key.toString() == edit.at("key"); });
    if (marker == markers.end() || start < 0 || end <= start || end > result.candidate.frameCount ||
        (marker != markers.begin() && start < (marker - 1)->ownedSpan.end) ||
        (marker + 1 != markers.end() && end > (marker + 1)->ownedSpan.start)) return fail("Candidate marker edit overlaps or exceeds its raw audio");
    marker->ownedSpan = {start, end};
    result.revisionId = revision.revisionId;
  }
  return result;
}
}
