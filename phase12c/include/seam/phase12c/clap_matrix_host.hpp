#pragma once
#include "seam/phase12c/canonical_evidence.hpp"
#include "seam/formats/json_value.hpp"
#include <filesystem>

namespace seam::phase12c {
// Loads the module; no live engine/editor runtime is linked into this host.
[[nodiscard]] formats::JsonValue runClapProcessMatrix(
    const std::filesystem::path& plugin, const std::filesystem::path& bank,
    const CanonicalEvidenceIdentity& identity, bool developmentFixture);
}
