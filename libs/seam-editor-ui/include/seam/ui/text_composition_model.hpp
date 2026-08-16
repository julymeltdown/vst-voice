#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/ids.hpp"

#include <cstddef>
#include <string>

namespace seam::ui {

struct CompositionSelection final {
  std::size_t start{0};
  std::size_t length{0};

  friend bool operator==(const CompositionSelection&, const CompositionSelection&) = default;
};

struct TextCommit final {
  domain::LyricTokenId lyricId;
  std::u32string text;
};

class TextCompositionModel final {
public:
  [[nodiscard]] core::Result<void> begin(domain::LyricTokenId lyricId,
                                          std::u32string currentText);
  [[nodiscard]] core::Result<void> update(std::u32string composition,
                                          CompositionSelection selection);
  [[nodiscard]] core::Result<TextCommit> commit();
  void cancel() noexcept;

  [[nodiscard]] bool active() const noexcept { return active_; }
  [[nodiscard]] domain::LyricTokenId lyricId() const noexcept { return lyricId_; }
  [[nodiscard]] const std::u32string& originalText() const noexcept { return originalText_; }
  [[nodiscard]] const std::u32string& compositionText() const noexcept {
    return compositionText_;
  }
  [[nodiscard]] CompositionSelection selection() const noexcept { return selection_; }

private:
  bool active_{false};
  domain::LyricTokenId lyricId_;
  std::u32string originalText_;
  std::u32string compositionText_;
  CompositionSelection selection_;
};

}  // namespace seam::ui
