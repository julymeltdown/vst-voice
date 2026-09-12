#pragma once

#include "seam/domain/project.hpp"
#include "seam/formats/json_value.hpp"

namespace seam::formats::detail {

[[nodiscard]] JsonValue encodeVibrato(const domain::NoteVibrato& vibrato);
[[nodiscard]] core::Result<domain::NoteVibrato> decodeVibrato(const JsonValue* value);
[[nodiscard]] core::Result<std::optional<std::string>> decodePhoneticHint(const JsonValue* value);
[[nodiscard]] JsonValue encodeDynamics(const domain::DynamicsAutomation& dynamics);
[[nodiscard]] core::Result<domain::DynamicsAutomation> decodeDynamics(const JsonValue* value);
[[nodiscard]] JsonValue encodeStyleSelection(const domain::VoiceStyleSelection& selection);
[[nodiscard]] core::Result<domain::VoiceStyleSelection> decodeStyleSelection(const JsonValue* value);

}
