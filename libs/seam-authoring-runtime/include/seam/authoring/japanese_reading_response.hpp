#pragma once
#include "seam/phonemizer/japanese_reading.hpp"

namespace seam::authoring {
// The caller must establish engine/dictionary identity from a verified resource
// and bind this response to its own request. JSON is not resource attestation.
[[nodiscard]] core::Result<phonemizer::JapaneseReadingResult> decodeJapaneseReadingResponse(
    std::string_view source, std::string_view response,
    const phonemizer::JapaneseReadingIdentity& verifiedIdentity, std::stop_token stop = {});
}
