#pragma once

#include "seam/formats/json_value.hpp"
#include "seam/native_ui/accessibility_tree.hpp"
#include "seam/native_ui/design/sing_shell.hpp"

#include <cstddef>

namespace seam::native_ui::design {

// Evidence for the UI-fidelity packet (docs/design/SEAM_UI_FIDELITY_REVIEW_2026-09-25.md §11.2),
// read from the same snapshot the shell painted, hit-tested and published: the SingLayout of the
// presented frame and the accessibility tree built from it. Nothing here is handwritten metadata.

// geometry.json: every canonical region of ui-fidelity-contract-v1.json plus the controls inside
// them, in logical points, with the presentation that produced them.
[[nodiscard]] formats::JsonValue singLayoutEvidence(const SingShell& shell, double deviceScale);

// semantic-bounds.json: every published accessibility node (flattened, depth-first) with its role,
// state and bounds, the focused id, and up to noteLimit virtualized notes.
[[nodiscard]] formats::JsonValue semanticEvidence(const AccessibilityTree& tree,
                                                  std::size_t noteLimit = 256U);

}  // namespace seam::native_ui::design

