#include "seam/native_ui/design/sing_shell.hpp"

namespace seam::native_ui::design {

// Platforms without the vector backend cannot present the shell; nothing is persisted.
DesignPreferences loadDesignPreferences() { return DesignPreferences{}; }
void saveDesignPreferences(const DesignPreferences&) {}
bool systemIncreaseContrast() { return false; }
bool systemReduceMotion() { return false; }
std::string systemPreferredLanguage() { return {}; }
std::shared_ptr<void> observeSystemDisplayOptions(std::function<void()>) { return {}; }
void postSystemDisplayOptionsChanged() {}

}  // namespace seam::native_ui::design
