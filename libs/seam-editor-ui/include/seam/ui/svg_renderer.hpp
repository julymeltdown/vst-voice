#pragma once

#include "seam/ui/piano_roll_model.hpp"

#include <filesystem>
#include <string>

namespace seam::ui {

struct EditorRenderStats final {
  std::size_t totalNotes{0};
  std::size_t visibleNotes{0};
  double buildMilliseconds{0.0};
  double renderMilliseconds{0.0};
};

class SvgEditorRenderer final {
public:
  [[nodiscard]] core::Result<EditorRenderStats> render(
      const PianoRollModel& model,
      const std::filesystem::path& output,
      std::string_view projectName,
      std::uint64_t revision) const;

private:
  [[nodiscard]] static std::string escape(std::string_view text);
};

}  // namespace seam::ui
