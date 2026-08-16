#pragma once

#include "seam/domain/ids.hpp"

#include <unordered_set>
#include <vector>

namespace seam::application {

class SelectionModel final {
public:
  void clear() noexcept { notes_.clear(); }
  void replace(const std::vector<domain::NoteId>& noteIds) {
    notes_.clear();
    for (const auto noteId : noteIds) {
      if (noteId.valid()) {
        notes_.insert(noteId);
      }
    }
  }
  void selectOnly(domain::NoteId noteId) {
    notes_.clear();
    if (noteId.valid()) {
      notes_.insert(noteId);
    }
  }
  void add(domain::NoteId noteId) {
    if (noteId.valid()) {
      notes_.insert(noteId);
    }
  }
  void remove(domain::NoteId noteId) { notes_.erase(noteId); }
  void toggle(domain::NoteId noteId) {
    if (notes_.contains(noteId)) {
      notes_.erase(noteId);
    } else if (noteId.valid()) {
      notes_.insert(noteId);
    }
  }
  [[nodiscard]] bool contains(domain::NoteId noteId) const noexcept {
    return notes_.contains(noteId);
  }
  [[nodiscard]] bool empty() const noexcept { return notes_.empty(); }
  [[nodiscard]] std::size_t size() const noexcept { return notes_.size(); }
  [[nodiscard]] std::vector<domain::NoteId> noteIds() const {
    return {notes_.begin(), notes_.end()};
  }

private:
  std::unordered_set<domain::NoteId> notes_;
};

}  // namespace seam::application
