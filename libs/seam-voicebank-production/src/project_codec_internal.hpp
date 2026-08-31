#pragma once

#include "seam/core/result.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank_production/project.hpp"

#include <string_view>

namespace seam::voicebank_production::codec_internal {

[[nodiscard]] bool readString(const formats::JsonValue& object,
                              std::string_view key, std::string& output);
[[nodiscard]] bool readInteger(const formats::JsonValue& object,
                               std::string_view key, std::int64_t& output);
[[nodiscard]] bool readBool(const formats::JsonValue& object,
                            std::string_view key, bool& output);
[[nodiscard]] core::Result<SourceStrategyAssessment> decodeStrategy(
    const formats::JsonValue& value);
[[nodiscard]] core::Result<AssetRecord> decodeAsset(
    const formats::JsonValue& value);
[[nodiscard]] core::Result<DerivedRevision> decodeRevision(
    const formats::JsonValue& value);
[[nodiscard]] core::Result<MetadataRevision> decodeMetadataRevision(
    const formats::JsonValue& value);
[[nodiscard]] core::Result<TakeRecord> decodeTake(
    const formats::JsonValue& value);
[[nodiscard]] core::Result<UnitAssignment> decodeAssignment(
    const formats::JsonValue& value);

}
