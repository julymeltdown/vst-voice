#pragma once

#include "seam/domain/project.hpp"
#include "seam/formats/json_value.hpp"

namespace seam::formats::detail {

[[nodiscard]] JsonValue encodeVibrato(const domain::NoteVibrato& vibrato);
[[nodiscard]] core::Result<domain::NoteVibrato> decodeVibrato(const JsonValue* value);
[[nodiscard]] core::Result<std::optional<std::string>> decodePhoneticHint(const JsonValue* value);
[[nodiscard]] JsonValue encodeDynamics(const domain::DynamicsAutomation& dynamics);
[[nodiscard]] core::Result<domain::DynamicsAutomation> decodeDynamics(const JsonValue* value);
[[nodiscard]] JsonValue encodeFormant(const domain::FormantAutomation& formant);
[[nodiscard]] JsonValue encodeBreathiness(const domain::BreathinessAutomation& breathiness);
[[nodiscard]] core::Result<domain::FormantAutomation> decodeFormant(const JsonValue* value);
[[nodiscard]] core::Result<domain::BreathinessAutomation> decodeBreathiness(const JsonValue* value);
[[nodiscard]] JsonValue encodeTension(const domain::TensionAutomation& tension);
[[nodiscard]] core::Result<domain::TensionAutomation> decodeTension(const JsonValue* value);
[[nodiscard]] JsonValue encodeAiriness(const domain::AirinessAutomation& airiness);
[[nodiscard]] core::Result<domain::AirinessAutomation> decodeAiriness(const JsonValue* value);
[[nodiscard]] JsonValue encodeStyleSelection(const domain::VoiceStyleSelection& selection);
[[nodiscard]] core::Result<domain::VoiceStyleSelection> decodeStyleSelection(const JsonValue* value);

}
