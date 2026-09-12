#pragma once

#include "seam/voice_design/recipe_resource.hpp"

namespace seam::native_ui {

// Owner-thread draft state only. No producer/source approval, song mutation or IO.
class VoiceDesignerModel final {
public:
  [[nodiscard]] static core::Result<VoiceDesignerModel> create(voice_design::VoiceRecipe recipe);
  [[nodiscard]] const voice_design::VoiceRecipe& recipe() const noexcept { return current_.recipe; }
  [[nodiscard]] const synthesis::ProceduralSingerResource& resource() const noexcept { return current_.resource; }
  [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
  [[nodiscard]] bool dirty() const noexcept { return savedHash_ != current_.resource.identity.contentHash; }
  [[nodiscard]] bool canUndo() const noexcept { return !gesture_ && !undo_.empty(); }
  [[nodiscard]] bool canRedo() const noexcept { return !gesture_ && !redo_.empty(); }
  [[nodiscard]] bool gestureActive() const noexcept { return gesture_.has_value(); }
  // A slider gesture can submit its final recipe as one edit/undo group. No-op
  // edits preserve revision and redo. Recipe IDs/engine identity are not editable.
  [[nodiscard]] core::Result<void> edit(std::uint64_t expectedRevision, voice_design::VoiceRecipe desired);
  [[nodiscard]] core::Result<void> beginGesture(std::uint64_t expectedRevision);
  [[nodiscard]] core::Result<void> updateGesture(std::uint64_t expectedRevision, voice_design::VoiceRecipe desired);
  [[nodiscard]] core::Result<void> commitGesture(std::uint64_t expectedRevision);
  [[nodiscard]] core::Result<void> cancelGesture(std::uint64_t expectedRevision);
  [[nodiscard]] core::Result<void> undo(std::uint64_t expectedRevision);
  [[nodiscard]] core::Result<void> redo(std::uint64_t expectedRevision);
  // Invoke only after successful external persistence of this exact snapshot.
  [[nodiscard]] core::Result<void> acknowledgeSave(std::uint64_t expectedRevision, std::string_view persistedHash);
private:
  struct Snapshot final { voice_design::VoiceRecipe recipe; synthesis::ProceduralSingerResource resource; };
  explicit VoiceDesignerModel(Snapshot snapshot) : current_(std::move(snapshot)) {}
  [[nodiscard]] core::Result<void> checkRevision(std::uint64_t expected) const;
  [[nodiscard]] core::Result<void> apply(std::uint64_t expected, voice_design::VoiceRecipe desired, bool preview);
  [[nodiscard]] core::Result<void> restore(std::uint64_t expected, std::vector<Snapshot>& from, std::vector<Snapshot>& to);
  static constexpr std::size_t kHistoryLimit = 128U;
  Snapshot current_;
  std::optional<Snapshot> gesture_;
  std::uint64_t revision_{0U};
  std::string savedHash_;
  std::vector<Snapshot> undo_, redo_;
};

} // namespace seam::native_ui
