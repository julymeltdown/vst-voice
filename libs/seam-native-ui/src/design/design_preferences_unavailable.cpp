#include "seam/native_ui/design/sing_shell.hpp"

namespace seam::native_ui::design {

// Platforms without the vector backend keep the classic editor; nothing is persisted.
DesignPreferences loadDesignPreferences() { return DesignPreferences{.shellEnabled = false}; }
void saveDesignPreferences(const DesignPreferences&) {}

}  // namespace seam::native_ui::design
