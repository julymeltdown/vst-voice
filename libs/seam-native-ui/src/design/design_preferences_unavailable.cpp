#include "seam/native_ui/design/sing_shell.hpp"

namespace seam::native_ui::design {

// Platforms without the vector backend cannot present the shell; nothing is persisted.
DesignPreferences loadDesignPreferences() { return DesignPreferences{}; }
void saveDesignPreferences(const DesignPreferences&) {}

}  // namespace seam::native_ui::design
