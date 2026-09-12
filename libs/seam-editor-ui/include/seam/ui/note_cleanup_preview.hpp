#pragma once

#include "seam/application/editor_session.hpp"
#include "seam/application/note_commands.hpp"
#include "seam/ui/dependent_edit_review.hpp"
#include <algorithm>
#include <span>
#include <stop_token>
#include <unordered_set>
#include <unordered_map>

namespace seam::ui {
enum class NoteCleanupKind { RemoveOverlap, CloseGap, AutoLegato };
enum class NoteCleanupOutcome {
  Changed, NotNeeded, NoSuccessor, UnselectedNeighbor, SimultaneousStarts,
  OffGridSuccessor, Staccato, NonPositiveDuration, ExplicitSeparation, ExistingOverlap,
};
inline const char* noteCleanupOutcomeName(NoteCleanupOutcome outcome) noexcept {
  switch (outcome) {
    case NoteCleanupOutcome::Changed: return "changed";
    case NoteCleanupOutcome::NotNeeded: return "not needed";
    case NoteCleanupOutcome::NoSuccessor: return "no successor";
    case NoteCleanupOutcome::UnselectedNeighbor: return "unselected neighbor";
    case NoteCleanupOutcome::SimultaneousStarts: return "ambiguous simultaneous starts";
    case NoteCleanupOutcome::OffGridSuccessor: return "successor off grid";
    case NoteCleanupOutcome::Staccato: return "staccato preserved";
    case NoteCleanupOutcome::NonPositiveDuration: return "snap would erase note";
    case NoteCleanupOutcome::ExplicitSeparation: return "long gap or separate slur groups";
    case NoteCleanupOutcome::ExistingOverlap: return "overlap preserved; normalize first";
  }
  return "unknown";
}
struct NoteCleanupRow final {
  domain::NoteId noteId;
  std::optional<domain::NoteId> successor;
  time::Tick beforeDuration, afterDuration;
  NoteCleanupOutcome outcome{NoteCleanupOutcome::NotNeeded};
  domain::NoteArticulation beforeArticulation{domain::NoteArticulation::Normal};
  domain::NoteArticulation afterArticulation{domain::NoteArticulation::Normal};
};

// Bounded cleanup. Explicit policy skips ambiguous/polyphonic adjacency,
// never moves note starts and never crosses an unselected intervening note.
class NoteCleanupPreview final {
public:
  enum class State { Ready, Applied, Cancelled };
  [[nodiscard]] static core::Result<NoteCleanupPreview> prepare(
      const application::EditorSession& session, domain::RegionId regionId,
      NoteCleanupKind kind, std::stop_token stop = {}) {
    if (stop.stop_requested()) return core::failure<NoteCleanupPreview>(core::ErrorCode::Conflict, "Note cleanup cancelled");
    const auto* region = session.project().findRegion(regionId);
    auto selected = session.selection().noteIds();
    const auto grid = session.project().settings().snapGrid.value();
    if (!region || selected.empty() || selected.size() > 10000U || region->notes.size() > 10000U || grid <= 0 ||
        (kind != NoteCleanupKind::RemoveOverlap && kind != NoteCleanupKind::CloseGap && kind != NoteCleanupKind::AutoLegato))
      return core::failure<NoteCleanupPreview>(core::ErrorCode::InvalidArgument, "Note cleanup needs a valid grid and 1 to 10000 selected region notes");
    const auto dependencyCount = dependencyReviewRecordCount(*region);
    if (!dependencyCount) return core::Result<NoteCleanupPreview>{dependencyCount.error()};
    auto context = session.capturePerformanceJob(); if (!context) return core::Result<NoteCleanupPreview>{context.error()};
    std::sort(selected.begin(), selected.end());
    NoteCleanupPreview preview{std::move(context.value()), regionId, session.revision(), std::move(selected), kind};
    const std::unordered_set<domain::NoteId> ids(preview.selected_.begin(), preview.selected_.end());
    std::vector<const domain::Note*> notes;
    std::unordered_map<domain::NoteId, const domain::Note*> sourceNotes;
    std::unordered_set<domain::NoteId> legatoNotes;
    for (const auto& note : preview.context_.sourceProject().findRegion(regionId)->notes) notes.push_back(&note);
    for (const auto* note : notes) sourceNotes.emplace(note->id, note);
    std::sort(notes.begin(), notes.end(), [](const auto* a, const auto* b) { return a->startTick != b->startTick ? a->startTick < b->startTick : a->id < b->id; });
    std::vector<time::Tick> priorEnds;
    time::Tick latestEnd{0};
    for (const auto* note : notes) { priorEnds.push_back(latestEnd); latestEnd = std::max(latestEnd, note->endTick()); }
    for (std::size_t i = 0U; i < notes.size(); ++i) {
      if (stop.stop_requested()) return core::failure<NoteCleanupPreview>(core::ErrorCode::Conflict, "Note cleanup cancelled");
      const auto& note = *notes[i]; if (!ids.contains(note.id)) continue;
      NoteCleanupRow row{note.id, std::nullopt, note.durationTick, note.durationTick};
      row.beforeArticulation = note.articulation; row.afterArticulation = note.articulation;
      const auto plan = [&] {
        if ((i > 0U && notes[i - 1U]->startTick == note.startTick) ||
            (i + 1U < notes.size() && notes[i + 1U]->startTick == note.startTick)) {
          row.outcome = NoteCleanupOutcome::SimultaneousStarts; return;
        }
        if (i + 1U == notes.size()) { row.outcome = NoteCleanupOutcome::NoSuccessor; return; }
        const auto& next = *notes[i + 1U]; row.successor = next.id;
        if (!ids.contains(next.id)) { row.outcome = NoteCleanupOutcome::UnselectedNeighbor; return; }
        if (i + 2U < notes.size() && notes[i + 2U]->startTick == next.startTick) {
          row.outcome = NoteCleanupOutcome::SimultaneousStarts; return;
        }
        const auto end = note.endTick(); // Captured project validation rejects int64 overflow first.
        if (kind == NoteCleanupKind::AutoLegato) {
          if (priorEnds[i] > note.startTick || priorEnds[i + 1U] > next.startTick) {
            row.outcome = NoteCleanupOutcome::ExistingOverlap; return;
          }
          if (note.articulation == domain::NoteArticulation::Staccato || next.articulation == domain::NoteArticulation::Staccato) {
            row.outcome = NoteCleanupOutcome::Staccato; return;
          }
          if (end > next.startTick) { row.outcome = NoteCleanupOutcome::ExistingOverlap; return; }
          const auto gap = next.startTick.value() - end.value();
          if (gap > grid || note.slurGroup != next.slurGroup) { row.outcome = NoteCleanupOutcome::ExplicitSeparation; return; }
          if (gap > 0 && next.startTick.value() % grid != 0) { row.outcome = NoteCleanupOutcome::OffGridSuccessor; return; }
          legatoNotes.insert(note.id); legatoNotes.insert(next.id);
          if (gap > 0) {
            row.afterDuration = next.startTick - note.startTick; row.outcome = NoteCleanupOutcome::Changed;
            preview.edits_.push_back({note.id, note.startTick, note.durationTick, note.startTick, row.afterDuration});
          }
          return;
        }
        if ((kind == NoteCleanupKind::RemoveOverlap && end <= next.startTick) ||
            (kind == NoteCleanupKind::CloseGap && end >= next.startTick)) return;
        if (kind == NoteCleanupKind::CloseGap && note.articulation == domain::NoteArticulation::Staccato) {
          row.outcome = NoteCleanupOutcome::Staccato; return;
        }
        const auto nextStart = next.startTick.value();
        if (kind == NoteCleanupKind::CloseGap && nextStart % grid != 0) {
          row.outcome = NoteCleanupOutcome::OffGridSuccessor; return;
        }
        // Nonnegative floor via subtraction avoids rounded-up int64 overflow.
        const auto targetEnd = kind == NoteCleanupKind::RemoveOverlap ? nextStart - nextStart % grid : nextStart;
        if (targetEnd <= note.startTick.value()) { row.outcome = NoteCleanupOutcome::NonPositiveDuration; return; }
        row.afterDuration = time::Tick{targetEnd - note.startTick.value()};
        row.outcome = NoteCleanupOutcome::Changed;
        preview.edits_.push_back({note.id, note.startTick, note.durationTick, note.startTick, row.afterDuration});
      };
      plan(); preview.rows_.push_back(row);
    }
    if (preview.rows_.size() != preview.selected_.size())
      return core::failure<NoteCleanupPreview>(core::ErrorCode::Conflict, "All cleanup targets must belong to the active region");
    for (auto& row : preview.rows_) {
      if (legatoNotes.contains(row.noteId)) row.afterArticulation = domain::NoteArticulation::Legato;
      if (row.beforeArticulation != row.afterArticulation) {
        const auto& note = *sourceNotes.at(row.noteId);
        preview.articulations_.push_back({note.id, note.articulation, row.afterArticulation,
            note.slurGroup, note.slurGroup, note.lyricTokenId, note.lyricTokenId});
      }
      if (row.beforeDuration != row.afterDuration || row.beforeArticulation != row.afterArticulation) ++preview.changedNotes_;
    }
    if (stop.stop_requested()) return core::failure<NoteCleanupPreview>(core::ErrorCode::Conflict, "Note cleanup cancelled");
    auto staged = preview.context_.sourceProject();
    if (preview.changedNotes_ > 0U) {
      auto command = preview.command();
      const auto result = command->apply(staged); if (!result) return core::Result<NoteCleanupPreview>{result.error()};
    }
    auto outcomes = compareDependentEdits(*preview.context_.sourceProject().findRegion(regionId), *staged.findRegion(regionId), stop);
    if (!outcomes) return core::Result<NoteCleanupPreview>{outcomes.error()};
    preview.dependencies_ = std::move(outcomes.value());
    if (stop.stop_requested()) return core::failure<NoteCleanupPreview>(core::ErrorCode::Conflict, "Note cleanup cancelled");
    return core::success(std::move(preview));
  }

  [[nodiscard]] std::span<const NoteCleanupRow> rows() const noexcept { return rows_; }
  [[nodiscard]] std::size_t changedCount() const noexcept { return changedNotes_; }
  [[nodiscard]] std::span<const DependentEditOutcome> dependencies() const noexcept { return dependencies_; }
  [[nodiscard]] NoteCleanupKind kind() const noexcept { return kind_; }
  [[nodiscard]] State state() const noexcept { return state_; }
  [[nodiscard]] time::Tick grid() const noexcept { return context_.sourceProject().settings().snapGrid; }
  [[nodiscard]] bool matches(const application::EditorSession& session, domain::RegionId selectedRegion) const {
    auto selected = session.selection().noteIds(); std::sort(selected.begin(), selected.end());
    const auto* region = session.project().findRegion(regionId_);
    return state_ == State::Ready && selectedRegion == regionId_ && session.revision() == revision_ &&
        session.project().id() == context_.sourceProject().id() && selected == selected_ &&
        session.project().settings().snapGrid == grid() && region && *region == *context_.sourceProject().findRegion(regionId_);
  }
  [[nodiscard]] core::Result<void> apply(application::EditorSession& session,
      domain::RegionId selectedRegion, std::stop_token stop = {}) {
    if (stop.stop_requested() || !matches(session, selectedRegion))
      return core::failure(core::ErrorCode::Conflict, "Note cleanup is cancelled, stale or already applied");
    const auto valid = session.validatePerformanceJob(context_); if (!valid) return valid;
    if (stop.stop_requested()) return core::failure(core::ErrorCode::Conflict, "Note cleanup cancelled before commit");
    if (changedNotes_ > 0U) {
      const auto result = session.executePerformanceResult(context_, command());
      if (!result) return result;
    }
    state_ = State::Applied; return core::success();
  }
  void cancel() noexcept { if (state_ == State::Ready) state_ = State::Cancelled; }
private:
  [[nodiscard]] std::unique_ptr<application::ICommand> command() const {
    auto result = std::make_unique<application::CompositeCommand>(kind_ == NoteCleanupKind::AutoLegato ? "Auto legato" : "Clean note durations");
    if (!edits_.empty()) result->add(std::make_unique<application::ResizeNotesCommand>(edits_));
    if (!articulations_.empty()) result->add(std::make_unique<application::SetNotePerformanceCommand>(articulations_));
    return result;
  }
  NoteCleanupPreview(application::PerformanceJobContext context, domain::RegionId regionId, std::uint64_t revision,
      std::vector<domain::NoteId> selected, NoteCleanupKind kind)
      : context_(std::move(context)), regionId_(regionId), revision_(revision), selected_(std::move(selected)), kind_(kind) {}
  application::PerformanceJobContext context_;
  domain::RegionId regionId_;
  std::uint64_t revision_;
  std::vector<domain::NoteId> selected_;
  NoteCleanupKind kind_;
  std::vector<NoteCleanupRow> rows_;
  std::vector<application::NoteResize> edits_;
  std::vector<application::NotePerformanceEdit> articulations_;
  std::size_t changedNotes_{0U};
  std::vector<DependentEditOutcome> dependencies_;
  State state_{State::Ready};
};
}
