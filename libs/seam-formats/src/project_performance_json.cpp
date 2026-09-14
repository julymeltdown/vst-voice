#include "project_performance_json.hpp"

#include <array>
#include <cmath>

namespace seam::formats::detail {
namespace {

core::Result<double> boundedNumber(const JsonValue* value, double minimum,
                                    double maximum) {
  if (value == nullptr || !value->isNumber() ||
      !std::isfinite(value->asNumber()) ||
      value->asNumber() < minimum || value->asNumber() > maximum) {
    return core::failure<double>(core::ErrorCode::ParseError,
                                 "Raw performance value is outside its finite bounds");
  }
  return value->asNumber();
}

std::string_view styleOriginName(domain::VoiceStyleOrigin origin) {
  switch (origin) {
    case domain::VoiceStyleOrigin::Unselected: return "unselected";
    case domain::VoiceStyleOrigin::Explicit: return "explicit";
    case domain::VoiceStyleOrigin::SoleDeclaredStyle: return "sole-declared-style";
    case domain::VoiceStyleOrigin::LegacyNeedsExactBankResolution:
      return "legacy-needs-exact-bank-resolution";
    case domain::VoiceStyleOrigin::LegacyManifestFirst: return "legacy-manifest-first";
  }
  return {};
}

}

JsonValue encodeVibrato(const domain::NoteVibrato& vibrato) {
  return JsonValue::Object{
      {"enabled", JsonValue{vibrato.enabled}},
      {"startFraction", JsonValue{vibrato.startFraction}},
      {"fadeInFraction", JsonValue{vibrato.fadeInFraction}},
      {"fadeOutFraction", JsonValue{vibrato.fadeOutFraction}},
      {"depthCents", JsonValue{vibrato.depthCents}},
      {"periodMilliseconds", JsonValue{vibrato.periodMilliseconds}},
      {"phaseTurns", JsonValue{vibrato.phaseTurns}},
  };
}

core::Result<domain::NoteVibrato> decodeVibrato(const JsonValue* value) {
  if (value == nullptr || !value->isObject() || value->asObject().size() != 7U) {
    return core::failure<domain::NoteVibrato>(core::ErrorCode::ParseError,
        "Schema 8 note requires a complete vibrato object");
  }
  const auto* enabled = value->find("enabled");
  if (enabled == nullptr || !enabled->isBool()) {
    return core::failure<domain::NoteVibrato>(core::ErrorCode::ParseError,
                                            "Vibrato enabled must be boolean");
  }
  domain::NoteVibrato result;
  result.enabled = enabled->asBool();
  struct Field final {
    const char* name;
    float domain::NoteVibrato::*member;
    double minimum;
    double maximum;
  };
  constexpr std::array fields{
      Field{"startFraction", &domain::NoteVibrato::startFraction, 0.0, 1.0},
      Field{"fadeInFraction", &domain::NoteVibrato::fadeInFraction, 0.0, 1.0},
      Field{"fadeOutFraction", &domain::NoteVibrato::fadeOutFraction, 0.0, 1.0},
      Field{"depthCents", &domain::NoteVibrato::depthCents, 0.0, 200.0},
      Field{"periodMilliseconds", &domain::NoteVibrato::periodMilliseconds, 5.0, 500.0},
      Field{"phaseTurns", &domain::NoteVibrato::phaseTurns, 0.0, 1.0},
  };
  std::array<double, fields.size()> raw{};
  for (std::size_t index = 0U; index < fields.size(); ++index) {
    const auto& field = fields[index];
    const auto parsed = boundedNumber(value->find(field.name), field.minimum, field.maximum);
    if (!parsed) return core::Result<domain::NoteVibrato>{parsed.error()};
    raw[index] = parsed.value();
  }
  if (raw[1] + raw[2] > 1.0) {
    const auto fadeIn = static_cast<float>(raw[1]);
    const auto fadeOut = static_cast<float>(raw[2]);
    // Preserve exact stored-float pairs accepted by the existing float-sum
    // contract; decimal rounding must not turn a raw overshoot into valid fades.
    if (raw[1] != static_cast<double>(fadeIn) ||
        raw[2] != static_cast<double>(fadeOut) || fadeIn + fadeOut > 1.0F) {
      return core::failure<domain::NoteVibrato>(core::ErrorCode::ParseError,
                                              "Raw vibrato fade fractions exceed one");
    }
  }
  if (raw[5] >= 1.0) {
    return core::failure<domain::NoteVibrato>(core::ErrorCode::ParseError,
                                            "Raw vibrato phase must be less than one turn");
  }
  for (std::size_t index = 0U; index < fields.size(); ++index) {
    result.*fields[index].member = static_cast<float>(raw[index]);
  }
  const auto validation = result.validate();
  if (!validation) return core::Result<domain::NoteVibrato>{validation.error()};
  return result;
}

core::Result<std::optional<std::string>> decodePhoneticHint(const JsonValue* value) {
  if (value == nullptr) {
    return core::failure<std::optional<std::string>>(core::ErrorCode::ParseError,
        "Schema 8 note requires phoneticHint, using null when unset");
  }
  if (value->isNull()) return std::optional<std::string>{};
  if (!value->isString() || value->asString().empty() ||
      value->asString().size() > 4096U || !domain::fromUtf8(value->asString())) {
    return core::failure<std::optional<std::string>>(core::ErrorCode::ParseError,
        "Phonetic hint must be nonempty bounded UTF-8 or null");
  }
  return std::optional<std::string>{value->asString()};
}

JsonValue encodeDynamics(const domain::DynamicsAutomation& dynamics) {
  JsonValue::Array points;
  points.reserve(dynamics.points().size());
  for (const auto& point : dynamics.points()) {
    points.emplace_back(JsonValue::Object{
        {"tick", JsonValue{point.tick.value()}},
        {"linearGain", JsonValue{point.linearGain}},
    });
  }
  return JsonValue{std::move(points)};
}

core::Result<domain::DynamicsAutomation> decodeDynamics(const JsonValue* value) {
  if (value == nullptr || !value->isArray() ||
      value->asArray().size() > domain::kMaximumDynamicsPoints) {
    return core::failure<domain::DynamicsAutomation>(core::ErrorCode::ParseError,
        "Schema 8 region requires bounded dynamicsAutomation points");
  }
  std::vector<domain::DynamicsAutomationPoint> points;
  points.reserve(value->asArray().size());
  for (const auto& point : value->asArray()) {
    if (!point.isObject() || point.asObject().size() != 2U) {
      return core::failure<domain::DynamicsAutomation>(core::ErrorCode::ParseError,
          "Dynamics point requires tick and linearGain");
    }
    const auto* tick = point.find("tick");
    const auto gain = boundedNumber(point.find("linearGain"), 0.0,
                                    static_cast<double>(domain::kMaximumDynamicsGain));
    if (tick == nullptr || !tick->isInteger()) {
      return core::failure<domain::DynamicsAutomation>(core::ErrorCode::ParseError,
                                                      "Dynamics tick must be an integer");
    }
    if (!gain) return core::Result<domain::DynamicsAutomation>{gain.error()};
    points.push_back({.tick = time::Tick{tick->asInt64()},
                      .linearGain = static_cast<float>(gain.value())});
  }
  domain::DynamicsAutomation result;
  const auto validation = result.replacePoints(std::move(points));
  if (!validation) return core::Result<domain::DynamicsAutomation>{validation.error()};
  return result;
}

JsonValue encodeFormant(const domain::FormantAutomation& formant) {
  JsonValue::Array points;
  points.reserve(formant.points().size());
  for (const auto& point : formant.points()) {
    points.emplace_back(JsonValue::Object{
        {"tick", JsonValue{point.tick.value()}},
        {"semitones", JsonValue{point.semitones}},
    });
  }
  return JsonValue{std::move(points)};
}

core::Result<domain::FormantAutomation> decodeFormant(const JsonValue* value) {
  if (value == nullptr || !value->isArray() ||
      value->asArray().size() > domain::kMaximumFormantPoints) {
    return core::failure<domain::FormantAutomation>(core::ErrorCode::ParseError,
        "Schema 12 region requires bounded formantAutomation points");
  }
  std::vector<domain::FormantAutomationPoint> points;
  points.reserve(value->asArray().size());
  for (const auto& point : value->asArray()) {
    if (!point.isObject() || point.asObject().size() != 2U) {
      return core::failure<domain::FormantAutomation>(core::ErrorCode::ParseError,
          "Formant point requires tick and semitones");
    }
    const auto* tick = point.find("tick");
    const auto shift = boundedNumber(point.find("semitones"),
        -static_cast<double>(domain::kMaximumFormantShiftSemitones),
        static_cast<double>(domain::kMaximumFormantShiftSemitones));
    if (tick == nullptr || !tick->isInteger()) {
      return core::failure<domain::FormantAutomation>(core::ErrorCode::ParseError,
                                                      "Formant tick must be an integer");
    }
    if (!shift) return core::Result<domain::FormantAutomation>{shift.error()};
    points.push_back({.tick = time::Tick{tick->asInt64()},
                      .semitones = static_cast<float>(shift.value())});
  }
  domain::FormantAutomation result;
  const auto validation = result.replacePoints(std::move(points));
  if (!validation) return core::Result<domain::FormantAutomation>{validation.error()};
  return result;
}

JsonValue encodeBreathiness(const domain::BreathinessAutomation& breathiness) {
  JsonValue::Array points;
  points.reserve(breathiness.points().size());
  for (const auto& point : breathiness.points()) {
    points.emplace_back(JsonValue::Object{
        {"tick", JsonValue{point.tick.value()}},
        {"amount", JsonValue{point.amount}},
    });
  }
  return JsonValue{std::move(points)};
}

core::Result<domain::BreathinessAutomation> decodeBreathiness(const JsonValue* value) {
  if (value == nullptr || !value->isArray() ||
      value->asArray().size() > domain::kMaximumBreathinessPoints) {
    return core::failure<domain::BreathinessAutomation>(core::ErrorCode::ParseError,
        "Schema 13 region requires bounded breathinessAutomation points");
  }
  std::vector<domain::BreathinessAutomationPoint> points;
  points.reserve(value->asArray().size());
  for (const auto& point : value->asArray()) {
    if (!point.isObject() || point.asObject().size() != 2U) {
      return core::failure<domain::BreathinessAutomation>(core::ErrorCode::ParseError,
          "Breathiness point requires tick and amount");
    }
    const auto* tick = point.find("tick");
    const auto amount = boundedNumber(point.find("amount"), 0.0,
                                      static_cast<double>(domain::kMaximumBreathiness));
    if (tick == nullptr || !tick->isInteger()) {
      return core::failure<domain::BreathinessAutomation>(core::ErrorCode::ParseError,
                                                          "Breathiness tick must be an integer");
    }
    if (!amount) return core::Result<domain::BreathinessAutomation>{amount.error()};
    points.push_back({.tick = time::Tick{tick->asInt64()},
                      .amount = static_cast<float>(amount.value())});
  }
  domain::BreathinessAutomation result;
  const auto validation = result.replacePoints(std::move(points));
  if (!validation) return core::Result<domain::BreathinessAutomation>{validation.error()};
  return result;
}

JsonValue encodeTension(const domain::TensionAutomation& tension) {
  JsonValue::Array points;
  points.reserve(tension.points().size());
  for (const auto& point : tension.points()) {
    points.emplace_back(JsonValue::Object{
        {"tick", JsonValue{point.tick.value()}},
        {"amount", JsonValue{point.amount}},
    });
  }
  return JsonValue{std::move(points)};
}

core::Result<domain::TensionAutomation> decodeTension(const JsonValue* value) {
  if (value == nullptr || !value->isArray() ||
      value->asArray().size() > domain::kMaximumTensionPoints) {
    return core::failure<domain::TensionAutomation>(core::ErrorCode::ParseError,
        "Schema 14 region requires bounded tensionAutomation points");
  }
  std::vector<domain::TensionAutomationPoint> points;
  points.reserve(value->asArray().size());
  for (const auto& point : value->asArray()) {
    if (!point.isObject() || point.asObject().size() != 2U) {
      return core::failure<domain::TensionAutomation>(core::ErrorCode::ParseError,
          "Tension point requires tick and amount");
    }
    const auto* tick = point.find("tick");
    const auto amount = boundedNumber(point.find("amount"), 0.0,
                                      static_cast<double>(domain::kMaximumTension));
    if (tick == nullptr || !tick->isInteger()) {
      return core::failure<domain::TensionAutomation>(core::ErrorCode::ParseError,
                                                      "Tension tick must be an integer");
    }
    if (!amount) return core::Result<domain::TensionAutomation>{amount.error()};
    points.push_back({.tick = time::Tick{tick->asInt64()},
                      .amount = static_cast<float>(amount.value())});
  }
  domain::TensionAutomation result;
  const auto validation = result.replacePoints(std::move(points));
  if (!validation) return core::Result<domain::TensionAutomation>{validation.error()};
  return result;
}

JsonValue encodeAiriness(const domain::AirinessAutomation& airiness) {
  JsonValue::Array points;
  points.reserve(airiness.points().size());
  for (const auto& point : airiness.points()) {
    points.emplace_back(JsonValue::Object{
        {"tick", JsonValue{point.tick.value()}},
        {"amount", JsonValue{point.amount}},
    });
  }
  return JsonValue{std::move(points)};
}

core::Result<domain::AirinessAutomation> decodeAiriness(const JsonValue* value) {
  if (value == nullptr || !value->isArray() ||
      value->asArray().size() > domain::kMaximumAirinessPoints) {
    return core::failure<domain::AirinessAutomation>(core::ErrorCode::ParseError,
        "Schema 15 region requires bounded airinessAutomation points");
  }
  std::vector<domain::AirinessAutomationPoint> points;
  points.reserve(value->asArray().size());
  for (const auto& point : value->asArray()) {
    if (!point.isObject() || point.asObject().size() != 2U) {
      return core::failure<domain::AirinessAutomation>(core::ErrorCode::ParseError,
          "Airiness point requires tick and amount");
    }
    const auto* tick = point.find("tick");
    const auto amount = boundedNumber(point.find("amount"), 0.0,
                                      static_cast<double>(domain::kMaximumAiriness));
    if (tick == nullptr || !tick->isInteger()) {
      return core::failure<domain::AirinessAutomation>(core::ErrorCode::ParseError,
                                                       "Airiness tick must be an integer");
    }
    if (!amount) return core::Result<domain::AirinessAutomation>{amount.error()};
    points.push_back({.tick = time::Tick{tick->asInt64()},
                      .amount = static_cast<float>(amount.value())});
  }
  domain::AirinessAutomation result;
  const auto validation = result.replacePoints(std::move(points));
  if (!validation) return core::Result<domain::AirinessAutomation>{validation.error()};
  return result;
}

JsonValue encodeGender(const domain::GenderAutomation& gender) {
  JsonValue::Array points;
  points.reserve(gender.points().size());
  for (const auto& point : gender.points()) {
    points.emplace_back(JsonValue::Object{
        {"tick", JsonValue{point.tick.value()}},
        {"amount", JsonValue{point.amount}},
    });
  }
  return JsonValue{std::move(points)};
}

core::Result<domain::GenderAutomation> decodeGender(const JsonValue* value) {
  if (value == nullptr || !value->isArray() ||
      value->asArray().size() > domain::kMaximumGenderPoints) {
    return core::failure<domain::GenderAutomation>(core::ErrorCode::ParseError,
        "Schema 16 region requires bounded genderAutomation points");
  }
  std::vector<domain::GenderAutomationPoint> points;
  points.reserve(value->asArray().size());
  for (const auto& point : value->asArray()) {
    if (!point.isObject() || point.asObject().size() != 2U) {
      return core::failure<domain::GenderAutomation>(core::ErrorCode::ParseError,
          "Gender point requires tick and amount");
    }
    const auto* tick = point.find("tick");
    const auto amount = boundedNumber(point.find("amount"),
                                      -static_cast<double>(domain::kMaximumGender),
                                      static_cast<double>(domain::kMaximumGender));
    if (tick == nullptr || !tick->isInteger()) {
      return core::failure<domain::GenderAutomation>(core::ErrorCode::ParseError,
                                                     "Gender tick must be an integer");
    }
    if (!amount) return core::Result<domain::GenderAutomation>{amount.error()};
    points.push_back({.tick = time::Tick{tick->asInt64()},
                      .amount = static_cast<float>(amount.value())});
  }
  domain::GenderAutomation result;
  const auto validation = result.replacePoints(std::move(points));
  if (!validation) return core::Result<domain::GenderAutomation>{validation.error()};
  return result;
}

JsonValue encodeGrowl(const domain::GrowlAutomation& growl) {
  JsonValue::Array points;
  points.reserve(growl.points().size());
  for (const auto& point : growl.points()) {
    points.emplace_back(JsonValue::Object{
        {"tick", JsonValue{point.tick.value()}},
        {"amount", JsonValue{point.amount}},
    });
  }
  return JsonValue{std::move(points)};
}

core::Result<domain::GrowlAutomation> decodeGrowl(const JsonValue* value) {
  if (value == nullptr || !value->isArray() ||
      value->asArray().size() > domain::kMaximumGrowlPoints) {
    return core::failure<domain::GrowlAutomation>(core::ErrorCode::ParseError,
        "Schema 17 region requires bounded growlAutomation points");
  }
  std::vector<domain::GrowlAutomationPoint> points;
  points.reserve(value->asArray().size());
  for (const auto& point : value->asArray()) {
    if (!point.isObject() || point.asObject().size() != 2U) {
      return core::failure<domain::GrowlAutomation>(core::ErrorCode::ParseError,
          "Growl point requires tick and amount");
    }
    const auto* tick = point.find("tick");
    const auto amount = boundedNumber(point.find("amount"), 0.0,
                                      static_cast<double>(domain::kMaximumGrowl));
    if (tick == nullptr || !tick->isInteger()) {
      return core::failure<domain::GrowlAutomation>(core::ErrorCode::ParseError,
                                                    "Growl tick must be an integer");
    }
    if (!amount) return core::Result<domain::GrowlAutomation>{amount.error()};
    points.push_back({.tick = time::Tick{tick->asInt64()},
                      .amount = static_cast<float>(amount.value())});
  }
  domain::GrowlAutomation result;
  const auto validation = result.replacePoints(std::move(points));
  if (!validation) return core::Result<domain::GrowlAutomation>{validation.error()};
  return result;
}

JsonValue encodeStyleSelection(const domain::VoiceStyleSelection& selection) {
  return JsonValue::Object{
      {"origin", JsonValue{std::string{styleOriginName(selection.origin)}}},
      {"styleId", JsonValue{selection.styleId}},
  };
}

core::Result<domain::VoiceStyleSelection> decodeStyleSelection(const JsonValue* value) {
  if (value == nullptr || !value->isObject() || value->asObject().size() != 2U) {
    return core::failure<domain::VoiceStyleSelection>(core::ErrorCode::ParseError,
        "Schema 8 track requires styleSelection origin and styleId");
  }
  const auto* origin = value->find("origin");
  const auto* styleId = value->find("styleId");
  if (origin == nullptr || styleId == nullptr || !origin->isString() ||
      !styleId->isString() || styleId->asString().size() > 1024U) {
    return core::failure<domain::VoiceStyleSelection>(core::ErrorCode::ParseError,
                                                    "Style selection fields are invalid");
  }
  constexpr std::array origins{
      domain::VoiceStyleOrigin::Unselected, domain::VoiceStyleOrigin::Explicit,
      domain::VoiceStyleOrigin::SoleDeclaredStyle,
      domain::VoiceStyleOrigin::LegacyNeedsExactBankResolution,
      domain::VoiceStyleOrigin::LegacyManifestFirst,
  };
  for (const auto candidate : origins) {
    if (origin->asString() != styleOriginName(candidate)) continue;
    domain::VoiceStyleSelection selection{.origin = candidate, .styleId = styleId->asString()};
    const auto validation = selection.validate();
    if (!validation) return core::Result<domain::VoiceStyleSelection>{validation.error()};
    return selection;
  }
  return core::failure<domain::VoiceStyleSelection>(core::ErrorCode::ParseError,
                                                  "Unknown voice style selection origin");
}

}
