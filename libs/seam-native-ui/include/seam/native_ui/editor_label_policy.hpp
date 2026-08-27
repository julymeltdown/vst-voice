#pragma once

#include <string>
#include <string_view>

namespace seam::native_ui {

enum class EditorLabelMode { Hidden, Compact, Full };

struct EditorLabel final {
  EditorLabelMode mode{EditorLabelMode::Hidden};
  std::string text;
  std::string fullText;
};

class EditorLabelPolicy final {
public:
  [[nodiscard]] static EditorLabel note(std::string_view value,
                                        double availableWidth) noexcept;
  [[nodiscard]] static EditorLabel technical(std::string_view value,
                                             double availableWidth,
                                             double minimumWidth,
                                             double fullWidth) noexcept;
};

}
