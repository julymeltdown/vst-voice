#include "seam/authoring/audio_settings.hpp"

#include "seam/core/file_io.hpp"
#include "seam/formats/json_value.hpp"

namespace seam::authoring {

core::Result<AudioSettings> AudioSettingsStore::load() const {
  if (path_.empty()) {
    return core::failure<AudioSettings>(core::ErrorCode::InvalidArgument,
                                        "Audio settings path cannot be empty");
  }
  if (!std::filesystem::exists(path_)) return AudioSettings{};
  auto text = core::readTextFileLimited(path_, 64U * 1024U);
  if (!text) return core::Result<AudioSettings>{text.error()};
  auto parsed = formats::parseJson(text.value());
  if (!parsed || !parsed.value().isObject()) {
    return core::failure<AudioSettings>(core::ErrorCode::ParseError,
                                        "Audio settings JSON is invalid");
  }
  const auto* device = parsed.value().find("deviceId");
  const auto* rate = parsed.value().find("sampleRate");
  const auto* block = parsed.value().find("blockFrames");
  const auto* channels = parsed.value().find("outputChannels");
  const auto* revision = parsed.value().find("revision");
  if (device == nullptr || rate == nullptr || block == nullptr ||
      channels == nullptr || revision == nullptr || !device->isString() ||
      !rate->isNumber() || !block->isNumber() || !channels->isNumber() ||
      !revision->isNumber()) {
    return core::failure<AudioSettings>(core::ErrorCode::ParseError,
                                        "Audio settings fields are incomplete");
  }
  AudioSettings result{
      .deviceId = device->asString(),
      .sampleRate = static_cast<std::uint32_t>(rate->asInt64()),
      .blockFrames = static_cast<std::size_t>(block->asInt64()),
      .outputChannels = static_cast<std::uint8_t>(channels->asInt64()),
      .revision = static_cast<std::uint64_t>(revision->asInt64()),
  };
  if (result.sampleRate < 8000U || result.sampleRate > 384000U ||
      result.blockFrames == 0U || result.blockFrames > 16384U ||
      result.outputChannels == 0U || result.outputChannels > 8U ||
      result.revision == 0U) {
    return core::failure<AudioSettings>(core::ErrorCode::InvalidArgument,
                                        "Audio settings values are out of range");
  }
  return result;
}

core::Result<void> AudioSettingsStore::save(const AudioSettings& settings) const {
  if (path_.empty() || settings.sampleRate < 8000U ||
      settings.sampleRate > 384000U || settings.blockFrames == 0U ||
      settings.blockFrames > 16384U || settings.outputChannels == 0U ||
      settings.outputChannels > 8U || settings.revision == 0U) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Audio settings values are out of range");
  }
  const auto text = formats::stringifyJson(
      formats::JsonValue{formats::JsonValue::Object{
          {"deviceId", formats::JsonValue{settings.deviceId}},
          {"sampleRate", formats::JsonValue{static_cast<std::int64_t>(settings.sampleRate)}},
          {"blockFrames", formats::JsonValue{static_cast<std::int64_t>(settings.blockFrames)}},
          {"outputChannels", formats::JsonValue{static_cast<std::int64_t>(settings.outputChannels)}},
          {"revision", formats::JsonValue{static_cast<std::int64_t>(settings.revision)}},
      }},
      true);
  return core::durableAtomicWriteText(path_, text);
}

}
