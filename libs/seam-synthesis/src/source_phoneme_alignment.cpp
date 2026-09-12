#include "seam/synthesis/source_phoneme_alignment.hpp"
#include "seam/formats/json_value.hpp"
#include <algorithm>

namespace seam::synthesis {
core::Result<void> SourcePhonemeAlignment::validate(const voicebank::Unit& unit,
    std::string_view verifiedAudioSha256, time::SampleFrame decodedFrames) const {
  const auto validUnit = unit.validate();
  if (!validUnit) return validUnit;
  const auto hashValid = [](std::string_view hash) {
    return hash.size() == 64U && std::all_of(hash.begin(), hash.end(), [](char c) {
      return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
  };
  if (unitId.size() > 1024U || unitId != unit.id || !hashValid(audioSha256) || !hashValid(verifiedAudioSha256) || audioSha256 != verifiedAudioSha256) {
    return core::failure(core::ErrorCode::Conflict, "Source alignment does not match the unit and verified audio", unit.id);
  }
  if (decodedFrames <= 0 || unit.markers.audioEnd > decodedFrames || landmarks.empty() ||
      landmarks.size() > 256U || landmarks.size() != unit.phones.size()) {
    return core::failure(core::ErrorCode::InvalidArgument, "Source alignment coverage or decoded audio bounds are invalid", unit.id);
  }
  auto previous = unit.markers.audioOffset;
  for (std::size_t i = 0U; i < landmarks.size(); ++i) {
    const auto& landmark = landmarks[i];
    if (landmark.phone.size() > 1024U || landmark.phone != unit.phones[i] || landmark.frame < unit.markers.audioOffset ||
        landmark.frame >= unit.markers.audioEnd || (i > 0U && landmark.frame <= previous)) {
      return core::failure(core::ErrorCode::Conflict, "Source phone landmarks must match phone order and increase within audio bounds", unit.id);
    }
    previous = landmark.frame;
  }
  return core::success();
}

core::Result<std::string> encodeSourcePhonemeAlignment(const SourcePhonemeAlignment& alignment,
    const voicebank::Unit& unit, std::string_view verifiedAudioSha256, time::SampleFrame decodedFrames) {
  const auto valid = alignment.validate(unit, verifiedAudioSha256, decodedFrames);
  if (!valid) return core::Result<std::string>{valid.error()};
  using J = formats::JsonValue;
  J::Array landmarks;
  for (const auto& point : alignment.landmarks) landmarks.emplace_back(J::Object{{"phone", J{point.phone}}, {"frame", J{point.frame}}});
  auto json = formats::stringifyJson(J{J::Object{
      {"formatId", J{"com.project-seam.source-phoneme-alignment"}}, {"schemaVersion", J{std::int64_t{1}}},
      {"unitId", J{alignment.unitId}}, {"audioSha256", J{alignment.audioSha256}}, {"landmarks", J{std::move(landmarks)}}}});
  if (json.size() > 512U * 1024U) return core::failure<std::string>(core::ErrorCode::InvalidArgument, "Encoded source alignment exceeds bounds");
  return json;
}

core::Result<SourcePhonemeAlignment> decodeSourcePhonemeAlignment(std::string_view json,
    const voicebank::Unit& unit, std::string_view verifiedAudioSha256, time::SampleFrame decodedFrames) {
  const auto parsed = formats::parseJson(json, {.maximumInputBytes = 512U * 1024U, .maximumDepth = 4U,
      .maximumNodes = 2048U, .maximumStringBytes = 4096U, .maximumCollectionEntries = 256U});
  if (!parsed) return core::Result<SourcePhonemeAlignment>{parsed.error()};
  const auto& root = parsed.value();
  const auto* format = root.find("formatId");
  const auto* schema = root.find("schemaVersion");
  const auto* id = root.find("unitId");
  const auto* hash = root.find("audioSha256");
  const auto* points = root.find("landmarks");
  if (!root.isObject() || root.asObject().size() != 5U || !format || !format->isString() ||
      format->asString() != "com.project-seam.source-phoneme-alignment" || !schema || !schema->isInteger() || schema->asInt64() != 1 ||
      !id || !id->isString() || !hash || !hash->isString() || !points || !points->isArray()) {
    return core::failure<SourcePhonemeAlignment>(core::ErrorCode::ParseError, "Source alignment format or schema is invalid");
  }
  SourcePhonemeAlignment result{id->asString(), hash->asString(), {}};
  for (const auto& point : points->asArray()) {
    const auto* phone = point.find("phone");
    const auto* frame = point.find("frame");
    if (!point.isObject() || point.asObject().size() != 2U || !phone || !phone->isString() || !frame || !frame->isInteger()) {
      return core::failure<SourcePhonemeAlignment>(core::ErrorCode::ParseError, "Source alignment landmark requires a phone and integer frame");
    }
    result.landmarks.push_back({phone->asString(), frame->asInt64()});
  }
  const auto valid = result.validate(unit, verifiedAudioSha256, decodedFrames);
  if (!valid) return core::Result<SourcePhonemeAlignment>{valid.error()};
  return result;
}
}
