#pragma once

#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/time/quantizer.hpp"
#include "seam/ui/geometry.hpp"
#include "seam/ui/note_spatial_index.hpp"
#include "seam/ui/timeline_transform.hpp"

#include <optional>
#include <string>
#include <vector>

namespace seam::ui {

struct NoteVisual final {
  domain::NoteId noteId;
  Rect bounds;
  Rect timelineBounds;
  Rect hitBounds;
  std::uint8_t midiKey{60};
  time::Tick absoluteStart;
  time::Tick duration;
  bool selected{false};
  std::size_t overlapGroup{0U};
  std::size_t overlapMemberCount{1U};
  std::size_t overlapBand{0U};
  std::size_t visibleOverlapBands{1U};
  std::size_t hiddenOverlapMembers{0U};
  bool hiddenByOverlapDensity{false};
  bool drawsOverlapIndicator{false};
  std::string lyric;
};

struct LyricDistributionReport final {
  std::size_t requestedSyllables{0U};
  std::size_t targetNotes{0U};
  std::size_t appliedSyllables{0U};
  std::size_t missingSyllables{0U};
  std::size_t leftoverSyllables{0U};
  bool committed{false};
};

struct PianoRollViewport final {
  Rect bounds;
  double keyboardWidth{72.0};
};

class PianoRollModel final {
public:
  PianoRollModel(application::EditorSession& session,
                 application::ProjectFactory& factory,
                 domain::RegionId regionId);

  [[nodiscard]] TimelineTransform& timeline() noexcept { return timeline_; }
  [[nodiscard]] PitchTransform& pitch() noexcept { return pitch_; }
  [[nodiscard]] const TimelineTransform& timeline() const noexcept { return timeline_; }
  [[nodiscard]] const PitchTransform& pitch() const noexcept { return pitch_; }

  void setRegionId(domain::RegionId regionId) noexcept { regionId_ = regionId; }

  void setViewport(PianoRollViewport viewport) noexcept { viewport_ = viewport; }
  [[nodiscard]] const PianoRollViewport& viewport() const noexcept { return viewport_; }
  [[nodiscard]] const domain::Project& project() const noexcept { return session_.project(); }
  [[nodiscard]] domain::RegionId regionId() const noexcept { return regionId_; }
  [[nodiscard]] double pixelAtMicrosecondOffset(
      time::Tick absoluteStart, time::Microseconds offset) const noexcept;

  void rebuildIndex();
  [[nodiscard]] std::vector<NoteVisual> visibleNotes() const;
  [[nodiscard]] std::vector<NoteVisual> allNotes() const;
  [[nodiscard]] std::size_t noteCount() const noexcept;
  [[nodiscard]] std::optional<NoteVisual> noteAt(std::size_t index) const;
  [[nodiscard]] std::optional<domain::NoteId> hitTest(Point point) const;
  [[nodiscard]] std::vector<domain::NoteId> overlapCandidatesAt(
      Point point) const;
  [[nodiscard]] std::vector<domain::NoteId> notesInBox(Rect box) const;
  void selectInBox(Rect box, bool additive = false);

  [[nodiscard]] core::Result<domain::NoteId> drawNote(
      Point point,
      time::Tick duration,
      std::u32string lyric = U"あ");
  [[nodiscard]] core::Result<void> moveSelection(
      time::Tick deltaTick,
      std::int32_t deltaSemitones);
  [[nodiscard]] core::Result<void> resizeSelection(
      time::Tick deltaStart,
      time::Tick deltaEnd);
  [[nodiscard]] core::Result<void> quantizeSelection(time::Tick grid);
  [[nodiscard]] core::Result<void> setSelectionSlur(bool enabled);
  [[nodiscard]] core::Result<void> setSelectionMelisma();
  [[nodiscard]] core::Result<void> deleteSelection();
  [[nodiscard]] core::Result<domain::NoteId> duplicateSelection();
  [[nodiscard]] core::Result<LyricDistributionReport> distributeSelectedLyrics(
      std::u32string text,
      domain::Language language = domain::Language::Unspecified);

private:
  [[nodiscard]] const domain::VocalRegion* region() const noexcept;
  [[nodiscard]] domain::VocalRegion* region() noexcept;
  [[nodiscard]] Rect noteBounds(const IndexedNote& indexed) const noexcept;
  [[nodiscard]] NoteVisual makeNoteVisual(const IndexedNote& indexed) const;

  application::EditorSession& session_;
  application::ProjectFactory& factory_;
  domain::RegionId regionId_;
  PianoRollViewport viewport_{{0.0, 0.0, 1280.0, 720.0}, 72.0};
  TimelineTransform timeline_;
  PitchTransform pitch_{18.0, 84};
  NoteSpatialIndex index_;
};

}  // namespace seam::ui
