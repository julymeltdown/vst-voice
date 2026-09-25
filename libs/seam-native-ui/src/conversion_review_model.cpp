#include "seam/native_ui/conversion_review_model.hpp"

namespace seam::native_ui {
namespace {

std::string countLabel(std::size_t count, std::string_view singular,
                       std::string_view plural) {
  return std::to_string(count) + " " +
      std::string{count == 1U ? singular : plural};
}

bool hasIncompleteSingerIdentity(const domain::VocalTrack& track) noexcept {
  if (track.proceduralRecipe.has_value()) {
    const auto& identity = track.proceduralRecipe->resource;
    return identity.id.empty() || identity.version.empty() ||
           identity.contentHash.empty();
  }
  if (track.neuralResource.has_value()) {
    const auto& identity = track.neuralResource->resource;
    return identity.id.empty() || identity.version.empty() ||
           identity.contentHash.empty();
  }
  return track.voicebank.id.empty() || track.voicebank.version.empty() ||
         track.voicebank.contentHash.empty() ||
         track.voicebank.id.starts_with("interchange.unresolved.");
}

}  // namespace

ConversionReviewModel::ConversionReviewModel(
    const authoring::InterchangeImportDraft& draft) noexcept : draft_(draft) {
  for (const auto& item : draft_.issues) {
    if (item.loss) ++losses_;
    else ++warnings_;
  }
  tracks_ = draft_.project.vocalTracks().size() +
            draft_.project.audioTracks().size();
  for (const auto& track : draft_.project.vocalTracks()) {
    regions_ += track.regions.size();
    for (const auto& region : track.regions) notes_ += region.notes.size();
    if (hasIncompleteSingerIdentity(track)) ++unresolvedSingers_;
  }
}

std::string_view ConversionReviewModel::formatName() const noexcept {
  return draft_.format == authoring::InterchangeFormat::Ustx ? "USTX" : "MIDI";
}

std::size_t ConversionReviewModel::issueCount() const noexcept {
  return draft_.issues.size();
}

const authoring::InterchangeIssue* ConversionReviewModel::issue(
    std::size_t index) const noexcept {
  return index < draft_.issues.size() ? &draft_.issues[index] : nullptr;
}

const std::filesystem::path& ConversionReviewModel::sourcePath() const noexcept {
  return draft_.sourcePath;
}

std::string_view ConversionReviewModel::sourceHash() const noexcept {
  return draft_.sourceHash;
}

std::string ConversionReviewModel::summary() const {
  return std::string{formatName()} + " import: " +
         countLabel(tracks_, "track", "tracks") + ", " +
         countLabel(regions_, "vocal region", "vocal regions") + ", " +
         countLabel(notes_, "note", "notes") + "\n" +
         countLabel(losses_, "loss", "losses") + "; " +
         countLabel(warnings_, "warning", "warnings");
}

std::string ConversionReviewModel::singerDisclosure() const {
  return std::to_string(unresolvedSingers_) + " of " +
         std::to_string(draft_.project.vocalTracks().size()) +
         " vocal tracks lack a complete singer identity. Installed singer "
         "availability is not verified by conversion. No singer is "
         "automatically substituted. Select and verify a singer after import "
         "before preview or export.";
}

std::string ConversionReviewModel::sourceDetails() const {
  return "Source: " + draft_.sourcePath.string() + "\nSHA-256: " +
         draft_.sourceHash + "\nProject: " + draft_.project.name();
}

std::string ConversionReviewModel::issueDetails(std::size_t index) const {
  const auto* item = issue(index);
  if (item == nullptr) return {};
  return std::string{item->loss ? "Loss" : "Warning"} + " (" +
         (item->format == authoring::InterchangeFormat::Ustx ? "USTX" : "MIDI") +
         ")\nLocation: " + item->path + "\n\n" + item->message;
}

}  // namespace seam::native_ui
