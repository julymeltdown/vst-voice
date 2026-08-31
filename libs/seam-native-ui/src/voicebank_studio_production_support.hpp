#pragma once

#include "seam/voicebank/voicebank.hpp"

#include <map>
#include <string>

namespace seam::native_ui::voicebank_studio_internal {

[[nodiscard]] std::string coverageKey(const voicebank::Unit& unit);
[[nodiscard]] std::string currentUtcTimestamp();
[[nodiscard]] std::map<std::string, std::string, std::less<>> metadataValues(
    const voicebank::Unit& unit);

}
