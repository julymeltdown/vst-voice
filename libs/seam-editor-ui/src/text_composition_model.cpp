#include "seam/ui/text_composition_model.hpp"

namespace seam::ui {

core::Result<void> TextCompositionModel::begin(domain::LyricTokenId lyricId,
                                                std::u32string currentText) {
  if (!lyricId.valid()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Text composition requires a valid lyric ID");
  }
  if (active_) {
    return core::failure(core::ErrorCode::Conflict,
                         "Another text composition is already active");
  }
  active_ = true;
  lyricId_ = lyricId;
  originalText_ = currentText;
  compositionText_ = std::move(currentText);
  selection_ = CompositionSelection{compositionText_.size(), 0};
  return core::success();
}

core::Result<void> TextCompositionModel::update(
    std::u32string composition, CompositionSelection selection) {
  if (!active_) {
    return core::failure(core::ErrorCode::Conflict,
                         "Text composition is not active");
  }
  if (selection.start > composition.size() ||
      selection.length > composition.size() - selection.start) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "IME composition selection is outside the text range");
  }
  compositionText_ = std::move(composition);
  selection_ = selection;
  return core::success();
}

core::Result<TextCommit> TextCompositionModel::commit() {
  if (!active_) {
    return core::failure<TextCommit>(core::ErrorCode::Conflict,
                                     "Text composition is not active");
  }
  if (compositionText_.empty()) {
    return core::failure<TextCommit>(core::ErrorCode::InvalidArgument,
                                     "Committed lyric text must not be empty");
  }
  TextCommit commitValue{lyricId_, compositionText_};
  cancel();
  return core::success(std::move(commitValue));
}

void TextCompositionModel::cancel() noexcept {
  active_ = false;
  lyricId_ = {};
  originalText_.clear();
  compositionText_.clear();
  selection_ = {};
}

}  // namespace seam::ui
