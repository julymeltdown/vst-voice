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
  std::uint8_t midiKey{60};
  bool selected{false};
  std::string lyric;
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

  void setViewport(PianoRollViewport viewport) noexcept { viewport_ = viewport; }
  [[nodiscard]] const PianoRollViewport& viewport() const noexcept { return viewport_; }

  void rebuildIndex();
  [[nodiscard]] std::vector<NoteVisual> visibleNotes() const;
  [[nodiscard]] std::optional<domain::NoteId> hitTest(Point point) const;
  [[nodiscard]] std::vector<domain::NoteId> notesInBox(Rect box) const;
  void selectInBox(Rect box, bool additive = false);

  [[nodiscard]] core::Result<domain::NoteId> drawNote(
      Point point,
      time::Tick duration,
      std::u32string lyric = U"a");
  [[nodiscard]] core::Result<void> moveSelection(
      time::Tick deltaTick,
      std::int32_t deltaSemitones);
  [[nodiscard]] core::Result<void> resizeSelection(
      time::Tick deltaStart,
      time::Tick deltaEnd);
  [[nodiscard]] core::Result<void> deleteSelection();

private:
  [[nodiscard]] const domain::VocalRegion* region() const noexcept;
  [[nodiscard]] domain::VocalRegion* region() noexcept;
  [[nodiscard]] Rect noteBounds(const IndexedNote& indexed) const noexcept;

  application::EditorSession& session_;
  application::ProjectFactory& factory_;
  domain::RegionId regionId_;
  PianoRollViewport viewport_{{0.0, 0.0, 1280.0, 720.0}, 72.0};
  TimelineTransform timeline_;
  PitchTransform pitch_{18.0, 84};
  NoteSpatialIndex index_;
};

}  // namespace seam::ui
